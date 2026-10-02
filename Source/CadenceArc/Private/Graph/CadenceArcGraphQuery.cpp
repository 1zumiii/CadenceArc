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
		const FGameplayTagContainer& ContextTags,
		const double PauseDurationSeconds,
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

		// 2. 先过滤输入和条件，再统计最高优先级；低优先级打平不影响最终选择。
		const FCadenceArcTransition* Matched = nullptr;
		int32 MatchCount = 0;
		bool bHasMatchingInput = false;
		for (const FCadenceArcTransition& Edge : *Edges)
		{
			if (!Edge.Matches(Event))
			{
				continue;
			}
			bHasMatchingInput = true;
			if (!ContextTags.HasAll(Edge.RequiredContextTags)
				|| ContextTags.HasAny(Edge.BlockedContextTags)
				|| (Edge.bUsePauseRange && !Edge.PauseRange.Contains(PauseDurationSeconds)))
			{
				continue;
			}
			if (!Matched || Edge.Priority > Matched->Priority)
			{
				Matched = &Edge;
				MatchCount = 1;
			}
			else if (Edge.Priority == Matched->Priority)
			{
				++MatchCount;
			}
		}
		if (MatchCount == 0)
		{
			Result.Reason = bHasMatchingInput
				? ECadenceArcResolutionReason::ConditionNotMet
				: ECadenceArcResolutionReason::NoMatchingTransition;
			return Result;
		}
		if (MatchCount > 1)
		{
			Result.Reason = ECadenceArcResolutionReason::AmbiguousTransition;
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
