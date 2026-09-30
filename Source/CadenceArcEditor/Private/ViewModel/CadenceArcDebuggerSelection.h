#pragma once

#include "CoreMinimal.h"

class UCadenceArcResolver;

// Arc Debugger 当前选中的 Resolver。历史 Tab 跟随这个选择，自己不再提供下拉框，
// 两个面板看的一定是同一个实例。只存弱引用，不延长 Resolver 的生命周期。
namespace CadenceArc::Editor::DebuggerSelection
{
	void Set(UCadenceArcResolver* Resolver, const FString& Label);
	void Clear();
	UCadenceArcResolver* GetResolver();
	FString GetLabel();
}
