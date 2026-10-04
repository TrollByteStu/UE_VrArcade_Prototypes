// IncineratorBrainComponent.cpp

#include "IncineratorBrainComponent.h"

#include "AIController.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SpotLightComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogIncineratorBrain, Log, All);

namespace IncineratorBrain
{
	// Finds a component by its BP variable name, falls back to the first of that class.
	template <typename T>
	T* FindComponentNamed(AActor* Owner, FName Name)
	{
		if (!Owner || Name.IsNone())
		{
			return nullptr;
		}

		TInlineComponentArray<T*> Components;
		Owner->GetComponents(Components);
		for (T* Comp : Components)
		{
			if (Comp && Comp->GetFName() == Name)
			{
				return Comp;
			}
		}

		if (Components.Num() > 0)
		{
			UE_LOG(LogIncineratorBrain, Warning, TEXT("%s: no %s named '%s', using '%s'."),
				*Owner->GetName(), *T::StaticClass()->GetName(), *Name.ToString(), *Components[0]->GetName());
			return Components[0];
		}
		return nullptr;
	}

	FORCEINLINE FVector Flat(const FVector& V) { return FVector(V.X, V.Y, 0.f); }

	constexpr float FocusDistance = 1000.f;
}

using namespace IncineratorBrain;

UIncineratorBrainComponent::UIncineratorBrainComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

// ==================================================================
// Lifecycle
// ==================================================================

void UIncineratorBrainComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogIncineratorBrain, Error, TEXT("%s: IncineratorBrainComponent needs a Character owner."), *GetNameSafe(GetOwner()));
		SetComponentTickEnabled(false);
		return;
	}

	MoveComp = OwnerCharacter->GetCharacterMovement();

	if (!OwnerCharacter->GetController())
	{
		OwnerCharacter->SpawnDefaultController();
	}
	if (!GetAI())
	{
		UE_LOG(LogIncineratorBrain, Warning, TEXT("%s: no AIController. Set AI Controller Class + Auto Possess AI on the BP."), *OwnerCharacter->GetName());
	}

	if (bConfigureRotation && MoveComp)
	{
		OwnerCharacter->bUseControllerRotationYaw = false;
		MoveComp->bUseControllerDesiredRotation = true;
		MoveComp->bOrientRotationToMovement = false;
		MoveComp->RotationRate = FRotator(0.f, TurnRate, 0.f);
	}

	if (!EyeLight)
	{
		EyeLight = FindComponentNamed<USpotLightComponent>(OwnerCharacter, EyeLightComponentName);
	}
	if (!EngineAudio)
	{
		EngineAudio = FindComponentNamed<UAudioComponent>(OwnerCharacter, EngineAudioComponentName);
	}
	if (EyeLight)
	{
		CurrentLightColor = EyeLight->GetLightColor();
	}

	if (StopDistance >= AttackRange)
	{
		UE_LOG(LogIncineratorBrain, Warning, TEXT("%s: StopDistance (%.0f) >= AttackRange (%.0f). It may stall just out of reach."),
			*OwnerCharacter->GetName(), StopDistance, AttackRange);
	}

	OwnerCharacter->OnTakeAnyDamage.AddDynamic(this, &UIncineratorBrainComponent::HandleOwnerDamaged);

	SetHomeToCurrentTransform();

	bHostile = bStartHostile;
	Anger = bStartHostile ? 1.f : 0.f;

	// Stagger traces when several Incinerators exist.
	PerceptionAccumulator = FMath::FRand() * PerceptionInterval;

	// Start the engine loop silent; UpdatePresentation fades it in once it moves.
	// (Otherwise it plays at the BP's default volume until the first movement.)
	if (EngineAudio)
	{
		CurrentEngineVolume = 0.f;
		CurrentEnginePitch = EnginePitchMin;
		EngineAudio->SetVolumeMultiplier(CurrentEngineVolume);
		EngineAudio->SetPitchMultiplier(CurrentEnginePitch);
	}

	// Respect SetAIEnabled(false) if it was called before BeginPlay.
	ChangeState(CurrentState == EIncineratorState::Disabled ? EIncineratorState::Disabled : EIncineratorState::Idle, true);
}

void UIncineratorBrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (OwnerCharacter)
	{
		OwnerCharacter->OnTakeAnyDamage.RemoveDynamic(this, &UIncineratorBrainComponent::HandleOwnerDamaged);
	}
	Super::EndPlay(EndPlayReason);
}

void UIncineratorBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!OwnerCharacter)
	{
		return;
	}

	StateTime += DeltaTime;

	ResolveTarget();

	DistanceToTarget = IsValid(Target)
		? FVector::Dist(OwnerCharacter->GetActorLocation(), Target->GetActorLocation())
		: TNumericLimits<float>::Max();

	if (CurrentState != EIncineratorState::Disabled)
	{
		PerceptionAccumulator += DeltaTime;
		if (PerceptionAccumulator >= PerceptionInterval)
		{
			PerceptionAccumulator = 0.f;
			UpdatePerception();
		}

		UpdateAnger(DeltaTime);
		UpdateState(DeltaTime);
	}

	UpdatePresentation(DeltaTime);

#if ENABLE_DRAW_DEBUG
	if (bDrawDebug)
	{
		DrawDebug();
	}
#endif
}

// ==================================================================
// Senses & mood
// ==================================================================

void UIncineratorBrainComponent::ResolveTarget()
{
	if (!bAutoTargetPlayer)
	{
		if (!IsValid(Target))
		{
			Target = nullptr;
		}
		return;
	}

	// Follows respawns / pawn swaps automatically.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (PlayerPawn && PlayerPawn != Target.Get())
	{
		Target = PlayerPawn;
	}
	else if (!IsValid(Target))
	{
		Target = nullptr;
	}
}

void UIncineratorBrainComponent::UpdatePerception()
{
	bCanSeeTarget = false;

	if (!IsValid(Target))
	{
		return;
	}

	const FVector Eye = GetEyeLocation();
	const FVector Head = GetTargetHeadLocation();
	const FVector Body = Target->GetActorLocation();

	const float Dist = FVector::Dist(Eye, Head);
	if (Dist > SightRange)
	{
		return;
	}

	// Cone check, skipped up close.
	if (Dist > CloseSenseRadius)
	{
		const FVector Forward2D = Flat(OwnerCharacter->GetActorForwardVector()).GetSafeNormal();
		const FVector ToHead2D = Flat(Head - OwnerCharacter->GetActorLocation()).GetSafeNormal();
		if (!ToHead2D.IsNearlyZero() && FVector::DotProduct(Forward2D, ToHead2D) < FMath::Cos(FMath::DegreesToRadians(SightHalfAngle)))
		{
			return;
		}
	}

	// Line of sight. Ignore the monkey and whatever it's holding.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(IncineratorSight), false, OwnerCharacter);
	Params.AddIgnoredActor(Target.Get());
	TArray<AActor*> Attached;
	Target->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);

	UWorld* World = GetWorld();
	FHitResult Hit;
	const bool bHeadBlocked = World->LineTraceSingleByChannel(Hit, Eye, Head, SightTraceChannel, Params);
	const bool bBodyBlocked = bHeadBlocked && World->LineTraceSingleByChannel(Hit, Eye, Body, SightTraceChannel, Params);

	if (bHeadBlocked && bBodyBlocked)
	{
		return;
	}

	bCanSeeTarget = true;
	bHasLastKnown = true;
	LastKnownLocation = Body;
	LastSeenTime = World->GetTimeSeconds();
}

void UIncineratorBrainComponent::UpdateAnger(float DeltaTime)
{
	// Holds the grudge while it can see you.
	if (!bCanSeeTarget || !bHostile)
	{
		Anger = FMath::Max(0.f, Anger - AngerDecayPerSecond * DeltaTime);
	}

	if (bHostile && AngerDecayPerSecond > 0.f && Anger <= CalmThreshold)
	{
		SetHostileInternal(false);
	}
}

void UIncineratorBrainComponent::SetHostile(bool bNewHostile)
{
	Anger = bNewHostile ? 1.f : 0.f;
	SetHostileInternal(bNewHostile);
}

void UIncineratorBrainComponent::SetHostileInternal(bool bNewHostile)
{
	if (bHostile == bNewHostile)
	{
		return;
	}
	bHostile = bNewHostile;
	OnHostilityChanged.Broadcast(bHostile);

	if (!bHostile && IsCombatState(CurrentState))
	{
		ChangeState(EIncineratorState::Return);
	}
}

