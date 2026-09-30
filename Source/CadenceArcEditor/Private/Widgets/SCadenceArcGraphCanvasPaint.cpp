// SCadenceArcGraphCanvas 的绘制部分。OnPaint 按图层从下往上调用各个 Paint 函数：
// 紧凑链分组框 -> 连线（含引用标签）-> 历史焦点边 -> 节点 -> 端口行 -> 悬停提示。
// 所有几何都来自布局快照，这里只决定颜色、粗细和透明度。

#include "SCadenceArcGraphCanvas.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "CadenceArcCanvasDrawing.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

struct SCadenceArcGraphCanvas::FPaintContext
{
	FSlateWindowElementList& Out;
	FGeometry Geometry; // 布局坐标：缩放放在这层子几何上，文字、线宽和节点一起按比例缩放
	FPaintGeometry CanvasGeometry; // 整块画布，点坐标用局部坐标
	int32 GroupLayer = 0; // 最底层：紧凑链的分组边框
	int32 EdgeLayer = 0; // 连线
	int32 BodyLayer = 0; // 节点主体、引用标签底色
	int32 HeaderLayer = 0; // 标题行底色、端口圆点
	int32 OutlineLayer = 0; // 高亮描边
	int32 TextLayer = 0; // 文字
	FSlateFontInfo HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	FSlateFontInfo PortFont = FCoreStyle::GetDefaultFontStyle("Regular", 8);
	FSlateFontInfo TagFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush"); // 纯白画刷，靠 Tint 着色
	FCadenceArcGraphPalette Palette;
};

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
	const FGeometry Geometry = AllottedGeometry.MakeChild(Layout.Size, FSlateLayoutTransform(Zoom));
	FPaintContext Paint{OutDrawElements, Geometry, Geometry.ToPaintGeometry()};
	Paint.GroupLayer = LayerId;
	Paint.EdgeLayer = LayerId + 1;
	Paint.BodyLayer = LayerId + 2;
	Paint.HeaderLayer = LayerId + 3;
	Paint.OutlineLayer = LayerId + 4;
	Paint.TextLayer = LayerId + 5;

	PaintChainGroups(Paint);
	PaintEdges(Paint);
	PaintHistoryFocusEdge(Paint);
	PaintNodes(Paint);
	PaintPorts(Paint);
	return PaintHoverTooltip(Paint);
}

int32 SCadenceArcGraphCanvas::GetNodeDistance(const int32 NodeIndex) const
{
	return DebugView.NodeDistance.IsValidIndex(NodeIndex) ? DebugView.NodeDistance[NodeIndex] : 0;
}

SCadenceArcGraphCanvas::EEdgeEmphasis SCadenceArcGraphCanvas::GetEdgeEmphasis(const int32 EdgeIndex) const
{
	// 已提交节点的出边（下一步）、候选边、预备边不淡化；源节点还能走到的边中等；
	// 源节点在这条路上已经走不到的边最淡。没有已提交节点时（Resolver 未选中或未初始化）都不淡化。
	const bool bPreparatory = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex)
		&& DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
	const int32 Source = Layout.Edges[EdgeIndex].SourceNodeIndex;
	if (DebugView.CommittedNodeIndex == INDEX_NONE || Source == DebugView.CommittedNodeIndex
		|| EdgeIndex == DebugView.CandidateEdgeIndex || bPreparatory)
	{
		return EEdgeEmphasis::Full;
	}
	return GetNodeDistance(Source) == INDEX_NONE ? EEdgeEmphasis::OffPath : EEdgeEmphasis::OnPath;
}

void SCadenceArcGraphCanvas::PaintChainGroups(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// 分组只标明纵向折叠关系，不参与命中或扩大画布；连线和运行时高亮仍在它上方。
	for (const FCadenceArcLayoutChain& Chain : Layout.FoldedChains)
	{
		if (Chain.Bounds.bIsValid)
		{
			FSlateDrawElement::MakeLines(
				Paint.Out, Paint.GroupLayer, Paint.CanvasGeometry, BoxOutline(Chain.Bounds),
				ESlateDrawEffect::None, ChainGroupColor, true, 1.f);
		}
	}
}

