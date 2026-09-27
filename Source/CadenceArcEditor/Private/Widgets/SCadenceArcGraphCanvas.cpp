#include "SCadenceArcGraphCanvas.h"

#include "Graph/CadenceArcGraph.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float NodeWidth = 180.f;
	constexpr float HeaderHeight = 24.f;   // 节点标题行
	constexpr float PortHeight = 18.f;     // 每条出边一行
	constexpr float NodeBottomPad = 4.f;   // 有端口的节点底部留白
	constexpr float ColumnSpacing = 300.f; // 相邻两列左边缘的距离，剩下的空间留给曲线
	constexpr float RowGap = 28.f;         // 同一列上下两个节点之间的空隙
	constexpr float Padding = 24.f;
	constexpr float LoopMargin = 14.f;     // 自环绕出节点的距离
	constexpr float BrokenStubLength = 24.f;

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
	void DrawLabel(FSlateWindowElementList& OutDrawElements, const int32 Layer, const FGeometry& Geometry,
	               const FVector2D& TopLeft, const FVector2D& Size, const FString& Text,
	               const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
		                            Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
		                            Text, Font, ESlateDrawEffect::None, Color);
	}
}

void SCadenceArcGraphCanvas::SetGraph(const UCadenceArcGraph* InGraph)
{
	Graph = InGraph;
	Layout = InGraph ? BuildGraphLayout(*InGraph) : FCadenceArcGraphLayout{};

	PortCounts.Init(0, Layout.Nodes.Num());
	EdgeLabels.Reset(Layout.Edges.Num());
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		++PortCounts[Edge.SourceNodeIndex];
		// 布局刚由同一张图生成，这里的下标一定有效；之后 OnPaint 只用缓存的文字
		EdgeLabels.Add(FormatTransitionLabel(
			InGraph->Nodes[Edge.SourceNodeIndex].Transitions[Edge.TransitionIndex]));
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
	// 右侧留出自环和坏目标短线的位置，下方留出自环绕过节点底部的位置
	const float Width = (Layout.NumColumns - 1) * ColumnSpacing + NodeWidth + BrokenStubLength + 16.f;
	const float Height = (Layout.MaxRows - 1) * RowSpacing + MaxNodeHeight + LoopMargin;
	return FVector2D(Width + 2.f * Padding, Height + 2.f * Padding);
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

	const int32 EdgeLayer = LayerId;       // 最底层：连线
	const int32 BodyLayer = LayerId + 1;   // 节点主体
	const int32 HeaderLayer = LayerId + 2; // 标题行底色、端口圆点
	const int32 TextLayer = LayerId + 3;   // 最上层：文字
	const FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	const FSlateFontInfo PortFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush"); // 纯白画刷，靠 Tint 着色
	const FPaintGeometry CanvasGeometry = AllottedGeometry.ToPaintGeometry(); // 整块画布，点坐标用局部坐标

	// 1. 连线：从端口行右侧引出
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FVector2D Start = GetPortAnchor(Edge);

		if (Edge.IsBrokenTarget())
		{
			// 目标不存在：一小段红线加问号，不猜它原本想连到哪里
			const FVector2D StubEnd = Start + FVector2D(BrokenStubLength, 0.f);
			FSlateDrawElement::MakeLines(OutDrawElements, EdgeLayer, CanvasGeometry,
			                             TArray<FVector2D>{Start, StubEnd}, ESlateDrawEffect::None,
			                             BrokenColor, true, 1.5f);
			DrawLabel(OutDrawElements, TextLayer, AllottedGeometry, StubEnd + FVector2D(3.f, -8.f),
			          FVector2D(16.f, 16.f), TEXT("?"), HeaderFont, BrokenColor);
			continue;
		}

		const FLinearColor Color = ColorForEdge(EdgeIndex);
		const FVector2D End = GetInputAnchor(Edge.TargetNodeIndex);

		if (Edge.SourceNodeIndex == Edge.TargetNodeIndex)
		{
			// 自环：从端口向右出去，绕过节点底部，从左侧回到自己的标题行
			const FVector2D TopLeft = GetNodeTopLeft(Layout.Nodes[Edge.SourceNodeIndex]);
			const double Bottom = TopLeft.Y + GetNodeHeight(Edge.SourceNodeIndex) + LoopMargin * 0.5f;
			const double Right = Start.X + LoopMargin;
			const double Left = TopLeft.X - LoopMargin;
			FSlateDrawElement::MakeLines(OutDrawElements, EdgeLayer, CanvasGeometry,
			                             TArray<FVector2D>{
				                             Start, FVector2D(Right, Start.Y), FVector2D(Right, Bottom),
				                             FVector2D(Left, Bottom), FVector2D(Left, End.Y), End
			                             },
			                             ESlateDrawEffect::None, Color, true, 1.5f);
			continue;
		}

		// 普通边：蓝图式 S 形曲线，两端切线都朝右。回边（目标在左侧）会从右侧绕回来。
		const double Bend = FMath::Max(60.0, FMath::Abs(End.X - Start.X) * 0.5);
		FSlateDrawElement::MakeSpline(OutDrawElements, EdgeLayer, CanvasGeometry,
		                              Start, FVector2D(Bend, 0.f), End, FVector2D(Bend, 0.f),
		                              /*Thickness=*/1.5f, ESlateDrawEffect::None, Color);
	}

	// 2. 节点：主体、标题行、端口行
	for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
		const FVector2D TopLeft = GetNodeTopLeft(Node);
		const float Height = GetNodeHeight(NodeIndex);

		FSlateDrawElement::MakeBox(OutDrawElements, BodyLayer,
		                           AllottedGeometry.ToPaintGeometry(FVector2D(NodeWidth, Height),
		                                                            FSlateLayoutTransform(TopLeft)),
		                           WhiteBrush, ESlateDrawEffect::None,
		                           Node.bReachable ? BodyColor : UnreachableBodyColor); // 不可达节点调暗
		FSlateDrawElement::MakeBox(OutDrawElements, HeaderLayer,
		                           AllottedGeometry.ToPaintGeometry(FVector2D(NodeWidth, HeaderHeight),
		                                                            FSlateLayoutTransform(TopLeft)),
		                           WhiteBrush, ESlateDrawEffect::None,
		                           Node.bReachable ? HeaderColor : UnreachableHeaderColor);
		DrawLabel(OutDrawElements, TextLayer, AllottedGeometry, TopLeft + FVector2D(8.f, 4.f),
		          FVector2D(NodeWidth - 16.f, HeaderHeight), ShortTagName(Node.ActionTag), HeaderFont,
		          FLinearColor::White);
	}

	// 3. 端口行：条件文字与连线同色，右侧一个小圆点作为引出点
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FLinearColor Color = Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex);
		const FVector2D TopLeft = GetNodeTopLeft(Layout.Nodes[Edge.SourceNodeIndex]);
		const FVector2D RowTopLeft = TopLeft + FVector2D(10.f, HeaderHeight + Edge.TransitionIndex * PortHeight + 1.f);
		DrawLabel(OutDrawElements, TextLayer, AllottedGeometry, RowTopLeft,
		          FVector2D(NodeWidth - 24.f, PortHeight), EdgeLabels[EdgeIndex], PortFont, Color);

		const FVector2D Anchor = GetPortAnchor(Edge);
		FSlateDrawElement::MakeBox(OutDrawElements, HeaderLayer,
		                           AllottedGeometry.ToPaintGeometry(FVector2D(6.f, 6.f),
		                                                            FSlateLayoutTransform(Anchor - FVector2D(3.f, 3.f))),
		                           WhiteBrush, ESlateDrawEffect::None, Color);
	}

	return TextLayer;
}