void UIncineratorBrainComponent::ReportProvocation(float AngerAmount, AActor* Instigator)
{
	Anger = FMath::Clamp(Anger + AngerAmount, 0.f, 1.f);

	// "You hit my turret from behind - I know where you are."
	if (IsValid(Instigator))
	{
		LastKnownLocation = Instigator->GetActorLocation();
		bHasLastKnown = true;
	}

	if (!bHostile && Anger >= HostileThreshold)
	{
		SetHostileInternal(true);
	}

	if (bHostile && (CurrentState == EIncineratorState::Idle || CurrentState == EIncineratorState::Return))
	{
		ChangeState(bCanSeeTarget ? EIncineratorState::Alert : EIncineratorState::Search);
	}
	else if (bHostile && CurrentState == EIncineratorState::Search && !bCanSeeTarget && IsValid(Instigator))
	{
		// Already searching but got hit from somewhere new: restart the search toward the new position.
		ChangeState(EIncineratorState::Search, /*bForce*/ true);
	}
}

void UIncineratorBrainComponent::HandleOwnerDamaged(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	if (!bProvokeOnDamage || Damage <= 0.f)
	{
		return;
	}
	AActor* Instigator = InstigatedBy && InstigatedBy->GetPawn() ? static_cast<AActor*>(InstigatedBy->GetPawn()) : DamageCauser;
	ReportProvocation(Damage * AngerPerDamagePoint, Instigator);
}

float UIncineratorBrainComponent::GetTimeSinceSeen() const
{
	return GetWorld()->GetTimeSeconds() - LastSeenTime;
}

float UIncineratorBrainComponent::GetEnrageAlpha() const
{
	if (!bHostile)
	{
		return 0.f;
	}
	const float Span = FMath::Max(1.f - HostileThreshold, KINDA_SMALL_NUMBER);
	return FMath::Clamp((Anger - HostileThreshold) / Span, 0.f, 1.f);
}

bool UIncineratorBrainComponent::IsCombatState(EIncineratorState State) const
{
	switch (State)
	{
	case EIncineratorState::Alert:
	case EIncineratorState::Chase:
	case EIncineratorState::Windup:
	case EIncineratorState::Recover:
	case EIncineratorState::Search:
		return true;
	default:
		return false;
	}
}

// ==================================================================
// State machine
// ==================================================================

void UIncineratorBrainComponent::ChangeState(EIncineratorState NewState, bool bForce)
{
	// SetAIEnabled / ReportProvocation / SetHostile are BlueprintCallable and can arrive before BeginPlay
	// (or on a non-Character owner). EnterState dereferences OwnerCharacter, so bail out instead of crashing.
	if (!OwnerCharacter)
	{
		CurrentState = NewState;
		return;
	}

	if (NewState == CurrentState && !bForce)
	{
		return;
	}

	const EIncineratorState OldState = CurrentState;
	ExitState(OldState);

	CurrentState = NewState;
	StateTime = 0.f;

	EnterState(NewState, OldState);
	OnStateChanged.Broadcast(NewState, OldState);
}

void UIncineratorBrainComponent::ExitState(EIncineratorState OldState)
{
	if (OldState == EIncineratorState::Windup && !bHitResolved && AttackMontage && OwnerCharacter)
	{
		OwnerCharacter->StopAnimMontage(AttackMontage); // interrupted swing
	}
}

