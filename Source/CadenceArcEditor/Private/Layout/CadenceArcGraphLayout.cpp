#include "CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"

// 分层布局（Sugiyama 的简化版），每一步都只依赖数组顺序，同一张图每次结果一致：
// 1. 从入口深度优先遍历，标出回边（指向当前递归路径上的节点），同时得到可达性和前序、后序；
// 2. 去掉回边后的图无环，按拓扑序求最长路径作为列号，所有剩余的边都严格指向右侧；
// 3. 跨多列的边在每个中间列放一个通道（不绘制的占位），和真实节点一起按重心排序以减少交叉；
// 4. 纵坐标按连线的端口位置双向对齐，每列保持顺序和最小间距（保序回归），通道同样占高度；
// 5. 目标列不在源列右侧的边分配底部通道；最后生成每条边的实际绘制折线。
// 打开引用标记（ReferenceMinSpan）时，跨列多的边在第 2 步之后改成引用：不占通道、不参与排序，只画两端的短标记。
// CompactChains 在分层前把简单链收成一个格子，链内成员纵向展开；外部沿用同一排序与走线算法。
// 因此紧凑模式的 Column 表示显示列，链内前进允许同列，不能再仅凭几何方向判定回边。
// 路径合法性来自结构：穿过中间列时是通道里的水平线，换高度的 S 曲线只出现在两列之间的空隙里，
// 回边和自环的竖线也只在空隙里，所以线不会压在无关节点上。
namespace
{
	constexpr int32 NumOrderingPasses = 4;
	constexpr int32 NumPlacementPasses = 6; // 下、上交替，以向上结束：入口等父节点最终居中于后继

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

