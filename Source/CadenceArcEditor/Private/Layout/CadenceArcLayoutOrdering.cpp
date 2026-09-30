// 布局第 3 步：把可达节点（或整条紧凑链）和长边在中间列的通道做成格子，再按重心法排出每列的上下顺序。

#include "CadenceArcLayoutBuild.h"

namespace CadenceArc::Editor::LayoutBuild
{
	namespace
	{
		constexpr int32 NumOrderingPasses = 4;

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
	}

	void BuildLayerItems(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		const FCadenceArcGraphLayout& Layout = Context.Layout;
		TArray<FLayerItem>& Items = Context.Items;
		TArray<int32>& ItemOfNode = Context.ItemOfNode;
		ItemOfNode.Init(INDEX_NONE, Context.NumNodes);
		for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			if (Node.bReachable && ItemOfNode[Node.NodeIndex] == INDEX_NONE)
			{
				FLayerItem Item;
				Item.Members = Context.ChainOfNode[Node.NodeIndex] == INDEX_NONE
					? TArray<int32>{Node.NodeIndex} : Layout.FoldedChains[Context.ChainOfNode[Node.NodeIndex]].NodeIndices;
				Item.NodeIndex = Item.Members[0];
				Item.Column = Node.Column;
				Item.Height = Context.NodeOffset[Item.Members.Last()] + Layout.Nodes[Item.Members.Last()].Size.Y;
				for (const int32 Member : Item.Members)
				{
					ItemOfNode[Member] = Items.Num();
				}
				Items.Add(MoveTemp(Item));
			}
		}
		Context.EdgeChannels.SetNum(Layout.Edges.Num());
		Context.EdgeFirstLink.Init(INDEX_NONE, Layout.Edges.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (Edge.TargetNodeIndex != INDEX_NONE && Edge.TargetNodeIndex == Edge.SourceNodeIndex
				&& ItemOfNode[Edge.SourceNodeIndex] != INDEX_NONE)
			{
				Items[ItemOfNode[Edge.SourceNodeIndex]].bHasSelfLoop = true;
			}
			if (!Context.IsForwardEdge(Edge) || Edge.bIsReference || Context.IsChainEdge(Edge))
			{
				continue;
			}
			int32 Previous = ItemOfNode[Edge.SourceNodeIndex];
			double PreviousOffset = Context.NodeOffset[Edge.SourceNodeIndex]
				+ Params.HeaderHeight + (Edge.PortSlot + 0.5) * Params.PortHeight;
			Context.EdgeFirstLink[EdgeIndex] = Context.Links.Num(); // 下面加入的第一段就是从源端口出发的那段
			const int32 TargetColumn = Layout.Nodes[Edge.TargetNodeIndex].Column;
			for (int32 Column = Layout.Nodes[Edge.SourceNodeIndex].Column + 1; Column < TargetColumn; ++Column)
			{
				FLayerItem Channel;
				Channel.EdgeIndex = EdgeIndex;
				Channel.Column = Column;
				Channel.Height = Params.ChannelHeight;
				const int32 ChannelItem = Items.Add(Channel);
				Context.EdgeChannels[EdgeIndex].Add(ChannelItem);
				Context.Links.Add({Previous, ChannelItem, PreviousOffset, Params.ChannelHeight * 0.5});
				Previous = ChannelItem;
				PreviousOffset = Params.ChannelHeight * 0.5;
			}
			Context.Links.Add({Previous, ItemOfNode[Edge.TargetNodeIndex], PreviousOffset,
				Context.NodeOffset[Edge.TargetNodeIndex] + Params.HeaderHeight * 0.5});
		}
		Context.IncomingLinks.SetNum(Items.Num());
		Context.OutgoingLinks.SetNum(Items.Num());
		Context.ItemPredecessors.SetNum(Items.Num());
		Context.ItemSuccessors.SetNum(Items.Num());
		for (int32 LinkIndex = 0; LinkIndex < Context.Links.Num(); ++LinkIndex)
		{
			const FItemLink& Link = Context.Links[LinkIndex];
			Context.IncomingLinks[Link.To].Add(LinkIndex);
			Context.OutgoingLinks[Link.From].Add(LinkIndex);
			Context.ItemPredecessors[Link.To].Add(Link.From);
			Context.ItemSuccessors[Link.From].Add(Link.To);
		}
	}

	void OrderColumns(FBuildContext& Context)
	{
		// 初始按前序（通道跟在所属边的源节点后面，再按边序号），再做重心扫描
		FCadenceArcGraphLayout& Layout = Context.Layout;
		const TArray<FLayerItem>& Items = Context.Items;
		TArray<int32> ItemOrder;
		for (int32 Item = 0; Item < Items.Num(); ++Item)
		{
			ItemOrder.Add(Item);
		}
		const auto AnchorPreOrder = [&](const int32 Item)
		{
			const FLayerItem& Entry = Items[Item];
			return Context.PreOrder[Entry.IsChannel() ? Layout.Edges[Entry.EdgeIndex].SourceNodeIndex : Entry.NodeIndex];
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
		TArray<TArray<int32>>& Columns = Context.Columns;
		Columns.SetNum(Context.NumReachableColumns);
		TArray<int32> Position;
		Position.Init(0, Items.Num());
		for (const int32 Item : ItemOrder)
		{
			Position[Item] = Columns[Items[Item].Column].Add(Item);
		}
		for (int32 Pass = 0; Pass < NumOrderingPasses; ++Pass)
		{
			const bool bDown = Pass % 2 == 0;
			OrderColumnsByBarycenter(Columns, Position, bDown ? Context.ItemPredecessors : Context.ItemSuccessors, bDown);
		}
		Context.ItemRow = Position;
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
	}

	void AssignPortSlots(FBuildContext& Context)
	{
		// 端口行从上到下依次是：
		// 0. 前向边，按下一步所到格子在下一列里的上下位置（纵坐标保持列内顺序，所以这就是它们到达时的上下顺序），
		//    同一格子（平行边、紧凑链）再按到达点在格子里的偏移；这样相邻出边在离开节点的空隙里不会互相穿过；
		// 1. 引用边和坏目标：只有一小段水平线，不往上下走；
		// 2. 回边和链内边：先在右侧空隙里往下走，放在下面才不会横穿其他出边的水平段；
		// 3. 自环：竖线离节点最近（LoopMargin），放在最下面，其余出边的水平段都不会被它截断。
		// 同一组内按 Transition 顺序，结果确定。
		if (!Context.Params.bSortPorts)
		{
			return;
		}
		const FCadenceArcLayoutParams& Params = Context.Params;
		FCadenceArcGraphLayout& Layout = Context.Layout;
		struct FPortKey
		{
			int32 Group = 0;
			int32 Row = 0;
			double Offset = 0.0;
			int32 TransitionIndex = 0;
			int32 EdgeIndex = INDEX_NONE;
		};
		for (int32 NodeIndex = 0; NodeIndex < Context.NumNodes; ++NodeIndex)
		{
			TArray<FPortKey> Keys;
			for (const int32 EdgeIndex : Context.OutEdges[NodeIndex])
			{
				const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
				FPortKey Key;
				Key.TransitionIndex = Edge.TransitionIndex;
				Key.EdgeIndex = EdgeIndex;
				if (const int32 LinkIndex = Context.EdgeFirstLink[EdgeIndex]; LinkIndex != INDEX_NONE)
				{
					const FItemLink& Link = Context.Links[LinkIndex];
					Key.Group = 0;
					Key.Row = Context.ItemRow[Link.To];
					Key.Offset = Link.ToOffset;
				}
				else if (Edge.bIsReference || Edge.IsBrokenTarget())
				{
					Key.Group = 1;
				}
				else if (Edge.SourceNodeIndex != Edge.TargetNodeIndex)
				{
					Key.Group = 2;
				}
				else
				{
					Key.Group = 3;
				}
				Keys.Add(Key);
			}
			Keys.Sort([](const FPortKey& A, const FPortKey& B)
			{
				if (A.Group != B.Group)
				{
					return A.Group < B.Group;
				}
				if (A.Row != B.Row)
				{
					return A.Row < B.Row;
				}
				if (A.Offset != B.Offset)
				{
					return A.Offset < B.Offset;
				}
				return A.TransitionIndex < B.TransitionIndex;
			});
			for (int32 Slot = 0; Slot < Keys.Num(); ++Slot)
			{
				FCadenceArcLayoutEdge& Edge = Layout.Edges[Keys[Slot].EdgeIndex];
				Edge.PortSlot = Slot;
				if (const int32 LinkIndex = Context.EdgeFirstLink[Keys[Slot].EdgeIndex]; LinkIndex != INDEX_NONE)
				{
					Context.Links[LinkIndex].FromOffset = Context.NodeOffset[NodeIndex]
						+ Params.HeaderHeight + (Slot + 0.5) * Params.PortHeight;
				}
			}
		}
	}
}