void UIncineratorBrainComponent::EnterState(EIncineratorState NewState, EIncineratorState OldState)
{
	UWorld* World = GetWorld();
	const float Now = World->GetTimeSeconds();

	switch (NewState)
	{
	case EIncineratorState::Idle:
		StopMove();
		ApplySpeed(WalkSpeed);
		FocusHome();
		break;

	case EIncineratorState::Alert:
	{
		StopMove();
		FocusTarget();
		const bool bRecentFight = (Now - LastCombatTime) <= CombatMemoryTime;
		CurrentAlertTime = bRecentFight ? ReacquireAlertTime : AlertTime;
		if (OldState == EIncineratorState::Idle || OldState == EIncineratorState::Return || OldState == EIncineratorState::Search)
		{
			OnTargetSpotted.Broadcast(Target);
		}
		break;
	}

	case EIncineratorState::Chase:
		StuckTime = 0.f;
		bUnreachableNotified = false;
		bMoveActive = false; // forces a fresh MoveTo on the first chase tick
		break;

	case EIncineratorState::Windup:
	{
		StopMove();
		bHitResolved = false;
		bRotationLocked = false;
		CurrentWindupTime = WindupTime * FMath::Lerp(1.f, EnragedWindupMultiplier, GetEnrageAlpha());
		CurrentMontageLength = 0.f;

		if (AttackMontage)
		{
			// Speed the animation up to match an enraged (shorter) windup.
			const float Rate = (WindupTime > 0.f && CurrentWindupTime > 0.f) ? WindupTime / CurrentWindupTime : 1.f;
			CurrentMontageLength = OwnerCharacter->PlayAnimMontage(AttackMontage, Rate);
		}

		OnAttackStarted.Broadcast();
		break;
	}

	case EIncineratorState::Recover:
		FocusTarget();
		break;

	case EIncineratorState::Search:
	{
		ApplySpeed(SearchSpeed);
		ClearAIFocus();
		bSearchArrived = false;
		OnTargetLost.Broadcast(Target);

		AAIController* AI = GetAI();
		const bool bValidLastKnown = bHasLastKnown && IsInsideLeash(LastKnownLocation, LeashBuffer);
		if (AI && bValidLastKnown)
		{
			// Project to navmesh: last known position may be up on a crane.
			AI->MoveToLocation(LastKnownLocation, SearchAcceptRadius, false, true, true, true, nullptr, true);
			bMoveActive = true;
			LastMoveRequestTime = Now;
		}
		else
		{
			// Nowhere sensible to go: look around on the spot.
			StopMove();
			bSearchArrived = true;
			SearchArriveTime = 0.f;
			SearchBaseYaw = OwnerCharacter->GetActorRotation().Yaw;
		}
		break;
	}

	case EIncineratorState::Return:
		ApplySpeed(WalkSpeed);
		ClearAIFocus();
		if (AAIController* AI = GetAI())
		{
			AI->MoveToLocation(HomeLocation, HomeAcceptRadius * 0.5f, false, true, true, true, nullptr, true);
			bMoveActive = true;
			LastMoveRequestTime = Now;
		}
		break;

	case EIncineratorState::Disabled:
		StopMove();
		ClearAIFocus();
		if (AttackMontage)
		{
			OwnerCharacter->StopAnimMontage(AttackMontage);
		}
		break;
	}
}

void UIncineratorBrainComponent::UpdateState(float DeltaTime)
{
	// Safety net: calm robots don't fight.
	if (!bHostile && IsCombatState(CurrentState))
	{
		ChangeState(EIncineratorState::Return);
		return;
	}

	switch (CurrentState)
	{
	case EIncineratorState::Idle:		TickIdle();				break;
	case EIncineratorState::Alert:		TickAlert();			break;
	case EIncineratorState::Chase:		TickChase(DeltaTime);	break;
	case EIncineratorState::Windup:		TickWindup();			break;
	case EIncineratorState::Recover:	TickRecover();			break;
	case EIncineratorState::Search:		TickSearch();			break;
	case EIncineratorState::Return:		TickReturn();			break;
	default:													break;
	}
}

void UIncineratorBrainComponent::TickIdle()
{
	if (!IsValid(Target) || !bCanSeeTarget)
	{
		FocusHome();
		return;
	}

	if (bHostile)
	{
		if (IsInsideLeash(Target->GetActorLocation()))
		{
			ChangeState(EIncineratorState::Alert);
		}
		else
		{
			FocusTarget(); // you're outside its turf: it just stares you down
		}
		return;
	}

	// Friendly: keeps an eye on the good monkey.
	FocusTarget();
}

void UIncineratorBrainComponent::TickAlert()
{
	FocusTarget();

	if (GetTimeSinceSeen() > LoseSightGrace)
	{
		ChangeState(EIncineratorState::Search);
		return;
	}
	if (StateTime >= CurrentAlertTime)
	{
		ChangeState(EIncineratorState::Chase);
	}
}

void UIncineratorBrainComponent::TickChase(float DeltaTime)
{
	const float Now = GetWorld()->GetTimeSeconds();
	LastCombatTime = Now;

	if (!IsValid(Target))
	{
		ChangeState(EIncineratorState::Return);
		return;
	}

	// Leash: it won't follow you across the island.
	if (!IsInsideLeash(OwnerCharacter->GetActorLocation()) || !IsInsideLeash(Target->GetActorLocation(), LeashBuffer))
	{
		ChangeState(EIncineratorState::Return);
		return;
	}

	if (GetTimeSinceSeen() > LoseSightGrace)
	{
		ChangeState(EIncineratorState::Search);
		return;
	}

	ApplySpeed(ChaseSpeed * FMath::Lerp(1.f, EnragedSpeedMultiplier, GetEnrageAlpha()));
	FocusTarget();

	// In reach: plant feet, turn, swing once facing.
	if (IsTargetInAttackReach())
	{
		if (bMoveActive)
		{
			StopMove();
		}
		StuckTime = 0.f;

		if (GetFacingDotToTarget() >= AttackFacingDot)
		{
			ChangeState(EIncineratorState::Windup);
		}
		return;
	}

	// Keep pathing (partial paths allowed, so it gets as close as possible).
	AAIController* AI = GetAI();
	if (AI)
	{
		const bool bPathIdle = AI->GetMoveStatus() == EPathFollowingStatus::Idle;
		if (!bMoveActive || (bPathIdle && Now - LastMoveRequestTime >= RepathInterval))
		{
			AI->MoveToActor(Target, StopDistance, false, true, true, nullptr, true);
			bMoveActive = true;
			LastMoveRequestTime = Now;
		}
	}

	// Unreachable detection: chasing but not actually moving.
	if (OwnerCharacter->GetVelocity().Size2D() < StuckSpeedThreshold)
	{
		StuckTime += DeltaTime;
	}
	else
	{
		StuckTime = 0.f;
		bUnreachableNotified = false;
	}

	if (!bUnreachableNotified && StuckTime >= UnreachableNotifyTime)
	{
		bUnreachableNotified = true;
		OnTargetUnreachable.Broadcast(Target); // monkey's up a crane: stomp, roar, glare
	}

	if (StuckTime >= UnreachableGiveUpTime)
	{
		ChangeState(EIncineratorState::Return);
	}
}

