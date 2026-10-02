// IncineratorBrainComponent.h
// Drop-in AI brain for BPC_NPC_Incinerator (Monkey Simulator). Add it to the Character BP, no reparenting needed.
// States: Idle -> Alert -> Chase -> Windup -> Recover -> (Chase | Search) -> Return -> Idle.
// Perception = sight cone + line of sight + "last known position". Hostility = anger meter (good monkey / bad monkey).
// Movement goes through the AIController (navmesh). Damage, sound and VFX stay in Blueprint via the events.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "IncineratorBrainComponent.generated.h"

class AAIController;
class AController;
class ACharacter;
class UAnimMontage;
class UAudioComponent;
class UCameraComponent;
class UCharacterMovementComponent;
class UDamageType;
class USceneComponent;
class USpotLightComponent;

UENUM(BlueprintType)
enum class EIncineratorState : uint8
{
	Idle		UMETA(DisplayName = "Idle"),		// guarding home. Friendly: watches the monkey. Hostile: waits for it to enter the leash
	Alert		UMETA(DisplayName = "Alert"),		// spotted a hostile monkey, turning + telegraphing before it charges
	Chase		UMETA(DisplayName = "Chase"),		// pathing toward the monkey
	Windup		UMETA(DisplayName = "Windup"),		// swing telegraph: tracks first, then locks rotation (dodgeable)
	Recover		UMETA(DisplayName = "Recover"),		// post-swing cooldown, turns back toward the monkey
	Search		UMETA(DisplayName = "Search"),		// lost sight: walks to last known position and looks around
	Return		UMETA(DisplayName = "Return"),		// gave up / calmed down: walks home
	Disabled	UMETA(DisplayName = "Disabled")		// brain off (cutscene, death, dev toggle)
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIncineratorStateChangedSignature, EIncineratorState, NewState, EIncineratorState, OldState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FIncineratorHitSignature, AActor*, Victim, FVector, KnockbackVelocity);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIncineratorTargetSignature, AActor*, TargetActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FIncineratorHostilitySignature, bool, bIsHostile);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FIncineratorSimpleSignature);

