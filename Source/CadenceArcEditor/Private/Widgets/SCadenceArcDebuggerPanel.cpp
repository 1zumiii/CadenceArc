#include "SCadenceArcDebuggerPanel.h"

#include "SCadenceArcChargeTimeline.h"
#include "SCadenceArcGraphCanvas.h"
#include "SlateOptMacros.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Misc/ConfigCacheIni.h"
#include "Resolver/CadenceArcResolver.h"
#include "Styling/CoreStyle.h"
#include "ViewModel/CadenceArcDebuggerSelection.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* ConfigSection = TEXT("CadenceArc.Debugger");

	const TCHAR* StateName(const ECadenceArcResolverState State)
	{
		switch (State)
		{
		case ECadenceArcResolverState::Ready: return TEXT("Ready");
		case ECadenceArcResolverState::AwaitingStart: return TEXT("AwaitingStart");
		case ECadenceArcResolverState::Executing: return TEXT("Executing");
		default: return TEXT("Uninitialized");
		}
	}

	const TCHAR* StageName(const ECadenceArcHoldStage Stage)
	{
		switch (Stage)
		{
		case ECadenceArcHoldStage::Holding: return TEXT("Holding");
		case ECadenceArcHoldStage::Charging: return TEXT("Charging");
		case ECadenceArcHoldStage::Charged: return TEXT("Charged");
		default: return TEXT("None");
		}
	}

	FString TagNameOrNone(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return TEXT("None");
		}
		const FString Name = Tag.ToString();
		int32 DotIndex = INDEX_NONE;
		return Name.FindLastChar(TEXT('.'), DotIndex) ? Name.RightChop(DotIndex + 1) : Name;
	}

	// Request 不携带输入 Phase；只有当前图中所有同源、同目标、同 Tag 的边都是 Pressed 时才认定它是新按下。
	bool IsDefinitelyPressedRequest(
		const FCadenceArcGraphLayout& Layout, const FCadenceArcActionRequest& Request)
	{
		bool bFound = false;
		for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
		{
			if (!Layout.Nodes.IsValidIndex(Edge.SourceNodeIndex) ||
				!Layout.Nodes.IsValidIndex(Edge.TargetNodeIndex) ||
				Layout.Nodes[Edge.SourceNodeIndex].ActionTag != Request.SourceActionTag ||
				Layout.Nodes[Edge.TargetNodeIndex].ActionTag != Request.TargetActionTag ||
				Edge.Transition.InputTag != Request.InputTag)
			{
				continue;
			}
			if (Edge.Transition.InputPhase != ECadenceArcInputPhase::Pressed)
			{
				return false;
			}
			bFound = true;
		}
		return bFound;
	}
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SCadenceArcDebuggerPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(30.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.Padding(0.0f, 0.0f, 10.0f, 0.0f)
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(500.f)
					[
						SAssignNew(ResolverCombo, SComboBox<TSharedPtr<FResolverOption>>)
						.OptionsSource(&Options)
						.OnGenerateWidget(this, &SCadenceArcDebuggerPanel::MakeOptionWidget)
						.OnSelectionChanged(this, &SCadenceArcDebuggerPanel::OnResolverSelected)
						[
							SNew(STextBlock)
							.Text(this, &SCadenceArcDebuggerPanel::GetSelectedLabel)
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(100.f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center) // 按钮文字水平居中
						.VAlign(VAlign_Center) // 按钮文字垂直居中
						.OnClicked(this, &SCadenceArcDebuggerPanel::OnRefreshClicked)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Refresh"))
							.ColorAndOpacity(FSlateColor(FLinearColor::White))
						]
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.f, 0.f, 0.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bFollowCommittedNode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						bFollowCommittedNode = NewState == ECheckBoxState::Checked;
						if (bFollowCommittedNode)
						{
							RequestFollow(); // 重新打开时立即把当前节点滚进视口
						}
						else
						{
							Canvas->SetZoom(1.f); // 关闭跟随后回到原始比例，方便手动阅读
						}
					})
					.ToolTipText(FText::FromString(TEXT("When the committed node changes, zoom out if needed and scroll so it and its next steps are visible.")))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Follow")))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(16.f, 0.f, 0.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bUseReferences ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						bUseReferences = NewState == ECheckBoxState::Checked;
						ApplyReferenceSetting(true);
					})
					.ToolTipText(FText::FromString(TEXT("Draw edges that span at least this many columns as a short reference next to the source instead of a long line. Click a reference to jump to its target; click the stub at the target to jump back.")))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("References, span ≥")))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.f, 0.f, 0.f, 0.f)
				[
					SNew(SBox)
					.WidthOverride(48.f)
					[
						SNew(SSpinBox<int32>)
						.MinValue(2)
						.MaxValue(9)
						.Delta(1)
						.IsEnabled_Lambda([this]() { return bUseReferences; })
						.Value_Lambda([this]() { return ReferenceMinSpan; })
						.OnValueChanged_Lambda([this](const int32 NewValue)
						{
							ReferenceMinSpan = NewValue;
							ApplyReferenceSetting(false); // 拖动中实时预览，松手再写配置
						})
						.OnValueCommitted_Lambda([this](const int32 NewValue, ETextCommit::Type)
						{
							ReferenceMinSpan = NewValue;
							ApplyReferenceSetting(true);
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(16.f, 0.f, 0.f, 0.f)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]()
					{
						return bCompactChains ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						bCompactChains = NewState == ECheckBoxState::Checked;
						ApplyLayoutSetting(true);
					})
					.ToolTipText(FText::FromString(TEXT("Arrange simple chains vertically in one column to reduce graph width. Branches and merges remain connected to the same nodes. Has no effect when no simple chains can be compacted.")))
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Compact chains")))
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
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
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(340.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f, 4.f, 4.f, 12.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Runtime")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SNew(STextBlock).Text(this, &SCadenceArcDebuggerPanel::GetStateText)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SNew(STextBlock)
							.Text(this, &SCadenceArcDebuggerPanel::GetRequestText)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SNew(STextBlock).Text(this, &SCadenceArcDebuggerPanel::GetWindowText)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SNew(STextBlock)
							.Text(this, &SCadenceArcDebuggerPanel::GetBufferedInputText)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f, 16.f, 4.f, 8.f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Hold")))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SNew(STextBlock)
							.Text(this, &SCadenceArcDebuggerPanel::GetHoldText)
							.AutoWrapText(true)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(4.f)
						[
							SAssignNew(ChargeTimeline, SCadenceArcChargeTimeline)
							.Visibility(this, &SCadenceArcDebuggerPanel::GetChargeVisibility)
						]
					]
				]
			]
		]
	];
	Canvas->SetGraph(nullptr); // 初始化 Canvas 的 Graph 为 nullptr
	// 画布识别手势，滚动区在这里：右键或中键拖动平移，Ctrl + 滚轮缩放
	Canvas->SetInteractionHandlers(
		[this](const FVector2D& ScreenDelta) { PanBy(ScreenDelta); },
		[this](const float WheelDelta, const FVector2D& CanvasLocal) { ZoomAt(WheelDelta, CanvasLocal); });
	Canvas->SetNavigateHandler([this](const int32 NodeIndex) { ScrollNodeIntoView(NodeIndex); });
	GConfig->GetBool(ConfigSection, TEXT("bUseReferences"), bUseReferences, GEditorPerProjectIni);
	GConfig->GetInt(ConfigSection, TEXT("ReferenceMinSpan"), ReferenceMinSpan, GEditorPerProjectIni);
	GConfig->GetBool(ConfigSection, TEXT("bCompactChains"), bCompactChains, GEditorPerProjectIni);
	ReferenceMinSpan = FMath::Clamp(ReferenceMinSpan, 2, 9);
	ApplyReferenceSetting(false);
	ApplyLayoutSetting(false);

	EndPIEHandle = FEditorDelegates::EndPIE.AddSP(
		this, &SCadenceArcDebuggerPanel::OnEndPIE
	);
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

