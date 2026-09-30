#pragma once

#include "CoreMinimal.h"
#include "ViewModel/CadenceArcDebugEventText.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class UCadenceArcResolver;

/**
 * Arc History：跟随 Arc Debugger 选中的 Resolver，按时间倒序列出它的调试历史。
 * 成功的操作只显示一行摘要；失败的操作额外显示结果和原因。只读：不调用任何会改变 Resolver 的接口。
 */
class SCadenceArcHistoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcHistoryPanel)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FRow
	{
		uint64 Sequence = 0; // 0 表示"中间有记录被覆盖"的提示行
		FCadenceArcDebugEventText Text;
	};

	EActiveTimerReturnType OnRefreshTick(double CurrentTime, float DeltaTime);
	void ResetRows();
	void RebuildVisibleRows();
	bool PassesFilter(const FRow& Row) const;
	TSharedRef<ITableRow> MakeRowWidget(TSharedPtr<FRow> Row, const TSharedRef<STableViewBase>& Owner) const;
	FText GetFollowingText() const;

	TWeakObjectPtr<UCadenceArcResolver> ObservedResolver;
	FString ObservedLabel; // 正在显示的实例名；实例结束后保留，用于回看
	uint64 LastSequence = 0; // 已经读过的最新序号
	uint64 ClearedThrough = 0; // Clear 之后只显示序号更大的记录
	bool bFailuresOnly = false;

	TArray<TSharedPtr<FRow>> AllRows; // 最新的在前
	TArray<TSharedPtr<FRow>> VisibleRows;
	TSharedPtr<SListView<TSharedPtr<FRow>>> ListView;
};
