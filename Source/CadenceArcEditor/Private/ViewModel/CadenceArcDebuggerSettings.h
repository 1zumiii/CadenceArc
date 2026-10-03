#pragma once

#include "CoreMinimal.h"

struct FCadenceArcLayoutParams;

// Arc Debugger 的布局选项，记在编辑器的个人项目配置（EditorPerProjectUserSettings.ini）里。
// 配置里没有的项保持这里的默认值。
struct FCadenceArcDebuggerSettings
{
	bool bOrthogonalEdges = true; // 前向边画成直角折线；关闭时是光滑曲线
	bool bSortPorts = true; // 按出边去向重排端口行，减少交叉（不改资产里的顺序）
	bool bCompactChains = false; // 简单链纵向展开
	bool bUseReferences = false; // 长边改成引用标签
	int32 ReferenceMinSpan = 3; // 列号差不小于它的边改成引用标签（2 .. 9）
	bool bShowInputDisplay = true; // 画布左下角显示解析器最近收到的输入、上下文和停顿

	static FCadenceArcDebuggerSettings Load();
	void Save() const;
	// 把这些选项写进布局参数，其余几何参数保持不变
	void ApplyTo(FCadenceArcLayoutParams& Params) const;
};
