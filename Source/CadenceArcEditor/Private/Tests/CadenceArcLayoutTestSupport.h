#pragma once

// 布局测试共用的构图工具和断言。几个测试文件都要用，所以是外部链接的普通函数（定义在 .cpp 里），
// 不能写成 static，否则 Unity Build 关闭时每个翻译单元各有一份、开启时又会重复定义。

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Layout/CadenceArcGraphLayout.h"

class FAutomationTestBase;
class UCadenceArcGraph;

namespace CadenceArc::Editor::Tests
{
	// Editor 类型的模块不允许定义原生 GameplayTag（引擎在模块加载时 ensure，编辑器会直接起不来），
	// 所以这里按名字复用插件 Runtime 模块注册的测试 Tag。布局只关心 Tag 是否相同，不关心名字含义。
	// 必须在测试运行时再取：模块静态初始化阶段 GameplayTag 管理器可能还没准备好。
	FGameplayTag LayoutTag(const TCHAR* Name);
	FGameplayTag Layout_A();
	FGameplayTag Layout_B();
	FGameplayTag Layout_C();
	FGameplayTag Layout_D();
	FGameplayTag Layout_E();
	FGameplayTag Layout_F();
	FGameplayTag Layout_G();
	// 有效但不作为任何节点出现的 Tag，用来制造坏目标和缺失入口
	FGameplayTag Layout_Missing();
	FGameplayTag Layout_InputLight();
	FGameplayTag Layout_InputHeavy();

	// 布局不做图校验，所以可以直接构造重复 Tag、坏目标等非法图。
	UCadenceArcGraph* MakeLayoutGraph(const FGameplayTag& Entry, const TArray<FGameplayTag>& NodeTags);
	void AddLayoutEdge(
		UCadenceArcGraph* Graph, int32 SourceIndex, const FGameplayTag& Target,
		const FGameplayTag& Input = Layout_InputLight());

	// 仿照 Sandbox 的 DA_ComboGraphCombo（真实形状：分支树 + 共享终结技 + 一条循环回边）。
	UCadenceArcGraph* MakeRealComboGraph();

	bool ExpectCell(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, int32 NodeIndex,
		int32 ExpectedColumn, int32 ExpectedRow, bool bExpectReachable);
	bool ExpectEdge(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, int32 EdgeIndex,
		int32 ExpectedSource, int32 ExpectedTransition, int32 ExpectedTarget);
	bool ExpectExtent(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcGraphLayout& Layout,
		int32 ExpectedColumns, int32 ExpectedMaxRows);
	bool ExpectRouting(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, int32 EdgeIndex,
		bool bExpectBackEdge, int32 ExpectedLane);

	// 线段是否进入矩形（Liang-Barsky 裁剪）
	bool SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box);
	// 路径相邻两小段之间的最大转角（度）。光滑曲线的采样点之间只有小角度变化，折角会是几十度。
	double MaxTurnDegrees(const TArray<FVector2D>& Path);

	// 实际绘制路径和节点位置的几何性质（见定义处的说明）
	void ExpectGeometry(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout);
	// 对任意布局都应成立的性质：格子唯一、底部通道分配、几何合法
	void ExpectLayoutInvariants(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout);
}

#endif // WITH_DEV_AUTOMATION_TESTS
