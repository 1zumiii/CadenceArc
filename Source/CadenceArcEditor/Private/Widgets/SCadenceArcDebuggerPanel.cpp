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
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
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
					})
					.ToolTipText(FText::FromString(TEXT("Scroll the committed node into view whenever it changes.")))
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
		PendingFollowFrames = 2;
	}
	if (!bFollowCommittedNode || PendingFollowFrames <= 0)
	{
		return;
	}
	--PendingFollowFrames;
	const TOptional<FBox2D> Primary = Canvas->GetNodeBounds(NodeIndex);
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
			if (const TOptional<FBox2D> Successor = Canvas->GetNodeBounds(Edge.TargetNodeIndex))
			{
				Group += Successor.GetValue();
			}
		}
	}

	// 滚动规则见 ComputeFollowOffset：整组放得下就最小移动显示整组，放不下时保证已提交节点可见
	const auto ScrollAxis = [](SScrollBox& Scroll, const double Visible,
	                           const double PrimaryMin, const double PrimaryMax,
	                           const double GroupMin, const double GroupMax)
	{
		constexpr double Margin = 40.0;
		const double Offset = Scroll.GetScrollOffset();
		const double NewOffset = ComputeFollowOffset(
			Offset, Visible, PrimaryMin, PrimaryMax, GroupMin, GroupMax, Margin, Scroll.GetScrollOffsetOfEnd());
		if (NewOffset != Offset)
		{
			Scroll.SetScrollOffset(static_cast<float>(NewOffset));
		}
	};
	ScrollAxis(*HorizontalScroll, HorizontalScroll->GetCachedGeometry().GetLocalSize().X,
	           Primary->Min.X, Primary->Max.X, Group.Min.X, Group.Max.X);
	ScrollAxis(*VerticalScroll, VerticalScroll->GetCachedGeometry().GetLocalSize().Y,
	           Primary->Min.Y, Primary->Max.Y, Group.Min.Y, Group.Max.Y);
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
	Options.Reset();
	ResolverCombo->RefreshOptions();
	ResolverCombo->ClearSelection();
	SelectedResolver.Reset();
	RefreshSelectedResolver();
}
