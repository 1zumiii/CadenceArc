#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class UCadenceArcResolver;
class SCadenceArcGraphCanvas;
/**
 * 
 */
class SCadenceArcDebuggerPanel : public SCompoundWidget
{
	TSharedPtr<SCadenceArcGraphCanvas> Canvas;

	struct FResolverOption
	{
		TWeakObjectPtr<UCadenceArcResolver> Resolver;
		FString Label;
	};

	TArray<TSharedPtr<FResolverOption>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FResolverOption>>> ResolverCombo;
	TWeakObjectPtr<UCadenceArcResolver> SelectedResolver;
	FDelegateHandle EndPIEHandle;

public:
	SLATE_BEGIN_ARGS(SCadenceArcDebuggerPanel)
		{
		}

	SLATE_END_ARGS()

	FReply OnRefreshClicked();
	TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FResolverOption> Shared);
	FText GetSelectedLabel() const;
	void OnResolverSelected(TSharedPtr<FResolverOption> ResolverOption, ESelectInfo::Type Arg);
	void OnEndPIE(bool bArg);
	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	virtual ~SCadenceArcDebuggerPanel() override
	{
		if (EndPIEHandle.IsValid())
		{
			FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		}
	}
};
