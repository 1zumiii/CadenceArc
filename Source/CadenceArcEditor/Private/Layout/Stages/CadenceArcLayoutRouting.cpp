// 布局第 5 步：走线。底部通道、接入点错开、每条边的实际绘制路径和画布尺寸。

#include "Layout/Stages/CadenceArcLayoutBuild.h"

namespace CadenceArc::Editor::LayoutBuild
{
	namespace
	{
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

		// 边到达目标时的高度，用来给同一目标的多条入边排上下顺序
		double ApproachY(const FBuildContext& Context, const int32 EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Context.Layout.Edges[EdgeIndex];
			if (Edge.bIsReference)
			{
				return Context.PortPoint(Edge).Y; // 接入线是水平的，按源端口的高度排
			}
			if (Edge.ReturnLane != INDEX_NONE)
			{
				return Context.LaneY(Edge.ReturnLane);
			}
			if (Edge.SourceNodeIndex == Edge.TargetNodeIndex)
			{
				const FCadenceArcLayoutNode& Node = Context.Layout.Nodes[Edge.SourceNodeIndex];
				return Node.Position.Y + Node.Size.Y + Context.Params.LoopMargin;
			}
			const TArray<int32>& Channels = Context.EdgeChannels[EdgeIndex];
			return Channels.IsEmpty()
				? Context.PortPoint(Edge).Y
				: Context.Items[Channels.Last()].Y + Context.Params.ChannelHeight * 0.5;
		}

		// 前向边：一条光滑曲线。理想走向是依次连接起点、各通道中心和终点的折线；
		// 在每个中间列的左右边界上取理想高度并夹进该列的空闲带，再用保单调样条连起来。
		// 列内的曲线落在两个边界点之间，也就一定在空闲带内；列间空隙里没有节点。
		TArray<FVector2D> ForwardCurve(
			const FBuildContext& Context, const int32 EdgeIndex, const FVector2D& Start, const FVector2D& End)
		{
			const FCadenceArcLayoutParams& Params = Context.Params;
			const TArray<int32>& Channels = Context.EdgeChannels[EdgeIndex];
			TArray<FVector2D> Ideal = {Start};
			for (const int32 Channel : Channels)
			{
				Ideal.Add(FVector2D(Context.ColumnX(Context.Items[Channel].Column) + Params.NodeWidth * 0.5,
				                    Context.Items[Channel].Y + Params.ChannelHeight * 0.5));
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
				const double Left = Context.ColumnX(Context.Items[Channel].Column);
				const TPair<double, double>& Band = Context.ChannelBands[Channel];
				Waypoints.Add(FVector2D(Left, FMath::Clamp(IdealY(Left), Band.Key, Band.Value)));
				Waypoints.Add(FVector2D(Left + Params.NodeWidth,
				                        FMath::Clamp(IdealY(Left + Params.NodeWidth), Band.Key, Band.Value)));
			}
			Waypoints.Add(End);
			return SampleMonotoneCurve(Waypoints, Params.CurveSamples);
		}

		// 前向边依次经过的高度：起点端口、各中间列通道的中心、终点接入点。
		// 第 k 段从 Levels[k] 换到 Levels[k + 1]，换高度只发生在源列 + k 右侧的空隙里。
		TArray<double> ForwardLevels(const FBuildContext& Context, const int32 EdgeIndex, const FVector2D& Start, const FVector2D& End)
		{
			TArray<double> Levels = {Start.Y};
			for (const int32 Channel : Context.EdgeChannels[EdgeIndex])
			{
				Levels.Add(Context.Items[Channel].Y + Context.Params.ChannelHeight * 0.5);
			}
			Levels.Add(End.Y);
			return Levels;
		}

		// 直角走线的一段竖线：某条边在某个空隙里从 FromY 换到 ToY
		struct FJog
		{
			int32 EdgeIndex = INDEX_NONE;
			int32 Step = 0; // 这条边的第几段
			double FromY = 0.0;
			double ToY = 0.0;
			bool IsDown() const { return ToY > FromY; }
		};

		constexpr double StraightTolerance = 0.5; // 高度差在这以内就不拐弯，直接水平穿过

