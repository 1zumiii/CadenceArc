#include "SCadenceArcDebuggerPanel.h"

#include "SCadenceArcChargeTimeline.h"
#include "SCadenceArcGraphCanvas.h"
#include "SlateOptMacros.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Styling/CoreStyle.h"
#include "ViewModel/CadenceArcDebuggerSelection.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
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
	UpdateOffscreenHints();
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
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
	// 整组 = 已提交节点 + 它的直接后继（下一步能去的地方）；自环和坏目标不扩大范围
	FBox2D Group = Primary.GetValue();
	for (const FCadenceArcLayoutEdge& Edge : Canvas->GetLayout().Edges)
	{
		if (Edge.SourceNodeIndex == NodeIndex && !Edge.IsBrokenTarget() && Edge.TargetNodeIndex != NodeIndex)
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
			if (Edge.SourceNodeIndex != NodeIndex || Edge.IsBrokenTarget() || Edge.TargetNodeIndex == NodeIndex)
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
			FString::Printf(TEXT("%s @ %s"), *OwnerName, *World->GetName());

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
