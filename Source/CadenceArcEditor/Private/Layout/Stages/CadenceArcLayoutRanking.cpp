// 布局第 1、2 步：读图、从入口深度优先标出回边和可达性、最长路径列号，以及引用边的判定。

#include "Layout/Stages/CadenceArcLayoutBuild.h"

#include "Graph/CadenceArcGraph.h"

namespace CadenceArc::Editor::LayoutBuild
{
	namespace
	{
		void VisitDepthFirst(const int32 NodeIndex, FBuildContext& Context)
		{
			Context.Visit[NodeIndex] = 1;
			Context.PreOrder[NodeIndex] = Context.NextPreOrder++;
			for (const int32 EdgeIndex : Context.OutEdges[NodeIndex])
			{
				FCadenceArcLayoutEdge& Edge = Context.Layout.Edges[EdgeIndex];
				const int32 Target = Edge.TargetNodeIndex;
				if (Target == INDEX_NONE)
				{
					continue;
				}
				if (Context.Visit[Target] == 1)
				{
					Edge.bIsBackEdge = true; // 自环也在这里：目标就是自己，正在路径上
				}
				else if (Context.Visit[Target] == 0)
				{
					VisitDepthFirst(Target, Context);
				}
			}
			Context.Visit[NodeIndex] = 2;
			Context.PostOrder.Add(NodeIndex);
		}
	}

	bool FBuildContext::IsForwardEdge(const FCadenceArcLayoutEdge& Edge) const
	{
		return Edge.TargetNodeIndex != INDEX_NONE && !Edge.bIsBackEdge
			&& Layout.Nodes[Edge.SourceNodeIndex].bReachable && Layout.Nodes[Edge.TargetNodeIndex].bReachable;
	}

	void ReadGraph(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		FCadenceArcGraphLayout& Layout = Context.Layout;
		Context.NumNodes = Context.Graph.Nodes.Num();
		Layout.Nodes.Reserve(Context.NumNodes);
		for (int32 NodeIndex = 0; NodeIndex < Context.NumNodes; ++NodeIndex)
		{
			const FCadenceArcNode& Node = Context.Graph.Nodes[NodeIndex];
			FCadenceArcLayoutNode LayoutNode;
			LayoutNode.NodeIndex = NodeIndex;
			LayoutNode.ActionTag = Node.ActionTag;
			LayoutNode.NumPorts = Node.Transitions.Num();
			LayoutNode.Size = FVector2D(
				Params.NodeWidth,
				LayoutNode.NumPorts > 0
					? Params.HeaderHeight + LayoutNode.NumPorts * Params.PortHeight + Params.NodeBottomPad
					: Params.HeaderHeight);
			Layout.Nodes.Add(LayoutNode);
			// 资产在 PIE 中被改坏时也保持确定性：重复 Tag 始终指向数组中的第一个节点。
			if (!Context.NodeIndexMap.Contains(Node.ActionTag))
			{
				Context.NodeIndexMap.Add(Node.ActionTag, NodeIndex);
			}
		}
		if (const int32* EntryIndex = Context.NodeIndexMap.Find(Context.Graph.EntryActionTag))
		{
			Context.EntryIndex = *EntryIndex;
		}

		// 边按原数组顺序收集，包含自环、平行边和找不到目标的坏边。
		Context.OutEdges.SetNum(Context.NumNodes);
		for (int32 SourceIndex = 0; SourceIndex < Context.NumNodes; ++SourceIndex)
		{
			const TArray<FCadenceArcTransition>& Transitions = Context.Graph.Nodes[SourceIndex].Transitions;
			for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num(); ++TransitionIndex)
			{
				FCadenceArcLayoutEdge Edge;
				Edge.SourceNodeIndex = SourceIndex;
				Edge.TransitionIndex = TransitionIndex;
				Edge.PortSlot = TransitionIndex; // 重排端口时由 AssignPortSlots 改写
				Edge.Transition = Transitions[TransitionIndex];
				if (const int32* TargetIndex = Context.NodeIndexMap.Find(Transitions[TransitionIndex].TargetActionTag))
				{
					Edge.TargetNodeIndex = *TargetIndex;
				}
				Context.OutEdges[SourceIndex].Add(Layout.Edges.Add(Edge));
			}
		}
	}

	void MarkBackEdgesAndReachability(FBuildContext& Context)
	{
		Context.Visit.Init(0, Context.NumNodes);
		Context.PreOrder.Init(INDEX_NONE, Context.NumNodes);
		if (Context.EntryIndex != INDEX_NONE)
		{
			VisitDepthFirst(Context.EntryIndex, Context);
		}
		for (FCadenceArcLayoutNode& Node : Context.Layout.Nodes)
		{
			Node.bReachable = Context.Visit[Node.NodeIndex] != 0;
		}
	}

	void AssignColumns(FBuildContext& Context)
	{
		// 链内边长度为 0（共享一列），其余前向边长度为 1。
		// 逆后序仍是原图去掉回边后的拓扑序；分层模式没有分组，保留原有列号。
		FCadenceArcGraphLayout& Layout = Context.Layout;
		TArray<TArray<int32>> NodePredecessors;
		NodePredecessors.SetNum(Context.NumNodes);
		for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
		{
			if (Context.IsForwardEdge(Edge))
			{
				NodePredecessors[Edge.TargetNodeIndex].Add(Edge.SourceNodeIndex);
			}
		}
		for (int32 Index = Context.PostOrder.Num() - 1; Index >= 0; --Index)
		{
			FCadenceArcLayoutNode& Node = Layout.Nodes[Context.PostOrder[Index]];
			for (const int32 Predecessor : NodePredecessors[Node.NodeIndex])
			{
				const bool bSameChain = Context.ChainOfNode[Node.NodeIndex] != INDEX_NONE
					&& Context.ChainOfNode[Node.NodeIndex] == Context.ChainOfNode[Predecessor];
				Node.Column = FMath::Max(Node.Column, Layout.Nodes[Predecessor].Column + (bSameChain ? 0 : 1));
			}
			Context.NumReachableColumns = FMath::Max(Context.NumReachableColumns, Node.Column + 1);
		}
	}

	void MarkReferences(FBuildContext& Context)
	{
		// 不可达节点按它们稍后所在的最后一列算。引用边仍参与上面的列号计算，
		// 只是不再占通道，也不参与排序和纵向对齐。自环的列号差为 0，坏目标没有目标，都不会成为引用。
		if (Context.Params.ReferenceMinSpan <= 0)
		{
			return;
		}
		const FCadenceArcGraphLayout& Layout = Context.Layout;
		const int32 NumReachableColumns = Context.NumReachableColumns;
		const auto FinalColumn = [&Layout, NumReachableColumns](const int32 NodeIndex)
		{
			const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
			return Node.bReachable ? Node.Column : NumReachableColumns;
		};
		for (FCadenceArcLayoutEdge& Edge : Context.Layout.Edges)
		{
			Edge.bIsReference = !Edge.IsBrokenTarget() && FMath::Abs(
				FinalColumn(Edge.TargetNodeIndex) - FinalColumn(Edge.SourceNodeIndex)) >= Context.Params.ReferenceMinSpan;
		}
	}
}