		// 给每个空隙里的竖线分配轨道（横坐标），返回 JogX[边][段]，不需要竖线的段没有值。
		// 纵向范围重叠的竖线必须在不同轨道上；不重叠的可以共用一条，这样线少的空隙里竖线靠近中间。
		// 同方向的两条竖线按这个顺序从右往左排就不会交叉：往下走的，起点越高越靠右；往上走的，起点越低越靠右。
		// （例如往下的 A 从 100 到 200、B 从 150 到 300：A 在右，A 的出线向右不经过 B，B 的入线从左来也碰不到 A。）
		// 往下的整体放在往上的右边；一上一下且范围重叠时交叉本身无法避免。
		TArray<TArray<TOptional<double>>> AssignJogTracks(const FBuildContext& Context, const TArray<TArray<double>>& EdgeLevels)
		{
			const FCadenceArcLayoutParams& Params = Context.Params;
			const FCadenceArcGraphLayout& Layout = Context.Layout;
			TArray<TArray<TOptional<double>>> JogX;
			JogX.SetNum(Layout.Edges.Num());
			TArray<TArray<FJog>> GapJogs;
			GapJogs.SetNum(FMath::Max(Context.NumReachableColumns, 1));
			for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
			{
				const TArray<double>& Levels = EdgeLevels[EdgeIndex];
				JogX[EdgeIndex].Init(TOptional<double>(), FMath::Max(Levels.Num() - 1, 0));
				const int32 SourceColumn = Layout.Nodes[Layout.Edges[EdgeIndex].SourceNodeIndex].Column;
				for (int32 Step = 0; Step + 1 < Levels.Num(); ++Step)
				{
					if (FMath::Abs(Levels[Step + 1] - Levels[Step]) > StraightTolerance)
					{
						GapJogs[SourceColumn + Step].Add({EdgeIndex, Step, Levels[Step], Levels[Step + 1]});
					}
				}
			}
			const double Margin = Params.CornerRadius; // 纵向范围再各留一个圆角的余量，拐角也不会碰在一起
			for (int32 Gap = 0; Gap < GapJogs.Num(); ++Gap)
			{
				TArray<FJog>& Jogs = GapJogs[Gap];
				if (Jogs.IsEmpty())
				{
					continue;
				}
				Jogs.Sort([](const FJog& A, const FJog& B)
				{
					if (A.IsDown() != B.IsDown())
					{
						return A.IsDown();
					}
					const double KeyA = A.IsDown() ? A.FromY : -A.FromY;
					const double KeyB = B.IsDown() ? B.FromY : -B.FromY;
					if (KeyA != KeyB)
					{
						return KeyA < KeyB;
					}
					return A.EdgeIndex != B.EdgeIndex ? A.EdgeIndex < B.EdgeIndex : A.Step < B.Step;
				});
				// 按上面的顺序依次放：轨道号 = 与它重叠的已放竖线的最大轨道号 + 1（0 是最右边）
				TArray<int32> Tracks;
				int32 NumTracks = 0;
				for (int32 Index = 0; Index < Jogs.Num(); ++Index)
				{
					const double Min = FMath::Min(Jogs[Index].FromY, Jogs[Index].ToY) - Margin;
					const double Max = FMath::Max(Jogs[Index].FromY, Jogs[Index].ToY) + Margin;
					int32 Track = 0;
					for (int32 Placed = 0; Placed < Index; ++Placed)
					{
						const double PlacedMin = FMath::Min(Jogs[Placed].FromY, Jogs[Placed].ToY) - Margin;
						const double PlacedMax = FMath::Max(Jogs[Placed].FromY, Jogs[Placed].ToY) + Margin;
						if (Min <= PlacedMax && PlacedMin <= Max)
						{
							Track = FMath::Max(Track, Tracks[Placed] + 1);
						}
					}
					Tracks.Add(Track);
					NumTracks = FMath::Max(NumTracks, Track + 1);
				}
				const double Left = Context.ColumnX(Gap) + Params.NodeWidth + Params.TrackInset;
				const double Right = FMath::Max(Left, Context.ColumnX(Gap + 1) - Params.TrackInset);
				const double Spacing = NumTracks > 1
					? FMath::Min(static_cast<double>(Params.TrackSpacing), (Right - Left) / (NumTracks - 1))
					: 0.0;
				const double Center = (Left + Right) * 0.5;
				for (int32 Index = 0; Index < Jogs.Num(); ++Index)
				{
					JogX[Jogs[Index].EdgeIndex][Jogs[Index].Step] = Center + ((NumTracks - 1) * 0.5 - Tracks[Index]) * Spacing;
				}
			}
			return JogX;
		}