SCadenceArcDebuggerPanel::~SCadenceArcDebuggerPanel()
{
	StopRefreshTimer();
	CadenceArc::Editor::DebuggerSelection::Clear();
	if (EndPIEHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
	}
}

void SCadenceArcDebuggerPanel::StopRefreshTimer()
{
	if (RefreshTimerHandle.IsValid())
	{
		UnRegisterActiveTimer(RefreshTimerHandle.ToSharedRef());
		RefreshTimerHandle.Reset();
	}
}

void SCadenceArcDebuggerPanel::RefreshSelectedResolver()
{
	const UCadenceArcResolver* Resolver = SelectedResolver.Get();
	Canvas->SetGraph(Resolver ? Resolver->GetGraph() : nullptr);
	if (!Resolver)
	{
		LatestView = FCadenceArcDebugView{};
		DisplayedHoldSnapshot = FCadenceArcHoldSnapshot{};
		LastObservedActionTag = FGameplayTag{};
	}
	else
	{
		const FCadenceArcDebugView PreviousView = LatestView;
		LatestView = BuildDebugView(*Resolver, Canvas->GetLayout());
		const FGameplayTag CurrentActionTag = Resolver->GetCurrentActionTag();
		const FGameplayTag EntryTag = Resolver->GetGraph()->EntryActionTag;
		const bool bReturnedToEntry = LastObservedActionTag.IsValid() &&
			LastObservedActionTag != EntryTag && CurrentActionTag == EntryTag &&
			LatestView.ResolverState == ECadenceArcResolverState::Ready;
		const bool bHoldJustEnded = PreviousView.HoldSnapshot.bHasHold && !LatestView.HoldSnapshot.bHasHold;
		const bool bNewRequest = LatestView.OutstandingRequest.RequestId != 0 &&
			LatestView.OutstandingRequest.RequestId != PreviousView.OutstandingRequest.RequestId;
		// 松手产生的请求应保留刚结束的按住状态；能确定是 Pressed 的新请求则清掉旧显示。
		const bool bNewPressedRequest = bNewRequest && (!bHoldJustEnded ||
			IsDefinitelyPressedRequest(Canvas->GetLayout(), LatestView.OutstandingRequest));
		const bool bNewBufferedInput = LatestView.BufferedInputTag.IsValid() &&
			LatestView.BufferedInputTag != PreviousView.BufferedInputTag &&
			(!bHoldJustEnded || LatestView.BufferedInputTag != PreviousView.HoldSnapshot.InputTag);
		const bool bNewHold = LatestView.HoldSnapshot.bHasHold &&
			(!PreviousView.HoldSnapshot.bHasHold ||
			!(LatestView.HoldSnapshot.Token == PreviousView.HoldSnapshot.Token));
		if (bReturnedToEntry || bNewPressedRequest || bNewBufferedInput || bNewHold)
		{
			DisplayedHoldSnapshot = FCadenceArcHoldSnapshot{};
		}
		if (LatestView.HoldSnapshot.bHasHold)
		{
			DisplayedHoldSnapshot = LatestView.HoldSnapshot;
		}
		LastObservedActionTag = CurrentActionTag;
	}
	Canvas->SetDebugView(LatestView);
	ChargeTimeline->SetSnapshot(DisplayedHoldSnapshot);
	FollowCommittedNode();
	ApplyHistoryFocus();
	UpdateOffscreenHints();
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

void SCadenceArcDebuggerPanel::ApplyReferenceSetting(const bool bSave)
{
	Canvas->SetReferenceMinSpan(bUseReferences ? ReferenceMinSpan : 0);
	RequestFollow(); // 布局变了，按新位置重新定位当前节点
	HintSignature.Reset();
	if (bSave)
	{
		GConfig->SetBool(ConfigSection, TEXT("bUseReferences"), bUseReferences, GEditorPerProjectIni);
		GConfig->SetInt(ConfigSection, TEXT("ReferenceMinSpan"), ReferenceMinSpan, GEditorPerProjectIni);
	}
}

void SCadenceArcDebuggerPanel::ApplyLayoutSetting(const bool bSave)
{
	Canvas->SetLayoutMode(bCompactChains ? ECadenceArcLayoutMode::CompactChains : ECadenceArcLayoutMode::Layered);
	RequestFollow();
	HintSignature.Reset();
	if (bSave)
	{
		GConfig->SetBool(ConfigSection, TEXT("bCompactChains"), bCompactChains, GEditorPerProjectIni);
	}
}

void SCadenceArcDebuggerPanel::RequestFollow()
{
	LastFollowedNodeIndex = INDEX_NONE;
}

void SCadenceArcDebuggerPanel::FollowCommittedNode()
{
	const int32 NodeIndex = LatestView.CommittedNodeIndex;
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
	const double VisibleWidth = HorizontalScroll->GetCachedGeometry().GetLocalSize().X;
	const double VisibleHeight = VerticalScroll->GetCachedGeometry().GetLocalSize().Y;
	const double Zoom = ComputeFollowZoom(Canvas->GetZoom(), VisibleWidth, VisibleHeight,
	                                      Group.GetSize().X, Group.GetSize().Y, Margin, MinZoom);
	Canvas->SetZoom(static_cast<float>(Zoom));

	// 滚动规则见 ComputeFollowOffset：整组放得下就最小移动显示整组，放不下时保证已提交节点可见
	const auto ScrollAxis = [Margin](SScrollBox& Scroll, const double Visible,
	                                 const double PrimaryMin, const double PrimaryMax,
	                                 const double GroupMin, const double GroupMax)
	{
		const double Offset = Scroll.GetScrollOffset();
		const double NewOffset = ComputeFollowOffset(
			Offset, Visible, PrimaryMin, PrimaryMax, GroupMin, GroupMax, Margin, Scroll.GetScrollOffsetOfEnd());
		if (NewOffset != Offset)
		{
			Scroll.SetScrollOffset(static_cast<float>(NewOffset));
		}
	};
	ScrollAxis(*HorizontalScroll, VisibleWidth,
	           Primary->Min.X * Zoom, Primary->Max.X * Zoom, Group.Min.X * Zoom, Group.Max.X * Zoom);
	ScrollAxis(*VerticalScroll, VisibleHeight,
	           Primary->Min.Y * Zoom, Primary->Max.Y * Zoom, Group.Min.Y * Zoom, Group.Max.Y * Zoom);
}

void SCadenceArcDebuggerPanel::PanBy(const FVector2D& ScreenDelta)
{
	// 手动平移或缩放之后不再自动跟随，免得下一次切换节点时视口被拽走；重新勾选 Follow 即可恢复
	bFollowCommittedNode = false;
	const FVector2D Visible(HorizontalScroll->GetCachedGeometry().GetLocalSize().X,
	                        VerticalScroll->GetCachedGeometry().GetLocalSize().Y);
	const FVector2D Content = Canvas->GetDesiredSize();
	HorizontalScroll->SetScrollOffset(static_cast<float>(FMath::Clamp(
		HorizontalScroll->GetScrollOffset() - ScreenDelta.X, 0.0, FMath::Max(0.0, Content.X - Visible.X))));
	VerticalScroll->SetScrollOffset(static_cast<float>(FMath::Clamp(
		VerticalScroll->GetScrollOffset() - ScreenDelta.Y, 0.0, FMath::Max(0.0, Content.Y - Visible.Y))));
}

void SCadenceArcDebuggerPanel::ZoomAt(const float WheelDelta, const FVector2D& CanvasLocal)
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
	const FVector2D Visible(HorizontalScroll->GetCachedGeometry().GetLocalSize().X,
	                        VerticalScroll->GetCachedGeometry().GetLocalSize().Y);
	const FVector2D Content = Canvas->GetLayout().Size * NewZoom;
	HorizontalScroll->SetScrollOffset(static_cast<float>(
		FMath::Clamp(NewOffset.X, 0.0, FMath::Max(0.0, Content.X - Visible.X))));
	VerticalScroll->SetScrollOffset(static_cast<float>(
		FMath::Clamp(NewOffset.Y, 0.0, FMath::Max(0.0, Content.Y - Visible.Y))));
}

