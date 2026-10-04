#include "AnimNotifyState_CadenceArcBufferWindow.h"

#include "CadenceArcAbilityExecutorComponent.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Components/SkeletalMeshComponent.h"

namespace
{
	UCadenceArcAbilityExecutorComponent* FindExecutor(const USkeletalMeshComponent* MeshComp)
	{
		const AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
		return Owner ? Owner->FindComponentByClass<UCadenceArcAbilityExecutorComponent>() : nullptr;
	}

	// 同一个蒙太奇资产可能同时有两个播放实例（一个淡出、一个刚开始），用实例 ID 区分；不在蒙太奇中时为 INDEX_NONE
	int32 GetMontageInstanceId(const FAnimNotifyEventReference& EventReference)
	{
		const UE::Anim::FAnimNotifyMontageInstanceContext* Context =
			EventReference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
		return Context ? Context->MontageInstanceID : INDEX_NONE;
	}
}

void UAnimNotifyState_CadenceArcBufferWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                          const float TotalDuration,
                                                          const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	if (UCadenceArcAbilityExecutorComponent* Executor = FindExecutor(MeshComp))
	{
		Executor->OpenBufferWindowForAnimation(Animation, GetMontageInstanceId(EventReference));
	}
}

void UAnimNotifyState_CadenceArcBufferWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                                        const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (UCadenceArcAbilityExecutorComponent* Executor = FindExecutor(MeshComp))
	{
		Executor->CloseBufferWindowForAnimation(Animation, GetMontageInstanceId(EventReference));
	}
}

FString UAnimNotifyState_CadenceArcBufferWindow::GetNotifyName_Implementation() const
{
	return TEXT("CadenceArc Buffer Window");
}
