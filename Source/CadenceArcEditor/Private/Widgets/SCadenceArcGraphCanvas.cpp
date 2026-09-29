#include "SCadenceArcGraphCanvas.h"

#include "Graph/CadenceArcGraph.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float NodeWidth = 180.f;
	constexpr float HeaderHeight = 24.f; // 节点标题行
	constexpr float PortHeight = 18.f; // 每条出边一行
	constexpr float NodeBottomPad = 4.f; // 有端口的节点底部留白
	constexpr float ColumnSpacing = 300.f; // 相邻两列左边缘的距离，剩下的空间留给曲线
	constexpr float RowGap = 28.f; // 同一列上下两个节点之间的空隙
	constexpr float Padding = 24.f;
	constexpr float LoopMargin = 14.f; // 自环绕出节点的距离
	constexpr float BrokenStubLength = 24.f;
	constexpr float ReturnLaneTopGap = 20.f; // 节点区域（含自环）与第一条底部通道的距离
	constexpr float ReturnLaneSpacing = 12.f; // 相邻底部通道的距离
	constexpr float ReturnStub = 12.f; // 回边离开端口、接近目标时在列间空隙里的水平距离
	constexpr float UnfocusedAlpha = 0.25f; // 与当前焦点无关的边淡化到这个透明度

	const FLinearColor BodyColor(0.16f, 0.16f, 0.20f);
	const FLinearColor HeaderColor(0.26f, 0.26f, 0.32f);
	const FLinearColor UnreachableBodyColor(0.07f, 0.07f, 0.07f);
	const FLinearColor UnreachableHeaderColor(0.11f, 0.11f, 0.11f);
	const FLinearColor BrokenColor(1.f, 0.25f, 0.25f);

	// CadenceArc.Test.Action.Light01 -> Light01
	FString ShortTagName(const FGameplayTag& Tag)
	{
		const FString Name = Tag.ToString();
		int32 Dot = INDEX_NONE;
		return Name.FindLastChar(TEXT('.'), Dot) ? Name.RightChop(Dot + 1) : Name;
	}

	// 边的颜色只由它在整张图的边数组里的序号决定：同一张图每帧颜色都一样。
	// 按黄金分割比例在色相环上取点，相邻序号的颜色差得最开。
	FLinearColor ColorForEdge(const int32 EdgeIndex)
	{
		const float Hue = FMath::Frac(EdgeIndex * 0.618034f);
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue * 255.f), 170, 255);
	}

	// "Heavy R [0, 0.8)" / "Heavy R [0.8, ∞)" / "Light P"
	FString FormatTransitionLabel(const FCadenceArcTransition& Transition)
	{
		const FString Input = ShortTagName(Transition.InputTag);
		const TCHAR* Phase = Transition.InputPhase == ECadenceArcInputPhase::Pressed ? TEXT("P") : TEXT("R");
		if (Transition.InputPhase != ECadenceArcInputPhase::Released || !Transition.bUseDurationRange)
		{
			return FString::Printf(TEXT("%s %s"), *Input, Phase);
		}
		const FCadenceArcHeldDurationRange& Range = Transition.DurationRange;
		// 上限不包含在区间内，所以右边是圆括号
		const FString Upper = Range.bHasMaxHeldDuration
			? FString::SanitizeFloat(Range.MaxHeldDurationSecondsExclusive)
			: FString(TEXT("∞"));
		return FString::Printf(TEXT("%s %s [%s, %s)"), *Input, Phase,
		                       *FString::SanitizeFloat(Range.MinHeldDurationSeconds), *Upper);
	}

	// 文字需要一个非零的绘制区域，否则会被 Slate 剔除
	void DrawLabel(
		FSlateWindowElementList& OutDrawElements, const int32 Layer,
		const FGeometry& Geometry, const FVector2D& TopLeft,
		const FVector2D& Size, const FString& Text,
		const FSlateFontInfo& Font, const FLinearColor& Color
	)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
		                            Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
		                            Text, Font, ESlateDrawEffect::None, Color);
	}

	FVector2f ToFloatPoint(const FVector2D& Point)
	{
		return FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y));
	}

	TArray<FVector2f> SampleSplinePath(const FVector2D& Start, const FVector2D& End, const double Bend)
	{
		constexpr int32 NumSegments = 48;
		const FVector2D Tangent(Bend, 0.0);
		TArray<FVector2f> Points;
		Points.Reserve(NumSegments + 1);
		for (int32 Index = 0; Index <= NumSegments; ++Index)
		{
			const float Alpha = static_cast<float>(Index) / NumSegments;
			const FVector2D Point = FMath::CubicInterp(Start, Tangent, End, Tangent, Alpha);
			Points.Add(ToFloatPoint(Point));
		}
		return Points;
	}

	// 按累计路径长度截取，不直接按 Hermite 参数截取，视觉进度才会匀速沿曲线前进。
	TArray<FVector2f> TakePathPrefix(const TArray<FVector2f>& Path, const float Fraction)
	{
		if (Path.Num() < 2 || Fraction <= 0.f)
		{
			return {};
		}
		if (Fraction >= 1.f)
		{
			return Path;
		}
		float TotalLength = 0.f;
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			TotalLength += (Path[Index] - Path[Index - 1]).Size();
		}
		const float TargetLength = TotalLength * Fraction;
		float AccumulatedLength = 0.f;
		TArray<FVector2f> Prefix;
		Prefix.Reserve(Path.Num());
		Prefix.Add(Path[0]);
		for (int32 Index = 1; Index < Path.Num(); ++Index)
		{
			const float SegmentLength = (Path[Index] - Path[Index - 1]).Size();
			if (AccumulatedLength + SegmentLength >= TargetLength && SegmentLength > 0.f)
			{
				const float SegmentFraction = (TargetLength - AccumulatedLength) / SegmentLength;
				Prefix.Add(FMath::Lerp(Path[Index - 1], Path[Index], SegmentFraction));
				break;
			}
			Prefix.Add(Path[Index]);
			AccumulatedLength += SegmentLength;
		}
		return Prefix;
	}

	void DrawPreparatoryPath(
		FSlateWindowElementList& OutDrawElements, const int32 Layer,
		const FPaintGeometry& CanvasGeometry, TArray<FVector2f> Path,
		const float Progress, const bool bCurrentRelease,
		const FLinearColor& DashedColor, const FLinearColor& ProgressColor)
	{
		TArray<FVector2f> FilledPath = TakePathPrefix(Path, Progress);
		if (FilledPath.Num() >= 2)
		{
			// 已走过的长度先画成较宽的底色，再叠虚线，填满后仍能看出这是预备边。
			FSlateDrawElement::MakeLines(
				OutDrawElements, Layer, CanvasGeometry, MoveTemp(FilledPath),
				ESlateDrawEffect::None, ProgressColor, true, bCurrentRelease ? 5.f : 3.5f);
		}
		FSlateDrawElement::MakeDashedLines(
			OutDrawElements, Layer, CanvasGeometry, MoveTemp(Path),
			ESlateDrawEffect::None, DashedColor, bCurrentRelease ? 3.f : 2.f, 8.f);
	}
}

