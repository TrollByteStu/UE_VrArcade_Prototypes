// SharkBrainComponent.cpp

#include "SharkBrainComponent.h"

#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/UnrealType.h"

USharkBrainComponent::USharkBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

// ============================================================================ Setup

void USharkBrainComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn)
	{
		UE_LOG(LogTemp, Error, TEXT("SharkBrain: owner %s is not a Pawn. Disabling."), *GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	if (ACharacter* Character = Cast<ACharacter>(OwnerPawn))
	{
		Character->bUseControllerRotationYaw = false;
		Character->bUseControllerRotationPitch = false;
		Character->bUseControllerRotationRoll = false;

		MoveComp = Character->GetCharacterMovement();
		if (MoveComp)
		{
			MoveComp->DefaultLandMovementMode = MOVE_Flying;
			MoveComp->SetMovementMode(MOVE_Flying);
			MoveComp->bOrientRotationToMovement = true;
			MoveComp->bUseControllerDesiredRotation = false;
			MoveComp->BrakingDecelerationFlying = BrakingDeceleration;
			// Lets it move even if nobody possesses it.
			MoveComp->bRunPhysicsWithNoController = true;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("SharkBrain: %s is not a Character. Speeds/turn rates won't be applied."), *GetNameSafe(OwnerPawn));
	}

	ResolveTarget();
	ChangeState(ESharkState::Patrol, /*bForce*/ true);
}

void USharkBrainComponent::ResolveTarget()
{
	if (!IsValid(Target) && bAutoTargetPlayer)
	{
		// Re-runs every tick while empty, so it recovers after a player restart/respawn.
		Target = UGameplayStatics::GetPlayerCharacter(this, 0);
	}
}

// ============================================================================ State

void USharkBrainComponent::SetState(ESharkState NewState)
{
	ChangeState(NewState);
}

void USharkBrainComponent::ChangeState(ESharkState NewState, bool bForce)
{
	if (NewState == CurrentState && !bForce)
	{
		return;
	}

	const ESharkState OldState = CurrentState;
	CurrentState = NewState;
	StateTime = 0.f;
	bBurstFired = false;

	switch (NewState)
	{
	case ESharkState::Patrol:
		ApplySpeed(PatrolSpeed);
		ApplyTurnRate(TurnRate);
		break;

	case ESharkState::Stalk:
	{
		ApplySpeed(StalkSpeed);
		ApplyTurnRate(TurnRate);
		CurrentStalkDuration = FMath::FRandRange(StalkDurationMin, FMath::Max(StalkDurationMin, StalkDurationMax));
		OrbitDirection = FMath::RandBool() ? 1.f : -1.f;

		// Start the orbit from wherever the shark already is, so it doesn't snap to a new side.
		const FVector Rel = GetOwner()->GetActorLocation() - GetTargetHeadLocation();
		OrbitAngle = FMath::RadiansToDegrees(FMath::Atan2(Rel.Y, Rel.X));
		break;
	}

	case ESharkState::Lunge:
		ApplySpeed(WindupSpeed); // hesitate first, burst happens in Tick
		ApplyTurnRate(LungeTurnRate);
		break;

	case ESharkState::Retreat:
		ApplySpeed(RetreatSpeed);
		ApplyTurnRate(TurnRate);
		break;
	}

	OnStateChanged.Broadcast(NewState, OldState);
}

void USharkBrainComponent::ApplySpeed(float Speed)
{
	if (MoveComp)
	{
		MoveComp->MaxFlySpeed = Speed;
	}
}

void USharkBrainComponent::ApplyTurnRate(float Rate)
{
	if (MoveComp)
	{
		MoveComp->RotationRate = FRotator(Rate, Rate, Rate);
	}
}

// ============================================================================ Queries

bool USharkBrainComponent::IsTargetInWater() const
{
	if (!IsValid(Target))
	{
		return false;
	}
	if (WaterFlagPropertyName.IsNone())
	{
		return true;
	}

	UClass* TargetClass = Target->GetClass();
	if (CachedWaterClass.Get() != TargetClass)
	{
		CachedWaterClass = TargetClass;
		CachedWaterProp = FindFProperty<FBoolProperty>(TargetClass, WaterFlagPropertyName);
	}

	if (!CachedWaterProp)
	{
		if (!bWarnedMissingWaterProp)
		{
			bWarnedMissingWaterProp = true;
			UE_LOG(LogTemp, Warning, TEXT("SharkBrain: no bool '%s' on %s. Treating target as always in water."),
				*WaterFlagPropertyName.ToString(), *GetNameSafe(TargetClass));
		}
		return true;
	}

	return CachedWaterProp->GetPropertyValue_InContainer(Target.Get());
}

FVector USharkBrainComponent::GetTargetHeadLocation() const
{
	if (!IsValid(Target))
	{
		return GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	}

	if (CachedCameraOwner.Get() != Target.Get())
	{
		CachedCameraOwner = Target.Get();
		CachedCamera = Target->FindComponentByClass<UCameraComponent>();
	}

	if (const UCameraComponent* Cam = CachedCamera.Get())
	{
		return Cam->GetComponentLocation(); // the HMD in VR
	}
	return Target->GetActorLocation();
}

FVector USharkBrainComponent::GetMouthLocation() const
{
	if (MouthComponent)
	{
		return MouthComponent->GetComponentLocation();
	}
	const AActor* Owner = GetOwner();
	return Owner->GetActorLocation() + Owner->GetActorForwardVector() * MouthForwardOffset;
}

bool USharkBrainComponent::CanHunt(const FVector& SharkLocation) const
{
	if (!IsValid(Target) || !IsTargetInWater())
	{
		return false;
	}
	// Hysteresis: easier to keep hunting than to start.
	const float Radius = (CurrentState == ESharkState::Patrol) ? DetectRadius : LoseRadius;
	return FVector::DistSquared(SharkLocation, Target->GetActorLocation()) <= FMath::Square(Radius);
}

// ============================================================================ Tick

void USharkBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerPawn)
	{
		return;
	}

	StateTime += DeltaTime;
	ResolveTarget();

	const FVector SharkLoc = OwnerPawn->GetActorLocation();
	const bool bCanHunt = CanHunt(SharkLoc);
	FVector MoveTarget = SharkLoc; // default: hold position
	bool bClampToSurface = true;

	switch (CurrentState)
	{
		// ------------------------------------------------------------------ Patrol
	case ESharkState::Patrol:
	{
		if (PatrolTargetComponent)
		{
			MoveTarget = PatrolTargetComponent->GetComponentLocation();
		}
		if (bCanHunt)
		{
			ChangeState(ESharkState::Stalk);
		}
		break;
	}

	// ------------------------------------------------------------------ Stalk
	case ESharkState::Stalk:
	{
		if (!bCanHunt)
		{
			ChangeState(ESharkState::Patrol);
			break;
		}

		const FVector Head = GetTargetHeadLocation();
		OrbitAngle = FMath::UnwindDegrees(OrbitAngle + OrbitDegreesPerSecond * OrbitDirection * DeltaTime);

		const FVector Offset = FRotator(0.f, OrbitAngle, 0.f).RotateVector(FVector(StalkRadius, 0.f, 0.f));
		MoveTarget = Head + Offset;
		MoveTarget.Z = Head.Z - StalkHeightBelowHead;

		if (StateTime >= CurrentStalkDuration)
		{
			ChangeState(ESharkState::Lunge);
		}
		break;
	}

	// ------------------------------------------------------------------ Lunge
	case ESharkState::Lunge:
	{
		bClampToSurface = false; // allow a breach at your face

		if (!IsValid(Target) || !IsTargetInWater())
		{
			ChangeState(ESharkState::Retreat);
			break;
		}

		const FVector Head = GetTargetHeadLocation();
		MoveTarget = Head;

		if (!bBurstFired && StateTime >= WindupTime)
		{
			bBurstFired = true;
			ApplySpeed(LungeSpeed);
			OnLungeBurst.Broadcast();
		}

		if (FVector::DistSquared(GetMouthLocation(), Head) <= FMath::Square(BiteRadius))
		{
			if (MoveComp)
			{
				MoveComp->Velocity *= PostBiteVelocityScale;
			}
			OnBite.Broadcast(Target.Get());
			ChangeState(ESharkState::Retreat);
			break;
		}

		if (StateTime >= WindupTime + LungeMaxTime)
		{
			ChangeState(ESharkState::Retreat); // missed
		}
		break;
	}

	// ------------------------------------------------------------------ Retreat
	case ESharkState::Retreat:
	{
		FVector Away = IsValid(Target)
			? SharkLoc - GetTargetHeadLocation()
			: OwnerPawn->GetActorForwardVector();
		Away.Z *= 0.2f;             // mostly horizontal
		Away = Away.GetSafeNormal();
		Away.Z -= 0.3f;             // and a bit downward (dive after a breach)
		MoveTarget = SharkLoc + Away.GetSafeNormal() * RetreatDistance;

		if (StateTime >= RetreatTime)
		{
			ChangeState(bCanHunt ? ESharkState::Stalk : ESharkState::Patrol);
		}
		break;
	}
	}

	if (bUseWaterSurface && bClampToSurface)
	{
		MoveTarget.Z = FMath::Min(MoveTarget.Z, WaterSurfaceZ - SurfaceMargin);
	}

	// ------------------------------------------------------------------ Steer
	const FVector ToTarget = MoveTarget - SharkLoc;
	if (ToTarget.SizeSquared() > FMath::Square(25.f))
	{
		OwnerPawn->AddMovementInput(ToTarget.GetSafeNormal(), 1.f);
	}

	// ------------------------------------------------------------------ Debug
	if (bDrawDebug)
	{
		FColor Color = FColor::Green;
		switch (CurrentState)
		{
		case ESharkState::Stalk:   Color = FColor::Yellow; break;
		case ESharkState::Lunge:   Color = FColor::Red;    break;
		case ESharkState::Retreat: Color = FColor::Blue;   break;
		default: break;
		}
		const UWorld* World = GetWorld();
		DrawDebugLine(World, SharkLoc, MoveTarget, Color, false, 0.f, 0, 2.f);
		DrawDebugSphere(World, MoveTarget, 30.f, 8, Color, false, 0.f);
		DrawDebugSphere(World, GetMouthLocation(), BiteRadius, 12, FColor::Red, false, 0.f);
		DrawDebugString(World, SharkLoc + FVector(0, 0, 120), UEnum::GetValueAsString(CurrentState), nullptr, Color, 0.f);
	}
}