UCLASS(ClassGroup = (MonkeySimulator), meta = (BlueprintSpawnableComponent))
class UE_VRARCADEPROTOTYPE_API UIncineratorBrainComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIncineratorBrainComponent();

	// ---------------------------------------------------------------- Target

	/** Who it watches/hunts. Auto-filled (and re-filled after respawns) with player 0 when bAutoTargetPlayer is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Target")
	TObjectPtr<AActor> Target;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Target")
	bool bAutoTargetPlayer = true;

	// ---------------------------------------------------------------- Perception

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception", meta = (ClampMin = "0"))
	float SightRange = 3000.f;

	/** Half-angle of the vision cone in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception", meta = (ClampMin = "1", ClampMax = "180"))
	float SightHalfAngle = 70.f;

	/** Inside this radius the cone is ignored (it "feels" you behind it). Line of sight still required. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception", meta = (ClampMin = "0"))
	float CloseSenseRadius = 400.f;

	/** Seconds between line-of-sight traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception", meta = (ClampMin = "0.02"))
	float PerceptionInterval = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception")
	TEnumAsByte<ECollisionChannel> SightTraceChannel = ECC_Visibility;

	/** Keeps chasing this long after losing sight (you ducked behind a crate) before switching to Search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Perception", meta = (ClampMin = "0"))
	float LoseSightGrace = 1.f;

	// ---------------------------------------------------------------- Hostility (good monkey / bad monkey)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility")
	bool bStartHostile = false;

	/** Anger (0-1) at which it turns hostile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility", meta = (ClampMin = "0", ClampMax = "1"))
	float HostileThreshold = 0.5f;

	/** Anger at which it calms down again. Lower than HostileThreshold so it doesn't flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility", meta = (ClampMin = "0", ClampMax = "1"))
	float CalmThreshold = 0.15f;

	/** Anger lost per second while it can't see the monkey. 0 = holds the grudge forever. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility", meta = (ClampMin = "0"))
	float AngerDecayPerSecond = 0.02f;

	/** Raise anger automatically from engine damage (ApplyDamage) on the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility")
	bool bProvokeOnDamage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility", meta = (ClampMin = "0", EditCondition = "bProvokeOnDamage"))
	float AngerPerDamagePoint = 0.05f;

	/** Each missed swing makes it a bit angrier (= faster, see Enraged multipliers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Hostility", meta = (ClampMin = "0", ClampMax = "1"))
	float MissAngerGain = 0.08f;

	// ---------------------------------------------------------------- Leash

	/** Never chases further than this from where it started. Stops it being kited across the island. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Leash", meta = (ClampMin = "0"))
	float LeashRadius = 4000.f;

	/** Extra room the monkey can have outside the leash before it gives up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Leash", meta = (ClampMin = "0"))
	float LeashBuffer = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Leash", meta = (ClampMin = "10"))
	float HomeAcceptRadius = 120.f;

	// ---------------------------------------------------------------- Movement

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float WalkSpeed = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float SearchSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float ChaseSpeed = 500.f;

	/** Chase speed multiplier at max anger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "1"))
	float EnragedSpeedMultiplier = 1.3f;

	/** Yaw degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "1"))
	float TurnRate = 180.f;

	/** Set up CharacterMovement so AI focus actually turns the body. Turn off if your BP handles rotation itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement")
	bool bConfigureRotation = true;

	/** MoveTo acceptance radius. Keep below AttackRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float StopDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0.05"))
	float RepathInterval = 0.5f;

	/** Below this 2D speed while chasing (and out of reach) counts as "stuck / can't reach". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float StuckSpeedThreshold = 30.f;

	/** Seconds stuck before OnTargetUnreachable fires (monkey climbed something). It keeps staring up at you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float UnreachableNotifyTime = 1.5f;

	/** Seconds stuck before it gives up and walks home. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Movement", meta = (ClampMin = "0"))
	float UnreachableGiveUpTime = 8.f;

	// ---------------------------------------------------------------- Alert

	/** Telegraph time before the first charge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Alert", meta = (ClampMin = "0"))
	float AlertTime = 0.8f;

	/** Alert time when it re-spots you shortly after a fight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Alert", meta = (ClampMin = "0"))
	float ReacquireAlertTime = 0.2f;

	/** How long "shortly after a fight" is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Alert", meta = (ClampMin = "0"))
	float CombatMemoryTime = 10.f;

	// ---------------------------------------------------------------- Attack

	/** Distance (hit origin -> monkey head or body) at which it starts a swing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float AttackRange = 350.f;

	/** Must be facing at least this much (2D dot) before swinging. Stops sideways swings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "-1", ClampMax = "1"))
	float AttackFacingDot = 0.85f;

	/** Distance checked when the swing lands. Slightly bigger than AttackRange so a dodge has to be real. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float HitRange = 400.f;

	/** Min 2D dot(forward, dir to monkey) for a hit. 0.5 = 60 degree half-angle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "-1", ClampMax = "1"))
	float HitAngleDot = 0.5f;

	/** Used when no hit origin component is set: actor location + forward * this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	float HitForwardOffset = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float WindupTime = 0.6f;

	/** Part of the windup where it still tracks you. After that rotation locks, so stepping aside dodges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0", ClampMax = "1"))
	float WindupTrackFraction = 0.6f;

	/** Aims at where you'll be this many seconds from now while tracking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float AimLeadTime = 0.25f;

	/** Windup time multiplier at max anger (montage play rate is scaled to match). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0.1", ClampMax = "1"))
	float EnragedWindupMultiplier = 0.8f;

	/** Random cooldown range so the rhythm isn't a metronome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float CooldownMin = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float CooldownMax = 1.6f;

	/** Extra breathing room after a hit (you just got launched in VR, give you a second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack", meta = (ClampMin = "0"))
	float PostHitExtraCooldown = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	bool bApplyKnockback = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	float KnockbackStrength = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	float KnockbackUp = 300.f;

	/** Resolve the hit from an AnimNotify calling ResolveAttackHit. The timer then only acts as a safety net. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	bool bUseAnimNotifyForHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Attack")
	TObjectPtr<UAnimMontage> AttackMontage;

	// ---------------------------------------------------------------- Search

	/** Hard cap on the whole search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Search", meta = (ClampMin = "0"))
	float SearchMaxTime = 12.f;

	/** How long it looks around once it reaches the last known position. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Search", meta = (ClampMin = "0"))
	float SearchLookTime = 4.f;

	/** Degrees left/right it sweeps while looking around. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Search", meta = (ClampMin = "0", ClampMax = "180"))
	float SearchLookAngle = 60.f;

	/** Sweep speed (radians per second of the sine). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Search", meta = (ClampMin = "0.1"))
	float SearchLookSpeed = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Search", meta = (ClampMin = "10"))
	float SearchAcceptRadius = 100.f;

	// ---------------------------------------------------------------- Presentation

	/** Auto-found by name at BeginPlay (BP component variable name). Or call SetEyeLightComponent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation")
	FName EyeLightComponentName = TEXT("SpotLight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation")
	FName EngineAudioComponentName = TEXT("Audio_SW_Machine_Loop_RustyEngine_Strain");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation")
	float LightOffDistance = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation")
	float LightFullDistance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float MaxLightIntensity = 15000.f;

	/** Tint the eye by mood. Turn off to keep the color set on the light in BP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation")
	bool bDriveLightColor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (EditCondition = "bDriveLightColor"))
	FLinearColor FriendlyColor = FLinearColor(0.2f, 0.6f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (EditCondition = "bDriveLightColor"))
	FLinearColor AlertColor = FLinearColor(1.f, 0.7f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (EditCondition = "bDriveLightColor"))
	FLinearColor HostileColor = FLinearColor(1.f, 0.15f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float LightColorInterpSpeed = 6.f;

	/** Extra intensity flashing during the windup (the "it's about to swing" tell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float WindupFlashBoost = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float WindupFlashHz = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float MoveSoundThreshold = 10.f;

	/** Engine volume at slow walk / at full chase speed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float EngineMinVolume = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float EngineLoopVolume = 3.f;

	/** Engine strains harder (higher pitch) the faster it goes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0.1"))
	float EnginePitchMin = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0.1"))
	float EnginePitchMax = 1.25f;

	/** 0 = snap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Presentation", meta = (ClampMin = "0"))
	float EngineInterpSpeed = 8.f;

	// ---------------------------------------------------------------- Debug

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incinerator|Debug")
	bool bDrawDebug = false;

	// ---------------------------------------------------------------- Events

	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorStateChangedSignature OnStateChanged;

	/** Fired once per landed swing, BEFORE knockback. Call HitPointsWounded here. */
	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorHitSignature OnAttackHit;

	/** Windup started. Good spot for the attack wind-up sound / roar. */
	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorSimpleSignature OnAttackStarted;

	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorSimpleSignature OnAttackMissed;

	/** Spotted a hostile monkey (entering Alert from a calm state). Siren / voice line. */
	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorTargetSignature OnTargetSpotted;

	/** Lost sight and started searching. */
	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorTargetSignature OnTargetLost;

	/** Monkey is somewhere it can't path to (climbed up). Angry stomp / taunt animation. */
	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorTargetSignature OnTargetUnreachable;

	UPROPERTY(BlueprintAssignable, Category = "Incinerator|Events")
	FIncineratorHostilitySignature OnHostilityChanged;

	// ---------------------------------------------------------------- API

	/** Force hostile (anger = 1) or friendly (anger = 0). Calming down walks it home. */
	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetHostile(bool bNewHostile);

	/** Bad monkey did something (hit a turret, threw trash). Adds anger and reveals the instigator's position. */
	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void ReportProvocation(float AngerAmount, AActor* Instigator);

	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetAIEnabled(bool bEnabled);

	/** Resolves the swing. Called automatically after the windup, or from an AnimNotify if bUseAnimNotifyForHit. */
	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void ResolveAttackHit();

	/** Makes the current position/rotation its new home (for leash + return). */
	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetHomeToCurrentTransform();

	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetEyeLightComponent(USpotLightComponent* InComponent) { EyeLight = InComponent; }

	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetEngineAudioComponent(UAudioComponent* InComponent) { EngineAudio = InComponent; }

	/** Where swings are measured from (e.g. a socket-attached scene component on the arm). */
	UFUNCTION(BlueprintCallable, Category = "Incinerator")
	void SetHitOriginComponent(USceneComponent* InComponent) { HitOriginComponent = InComponent; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	EIncineratorState GetState() const { return CurrentState; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	float GetTimeInState() const { return StateTime; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	bool IsHostile() const { return bHostile; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	float GetAnger() const { return Anger; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	bool CanSeeTarget() const { return bCanSeeTarget; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	float GetDistanceToTarget() const { return DistanceToTarget; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	FVector GetLastKnownLocation() const { return LastKnownLocation; }

	UFUNCTION(BlueprintPure, Category = "Incinerator")
	FVector GetTargetHeadLocation() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void HandleOwnerDamaged(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	// State machine
	void ChangeState(EIncineratorState NewState, bool bForce = false);
	void EnterState(EIncineratorState NewState, EIncineratorState OldState);
	void ExitState(EIncineratorState OldState);
	void UpdateState(float DeltaTime);
	void TickIdle();
	void TickAlert();
	void TickChase(float DeltaTime);
	void TickWindup();
	void TickRecover();
	void TickSearch();
	void TickReturn();

	// Senses & mood
	void ResolveTarget();
	void UpdatePerception();
	void UpdateAnger(float DeltaTime);
	void SetHostileInternal(bool bNewHostile);
	float GetTimeSinceSeen() const;
	float GetEnrageAlpha() const;
	bool IsCombatState(EIncineratorState State) const;

	// Geometry helpers
	FVector GetEyeLocation() const;
	FVector GetHitOrigin() const;
	bool IsInsideLeash(const FVector& Point, float Extra = 0.f) const;
	bool IsTargetInAttackReach() const;
	bool IsPointInHitZone(const FVector& Point) const;
	float GetFacingDotToTarget() const;

	// Controller helpers
	AAIController* GetAI() const;
	void StopMove();
	void ApplySpeed(float Speed);
	void FocusTarget();
	void FocusPoint(const FVector& Point);
	void FocusHome();
	void ClearAIFocus();

	void UpdatePresentation(float DeltaTime);
	void DrawDebug() const;

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<UCharacterMovementComponent> MoveComp;

	UPROPERTY(Transient)
	TObjectPtr<USpotLightComponent> EyeLight;

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> EngineAudio;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> HitOriginComponent;

	EIncineratorState CurrentState = EIncineratorState::Idle;
	float StateTime = 0.f;

	// Mood
	bool bHostile = false;
	float Anger = 0.f;

	// Perception
	bool bCanSeeTarget = false;
	bool bHasLastKnown = false;
	FVector LastKnownLocation = FVector::ZeroVector;
	float LastSeenTime = -1000.f;
	float LastCombatTime = -1000.f;
	float DistanceToTarget = TNumericLimits<float>::Max();
	float PerceptionAccumulator = 0.f;

	// Home
	FVector HomeLocation = FVector::ZeroVector;
	FVector HomeForward = FVector::ForwardVector;

	// Movement bookkeeping
	bool bMoveActive = false;
	float LastMoveRequestTime = -1000.f;
	float StuckTime = 0.f;
	bool bUnreachableNotified = false;

	// Attack bookkeeping
	float CurrentAlertTime = 0.f;
	float CurrentWindupTime = 0.f;
	float CurrentMontageLength = 0.f;
	float CurrentCooldown = 0.f;
	bool bHitResolved = false;
	bool bRotationLocked = false;
	int32 ConsecutiveMisses = 0;

	// Search bookkeeping
	bool bSearchArrived = false;
	float SearchArriveTime = 0.f;
	float SearchBaseYaw = 0.f;

	// Presentation
	FLinearColor CurrentLightColor = FLinearColor::White;
	float LastLightIntensity = -1.f;
	float CurrentEngineVolume = 0.f;
	float CurrentEnginePitch = 1.f;

	// Caches (mutable so the BlueprintPure getters can fill them)
	mutable TWeakObjectPtr<AActor> CachedCameraOwner;
	mutable TWeakObjectPtr<UCameraComponent> CachedCamera;
};