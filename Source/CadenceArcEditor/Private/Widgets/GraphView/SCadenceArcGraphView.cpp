#include "SCadenceArcGraphView.h"

#include "SCadenceArcGraphCanvas.h"
#include "SCadenceArcInputStrip.h"
#include "Layout/CadenceArcViewportMath.h"
#include "Styling/CoreStyle.h"
#include "ViewModel/CadenceArcDebuggerSelection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

void SCadenceArcGraphView::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SAssignNew(HorizontalScroll, SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot()
			[
				SAssignNew(VerticalScroll, SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(Canvas, SCadenceArcGraphCanvas)
				]
			]
		]
		// 视口外去向提示：自身不接收鼠标，只有提示按钮可点，不挡画布滚动
		+ SOverlay::Slot()
		[
			SAssignNew(HintCanvas, SCanvas)
			.Visibility(EVisibility::SelfHitTestInvisible)
		]
		// 输入显示固定在左下角，不随画布滚动，也不挡鼠标
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Bottom)
		.Padding(10.f, 0.f, 0.f, 22.f) // 底部留出横向滚动条
		[
			SAssignNew(InputStrip, SCadenceArcInputStrip)
			.Visibility(EVisibility::Collapsed)
		]
	];
	Canvas->SetGraph(nullptr);
	// 画布识别手势，滚动区在这里：右键或中键拖动平移，Ctrl + 滚轮缩放，左键点引用标签跳转
	Canvas->SetInteractionHandlers(
		[this](const FVector2D& ScreenDelta) { PanBy(ScreenDelta); },
		[this](const float WheelDelta, const FVector2D& CanvasLocal) { ZoomAt(WheelDelta, CanvasLocal); });
	Canvas->SetNavigateHandler([this](const int32 NodeIndex) { ScrollNodeIntoView(NodeIndex); });
}

void SCadenceArcGraphView::SetGraph(const UCadenceArcGraph* Graph)
{
	Canvas->SetGraph(Graph);
}

const FCadenceArcGraphLayout& SCadenceArcGraphView::GetLayout() const
{
	return Canvas->GetLayout();
}

void SCadenceArcGraphView::ShowDebugView(const FCadenceArcDebugView& View)
{
	CommittedNodeIndex = View.CommittedNodeIndex;
	Canvas->SetDebugView(View);
	FollowCommittedNode();
	ApplyHistoryFocus();
	UpdateOffscreenHints();
}

void SCadenceArcGraphView::ShowInputDisplay(const FCadenceArcInputDisplay& Display, const bool bVisible)
{
	const bool bShow = bVisible && Display.bValid;
	InputStrip->SetVisibility(bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed);
	if (bShow)
	{
		InputStrip->SetDisplay(Display);
	}
}

void SCadenceArcGraphView::SetLayoutParams(const FCadenceArcLayoutParams& Params)
{
	Canvas->SetLayoutParams(Params);
	RequestFollow(); // 布局变了，按新位置重新定位当前节点
	HintSignature.Reset();
}

const FCadenceArcLayoutParams& SCadenceArcGraphView::GetLayoutParams() const
{
	return Canvas->GetLayoutParams();
}

void SCadenceArcGraphView::SetFollowing(const bool bFollow)
{
	bFollowCommittedNode = bFollow;
	if (bFollowCommittedNode)
	{
		RequestFollow(); // 重新打开时立即把当前节点滚进视口
	}
	else
	{
		Canvas->SetZoom(1.f); // 关闭跟随后回到原始比例，方便手动阅读
	}
}

void SCadenceArcGraphView::RequestFollow()
{
	LastFollowedNodeIndex = INDEX_NONE;
}

FVector2D SCadenceArcGraphView::GetVisibleSize() const
{
	return FVector2D(HorizontalScroll->GetCachedGeometry().GetLocalSize().X,
	                 VerticalScroll->GetCachedGeometry().GetLocalSize().Y);
}