void UIncineratorBrainComponent::TickWindup()
{
	LastCombatTime = GetWorld()->GetTimeSeconds();

	// Phase 1: track (with lead). Phase 2: commit, so the swing is dodgeable.
	const float TrackTime = CurrentWindupTime * WindupTrackFraction;
	if (StateTime < TrackTime)
	{
		if (IsValid(Target))
		{
			FocusPoint(Target->GetActorLocation() + Target->GetVelocity() * AimLeadTime);
		}
	}
	else if (!bRotationLocked)
	{
		bRotationLocked = true;
		FocusPoint(OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorForwardVector() * FocusDistance);
	}

	// With anim notifies this is only a fallback in case the notify never fires.
	const float ResolveAt = bUseAnimNotifyForHit
		? FMath::Max(CurrentWindupTime, CurrentMontageLength) + 0.1f
		: CurrentWindupTime;

	if (StateTime >= ResolveAt)
	{
		ResolveAttackHit();
	}
}

void UIncineratorBrainComponent::ResolveAttackHit()
{
	if (CurrentState != EIncineratorState::Windup || bHitResolved || !OwnerCharacter)
	{
		return;
	}
	bHitResolved = true;

	// Hit if either the VR head or the body is in the zone (ducking only half works).
	const bool bHit = IsValid(Target)
		&& (IsPointInHitZone(GetTargetHeadLocation()) || IsPointInHitZone(Target->GetActorLocation()));

	const float BaseCooldown = FMath::FRandRange(CooldownMin, FMath::Max(CooldownMin, CooldownMax));

	if (bHit)
	{
		ConsecutiveMisses = 0;

		FVector Dir = Flat(Target->GetActorLocation() - OwnerCharacter->GetActorLocation()).GetSafeNormal();
		if (Dir.IsNearlyZero())
		{
			Dir = Flat(OwnerCharacter->GetActorForwardVector()).GetSafeNormal();
		}
		const FVector Launch = Dir * KnockbackStrength + FVector(0.f, 0.f, KnockbackUp);

		OnAttackHit.Broadcast(Target, Launch); // BP: HitPointsWounded

		if (bApplyKnockback && IsValid(Target))
		{
			if (ACharacter* Victim = Cast<ACharacter>(Target))
			{
				Victim->LaunchCharacter(Launch, true, true);
			}
		}

		CurrentCooldown = BaseCooldown + PostHitExtraCooldown;
	}
	else
	{
		++ConsecutiveMisses;
		Anger = FMath::Min(1.f, Anger + MissAngerGain); // missing makes it madder
		OnAttackMissed.Broadcast();
		CurrentCooldown = BaseCooldown;
	}

	ChangeState(EIncineratorState::Recover);
}

void UIncineratorBrainComponent::TickRecover()
{
	LastCombatTime = GetWorld()->GetTimeSeconds();
	FocusTarget();

	if (StateTime < CurrentCooldown)
	{
		return;
	}

	ChangeState(GetTimeSinceSeen() <= LoseSightGrace ? EIncineratorState::Chase : EIncineratorState::Search);
}

