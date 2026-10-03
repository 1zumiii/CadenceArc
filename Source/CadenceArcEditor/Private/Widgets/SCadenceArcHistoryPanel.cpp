#include "SCadenceArcHistoryPanel.h"

#include "Engine/World.h"
#include "Resolver/CadenceArcResolver.h"
#include "Styling/CoreStyle.h"
#include "ViewModel/CadenceArcDebuggerSelection.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// 面板里最多保留的行数；Resolver 自己只保留 256 条，这里多留一些，Clear 前的旧记录仍可回看
	constexpr int32 MaxRows = 1024;
	const FLinearColor SuccessColor(0.85f, 0.85f, 0.88f);
	const FLinearColor FailureColor(1.0f, 0.45f, 0.40f);
	const FLinearColor DetailColor(1.0f, 0.70f, 0.45f);
	const FLinearColor MutedColor(0.55f, 0.55f, 0.60f);
}

void SCadenceArcHistoryPanel::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(this, &SCadenceArcHistoryPanel::GetFollowingText)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this]()
				{
					return bFailuresOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				})
				.OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState)
				{
					bFailuresOnly = NewState == ECheckBoxState::Checked;
					RebuildVisibleRows();
				})
				.ToolTipText(FText::FromString(TEXT("Only show calls that did not do what they asked for.")))
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("Failures only")))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(12.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Clear")))
				.ToolTipText(FText::FromString(TEXT("Hide everything recorded so far. Recording itself never stops.")))
				.OnClicked_Lambda([this]()
				{
					ClearedThrough = LastSequence;
					ListView->ClearSelection();
					CadenceArc::Editor::DebuggerSelection::SetHistoryFocus(FCadenceArcHistoryFocus());
					AllRows.Reset();
					RebuildVisibleRows();
					return FReply::Handled();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SAssignNew(ListView, SListView<TSharedPtr<FRow>>)
			.ListItemsSource(&VisibleRows)
			.OnGenerateRow(this, &SCadenceArcHistoryPanel::MakeRowWidget)
			.SelectionMode(ESelectionMode::Single)
			.OnSelectionChanged(this, &SCadenceArcHistoryPanel::OnRowSelected)
		]
	];

	// 与 Arc Debugger 一样只在面板打开时拉取；Resolver 不回调面板
	RegisterActiveTimer(0.f, FWidgetActiveTimerDelegate::CreateSP(this, &SCadenceArcHistoryPanel::OnRefreshTick));
}

EActiveTimerReturnType SCadenceArcHistoryPanel::OnRefreshTick(double CurrentTime, float DeltaTime)
{
	UCadenceArcResolver* Resolver = CadenceArc::Editor::DebuggerSelection::GetResolver();
	if (!Resolver)
	{
		// PIE 结束或取消了选择：保留已读到的记录供回看，标题标明已结束
		return EActiveTimerReturnType::Continue;
	}
	if (Resolver != ObservedResolver.Get())
	{
		// 选了另一个实例：历史属于实例本身，不能串到新实例上
		ObservedResolver = Resolver;
		ObservedLabel = CadenceArc::Editor::DebuggerSelection::GetLabel();
		ResetRows();
	}

	const FCadenceArcDebugHistory& History = Resolver->GetDebugHistory();
	if (History.GetNewestSequence() == LastSequence)
	{
		return EActiveTimerReturnType::Continue;
	}
	TArray<FCadenceArcDebugEvent> NewEvents;
	History.CopyEventsAfter(FMath::Max(LastSequence, ClearedThrough), NewEvents);
	// 两次读取之间 Resolver 覆盖掉了一些记录：明确告诉读者中间有缺口，不假装连续
	const uint64 FirstExpected = FMath::Max(LastSequence, ClearedThrough) + 1;
	if (!NewEvents.IsEmpty() && NewEvents[0].Sequence > FirstExpected && LastSequence > 0)
	{
		const TSharedPtr<FRow> Gap = MakeShared<FRow>();
		Gap->Text.Summary = FString::Printf(TEXT("… %llu earlier records were overwritten before they could be shown"),
		                                    static_cast<unsigned long long>(NewEvents[0].Sequence - FirstExpected));
		AllRows.Insert(Gap, 0);
	}
	// 记录里完全没有时间的（例如宿主还没传过任何时间之前的 Initialize），用面板读到它时的 PIE 世界时间补上
	const UWorld* World = Resolver->GetWorld();
	for (const FCadenceArcDebugEvent& Event : NewEvents)
	{
		const TSharedPtr<FRow> Row = MakeShared<FRow>();
		Row->Sequence = Event.Sequence;
		Row->Text = FormatDebugEvent(Event, Resolver->GetGraph());
		Row->Focus = MakeHistoryFocus(Event);
		if (Row->Text.Time.IsEmpty() && World)
		{
			Row->Text.Time = FString::Printf(TEXT("~%.2fs"), World->GetTimeSeconds());
		}
		AllRows.Insert(Row, 0);
	}
	if (AllRows.Num() > MaxRows)
	{
		AllRows.SetNum(MaxRows);
	}
	LastSequence = History.GetNewestSequence();
	RebuildVisibleRows();
	return EActiveTimerReturnType::Continue;
}

