// SCadenceArcGraphCanvas 的状态和交互：换图、布局选项、缩放、历史焦点、鼠标悬停、平移缩放手势和引用跳转。
// 绘制在 SCadenceArcGraphCanvasPaint.cpp。

#include "SCadenceArcGraphCanvas.h"

#include "CadenceArcCanvasDrawing.h"
#include "Graph/CadenceArcGraph.h"
#include "InputCoreTypes.h"
#include "Layout/CadenceArcLayoutHitTest.h"

void SCadenceArcGraphCanvas::SetDebugView(const FCadenceArcDebugView& InDebugView)
{
	DebugView = InDebugView;
	Invalidate(EInvalidateWidgetReason::Paint);
}

void SCadenceArcGraphCanvas::SetGraph(const UCadenceArcGraph* InGraph)
{
	const bool bGraphChanged = Graph.Get() != InGraph;
	DebugView = FCadenceArcDebugView();
	Graph = InGraph;
	RebuildGeometry(bGraphChanged);
}

void SCadenceArcGraphCanvas::RebuildGeometry(const bool bResetHover)
{
	const UCadenceArcGraph* CurrentGraph = Graph.Get();
	Layout = CurrentGraph ? BuildGraphLayout(*CurrentGraph, Params) : FCadenceArcGraphLayout{};
	// 换布局后丢弃旧位置的悬停；同一张图的定时刷新则保留，避免提示不断闪烁。
	if (bResetHover)
	{
		HoveredNode = INDEX_NONE;
		HoveredEdge = INDEX_NONE;
		HoverPoint = FVector2D::ZeroVector;
	}

	EdgeLabels.Reset(Layout.Edges.Num());
	for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
	{
		EdgeLabels.Add(CadenceArc::Editor::CanvasDrawing::FormatTransitionLabel(Edge.Transition));
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

void SCadenceArcGraphCanvas::SetInteractionHandlers(
	TFunction<void(const FVector2D& ScreenDelta)> InOnPan,
	TFunction<void(float WheelDelta, const FVector2D& CanvasLocalPosition)> InOnZoom)
{
	OnPan = MoveTemp(InOnPan);
	OnZoom = MoveTemp(InOnZoom);
}

void SCadenceArcGraphCanvas::SetHistoryFocus(const TArray<int32>& InNodes, const int32 InEdge)
{
	if (InNodes != FocusNodes || InEdge != FocusEdge)
	{
		FocusNodes = InNodes;
		FocusEdge = InEdge;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

void SCadenceArcGraphCanvas::SetLayoutParams(const FCadenceArcLayoutParams& InParams)
{
	Params = InParams;
	RebuildGeometry();
}

FReply SCadenceArcGraphCanvas::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// 右键或中键拖动平移
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton
		|| MouseEvent.GetEffectingButton() == EKeys::MiddleMouseButton)
	{
		bPanning = true;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	// 左键点引用边：点在目标一侧的接入线上回到源节点，点标记或端口短线去目标
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && OnNavigate)
	{
		const double Tolerance = 6.0 / FMath::Max(Zoom, 0.01f);
		const FVector2D Point = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()) / FMath::Max(Zoom, 0.01f);
		const int32 EdgeIndex = HitTestNode(Layout, Point) == INDEX_NONE ? HitTestEdge(Layout, Point, Tolerance) : INDEX_NONE;
		if (Layout.Edges.IsValidIndex(EdgeIndex) && Layout.Edges[EdgeIndex].bIsReference)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			const bool bAtEntry = Edge.EntryStub.Num() >= 2 && FMath::PointDistToSegmentSquared(
				FVector(Point, 0.0), FVector(Edge.EntryStub[0], 0.0), FVector(Edge.EntryStub.Last(), 0.0)) <= Tolerance * Tolerance;
			OnNavigate(bAtEntry ? Edge.SourceNodeIndex : Edge.TargetNodeIndex);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

FReply SCadenceArcGraphCanvas::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPanning)
	{
		bPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SCadenceArcGraphCanvas::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (bPanning && HasMouseCapture())
	{
		if (OnPan)
		{
			OnPan(MouseEvent.GetCursorDelta());
		}
		return FReply::Handled();
	}
	// 悬停：先看节点，再看离得最近的连线（容差按屏幕约 6 像素换算到布局坐标）
	HoverPoint = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()) / FMath::Max(Zoom, 0.01f);
	const int32 Node = HitTestNode(Layout, HoverPoint);
	const int32 Edge = Node == INDEX_NONE ? HitTestEdge(Layout, HoverPoint, 6.0 / FMath::Max(Zoom, 0.01f)) : INDEX_NONE;
	if (Node != HoveredNode || Edge != HoveredEdge || Edge != INDEX_NONE)
	{
		HoveredNode = Node;
		HoveredEdge = Edge;
		Invalidate(EInvalidateWidgetReason::Paint); // 悬停在边上时提示框跟着鼠标走
	}
	return FReply::Unhandled();
}

FReply SCadenceArcGraphCanvas::OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// Ctrl + 滚轮以鼠标为中心缩放；普通滚轮交给外面的滚动区
	if (MouseEvent.IsControlDown() && OnZoom)
	{
		OnZoom(MouseEvent.GetWheelDelta(), MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition()));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SCadenceArcGraphCanvas::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	if (HoveredNode != INDEX_NONE || HoveredEdge != INDEX_NONE)
	{
		HoveredNode = INDEX_NONE;
		HoveredEdge = INDEX_NONE;
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FCursorReply SCadenceArcGraphCanvas::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	if (bPanning)
	{
		return FCursorReply::Cursor(EMouseCursor::GrabHandClosed);
	}
	// 引用边可以点击跳转
	if (Layout.Edges.IsValidIndex(HoveredEdge) && Layout.Edges[HoveredEdge].bIsReference)
	{
		return FCursorReply::Cursor(EMouseCursor::Hand);
	}
	return FCursorReply::Unhandled();
}
