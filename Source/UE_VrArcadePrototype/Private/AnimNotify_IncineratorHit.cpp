// AnimNotify_IncineratorHit.cpp

#include "AnimNotify_IncineratorHit.h"

#include "Components/SkeletalMeshComponent.h"
#include "IncineratorBrainComponent.h"

void UAnimNotify_IncineratorHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);

	// In the montage editor preview there's no brain, so this safely does nothing.
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	if (UIncineratorBrainComponent* Brain = Owner ? Owner->FindComponentByClass<UIncineratorBrainComponent>() : nullptr)
	{
		Brain->ResolveAttackHit();
	}
}
