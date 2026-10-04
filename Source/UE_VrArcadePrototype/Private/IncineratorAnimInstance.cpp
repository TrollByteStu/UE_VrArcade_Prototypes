// IncineratorAnimInstance.cpp

#include "IncineratorAnimInstance.h"

#include "GameFramework/Pawn.h"

void UIncineratorAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	if (APawn* Pawn = TryGetPawnOwner())
	{
		Brain = Pawn->FindComponentByClass<UIncineratorBrainComponent>();
		LastYaw = Pawn->GetActorRotation().Yaw;
		bHasLastYaw = true;
	}
}

void UIncineratorAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	APawn* Pawn = TryGetPawnOwner();
	if (!Pawn || DeltaSeconds <= 0.f)
	{
		return; // also covers the AnimBP editor preview
	}

	if (!Brain.IsValid())
	{
		Brain = Pawn->FindComponentByClass<UIncineratorBrainComponent>();
	}

	const FRotator ActorRot = Pawn->GetActorRotation();

	// ---------------------------------------------------------------- Locomotion
	const FVector Velocity = Pawn->GetVelocity();
	Speed = Velocity.Size2D();
	bIsMoving = Speed > MoveThreshold;

	if (bIsMoving)
	{
		const FVector Local = ActorRot.UnrotateVector(Velocity);
		Direction = FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X));
	}
	// When stopped, Direction keeps its last value so the blendspace doesn't snap.

	const float Yaw = ActorRot.Yaw;
	if (bHasLastYaw)
	{
		const float RawTurnRate = FMath::FindDeltaAngleDegrees(LastYaw, Yaw) / DeltaSeconds;
		TurnRate = FMath::FInterpTo(TurnRate, RawTurnRate, DeltaSeconds, TurnRateInterpSpeed);
	}
	LastYaw = Yaw;
	bHasLastYaw = true;

	bIsTurningInPlace = !bIsMoving && FMath::Abs(TurnRate) > TurnInPlaceThreshold;

	const float TargetLean = bIsMoving ? FMath::Clamp(TurnRate / FMath::Max(LeanMaxTurnRate, 1.f), -1.f, 1.f) : 0.f;
	LeanAmount = FMath::FInterpTo(LeanAmount, TargetLean, DeltaSeconds, TurnRateInterpSpeed);

	// ---------------------------------------------------------------- Brain
	bool bWantsLook = false;
	FVector LookGoal = Pawn->GetPawnViewLocation() + ActorRot.Vector() * 1000.f;

	if (UIncineratorBrainComponent* B = Brain.Get())
	{
		State = B->GetState();
		bIsHostile = B->IsHostile();
		Anger = B->GetAnger();
		bCanSeeTarget = B->CanSeeTarget();
		bIsAlert = State == EIncineratorState::Alert;
		bIsSearching = State == EIncineratorState::Search;
		bIsAttacking = State == EIncineratorState::Windup || State == EIncineratorState::Recover;
		bIsDisabled = State == EIncineratorState::Disabled;

		if (bCanSeeTarget && !bIsDisabled)
		{
			bWantsLook = bIsHostile || B->GetDistanceToTarget() <= FriendlyLookDistance;
			if (bWantsLook)
			{
				LookGoal = B->GetTargetHeadLocation();
			}
		}
	}

	// ---------------------------------------------------------------- Head aim
	// Snap the look point if we were fully faded out, so the head doesn't sweep in from an old spot.
	if (LookAtAlpha < 0.01f)
	{
		LookAtLocation = LookGoal;
	}
	else
	{
		LookAtLocation = FMath::VInterpTo(LookAtLocation, LookGoal, DeltaSeconds, LookAtInterpSpeed);
	}
	LookAtAlpha = FMath::FInterpTo(LookAtAlpha, bWantsLook ? 1.f : 0.f, DeltaSeconds, LookAtInterpSpeed);

	const FRotator LookRot = (LookAtLocation - Pawn->GetPawnViewLocation()).Rotation();
	const FRotator Delta = (LookRot - ActorRot).GetNormalized();
	AimYaw = FMath::Clamp(Delta.Yaw, -MaxAimYaw, MaxAimYaw) * LookAtAlpha;
	AimPitch = FMath::Clamp(Delta.Pitch, -MaxAimPitch, MaxAimPitch) * LookAtAlpha;

	// ---------------------------------------------------------------- Engine strain + rattle
	const float SpeedAlpha = FMath::Clamp(Speed / FMath::Max(StrainFullSpeed, 1.f), 0.f, 1.f);
	const float WindupBoost = (State == EIncineratorState::Windup) ? 0.5f : 0.f;
	const float TargetStrain = FMath::Clamp(SpeedAlpha * 0.6f + WindupBoost + (bIsHostile ? Anger * 0.2f : 0.f), 0.f, 1.f);
	EngineStrain = FMath::FInterpTo(EngineStrain, TargetStrain, DeltaSeconds, 5.f);

	// PerlinNoise1D repeats every 256 units, so wrapping keeps float precision over long convention sessions.
	ShakeTime = FMath::Fmod(ShakeTime + DeltaSeconds * ShakeFrequency, 256.f);
	const float Amp = ShakeMaxDegrees * EngineStrain;
	BodyShake = FRotator(
		FMath::PerlinNoise1D(ShakeTime) * Amp,				// pitch
		FMath::PerlinNoise1D(ShakeTime + 37.1f) * Amp * 0.5f,	// yaw
		FMath::PerlinNoise1D(ShakeTime + 91.7f) * Amp);		// roll
}