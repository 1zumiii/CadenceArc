#include "SCadenceArcGraphCanvas.h"

#include "Graph/CadenceArcGraph.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	// 分支聚焦的三档透明度：下一步（及候选、预备边）不透明；之后还能走到的一档；这条路上已经走不到的最淡
	constexpr float OnPathEdgeAlpha = 0.55f;
	constexpr float OffPathEdgeAlpha = 0.12f;
	constexpr float OnPathLabelAlpha = 0.8f;
	constexpr float OffPathLabelAlpha = 0.3f;
	constexpr float OffPathNodeAlpha = 0.35f;
	const FLinearColor NextStepHeaderColor(0.36f, 0.38f, 0.50f); // 下一步节点的标题行稍亮

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

	// 终点箭头：沿路径最后一段的方向，落在目标左侧的空隙里
	void DrawArrowHead(
		FSlateWindowElementList& OutDrawElements, const int32 Layer, const FPaintGeometry& CanvasGeometry,
		const TArray<FVector2D>& Path, const FLinearColor& Color, const float Thickness)
	{
		if (Path.Num() < 2)
		{
			return;
		}
		const FVector2D Tip = Path.Last();
		const FVector2D Direction = (Tip - Path[Path.Num() - 2]).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return;
		}
		const FVector2D Normal(-Direction.Y, Direction.X);
		constexpr double Length = 7.0;
		constexpr double HalfWidth = 4.0;
		FSlateDrawElement::MakeLines(
			OutDrawElements, Layer, CanvasGeometry,
			TArray<FVector2f>{
				ToFloatPoint(Tip - Direction * Length + Normal * HalfWidth), ToFloatPoint(Tip),
				ToFloatPoint(Tip - Direction * Length - Normal * HalfWidth)
			},
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	TArray<FVector2f> ToFloatPath(const TArray<FVector2D>& Path)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Path.Num());
		for (const FVector2D& Point : Path)
		{
			Points.Add(ToFloatPoint(Point));
		}
		return Points;
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
	Layout = InGraph ? BuildGraphLayout(*InGraph, Params) : FCadenceArcGraphLayout{};

	EdgeLabels.Reset(Layout.Edges.Num());
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		EdgeLabels.Add(FormatTransitionLabel(Edge.Transition));
	}

	// 尺寸可能变了（布局阶段），画面也要重画（绘制阶段）
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

FVector2D SCadenceArcGraphCanvas::ComputeDesiredSize(float) const
{
	return (Layout.Nodes.IsEmpty() ? FVector2D(2.0 * Params.Padding, 2.0 * Params.Padding) : Layout.Size) * Zoom;
}

void SCadenceArcGraphCanvas::SetZoom(const float InZoom)
{
	if (InZoom != Zoom)
	{
		Zoom = InZoom;
		Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
	}
}

TOptional<FBox2D> SCadenceArcGraphCanvas::GetNodeLayoutBounds(const int32 NodeIndex) const
{
	if (!Layout.Nodes.IsValidIndex(NodeIndex))
	{
		return {};
	}
	const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
	return FBox2D(Node.Position, Node.Position + Node.Size);
}

