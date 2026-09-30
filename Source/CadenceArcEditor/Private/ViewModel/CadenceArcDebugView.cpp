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

	float GetPreparatoryProgress(const FCadenceArcTransition& Transition,
		const FCadenceArcHoldSnapshot& Hold, const double Held)
	{
		// 每条 Released 边只填自己的 [Min, Max)；最高档的可视终点取快照中的自动释放时间。
		const double Min = Transition.bUseDurationRange
			? Transition.DurationRange.MinHeldDurationSeconds : 0.0;
		const bool bHasMax = Transition.bUseDurationRange && Transition.DurationRange.bHasMaxHeldDuration;
		if (!bHasMax && !Hold.bHasChargeConfig)
		{
			// 没有自动截止时间时，无上限档位在达到下限的瞬间填满。
			return Held >= Min ? 1.f : 0.f;
		}
		const double Max = bHasMax
			? Transition.DurationRange.MaxHeldDurationSecondsExclusive
			: Hold.AutoReleaseTimestampSeconds - Hold.PressedTimestampSeconds;
		if (Max <= Min)
		{
			return Held >= Min ? 1.f : 0.f;
		}
		return static_cast<float>(FMath::Clamp((Held - Min) / (Max - Min), 0.0, 1.0));
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

	// 分支聚焦：从已提交节点沿出边做一次广度优先，得到每个节点还要几步才能走到
	if (DebugView.CommittedNodeIndex != INDEX_NONE)
	{
		DebugView.NodeDistance.Init(INDEX_NONE, InLayout.Nodes.Num());
		DebugView.NodeDistance[DebugView.CommittedNodeIndex] = 0;
		TArray<int32> Frontier = {DebugView.CommittedNodeIndex};
		for (int32 Head = 0; Head < Frontier.Num(); ++Head)
		{
			const int32 Source = Frontier[Head];
			for (const FCadenceArcLayoutEdge& Edge : InLayout.Edges)
			{
				if (Edge.SourceNodeIndex == Source && !Edge.IsBrokenTarget()
					&& DebugView.NodeDistance[Edge.TargetNodeIndex] == INDEX_NONE)
				{
					DebugView.NodeDistance[Edge.TargetNodeIndex] = DebugView.NodeDistance[Source] + 1;
					Frontier.Add(Edge.TargetNodeIndex);
				}
			}
		}
	}

	DebugView.bBufferWindowOpen = InResolver.IsBufferWindowOpen();
	DebugView.BufferedInputTag = InResolver.GetBufferedInputTag();
	DebugView.HoldSnapshot = InResolver.GetInputHoldSnapshot();
	DebugView.PreparatoryEdgeProgress.Init(-1.f, InLayout.Edges.Num());
	if (DebugView.HoldSnapshot.bHasHold)
	{
		const FCadenceArcHoldSnapshot& Hold = DebugView.HoldSnapshot;
		const int32 SourceIndex = FindNodeIndex(InLayout, Hold.SourceActionTag);
		const double Held = Hold.LastObservedTimestampSeconds - Hold.PressedTimestampSeconds;
		FCadenceArcInputEvent ReleaseEvent;
		ReleaseEvent.InputTag = Hold.InputTag;
		ReleaseEvent.InputPhase = ECadenceArcInputPhase::Released;
		ReleaseEvent.TimestampSeconds = Hold.LastObservedTimestampSeconds;
		ReleaseEvent.HeldDurationSeconds = Held;

		bool bMultipleMatches = false;
		for (int32 EdgeIndex = 0; EdgeIndex < InLayout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = InLayout.Edges[EdgeIndex];
			if (Edge.SourceNodeIndex != SourceIndex ||
				Edge.Transition.InputTag != Hold.InputTag ||
				Edge.Transition.InputPhase != ECadenceArcInputPhase::Released)
			{
				continue;
			}
			DebugView.PreparatoryEdgeProgress[EdgeIndex] = GetPreparatoryProgress(Edge.Transition, Hold, Held);
			if (Edge.Transition.Matches(ReleaseEvent))
			{
				if (DebugView.CurrentReleaseEdgeIndex != INDEX_NONE)
				{
					bMultipleMatches = true;
				}
				else
				{
					DebugView.CurrentReleaseEdgeIndex = EdgeIndex;
				}
			}
		}
		if (bMultipleMatches)
		{
			DebugView.CurrentReleaseEdgeIndex = INDEX_NONE;
		}
		if (DebugView.CurrentReleaseEdgeIndex != INDEX_NONE)
		{
			DebugView.CurrentReleaseTargetNodeIndex =
				InLayout.Edges[DebugView.CurrentReleaseEdgeIndex].TargetNodeIndex;
		}
	}
	return DebugView;
}
