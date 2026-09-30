// 布局第 4 步：纵坐标。每列保持排好的顺序和最小间距，按连线端口位置上下交替对齐（保序回归）；
// 然后算出每个通道能用的空闲带，最后把不可达节点放到最后一列。

#include "CadenceArcLayoutBuild.h"

namespace CadenceArc::Editor::LayoutBuild
{
	namespace
	{
		constexpr int32 NumPlacementPasses = 6; // 下、上交替，以向上结束：入口等父节点最终居中于后继

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
	}

	double FBuildContext::ColumnX(const int32 Column) const
	{
		return Params.Padding + Column * (static_cast<double>(Params.NodeWidth) + Params.ColumnGap);
	}

	void PlaceItems(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		TArray<FLayerItem>& Items = Context.Items;
		const TArray<TArray<int32>>& Columns = Context.Columns;
		const int32 NumReachableColumns = Context.NumReachableColumns;

		// 先按顺序紧排，再按连线端口双向对齐
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
					const TArray<int32>& Neighbors = bDown ? Context.IncomingLinks[Item] : Context.OutgoingLinks[Item];
					double Sum = 0.0;
					for (const int32 LinkIndex : Neighbors)
					{
						const FItemLink& Link = Context.Links[LinkIndex];
						Sum += bDown
							? Items[Link.From].Y + Link.FromOffset - Link.ToOffset
							: Items[Link.To].Y + Link.ToOffset - Link.FromOffset;
					}
					Desired.Add(Neighbors.IsEmpty() ? Items[Item].Y : Sum / Neighbors.Num());
				}
				PlaceColumn(Column, Desired, Items, Params);
			}
		}

		// 整体移到上方留白之下，写出节点位置
		double MinY = TNumericLimits<double>::Max();
		for (const FLayerItem& Item : Items)
		{
			MinY = FMath::Min(MinY, Item.Y);
		}
		const double ShiftY = Items.IsEmpty() ? 0.0 : Params.Padding - MinY;
		for (FLayerItem& Item : Items)
		{
			Item.Y += ShiftY;
			if (!Item.IsChannel())
			{
				for (const int32 Member : Item.Members)
				{
					Context.Layout.Nodes[Member].Position =
						FVector2D(Context.ColumnX(Item.Column), Item.Y + Context.NodeOffset[Member]);
				}
			}
		}
	}

	void ComputeChannelBands(FBuildContext& Context)
	{
		// 每个通道的空闲带：同列中它上方最近的真实节点（含自环）之下、下方最近的真实节点之上，
		// 各留出通道间距。其他通道只是线，不算障碍；空闲带一定包含通道本身。
		const FCadenceArcLayoutParams& Params = Context.Params;
		const TArray<FLayerItem>& Items = Context.Items;
		TArray<TPair<double, double>>& ChannelBands = Context.ChannelBands;
		ChannelBands.Init(TPair<double, double>(-TNumericLimits<double>::Max(), TNumericLimits<double>::Max()),
		                  Items.Num());
		for (const TArray<int32>& Column : Context.Columns)
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
	}

	void PlaceUnreachableNodes(FBuildContext& Context)
	{
		// 不可达节点放在最深可达列之后，行号按数组顺序，自上而下紧排；没有入口时从第 0 列开始。
		const FCadenceArcLayoutParams& Params = Context.Params;
		FCadenceArcGraphLayout& Layout = Context.Layout;
		int32 UnreachableRow = 0;
		double UnreachableY = Params.Padding;
		for (FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			if (!Node.bReachable)
			{
				Node.Column = Context.NumReachableColumns;
				Node.Row = UnreachableRow++;
				Node.Position = FVector2D(Context.ColumnX(Node.Column), UnreachableY);
				UnreachableY += Node.Size.Y + Params.NodeGap;
			}
		}
		Layout.NumColumns = Context.NumReachableColumns + (UnreachableRow > 0 ? 1 : 0);
		Layout.MaxRows = FMath::Max(Layout.MaxRows, UnreachableRow);
	}
}