void SCadenceArcGraphCanvas::PaintReferenceEnds(
	const FPaintContext& Paint, const FCadenceArcLayoutEdge& Edge, const FLinearColor& LineColor,
	const FLinearColor& TextColor, const float Thickness, const float Progress) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// 源一侧是一个圆角标签：底色带一点边的颜色，边框是边的颜色，目标名在左侧区域里水平、垂直居中，
	// 右端一个分隔线加方向图标（往前 / 掉头往回）；预备边在标签里从左往右填进度。
	// 目标一侧是一小段带箭头的接入线，尾端一个圆点，表示"从别处接过来"。标签底色盖在其他连线上面。
	static const FSlateRoundedBoxBrush TagFillBrush(FLinearColor::White, ReferenceTagRadius);
	static const FSlateRoundedBoxBrush DotBrush(FLinearColor::White, 3.f);
	const FBox2D& Box = Edge.ReferenceBox;
	const FVector2D Size = Box.GetSize();
	const float Alpha = TextColor.A;
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.09f, 0.09f, 0.11f), LineColor, 0.22f).CopyWithNewOpacity(0.97f * Alpha);
	FSlateDrawElement::MakeBox(Paint.Out, Paint.BodyLayer, Paint.Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Box.Min)),
	                           &TagFillBrush, ESlateDrawEffect::None, Fill);
	if (Progress > 0.f)
	{
		FSlateDrawElement::MakeBox(
			Paint.Out, Paint.HeaderLayer,
			Paint.Geometry.ToPaintGeometry(FVector2D(Size.X * FMath::Min(Progress, 1.f), Size.Y), FSlateLayoutTransform(Box.Min)),
			&TagFillBrush, ESlateDrawEffect::None, Paint.Palette.PreparatoryProgress.CopyWithNewOpacity(0.4f));
	}
	// 边框比连线稍亮一些，淡化时也能看出是个标签；悬停、候选时和连线一样加粗
	FSlateDrawElement::MakeLines(Paint.Out, Paint.OutlineLayer, Paint.CanvasGeometry, RoundedOutline(Box, ReferenceTagRadius),
	                             ESlateDrawEffect::None, LineColor.CopyWithNewOpacity(FMath::Max(LineColor.A, 0.6f * Alpha)),
	                             true, FMath::Max(1.f, Thickness - 0.5f));

	// 右端图标区：一条竖分隔线 + 方向图标
	constexpr double IconWidth = 16.0;
	const double DividerX = Box.Max.X - IconWidth;
	const FLinearColor IconColor = FMath::Lerp(FLinearColor::White, LineColor, 0.35f).CopyWithNewOpacity(Alpha);
	FSlateDrawElement::MakeLines(
		Paint.Out, Paint.TextLayer, Paint.CanvasGeometry,
		TArray<FVector2f>{ToFloatPoint(FVector2D(DividerX, Box.Min.Y + 3.0)), ToFloatPoint(FVector2D(DividerX, Box.Max.Y - 3.0))},
		ESlateDrawEffect::None, LineColor.CopyWithNewOpacity(0.5f * Alpha), true, 1.f);
	const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
	const FCadenceArcLayoutNode& Target = Layout.Nodes[Edge.TargetNodeIndex];
	const bool bJumpsBack = Target.Column <= Source.Column;
	DrawReferenceIcon(Paint.Out, Paint.TextLayer, Paint.CanvasGeometry,
	                  FVector2D(Box.Max.X - IconWidth * 0.5, Box.GetCenter().Y), bJumpsBack, IconColor);

	// 目标名：在分隔线左侧的区域里居中
	const double TextArea = DividerX - Box.Min.X - 8.0;
	const FString Name = FitText(ShortTagName(Target.ActionTag), Paint.TagFont, TextArea);
	const FVector2D TextSize = MeasureText(Name, Paint.TagFont);
	const FVector2D TextTopLeft(Box.Min.X + 4.0 + (TextArea - TextSize.X) * 0.5, Box.GetCenter().Y - TextSize.Y * 0.5);
	DrawLabel(Paint.Out, Paint.TextLayer, Paint.Geometry, TextTopLeft, TextSize + FVector2D(2.0, 0.0), Name, Paint.TagFont,
	          FMath::Lerp(FLinearColor::White, LineColor, 0.25f).CopyWithNewOpacity(Alpha));

	if (Edge.EntryStub.Num() >= 2)
	{
		FSlateDrawElement::MakeLines(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.EntryStub),
		                             ESlateDrawEffect::None, LineColor, true, Thickness);
		DrawArrowHead(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, Edge.EntryStub, LineColor, Thickness);
		FSlateDrawElement::MakeBox(
			Paint.Out, Paint.EdgeLayer,
			Paint.Geometry.ToPaintGeometry(FVector2D(6.0, 6.0), FSlateLayoutTransform(Edge.EntryStub[0] - FVector2D(3.0, 3.0))),
			&DotBrush, ESlateDrawEffect::None, LineColor);
	}
}

