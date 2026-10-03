#include "CadenceArcGraphQuery.h"

#include "Graph/CadenceArcGraph.h"
#include "Graph/CadenceArcGraphTypes.h"

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
		FTransitionMatch Result; // 默认 NoMatchingInput

		// 1. 决定用哪组边
		const TArray<FCadenceArcTransition>* Edges = EdgesOverride;
		if (!Edges)
		{
			const FCadenceArcNode* SourceNode = Graph.FindAction(SourceActionTag);
			if (!SourceNode)
			{
				Result.Result = EMatchResult::SourceNodeNotFound;
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
			Result.Result = bHasMatchingInput
				? EMatchResult::ConditionNotMet
				: EMatchResult::NoMatchingInput;
			return Result;
		}
		if (MatchCount > 1)
		{
			Result.Result = EMatchResult::Ambiguous;
			return Result;
		}

		// 3. 目标必须存在于当前图
		if (!Graph.ContainsAction(Matched->TargetActionTag))
		{
			Result.Result = EMatchResult::TargetNodeNotFound;
			return Result;
		}

		Result.Result = EMatchResult::Found;
		Result.TargetActionTag = Matched->TargetActionTag;
		return Result;
	}
}