void SCadenceArcGraphView::FollowCommittedNode()
{
	const int32 NodeIndex = CommittedNodeIndex;
	if (NodeIndex != LastFollowedNodeIndex)
	{
		LastFollowedNodeIndex = NodeIndex;
		PendingFollowFrames = 3; // 第一帧定缩放，之后滚动区按新尺寸排布，再滚动到位
	}
	if (!bFollowCommittedNode || PendingFollowFrames <= 0)
	{
		return;
	}
	--PendingFollowFrames;
	const TOptional<FBox2D> Primary = Canvas->GetNodeLayoutBounds(NodeIndex);
	if (!Primary.IsSet())
	{
		return;
	}
	// 整组 = 已提交节点 + 它的直接后继（下一步能去的地方）；自环和坏目标不扩大范围。
	// 引用边的目标名已经写在源节点旁的标记上，只需让标记可见，不必为远处的目标缩小视图
	FBox2D Group = Primary.GetValue();
	for (const FCadenceArcLayoutEdge& Edge : Canvas->GetLayout().Edges)
	{
		if (Edge.SourceNodeIndex == NodeIndex && Edge.bIsReference)
		{
			Group += Edge.ReferenceBox;
		}
		else if (Edge.SourceNodeIndex == NodeIndex && !Edge.IsBrokenTarget() && Edge.TargetNodeIndex != NodeIndex)
		{
			if (const TOptional<FBox2D> Successor = Canvas->GetNodeLayoutBounds(Edge.TargetNodeIndex))
			{
				Group += Successor.GetValue();
			}
		}
	}

	// 1 倍放不下整组时缩小（不低于可读下限），再按缩放后的坐标滚动
	constexpr double Margin = 40.0;
	constexpr double MinZoom = 0.6;
	const FVector2D Visible = GetVisibleSize();
	const double Zoom = ComputeFollowZoom(Canvas->GetZoom(), Visible.X, Visible.Y,
	                                      Group.GetSize().X, Group.GetSize().Y, Margin, MinZoom);
	Canvas->SetZoom(static_cast<float>(Zoom));

	// 滚动规则见 ComputeFollowOffset：整组放得下就最小移动显示整组，放不下时保证已提交节点可见
	const auto ScrollAxis = [Margin](SScrollBox& Scroll, const double VisibleLength,
	                                 const double PrimaryMin, const double PrimaryMax,
	                                 const double GroupMin, const double GroupMax)
	{
		const double Offset = Scroll.GetScrollOffset();
		const double NewOffset = ComputeFollowOffset(
			Offset, VisibleLength, PrimaryMin, PrimaryMax, GroupMin, GroupMax, Margin, Scroll.GetScrollOffsetOfEnd());
		if (NewOffset != Offset)
		{
			Scroll.SetScrollOffset(static_cast<float>(NewOffset));
		}
	};
	ScrollAxis(*HorizontalScroll, Visible.X,
	           Primary->Min.X * Zoom, Primary->Max.X * Zoom, Group.Min.X * Zoom, Group.Max.X * Zoom);
	ScrollAxis(*VerticalScroll, Visible.Y,
	           Primary->Min.Y * Zoom, Primary->Max.Y * Zoom, Group.Min.Y * Zoom, Group.Max.Y * Zoom);
}

void SCadenceArcGraphView::PanBy(const FVector2D& ScreenDelta)
{
	// 手动平移或缩放之后不再自动跟随，免得下一次切换节点时视口被拽走；重新勾选 Follow 即可恢复
	bFollowCommittedNode = false;
	const FVector2D Visible = GetVisibleSize();
	const FVector2D Content = Canvas->GetDesiredSize();
	HorizontalScroll->SetScrollOffset(static_cast<float>(FMath::Clamp(
		HorizontalScroll->GetScrollOffset() - ScreenDelta.X, 0.0, FMath::Max(0.0, Content.X - Visible.X))));
	VerticalScroll->SetScrollOffset(static_cast<float>(FMath::Clamp(
		VerticalScroll->GetScrollOffset() - ScreenDelta.Y, 0.0, FMath::Max(0.0, Content.Y - Visible.Y))));
}