void UIncineratorBrainComponent::TickSearch()
{
	if (bCanSeeTarget && IsValid(Target) && IsInsideLeash(Target->GetActorLocation(), LeashBuffer))
	{
		ChangeState(EIncineratorState::Alert); // short alert, it remembers the fight
		return;
	}

	if (StateTime >= SearchMaxTime)
	{
		ChangeState(EIncineratorState::Return);
		return;
	}

	if (!bSearchArrived)
	{
		AAIController* AI = GetAI();
		// Small delay so the fresh MoveTo has a chance to report "Moving".
		if (!AI || (StateTime > 0.25f && AI->GetMoveStatus() == EPathFollowingStatus::Idle))
		{
			bSearchArrived = true;
			SearchArriveTime = StateTime;
			SearchBaseYaw = OwnerCharacter->GetActorRotation().Yaw;
			bMoveActive = false;
		}
		return;
	}

	// Look left/right around where it lost you.
	const float LookT = StateTime - SearchArriveTime;
	if (LookT >= SearchLookTime)
	{
		ChangeState(EIncineratorState::Return);
		return;
	}

	const float Yaw = SearchBaseYaw + FMath::Sin(LookT * SearchLookSpeed) * SearchLookAngle;
	FocusPoint(OwnerCharacter->GetActorLocation() + FRotator(0.f, Yaw, 0.f).Vector() * FocusDistance);
}

void UIncineratorBrainComponent::TickReturn()
{
	const FVector OwnerLoc = OwnerCharacter->GetActorLocation();

	// Re-spots a hostile monkey on its turf on the way home.
	// It must be back inside its own leash (with some margin) first. Otherwise, if it walked past the leash edge
	// while the monkey stands just inside it, it loops Return -> Alert -> Chase -> Return every few frames
	// (and spams OnTargetSpotted).
	const float ReengageMargin = FMath::Min(LeashBuffer, LeashRadius * 0.5f);
	if (bHostile && bCanSeeTarget && IsValid(Target)
		&& IsInsideLeash(Target->GetActorLocation())
		&& IsInsideLeash(OwnerLoc, -ReengageMargin))
	{
		ChangeState(EIncineratorState::Alert);
		return;
	}

	if (FVector::Dist2D(OwnerLoc, HomeLocation) <= HomeAcceptRadius)
	{
		ChangeState(EIncineratorState::Idle);
		return;
	}

	AAIController* AI = GetAI();
	const float Now = GetWorld()->GetTimeSeconds();
	if (AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle && Now - LastMoveRequestTime >= RepathInterval)
	{
		AI->MoveToLocation(HomeLocation, HomeAcceptRadius * 0.5f, false, true, true, true, nullptr, true);
		bMoveActive = true;
		LastMoveRequestTime = Now;
	}
}

void UIncineratorBrainComponent::SetAIEnabled(bool bEnabled)
{
	if (!bEnabled)
	{
		ChangeState(EIncineratorState::Disabled);
	}
	else if (CurrentState == EIncineratorState::Disabled)
	{
		ChangeState(EIncineratorState::Return);
	}
}

void UIncineratorBrainComponent::SetHomeToCurrentTransform()
{
	if (AActor* Owner = GetOwner())
	{
		HomeLocation = Owner->GetActorLocation();
		HomeForward = Flat(Owner->GetActorForwardVector()).GetSafeNormal();
		if (HomeForward.IsNearlyZero())
		{
			HomeForward = FVector::ForwardVector;
		}
	}
}

// ==================================================================
// Geometry helpers
// ==================================================================

FVector UIncineratorBrainComponent::GetTargetHeadLocation() const
{
	if (!IsValid(Target))
	{
		return FVector::ZeroVector;
	}

	if (CachedCameraOwner.Get() != Target.Get())
	{
		CachedCameraOwner = Target.Get();
		CachedCamera = Target->FindComponentByClass<UCameraComponent>();
	}

	if (const UCameraComponent* Cam = CachedCamera.Get())
	{
		return Cam->GetComponentLocation(); // VR HMD
	}
	if (const APawn* Pawn = Cast<APawn>(Target))
	{
		return Pawn->GetPawnViewLocation();
	}
	return Target->GetActorLocation();
}

FVector UIncineratorBrainComponent::GetEyeLocation() const
{
	if (EyeLight)
	{
		return EyeLight->GetComponentLocation();
	}
	return OwnerCharacter ? OwnerCharacter->GetPawnViewLocation() : FVector::ZeroVector;
}

FVector UIncineratorBrainComponent::GetHitOrigin() const
{
	if (HitOriginComponent)
	{
		return HitOriginComponent->GetComponentLocation();
	}
	return OwnerCharacter->GetActorLocation() + OwnerCharacter->GetActorForwardVector() * HitForwardOffset;
}

