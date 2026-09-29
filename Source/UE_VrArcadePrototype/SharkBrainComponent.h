// SharkBrainComponent.h
// Drop-in AI brain for BPA_Shark (Monkey Simulator).
// States: Patrol -> Stalk -> Lunge -> Retreat. Movement goes through CharacterMovement (Flying mode).
// Damage, sound and haptics stay in Blueprint via the OnBite / OnStateChanged / OnLungeBurst events.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SharkBrainComponent.generated.h"

class APawn;
class UCameraComponent;
class UCharacterMovementComponent;
class USceneComponent;
class FBoolProperty;

UENUM(BlueprintType)
enum class ESharkState : uint8
{
	Patrol	UMETA(DisplayName = "Patrol"),
	Stalk	UMETA(DisplayName = "Stalk"),
	Lunge	UMETA(DisplayName = "Lunge"),
	Retreat	UMETA(DisplayName = "Retreat")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSharkStateChangedSignature, ESharkState, NewState, ESharkState, OldState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSharkBiteSignature, AActor*, Victim);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSharkLungeBurstSignature);

UCLASS(ClassGroup = (MonkeySimulator), meta = (BlueprintSpawnableComponent))
class UE_VRARCADEPROTOTYPE_API USharkBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USharkBrainComponent();

	// ---------------------------------------------------------------- Target

	/** Who the shark hunts. Auto-filled with player 0 when empty and bAutoTargetPlayer is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Target")
	TObjectPtr<AActor> Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Target")
	bool bAutoTargetPlayer = true;

	/** Bool variable on the target that says it's swimming. Read by name, so it works with BP variables like "InWater?". Set to None to skip the check. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Target")
	FName WaterFlagPropertyName = TEXT("InWater?");

	/** Start hunting when the target is this close (while patrolling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Target", meta = (ClampMin = "0"))
	float DetectRadius = 1500.f;

	/** Give up when the target gets this far away. Bigger than DetectRadius so it doesn't flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Target", meta = (ClampMin = "0"))
	float LoseRadius = 2000.f;

	// ---------------------------------------------------------------- Movement

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float PatrolSpeed = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float StalkSpeed = 350.f;

	/** Speed during the short "hesitation" right before the burst. The telegraph. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float WindupSpeed = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float LungeSpeed = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float RetreatSpeed = 400.f;

	/** Degrees per second. Low = heavy, wide shark turns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "1"))
	float TurnRate = 60.f;

	/** Turn rate during the lunge, so it can actually track your head. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "1"))
	float LungeTurnRate = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "0"))
	float BrakingDeceleration = 200.f;

	/** Within this distance of its move target the shark eases off instead of stop-starting. Not used during Lunge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Movement", meta = (ClampMin = "1"))
	float ArriveRadius = 150.f;

	// ---------------------------------------------------------------- Stalk

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Stalk", meta = (ClampMin = "0"))
	float StalkRadius = 400.f;

	/** How far ahead on the circle the shark aims. Higher = tighter, smoother orbit. Orbit speed comes from StalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Stalk", meta = (ClampMin = "5", ClampMax = "90"))
	float OrbitLeadDegrees = 35.f;

	/** How far below the target's head the shark circles. Small value = fin near the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Stalk")
	float StalkHeightBelowHead = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Stalk", meta = (ClampMin = "0"))
	float StalkDurationMin = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Stalk", meta = (ClampMin = "0"))
	float StalkDurationMax = 6.f;

	// ---------------------------------------------------------------- Lunge / Bite

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Lunge", meta = (ClampMin = "0"))
	float WindupTime = 0.35f;

	/** Time after the burst before it counts as a miss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Lunge", meta = (ClampMin = "0"))
	float LungeMaxTime = 1.5f;

	/** Mouth-to-head distance that counts as a bite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Lunge", meta = (ClampMin = "0"))
	float BiteRadius = 120.f;

	/** Used when no mouth component is set: actor location + forward * this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Lunge")
	float MouthForwardOffset = 150.f;

	/** Velocity kept after a bite (0-1). Stops the shark plowing through your face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Lunge", meta = (ClampMin = "0", ClampMax = "1"))
	float PostBiteVelocityScale = 0.3f;

	// ---------------------------------------------------------------- Retreat

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Retreat", meta = (ClampMin = "0"))
	float RetreatTime = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Retreat", meta = (ClampMin = "0"))
	float RetreatDistance = 1000.f;

	// ---------------------------------------------------------------- Water

	/** Keep the shark under WaterSurfaceZ in every state except Lunge (so lunges can breach). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Water")
	bool bUseWaterSurface = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Water", meta = (EditCondition = "bUseWaterSurface"))
	float WaterSurfaceZ = 0.f;

	/** Minimum depth below the surface when clamped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Water", meta = (EditCondition = "bUseWaterSurface"))
	float SurfaceMargin = 40.f;

	// ---------------------------------------------------------------- Debug

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shark|Debug")
	bool bDrawDebug = false;

	// ---------------------------------------------------------------- Events

	UPROPERTY(BlueprintAssignable, Category = "Shark|Events")
	FSharkStateChangedSignature OnStateChanged;

	/** Fired once per successful bite. Do damage / sound / haptics here. */
	UPROPERTY(BlueprintAssignable, Category = "Shark|Events")
	FSharkBiteSignature OnBite;

	/** Fired when the windup ends and the shark bursts forward. Good spot for an audio sting. */
	UPROPERTY(BlueprintAssignable, Category = "Shark|Events")
	FSharkLungeBurstSignature OnLungeBurst;

	// ---------------------------------------------------------------- API

	UFUNCTION(BlueprintCallable, Category = "Shark")
	void SetState(ESharkState NewState);

	UFUNCTION(BlueprintPure, Category = "Shark")
	ESharkState GetState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Shark")
	float GetTimeInState() const { return StateTime; }

	/** Point the shark follows while patrolling (e.g. SharkSplineTarget -> TargetMesh). */
	UFUNCTION(BlueprintCallable, Category = "Shark")
	void SetPatrolTargetComponent(USceneComponent* InComponent) { PatrolTargetComponent = InComponent; }

	/** Where bites are measured from (e.g. AttackSphere). */
	UFUNCTION(BlueprintCallable, Category = "Shark")
	void SetMouthComponent(USceneComponent* InComponent) { MouthComponent = InComponent; }

	UFUNCTION(BlueprintPure, Category = "Shark")
	bool IsTargetInWater() const;

	UFUNCTION(BlueprintPure, Category = "Shark")
	FVector GetTargetHeadLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ChangeState(ESharkState NewState, bool bForce = false);
	void ResolveTarget();
	bool CanHunt(const FVector& SharkLocation) const;
	FVector GetMouthLocation() const;
	void ApplySpeed(float Speed);
	void ApplyTurnRate(float Rate);

	UPROPERTY(Transient)
	TObjectPtr<APawn> OwnerPawn;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MoveComp;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> PatrolTargetComponent;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> MouthComponent;

	ESharkState CurrentState = ESharkState::Patrol;
	float StateTime = 0.f;
	float OrbitDirection = 1.f;
	float CurrentStalkDuration = 4.f;
	bool bBurstFired = false;

	// Caches (mutable so the BlueprintPure getters can fill them)
	mutable TWeakObjectPtr<UClass> CachedWaterClass;
	mutable FBoolProperty* CachedWaterProp = nullptr;
	mutable bool bWarnedMissingWaterProp = false;
	mutable TWeakObjectPtr<AActor> CachedCameraOwner;
	mutable TWeakObjectPtr<UCameraComponent> CachedCamera;
};