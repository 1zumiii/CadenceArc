#include "SCadenceArcDebuggerPanel.h"

#include "SCadenceArcGraphCanvas.h"
#include "SlateOptMacros.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Resolver/CadenceArcResolver.h"
#include "UObject/UObjectIterator.h"


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
			]
		]
		+ SVerticalBox::Slot()
		[
			SNew(SScrollBox)
			.Orientation(Orient_Horizontal)
			+ SScrollBox::Slot() // SScrollBox 可以放多个子项，所以用 + Slot()
			[
				SNew(SScrollBox)
				.Orientation(Orient_Vertical)
				+ SScrollBox::Slot()
				[
					SAssignNew(Canvas, SCadenceArcGraphCanvas) // 和 SNew 一样，但会把指针存进 Canvas
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

void SCadenceArcDebuggerPanel::OnResolverSelected(TSharedPtr<FResolverOption> ResolverOption, ESelectInfo::Type Arg)
{
	SelectedResolver = ResolverOption.IsValid() ? ResolverOption->Resolver : nullptr;
	const UCadenceArcResolver* Resolver = SelectedResolver.Get();
	Canvas->SetGraph(Resolver ? Resolver->GetGraph() : nullptr);
}

void SCadenceArcDebuggerPanel::OnEndPIE(bool bArg)
{
	Options.Reset();
	ResolverCombo->RefreshOptions();
	ResolverCombo->ClearSelection();
	SelectedResolver.Reset();
	Canvas->SetGraph(nullptr);
}
