// AnimNotify_IncineratorHit.h
// Drop this notify on the impact frame of AM_NPC_Attack. It tells the IncineratorBrainComponent to resolve the swing.
// Turn on "Use Anim Notify For Hit" on the brain so the timer only acts as a fallback.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotify_IncineratorHit.generated.h"

UCLASS(meta = (DisplayName = "Incinerator Hit"))
class UE_VRARCADEPROTOTYPE_API UAnimNotify_IncineratorHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override { return TEXT("Incinerator Hit"); }
};