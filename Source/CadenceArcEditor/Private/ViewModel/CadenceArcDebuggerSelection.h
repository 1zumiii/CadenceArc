#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UCadenceArcResolver;

// 在 Arc History 里点中的一条记录对应图上的哪里：一条边（源、目标、输入都有），或者只是一个节点（Target 为空）。
// 用 Tag 而不是索引表示，Arc Debugger 按自己当前的布局去解析，布局重建也不会指错。
struct FCadenceArcHistoryFocus
{
	uint64 Sequence = 0; // 0 表示没有选中任何记录
	FGameplayTag SourceNode;
	FGameplayTag TargetNode;
	FGameplayTag InputTag;
};

// Arc Debugger 当前选中的 Resolver，以及 Arc History 里当前点中的记录。
// 历史 Tab 跟随这个选择，自己不再提供下拉框，两个面板看的一定是同一个实例。只存弱引用，不延长 Resolver 的生命周期。
namespace CadenceArc::Editor::DebuggerSelection
{
	void Set(UCadenceArcResolver* Resolver, const FString& Label);
	void Clear();
	UCadenceArcResolver* GetResolver();
	FString GetLabel();

	void SetHistoryFocus(const FCadenceArcHistoryFocus& Focus);
	const FCadenceArcHistoryFocus& GetHistoryFocus();
}