	// 按真实入/出边计数，而不是去重后的邻居数：平行条件、坏目标、不可达来源都不能被悄悄藏进链。
	// 分组只接收一进一出的中间节点；入口、分叉、汇合及接触 DFS 回边的节点保留在外层。
	void FindCompactChains(FCadenceArcGraphLayout& Layout, const int32 EntryIndex)
	{
		TArray<TArray<int32>> Incoming;
		TArray<TArray<int32>> Outgoing;
		Incoming.SetNum(Layout.Nodes.Num());
		Outgoing.SetNum(Layout.Nodes.Num());
		for (int32 Index = 0; Index < Layout.Edges.Num(); ++Index)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[Index];
			Outgoing[Edge.SourceNodeIndex].Add(Index);
			if (!Edge.IsBrokenTarget())
			{
				Incoming[Edge.TargetNodeIndex].Add(Index);
			}
		}
		TArray<bool> Eligible;
		Eligible.Init(false, Layout.Nodes.Num());
		for (int32 Index = 0; Index < Layout.Nodes.Num(); ++Index)
		{
			Eligible[Index] = Index != EntryIndex && Layout.Nodes[Index].bReachable
				&& Incoming[Index].Num() == 1 && Outgoing[Index].Num() == 1
				&& IsForwardEdge(Layout.Edges[Incoming[Index][0]], Layout.Nodes)
				&& IsForwardEdge(Layout.Edges[Outgoing[Index][0]], Layout.Nodes);
		}
		for (int32 Index = 0; Index < Layout.Nodes.Num(); ++Index)
		{
			if (!Eligible[Index] || Eligible[Layout.Edges[Incoming[Index][0]].SourceNodeIndex])
			{
				continue; // 只从每段最大简单链的头部收集，数组次序不必等于执行次序。
			}
			FCadenceArcLayoutChain Chain;
			int32 Next = Index;
			while (Eligible[Next])
			{
				Chain.NodeIndices.Add(Next);
				Next = Layout.Edges[Outgoing[Next][0]].TargetNodeIndex;
			}
			if (Chain.NodeIndices.Num() >= 2)
			{
				Layout.FoldedChains.Add(MoveTemp(Chain));
			}
		}
	}

	// 分层后的一个格子：可达的真实节点，或长边在中间列的通道
	struct FLayerItem
	{
		int32 NodeIndex = INDEX_NONE; // 通道为 INDEX_NONE
		TArray<int32> Members; // 普通格子只有一个成员；紧凑链作为一个整体参与排序和保序回归。
		int32 EdgeIndex = INDEX_NONE; // 通道所属的边
		int32 Column = 0;
		double Height = 0.0;
		double Y = 0.0;
		bool bHasSelfLoop = false; // 自环从节点底部绕过，下方要多留一点
		bool IsChannel() const { return NodeIndex == INDEX_NONE; }
	};

	// 相邻两列之间的一段连线：From 在左列，To 在右列。偏移是连线在格子上的纵向位置
	struct FItemLink
	{
		int32 From = INDEX_NONE;
		int32 To = INDEX_NONE;
		double FromOffset = 0.0;
		double ToOffset = 0.0;
	};

	// 一次扫描：按相邻格子平均位置重排每一列。没有相邻格子的保持当前位置作为排序键。
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
			for (const int32 Item : Column)
			{
				double Sum = 0.0;
				for (const int32 Neighbor : Neighbors[Item])
				{
					Sum += Position[Neighbor];
				}
				const double Key = Neighbors[Item].IsEmpty() ? Position[Item] : Sum / Neighbors[Item].Num();
				Keys.Emplace(Key, Position[Item]);
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

	// 上方格子与下方格子左上角之间的最小距离
	double MinSeparation(const FLayerItem& Above, const FLayerItem& Below, const FCadenceArcLayoutParams& Params)
	{
		double Gap = !Above.IsChannel() && !Below.IsChannel() ? Params.NodeGap : Params.ChannelGap;
		if (Above.bHasSelfLoop)
		{
			Gap = FMath::Max(Gap, static_cast<double>(Params.LoopMargin * 0.5f + Params.ChannelGap));
		}
		return Above.Height + Gap;
	}

	// 在保持列内顺序和最小间距的前提下，让纵坐标尽量接近期望值（最小二乘，保序回归）。
	// 令 Z_i = Y_i - C_i（C_i 是累计最小间距），约束变成 Z 单调不减，用相邻违例合并求解。
	void PlaceColumn(
		const TArray<int32>& Column, const TArray<double>& Desired,
		TArray<FLayerItem>& Items, const FCadenceArcLayoutParams& Params)
	{
		const int32 Count = Column.Num();
		TArray<double> Cumulative;
		Cumulative.Init(0.0, Count);
		for (int32 Index = 1; Index < Count; ++Index)
		{
			Cumulative[Index] = Cumulative[Index - 1]
				+ MinSeparation(Items[Column[Index - 1]], Items[Column[Index]], Params);
		}
		struct FBlock
		{
			double Sum = 0.0;
			int32 Count = 0;
			double Mean() const { return Sum / Count; }
		};
		TArray<FBlock> Blocks;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Blocks.Add({Desired[Index] - Cumulative[Index], 1});
			while (Blocks.Num() > 1 && Blocks.Last(1).Mean() > Blocks.Last().Mean())
			{
				const FBlock Last = Blocks.Pop();
				Blocks.Last().Sum += Last.Sum;
				Blocks.Last().Count += Last.Count;
			}
		}
		int32 Index = 0;
		for (const FBlock& Block : Blocks)
		{
			for (int32 InBlock = 0; InBlock < Block.Count; ++InBlock, ++Index)
			{
				Items[Column[Index]].Y = Block.Mean() + Cumulative[Index];
			}
		}
	}

	// 保单调的三次样条（Fritsch-Butland 斜率）：x 严格递增的途经点连成一条光滑曲线，两端切线水平。
	// 每一段内 y 都落在该段两个端点之间，不会越过途经点，所以把途经点夹进空闲带就能保证整段都在带内。
	TArray<FVector2D> SampleMonotoneCurve(const TArray<FVector2D>& Points, const int32 Samples)
	{
		const int32 Count = Points.Num();
		TArray<double> Slopes;
		Slopes.Init(0.0, Count);
		for (int32 Index = 1; Index + 1 < Count; ++Index)
		{
			const double H0 = Points[Index].X - Points[Index - 1].X;
			const double H1 = Points[Index + 1].X - Points[Index].X;
			const double D0 = (Points[Index].Y - Points[Index - 1].Y) / H0;
			const double D1 = (Points[Index + 1].Y - Points[Index].Y) / H1;
			if (D0 * D1 > 0.0)
			{
				const double W0 = 2.0 * H1 + H0;
				const double W1 = H1 + 2.0 * H0;
				Slopes[Index] = (W0 + W1) / (W0 / D0 + W1 / D1);
			}
		}
		TArray<FVector2D> Result;
		Result.Add(Points[0]);
		for (int32 Index = 0; Index + 1 < Count; ++Index)
		{
			const FVector2D& A = Points[Index];
			const FVector2D& B = Points[Index + 1];
			const double H = B.X - A.X;
			for (int32 Step = 1; Step <= Samples; ++Step)
			{
				const double T = static_cast<double>(Step) / Samples;
				const double T2 = T * T;
				const double T3 = T2 * T;
				const double Y = (2 * T3 - 3 * T2 + 1) * A.Y + (T3 - 2 * T2 + T) * H * Slopes[Index]
					+ (-2 * T3 + 3 * T2) * B.Y + (T3 - T2) * H * Slopes[Index + 1];
				Result.Add(FVector2D(A.X + T * H, Y));
			}
		}
		return Result;
	}

	// 折线拐角改成小圆弧。圆弧只落在拐角两侧线段的前后 Radius 范围内，不会离开原来的走线区域。
	TArray<FVector2D> RoundCorners(const TArray<FVector2D>& Points, const double Radius)
	{
		constexpr int32 ArcSamples = 4;
		TArray<FVector2D> Result;
		Result.Add(Points[0]);
		for (int32 Index = 1; Index + 1 < Points.Num(); ++Index)
		{
			const FVector2D Corner = Points[Index];
			const FVector2D In = Corner - Points[Index - 1];
			const FVector2D Out = Points[Index + 1] - Corner;
			const double R = FMath::Min3(Radius, In.Size() * 0.5, Out.Size() * 0.5);
			if (R <= 0.0 || FMath::IsNearlyZero(FVector2D::CrossProduct(In, Out)))
			{
				Result.Add(Corner);
				continue;
			}
			const FVector2D A = Corner - In.GetSafeNormal() * R;
			const FVector2D B = Corner + Out.GetSafeNormal() * R;
			for (int32 Step = 0; Step <= ArcSamples; ++Step)
			{
				const double T = static_cast<double>(Step) / ArcSamples;
				Result.Add(FMath::Lerp(FMath::Lerp(A, Corner, T), FMath::Lerp(Corner, B, T), T));
			}
		}
		Result.Add(Points.Last());
		return Result;
	}
}

FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph, const FCadenceArcLayoutParams& Params)
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
		LayoutNode.NumPorts = Node.Transitions.Num();
		LayoutNode.Size = FVector2D(
			Params.NodeWidth,
			LayoutNode.NumPorts > 0
				? Params.HeaderHeight + LayoutNode.NumPorts * Params.PortHeight + Params.NodeBottomPad
				: Params.HeaderHeight);
		Layout.Nodes.Add(LayoutNode);
		// 资产在 PIE 中被改坏时也保持确定性：重复 Tag 始终指向数组中的第一个节点。
		if (!NodeIndexMap.Contains(Node.ActionTag))
		{
			NodeIndexMap.Add(Node.ActionTag, NodeIndex);
		}
	}
	if (NumNodes == 0)
	{
		Layout.Size = FVector2D(2.0 * Params.Padding, 2.0 * Params.Padding);
		return Layout;
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

	if (Params.Mode == ECadenceArcLayoutMode::CompactChains)
	{
		const int32* Entry = NodeIndexMap.Find(Graph.EntryActionTag);
		FindCompactChains(Layout, Entry ? *Entry : INDEX_NONE);
	}
	TArray<int32> ChainOfNode;
	ChainOfNode.Init(INDEX_NONE, NumNodes);
	TArray<double> NodeOffset;
	NodeOffset.Init(0.0, NumNodes);
	// 内部的横线从相邻节点的间隙经过，圆角与最粗的高亮线也需要空间。
	const double ChainGap = FMath::Max(static_cast<double>(Params.NodeGap),
		2.0 * (Params.CornerRadius + Params.ChannelGap + Params.ChannelHeight * 0.5));
	const double ChainStub = FMath::Max(static_cast<double>(Params.ReturnStub),
		Params.CornerRadius + Params.ChannelGap + Params.ChannelHeight * 0.5);
	for (int32 ChainIndex = 0; ChainIndex < Layout.FoldedChains.Num(); ++ChainIndex)
	{
		double Offset = 0.0;
		for (const int32 Member : Layout.FoldedChains[ChainIndex].NodeIndices)
		{
			ChainOfNode[Member] = ChainIndex;
			NodeOffset[Member] = Offset;
			Offset += Layout.Nodes[Member].Size.Y + ChainGap;
		}
	}
	const auto IsChainEdge = [&ChainOfNode](const FCadenceArcLayoutEdge& Edge)
	{
		return !Edge.IsBrokenTarget() && ChainOfNode[Edge.SourceNodeIndex] != INDEX_NONE
			&& ChainOfNode[Edge.SourceNodeIndex] == ChainOfNode[Edge.TargetNodeIndex];
	};

	// 2. 最长路径列号：链内边长度为 0（共享一列），其余前向边长度为 1。
	// 逆后序仍是原图去掉回边后的拓扑序；分层模式没有分组，保留原有列号。
	TArray<TArray<int32>> NodePredecessors;
	NodePredecessors.SetNum(NumNodes);
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		if (IsForwardEdge(Edge, Layout.Nodes))
		{
			NodePredecessors[Edge.TargetNodeIndex].Add(Edge.SourceNodeIndex);
		}
	}
	int32 NumReachableColumns = 0;
	for (int32 Index = State.PostOrder.Num() - 1; Index >= 0; --Index)
	{
		FCadenceArcLayoutNode& Node = Layout.Nodes[State.PostOrder[Index]];
		for (const int32 Predecessor : NodePredecessors[Node.NodeIndex])
		{
			const bool bSameChain = ChainOfNode[Node.NodeIndex] != INDEX_NONE
				&& ChainOfNode[Node.NodeIndex] == ChainOfNode[Predecessor];
			Node.Column = FMath::Max(Node.Column, Layout.Nodes[Predecessor].Column + (bSameChain ? 0 : 1));
		}
		NumReachableColumns = FMath::Max(NumReachableColumns, Node.Column + 1);
	}

	// 引用边：列号差不小于阈值的边（不可达节点按它们稍后所在的最后一列算）。它们仍参与上面的列号计算，
	// 只是不再占通道，也不参与排序和纵向对齐。自环的列号差为 0，坏目标没有目标，都不会成为引用。
	if (Params.ReferenceMinSpan > 0)
	{
		const auto FinalColumn = [&Layout, NumReachableColumns](const int32 NodeIndex)
		{
			const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
			return Node.bReachable ? Node.Column : NumReachableColumns;
		};
		for (FCadenceArcLayoutEdge& Edge : Layout.Edges)
		{
			Edge.bIsReference = !Edge.IsBrokenTarget() && FMath::Abs(
				FinalColumn(Edge.TargetNodeIndex) - FinalColumn(Edge.SourceNodeIndex)) >= Params.ReferenceMinSpan;
		}
	}

	// 3. 格子：可达的真实节点，加上长边在每个中间列的通道；相邻列之间的连线
	TArray<FLayerItem> Items;
	TArray<int32> ItemOfNode;
	ItemOfNode.Init(INDEX_NONE, NumNodes);
	for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (Node.bReachable && ItemOfNode[Node.NodeIndex] == INDEX_NONE)
		{
			FLayerItem Item;
			Item.Members = ChainOfNode[Node.NodeIndex] == INDEX_NONE
				? TArray<int32>{Node.NodeIndex} : Layout.FoldedChains[ChainOfNode[Node.NodeIndex]].NodeIndices;
			Item.NodeIndex = Item.Members[0];
			Item.Column = Node.Column;
			Item.Height = NodeOffset[Item.Members.Last()] + Layout.Nodes[Item.Members.Last()].Size.Y;
			for (const int32 Member : Item.Members)
			{
				ItemOfNode[Member] = Items.Num();
			}
			Items.Add(MoveTemp(Item));
		}
	}
	TArray<TArray<int32>> EdgeChannels; // 每条前向边依次经过的通道
	EdgeChannels.SetNum(Layout.Edges.Num());
	TArray<FItemLink> Links;
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.TargetNodeIndex != INDEX_NONE && Edge.TargetNodeIndex == Edge.SourceNodeIndex
			&& ItemOfNode[Edge.SourceNodeIndex] != INDEX_NONE)
		{
			Items[ItemOfNode[Edge.SourceNodeIndex]].bHasSelfLoop = true;
		}
		if (!IsForwardEdge(Edge, Layout.Nodes) || Edge.bIsReference || IsChainEdge(Edge))
		{
			continue;
		}
		int32 Previous = ItemOfNode[Edge.SourceNodeIndex];
		double PreviousOffset = NodeOffset[Edge.SourceNodeIndex]
			+ Params.HeaderHeight + (Edge.TransitionIndex + 0.5) * Params.PortHeight;
		const int32 TargetColumn = Layout.Nodes[Edge.TargetNodeIndex].Column;
		for (int32 Column = Layout.Nodes[Edge.SourceNodeIndex].Column + 1; Column < TargetColumn; ++Column)
		{
			FLayerItem Channel;
			Channel.EdgeIndex = EdgeIndex;
			Channel.Column = Column;
			Channel.Height = Params.ChannelHeight;
			const int32 ChannelItem = Items.Add(Channel);
			EdgeChannels[EdgeIndex].Add(ChannelItem);
			Links.Add({Previous, ChannelItem, PreviousOffset, Params.ChannelHeight * 0.5});
			Previous = ChannelItem;
			PreviousOffset = Params.ChannelHeight * 0.5;
		}
		Links.Add({Previous, ItemOfNode[Edge.TargetNodeIndex], PreviousOffset,
			NodeOffset[Edge.TargetNodeIndex] + Params.HeaderHeight * 0.5});
	}
	TArray<TArray<int32>> IncomingLinks;
	TArray<TArray<int32>> OutgoingLinks;
	TArray<TArray<int32>> ItemPredecessors;
	TArray<TArray<int32>> ItemSuccessors;
	IncomingLinks.SetNum(Items.Num());
	OutgoingLinks.SetNum(Items.Num());
	ItemPredecessors.SetNum(Items.Num());
	ItemSuccessors.SetNum(Items.Num());
	for (int32 LinkIndex = 0; LinkIndex < Links.Num(); ++LinkIndex)
	{
		const FItemLink& Link = Links[LinkIndex];
		IncomingLinks[Link.To].Add(LinkIndex);
		OutgoingLinks[Link.From].Add(LinkIndex);
		ItemPredecessors[Link.To].Add(Link.From);
		ItemSuccessors[Link.From].Add(Link.To);
	}

	// 4. 列内排序：初始按前序（通道跟在所属边的源节点后面，再按边序号），再做重心扫描
	TArray<int32> ItemOrder;
	for (int32 Item = 0; Item < Items.Num(); ++Item)
	{
		ItemOrder.Add(Item);
	}
	const auto AnchorPreOrder = [&](const int32 Item)
	{
		const FLayerItem& Entry = Items[Item];
		return State.PreOrder[Entry.IsChannel() ? Layout.Edges[Entry.EdgeIndex].SourceNodeIndex : Entry.NodeIndex];
	};
	ItemOrder.Sort([&](const int32 A, const int32 B)
	{
		if (AnchorPreOrder(A) != AnchorPreOrder(B))
		{
			return AnchorPreOrder(A) < AnchorPreOrder(B);
		}
		if (Items[A].IsChannel() != Items[B].IsChannel())
		{
			return !Items[A].IsChannel();
		}
		return Items[A].EdgeIndex < Items[B].EdgeIndex;
	});
	TArray<TArray<int32>> Columns;
	Columns.SetNum(NumReachableColumns);
	TArray<int32> Position;
	Position.Init(0, Items.Num());
	for (const int32 Item : ItemOrder)
	{
		Position[Item] = Columns[Items[Item].Column].Add(Item);
	}
	for (int32 Pass = 0; Pass < NumOrderingPasses; ++Pass)
	{
		const bool bDown = Pass % 2 == 0;
		OrderColumnsByBarycenter(Columns, Position, bDown ? ItemPredecessors : ItemSuccessors, bDown);
	}
	for (const TArray<int32>& Column : Columns)
	{
		int32 Row = 0;
		for (const int32 Item : Column)
		{
			if (!Items[Item].IsChannel())
			{
				for (const int32 Member : Items[Item].Members)
				{
					Layout.Nodes[Member].Row = Row++;
				}
			}
		}
		Layout.MaxRows = FMath::Max(Layout.MaxRows, Row);
	}

	// 5. 纵坐标：先按顺序紧排，再按连线端口双向对齐
	for (const TArray<int32>& Column : Columns)
	{
		double Y = 0.0;
		for (int32 Index = 0; Index < Column.Num(); ++Index)
		{
			if (Index > 0)
			{
				Y += MinSeparation(Items[Column[Index - 1]], Items[Column[Index]], Params);
			}
			Items[Column[Index]].Y = Y;
		}
	}
	for (int32 Pass = 0; Pass < NumPlacementPasses; ++Pass)
	{
		const bool bDown = Pass % 2 == 0;
		for (int32 Step = 0; Step < NumReachableColumns; ++Step)
		{
			const TArray<int32>& Column = Columns[bDown ? Step : NumReachableColumns - 1 - Step];
			TArray<double> Desired;
			for (const int32 Item : Column)
			{
				const TArray<int32>& Neighbors = bDown ? IncomingLinks[Item] : OutgoingLinks[Item];
				double Sum = 0.0;
				for (const int32 LinkIndex : Neighbors)
				{
					const FItemLink& Link = Links[LinkIndex];
					Sum += bDown
						? Items[Link.From].Y + Link.FromOffset - Link.ToOffset
						: Items[Link.To].Y + Link.ToOffset - Link.FromOffset;
				}
				Desired.Add(Neighbors.IsEmpty() ? Items[Item].Y : Sum / Neighbors.Num());
			}
			PlaceColumn(Column, Desired, Items, Params);
		}
	}
	double MinY = TNumericLimits<double>::Max();
	for (const FLayerItem& Item : Items)
	{
		MinY = FMath::Min(MinY, Item.Y);
	}
	const double ShiftY = Items.IsEmpty() ? 0.0 : Params.Padding - MinY;
	const auto ColumnX = [&Params](const int32 Column)
	{
		return Params.Padding + Column * (static_cast<double>(Params.NodeWidth) + Params.ColumnGap);
	};
	for (FLayerItem& Item : Items)
	{
		Item.Y += ShiftY;
		if (!Item.IsChannel())
		{
			for (const int32 Member : Item.Members)
			{
				Layout.Nodes[Member].Position = FVector2D(ColumnX(Item.Column), Item.Y + NodeOffset[Member]);
			}
		}
	}

	// 每个通道的空闲带：同列中它上方最近的真实节点（含自环）之下、下方最近的真实节点之上，
	// 各留出通道间距。其他通道只是线，不算障碍；空闲带一定包含通道本身。
	TArray<TPair<double, double>> ChannelBands;
	ChannelBands.Init(TPair<double, double>(-TNumericLimits<double>::Max(), TNumericLimits<double>::Max()),
	                  Items.Num());
	for (const TArray<int32>& Column : Columns)
	{
		for (int32 Index = 0; Index < Column.Num(); ++Index)
		{
			if (!Items[Column[Index]].IsChannel())
			{
				continue;
			}
			TPair<double, double>& Band = ChannelBands[Column[Index]];
			for (int32 Above = Index - 1; Above >= 0; --Above)
			{
				const FLayerItem& Node = Items[Column[Above]];
				if (!Node.IsChannel())
				{
					const double LoopExtra = Node.bHasSelfLoop ? Params.LoopMargin * 0.5 : 0.0;
					Band.Key = Node.Y + Node.Height + LoopExtra + Params.ChannelGap;
					break;
				}
			}
			for (int32 Below = Index + 1; Below < Column.Num(); ++Below)
			{
				const FLayerItem& Node = Items[Column[Below]];
				if (!Node.IsChannel())
				{
					Band.Value = Node.Y - Params.ChannelGap;
					break;
				}
			}
		}
	}

	// 不可达节点放在最深可达列之后，行号按数组顺序，自上而下紧排；没有入口时从第 0 列开始。
	int32 UnreachableRow = 0;
	double UnreachableY = Params.Padding;
	for (FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (!Node.bReachable)
		{
			Node.Column = NumReachableColumns;
			Node.Row = UnreachableRow++;
			Node.Position = FVector2D(ColumnX(Node.Column), UnreachableY);
			UnreachableY += Node.Size.Y + Params.NodeGap;
		}
	}
	Layout.NumColumns = NumReachableColumns + (UnreachableRow > 0 ? 1 : 0);
	Layout.MaxRows = FMath::Max(Layout.MaxRows, UnreachableRow);

	// 6. 底部通道：跨度短的放在上面（离节点近），跨度相同按边序号
	TArray<int32> ReturnEdges;
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.TargetNodeIndex != INDEX_NONE && Edge.TargetNodeIndex != Edge.SourceNodeIndex && !Edge.bIsReference
			&& !IsChainEdge(Edge)
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

	double ContentBottom = 0.0;
	for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		ContentBottom = FMath::Max(ContentBottom, Node.Position.Y + Node.Size.Y + Params.LoopMargin);
	}
	for (const FLayerItem& Item : Items)
	{
		ContentBottom = FMath::Max(ContentBottom, Item.Y + Item.Height);
	}
	const auto LaneY = [&](const int32 Lane)
	{
		return ContentBottom + Params.ReturnLaneTopGap + Lane * static_cast<double>(Params.ReturnLaneSpacing);
	};

	// 7. 接入点：同一目标的入边按到达时的高度从上到下错开，限制在标题行内
	const auto PortPoint = [&](const FCadenceArcLayoutEdge& Edge)
	{
		const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
		return Source.Position + FVector2D(
			Params.NodeWidth, Params.HeaderHeight + (Edge.TransitionIndex + 0.5) * Params.PortHeight);
	};
	const auto ApproachY = [&](const int32 EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.bIsReference)
		{
			return PortPoint(Edge).Y; // 接入线是水平的，按源端口的高度排
		}
		if (Edge.ReturnLane != INDEX_NONE)
		{
			return LaneY(Edge.ReturnLane);
		}
		if (Edge.SourceNodeIndex == Edge.TargetNodeIndex)
		{
			const FCadenceArcLayoutNode& Node = Layout.Nodes[Edge.SourceNodeIndex];
			return Node.Position.Y + Node.Size.Y + Params.LoopMargin;
		}
		const TArray<int32>& Channels = EdgeChannels[EdgeIndex];
		return Channels.IsEmpty() ? PortPoint(Edge).Y : Items[Channels.Last()].Y + Params.ChannelHeight * 0.5;
	};
	TArray<FVector2D> EntryPoints;
	EntryPoints.SetNum(Layout.Edges.Num());
	{
		TArray<TArray<int32>> Incoming;
		Incoming.SetNum(NumNodes);
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			if (!Layout.Edges[EdgeIndex].IsBrokenTarget())
			{
				Incoming[Layout.Edges[EdgeIndex].TargetNodeIndex].Add(EdgeIndex);
			}
		}
		for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
		{
			TArray<int32>& Edges = Incoming[NodeIndex];
			TArray<double> Approach;
			Approach.SetNum(Layout.Edges.Num());
			for (const int32 EdgeIndex : Edges)
			{
				Approach[EdgeIndex] = ApproachY(EdgeIndex);
			}
			Edges.Sort([&Approach](const int32 A, const int32 B)
			{
				return Approach[A] != Approach[B] ? Approach[A] < Approach[B] : A < B;
			});
			const int32 Count = Edges.Num();
			const double Spread = Count > 1
				? FMath::Min(static_cast<double>(Params.EntrySpread), (Params.HeaderHeight - 6.0) / (Count - 1))
				: 0.0;
			const FCadenceArcLayoutNode& Target = Layout.Nodes[NodeIndex];
			for (int32 Rank = 0; Rank < Count; ++Rank)
			{
				EntryPoints[Edges[Rank]] = Target.Position + FVector2D(
					0.0, Params.HeaderHeight * 0.5 + (Rank - (Count - 1) * 0.5) * Spread);
			}
		}
	}

	// 8. 每条边的实际绘制路径
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FVector2D Start = PortPoint(Edge);
		if (Edge.IsBrokenTarget())
		{
			// 目标不存在：一小段短线，不猜它原本想连到哪里
			Edge.Path = {Start, Start + FVector2D(Params.BrokenStubLength, 0.0)};
			continue;
		}
		const FVector2D End = EntryPoints[EdgeIndex];
		if (Edge.bIsReference)
		{
			// 引用边：端口引出一小段接到标记；目标一侧只在左侧空隙里画一小段接入线。
			// 标记和接入线都落在列间空隙里（标记右侧、接入线左侧都不到相邻列），不会压在节点上。
			const FVector2D Min(Start.X + Params.ReferenceGap, Start.Y - Params.ReferenceHeight * 0.5);
			Edge.ReferenceBox = FBox2D(Min, Min + FVector2D(Params.ReferenceWidth, Params.ReferenceHeight));
			Edge.Path = {Start, FVector2D(Min.X, Start.Y)};
			Edge.EntryStub = {End - FVector2D(Params.ReferenceEntryStub, 0.0), End};
			continue;
		}
		const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
		if (IsChainEdge(Edge))
		{
			// 同列的局部前进，不是回边：右侧下降到成员间的专用空隙，横穿空隙，再从左侧接入下一成员。
			// 整个链是排序阶段不可拆开的格子，因此这里不会混入其他节点或长边通道。
			const FCadenceArcLayoutNode& Target = Layout.Nodes[Edge.TargetNodeIndex];
			const double GapY = (Source.Position.Y + Source.Size.Y + Target.Position.Y) * 0.5;
			Edge.Path = RoundCorners({Start, FVector2D(Start.X + ChainStub, Start.Y),
				FVector2D(Start.X + ChainStub, GapY), FVector2D(End.X - ChainStub, GapY),
				FVector2D(End.X - ChainStub, End.Y), End}, Params.CornerRadius);
			continue;
		}
		if (Edge.SourceNodeIndex == Edge.TargetNodeIndex)
		{
			// 自环：从端口向右出去，绕过节点底部，从左侧回到自己的标题行
			const double Right = Start.X + Params.LoopMargin;
			const double Bottom = Source.Position.Y + Source.Size.Y + Params.LoopMargin * 0.5;
			const double Left = Source.Position.X - Params.LoopMargin;
			Edge.Path = RoundCorners({
				Start, FVector2D(Right, Start.Y), FVector2D(Right, Bottom),
				FVector2D(Left, Bottom), FVector2D(Left, End.Y), End
			}, Params.CornerRadius);
			continue;
		}
		if (Edge.ReturnLane != INDEX_NONE)
		{
			// 回边：在源列右侧的空隙里下到底部通道，向左走到目标列左侧的空隙，再上来接到标题行。
			// 竖直段按通道号错开几个像素，同一空隙里的多条回边不会完全重合。
			const double Offset = (Edge.ReturnLane % 4) * 3.0;
			const double Lane = LaneY(Edge.ReturnLane);
			const double OutX = Start.X + Params.ReturnStub + Offset;
			const double InX = End.X - Params.ReturnStub - Offset;
			Edge.Path = RoundCorners({
				Start, FVector2D(OutX, Start.Y), FVector2D(OutX, Lane),
				FVector2D(InX, Lane), FVector2D(InX, End.Y), End
			}, Params.CornerRadius);
			continue;
		}
		// 前向边：一条光滑曲线。理想走向是依次连接起点、各通道中心和终点的折线；
		// 在每个中间列的左右边界上取理想高度并夹进该列的空闲带，再用保单调样条连起来。
		// 列内的曲线落在两个边界点之间，也就一定在空闲带内；列间空隙里没有节点。
		const TArray<int32>& Channels = EdgeChannels[EdgeIndex];
		TArray<FVector2D> Ideal = {Start};
		for (const int32 Channel : Channels)
		{
			Ideal.Add(FVector2D(ColumnX(Items[Channel].Column) + Params.NodeWidth * 0.5,
			                    Items[Channel].Y + Params.ChannelHeight * 0.5));
		}
		Ideal.Add(End);
		const auto IdealY = [&Ideal](const double X)
		{
			for (int32 Index = 1; Index < Ideal.Num(); ++Index)
			{
				if (X <= Ideal[Index].X)
				{
					const double Alpha = (X - Ideal[Index - 1].X) / (Ideal[Index].X - Ideal[Index - 1].X);
					return FMath::Lerp(Ideal[Index - 1].Y, Ideal[Index].Y, Alpha);
				}
			}
			return Ideal.Last().Y;
		};
		TArray<FVector2D> Waypoints = {Start};
		for (const int32 Channel : Channels)
		{
			const double Left = ColumnX(Items[Channel].Column);
			const TPair<double, double>& Band = ChannelBands[Channel];
			Waypoints.Add(FVector2D(Left, FMath::Clamp(IdealY(Left), Band.Key, Band.Value)));
			Waypoints.Add(FVector2D(Left + Params.NodeWidth,
			                        FMath::Clamp(IdealY(Left + Params.NodeWidth), Band.Key, Band.Value)));
		}
		Waypoints.Add(End);
		Edge.Path = SampleMonotoneCurve(Waypoints, Params.CurveSamples);
	}

	const double ReturnBottom = Layout.NumReturnLanes > 0 ? LaneY(Layout.NumReturnLanes - 1) : ContentBottom;
	double RightMargin = FMath::Max3(Params.BrokenStubLength, Params.LoopMargin, Params.ReturnStub + 9.f) + 16.0;
	if (Layout.Edges.ContainsByPredicate([](const FCadenceArcLayoutEdge& Edge) { return Edge.bIsReference; }))
	{
		// 最后一列（例如不可达节点）的引用标记画在右侧留白里
		RightMargin = FMath::Max(RightMargin, Params.ReferenceGap + Params.ReferenceWidth + 8.0);
	}
	Layout.Size = FVector2D(
		ColumnX(Layout.NumColumns - 1) + Params.NodeWidth + RightMargin + Params.Padding,
		FMath::Max(ContentBottom, ReturnBottom) + Params.Padding);
	for (FCadenceArcLayoutChain& Chain : Layout.FoldedChains)
	{
		for (const int32 Member : Chain.NodeIndices)
		{
			Chain.Bounds += Layout.Nodes[Member].Position;
			Chain.Bounds += Layout.Nodes[Member].Position + Layout.Nodes[Member].Size;
		}
		// 横向包含局部绕线；纵向只添加分组轮廓留白，不能侵占相邻格子的通道。
		Chain.Bounds = FBox2D(Chain.Bounds.Min - FVector2D(ChainStub + 4.0, 4.0),
			Chain.Bounds.Max + FVector2D(ChainStub + 4.0, 4.0));
		Layout.Size.X = FMath::Max(Layout.Size.X, Chain.Bounds.Max.X + Params.Padding);
		Layout.Size.Y = FMath::Max(Layout.Size.Y, Chain.Bounds.Max.Y + Params.Padding);
	}
	return Layout;
}

