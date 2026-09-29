// BuildGraphLayout 的确定性测试。布局是不依赖 Slate 的纯函数，图全部在内存中构造，不依赖资产。
//
// 被测契约（Phase 7 / 7A 第 5 步）：
// - Layout.Nodes 与 Graph->Nodes 一一对应、同序；NodeIndex 等于数组索引。
// - 从入口深度优先遍历（按节点、Transition 数组顺序），指向当前递归路径上节点的边是回边（含自环）。
// - 列号是去掉回边后从入口出发的最长路径：非回边一律指向严格更右的列；环不会死循环。
// - 行号初始按深度优先前序，再按相邻节点平均行号（重心）上下交替扫描 4 遍；平局按当前位置，结果确定。
// - 目标列不在源列右侧的非自环边分配底部通道：跨度小的在上，跨度相同按边序号。
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
	static FGameplayTag Layout_G() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher02")); }
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

	static bool ExpectRouting(
		FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout, const int32 EdgeIndex,
		const bool bExpectBackEdge, const int32 ExpectedLane)
	{
		if (!Layout.Edges.IsValidIndex(EdgeIndex))
		{
			return Test.TestTrue(*FString::Printf(TEXT("Edge %d exists"), EdgeIndex), false);
		}
		const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
		bool bPassed = Test.TestTrue(*FString::Printf(TEXT("Edge %d back edge flag"), EdgeIndex),
		                             Edge.bIsBackEdge == bExpectBackEdge);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("Edge %d return lane"), EdgeIndex),
		                          Edge.ReturnLane, ExpectedLane);
		return bPassed;
	}

	// 对任意布局都应成立的几何性质：画布据此决定走线，不成立就会出现穿过节点的连线
	static void ExpectLayoutInvariants(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout)
	{
		TSet<TPair<int32, int32>> Cells;
		for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			Test.TestFalse(*FString::Printf(TEXT("Node %d has a unique cell"), Node.NodeIndex),
			               Cells.Contains(TPair<int32, int32>(Node.Column, Node.Row)));
			Cells.Add(TPair<int32, int32>(Node.Column, Node.Row));
		}
		int32 NumLanes = 0;
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (Edge.IsBrokenTarget() || Edge.SourceNodeIndex == Edge.TargetNodeIndex)
			{
				Test.TestEqual(*FString::Printf(TEXT("Edge %d needs no lane"), EdgeIndex), Edge.ReturnLane, INDEX_NONE);
				continue;
			}
			const bool bPointsRight =
				Layout.Nodes[Edge.TargetNodeIndex].Column > Layout.Nodes[Edge.SourceNodeIndex].Column;
			Test.TestTrue(*FString::Printf(TEXT("Edge %d has a lane exactly when it does not point right"), EdgeIndex),
			              bPointsRight == (Edge.ReturnLane == INDEX_NONE));
			NumLanes += Edge.ReturnLane != INDEX_NONE ? 1 : 0;
		}
		Test.TestEqual(TEXT("Lane count matches"), Layout.NumReturnLanes, NumLanes);
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
		// 数组顺序故意与发现顺序不同：行号跟随遍历和重心，而不是 Nodes 数组顺序
		//        A
		//      / | \
		//     D  B  C      第 1 列按深度优先前序：D、B、C（三者的前驱都是 A，重心相同，保持原位）
		//        |  |
		//        F  E      第 2 列按前驱的行号：F 跟着 B（第 1 行），E 跟着 C（第 2 行）
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
		FCadenceArcLayoutLongestPathTest,
		"CadenceArc.Editor.Layout.ColumnIsLongestForwardPath",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutLongestPathTest::RunTest(const FString& Parameters)
	{
		// A -> B -> C，另有捷径 A -> C。按最短距离 C 会和 B 同在第 1 列，B -> C 就成了同列的边；
		// 按最长路径 C 排在 B 右边，两条边都指向右侧，不需要回边通道。
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 1, Layout_C());
		AddLayoutEdge(Graph, 0, Layout_C(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, 1, 1, 0, true);
		ExpectCell(*this, Layout, 2, 2, 0, true);
		ExpectExtent(*this, TEXT("Shortcut"), Layout, 3, 1);
		TestEqual(TEXT("Shortcut needs no return lane"), Layout.NumReturnLanes, 0);
		ExpectLayoutInvariants(*this, Layout);
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
			// 两个自环和 B -> A 都闭合环；只有 B -> A 需要底部通道，自环由画布单独绕
			ExpectRouting(*this, Layout, 0, true, INDEX_NONE);
			ExpectRouting(*this, Layout, 1, false, INDEX_NONE);
			ExpectRouting(*this, Layout, 2, true, 0);
			ExpectRouting(*this, Layout, 3, true, INDEX_NONE);
		}
		TestEqual(TEXT("Cycles use one return lane"), Layout.NumReturnLanes, 1);
		ExpectLayoutInvariants(*this, Layout);
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
			// D 不在遍历路径上，所以 D -> A 不是回边；但它指向左侧，仍然走底部通道
			ExpectRouting(*this, Layout, 1, false, 0);
		}
		ExpectLayoutInvariants(*this, Layout);
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
			TestEqual(TEXT("Edge 0 input tag"), Layout.Edges[0].Transition.InputTag.ToString(), Layout_InputLight().ToString());
			TestEqual(TEXT("Edge 1 input tag"), Layout.Edges[1].Transition.InputTag.ToString(), Layout_InputHeavy().ToString());
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
			// 没有入口就没有遍历：A -> B 两端同在第 0 列，走底部通道
			ExpectRouting(*this, Layout, 0, false, 0);
		}
		ExpectLayoutInvariants(*this, Layout);
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
				TestEqual(*FString::Printf(TEXT("Edge %d lane stable"), Index),
				          Second.Edges[Index].ReturnLane, First.Edges[Index].ReturnLane);
				TestTrue(*FString::Printf(TEXT("Edge %d back flag stable"), Index),
				         Second.Edges[Index].bIsBackEdge == First.Edges[Index].bIsBackEdge);
			}
		}
		TestEqual(TEXT("Same lane count"), Second.NumReturnLanes, First.NumReturnLanes);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutBarycenterTest,
		"CadenceArc.Editor.Layout.RowsFollowBarycenter",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutBarycenterTest::RunTest(const FString& Parameters)
	{
		// A 分出 B、C、D、E；B 和 E 都连 X，C 连 Y。按前序 X 先被发现，初始排成 X 在上、Y 在下，
		// E -> X 与 C -> Y 交叉。重心扫描后：
		//   第 1 列 C、B、E、D（C 跟随 Y 上移；D 没有后继，保持当前位置排到最后）
		//   第 2 列 Y、X
		// 结果没有交叉。
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F(), Layout_G()});
		constexpr int32 A = 0, B = 1, C = 2, D = 3, E = 4, X = 5, Y = 6;
		AddLayoutEdge(Graph, A, Layout_B());
		AddLayoutEdge(Graph, A, Layout_C());
		AddLayoutEdge(Graph, A, Layout_D());
		AddLayoutEdge(Graph, A, Layout_E());
		AddLayoutEdge(Graph, B, Layout_F()); // B -> X
		AddLayoutEdge(Graph, C, Layout_G()); // C -> Y
		AddLayoutEdge(Graph, E, Layout_F()); // E -> X
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, C, 1, 0, true);
		ExpectCell(*this, Layout, B, 1, 1, true);
		ExpectCell(*this, Layout, E, 1, 2, true);
		ExpectCell(*this, Layout, D, 1, 3, true);
		ExpectCell(*this, Layout, Y, 2, 0, true);
		ExpectCell(*this, Layout, X, 2, 1, true);
		ExpectExtent(*this, TEXT("Barycenter"), Layout, 3, 4);
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutReturnLaneOrderTest,
		"CadenceArc.Editor.Layout.ReturnLanesOrderedBySpan",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutReturnLaneOrderTest::RunTest(const FString& Parameters)
	{
		// 链 A -> B -> C -> D，回边 D -> A（跨 3 列）和 C -> B（跨 1 列）。
		// 节点数组把 D 放在 B、C 前面，D -> A 的边序号更小；但它跨度大，仍放在下面的通道，
		// 短回边贴近节点，两条回边不交叉。
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_D(), Layout_B(), Layout_C()});
		constexpr int32 A = 0, D = 1, B = 2, C = 3;
		AddLayoutEdge(Graph, A, Layout_B());                        // e0 A -> B
		AddLayoutEdge(Graph, D, Layout_A());                        // e1 D -> A
		AddLayoutEdge(Graph, B, Layout_C());                        // e2 B -> C
		AddLayoutEdge(Graph, C, Layout_D());                        // e3 C -> D
		AddLayoutEdge(Graph, C, Layout_B(), Layout_InputHeavy());   // e4 C -> B
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		ExpectCell(*this, Layout, A, 0, 0, true);
		ExpectCell(*this, Layout, B, 1, 0, true);
		ExpectCell(*this, Layout, C, 2, 0, true);
		ExpectCell(*this, Layout, D, 3, 0, true);
		ExpectRouting(*this, Layout, 3, false, INDEX_NONE);
		ExpectRouting(*this, Layout, 4, true, 0);
		ExpectRouting(*this, Layout, 1, true, 1);
		TestEqual(TEXT("Two return lanes"), Layout.NumReturnLanes, 2);
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutComboShapeTest,
		"CadenceArc.Editor.Layout.ComboShapeFlowsRight",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutComboShapeTest::RunTest(const FString& Parameters)
	{
		// 仿照 Sandbox 的复杂连招图：交叉引用、互相连成环、自环、平行边、多个终点。
		// 深度优先：A B E G | E->C C->B(回) C->F F->E(回) F->H | A->D D->D(自环)
		// 最长路径：A0 | B1 D1 | E2 | C3 G3 | F4 | H5
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F(), Layout_G(), Layout_Missing()});
		constexpr int32 A = 0, B = 1, C = 2, D = 3, E = 4, F = 5, G = 6, H = 7;
		AddLayoutEdge(Graph, A, Layout_B());                        // e0
		AddLayoutEdge(Graph, A, Layout_C(), Layout_InputHeavy());   // e1
		AddLayoutEdge(Graph, A, Layout_D(), Layout_InputHeavy());   // e2
		AddLayoutEdge(Graph, B, Layout_E());                        // e3
		AddLayoutEdge(Graph, B, Layout_C(), Layout_InputHeavy());   // e4
		AddLayoutEdge(Graph, B, Layout_G(), Layout_InputHeavy());   // e5
		AddLayoutEdge(Graph, C, Layout_B());                        // e6 回边
		AddLayoutEdge(Graph, C, Layout_F(), Layout_InputHeavy());   // e7
		AddLayoutEdge(Graph, D, Layout_F());                        // e8
		AddLayoutEdge(Graph, D, Layout_D(), Layout_InputHeavy());   // e9 自环
		AddLayoutEdge(Graph, D, Layout_Missing(), Layout_InputHeavy()); // e10 D -> H
		AddLayoutEdge(Graph, E, Layout_G());                        // e11
		AddLayoutEdge(Graph, E, Layout_G(), Layout_InputHeavy());   // e12 平行边
		AddLayoutEdge(Graph, E, Layout_C(), Layout_InputHeavy());   // e13
		AddLayoutEdge(Graph, F, Layout_E());                        // e14 回边
		AddLayoutEdge(Graph, F, Layout_Missing(), Layout_InputHeavy()); // e15 F -> H
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		const int32 ExpectedColumns[] = {0, 1, 3, 1, 2, 4, 3, 5};
		for (int32 Index = 0; Index < 8; ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Node %d column"), Index), Layout.Nodes[Index].Column, ExpectedColumns[Index]);
			TestTrue(*FString::Printf(TEXT("Node %d reachable"), Index), Layout.Nodes[Index].bReachable);
		}
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const bool bExpectBack = EdgeIndex == 6 || EdgeIndex == 9 || EdgeIndex == 14;
			TestTrue(*FString::Printf(TEXT("Edge %d back edge flag"), EdgeIndex),
			         Layout.Edges[EdgeIndex].bIsBackEdge == bExpectBack);
		}
		// 两条回边跨度都是 2，按边序号分配通道
		ExpectRouting(*this, Layout, 6, true, 0);
		ExpectRouting(*this, Layout, 14, true, 1);
		ExpectRouting(*this, Layout, 9, true, INDEX_NONE);
		TestEqual(TEXT("Two return lanes"), Layout.NumReturnLanes, 2);
		ExpectExtent(*this, TEXT("Combo shape"), Layout, 6, 2);
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
