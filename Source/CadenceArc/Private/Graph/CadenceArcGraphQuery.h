#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Resolver/CadenceArcResolverEnums.h"

class UCadenceArcGraph;
struct FCadenceArcInputEvent;
struct FCadenceArcTransition;

namespace CadenceArc::GraphQuery
{
	// 找边的结果。Reason == None 才表示找到唯一候选，此时 TargetActionTag 有效。
	struct FTransitionMatch
	{
		ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::NoMatchingTransition;
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