bool UIncineratorBrainComponent::IsInsideLeash(const FVector& Point, float Extra) const
{
	return FVector::Dist2D(Point, HomeLocation) <= LeashRadius + Extra;
}

bool UIncineratorBrainComponent::IsTargetInAttackReach() const
{
	if (!IsValid(Target))
	{
		return false;
	}
	const FVector Origin = GetHitOrigin();
	return FVector::Dist(Origin, GetTargetHeadLocation()) <= AttackRange
		|| FVector::Dist(Origin, Target->GetActorLocation()) <= AttackRange;
}

bool UIncineratorBrainComponent::IsPointInHitZone(const FVector& Point) const
{
	if (FVector::Dist(GetHitOrigin(), Point) > HitRange)
	{
		return false;
	}

	const FVector ToPoint2D = Flat(Point - OwnerCharacter->GetActorLocation()).GetSafeNormal();
	if (ToPoint2D.IsNearlyZero())
	{
		return true; // standing right on top of it
	}
	const FVector Forward2D = Flat(OwnerCharacter->GetActorForwardVector()).GetSafeNormal();
	return FVector::DotProduct(Forward2D, ToPoint2D) >= HitAngleDot;
}

float UIncineratorBrainComponent::GetFacingDotToTarget() const
{
	if (!IsValid(Target))
	{
		return -1.f;
	}
	const FVector ToTarget2D = Flat(Target->GetActorLocation() - OwnerCharacter->GetActorLocation()).GetSafeNormal();
	if (ToTarget2D.IsNearlyZero())
	{
		return 1.f;
	}
	return FVector::DotProduct(Flat(OwnerCharacter->GetActorForwardVector()).GetSafeNormal(), ToTarget2D);
}

// ==================================================================
// Controller helpers
// ==================================================================

AAIController* UIncineratorBrainComponent::GetAI() const
{
	return OwnerCharacter ? Cast<AAIController>(OwnerCharacter->GetController()) : nullptr;
}

void UIncineratorBrainComponent::StopMove()
{
	if (AAIController* AI = GetAI())
	{
		AI->StopMovement();
	}
	bMoveActive = false;
}

void UIncineratorBrainComponent::ApplySpeed(float Speed)
{
	if (MoveComp)
	{
		MoveComp->MaxWalkSpeed = Speed;
	}
}

void UIncineratorBrainComponent::FocusTarget()
{
	AAIController* AI = GetAI();
	if (AI && IsValid(Target))
	{
		AI->SetFocus(Target, EAIFocusPriority::Gameplay);
	}
}

void UIncineratorBrainComponent::FocusPoint(const FVector& Point)
{
	if (AAIController* AI = GetAI())
	{
		AI->SetFocalPoint(Point, EAIFocusPriority::Gameplay);
	}
}

void UIncineratorBrainComponent::FocusHome()
{
	FocusPoint(OwnerCharacter->GetActorLocation() + HomeForward * FocusDistance);
}

void UIncineratorBrainComponent::ClearAIFocus()
{
	if (AAIController* AI = GetAI())
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
	}
}

// ==================================================================
// Presentation
// ==================================================================

