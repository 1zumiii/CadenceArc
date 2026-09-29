#include "CadenceArcDebugView.h"

#include "Layout/CadenceArcGraphLayout.h"
#include "Resolver/CadenceArcResolver.h"

namespace
{
	int32 FindNodeIndex(const FCadenceArcGraphLayout& Layout, const FGameplayTag& ActionTag)
	{
		for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
		{
			if (Layout.Nodes[NodeIndex].ActionTag == ActionTag)
			{
				return NodeIndex;
			}
		}
		return INDEX_NONE;
	}

	// 源→目标且 InputTag 相符的边恰好一条时返回它；没有或多于一条时返回 INDEX_NONE，不猜。
	int32 FindUniqueCandidateEdge(
		const FCadenceArcGraphLayout& Layout, const int32 SourceIndex, const int32 TargetIndex,
		const FGameplayTag& InputTag)
	{
		if (SourceIndex == INDEX_NONE || TargetIndex == INDEX_NONE)
		{
			return INDEX_NONE;
		}

		int32 MatchingEdgeIndex = INDEX_NONE;
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (Edge.SourceNodeIndex != SourceIndex ||
				Edge.TargetNodeIndex != TargetIndex ||
				Edge.Transition.InputTag != InputTag)
			{
				continue;
			}

			if (MatchingEdgeIndex != INDEX_NONE)
			{
				// 发现第二条：无法知道请求走的是哪条边。
				return INDEX_NONE;
			}

			MatchingEdgeIndex = EdgeIndex;
		}
		return MatchingEdgeIndex;
	}
}

FCadenceArcDebugView BuildDebugView(const UCadenceArcResolver& InResolver, const FCadenceArcGraphLayout& InLayout)
{
	FCadenceArcDebugView DebugView;
	if (!InResolver.IsInitialized())
	{
		return DebugView;
	}

	DebugView.ResolverState = InResolver.GetState();
	DebugView.OutstandingRequest = InResolver.GetOutstandingRequest();
	DebugView.CommittedNodeIndex = FindNodeIndex(InLayout, InResolver.GetCurrentActionTag());

	if (DebugView.ResolverState == ECadenceArcResolverState::AwaitingStart)
	{
		const FCadenceArcActionRequest& Request = DebugView.OutstandingRequest;
		DebugView.CandidateTargetNodeIndex = FindNodeIndex(InLayout, Request.TargetActionTag);
		DebugView.CandidateEdgeIndex = FindUniqueCandidateEdge(
			InLayout, FindNodeIndex(InLayout, Request.SourceActionTag), DebugView.CandidateTargetNodeIndex,
			Request.InputTag);
	}

	DebugView.bBufferWindowOpen = InResolver.IsBufferWindowOpen();
	DebugView.BufferedInputTag = InResolver.GetBufferedInputTag();
	DebugView.HoldSnapshot = InResolver.GetInputHoldSnapshot();
	return DebugView;
}