void SCadenceArcGraphCanvas::SetDebugView(const FCadenceArcDebugView& InDebugView)
{
	DebugView = InDebugView;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SCadenceArcGraphCanvas::SetGraph(const UCadenceArcGraph* InGraph)
{
	DebugView = FCadenceArcDebugView();
	Graph = InGraph;
	Layout = InGraph ? BuildGraphLayout(*InGraph) : FCadenceArcGraphLayout{};

	PortCounts.Init(0, Layout.Nodes.Num());
	EdgeLabels.Reset(Layout.Edges.Num());
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		++PortCounts[Edge.SourceNodeIndex];
		EdgeLabels.Add(FormatTransitionLabel(Edge.Transition));
	}

	MaxNodeHeight = 0.f;
	for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
	{
		MaxNodeHeight = FMath::Max(MaxNodeHeight, GetNodeHeight(NodeIndex));
	}
	RowSpacing = MaxNodeHeight + RowGap;

	// 尺寸可能变了（布局阶段），画面也要重画（绘制阶段）
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

FVector2D SCadenceArcGraphCanvas::ComputeDesiredSize(float) const
{
	if (Layout.Nodes.IsEmpty())
	{
		return FVector2D(2.f * Padding, 2.f * Padding);
	}
	// 右侧留出自环和坏目标短线的位置，下方留出自环绕过节点底部的位置，再往下是回边通道
	const float Width = (Layout.NumColumns - 1) * ColumnSpacing + NodeWidth + BrokenStubLength + 16.f;
	float Height = (Layout.MaxRows - 1) * RowSpacing + MaxNodeHeight + LoopMargin;
	if (Layout.NumReturnLanes > 0)
	{
		Height += ReturnLaneTopGap + (Layout.NumReturnLanes - 1) * ReturnLaneSpacing;
	}
	return FVector2D(Width + 2.f * Padding, Height + 2.f * Padding);
}

double SCadenceArcGraphCanvas::GetReturnLaneY(const int32 Lane) const
{
	const double NodesBottom = Padding + (Layout.MaxRows - 1) * RowSpacing + MaxNodeHeight + LoopMargin;
	return NodesBottom + ReturnLaneTopGap + Lane * ReturnLaneSpacing;
}

TArray<FVector2f> SCadenceArcGraphCanvas::BuildEdgePath(const FCadenceArcLayoutEdge& Edge) const
{
	const FVector2D Start = GetPortAnchor(Edge);
	if (Edge.IsBrokenTarget())
	{
		// 目标不存在：一小段短线，不猜它原本想连到哪里
		return {ToFloatPoint(Start), ToFloatPoint(Start + FVector2D(BrokenStubLength, 0.f))};
	}

	const FVector2D End = GetInputAnchor(Edge.TargetNodeIndex);
	if (Edge.SourceNodeIndex == Edge.TargetNodeIndex)
	{
		// 自环：从端口向右出去，绕过节点底部，从左侧回到自己的标题行
		const FVector2D TopLeft = GetNodeTopLeft(Layout.Nodes[Edge.SourceNodeIndex]);
		const double Bottom = TopLeft.Y + GetNodeHeight(Edge.SourceNodeIndex) + LoopMargin * 0.5f;
		const double Right = Start.X + LoopMargin;
		const double Left = TopLeft.X - LoopMargin;
		return {
			ToFloatPoint(Start), ToFloatPoint(FVector2D(Right, Start.Y)), ToFloatPoint(FVector2D(Right, Bottom)),
			ToFloatPoint(FVector2D(Left, Bottom)), ToFloatPoint(FVector2D(Left, End.Y)), ToFloatPoint(End)
		};
	}

	if (Edge.ReturnLane != INDEX_NONE)
	{
		// 回边：在源列右侧的空隙里下到底部通道，向左走到目标列左侧的空隙，再上来接到标题行。
		// 竖直段按通道号错开几个像素，同一空隙里的多条回边不会完全重合。
		const double Offset = (Edge.ReturnLane % 4) * 3.0;
		const double LaneY = GetReturnLaneY(Edge.ReturnLane);
		const double OutX = Start.X + ReturnStub + Offset;
		const double InX = End.X - ReturnStub - Offset;
		return {
			ToFloatPoint(Start), ToFloatPoint(FVector2D(OutX, Start.Y)), ToFloatPoint(FVector2D(OutX, LaneY)),
			ToFloatPoint(FVector2D(InX, LaneY)), ToFloatPoint(FVector2D(InX, End.Y)), ToFloatPoint(End)
		};
	}

	// 普通边：蓝图式 S 形曲线，两端切线都朝右；布局保证目标在右侧的列
	const double Bend = FMath::Max(60.0, FMath::Abs(End.X - Start.X) * 0.5);
	return SampleSplinePath(Start, End, Bend);
}

TOptional<FBox2D> SCadenceArcGraphCanvas::GetNodeBounds(const int32 NodeIndex) const
{
	if (!Layout.Nodes.IsValidIndex(NodeIndex))
	{
		return {};
	}
	const FVector2D TopLeft = GetNodeTopLeft(Layout.Nodes[NodeIndex]);
	return FBox2D(TopLeft, TopLeft + FVector2D(NodeWidth, GetNodeHeight(NodeIndex)));
}

FVector2D SCadenceArcGraphCanvas::GetNodeTopLeft(const FCadenceArcLayoutNode& Node) const
{
	return FVector2D(Padding + Node.Column * ColumnSpacing, Padding + Node.Row * RowSpacing);
}

float SCadenceArcGraphCanvas::GetNodeHeight(const int32 NodeIndex) const
{
	const int32 Ports = PortCounts.IsValidIndex(NodeIndex) ? PortCounts[NodeIndex] : 0;
	return Ports > 0 ? HeaderHeight + Ports * PortHeight + NodeBottomPad : HeaderHeight;
}

FVector2D SCadenceArcGraphCanvas::GetPortAnchor(const FCadenceArcLayoutEdge& Edge) const
{
	return GetNodeTopLeft(Layout.Nodes[Edge.SourceNodeIndex])
		+ FVector2D(NodeWidth, HeaderHeight + (Edge.TransitionIndex + 0.5f) * PortHeight);
}

FVector2D SCadenceArcGraphCanvas::GetInputAnchor(const int32 NodeIndex) const
{
	return GetNodeTopLeft(Layout.Nodes[NodeIndex]) + FVector2D(0.f, HeaderHeight * 0.5f);
}

int32 SCadenceArcGraphCanvas::OnPaint(
	const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (Layout.Nodes.IsEmpty())
	{
		return LayerId;
	}

	const int32 EdgeLayer = LayerId; // 最底层：连线
	const int32 BodyLayer = LayerId + 1; // 节点主体
	const int32 HeaderLayer = LayerId + 2; // 标题行底色、端口圆点
	const int32 OutlineLayer = LayerId + 3; //高亮描边层
	const int32 TextLayer = LayerId + 4; // 最上层：文字
	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo PortFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush"); // 纯白画刷，靠 Tint 着色
	const FPaintGeometry CanvasGeometry = AllottedGeometry.ToPaintGeometry(); // 整块画布，点坐标用局部坐标
	const FCadenceArcGraphPalette Palette;

	// 焦点：已提交节点的出边（下一步可走的边）、候选边和预备边保持原样，其余的边淡化。
	// 没有已提交节点（Resolver 还没选中或未初始化）时不淡化。
	const auto IsFocusedEdge = [this](const int32 EdgeIndex)
	{
		const bool bPreparatory = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex)
			&& DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		return DebugView.CommittedNodeIndex == INDEX_NONE
			|| Layout.Edges[EdgeIndex].SourceNodeIndex == DebugView.CommittedNodeIndex
			|| EdgeIndex == DebugView.CandidateEdgeIndex || bPreparatory;
	};

	// 连线：从端口行右侧引出
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const bool bCandidateEdge = EdgeIndex == DebugView.CandidateEdgeIndex;
		const bool bPreparatoryEdge = !bCandidateEdge &&
			DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex) &&
			DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		const bool bCurrentRelease = EdgeIndex == DebugView.CurrentReleaseEdgeIndex;
		TArray<FVector2f> Path = BuildEdgePath(Edge);

		if (Edge.IsBrokenTarget())
		{
			// 目标不存在：短线末端加问号
			DrawLabel(
				OutDrawElements, TextLayer, AllottedGeometry,
				FVector2D(Path.Last().X + 3.f, Path.Last().Y - 8.f),
				FVector2D(16.f, 16.f), TEXT("?"), HeaderFont, BrokenColor
			);
		}

		if (bPreparatoryEdge)
		{
			DrawPreparatoryPath(
				OutDrawElements, EdgeLayer, CanvasGeometry, MoveTemp(Path),
				DebugView.PreparatoryEdgeProgress[EdgeIndex], bCurrentRelease,
				Palette.PreparatoryEdge, Palette.PreparatoryProgress);
			continue;
		}

		FLinearColor Color = bCandidateEdge
			? Palette.CandidateEdge
			: (Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex));
		if (!IsFocusedEdge(EdgeIndex))
		{
			Color.A = UnfocusedAlpha;
		}
		const float EdgeThickness = bCandidateEdge ? 3.f : 1.5f;
		if (Edge.ReturnLane != INDEX_NONE && !bCandidateEdge)
		{
			// 回边用虚线，一眼能看出是"往回跳"
			FSlateDrawElement::MakeDashedLines(
				OutDrawElements, EdgeLayer, CanvasGeometry, MoveTemp(Path),
				ESlateDrawEffect::None, Color, EdgeThickness, 8.f);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, EdgeLayer, CanvasGeometry, MoveTemp(Path),
				ESlateDrawEffect::None, Color, true, EdgeThickness);
		}
	}

	// 节点：主体、标题行、端口行
	for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
		const FVector2D TopLeft = GetNodeTopLeft(Node);
		const float Height = GetNodeHeight(NodeIndex);

		FSlateDrawElement::MakeBox(
			OutDrawElements, BodyLayer,
			AllottedGeometry.ToPaintGeometry(FVector2D(NodeWidth, Height), FSlateLayoutTransform(TopLeft)
			),
			WhiteBrush, ESlateDrawEffect::None,
			Node.bReachable ? BodyColor : UnreachableBodyColor); // 不可达节点调暗
		FSlateDrawElement::MakeBox(
			OutDrawElements, HeaderLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(NodeWidth, HeaderHeight), FSlateLayoutTransform(TopLeft)
			),
			WhiteBrush, ESlateDrawEffect::None,
			NodeIndex == DebugView.CommittedNodeIndex
				? Palette.CommittedNodeFill
				: (Node.bReachable ? HeaderColor : UnreachableHeaderColor));
		DrawLabel(OutDrawElements, TextLayer, AllottedGeometry, TopLeft + FVector2D(8.f, 4.f),
		          FVector2D(NodeWidth - 16.f, HeaderHeight),
		          ShortTagName(Node.ActionTag), HeaderFont,
		          FLinearColor::White);

		if (NodeIndex == DebugView.CurrentReleaseTargetNodeIndex &&
			NodeIndex != DebugView.CandidateTargetNodeIndex)
		{
			constexpr float OutlineMargin = 3.f;
			const FVector2D Min = TopLeft - FVector2D(OutlineMargin, OutlineMargin);
			const FVector2D Max = TopLeft + FVector2D(NodeWidth + OutlineMargin, Height + OutlineMargin);
			FSlateDrawElement::MakeDashedLines(
				OutDrawElements, OutlineLayer, CanvasGeometry,
				TArray<FVector2f>{
					ToFloatPoint(Min), ToFloatPoint(FVector2D(Max.X, Min.Y)),
					ToFloatPoint(Max), ToFloatPoint(FVector2D(Min.X, Max.Y)), ToFloatPoint(Min)
				},
				ESlateDrawEffect::None, Palette.PreparatoryEdge, 2.f, 6.f);
		}

		// 绘制候选目标节点的高亮描边
		if (NodeIndex == DebugView.CandidateTargetNodeIndex)
		{
			constexpr float OutlineMargin = 3.f;

			const FVector2D Min = TopLeft - FVector2D(OutlineMargin, OutlineMargin);
			const FVector2D Max = TopLeft
				+ FVector2D(NodeWidth + OutlineMargin, Height + OutlineMargin);

			FSlateDrawElement::MakeLines(
				OutDrawElements,
				OutlineLayer,
				CanvasGeometry,
				TArray{
					Min, FVector2D(Max.X, Min.Y),
					Max, FVector2D(Min.X, Max.Y),
					Min // 回到起点，闭合矩形
				},
				ESlateDrawEffect::None,
				Palette.CandidateOutline,
				true, // 抗锯齿
				2.f // 线宽
			);
		}
	}

	// 端口行：条件文字与连线同色，右侧一个小圆点作为引出点
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FLinearColor Color = Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex);
		const bool bPreparatoryEdge = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex) &&
			DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		FLinearColor DisplayColor = EdgeIndex == DebugView.CandidateEdgeIndex
			? Palette.CandidateEdge : (bPreparatoryEdge ? Palette.PreparatoryEdge : Color);
		if (!IsFocusedEdge(EdgeIndex))
		{
			DisplayColor.A = 0.5f; // 文字比连线淡得少一些，仍然要能读
		}
		const FVector2D TopLeft = GetNodeTopLeft(Layout.Nodes[Edge.SourceNodeIndex]);
		const FVector2D RowTopLeft = TopLeft + FVector2D(10.f, HeaderHeight + Edge.TransitionIndex * PortHeight + 1.f);
		DrawLabel(
			OutDrawElements, TextLayer, AllottedGeometry, RowTopLeft,
			FVector2D(NodeWidth - 24.f, PortHeight),
			EdgeLabels[EdgeIndex], PortFont,
			DisplayColor
		);

		const FVector2D Anchor = GetPortAnchor(Edge);
		FSlateDrawElement::MakeBox(
			OutDrawElements, HeaderLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2D(6.f, 6.f),
				FSlateLayoutTransform(Anchor - FVector2D(3.f, 3.f))
			),
			WhiteBrush, ESlateDrawEffect::None,
			DisplayColor);
	}
	return TextLayer;
}