void UIncineratorBrainComponent::UpdatePresentation(float DeltaTime)
{
	if (EyeLight)
	{
		float Intensity = 0.f;

		if (CurrentState != EIncineratorState::Disabled)
		{
			if (IsValid(Target))
			{
				Intensity = FMath::GetMappedRangeValueClamped(
					FVector2D(LightOffDistance, LightFullDistance),
					FVector2D(0.f, MaxLightIntensity),
					DistanceToTarget);
			}

			// Strobe during the windup = "it's about to swing".
			if (CurrentState == EIncineratorState::Windup)
			{
				const float Pulse = 0.5f + 0.5f * FMath::Sin(StateTime * WindupFlashHz * UE_TWO_PI);
				Intensity *= 1.f + WindupFlashBoost * Pulse;
			}
		}

		if (!FMath::IsNearlyEqual(Intensity, LastLightIntensity, 1.f))
		{
			EyeLight->SetIntensity(Intensity);
			LastLightIntensity = Intensity;
		}

		if (bDriveLightColor)
		{
			FLinearColor TargetColor = FriendlyColor;
			if (bHostile)
			{
				const bool bUnsure = CurrentState == EIncineratorState::Alert
					|| CurrentState == EIncineratorState::Search
					|| CurrentState == EIncineratorState::Idle
					|| CurrentState == EIncineratorState::Return;
				TargetColor = bUnsure ? AlertColor : HostileColor;
			}

			const float Alpha = LightColorInterpSpeed > 0.f ? FMath::Clamp(DeltaTime * LightColorInterpSpeed, 0.f, 1.f) : 1.f;
			const FLinearColor NewColor = FLinearColor::LerpUsingHSV(CurrentLightColor, TargetColor, Alpha);
			if (!NewColor.Equals(CurrentLightColor, 0.002f))
			{
				CurrentLightColor = NewColor;
				EyeLight->SetLightColor(CurrentLightColor);
			}
		}
	}

	if (EngineAudio)
	{
		const float Speed = OwnerCharacter->GetVelocity().Size2D();
		const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(ChaseSpeed, 1.f), 0.f, 1.f);

		const float TargetVolume = Speed > MoveSoundThreshold ? FMath::Lerp(EngineMinVolume, EngineLoopVolume, SpeedAlpha) : 0.f;
		const float TargetPitch = FMath::Lerp(EnginePitchMin, EnginePitchMax, SpeedAlpha);

		const float NewVolume = EngineInterpSpeed > 0.f ? FMath::FInterpTo(CurrentEngineVolume, TargetVolume, DeltaTime, EngineInterpSpeed) : TargetVolume;
		const float NewPitch = EngineInterpSpeed > 0.f ? FMath::FInterpTo(CurrentEnginePitch, TargetPitch, DeltaTime, EngineInterpSpeed) : TargetPitch;

		if (!FMath::IsNearlyEqual(NewVolume, CurrentEngineVolume, 0.001f))
		{
			CurrentEngineVolume = NewVolume;
			EngineAudio->SetVolumeMultiplier(CurrentEngineVolume);
		}
		if (!FMath::IsNearlyEqual(NewPitch, CurrentEnginePitch, 0.001f))
		{
			CurrentEnginePitch = NewPitch;
			EngineAudio->SetPitchMultiplier(CurrentEnginePitch);
		}
	}
}

// ==================================================================
// Debug
// ==================================================================

void UIncineratorBrainComponent::DrawDebug() const
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	const FVector Loc = OwnerCharacter->GetActorLocation();
	const FVector Fwd = OwnerCharacter->GetActorForwardVector();

	// Vision cone + close sense
	DrawDebugCone(World, GetEyeLocation(), Fwd, SightRange, FMath::DegreesToRadians(SightHalfAngle), FMath::DegreesToRadians(15.f),
		24, bCanSeeTarget ? FColor::Green : FColor(80, 80, 80), false, -1.f, 0, 1.f);
	DrawDebugCircle(World, Loc, CloseSenseRadius, 32, FColor::Cyan, false, -1.f, 0, 1.f, FVector::ForwardVector, FVector::RightVector, false);

	// Attack reach + hit cone
	DrawDebugCircle(World, GetHitOrigin(), AttackRange, 32, FColor::Orange, false, -1.f, 0, 2.f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugCone(World, Loc, Fwd, HitRange, FMath::Acos(FMath::Clamp(HitAngleDot, -1.f, 1.f)), 0.f, 12,
		CurrentState == EIncineratorState::Windup ? FColor::Red : FColor(120, 40, 40), false, -1.f, 0, 1.5f);

	// Home + leash
	DrawDebugCircle(World, HomeLocation, LeashRadius, 64, FColor::Emerald, false, -1.f, 0, 3.f, FVector::ForwardVector, FVector::RightVector, false);
	DrawDebugSphere(World, HomeLocation, 30.f, 8, FColor::Emerald, false, -1.f);

	// Last known position
	if (bHasLastKnown)
	{
		DrawDebugSphere(World, LastKnownLocation, 40.f, 8, FColor::Purple, false, -1.f);
	}

	// Sight line
	if (IsValid(Target))
	{
		DrawDebugLine(World, GetEyeLocation(), GetTargetHeadLocation(), bCanSeeTarget ? FColor::Green : FColor::Red, false, -1.f, 0, 1.f);
	}

	const UEnum* StateEnum = StaticEnum<EIncineratorState>();
	const FString Label = FString::Printf(TEXT("%s  %.1fs\n%s  anger %.2f\ndist %.0f  %s"),
		*StateEnum->GetDisplayNameTextByValue(static_cast<int64>(CurrentState)).ToString(),
		StateTime,
		bHostile ? TEXT("HOSTILE") : TEXT("friendly"),
		Anger,
		DistanceToTarget,
		bCanSeeTarget ? TEXT("SEES YOU") : TEXT("-"));
	DrawDebugString(World, Loc + FVector(0.f, 0.f, 220.f), Label, nullptr, FColor::White, 0.f, true);
#endif
}