void SCadenceArcGraphCanvas::PaintEdges(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// 路径由布局给出，第一个点是端口，最后一个点是接入点
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
			DrawLabel(Paint.Out, Paint.TextLayer, Paint.Geometry,
			          FVector2D(Edge.Path.Last().X + 3.f, Edge.Path.Last().Y - 8.f),
			          FVector2D(16.f, 16.f), TEXT("?"), Paint.HeaderFont, BrokenColor);
		}

		if (bPreparatoryEdge)
		{
			DrawPreparatoryPath(
				Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.Path),
				DebugView.PreparatoryEdgeProgress[EdgeIndex], bCurrentRelease,
				Paint.Palette.PreparatoryEdge, Paint.Palette.PreparatoryProgress);
			if (Edge.bIsReference)
			{
				PaintReferenceEnds(Paint, Edge, Paint.Palette.PreparatoryEdge, Paint.Palette.PreparatoryEdge,
				                   bCurrentRelease ? 2.5f : 1.5f, DebugView.PreparatoryEdgeProgress[EdgeIndex]);
			}
			else if (!Edge.IsBrokenTarget())
			{
				DrawArrowHead(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, Edge.Path, Paint.Palette.PreparatoryEdge,
				              bCurrentRelease ? 2.5f : 1.5f);
			}
			continue;
		}

		FLinearColor Color = bCandidateEdge
			? Paint.Palette.CandidateEdge
			: (Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex));
		float LabelAlpha = 1.f; // 引用标记上的文字，和端口行文字一样比连线淡得少
		switch (GetEdgeEmphasis(EdgeIndex))
		{
		case EEdgeEmphasis::OnPath: Color.A = OnPathEdgeAlpha; LabelAlpha = OnPathLabelAlpha; break;
		case EEdgeEmphasis::OffPath: Color.A = OffPathEdgeAlpha; LabelAlpha = OffPathLabelAlpha; break;
		default: break;
		}
		// 悬停：鼠标下的边，或者鼠标下节点的出入边，不透明并加粗
		const bool bHovered = EdgeIndex == HoveredEdge || (HoveredNode != INDEX_NONE
			&& (Edge.SourceNodeIndex == HoveredNode || Edge.TargetNodeIndex == HoveredNode));
		if (bHovered)
		{
			Color.A = 1.f;
			LabelAlpha = 1.f;
		}
		const float EdgeThickness = (bCandidateEdge ? 3.f : 1.5f) + (bHovered ? 1.5f : 0.f);
		if (Edge.ReturnLane != INDEX_NONE && !bCandidateEdge)
		{
			// 回边用虚线，一眼能看出是"往回跳"
			FSlateDrawElement::MakeDashedLines(
				Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.Path),
				ESlateDrawEffect::None, Color, EdgeThickness, 8.f);
		}
		else
		{
			FSlateDrawElement::MakeLines(
				Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.Path),
				ESlateDrawEffect::None, Color, true, EdgeThickness);
		}
		if (Edge.bIsReference)
		{
			PaintReferenceEnds(Paint, Edge, Color, Color.CopyWithNewOpacity(LabelAlpha), EdgeThickness, 0.f);
		}
		else if (!Edge.IsBrokenTarget())
		{
			DrawArrowHead(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, Edge.Path, Color, EdgeThickness);
		}
	}
}

void SCadenceArcGraphCanvas::PaintHistoryFocusEdge(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// Arc History 点中的记录对应的边：紫色粗线盖在其他连线之上；引用边连同两端的标记一起描
	if (!Layout.Edges.IsValidIndex(FocusEdge) || Layout.Edges[FocusEdge].Path.Num() < 2)
	{
		return;
	}
	const FCadenceArcLayoutEdge& Edge = Layout.Edges[FocusEdge];
	FSlateDrawElement::MakeLines(
		Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.Path),
		ESlateDrawEffect::None, HistoryFocusColor, true, 4.f);
	if (Edge.bIsReference)
	{
		FSlateDrawElement::MakeLines(Paint.Out, Paint.OutlineLayer, Paint.CanvasGeometry,
		                             RoundedOutline(Edge.ReferenceBox.ExpandBy(2.0), ReferenceTagRadius + 2.0),
		                             ESlateDrawEffect::None, HistoryFocusColor, true, 2.5f);
		FSlateDrawElement::MakeLines(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, ToFloatPath(Edge.EntryStub),
		                             ESlateDrawEffect::None, HistoryFocusColor, true, 4.f);
		DrawArrowHead(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, Edge.EntryStub, HistoryFocusColor, 3.f);
	}
	else
	{
		DrawArrowHead(Paint.Out, Paint.EdgeLayer, Paint.CanvasGeometry, Edge.Path, HistoryFocusColor, 3.f);
	}
}

