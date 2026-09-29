#include "CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"

// 分层布局（Sugiyama 的简化版），每一步都只依赖数组顺序，同一张图每次结果一致：
// 1. 从入口深度优先遍历，标出回边（指向当前递归路径上的节点），同时得到可达性和前序、后序；
// 2. 去掉回边后的图无环，按拓扑序求最长路径作为列号，所有剩余的边都严格指向右侧；
// 3. 同一列内按相邻节点的平均行号（重心）排序，上下交替扫描以减少交叉；
// 4. 目标列不在源列右侧的边分配底部通道，由画布从节点下方绕回。
namespace
{
	constexpr int32 NumOrderingPasses = 4;

	struct FDepthFirstState
	{
		TArray<uint8> Visit; // 0 未访问，1 在当前递归路径上，2 已完成
		TArray<int32> PreOrder; // 节点首次访问的序号，未访问为 INDEX_NONE
		TArray<int32> PostOrder; // 按完成顺序排列的节点
		int32 NextPreOrder = 0;
	};

	void VisitDepthFirst(
		const int32 NodeIndex, const TArray<TArray<int32>>& OutEdges,
		TArray<FCadenceArcLayoutEdge>& Edges, FDepthFirstState& State)
	{
		State.Visit[NodeIndex] = 1;
		State.PreOrder[NodeIndex] = State.NextPreOrder++;
		for (const int32 EdgeIndex : OutEdges[NodeIndex])
		{
			FCadenceArcLayoutEdge& Edge = Edges[EdgeIndex];
			const int32 Target = Edge.TargetNodeIndex;
			if (Target == INDEX_NONE)
			{
				continue;
			}
			if (State.Visit[Target] == 1)
			{
				Edge.bIsBackEdge = true; // 自环也在这里：目标就是自己，正在路径上
			}
			else if (State.Visit[Target] == 0)
			{
				VisitDepthFirst(Target, OutEdges, Edges, State);
			}
		}
		State.Visit[NodeIndex] = 2;
		State.PostOrder.Add(NodeIndex);
	}

	// 参与列号和排序的边：两端都可达、目标有效、不是回边
	bool IsForwardEdge(const FCadenceArcLayoutEdge& Edge, const TArray<FCadenceArcLayoutNode>& Nodes)
	{
		return Edge.TargetNodeIndex != INDEX_NONE && !Edge.bIsBackEdge
			&& Nodes[Edge.SourceNodeIndex].bReachable && Nodes[Edge.TargetNodeIndex].bReachable;
	}

	// 一次扫描：按相邻节点平均行号重排每一列。没有相邻节点的保持当前位置作为排序键。
	// bDown 为真时看前驱（从左往右扫），否则看后继（从右往左扫）。
	void OrderColumnsByBarycenter(
		TArray<TArray<int32>>& Columns, TArray<int32>& Position,
		const TArray<TArray<int32>>& Neighbors, const bool bDown)
	{
		const int32 NumColumns = Columns.Num();
		for (int32 Step = 0; Step < NumColumns; ++Step)
		{
			TArray<int32>& Column = Columns[bDown ? Step : NumColumns - 1 - Step];
			TArray<TPair<double, int32>> Keys; // (重心, 当前位置)
			Keys.Reserve(Column.Num());
			for (const int32 NodeIndex : Column)
			{
				double Sum = 0.0;
				for (const int32 Neighbor : Neighbors[NodeIndex])
				{
					Sum += Position[Neighbor];
				}
				const double Key = Neighbors[NodeIndex].IsEmpty()
					? Position[NodeIndex]
					: Sum / Neighbors[NodeIndex].Num();
				Keys.Emplace(Key, Position[NodeIndex]);
			}
			TArray<int32> Order;
			for (int32 Index = 0; Index < Column.Num(); ++Index)
			{
				Order.Add(Index);
			}
			// 键相同按当前位置，位置在同一列内唯一，所以排序结果完全确定
			Order.Sort([&Keys](const int32 A, const int32 B)
			{
				return Keys[A].Key != Keys[B].Key ? Keys[A].Key < Keys[B].Key : Keys[A].Value < Keys[B].Value;
			});
			TArray<int32> Sorted;
			Sorted.Reserve(Column.Num());
			for (const int32 Index : Order)
			{
				Sorted.Add(Column[Index]);
			}
			Column = MoveTemp(Sorted);
			for (int32 Row = 0; Row < Column.Num(); ++Row)
			{
				Position[Column[Row]] = Row;
			}
		}
	}
}

FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph)
{
	FCadenceArcGraphLayout Layout;
	const int32 NumNodes = Graph.Nodes.Num();
	TMap<FGameplayTag, int32> NodeIndexMap;
	Layout.Nodes.Reserve(NumNodes);
	for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
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

	// 边按原数组顺序收集，包含自环、平行边和找不到目标的坏边。
	TArray<TArray<int32>> OutEdges;
	OutEdges.SetNum(NumNodes);
	for (int32 SourceIndex = 0; SourceIndex < NumNodes; ++SourceIndex)
	{
		const TArray<FCadenceArcTransition>& Transitions = Graph.Nodes[SourceIndex].Transitions;
		for (int32 TransitionIndex = 0; TransitionIndex < Transitions.Num(); ++TransitionIndex)
		{
			FCadenceArcLayoutEdge Edge;
			Edge.SourceNodeIndex = SourceIndex;
			Edge.TransitionIndex = TransitionIndex;
			Edge.Transition = Transitions[TransitionIndex];
			if (const int32* TargetIndex = NodeIndexMap.Find(Transitions[TransitionIndex].TargetActionTag))
			{
				Edge.TargetNodeIndex = *TargetIndex;
			}
			OutEdges[SourceIndex].Add(Layout.Edges.Add(Edge));
		}
	}

	// 1. 深度优先：回边、可达性、前序
	FDepthFirstState State;
	State.Visit.Init(0, NumNodes);
	State.PreOrder.Init(INDEX_NONE, NumNodes);
	if (const int32* EntryIndex = NodeIndexMap.Find(Graph.EntryActionTag))
	{
		VisitDepthFirst(*EntryIndex, OutEdges, Layout.Edges, State);
	}
	for (FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		Node.bReachable = State.Visit[Node.NodeIndex] != 0;
	}

	// 2. 最长路径列号：逆后序就是去掉回边后的拓扑序，前驱一定先于后继处理
	TArray<TArray<int32>> Predecessors;
	TArray<TArray<int32>> Successors;
	Predecessors.SetNum(NumNodes);
	Successors.SetNum(NumNodes);
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		if (IsForwardEdge(Edge, Layout.Nodes))
		{
			Predecessors[Edge.TargetNodeIndex].Add(Edge.SourceNodeIndex);
			Successors[Edge.SourceNodeIndex].Add(Edge.TargetNodeIndex);
		}
	}
	int32 NumReachableColumns = 0;
	for (int32 Index = State.PostOrder.Num() - 1; Index >= 0; --Index)
	{
		FCadenceArcLayoutNode& Node = Layout.Nodes[State.PostOrder[Index]];
		for (const int32 Predecessor : Predecessors[Node.NodeIndex])
		{
			Node.Column = FMath::Max(Node.Column, Layout.Nodes[Predecessor].Column + 1);
		}
		NumReachableColumns = FMath::Max(NumReachableColumns, Node.Column + 1);
	}

	// 3. 列内排序：初始按前序，再做重心扫描
	TArray<TArray<int32>> Columns;
	Columns.SetNum(NumReachableColumns);
	TArray<int32> ReachableByPreOrder;
	for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (Node.bReachable)
		{
			ReachableByPreOrder.Add(Node.NodeIndex);
		}
	}
	ReachableByPreOrder.Sort([&State](const int32 A, const int32 B)
	{
		return State.PreOrder[A] < State.PreOrder[B];
	});
	TArray<int32> Position;
	Position.Init(0, NumNodes);
	for (const int32 NodeIndex : ReachableByPreOrder)
	{
		TArray<int32>& Column = Columns[Layout.Nodes[NodeIndex].Column];
		Position[NodeIndex] = Column.Add(NodeIndex);
	}
	for (int32 Pass = 0; Pass < NumOrderingPasses; ++Pass)
	{
		const bool bDown = Pass % 2 == 0;
		OrderColumnsByBarycenter(Columns, Position, bDown ? Predecessors : Successors, bDown);
	}
	for (const TArray<int32>& Column : Columns)
	{
		Layout.MaxRows = FMath::Max(Layout.MaxRows, Column.Num());
		for (const int32 NodeIndex : Column)
		{
			Layout.Nodes[NodeIndex].Row = Position[NodeIndex];
		}
	}

	// 不可达节点放在最深可达列之后，行号按数组顺序；没有入口时从第 0 列开始。
	int32 UnreachableRow = 0;
	for (FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (!Node.bReachable)
		{
			Node.Column = NumReachableColumns;
			Node.Row = UnreachableRow++;
		}
	}
	Layout.NumColumns = NumReachableColumns + (UnreachableRow > 0 ? 1 : 0);
	Layout.MaxRows = FMath::Max(Layout.MaxRows, UnreachableRow);

	// 4. 底部通道：跨度短的放在上面（离节点近），跨度相同按边序号
	TArray<int32> ReturnEdges;
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.TargetNodeIndex != INDEX_NONE && Edge.TargetNodeIndex != Edge.SourceNodeIndex
			&& Layout.Nodes[Edge.TargetNodeIndex].Column <= Layout.Nodes[Edge.SourceNodeIndex].Column)
		{
			ReturnEdges.Add(EdgeIndex);
		}
	}
	const auto Span = [&Layout](const int32 EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		return Layout.Nodes[Edge.SourceNodeIndex].Column - Layout.Nodes[Edge.TargetNodeIndex].Column;
	};
	ReturnEdges.Sort([&Span](const int32 A, const int32 B)
	{
		return Span(A) != Span(B) ? Span(A) < Span(B) : A < B;
	});
	for (int32 Lane = 0; Lane < ReturnEdges.Num(); ++Lane)
	{
		Layout.Edges[ReturnEdges[Lane]].ReturnLane = Lane;
	}
	Layout.NumReturnLanes = ReturnEdges.Num();

	return Layout;
}