void SCadenceArcDebuggerPanel::ApplyHistoryFocus()
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

void SCadenceArcDebuggerPanel::ScrollNodeIntoView(const int32 NodeIndex)
{
	const TOptional<FBox2D> Bounds = Canvas->GetNodeBounds(NodeIndex);
	if (!Bounds.IsSet())
	{
		return;
	}
	const auto ScrollAxis = [](SScrollBox& Scroll, const double Visible, const double Min, const double Max)
	{
		constexpr double Margin = 40.0;
		Scroll.SetScrollOffset(static_cast<float>(ComputeFollowOffset(
			Scroll.GetScrollOffset(), Visible, Min, Max, Min, Max, Margin, Scroll.GetScrollOffsetOfEnd())));
	};
	ScrollAxis(*HorizontalScroll, HorizontalScroll->GetCachedGeometry().GetLocalSize().X, Bounds->Min.X, Bounds->Max.X);
	ScrollAxis(*VerticalScroll, VerticalScroll->GetCachedGeometry().GetLocalSize().Y, Bounds->Min.Y, Bounds->Max.Y);
}

void SCadenceArcDebuggerPanel::UpdateOffscreenHints()
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
	const int32 NodeIndex = LatestView.CommittedNodeIndex;
	const FVector2D Visible(HorizontalScroll->GetCachedGeometry().GetLocalSize().X,
	                        VerticalScroll->GetCachedGeometry().GetLocalSize().Y);
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