void SCadenceArcGraphCanvas::PaintNodes(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	for (int32 NodeIndex = 0; NodeIndex < Layout.Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
		const FVector2D TopLeft = Node.Position;
		const FVector2D Size = Node.Size;
		// 这条路上已经走不到的节点整体变淡；下一步节点的标题行稍亮
		const bool bOffPath = GetNodeDistance(NodeIndex) == INDEX_NONE;
		const float NodeAlpha = bOffPath ? OffPathNodeAlpha : 1.f;
		FLinearColor BodyFill = Node.bReachable ? BodyColor : UnreachableBodyColor; // 不可达节点调暗
		FLinearColor HeaderFill = NodeIndex == DebugView.CommittedNodeIndex
			? Paint.Palette.CommittedNodeFill
			: (GetNodeDistance(NodeIndex) == 1 ? NextStepHeaderColor : (Node.bReachable ? HeaderColor : UnreachableHeaderColor));
		BodyFill.A *= NodeAlpha;
		HeaderFill.A *= NodeAlpha;

		FSlateDrawElement::MakeBox(
			Paint.Out, Paint.BodyLayer, Paint.Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
			Paint.WhiteBrush, ESlateDrawEffect::None, BodyFill);
		FSlateDrawElement::MakeBox(
			Paint.Out, Paint.HeaderLayer,
			Paint.Geometry.ToPaintGeometry(FVector2D(Size.X, Params.HeaderHeight), FSlateLayoutTransform(TopLeft)),
			Paint.WhiteBrush, ESlateDrawEffect::None, HeaderFill);
		DrawLabel(Paint.Out, Paint.TextLayer, Paint.Geometry, TopLeft + FVector2D(8.f, 4.f),
		          FVector2D(Size.X - 16.f, Params.HeaderHeight), ShortTagName(Node.ActionTag), Paint.HeaderFont,
		          FLinearColor(1.f, 1.f, 1.f, bOffPath ? 0.4f : 1.f));

		constexpr float OutlineMargin = 3.f;
		const FVector2D Min = TopLeft - FVector2D(OutlineMargin, OutlineMargin);
		const FVector2D Max = TopLeft + Size + FVector2D(OutlineMargin, OutlineMargin);
		const FBox2D OutlineBox(Min, Max);
		// 当前松手会选中的蓄力档位的目标：橙色虚线框（已经是候选目标时让位给黄色实线框）
		if (NodeIndex == DebugView.CurrentReleaseTargetNodeIndex && NodeIndex != DebugView.CandidateTargetNodeIndex)
		{
			FSlateDrawElement::MakeDashedLines(
				Paint.Out, Paint.OutlineLayer, Paint.CanvasGeometry, BoxOutline(OutlineBox),
				ESlateDrawEffect::None, Paint.Palette.PreparatoryEdge, 2.f, 6.f);
		}
		// 候选目标节点：黄色实线框
		if (NodeIndex == DebugView.CandidateTargetNodeIndex)
		{
			FSlateDrawElement::MakeLines(
				Paint.Out, Paint.OutlineLayer, Paint.CanvasGeometry, BoxOutline(OutlineBox),
				ESlateDrawEffect::None, Paint.Palette.CandidateOutline, true, 2.f);
		}

		// Arc History 点中的节点（紫色），以及鼠标悬停的节点或悬停边的两端（白色）
		const bool bHistoryFocus = FocusNodes.Contains(NodeIndex);
		const bool bHoverOutline = NodeIndex == HoveredNode || (Layout.Edges.IsValidIndex(HoveredEdge)
			&& (Layout.Edges[HoveredEdge].SourceNodeIndex == NodeIndex
				|| Layout.Edges[HoveredEdge].TargetNodeIndex == NodeIndex));
		if (bHistoryFocus || bHoverOutline)
		{
			const double Pad = bHistoryFocus ? 6.0 : 1.5;
			FSlateDrawElement::MakeLines(
				Paint.Out, Paint.OutlineLayer, Paint.CanvasGeometry,
				BoxOutline(FBox2D(TopLeft - FVector2D(Pad), TopLeft + Size + FVector2D(Pad))),
				ESlateDrawEffect::None, bHistoryFocus ? HistoryFocusColor : FLinearColor::White, true,
				bHistoryFocus ? 2.5f : 1.5f);
		}
	}
}