TOptional<FBox2D> SCadenceArcGraphCanvas::GetNodeBounds(const int32 NodeIndex) const
{
	if (!Layout.Nodes.IsValidIndex(NodeIndex))
	{
		return {};
	}
	const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
	return FBox2D(Node.Position * Zoom, (Node.Position + Node.Size) * Zoom);
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
	// 所有绘制都用布局坐标；缩放放在这层子几何上，文字、线宽和节点一起按比例缩放
	const FGeometry Geometry = AllottedGeometry.MakeChild(Layout.Size, FSlateLayoutTransform(Zoom));

	const int32 EdgeLayer = LayerId; // 最底层：连线
	const int32 BodyLayer = LayerId + 1; // 节点主体
	const int32 HeaderLayer = LayerId + 2; // 标题行底色、端口圆点
	const int32 OutlineLayer = LayerId + 3; //高亮描边层
	const int32 TextLayer = LayerId + 4; // 最上层：文字
	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo PortFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush"); // 纯白画刷，靠 Tint 着色
	const FPaintGeometry CanvasGeometry = Geometry.ToPaintGeometry(); // 整块画布，点坐标用局部坐标
	const FCadenceArcGraphPalette Palette;

	// 分支聚焦：按"从已提交节点出发还要几步"分档。
	// 已提交节点的出边（下一步）、候选边、预备边不淡化；源节点还能走到的边中等；
	// 源节点在这条路上已经走不到的边最淡。没有已提交节点时（Resolver 未选中或未初始化）都不淡化。
	enum class EEmphasis : uint8 { Full, OnPath, OffPath };
	const auto NodeDistance = [this](const int32 NodeIndex)
	{
		return DebugView.NodeDistance.IsValidIndex(NodeIndex) ? DebugView.NodeDistance[NodeIndex] : 0;
	};
	const auto EdgeEmphasis = [this, &NodeDistance](const int32 EdgeIndex)
	{
		const bool bPreparatory = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex)
			&& DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		const int32 Source = Layout.Edges[EdgeIndex].SourceNodeIndex;
		if (DebugView.CommittedNodeIndex == INDEX_NONE || Source == DebugView.CommittedNodeIndex
			|| EdgeIndex == DebugView.CandidateEdgeIndex || bPreparatory)
		{
			return EEmphasis::Full;
		}
		return NodeDistance(Source) == INDEX_NONE ? EEmphasis::OffPath : EEmphasis::OnPath;
	};

	// 连线：路径由布局给出，第一个点是端口，最后一个点是接入点
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		if (Edge.Path.Num() < 2)
		{
			continue;
		}
		const bool bCandidateEdge = EdgeIndex == DebugView.CandidateEdgeIndex;
		const bool bPreparatoryEdge = !bCandidateEdge &&
			DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex) &&
			DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		const bool bCurrentRelease = EdgeIndex == DebugView.CurrentReleaseEdgeIndex;

		if (Edge.IsBrokenTarget())
		{
			// 目标不存在：短线末端加问号
			DrawLabel(
				OutDrawElements, TextLayer, Geometry,
				FVector2D(Edge.Path.Last().X + 3.f, Edge.Path.Last().Y - 8.f),
				FVector2D(16.f, 16.f), TEXT("?"), HeaderFont, BrokenColor
			);
		}

		if (bPreparatoryEdge)
		{
			DrawPreparatoryPath(
				OutDrawElements, EdgeLayer, CanvasGeometry, ToFloatPath(Edge.Path),
				DebugView.PreparatoryEdgeProgress[EdgeIndex], bCurrentRelease,
				Palette.PreparatoryEdge, Palette.PreparatoryProgress);
			if (!Edge.IsBrokenTarget())
			{
				DrawArrowHead(OutDrawElements, EdgeLayer, CanvasGeometry, Edge.Path, Palette.PreparatoryEdge,
				              bCurrentRelease ? 2.5f : 1.5f);
			}
			continue;
		}

		FLinearColor Color = bCandidateEdge
			? Palette.CandidateEdge
			: (Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex));
		switch (EdgeEmphasis(EdgeIndex))
		{
		case EEmphasis::OnPath: Color.A = OnPathEdgeAlpha; break;
		case EEmphasis::OffPath: Color.A = OffPathEdgeAlpha; break;
		default: break;
		}
		const float EdgeThickness = bCandidateEdge ? 3.f : 1.5f;
		if (Edge.ReturnLane != INDEX_NONE && !bCandidateEdge)
		{
			// 回边用虚线，一眼能看出是"往回跳"
			FSlateDrawElement::MakeDashedLines(
				OutDrawElements, EdgeLayer, CanvasGeometry, ToFloatPath(Edge.Path),
				ESlateDrawEffect::None, Color, EdgeThickness, 8.f);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, EdgeLayer, CanvasGeometry, ToFloatPath(Edge.Path),
				ESlateDrawEffect::None, Color, true, EdgeThickness);
		}
		if (!Edge.IsBrokenTarget())
		{
			DrawArrowHead(OutDrawElements, EdgeLayer, CanvasGeometry, Edge.Path, Color, EdgeThickness);
		}
	}

	// 节点：主体、标题行
	for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
		const FVector2D TopLeft = Node.Position;
		const FVector2D Size = Node.Size;
		// 这条路上已经走不到的节点整体变淡；下一步节点的标题行稍亮
		const bool bOffPath = NodeDistance(NodeIndex) == INDEX_NONE;
		const float NodeAlpha = bOffPath ? OffPathNodeAlpha : 1.f;
		FLinearColor BodyFill = Node.bReachable ? BodyColor : UnreachableBodyColor; // 不可达节点调暗
		FLinearColor HeaderFill = NodeIndex == DebugView.CommittedNodeIndex
			? Palette.CommittedNodeFill
			: (NodeDistance(NodeIndex) == 1 ? NextStepHeaderColor : (Node.bReachable ? HeaderColor : UnreachableHeaderColor));
		BodyFill.A *= NodeAlpha;
		HeaderFill.A *= NodeAlpha;

		FSlateDrawElement::MakeBox(
			OutDrawElements, BodyLayer,
			Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			WhiteBrush, ESlateDrawEffect::None, BodyFill);
		FSlateDrawElement::MakeBox(
			OutDrawElements, HeaderLayer,
			Geometry.ToPaintGeometry(
				FVector2D(Size.X, Params.HeaderHeight), FSlateLayoutTransform(TopLeft)
			),
			WhiteBrush, ESlateDrawEffect::None, HeaderFill);
		DrawLabel(OutDrawElements, TextLayer, Geometry, TopLeft + FVector2D(8.f, 4.f),
		          FVector2D(Size.X - 16.f, Params.HeaderHeight),
		          ShortTagName(Node.ActionTag), HeaderFont,
		          FLinearColor(1.f, 1.f, 1.f, bOffPath ? 0.4f : 1.f));

		constexpr float OutlineMargin = 3.f;
		const FVector2D Min = TopLeft - FVector2D(OutlineMargin, OutlineMargin);
		const FVector2D Max = TopLeft + Size + FVector2D(OutlineMargin, OutlineMargin);
		if (NodeIndex == DebugView.CurrentReleaseTargetNodeIndex &&
			NodeIndex != DebugView.CandidateTargetNodeIndex)
		{
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

	// 端口行：条件文字与连线同色，右侧一个小圆点作为引出点（即路径的第一个点）
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FLinearColor Color = Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex);
		const bool bPreparatoryEdge = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex) &&
			DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		FLinearColor DisplayColor = EdgeIndex == DebugView.CandidateEdgeIndex
			? Palette.CandidateEdge : (bPreparatoryEdge ? Palette.PreparatoryEdge : Color);
		switch (EdgeEmphasis(EdgeIndex)) // 文字比连线淡得少一些，仍然要能读
		{
		case EEmphasis::OnPath: DisplayColor.A = OnPathLabelAlpha; break;
		case EEmphasis::OffPath: DisplayColor.A = OffPathLabelAlpha; break;
		default: break;
		}
		const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
		const FVector2D RowTopLeft = Source.Position
			+ FVector2D(10.f, Params.HeaderHeight + Edge.TransitionIndex * Params.PortHeight + 1.f);
		DrawLabel(
			OutDrawElements, TextLayer, Geometry, RowTopLeft,
			FVector2D(Source.Size.X - 24.f, Params.PortHeight),
			EdgeLabels[EdgeIndex], PortFont,
			DisplayColor
		);

		if (!Edge.Path.IsEmpty())
		{
			FSlateDrawElement::MakeBox(
				OutDrawElements, HeaderLayer,
				Geometry.ToPaintGeometry(
					FVector2D(6.f, 6.f),
					FSlateLayoutTransform(Edge.Path[0] - FVector2D(3.f, 3.f))
				),
				WhiteBrush, ESlateDrawEffect::None,
				DisplayColor);
		}
	}
	return TextLayer;
}
