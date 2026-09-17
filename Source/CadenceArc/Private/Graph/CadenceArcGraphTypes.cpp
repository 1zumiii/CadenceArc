#include "Graph/CadenceArcGraphTypes.h"

namespace
{
	bool IsKnownPhase(const ECadenceArcInputPhase Phase)
	{
		return Phase == ECadenceArcInputPhase::Pressed || Phase == ECadenceArcInputPhase::Released;
	}

	bool DoReleasedRangesOverlap(const FCadenceArcTransition& A, const FCadenceArcTransition& B)
	{
		// 未启用范围的Released边接受所有合法时长，相当于[0, 无上限)。
		const double MinA = A.bUseDurationRange ? A.DurationRange.MinHeldDurationSeconds : 0.0;
		const double MinB = B.bUseDurationRange ? B.DurationRange.MinHeldDurationSeconds : 0.0;
		const bool bHasMaxA = A.bUseDurationRange && A.DurationRange.bHasMaxHeldDuration;
		const bool bHasMaxB = B.bUseDurationRange && B.DurationRange.bHasMaxHeldDuration;
		// 上限不包含在区间内，所以MaxA == MinB只表示相接，不表示重叠。
		return (!bHasMaxA || MinB < A.DurationRange.MaxHeldDurationSecondsExclusive)
			&& (!bHasMaxB || MinA < B.DurationRange.MaxHeldDurationSecondsExclusive);
	}
}

bool FCadenceArcNode::IsValidTransition(TArray<FText>* OutErrors) const
{
	if (OutErrors)
	{
		OutErrors->Reset();
	}
	bool bValid = true;
	const auto AddError = [&](const FString& Message)
	{
		bValid = false;
		if (OutErrors)
		{
			OutErrors->Add(FText::FromString(FString::Printf(TEXT("Node '%s': %s"),
				*ActionTag.ToString(), *Message)));
		}
	};

	for (int32 Index = 0; Index < Transitions.Num(); ++Index)
	{
		const FCadenceArcTransition& Edge = Transitions[Index];
		if (!Edge.InputTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Transition at index %d has an invalid InputTag."), Index));
		}
		if (!Edge.TargetActionTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Transition at index %d has an invalid TargetActionTag."), Index));
		}
		if (!IsKnownPhase(Edge.InputPhase))
		{
			AddError(FString::Printf(TEXT("Transition at index %d has an invalid InputPhase."), Index));
			continue;
		}
		if (Edge.InputPhase == ECadenceArcInputPhase::Pressed && Edge.bUseDurationRange)
		{
			AddError(FString::Printf(TEXT("Pressed transition at index %d cannot enable a duration range."), Index));
		}
		if (Edge.bUseDurationRange && !Edge.DurationRange.IsValid())
		{
			AddError(FString::Printf(TEXT("Transition at index %d has an invalid duration range."), Index));
			continue;
		}

		// 两两比较无需排序原数组；同时保证输入顺序不会改变合法性或诊断顺序。
		for (int32 PreviousIndex = 0; PreviousIndex < Index; ++PreviousIndex)
		{
			const FCadenceArcTransition& Previous = Transitions[PreviousIndex];
			if (!Edge.InputTag.IsValid() || Edge.InputTag != Previous.InputTag
				|| Edge.InputPhase != Previous.InputPhase
				|| (Previous.bUseDurationRange && !Previous.DurationRange.IsValid()))
			{
				continue;
			}
			if (Edge.InputPhase == ECadenceArcInputPhase::Pressed || DoReleasedRangesOverlap(Previous, Edge))
			{
				AddError(FString::Printf(
					TEXT("Transitions at indices %d and %d overlap for InputTag '%s' and InputPhase %d."),
					PreviousIndex, Index, *Edge.InputTag.ToString(), static_cast<int32>(Edge.InputPhase)));
			}
		}
	}

	for (int32 ConfigIndex = 0; ConfigIndex < HoldChargeConfigs.Num(); ++ConfigIndex)
	{
		const FCadenceArcHoldChargeConfig& Config = HoldChargeConfigs[ConfigIndex];
		if (!Config.IsValid())
		{
			AddError(FString::Printf(TEXT("HoldChargeConfigs at index %d has invalid fields."), ConfigIndex));
		}
		if (!Config.InputTag.IsValid())
		{
			continue;
		}
		for (int32 PreviousIndex = 0; PreviousIndex < ConfigIndex; ++PreviousIndex)
		{
			if (HoldChargeConfigs[PreviousIndex].InputTag == Config.InputTag)
			{
				AddError(FString::Printf(
					TEXT("Duplicate HoldChargeConfigs at indices %d and %d for InputTag '%s'."),
					PreviousIndex, ConfigIndex, *Config.InputTag.ToString()));
			}
		}

		int32 FullRangeCount = 0;
		double FullDuration = 0.0;
		for (int32 EdgeIndex = 0; EdgeIndex < Transitions.Num(); ++EdgeIndex)
		{
			const FCadenceArcTransition& Edge = Transitions[EdgeIndex];
			if (Edge.InputTag != Config.InputTag || Edge.InputPhase != ECadenceArcInputPhase::Released)
			{
				continue;
			}
			if (!Edge.bUseDurationRange)
			{
				AddError(FString::Printf(
					TEXT("HoldChargeConfigs at index %d requires a duration range on transition at index %d."),
					ConfigIndex, EdgeIndex));
				continue;
			}
			if (Edge.DurationRange.IsValid() && !Edge.DurationRange.bHasMaxHeldDuration)
			{
				++FullRangeCount;
				FullDuration = Edge.DurationRange.MinHeldDurationSeconds;
			}
		}

		// 无上限尾段的下限就是最高档达标时间；普通边、中间档以及连续覆盖都不是必需的。
		// 没有整体计时配置时，上述限制不适用，短按单边和有空档的Released分支仍然合法。
		if (FullRangeCount != 1 || FullDuration <= 0.0)
		{
			AddError(FString::Printf(
				TEXT("HoldChargeConfigs at index %d for InputTag '%s' requires exactly one unbounded Released range with a positive minimum."),
				ConfigIndex, *Config.InputTag.ToString()));
		}
		else if (Config.IsValid())
		{
			if (Config.ChargeStartSeconds >= FullDuration)
			{
				AddError(FString::Printf(
					TEXT("HoldChargeConfigs at index %d: ChargeStartSeconds must be less than the highest-tier minimum."),
					ConfigIndex));
			}
			// 两个字段各自有限，求和仍可能溢出；拒绝无法表达的自动释放时长。
			if (!FMath::IsFinite(FullDuration + Config.MaxChargedHoldSeconds))
			{
				AddError(FString::Printf(
					TEXT("HoldChargeConfigs at index %d: highest-tier minimum plus MaxChargedHoldSeconds must be finite."),
					ConfigIndex));
			}
		}
	}
	return bValid;
}
