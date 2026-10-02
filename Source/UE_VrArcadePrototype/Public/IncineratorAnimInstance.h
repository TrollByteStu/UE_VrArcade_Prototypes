// IncineratorAnimInstance.h
// C++ parent for the Incinerator's Animation Blueprint.
// Reads the IncineratorBrainComponent + movement every frame and exposes clean variables for the AnimGraph:
// locomotion (Speed/Direction/TurnRate/Lean), mood (State/Hostile/Anger), head aim (LookAt / AimOffset) and a rusty engine shake.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "IncineratorBrainComponent.h"
#include "IncineratorAnimInstance.generated.h"

UCLASS()
class UE_VRARCADEPROTOTYPE_API UIncineratorAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	// ---------------------------------------------------------------- Locomotion (read in AnimGraph)

	/** Ground speed (cm/s). Drive a 1D/2D blendspace with this. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	float Speed = 0.f;

	/** -180..180, movement direction relative to facing. It strafes while staring at you, so this matters. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	bool bIsMoving = false;

	/** Smoothed yaw speed in deg/s (+ = turning right). */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	float TurnRate = 0.f;

	/** Standing still but rotating = play a turn/shuffle anim. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	bool bIsTurningInPlace = false;

	/** -1..1 lean into turns while moving. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Locomotion")
	float LeanAmount = 0.f;

	// ---------------------------------------------------------------- Mood (from the brain)

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	EIncineratorState State = EIncineratorState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bIsHostile = false;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	float Anger = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bCanSeeTarget = false;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bIsAlert = false;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bIsSearching = false;

	/** Windup or Recover. Use to block idle fidgets etc. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bIsAttacking = false;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Brain")
	bool bIsDisabled = false;

	// ---------------------------------------------------------------- Head aim

	/** World location the head should look at (smoothed). Feed a "Look At" node. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Look")
	FVector LookAtLocation = FVector::ZeroVector;

	/** 0..1 blend for the Look At / Aim Offset. Fades in when it sees you, out when it doesn't. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Look")
	float LookAtAlpha = 0.f;

	/** Head yaw/pitch relative to the body, clamped. Feed an Aim Offset if you have one. */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Look")
	float AimYaw = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Look")
	float AimPitch = 0.f;

	// ---------------------------------------------------------------- Rusty engine feel

	/** 0..1 how hard the engine is working (speed, windup, anger). */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Feel")
	float EngineStrain = 0.f;

	/** Procedural rattle. Plug into Transform (Modify) Bone on the body/spine, "Add to Existing". */
	UPROPERTY(BlueprintReadOnly, Category = "Incinerator|Feel")
	FRotator BodyShake = FRotator::ZeroRotator;

	// ---------------------------------------------------------------- Tuning

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float MoveThreshold = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float TurnInPlaceThreshold = 45.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float TurnRateInterpSpeed = 8.f;

	/** Turn rate (deg/s) that gives full lean. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float LeanMaxTurnRate = 180.f;

	/** Friendly mode: only looks at you inside this distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float FriendlyLookDistance = 1200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float LookAtInterpSpeed = 6.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float MaxAimYaw = 90.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float MaxAimPitch = 45.f;

	/** Speed that counts as full strain. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float StrainFullSpeed = 600.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float ShakeFrequency = 14.f;

	/** Max rattle in degrees at full strain. Keep small, it's VR. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Incinerator|Tuning")
	float ShakeMaxDegrees = 1.5f;

protected:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

private:
	TWeakObjectPtr<UIncineratorBrainComponent> Brain;
	float LastYaw = 0.f;
	bool bHasLastYaw = false;
	float ShakeTime = 0.f;
};