double ComputeFollowOffset(
	const double CurrentOffset, const double Visible, const double PrimaryMin, const double PrimaryMax,
	const double GroupMin, const double GroupMax, const double Margin, const double MaxOffset)
{
	if (Visible <= 0.0)
	{
		return CurrentOffset; // 还没完成第一次排布
	}
	double Result;
	if (PrimaryMax - PrimaryMin + 2.0 * Margin > Visible)
	{
		Result = PrimaryMin - Margin; // 节点本身比视口大：至少让它的开头可读
	}
	else if (GroupMax - GroupMin + 2.0 * Margin <= Visible)
	{
		// 整组放得下：已经完整可见就不动，否则移动最少的距离
		Result = FMath::Clamp(CurrentOffset, GroupMax + Margin - Visible, GroupMin - Margin);
	}
	else
	{
		// 放不下：保证已提交节点可见，其余自由度用来尽量朝整组中心靠
		Result = FMath::Clamp((GroupMin + GroupMax - Visible) * 0.5, PrimaryMax + Margin - Visible,
		                      PrimaryMin - Margin);
	}
	return FMath::Clamp(Result, 0.0, FMath::Max(MaxOffset, 0.0));
}

double ComputeFollowZoom(
	const double CurrentZoom, const double VisibleWidth, const double VisibleHeight,
	const double GroupWidth, const double GroupHeight, const double Margin, const double MinZoom)
{
	if (VisibleWidth <= 0.0 || VisibleHeight <= 0.0)
	{
		return CurrentZoom; // 还没完成第一次排布
	}
	double Zoom = 1.0;
	if (GroupWidth > 0.0)
	{
		Zoom = FMath::Min(Zoom, (VisibleWidth - 2.0 * Margin) / GroupWidth);
	}
	if (GroupHeight > 0.0)
	{
		Zoom = FMath::Min(Zoom, (VisibleHeight - 2.0 * Margin) / GroupHeight);
	}
	return FMath::Clamp(Zoom, MinZoom, 1.0);
}

