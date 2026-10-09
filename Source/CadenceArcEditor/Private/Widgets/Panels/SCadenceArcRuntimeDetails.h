#pragma once

#include "CoreMinimal.h"
#include "ViewModel/CadenceArcDebugView.h"
#include "Widgets/SCompoundWidget.h"

class SCadenceArcChargeTimeline;

/**
 * Arc Debugger 右侧的运行时信息：状态、待开始的请求、缓冲窗口、缓冲的输入、按住与蓄力时间轴。
 * 只显示面板传进来的快照。
 */
class SCadenceArcRuntimeDetails : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcRuntimeDetails)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// View：这一帧的调试状态；DisplayedHold：要显示的按住状态（松手后仍保留最后一次观察到的，见面板）
	void Update(bool bHasResolver, const FCadenceArcDebugView& View, const FCadenceArcHoldSnapshot& DisplayedHold);

private:
	TSharedPtr<SCadenceArcChargeTimeline> ChargeTimeline;
	bool bHasResolver = false;
	FCadenceArcDebugView LatestView;
	FCadenceArcHoldSnapshot DisplayedHoldSnapshot;

	FText GetStateText() const;
	FText GetRecoveryText() const;
	FText GetRequestText() const;
	FText GetWindowText() const;
	FText GetBufferedInputText() const;
	FText GetHoldText() const;
	EVisibility GetChargeVisibility() const;
};