		// 直角走线：从端口水平出发，在每个需要换高度的空隙里沿自己的轨道竖直移动，再水平穿过下一列的通道，
		// 最后水平接入目标。竖线都在列间空隙里（离列边至少 TrackInset），水平段穿过中间列时就在通道高度上，
		// 所以和曲线一样不会压在节点上。
		TArray<FVector2D> OrthogonalPath(
			const TArray<double>& Levels, const TArray<TOptional<double>>& JogX, const FVector2D& Start, const FVector2D& End)
		{
			// 不拐弯的段沿用当前高度（与通道中心最多差 StraightTolerance），水平段保持严格水平
			TArray<FVector2D> Points = {Start};
			double CurrentY = Start.Y;
			for (int32 Step = 0; Step + 1 < Levels.Num(); ++Step)
			{
				if (JogX[Step].IsSet())
				{
					Points.Add(FVector2D(JogX[Step].GetValue(), CurrentY));
					Points.Add(FVector2D(JogX[Step].GetValue(), Levels[Step + 1]));
					CurrentY = Levels[Step + 1];
				}
			}
			Points.Add(End);
			return Points;
		}
	}

	double FBuildContext::LaneY(const int32 Lane) const
	{
		return ContentBottom + Params.ReturnLaneTopGap + Lane * static_cast<double>(Params.ReturnLaneSpacing);
	}

	FVector2D FBuildContext::PortPoint(const FCadenceArcLayoutEdge& Edge) const
	{
		const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
		return Source.Position + FVector2D(
			Params.NodeWidth, Params.HeaderHeight + (Edge.PortSlot + 0.5) * Params.PortHeight);
	}

	void AssignReturnLanes(FBuildContext& Context)
	{
		// 跨度短的放在上面（离节点近），跨度相同按边序号
		FCadenceArcGraphLayout& Layout = Context.Layout;
		TArray<int32> ReturnEdges;
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (Edge.TargetNodeIndex != INDEX_NONE && Edge.TargetNodeIndex != Edge.SourceNodeIndex && !Edge.bIsReference
				&& !Context.IsChainEdge(Edge)
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

		// 底部通道从节点（含自环）和通道的最低点往下排
		double ContentBottom = 0.0;
		for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			ContentBottom = FMath::Max(ContentBottom, Node.Position.Y + Node.Size.Y + Context.Params.LoopMargin);
		}
		for (const FLayerItem& Item : Context.Items)
		{
			ContentBottom = FMath::Max(ContentBottom, Item.Y + Item.Height);
		}
		Context.ContentBottom = ContentBottom;
	}