TOptional<FCadenceArcOffscreenHint> ComputeOffscreenHint(const FBox2D& Viewport, const FBox2D& Target, const double Inset)
{
	if (Viewport.Intersect(Target))
	{
		return {};
	}
	const FVector2D Center = Target.GetCenter();
	FCadenceArcOffscreenHint Hint;
	Hint.Anchor = FVector2D(
		FMath::Clamp(Center.X, Viewport.Min.X + Inset, FMath::Max(Viewport.Min.X + Inset, Viewport.Max.X - Inset)),
		FMath::Clamp(Center.Y, Viewport.Min.Y + Inset, FMath::Max(Viewport.Min.Y + Inset, Viewport.Max.Y - Inset)));
	Hint.Direction = (Center - Hint.Anchor).GetSafeNormal();
	return Hint;
}

int32 HitTestNode(const FCadenceArcGraphLayout& Layout, const FVector2D& Point)
{
	for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
	{
		if (FBox2D(Node.Position, Node.Position + Node.Size).IsInside(Point))
		{
			return Node.NodeIndex;
		}
	}
	return INDEX_NONE;
}

int32 HitTestEdge(const FCadenceArcGraphLayout& Layout, const FVector2D& Point, const double Tolerance)
{
	int32 Best = INDEX_NONE;
	double BestDistance = Tolerance;
	const auto Consider = [&Best, &BestDistance](const int32 EdgeIndex, const double Distance)
	{
		if (Distance < BestDistance || (Distance == BestDistance && Best == INDEX_NONE))
		{
			Best = EdgeIndex;
			BestDistance = Distance;
		}
	};
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.bIsReference && Edge.ReferenceBox.IsInside(Point))
		{
			Consider(EdgeIndex, 0.0);
		}
		for (const TArray<FVector2D>* Path : {&Edge.Path, &Edge.EntryStub})
		{
			for (int32 Index = 1; Index < Path->Num(); ++Index)
			{
				Consider(EdgeIndex, FMath::Sqrt(FMath::PointDistToSegmentSquared(
					FVector(Point, 0.0), FVector((*Path)[Index - 1], 0.0), FVector((*Path)[Index], 0.0))));
			}
		}
	}
	return Best;
}
