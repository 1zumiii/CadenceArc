#pragma once

#include "CoreMinimal.h"
#include "CadenceArcDebuggerSettings.h"
#include "ViewModel/CadenceArcDebugView.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"


class UCadenceArcResolver;
class SCadenceArcGraphView;
class SCadenceArcRuntimeDetails;

/**
 * Arc Debugger 标签页：选择 PIE 里的 Resolver，每帧读取它的状态交给图视口和右侧运行时信息显示。
 * 工具栏：Resolver 下拉框、刷新、跟随开关、布局选项菜单。
 */
class SCadenceArcDebuggerPanel : public SCompoundWidget
{
	TSharedPtr<SCadenceArcGraphView> GraphView;
	TSharedPtr<SCadenceArcRuntimeDetails> RuntimeDetails;

	struct FResolverOption
	{
		TWeakObjectPtr<UCadenceArcResolver> Resolver;
		FString Label;
	};

	TArray<TSharedPtr<FResolverOption>> Options;
	TSharedPtr<SComboBox<TSharedPtr<FResolverOption>>> ResolverCombo;
	TWeakObjectPtr<UCadenceArcResolver> SelectedResolver;
	FDelegateHandle EndPIEHandle;
	FCadenceArcDebugView LatestView;
	FCadenceArcHoldSnapshot DisplayedHoldSnapshot;
	FGameplayTag LastObservedActionTag;

	TSharedPtr<FActiveTimerHandle> RefreshTimerHandle;

	// 布局选项，记在编辑器的个人项目配置里
	FCadenceArcDebuggerSettings Settings;
	void ApplySettings(bool bSave);
	TSharedRef<SWidget> MakeLayoutMenu();

	void StopRefreshTimer();
	void RefreshSelectedResolver();

public:
	SLATE_BEGIN_ARGS(SCadenceArcDebuggerPanel)
		{
		}

	SLATE_END_ARGS()

	FReply OnRefreshClicked();
	TSharedRef<SWidget> MakeOptionWidget(TSharedPtr<FResolverOption> Shared);
	FText GetSelectedLabel() const;
	EActiveTimerReturnType OnRefreshTick(double X, float Arg);
	void OnResolverSelected(TSharedPtr<FResolverOption> ResolverOption, ESelectInfo::Type Arg);
	void OnEndPIE(bool bArg);
	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	virtual ~SCadenceArcDebuggerPanel() override;
};
