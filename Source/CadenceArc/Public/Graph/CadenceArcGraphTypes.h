#pragma once
#include "GameplayTagContainer.h"
#include "Input/CadenceArcInputTypes.h"
#include "CadenceArcGraphTypes.generated.h"

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcTransition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag TargetActionTag;

	// 仅有当 InputPhase == Released 时，才会使用 DurationRange 进行判断
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	ECadenceArcInputPhase InputPhase = ECadenceArcInputPhase::Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	bool bUseDurationRange = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FCadenceArcHeldDurationRange DurationRange;

	bool operator ==(const FCadenceArcTransition& Others) const
	{
		bool IsEqual = false;
		// 当 InputPhase == Pressed 时，DurationRange 不参与比较
		if (InputPhase == ECadenceArcInputPhase::Pressed)
		{
			IsEqual = InputTag == Others.InputTag && TargetActionTag == Others.TargetActionTag
				&& InputPhase == Others.InputPhase && bUseDurationRange == Others.bUseDurationRange;
		}
		else
		{
			IsEqual = InputTag == Others.InputTag && TargetActionTag == Others.TargetActionTag
				&& InputPhase == Others.InputPhase && bUseDurationRange == Others.bUseDurationRange
				&& DurationRange == Others.DurationRange;
		}
		return IsEqual;
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcReleaseGestureConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag InputTag;
	// 整个按住过程进入蓄力保护的时长；0表示立即保护，不代表没有普通攻击边。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	double ChargeStartSeconds = 0.0;
	// 最高档达标后允许继续按住的时长；0表示最高档达标时立即自动释放。
	// 最高档门槛由同Tag、Released、无上限边的下限推导，较低档不单独自动释放。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	double MaxChargedHoldSeconds = 0.0;

	bool operator ==(const FCadenceArcReleaseGestureConfig& Others) const
	{
		return InputTag == Others.InputTag && ChargeStartSeconds == Others.ChargeStartSeconds
			&& MaxChargedHoldSeconds == Others.MaxChargedHoldSeconds;
	}

	bool IsValid() const
	{
		return InputTag.IsValid() && ChargeStartSeconds >= 0.0 && MaxChargedHoldSeconds >= 0.0
			&& FMath::IsFinite(ChargeStartSeconds) && FMath::IsFinite(MaxChargedHoldSeconds);
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcNode
{
	GENERATED_BODY()

	// 该节点对应的动作标签
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag ActionTag;

	// 每个Tag最多一份整体计时配置；同Tag可有多条Released边，表示多个攻击档位。
	// 仅按松手时长选择攻击、不需要蓄力保护或自动释放时，可以不填写配置。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	TArray<FCadenceArcReleaseGestureConfig> ReleaseGestureConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	TArray<FCadenceArcTransition> Transitions;

	bool operator ==(const FCadenceArcNode& Others) const
	{
		return ActionTag == Others.ActionTag && ReleaseGestureConfig == Others.ReleaseGestureConfig
			&& Transitions == Others.Transitions;
	}

	// 校验节点内的阶段、区间歧义与蓄力配置关联；目标节点是否存在由图校验负责。
	// 可选输出会先清空；诊断按数组顺序生成，不修改配置，也不要求区间连续。
	bool IsValidTransition(TArray<FText>* OutErrors = nullptr) const;
};
