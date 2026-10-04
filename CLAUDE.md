# Monkey Simulator — Claude Code notes

VR game in Unreal Engine 5 (student team project). Player is a monkey; built on the VRE template. Keep this file short and current.

## Project facts

- Engine: UE 5.7 installed at `C:/Program Files/Epic Games/UE_5.7`
- Project file: `UE_VrArcadePrototype.uproject` (this folder). The repo holds several prototypes (`Content/_MonkeySimulator`, `_KaijuPong`, `_JotunBash_Stuff`); Monkey Simulator is the one we work on here.
- C++ module: `Source/UE_VrArcadePrototype/` — mixed layout: the Shark files sit flat in the module root, the Incinerator files (`IncineratorBrainComponent`, `IncineratorAnimInstance`, `AnimNotify_IncineratorHit`) use `Public/` (headers) + `Private/` (.cpp). Put new files in `Public/` + `Private/`.
- Export macro: `UE_VRARCADEPROTOTYPE_API`
- Most gameplay is Blueprint. C++ is used for systems that are easier to tune in code (e.g. `USharkBrainComponent`, `UIncineratorBrainComponent`).

## Important Blueprint assets (binary .uasset — you can't read or edit these)

- Player: `/Game/VRE/Core/Character/BP_VRCharacter`
  - bool variable `InWater?` (read from C++ by name via reflection)
  - function `HitPointsWounded(DamageType: E_DamageTypes)`
- Damage enum: `/Game/_MonkeySimulator/System/Player/Stats/E_DamageTypes`
- Shark: `/Game/_MonkeySimulator/Models/SharkLowBaked/ReimportWAnims/BPA_Shark` (Character, uses `USharkBrainComponent`)
- Shark patrol: `BPA_SharkSpline` with component `TargetMesh`

When Blueprint logic matters, ask me to paste the nodes (select in editor → Ctrl+C → paste as text). Never pretend to have edited a .uasset — tell me which Blueprint changes I need to make by hand.

## Build

The `-Project=` paths below are the school PC clone (`C:/Users/STUVR/...`). On Emil's PC the project is at `C:/Users/emil_/UE_VrArcade_Prototypes/` — swap the path to wherever your clone lives.

The editor must be CLOSED (or Live Coding off) before command-line builds. New UCLASS / UENUM / UPROPERTY layout changes need a full build, not Live Coding.

```
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" UE_VrArcadePrototypeEditor Win64 Development -Project="C:/Users/STUVR/Documents/GitHub/UE_VrArcade_Prototypes/UE_VrArcadePrototype.uproject" -WaitMutex
```

Regenerate project files after adding/removing source files:

```
"C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" -projectfiles -project="C:/Users/STUVR/Documents/GitHub/UE_VrArcade_Prototypes/UE_VrArcadePrototype.uproject" -game -engine
```

Always build after C++ changes and fix errors before saying you're done.

## Code style

- UE naming: `U`/`A`/`F`/`E` prefixes, `b` for bools, PascalCase.
- Expose tunables as `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="X|Y")` with sensible defaults and ClampMin.
- Keep content (damage, sound, VFX, haptics) in Blueprint; C++ fires `BlueprintAssignable` events.
- Use `TObjectPtr` for UPROPERTY pointers, `TWeakObjectPtr` for caches.
- Log with a clear prefix, e.g. `UE_LOG(LogTemp, Warning, TEXT("SharkBrain: ..."))`.

## VR rules (these matter for player comfort)

- Never move, rotate or launch the player's camera/pawn without asking me first.
- Enemies must not physically push the player capsule (use Overlap vs Pawn).
- Target the HMD camera for "head" positions, not the character mesh.
- Watch performance: avoid per-tick heavy work, prefer timers or cached lookups.

## Don't touch

- `Content/` binary assets, `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`
- `Config/*.ini` unless I ask
- Engine source