FText SCadenceArcDebuggerPanel::GetStateText() const
{
	return SelectedResolver.IsValid()
		? FText::FromString(FString::Printf(TEXT("State: %s"), StateName(LatestView.ResolverState)))
		: FText::FromString(TEXT("State: no PIE Resolver selected"));
}

FText SCadenceArcDebuggerPanel::GetRequestText() const
{
	const FCadenceArcActionRequest& Request = LatestView.OutstandingRequest;
	if (Request.RequestId == 0)
	{
		return FText::FromString(TEXT("Request: None"));
	}
	return FText::FromString(FString::Printf(
		TEXT("Request Id: %lld\nSource: %s\nTarget: %s\nInput: %s"),
		static_cast<long long>(Request.RequestId),
		*TagNameOrNone(Request.SourceActionTag),
		*TagNameOrNone(Request.TargetActionTag),
		*TagNameOrNone(Request.InputTag)));
}

FText SCadenceArcDebuggerPanel::GetWindowText() const
{
	return FText::FromString(LatestView.bBufferWindowOpen
		? TEXT("Buffer window: Open") : TEXT("Buffer window: Closed"));
}

FText SCadenceArcDebuggerPanel::GetBufferedInputText() const
{
	return FText::FromString(FString::Printf(
		TEXT("Buffered Tag: %s"), *TagNameOrNone(LatestView.BufferedInputTag)));
}

