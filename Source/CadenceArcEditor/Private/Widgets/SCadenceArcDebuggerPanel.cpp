#include "SCadenceArcDebuggerPanel.h"

#include "SCadenceArcGraphView.h"
#include "SCadenceArcRuntimeDetails.h"
#include "SlateOptMacros.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "ViewModel/CadenceArcDebuggerSelection.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
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
						return GraphView->IsFollowing() ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					})
					.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
					{
						GraphView->SetFollowing(NewState == ECheckBoxState::Checked);
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
					SNew(SComboButton)
					.OnGetMenuContent(this, &SCadenceArcDebuggerPanel::MakeLayoutMenu)
					.ToolTipText(FText::FromString(TEXT("How the graph is laid out and drawn. Only affects this view, never the asset.")))
					.ButtonContent()
					[
						SNew(STextBlock).Text(FText::FromString(TEXT("Layout")))
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
				SAssignNew(GraphView, SCadenceArcGraphView)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(8.f, 0.f, 0.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(340.f)
				[
					SAssignNew(RuntimeDetails, SCadenceArcRuntimeDetails)
				]
			]
		]
	];
	Settings = FCadenceArcDebuggerSettings::Load();
	ApplySettings(false);

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
	GraphView->SetGraph(Resolver ? Resolver->GetGraph() : nullptr);
	if (!Resolver)
	{
		LatestView = FCadenceArcDebugView{};
		DisplayedHoldSnapshot = FCadenceArcHoldSnapshot{};
		LastObservedActionTag = FGameplayTag{};
	}
	else
	{
		const FCadenceArcDebugView PreviousView = LatestView;
		LatestView = BuildDebugView(*Resolver, GraphView->GetLayout());
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
			IsDefinitelyPressedRequest(GraphView->GetLayout(), LatestView.OutstandingRequest));
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
	GraphView->ShowDebugView(LatestView);
	RuntimeDetails->Update(Resolver != nullptr, LatestView, DisplayedHoldSnapshot);
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

void SCadenceArcDebuggerPanel::ApplySettings(const bool bSave)
{
	FCadenceArcLayoutParams Params = GraphView->GetLayoutParams();
	Settings.ApplyTo(Params);
	GraphView->SetLayoutParams(Params);
	if (bSave)
	{
		Settings.Save();
	}
}

TSharedRef<SWidget> SCadenceArcDebuggerPanel::MakeLayoutMenu()
{
	// 勾选后菜单不关闭，可以连续切换几项对比效果
	FMenuBuilder Menu(false, nullptr);
	const auto AddToggle = [this, &Menu](const TCHAR* Label, const TCHAR* ToolTip, bool FCadenceArcDebuggerSettings::* Option)
	{
		Menu.AddMenuEntry(
			FText::FromString(Label), FText::FromString(ToolTip), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Option]()
				{
					Settings.*Option = !(Settings.*Option);
					ApplySettings(true);
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this, Option]() { return Settings.*Option; })),
			NAME_None, EUserInterfaceActionType::ToggleButton);
	};

	Menu.BeginSection(NAME_None, FText::FromString(TEXT("Edges")));
	AddToggle(TEXT("Right-angle edges"),
	          TEXT("Draw forward edges with horizontal and vertical segments only. Each vertical segment gets its own track between columns. Off: smooth curves."),
	          &FCadenceArcDebuggerSettings::bOrthogonalEdges);
	AddToggle(TEXT("Reorder ports to reduce crossings"),
	          TEXT("Order each node's rows by where their edges go next: forward edges top to bottom, then references, jumps back, and self loops. Only the display order changes; the transition order in the asset is untouched."),
	          &FCadenceArcDebuggerSettings::bSortPorts);
	Menu.EndSection();

	Menu.BeginSection(NAME_None, FText::FromString(TEXT("Nodes")));
	AddToggle(TEXT("Compact chains"),
	          TEXT("Arrange simple chains vertically in one column to reduce graph width. Branches and merges remain connected to the same nodes. Has no effect when no simple chains can be compacted."),
	          &FCadenceArcDebuggerSettings::bCompactChains);
	AddToggle(TEXT("References for long edges"),
	          TEXT("Draw edges that span at least the minimum span as a short reference next to the source instead of a long line. Click a reference to jump to its target; click the stub at the target to jump back."),
	          &FCadenceArcDebuggerSettings::bUseReferences);
	Menu.AddWidget(
		SNew(SBox)
		.WidthOverride(48.f)
		[
			SNew(SSpinBox<int32>)
			.MinValue(2)
			.MaxValue(9)
			.Delta(1)
			.IsEnabled_Lambda([this]() { return Settings.bUseReferences; })
			.Value_Lambda([this]() { return Settings.ReferenceMinSpan; })
			.OnValueChanged_Lambda([this](const int32 NewValue)
			{
				Settings.ReferenceMinSpan = NewValue;
				ApplySettings(false); // 拖动中实时预览，松手再写配置
			})
			.OnValueCommitted_Lambda([this](const int32 NewValue, ETextCommit::Type)
			{
				Settings.ReferenceMinSpan = NewValue;
				ApplySettings(true);
			})
		],
		FText::FromString(TEXT("Minimum span (columns)")));
	Menu.EndSection();
	return Menu.MakeWidget();
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
		GraphView->RequestFollow(); // 换了实例，即使节点下标相同也重新定位一次
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