void SCadenceArcGraphView::ZoomAt(const float WheelDelta, const FVector2D& CanvasLocal)
{
	bFollowCommittedNode = false;
	const float OldZoom = Canvas->GetZoom();
	const float NewZoom = FMath::Clamp(OldZoom * FMath::Pow(1.1f, WheelDelta), 0.3f, 2.0f);
	if (NewZoom == OldZoom)
	{
		return;
	}
	Canvas->SetZoom(NewZoom);
	// 保持鼠标下的那一点不动：它在视口里的位置不变，画布坐标按比例放大
	const FVector2D Offset(HorizontalScroll->GetScrollOffset(), VerticalScroll->GetScrollOffset());
	const FVector2D InView = CanvasLocal - Offset;
	const FVector2D NewOffset = CanvasLocal * (NewZoom / OldZoom) - InView;
	const FVector2D Visible = GetVisibleSize();
	const FVector2D Content = Canvas->GetLayout().Size * NewZoom;
	HorizontalScroll->SetScrollOffset(static_cast<float>(
		FMath::Clamp(NewOffset.X, 0.0, FMath::Max(0.0, Content.X - Visible.X))));
	VerticalScroll->SetScrollOffset(static_cast<float>(
		FMath::Clamp(NewOffset.Y, 0.0, FMath::Max(0.0, Content.Y - Visible.Y))));
}

void SCadenceArcGraphView::ApplyHistoryFocus()
{
	// Arc History 点中的记录：按 Tag 在当前布局里找到节点和边（边要源、目标、输入都对上且唯一，否则只高亮节点）
	const FCadenceArcHistoryFocus& Focus = CadenceArc::Editor::DebuggerSelection::GetHistoryFocus();
	const FCadenceArcGraphLayout& Layout = Canvas->GetLayout();
	const auto FindNode = [&Layout](const FGameplayTag& ActionTag) -> int32
	{
		if (!ActionTag.IsValid())
		{
			return INDEX_NONE;
		}
		return Layout.Nodes.IndexOfByPredicate(
			[&ActionTag](const FCadenceArcLayoutNode& Node) { return Node.ActionTag == ActionTag; });
	};
	TArray<int32> Nodes;
	int32 Edge = INDEX_NONE;
	if (Focus.Sequence != 0)
	{
		const int32 Source = FindNode(Focus.SourceNode);
		const int32 Target = FindNode(Focus.TargetNode);
		for (const int32 Node : {Source, Target})
		{
			if (Node != INDEX_NONE)
			{
				Nodes.AddUnique(Node);
			}
		}
		if (Source != INDEX_NONE && Target != INDEX_NONE)
		{
			int32 Matches = 0;
			for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
			{
				const FCadenceArcLayoutEdge& Candidate = Layout.Edges[EdgeIndex];
				if (Candidate.SourceNodeIndex == Source && Candidate.TargetNodeIndex == Target
					&& Candidate.Transition.InputTag == Focus.InputTag)
				{
					Edge = EdgeIndex;
					++Matches;
				}
			}
			if (Matches != 1)
			{
				Edge = INDEX_NONE; // 没有或不止一条：不猜是哪条边
			}
		}
	}
	Canvas->SetHistoryFocus(Nodes, Edge);

	// 新点中一条记录时把它滚进视口（只滚一次，之后不干预手动浏览）
	if (Focus.Sequence != AppliedFocusSequence)
	{
		AppliedFocusSequence = Focus.Sequence;
		if (!Nodes.IsEmpty())
		{
			ScrollNodeIntoView(Nodes.Last());
		}
	}
}

void SCadenceArcGraphView::ScrollNodeIntoView(const int32 NodeIndex)
{
	const TOptional<FBox2D> Bounds = Canvas->GetNodeBounds(NodeIndex);
	if (!Bounds.IsSet())
	{
		return;
	}
	const auto ScrollAxis = [](SScrollBox& Scroll, const double VisibleLength, const double Min, const double Max)
	{
		constexpr double Margin = 40.0;
		Scroll.SetScrollOffset(static_cast<float>(ComputeFollowOffset(
			Scroll.GetScrollOffset(), VisibleLength, Min, Max, Min, Max, Margin, Scroll.GetScrollOffsetOfEnd())));
	};
	const FVector2D Visible = GetVisibleSize();
	ScrollAxis(*HorizontalScroll, Visible.X, Bounds->Min.X, Bounds->Max.X);
	ScrollAxis(*VerticalScroll, Visible.Y, Bounds->Min.Y, Bounds->Max.Y);
}

