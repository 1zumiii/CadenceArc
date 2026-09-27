#include "CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"

namespace
{
	// 每次递归处理完整的一层。按单个节点递归会变成 DFS，不能保证最短距离。
	void LayoutReachableLayer(
		const UCadenceArcGraph& Graph,
		const TMap<FGameplayTag, int32>& NodeIndexMap,
		FCadenceArcGraphLayout& Layout,
		TSet<int32>& Visited,
		const TArray<int32>& CurrentLayer,
		const int32 Column,
		TArray<int32>& RowsPerColumn)
	{
		if (CurrentLayer.IsEmpty())
		{
			return;
		}

		RowsPerColumn.Add(CurrentLayer.Num());
		TArray<int32> NextLayer;
		for (const int32 SourceIndex : CurrentLayer)
		{
			for (const FCadenceArcTransition& Transition : Graph.Nodes[SourceIndex].Transitions)
			{
				const int32* TargetIndex = NodeIndexMap.Find(Transition.TargetActionTag);
				if (!TargetIndex || Visited.Contains(*TargetIndex))
				{
					continue;
				}

				Visited.Add(*TargetIndex); // 入队前标记，自环、回边和平行边不会重复排布。
				FCadenceArcLayoutNode& Target = Layout.Nodes[*TargetIndex];
				Target.bReachable = true;
				Target.Column = Column + 1;
				Target.Row = NextLayer.Num();
				NextLayer.Add(*TargetIndex);
			}
		}

		LayoutReachableLayer(Graph, NodeIndexMap, Layout, Visited, NextLayer, Column + 1, RowsPerColumn);
	}

	void LayoutReachableNodes(
		const UCadenceArcGraph& Graph,
		const TMap<FGameplayTag, int32>& NodeIndexMap,
		FCadenceArcGraphLayout& Layout,
		TArray<int32>& RowsPerColumn)
	{
		const int32* EntryIndex = NodeIndexMap.Find(Graph.EntryActionTag);
		if (!EntryIndex)
		{
			return;
		}

		TSet<int32> Visited;
		Visited.Add(*EntryIndex);
		FCadenceArcLayoutNode& Entry = Layout.Nodes[*EntryIndex];
		Entry.bReachable = true;
		Entry.Column = 0;
		Entry.Row = 0;
		TArray<int32> EntryLayer;
		EntryLayer.Add(*EntryIndex);
		LayoutReachableLayer(Graph, NodeIndexMap, Layout, Visited, EntryLayer, 0, RowsPerColumn);
	}
}

FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph)
{
	FCadenceArcGraphLayout Layout;
	TMap<FGameplayTag, int32> NodeIndexMap;
	Layout.Nodes.Reserve(Graph.Nodes.Num());
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcNode& Node = Graph.Nodes[NodeIndex];
		FCadenceArcLayoutNode LayoutNode;
		LayoutNode.NodeIndex = NodeIndex;
		LayoutNode.ActionTag = Node.ActionTag;
		Layout.Nodes.Add(LayoutNode);
		// 资产在 PIE 中被改坏时也保持确定性：重复 Tag 始终指向数组中的第一个节点。
		if (!NodeIndexMap.Contains(Node.ActionTag))
		{
			NodeIndexMap.Add(Node.ActionTag, NodeIndex);
		}
	}

	TArray<int32> RowsPerColumn;
	LayoutReachableNodes(Graph, NodeIndexMap, Layout, RowsPerColumn);

	// 没有入口时从第 0 列开始；否则把不可达节点放在最深可达列之后。
	const int32 UnreachableColumn = RowsPerColumn.Num();
	int32 UnreachableRow = 0;
	for (FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (!Node.bReachable)
		{
			Node.Column = UnreachableColumn;
			Node.Row = UnreachableRow++;
		}
	}

	Layout.NumColumns = RowsPerColumn.Num() + (UnreachableRow > 0 ? 1 : 0);
	Layout.MaxRows = UnreachableRow;
	for (const int32 RowCount : RowsPerColumn)
	{
		Layout.MaxRows = FMath::Max(Layout.MaxRows, RowCount);
	}

	// 边按原数组顺序收集，包含自环、平行边和找不到目标的坏边。
	for (int32 SourceIndex = 0; SourceIndex < Graph.Nodes.Num(); ++SourceIndex)
	{
		const TArray<FCadenceArcTransition>& Transitions = Graph.Nodes[SourceIndex].Transitions;
		for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num(); ++TransitionIndex)
		{
			FCadenceArcLayoutEdge Edge;
			Edge.SourceNodeIndex = SourceIndex;
			Edge.TransitionIndex = TransitionIndex;
			if (const int32* TargetIndex = NodeIndexMap.Find(Transitions[TransitionIndex].TargetActionTag))
			{
				Edge.TargetNodeIndex = *TargetIndex;
			}
			Layout.Edges.Add(Edge);
		}
	}

	return Layout;
}