	void ComputeEntryPoints(FBuildContext& Context)
	{
		// 同一目标的入边按到达时的高度从上到下错开，限制在标题行内
		const FCadenceArcLayoutParams& Params = Context.Params;
		const FCadenceArcGraphLayout& Layout = Context.Layout;
		Context.EntryPoints.SetNum(Layout.Edges.Num());
		TArray<TArray<int32>> Incoming;
		Incoming.SetNum(Context.NumNodes);
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			if (!Layout.Edges[EdgeIndex].IsBrokenTarget())
			{
				Incoming[Layout.Edges[EdgeIndex].TargetNodeIndex].Add(EdgeIndex);
			}
		}
		for (int32 NodeIndex = 0; NodeIndex < Context.NumNodes; ++NodeIndex)
		{
			TArray<int32>& Edges = Incoming[NodeIndex];
			TArray<double> Approach;
			Approach.SetNum(Layout.Edges.Num());
			for (const int32 EdgeIndex : Edges)
			{
				Approach[EdgeIndex] = ApproachY(Context, EdgeIndex);
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
				Context.EntryPoints[Edges[Rank]] = Target.Position + FVector2D(
					0.0, Params.HeaderHeight * 0.5 + (Rank - (Count - 1) * 0.5) * Spread);
			}
		}
	}

	void BuildEdgePaths(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		FCadenceArcGraphLayout& Layout = Context.Layout;
		const auto IsPlainForward = [&Context](const FCadenceArcLayoutEdge& Edge)
		{
			return !Edge.IsBrokenTarget() && !Edge.bIsReference && !Context.IsChainEdge(Edge)
				&& Edge.SourceNodeIndex != Edge.TargetNodeIndex && Edge.ReturnLane == INDEX_NONE;
		};
		// 直角走线要先看到同一空隙里的所有竖线才能分配轨道，所以先收集每条前向边经过的高度
		const bool bOrthogonal = Params.EdgeStyle == ECadenceArcEdgeStyle::Orthogonal;
		TArray<TArray<double>> EdgeLevels;
		TArray<TArray<TOptional<double>>> JogX;
		if (bOrthogonal)
		{
			EdgeLevels.SetNum(Layout.Edges.Num());
			for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
			{
				const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
				if (IsPlainForward(Edge))
				{
					EdgeLevels[EdgeIndex] = ForwardLevels(
						Context, EdgeIndex, Context.PortPoint(Edge), Context.EntryPoints[EdgeIndex]);
				}
			}
			JogX = AssignJogTracks(Context, EdgeLevels);
		}
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			const FVector2D Start = Context.PortPoint(Edge);
			if (Edge.IsBrokenTarget())
			{
				// 目标不存在：一小段短线，不猜它原本想连到哪里
				Edge.Path = {Start, Start + FVector2D(Params.BrokenStubLength, 0.0)};
				continue;
			}
			const FVector2D End = Context.EntryPoints[EdgeIndex];
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
			if (Context.IsChainEdge(Edge))
			{
				// 同列的局部前进，不是回边：右侧下降到成员间的专用空隙，横穿空隙，再从左侧接入下一成员。
				// 整个链是排序阶段不可拆开的格子，因此这里不会混入其他节点或长边通道。
				const FCadenceArcLayoutNode& Target = Layout.Nodes[Edge.TargetNodeIndex];
				const double GapY = (Source.Position.Y + Source.Size.Y + Target.Position.Y) * 0.5;
				const double ChainStub = Context.ChainStub;
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
				const double Lane = Context.LaneY(Edge.ReturnLane);
				const double OutX = Start.X + Params.ReturnStub + Offset;
				const double InX = End.X - Params.ReturnStub - Offset;
				Edge.Path = RoundCorners({
					Start, FVector2D(OutX, Start.Y), FVector2D(OutX, Lane),
					FVector2D(InX, Lane), FVector2D(InX, End.Y), End
				}, Params.CornerRadius);
				continue;
			}
			// 走到这里的都是 IsPlainForward 的边：其他情况上面都已经 continue
			Edge.Path = bOrthogonal
				? RoundCorners(OrthogonalPath(EdgeLevels[EdgeIndex], JogX[EdgeIndex], Start, End), Params.CornerRadius)
				: ForwardCurve(Context, EdgeIndex, Start, End);
		}
	}

	void ComputeCanvasSize(FBuildContext& Context)
	{
		const FCadenceArcLayoutParams& Params = Context.Params;
		FCadenceArcGraphLayout& Layout = Context.Layout;
		const double ReturnBottom = Layout.NumReturnLanes > 0 ? Context.LaneY(Layout.NumReturnLanes - 1) : Context.ContentBottom;
		double RightMargin = FMath::Max3(Params.BrokenStubLength, Params.LoopMargin, Params.ReturnStub + 9.f) + 16.0;
		if (Layout.Edges.ContainsByPredicate([](const FCadenceArcLayoutEdge& Edge) { return Edge.bIsReference; }))
		{
			// 最后一列（例如不可达节点）的引用标记画在右侧留白里
			RightMargin = FMath::Max(RightMargin, Params.ReferenceGap + Params.ReferenceWidth + 8.0);
		}
		Layout.Size = FVector2D(
			Context.ColumnX(Layout.NumColumns - 1) + Params.NodeWidth + RightMargin + Params.Padding,
			FMath::Max(Context.ContentBottom, ReturnBottom) + Params.Padding);
	}
}
