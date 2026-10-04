#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AnimNotifyState_CadenceArcBufferWindow.generated.h"

/**
 * 蒙太奇中的缓冲窗口：开始时打开当前 CadenceArc 动作的缓冲窗口，结束时关闭。
 * 通过所属 Actor 上的 UCadenceArcAbilityExecutorComponent 生效；没有这个组件时（例如动画预览）不做任何事。
 */
UCLASS(meta=(DisplayName="CadenceArc Buffer Window"))
class CADENCEARCGAS_API UAnimNotifyState_CadenceArcBufferWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
	                         const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	                       const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
