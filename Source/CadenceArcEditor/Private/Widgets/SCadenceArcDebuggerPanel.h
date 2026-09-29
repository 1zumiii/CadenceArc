#pragma once

#include "CoreMinimal.h"
#include "ViewModel/CadenceArcDebugView.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"


class UCadenceArcResolver;
class SCadenceArcGraphCanvas;
class SCadenceArcChargeTimeline;
class SScrollBox;
/**
 * 
 */
class SCadenceArcDebuggerPanel : public SCompoundWidget
{
	TSharedPtr<SCadenceArcGraphCanvas> Canvas;
	TSharedPtr<SCadenceArcChargeTimeline> ChargeTimeline;

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

	// 视口跟随：已提交节点变化时把它滚进视口，其余时间不动用户手动滚到的位置
	TSharedPtr<SScrollBox> HorizontalScroll;
	TSharedPtr<SScrollBox> VerticalScroll;
	bool bFollowCommittedNode = true;
	int32 LastFollowedNodeIndex = INDEX_NONE;
	int32 PendingFollowFrames = 0; // 换图后滚动区的内容尺寸下一帧才更新，跟随要连续做几帧

	void StopRefreshTimer();
	void FollowCommittedNode();
	void RequestFollow();
	void RefreshSelectedResolver();
	FText GetStateText() const;
	FText GetRequestText() const;
	FText GetWindowText() const;
	FText GetBufferedInputText() const;
	FText GetHoldText() const;
	EVisibility GetChargeVisibility() const;

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