FText SCadenceArcDebuggerPanel::GetHoldText() const
{
	const FCadenceArcHoldSnapshot& Hold = DisplayedHoldSnapshot;
	if (!Hold.bHasHold)
	{
		return FText::FromString(TEXT("Stage: None\nPressId: None\nCharge config: No"));
	}
	return FText::FromString(FString::Printf(
		TEXT("%s\nStage: %s\nPressId: %lld\nCharge config: %s\nInput: %s"),
		LatestView.HoldSnapshot.bHasHold ? TEXT("Active") : TEXT("Last observed (no active hold)"),
		StageName(Hold.Stage), static_cast<long long>(Hold.Token.PressId),
		Hold.bHasChargeConfig ? TEXT("Yes") : TEXT("No"),
		*TagNameOrNone(Hold.InputTag)));
}

EVisibility SCadenceArcDebuggerPanel::GetChargeVisibility() const
{
	return DisplayedHoldSnapshot.bHasHold && DisplayedHoldSnapshot.bHasChargeConfig
		? EVisibility::Visible : EVisibility::Collapsed;
}

FReply SCadenceArcDebuggerPanel::OnRefreshClicked()
{
	UCadenceArcResolver* CurrentResolver = SelectedResolver.Get();
	Options.Reset();
	for (TObjectIterator<UCadenceArcResolver> It; It; ++It)
	{
		UCadenceArcResolver* Resolver = *It;
		if (!IsValid(Resolver) || !Resolver->IsInitialized())
			continue;

		const UWorld* World = Resolver->GetWorld();
		if (!IsValid(World) || !World->IsPlayInEditor())
			continue;

		const AActor* Actor = Resolver->GetTypedOuter<AActor>();
		const FString OwnerName = Actor ? Actor->GetName() : Resolver->GetName();
		const FString Label =
			// 多客户端 PIE 时各 World 的对象名都是地图名，加上 PIE 实例号才分得清
			FString::Printf(TEXT("%s @ %s [PIE %d]"), *OwnerName, *World->GetName(),
			                World->GetOutermost()->GetPIEInstanceID());

		Options.Add(MakeShared<FResolverOption>(
			FResolverOption{.Resolver = Resolver, .Label = Label}
		));
	}
	Options.Sort([](const TSharedPtr<FResolverOption>& A, const TSharedPtr<FResolverOption>& B)
	{
		return A->Label < B->Label;
	});
	ResolverCombo->RefreshOptions();
	// 若原选择仍在列表中就选中新建的那一项，否则清空选择。
	// SetSelectedItem / ClearSelection 都会触发 OnResolverSelected，由它同步 SelectedResolver 和画布。
	const TSharedPtr<FResolverOption>* Found = CurrentResolver
		? Options.FindByPredicate(
			[CurrentResolver](const TSharedPtr<FResolverOption>& Option)
			{
				return Option->Resolver == CurrentResolver;
			}
		)
		: nullptr;
	if (Found)
	{
		ResolverCombo->SetSelectedItem(*Found);
	}
	else
	{
		ResolverCombo->ClearSelection();
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SCadenceArcDebuggerPanel::MakeOptionWidget(TSharedPtr<FResolverOption> Shared)
{
	// 返回显示 Item->Label 的 STextBlock
	return SNew(STextBlock).Text(FText::FromString(Shared->Label));
}

FText SCadenceArcDebuggerPanel::GetSelectedLabel() const
{
	// 直接取下拉框当前选中项；Resolver 已被销毁时仍显示占位文字
	const TSharedPtr<FResolverOption> Selected =
		ResolverCombo.IsValid() ? ResolverCombo->GetSelectedItem() : nullptr;
	if (Selected.IsValid() && Selected->Resolver.IsValid())
	{
		return FText::FromString(Selected->Label);
	}
	return FText::FromString("Select PIE Resolver");
}

EActiveTimerReturnType SCadenceArcDebuggerPanel::OnRefreshTick(double X, float Arg)
{
	if (!SelectedResolver.IsValid() || !SelectedResolver->IsInitialized())
	{
		SelectedResolver.Reset();
		CadenceArc::Editor::DebuggerSelection::Clear();
		RefreshSelectedResolver();
		RefreshTimerHandle.Reset();
		return EActiveTimerReturnType::Stop;
	}
	RefreshSelectedResolver();
	return EActiveTimerReturnType::Continue;
}

void SCadenceArcDebuggerPanel::OnResolverSelected(TSharedPtr<FResolverOption> ResolverOption, ESelectInfo::Type Arg)
{
	StopRefreshTimer();
	const UCadenceArcResolver* PreviousResolver = SelectedResolver.Get();
	SelectedResolver = ResolverOption.IsValid() ? ResolverOption->Resolver : nullptr;
	// Arc History 跟随这里的选择
	CadenceArc::Editor::DebuggerSelection::Set(
		SelectedResolver.Get(), ResolverOption.IsValid() ? ResolverOption->Label : FString());
	if (SelectedResolver.Get() != PreviousResolver)
	{
		DisplayedHoldSnapshot = FCadenceArcHoldSnapshot{};
		LatestView = FCadenceArcDebugView{};
		LastObservedActionTag = FGameplayTag{};
		RequestFollow(); // 换了实例，即使节点下标相同也重新定位一次
	}
	RefreshSelectedResolver();
	if (SelectedResolver.IsValid())
	{
		RefreshTimerHandle = RegisterActiveTimer(
			0.f,
			FWidgetActiveTimerDelegate::CreateSP(
				this, &SCadenceArcDebuggerPanel::OnRefreshTick
			)
		);
	}
}

void SCadenceArcDebuggerPanel::OnEndPIE(bool bArg)
{
	StopRefreshTimer();
	CadenceArc::Editor::DebuggerSelection::Clear();
	Options.Reset();
	ResolverCombo->RefreshOptions();
	ResolverCombo->ClearSelection();
	SelectedResolver.Reset();
	RefreshSelectedResolver();
}
