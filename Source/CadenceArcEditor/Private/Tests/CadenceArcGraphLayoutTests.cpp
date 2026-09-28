// BuildGraphLayout 的确定性测试。布局是不依赖 Slate 的纯函数，图全部在内存中构造，不依赖资产。
//
// 被测契约（Phase 7 / 7A 第 5 步）：
// - Layout.Nodes 与 Graph->Nodes 一一对应、同序；NodeIndex 等于数组索引。
// - 列号是从入口出发的最短距离（BFS），行号是同一列内被发现的先后，按上一列顺序、Transitions 顺序展开。
// - 环、回边、自环、平行边不改变已确定的列号，也不会死循环。
// - 不可达节点放在"最深可达列 + 1"，行号按数组顺序；入口缺失时所有节点不可达，放在第 0 列。
// - 每条 Transition 生成一条边，按节点顺序、Transition 顺序排列；目标不存在时标记为坏目标。
// - 边携带 Transition 的 InputTag，平行边可据此区分。
// - 重复 Tag 始终指向数组中的第一个节点。
// - 同一张图每次构建结果完全一致。

#if WITH_DEV_AUTOMATION_TESTS

#include "Layout/CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Editor::Tests
{
	// Editor 类型的模块不允许定义原生 GameplayTag（引擎在模块加载时 ensure，编辑器会直接起不来），
	// 所以这里按名字复用插件 Runtime 模块注册的测试 Tag。布局只关心 Tag 是否相同，不关心名字含义。
	// 必须在测试运行时再取：模块静态初始化阶段 GameplayTag 管理器可能还没准备好。
	static FGameplayTag LayoutTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}

	static FGameplayTag Layout_A() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Root")); }
	static FGameplayTag Layout_B() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	static FGameplayTag Layout_C() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Light02")); }
	static FGameplayTag Layout_D() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Heavy01")); }
	static FGameplayTag Layout_E() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Heavy02")); }
	static FGameplayTag Layout_F() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher01")); }
	// 有效但不作为任何节点出现的 Tag，用来制造坏目标和缺失入口
	static FGameplayTag Layout_Missing() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher03")); }
	static FGameplayTag Layout_InputLight() { return LayoutTag(TEXT("CadenceArc.Automation.Input.Light")); }
	static FGameplayTag Layout_InputHeavy() { return LayoutTag(TEXT("CadenceArc.Automation.Input.Heavy")); }

	// 布局不做图校验，所以这里可以直接构造重复 Tag、坏目标等非法图。
	static UCadenceArcGraph* MakeLayoutGraph(const FGameplayTag& Entry, const TArray<FGameplayTag>& NodeTags)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Entry;
		for (const FGameplayTag& Tag : NodeTags)
		{
			Graph->Nodes.AddDefaulted_GetRef().ActionTag = Tag;
		}
		return Graph;
	}

	static void AddLayoutEdge(
		UCadenceArcGraph* Graph, const int32 SourceIndex, const FGameplayTag& Target,
		const FGameplayTag& Input = Layout_InputLight())
	{
		FCadenceArcTransition& Transition = Graph->Nodes[SourceIndex].Transitions.AddDefaulted_GetRef();
		Transition.InputTag = Input;
		Transition.TargetActionTag = Target;
	}

	static bool ExpectCell(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, const int32 NodeIndex,
		const int32 ExpectedColumn, const int32 ExpectedRow, const bool bExpectReachable)
	{
		if (!Test.TestTrue(*FString::Printf(TEXT("Node %d exists in layout"), NodeIndex),
		                   Layout.Nodes.IsValidIndex(NodeIndex)))
		{
			return false;
		}
		const FCadenceArcLayoutNode& Node = Layout.Nodes[NodeIndex];
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("Node %d column"), NodeIndex), Node.Column, ExpectedColumn);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("Node %d row"), NodeIndex), Node.Row, ExpectedRow);
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("Node %d reachable"), NodeIndex), Node.bReachable == bExpectReachable);
		return bPassed;
	}

	static bool ExpectEdge(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, const int32 EdgeIndex,
		const int32 ExpectedSource, const int32 ExpectedTransition, const int32 ExpectedTarget)
	{
		if (!Test.TestTrue(*FString::Printf(TEXT("Edge %d exists"), EdgeIndex), Layout.Edges.IsValidIndex(EdgeIndex)))
		{
			return false;
		}
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("Edge %d source"), EdgeIndex),
		                              Edge.SourceNodeIndex, ExpectedSource);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("Edge %d transition"), EdgeIndex),
		                          Edge.TransitionIndex, ExpectedTransition);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("Edge %d target"), EdgeIndex),
		                          Edge.TargetNodeIndex, ExpectedTarget);
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("Edge %d broken flag"), EdgeIndex), Edge.IsBrokenTarget() == (ExpectedTarget == INDEX_NONE));
		return bPassed;
	}

	static bool ExpectExtent(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcGraphLayout& Layout,
		const int32 ExpectedColumns, const int32 ExpectedMaxRows)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s column count"), What),
		                              Layout.NumColumns, ExpectedColumns);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s max rows"), What), Layout.MaxRows, ExpectedMaxRows);
		return bPassed;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutNodesAlignTest,
		"CadenceArc.Editor.Layout.NodesAlignWithGraph",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutNodesAlignTest::RunTest(const FString& Parameters)
	{
		// 入口不在第 0 个元素：布局节点仍按图数组顺序排列，而不是按访问顺序
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_C(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 2, Layout_A());
		AddLayoutEdge(Graph, 0, Layout_B());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		if (!TestEqual(TEXT("One layout node per graph node"), Layout.Nodes.Num(), Graph->Nodes.Num()))
		{
			return false;
		}
		for (int32 Index = 0; Index < Graph->Nodes.Num(); ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Node %d index"), Index), Layout.Nodes[Index].NodeIndex, Index);
			TestEqual(*FString::Printf(TEXT("Node %d tag"), Index), Layout.Nodes[Index].ActionTag.ToString(),
			          Graph->Nodes[Index].ActionTag.ToString());
		}
		ExpectCell(*this, Layout, 2, 0, 0, true); // 入口 C
		ExpectCell(*this, Layout, 0, 1, 0, true); // A
		ExpectCell(*this, Layout, 1, 2, 0, true); // B
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutLinearChainTest,
		"CadenceArc.Editor.Layout.LinearChain",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutLinearChainTest::RunTest(const FString& Parameters)
	{
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 1, Layout_C());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 0, 0, 0, true);
		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectCell(*this, Layout, 2, 2, 0, true);
		ExpectExtent(*this, TEXT("Linear chain"), Layout, 3, 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutBranchRowsTest,
		"CadenceArc.Editor.Layout.BranchRowOrder",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutBranchRowsTest::RunTest(const FString& Parameters)
	{
		// 数组顺序故意与发现顺序不同：行号必须跟随 Transitions 顺序，而不是 Nodes 数组顺序
		//        A
		//      / | \
		//     D  B  C      第 1 列按 A 的 Transitions 顺序：D、B、C
		//        |  |
		//        F  E      第 2 列按上一列顺序展开：先 B 的 F，再 C 的 E
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F()});
		AddLayoutEdge(Graph, 0, Layout_D());
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 0, Layout_C());
		AddLayoutEdge(Graph, 2, Layout_E()); // C -> E，C 在第 1 列第 2 行
		AddLayoutEdge(Graph, 1, Layout_F()); // B -> F，B 在第 1 列第 1 行
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 3, 1, 0, true); // D
		ExpectCell(*this, Layout, 1, 1, 1, true); // B
		ExpectCell(*this, Layout, 2, 1, 2, true); // C
		ExpectCell(*this, Layout, 5, 2, 0, true); // F：B 在 C 之前展开
		ExpectCell(*this, Layout, 4, 2, 1, true); // E
		ExpectExtent(*this, TEXT("Branching"), Layout, 3, 3);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutShortestDistanceTest,
		"CadenceArc.Editor.Layout.ColumnIsShortestDistance",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutShortestDistanceTest::RunTest(const FString& Parameters)
	{
		// A 先连 B、B 再连 C，A 的第二条边直接连 C。
		// 按深度优先会先经 B 走到 C，把 C 放在第 2 列；最短距离是 1。
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 1, Layout_C());
		AddLayoutEdge(Graph, 0, Layout_C(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectCell(*this, Layout, 2, 1, 1, true);
		ExpectExtent(*this, TEXT("Shortcut"), Layout, 2, 2);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutCyclesTest,
		"CadenceArc.Editor.Layout.CyclesAndSelfLoops",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCyclesTest::RunTest(const FString& Parameters)
	{
		// 回边 B -> A、自环 B -> B、入口自环 A -> A：都不改变已确定的列号，也不能死循环
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B()});
		AddLayoutEdge(Graph, 0, Layout_A(), Layout_InputHeavy());
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 1, Layout_A());
		AddLayoutEdge(Graph, 1, Layout_B(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 0, 0, 0, true);
		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectExtent(*this, TEXT("Cycles"), Layout, 2, 1);
		if (TestEqual(TEXT("Every transition becomes an edge"), Layout.Edges.Num(), 4))
		{
			ExpectEdge(*this, Layout, 0, 0, 0, 0); // A -> A
			ExpectEdge(*this, Layout, 1, 0, 1, 1); // A -> B
			ExpectEdge(*this, Layout, 2, 1, 0, 0); // B -> A
			ExpectEdge(*this, Layout, 3, 1, 1, 1); // B -> B
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutUnreachableTest,
		"CadenceArc.Editor.Layout.UnreachableNodesGoLast",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutUnreachableTest::RunTest(const FString& Parameters)
	{
		// C、D 从入口不可达。D -> A 是一条从不可达节点指回入口的边，不能让 D 变成可达
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_C(), Layout_A(), Layout_D(), Layout_B()});
		AddLayoutEdge(Graph, 1, Layout_B());
		AddLayoutEdge(Graph, 2, Layout_A());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 1, 0, 0, true);  // A
		ExpectCell(*this, Layout, 3, 1, 0, true);  // B
		ExpectCell(*this, Layout, 0, 2, 0, false); // C：最深可达列 1 之后，按数组顺序排第 0 行
		ExpectCell(*this, Layout, 2, 2, 1, false); // D
		ExpectExtent(*this, TEXT("Unreachable"), Layout, 3, 2);
		if (TestEqual(TEXT("Edges from unreachable nodes are kept"), Layout.Edges.Num(), 2))
		{
			ExpectEdge(*this, Layout, 0, 1, 0, 3); // A -> B
			ExpectEdge(*this, Layout, 1, 2, 0, 1); // D -> A
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutBrokenTargetTest,
		"CadenceArc.Editor.Layout.BrokenTargetEdge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutBrokenTargetTest::RunTest(const FString& Parameters)
	{
		// 目标 Tag 不在图里：边照样保留并标记为坏目标，也不影响其余节点的排布
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B()});
		AddLayoutEdge(Graph, 0, Layout_Missing());
		AddLayoutEdge(Graph, 0, Layout_B(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		if (TestEqual(TEXT("Broken edge is kept"), Layout.Edges.Num(), 2))
		{
			ExpectEdge(*this, Layout, 0, 0, 0, INDEX_NONE);
			ExpectEdge(*this, Layout, 1, 0, 1, 1);
		}
		ExpectCell(*this, Layout, 1, 1, 0, true); // 坏边不占第 1 列的行
		ExpectExtent(*this, TEXT("Broken target"), Layout, 2, 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutParallelEdgesTest,
		"CadenceArc.Editor.Layout.ParallelEdges",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutParallelEdgesTest::RunTest(const FString& Parameters)
	{
		// 同一对节点之间两条边（如轻按和长按两档都到 B）：两条都保留，B 只排一次
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B()});
		AddLayoutEdge(Graph, 0, Layout_B(), Layout_InputLight());
		AddLayoutEdge(Graph, 0, Layout_B(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		if (TestEqual(TEXT("Both parallel edges are kept"), Layout.Edges.Num(), 2))
		{
			ExpectEdge(*this, Layout, 0, 0, 0, 1);
			ExpectEdge(*this, Layout, 1, 0, 1, 1);
			// 平行边只能靠 InputTag 区分，布局必须原样带上
			TestEqual(TEXT("Edge 0 input tag"), Layout.Edges[0].InputTag.ToString(), Layout_InputLight().ToString());
			TestEqual(TEXT("Edge 1 input tag"), Layout.Edges[1].InputTag.ToString(), Layout_InputHeavy().ToString());
		}
		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectExtent(*this, TEXT("Parallel edges"), Layout, 2, 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutMissingEntryTest,
		"CadenceArc.Editor.Layout.MissingEntry",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutMissingEntryTest::RunTest(const FString& Parameters)
	{
		// 入口 Tag 不在图中：所有节点不可达，都放在第 0 列，行号按数组顺序；边仍然收集
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_Missing(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 0, Layout_B());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 0, 0, 0, false);
		ExpectCell(*this, Layout, 1, 0, 1, false);
		ExpectCell(*this, Layout, 2, 0, 2, false);
		ExpectExtent(*this, TEXT("Missing entry"), Layout, 1, 3);
		if (TestEqual(TEXT("Edges survive a missing entry"), Layout.Edges.Num(), 1))
		{
			ExpectEdge(*this, Layout, 0, 0, 0, 1);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutDuplicateTagTest,
		"CadenceArc.Editor.Layout.DuplicateTagUsesFirstNode",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutDuplicateTagTest::RunTest(const FString& Parameters)
	{
		// PIE 中资产被改出重复 Tag：边的目标固定指向第一个 B，第二个 B 从未被访问，按不可达处理
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B(), Layout_B()});
		AddLayoutEdge(Graph, 0, Layout_B());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		if (TestEqual(TEXT("One edge"), Layout.Edges.Num(), 1))
		{
			ExpectEdge(*this, Layout, 0, 0, 0, 1);
		}
		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectCell(*this, Layout, 2, 2, 0, false);
		ExpectExtent(*this, TEXT("Duplicate tag"), Layout, 3, 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutEmptyGraphTest,
		"CadenceArc.Editor.Layout.EmptyGraph",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutEmptyGraphTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*MakeLayoutGraph(Layout_A(), {}));
		TestEqual(TEXT("No nodes"), Layout.Nodes.Num(), 0);
		TestEqual(TEXT("No edges"), Layout.Edges.Num(), 0);
		ExpectExtent(*this, TEXT("Empty graph"), Layout, 0, 0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutDeterminismTest,
		"CadenceArc.Editor.Layout.Deterministic",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutDeterminismTest::RunTest(const FString& Parameters)
	{
		// 面板每帧重算布局：同一张图的结果必须逐字段一致，否则画面会闪
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_D(), Layout_A(), Layout_C(), Layout_B(), Layout_E()});
		AddLayoutEdge(Graph, 1, Layout_C());
		AddLayoutEdge(Graph, 1, Layout_B(), Layout_InputHeavy());
		AddLayoutEdge(Graph, 2, Layout_A());
		AddLayoutEdge(Graph, 3, Layout_Missing());
		AddLayoutEdge(Graph, 3, Layout_B());
		AddLayoutEdge(Graph, 0, Layout_E());

		const FCadenceArcGraphLayout First = BuildGraphLayout(*Graph);
		const FCadenceArcGraphLayout Second = BuildGraphLayout(*Graph);
		TestEqual(TEXT("Same column count"), Second.NumColumns, First.NumColumns);
		TestEqual(TEXT("Same max rows"), Second.MaxRows, First.MaxRows);
		if (TestEqual(TEXT("Same node count"), Second.Nodes.Num(), First.Nodes.Num()))
		{
			for (int32 Index = 0; Index < First.Nodes.Num(); ++Index)
			{
				TestEqual(*FString::Printf(TEXT("Node %d column stable"), Index),
				          Second.Nodes[Index].Column, First.Nodes[Index].Column);
				TestEqual(*FString::Printf(TEXT("Node %d row stable"), Index),
				          Second.Nodes[Index].Row, First.Nodes[Index].Row);
				TestTrue(*FString::Printf(TEXT("Node %d reachability stable"), Index), Second.Nodes[Index].bReachable == First.Nodes[Index].bReachable);
			}
		}
		if (TestEqual(TEXT("Same edge count"), Second.Edges.Num(), First.Edges.Num()))
		{
			for (int32 Index = 0; Index < First.Edges.Num(); ++Index)
			{
				TestEqual(*FString::Printf(TEXT("Edge %d target stable"), Index),
				          Second.Edges[Index].TargetNodeIndex, First.Edges[Index].TargetNodeIndex);
			}
		}
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
