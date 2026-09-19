#include "CadenceArcGraphQuery.h"

#include "Graph/CadenceArcGraph.h"
#include "Graph/CadenceArcGraphTypes.h"
#include "Resolver/CadenceArcResolverTypes.h"

namespace CadenceArc::GraphQuery
{
	FTransitionMatch FindUniqueTransition(
		const UCadenceArcGraph& Graph,
		const FGameplayTag& SourceActionTag,
		const FCadenceArcInputEvent& Event,
		const TArray<FCadenceArcTransition>* EdgesOverride)
	{
		FTransitionMatch Result; // 默认 NoMatchingTransition

		// 1. 决定用哪组边
		const TArray<FCadenceArcTransition>* Edges = EdgesOverride;
		if (!Edges)
		{
			const FCadenceArcNode* SourceNode = Graph.FindAction(SourceActionTag);
			if (!SourceNode)
			{
				Result.Reason = ECadenceArcResolutionReason::CurrentNodeNotFound;
				return Result;
			}
			Edges = &SourceNode->Transitions;
		}

		// 2. 统计候选，不提前 break：歧义必须被发现
		const FCadenceArcTransition* Matched = nullptr;
		int32 MatchCount = 0;
		for (const FCadenceArcTransition& Edge : *Edges)
		{
			if (Edge.Matches(Event))
			{
				Matched = &Edge;
				++MatchCount;
			}
		}
		if (MatchCount == 0)
		{
			return Result; // NoMatchingTransition
		}
		if (MatchCount > 1)
		{
			// 图校验禁止同源同 Tag 同 Phase 重叠，运行时出现歧义说明资产被改坏了
			Result.Reason = ECadenceArcResolutionReason::InvalidGraphConfiguration;
			return Result;
		}

		// 3. 目标必须存在于当前图
		if (!Graph.ContainsAction(Matched->TargetActionTag))
		{
			Result.Reason = ECadenceArcResolutionReason::TargetNodeNotFound;
			return Result;
		}

		Result.Reason = ECadenceArcResolutionReason::None;
		Result.TargetActionTag = Matched->TargetActionTag;
		return Result;
	}
}