void SCadenceArcHistoryPanel::OnRowSelected(TSharedPtr<FRow> Row, ESelectInfo::Type SelectInfo)
{
	// 点中一行：在 Arc Debugger 的图上高亮它对应的边或节点；取消选择就清掉高亮
	CadenceArc::Editor::DebuggerSelection::SetHistoryFocus(
		Row.IsValid() && Row->Sequence != 0 ? Row->Focus : FCadenceArcHistoryFocus());
}

void SCadenceArcHistoryPanel::ResetRows()
{
	CadenceArc::Editor::DebuggerSelection::SetHistoryFocus(FCadenceArcHistoryFocus());
	if (ListView.IsValid())
	{
		ListView->ClearSelection();
	}
	AllRows.Reset();
	LastSequence = 0;
	ClearedThrough = 0;
	RebuildVisibleRows();
}

bool SCadenceArcHistoryPanel::PassesFilter(const FRow& Row) const
{
	return !bFailuresOnly || Row.Text.bFailed || Row.Sequence == 0;
}

void SCadenceArcHistoryPanel::RebuildVisibleRows()
{
	VisibleRows.Reset();
	for (const TSharedPtr<FRow>& Row : AllRows)
	{
		if (PassesFilter(*Row))
		{
			VisibleRows.Add(Row);
		}
	}
	if (ListView.IsValid())
	{
		ListView->RequestListRefresh();
	}
}

TSharedRef<ITableRow> SCadenceArcHistoryPanel::MakeRowWidget(
	TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner) const
{
	const FSlateFontInfo MainFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	const FSlateFontInfo DetailFont = FCoreStyle::GetDefaultFontStyle("Italic", 8);
	const bool bGap = Row->Sequence == 0;
	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4.f, 1.f, 8.f, 1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(bGap ? FString() : FString::Printf(TEXT("#%llu"),
				                                                           static_cast<unsigned long long>(Row->Sequence))))
				.Font(MainFont)
				.ColorAndOpacity(MutedColor)
				.MinDesiredWidth(44.f)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 1.f, 8.f, 1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Row->Text.Time))
				.Font(MainFont)
				.ColorAndOpacity(MutedColor)
				.MinDesiredWidth(56.f)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 1.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString((Row->Text.bFailed ? TEXT("✗ ") : TEXT("")) + Row->Text.Summary))
				.Font(MainFont)
				.ColorAndOpacity(bGap ? MutedColor : (Row->Text.bFailed ? FailureColor : SuccessColor))
			]
		];
	if (Row->Text.bFailed && !Row->Text.FailureDetail.IsEmpty())
	{
		Content->AddSlot()
		.AutoHeight()
		.Padding(116.f, 0.f, 4.f, 3.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Row->Text.FailureDetail))
			.Font(DetailFont)
			.ColorAndOpacity(DetailColor)
			.AutoWrapText(true)
		];
	}
	return SNew(STableRow<TSharedPtr<FRow>>, Owner)[Content];
}

FText SCadenceArcHistoryPanel::GetFollowingText() const
{
	if (ObservedLabel.IsEmpty())
	{
		return FText::FromString(TEXT("Select a resolver in Arc Debugger"));
	}
	const bool bLive = ObservedResolver.IsValid()
		&& ObservedResolver.Get() == CadenceArc::Editor::DebuggerSelection::GetResolver();
	return FText::FromString(FString::Printf(TEXT("History of %s%s"), *ObservedLabel, bLive ? TEXT("") : TEXT(" (ended)")));
}
