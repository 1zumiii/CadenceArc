#include "AnimNotifyState_CadenceArcBufferWindow.h"

#include "CadenceArcAbilityExecutorComponent.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	UCadenceArcAbilityExecutorComponent* FindExecutor(const USkeletalMeshComponent* MeshComp)
	{
		const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		return Owner ? Owner->FindComponentByClass<UCadenceArcAbilityExecutorComponent>() : nullptr;
	}
}

void UAnimNotifyState_CadenceArcBufferWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                          const float TotalDuration,
                                                          const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UCadenceArcAbilityExecutorComponent* Executor = FindExecutor(MeshComp))
	{
		Executor->OpenBufferWindowForAnimation(Animation);
	}
}

void UAnimNotifyState_CadenceArcBufferWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                        const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UCadenceArcAbilityExecutorComponent* Executor = FindExecutor(MeshComp))
	{
		Executor->CloseBufferWindowForAnimation(Animation);
	}
}

FString UAnimNotifyState_CadenceArcBufferWindow::GetNotifyName_Implementation() const
{
	return TEXT("CadenceArc Buffer Window");
}