void SCadenceArcGraphCanvas::PaintPorts(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// 端口行：条件文字与连线同色，右侧一个小圆点作为引出点（即路径的第一个点）。
	// 行号用 PortSlot：打开端口重排时显示顺序可能与资产里的 Transition 顺序不同。
	for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
	{
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		const FLinearColor Color = Edge.IsBrokenTarget() ? BrokenColor : ColorForEdge(EdgeIndex);
		const bool bPreparatoryEdge = DebugView.PreparatoryEdgeProgress.IsValidIndex(EdgeIndex) &&
			DebugView.PreparatoryEdgeProgress[EdgeIndex] >= 0.f;
		FLinearColor DisplayColor = EdgeIndex == DebugView.CandidateEdgeIndex
			? Paint.Palette.CandidateEdge : (bPreparatoryEdge ? Paint.Palette.PreparatoryEdge : Color);
		switch (GetEdgeEmphasis(EdgeIndex)) // 文字比连线淡得少一些，仍然要能读
		{
		case EEdgeEmphasis::OnPath: DisplayColor.A = OnPathLabelAlpha; break;
		case EEdgeEmphasis::OffPath: DisplayColor.A = OffPathLabelAlpha; break;
		default: break;
		}
		const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
		const FVector2D RowTopLeft = Source.Position
			+ FVector2D(10.f, Params.HeaderHeight + Edge.PortSlot * Params.PortHeight + 1.f);
		DrawLabel(Paint.Out, Paint.TextLayer, Paint.Geometry, RowTopLeft,
		          FVector2D(Source.Size.X - 24.f, Params.PortHeight), EdgeLabels[EdgeIndex], Paint.PortFont, DisplayColor);

		if (!Edge.Path.IsEmpty())
		{
			FSlateDrawElement::MakeBox(
				Paint.Out, Paint.HeaderLayer,
				Paint.Geometry.ToPaintGeometry(FVector2D(6.f, 6.f), FSlateLayoutTransform(Edge.Path[0] - FVector2D(3.f, 3.f))),
				Paint.WhiteBrush, ESlateDrawEffect::None, DisplayColor);
		}
	}
}

int32 SCadenceArcGraphCanvas::PaintHoverTooltip(const FPaintContext& Paint) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	// 悬停在边上：在鼠标旁边写出这条边从哪到哪、什么条件
	if (!Layout.Edges.IsValidIndex(HoveredEdge))
	{
		return Paint.TextLayer;
	}
	const FCadenceArcLayoutEdge& Edge = Layout.Edges[HoveredEdge];
	const FString Text = FString::Printf(TEXT("%s → %s · %s%s"),
	                                     *ShortTagName(Layout.Nodes[Edge.SourceNodeIndex].ActionTag),
	                                     Edge.IsBrokenTarget() ? TEXT("?") : *ShortTagName(Layout.Nodes[Edge.TargetNodeIndex].ActionTag),
	                                     *EdgeLabels[HoveredEdge],
	                                     Edge.bIsReference ? TEXT(" · click to jump") : TEXT(""));
	const FVector2D TextSize = MeasureText(Text, Paint.PortFont);
	const FVector2D BoxSize(TextSize.X + 14.0, FMath::Max(20.0, TextSize.Y + 6.0));
	const FVector2D BoxTopLeft = HoverPoint + FVector2D(14.0, 12.0);
	FSlateDrawElement::MakeBox(
		Paint.Out, Paint.TextLayer + 1, Paint.Geometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(BoxTopLeft)),
		Paint.WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.05f, 0.05f, 0.07f, 0.92f));
	DrawLabel(Paint.Out, Paint.TextLayer + 2, Paint.Geometry, BoxTopLeft + FVector2D(7.0, (BoxSize.Y - TextSize.Y) * 0.5),
	          TextSize + FVector2D(2.0, 0.0), Text, Paint.PortFont, FLinearColor::White);
	return Paint.TextLayer + 2;
}
