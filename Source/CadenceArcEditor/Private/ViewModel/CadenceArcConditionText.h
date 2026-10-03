#pragma once

// 转移条件（Phase 8）的显示文字和求值说明，画布的端口行和 Arc History 共用。纯函数，不读资产以外的状态。

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"

namespace CadenceArc::Editor::ConditionText
{
	// Tag 的最后一段：CadenceArc.Context.Dir.Forward -> Forward
	FString ShortTag(const FGameplayTag& Tag);

	// 上下文的简短列表："Forward, Air"；为空时返回 "none"
	FString FormatContext(const FGameplayTagContainer& Context);

	// 停顿区间："pause≥0.25s" / "pause<0.25s" / "pause 0.1–0.3s"
	FString FormatPauseRange(const FCadenceArcHeldDurationRange& Range);

	// 一条边的条件和优先级，接在条件文字后面："+Forward -Air pause≥0.25s #2"。没有条件、优先级为 0 时为空
	FString FormatConditions(const FCadenceArcTransition& Transition);

	// 这条边在给定的上下文和停顿下没满足的条件，例如 "needs Forward"、"blocked by Air"、"pause 0.12s < 0.25s"。
	// 全部满足时返回空数组。bHasPause 为 false 表示还没有停顿起点，带停顿区间的边一律不满足。
	TArray<FString> UnmetConditions(
		const FCadenceArcTransition& Transition, const FGameplayTagContainer& Context,
		bool bHasPause, double PauseSeconds);
}
