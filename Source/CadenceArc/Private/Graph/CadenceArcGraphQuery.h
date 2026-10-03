#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UCadenceArcGraph;
struct FCadenceArcInputEvent;
struct FCadenceArcTransition;

namespace CadenceArc::GraphQuery
{
	// 图查询自己的结果，不认识解析器的失败原因；由 Resolver 翻译成 ECadenceArcResolutionReason。
	enum class EMatchResult : uint8
	{
		Found, // 唯一候选，TargetActionTag 有效
		SourceNodeNotFound, // 当前图里没有源节点
		NoMatchingInput, // 没有 Tag、阶段和按住时长都对得上的边
		ConditionNotMet, // 有对得上的边，但上下文或停顿条件全部不满足
		Ambiguous, // 满足条件的边里，最高优先级有多条
		TargetNodeNotFound, // 选中的边指向当前图里不存在的节点
	};

	struct FTransitionMatch
	{
		EMatchResult Result = EMatchResult::NoMatchingInput;
		FGameplayTag TargetActionTag;
	};

	// 纯查询，不修改任何状态。
	// EdgesOverride 为空时读当前图的源节点；按住资格传入授予时复制的边，
	// 这样等待期间资产被改也不会改变本次判定。目标节点始终按当前图确认。
	// ContextTags 是调用方合并后的上下文；PauseDurationSeconds 为负表示没有停顿起点。
	// 缓冲消费传入停顿 0，正常匹配包含 0 的区间；没有起点与停顿 0 是不同情况。
	FTransitionMatch FindUniqueTransition(
		const UCadenceArcGraph& Graph,
		const FGameplayTag& SourceActionTag,
		const FCadenceArcInputEvent& Event,
		const FGameplayTagContainer& ContextTags,
		double PauseDurationSeconds,
		const TArray<FCadenceArcTransition>* EdgesOverride = nullptr
	);
}