void SCadenceArcGraphView::UpdateOffscreenHints()
{
	// 当前节点的直接后继如果完全在视口外，就在视口边缘朝它的方向放一个可点击的提示
	struct FHint
	{
		int32 TargetNode = INDEX_NONE;
		FString Label;
		FVector2D Position = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
	};
	TArray<FHint> Hints;
	const int32 NodeIndex = CommittedNodeIndex;
	const FVector2D Visible = GetVisibleSize();
	if (NodeIndex != INDEX_NONE && Visible.X > 0.0 && Visible.Y > 0.0)
	{
		const FVector2D Offset(HorizontalScroll->GetScrollOffset(), VerticalScroll->GetScrollOffset());
		const FBox2D Viewport(Offset, Offset + Visible);
		const FCadenceArcGraphLayout& Layout = Canvas->GetLayout();
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			// 引用边在源节点旁已经有可点击的标记，不再重复提示
			if (Edge.SourceNodeIndex != NodeIndex || Edge.IsBrokenTarget() || Edge.TargetNodeIndex == NodeIndex
				|| Edge.bIsReference)
			{
				continue;
			}
			const TOptional<FBox2D> Target = Canvas->GetNodeBounds(Edge.TargetNodeIndex);
			if (!Target.IsSet())
			{
				continue;
			}
			const TOptional<FCadenceArcOffscreenHint> Hint = ComputeOffscreenHint(Viewport, Target.GetValue(), 16.0);
			if (!Hint.IsSet())
			{
				continue;
			}
			// 八个方向的箭头，按方向角取最近的一个（屏幕坐标 y 向下）
			static const TCHAR* Arrows[8] = {
				TEXT("→"), TEXT("↘"), TEXT("↓"), TEXT("↙"),
				TEXT("←"), TEXT("↖"), TEXT("↑"), TEXT("↗")
			};
			const double Angle = FMath::Atan2(Hint->Direction.Y, Hint->Direction.X);
			const int32 Octant = (FMath::RoundToInt(Angle / (UE_DOUBLE_PI / 4.0)) + 8) % 8;
			FString Name = Layout.Nodes[Edge.TargetNodeIndex].ActionTag.ToString();
			int32 Dot = INDEX_NONE;
			if (Name.FindLastChar(TEXT('.'), Dot))
			{
				Name.RightChopInline(Dot + 1);
			}
			FHint Entry;
			Entry.TargetNode = Edge.TargetNodeIndex;
			Entry.Label = FString::Printf(TEXT("%s %s · %s"), Arrows[Octant], *Name, *Canvas->GetEdgeLabel(EdgeIndex));
			Entry.Size = FVector2D(Entry.Label.Len() * 7.0 + 20.0, 22.0);
			// 提示框整体留在视口内，锚点尽量落在框的中心
			const FVector2D Anchor = Hint->Anchor - Offset;
			Entry.Position = FVector2D(
				FMath::Clamp(Anchor.X - Entry.Size.X * 0.5, 4.0, FMath::Max(4.0, Visible.X - Entry.Size.X - 4.0)),
				FMath::Clamp(Anchor.Y - Entry.Size.Y * 0.5, 4.0, FMath::Max(4.0, Visible.Y - Entry.Size.Y - 4.0)));
			// 与已放下的提示重叠时往下错开
			for (const FHint& Placed : Hints)
			{
				const FBox2D PlacedBox(Placed.Position, Placed.Position + Placed.Size);
				if (PlacedBox.Intersect(FBox2D(Entry.Position, Entry.Position + Entry.Size)))
				{
					Entry.Position.Y = Placed.Position.Y + Placed.Size.Y + 2.0;
				}
			}
			Hints.Add(Entry);
		}
	}

	// 提示没变就不重建控件，避免每帧重建按钮
	FString Signature;
	for (const FHint& Hint : Hints)
	{
		Signature += FString::Printf(TEXT("%d|%s|%.0f|%.0f;"), Hint.TargetNode, *Hint.Label, Hint.Position.X, Hint.Position.Y);
	}
	if (Signature == HintSignature)
	{
		return;
	}
	HintSignature = Signature;
	HintCanvas->ClearChildren();
	for (const FHint& Hint : Hints)
	{
		const int32 TargetNode = Hint.TargetNode;
		HintCanvas->AddSlot()
		.Position(Hint.Position)
		.Size(Hint.Size)
		[
			SNew(SButton)
			.ToolTipText(FText::FromString(TEXT("This next step is outside the view. Click to scroll to it.")))
			.OnClicked_Lambda([this, TargetNode]()
			{
				ScrollNodeIntoView(TargetNode);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(Hint.Label))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			]
		];
	}
}
