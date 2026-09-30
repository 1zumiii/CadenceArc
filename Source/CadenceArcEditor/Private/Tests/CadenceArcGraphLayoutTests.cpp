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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	// 线段是否进入矩形（Liang-Barsky 裁剪）。矩形按线路安全边距外扩后再判断。
	static bool SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
	{
		double T0 = 0.0;
		double T1 = 1.0;
		const FVector2D Delta = B - A;
		const double P[4] = {-Delta.X, Delta.X, -Delta.Y, Delta.Y};
		const double Q[4] = {A.X - Box.Min.X, Box.Max.X - A.X, A.Y - Box.Min.Y, Box.Max.Y - A.Y};
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (FMath::IsNearlyZero(P[Index]))
			{
				if (Q[Index] < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = Q[Index] / P[Index];
			if (P[Index] < 0.0)
			{
				T0 = FMath::Max(T0, T);
			}
			else
			{
				T1 = FMath::Min(T1, T);
			}
			if (T0 > T1)
			{
				return false;
			}
		}
		return true;
	}

	// 实际绘制路径的几何性质，检查的是画布照着画的点列本身，而不只是途经点：
	// - 每条边从源节点端口出发；有效目标的边终止于目标左边界、标题行高度以内；
	// - 路径的每一小段都不进入任何无关节点（外扩线路安全边距）；
	// - 引用边：端口短线接到标记左边缘，接入线终止于目标左边界；标记和接入线不碰任何节点（包括两端），
	//   标记右侧离下一列至少留出接入箭头的位置，并且在画布尺寸之内；
	// - 同一列真实节点按行号自上而下排列，彼此不重叠并留出最小间距。
	static void ExpectGeometry(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout)
	{
		const FCadenceArcLayoutParams Params;
		constexpr double Clearance = 2.0;
		constexpr double ArrowRoom = 8.0; // 画布上的箭头长 7
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (!Test.TestTrue(*FString::Printf(TEXT("Edge %d has a path"), EdgeIndex), Edge.Path.Num() >= 2))
			{
				continue;
			}
			const FCadenceArcLayoutNode& Source = Layout.Nodes[Edge.SourceNodeIndex];
			const FVector2D ExpectedPort = Source.Position + FVector2D(
				Params.NodeWidth, Params.HeaderHeight + (Edge.TransitionIndex + 0.5) * Params.PortHeight);
			Test.TestTrue(*FString::Printf(TEXT("Edge %d starts at its port"), EdgeIndex),
			              Edge.Path[0].Equals(ExpectedPort, 1.e-6));
			Test.TestTrue(*FString::Printf(TEXT("Edge %d has reference parts exactly when it is a reference"), EdgeIndex),
			              Edge.bIsReference == Edge.ReferenceBox.bIsValid && Edge.bIsReference == (Edge.EntryStub.Num() >= 2));
			if (!Edge.IsBrokenTarget())
			{
				const FCadenceArcLayoutNode& Target = Layout.Nodes[Edge.TargetNodeIndex];
				const FVector2D End = Edge.bIsReference ? Edge.EntryStub.Last() : Edge.Path.Last();
				Test.TestTrue(*FString::Printf(TEXT("Edge %d ends on the target title's left edge"), EdgeIndex),
				              FMath::IsNearlyEqual(End.X, Target.Position.X, 1.e-6)
				              && End.Y >= Target.Position.Y && End.Y <= Target.Position.Y + Params.HeaderHeight);
			}
			if (Edge.bIsReference)
			{
				const FBox2D& Box = Edge.ReferenceBox;
				Test.TestTrue(*FString::Printf(TEXT("Edge %d stub reaches its reference"), EdgeIndex),
				              Edge.Path.Last().Equals(FVector2D(Box.Min.X, Box.GetCenter().Y), 1.e-6));
				Test.TestTrue(*FString::Printf(TEXT("Edge %d reference leaves room before the next column"), EdgeIndex),
				              Box.Max.X <= Source.Position.X + Params.NodeWidth + Params.ColumnGap - ArrowRoom);
				Test.TestTrue(*FString::Printf(TEXT("Edge %d reference is inside the canvas"), EdgeIndex),
				              Box.Min.X >= 0.0 && Box.Min.Y >= 0.0 && Box.Max.X <= Layout.Size.X && Box.Max.Y <= Layout.Size.Y);
			}
			for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
			{
				const FBox2D Obstacle(Node.Position - FVector2D(Clearance), Node.Position + Node.Size + FVector2D(Clearance));
				if (Edge.bIsReference)
				{
					bool bStubHit = false;
					for (int32 Point = 1; Point < Edge.EntryStub.Num() && !bStubHit; ++Point)
					{
						// 接入线的终点就落在目标边界上，所以只对目标检查不外扩的矩形
						const FBox2D Box = Node.NodeIndex == Edge.TargetNodeIndex
							? FBox2D(Node.Position + FVector2D(0.1, 0.0), Node.Position + Node.Size) : Obstacle;
						bStubHit = SegmentHitsBox(Edge.EntryStub[Point - 1], Edge.EntryStub[Point], Box);
					}
					Test.TestFalse(*FString::Printf(TEXT("Edge %d entry stub crosses node %d"), EdgeIndex, Node.NodeIndex), bStubHit);
					Test.TestFalse(*FString::Printf(TEXT("Edge %d reference overlaps node %d"), EdgeIndex, Node.NodeIndex),
					               Edge.ReferenceBox.Intersect(Obstacle));
				}
				if (Node.NodeIndex == Edge.SourceNodeIndex || Node.NodeIndex == Edge.TargetNodeIndex)
				{
					continue;
				}
				bool bHit = false;
				for (int32 Point = 1; Point < Edge.Path.Num() && !bHit; ++Point)
				{
					bHit = SegmentHitsBox(Edge.Path[Point - 1], Edge.Path[Point], Obstacle);
				}
				Test.TestFalse(*FString::Printf(TEXT("Edge %d crosses unrelated node %d"), EdgeIndex, Node.NodeIndex), bHit);
			}
		}

		TMap<int32, TArray<const FCadenceArcLayoutNode*>> ByColumn;
		for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			ByColumn.FindOrAdd(Node.Column).Add(&Node);
		}
		for (TPair<int32, TArray<const FCadenceArcLayoutNode*>>& Pair : ByColumn)
		{
			Pair.Value.Sort([](const FCadenceArcLayoutNode& A, const FCadenceArcLayoutNode& B) { return A.Row < B.Row; });
			for (int32 Index = 1; Index < Pair.Value.Num(); ++Index)
			{
				const FCadenceArcLayoutNode& Above = *Pair.Value[Index - 1];
				const FCadenceArcLayoutNode& Below = *Pair.Value[Index];
				Test.TestTrue(*FString::Printf(TEXT("Node %d is below node %d with a gap"), Below.NodeIndex, Above.NodeIndex),
				              Below.Position.Y >= Above.Position.Y + Above.Size.Y + Params.ChannelGap - 1.e-6);
				Test.TestTrue(*FString::Printf(TEXT("Node %d shares its column's x"), Below.NodeIndex),
				              Below.Position.X == Above.Position.X);
			}
		}
	}

	// 对任意布局都应成立的性质：画布据此决定走线，不成立就会出现穿过节点的连线
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
			if (Edge.IsBrokenTarget() || Edge.SourceNodeIndex == Edge.TargetNodeIndex || Edge.bIsReference)
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
		ExpectGeometry(Test, Layout);
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
				TestTrue(*FString::Printf(TEXT("Node %d position stable"), Index),
				         Second.Nodes[Index].Position == First.Nodes[Index].Position
				         && Second.Nodes[Index].Size == First.Nodes[Index].Size);
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
				TestTrue(*FString::Printf(TEXT("Edge %d path stable point by point"), Index),
				         Second.Edges[Index].Path == First.Edges[Index].Path);
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

	// ---- 几何：长边通道、接入点错开、混合高度、视口跟随 ----

	// 仿照 Sandbox 的 DA_TestComboGraphCombo（真实形状：分支树 + 共享终结技 + 一条循环回边）。
	// 布局只比较 Tag 是否相同，这里借用两个输入 Tag 充当第 9、10 个动作节点。
	static UCadenceArcGraph* MakeRealComboGraph()
	{
		const FGameplayTag Root = Layout_A(), SkillA = Layout_B(), SkillD = Layout_C(), SkillE = Layout_D();
		const FGameplayTag SkillB = Layout_E(), SkillF = Layout_F(), SkillC = Layout_G();
		const FGameplayTag FinalA = Layout_Missing(), FinalB = Layout_InputLight(), FinalC = Layout_InputHeavy();
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Root, {Root, SkillA, SkillD, SkillE, SkillB, SkillF, SkillC, FinalA, FinalB, FinalC});
		AddLayoutEdge(Graph, 0, SkillA);
		AddLayoutEdge(Graph, 0, SkillB);
		AddLayoutEdge(Graph, 0, SkillC);
		AddLayoutEdge(Graph, 1, SkillD);
		AddLayoutEdge(Graph, 1, FinalA);
		AddLayoutEdge(Graph, 1, FinalB);
		AddLayoutEdge(Graph, 2, SkillE);
		AddLayoutEdge(Graph, 2, FinalB);
		AddLayoutEdge(Graph, 3, SkillA); // 循环回第一段
		AddLayoutEdge(Graph, 3, FinalB);
		AddLayoutEdge(Graph, 3, FinalC);
		AddLayoutEdge(Graph, 4, SkillF);
		AddLayoutEdge(Graph, 4, FinalA);
		AddLayoutEdge(Graph, 5, FinalA);
		AddLayoutEdge(Graph, 6, FinalC);
		return Graph;
	}

	// 路径相邻两小段之间的最大转角（度）。光滑曲线的采样点之间只有小角度变化，折角会是几十度。
	static double MaxTurnDegrees(const TArray<FVector2D>& Path)
	{
		double MaxTurn = 0.0;
		for (int32 Point = 2; Point < Path.Num(); ++Point)
		{
			const FVector2D In = (Path[Point - 1] - Path[Point - 2]).GetSafeNormal();
			const FVector2D Out = (Path[Point] - Path[Point - 1]).GetSafeNormal();
			const double Cosine = FMath::Clamp(FVector2D::DotProduct(In, Out), -1.0, 1.0);
			MaxTurn = FMath::Max(MaxTurn, FMath::RadiansToDegrees(FMath::Acos(Cosine)));
		}
		return MaxTurn;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutLongEdgeChannelTest,
		"CadenceArc.Editor.Layout.LongEdgesUseChannels",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutLongEdgeChannelTest::RunTest(const FString& Parameters)
	{
		// 共享终结技带来跨多列的长边（例如 SkillA -> FinalB、SkillC -> FinalC）。
		// 长边在中间列各有一个通道为它留出空间，整条路径是光滑曲线，且不压在任何无关节点上。
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*MakeRealComboGraph());
		const int32 ExpectedColumns[] = {0, 1, 2, 3, 1, 2, 1, 3, 4, 4};
		for (int32 Index = 0; Index < 10; ++Index)
		{
			TestEqual(*FString::Printf(TEXT("Node %d column"), Index), Layout.Nodes[Index].Column, ExpectedColumns[Index]);
		}
		int32 LongEdges = 0;
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
			if (Edge.ReturnLane != INDEX_NONE)
			{
				continue;
			}
			LongEdges += Layout.Nodes[Edge.TargetNodeIndex].Column - Layout.Nodes[Edge.SourceNodeIndex].Column > 1 ? 1 : 0;
			// 前向边是一条光滑曲线：没有折角（阈值远小于直角拐弯，远大于曲线采样间的转角）
			TestTrue(*FString::Printf(TEXT("Edge %d is a smooth curve"), EdgeIndex), MaxTurnDegrees(Edge.Path) < 35.0);
		}
		TestTrue(TEXT("The combo has long edges to route"), LongEdges >= 3);
		TestEqual(TEXT("Only the loop back uses a return lane"), Layout.NumReturnLanes, 1);
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutReferenceTest,
		"CadenceArc.Editor.Layout.ReferencesReplaceLongEdges",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutReferenceTest::RunTest(const FString& Parameters)
	{
		// 打开引用标记：列号差不小于阈值的边（前向和往回的都算）改成引用，列号不变；
		// 引用边不占底部通道，两端标记的几何由 ExpectLayoutInvariants 检查。
		const UCadenceArcGraph* Graph = MakeRealComboGraph();
		const FCadenceArcGraphLayout Plain = BuildGraphLayout(*Graph);
		for (const FCadenceArcLayoutEdge& Edge : Plain.Edges)
		{
			TestFalse(TEXT("References are off by default"), Edge.bIsReference);
		}
		for (const int32 MinSpan : {2, 3})
		{
			FCadenceArcLayoutParams Params;
			Params.ReferenceMinSpan = MinSpan;
			const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Params);
			for (int32 Index = 0; Index < Layout.Nodes.Num(); ++Index)
			{
				TestEqual(*FString::Printf(TEXT("Span %d: node %d keeps its column"), MinSpan, Index),
				          Layout.Nodes[Index].Column, Plain.Nodes[Index].Column);
			}
			for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
			{
				const FCadenceArcLayoutEdge& Edge = Layout.Edges[EdgeIndex];
				const int32 Span = FMath::Abs(Layout.Nodes[Edge.TargetNodeIndex].Column - Layout.Nodes[Edge.SourceNodeIndex].Column);
				TestTrue(*FString::Printf(TEXT("Span %d: edge %d is a reference exactly when it spans enough columns"), MinSpan, EdgeIndex),
				         Edge.bIsReference == (Span >= MinSpan));
			}
			ExpectLayoutInvariants(*this, Layout);
		}

		// 阈值 3：SkillA -> FinalB（e5）和 SkillC -> FinalC（e14）；跨 2 列的循环回边仍走底部通道
		FCadenceArcLayoutParams Three;
		Three.ReferenceMinSpan = 3;
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Three);
		TestTrue(TEXT("Span 3 references the two long finisher edges"),
		         Layout.Edges[5].bIsReference && Layout.Edges[14].bIsReference);
		TestEqual(TEXT("Span 3 keeps the loop back in a lane"), Layout.NumReturnLanes, 1);
		FCadenceArcLayoutParams Two;
		Two.ReferenceMinSpan = 2;
		TestEqual(TEXT("Span 2 turns the loop back into a reference"), BuildGraphLayout(*Graph, Two).NumReturnLanes, 0);

		// 命中测试：标记内部和目标一侧的接入线都算这条边
		const FCadenceArcLayoutEdge& Reference = Layout.Edges[5];
		TestEqual(TEXT("Point in the reference hits its edge"),
		          HitTestEdge(Layout, Reference.ReferenceBox.GetCenter(), 1.0), 5);
		TestEqual(TEXT("Point on the entry stub hits its edge"),
		          HitTestEdge(Layout, (Reference.EntryStub[0] + Reference.EntryStub.Last()) * 0.5, 1.0), 5);

		// 同一输入每次结果一致
		const FCadenceArcGraphLayout Again = BuildGraphLayout(*Graph, Three);
		for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
		{
			TestTrue(*FString::Printf(TEXT("Edge %d is deterministic"), EdgeIndex),
			         Layout.Edges[EdgeIndex].Path == Again.Edges[EdgeIndex].Path
			         && Layout.Edges[EdgeIndex].EntryStub == Again.Edges[EdgeIndex].EntryStub
			         && Layout.Edges[EdgeIndex].ReferenceBox == Again.Edges[EdgeIndex].ReferenceBox);
		}

		// 不可达节点在最后一列，它指回入口的边成为引用时，标记画在右侧留白里，仍在画布之内
		UCadenceArcGraph* Chain = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E()});
		AddLayoutEdge(Chain, 0, Layout_B());
		AddLayoutEdge(Chain, 1, Layout_C());
		AddLayoutEdge(Chain, 2, Layout_D());
		AddLayoutEdge(Chain, 4, Layout_A()); // E 不可达，放在第 4 列
		const FCadenceArcGraphLayout ChainLayout = BuildGraphLayout(*Chain, Three);
		TestTrue(TEXT("Unreachable node's edge back to the entry is a reference"), ChainLayout.Edges[3].bIsReference);
		ExpectLayoutInvariants(*this, ChainLayout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutEntrySpreadTest,
		"CadenceArc.Editor.Layout.SharedTargetEntriesSpread",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutEntrySpreadTest::RunTest(const FString& Parameters)
	{
		// FinalB 有三条入边（SkillA、SkillD、SkillE）：接入点各不相同，都在标题行内（由几何检查保证）
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*MakeRealComboGraph());
		constexpr int32 FinalB = 8;
		TArray<double> EntryYs;
		for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
		{
			if (Edge.TargetNodeIndex == FinalB)
			{
				EntryYs.Add(Edge.Path.Last().Y);
			}
		}
		if (TestEqual(TEXT("FinalB has three incoming edges"), EntryYs.Num(), 3))
		{
			EntryYs.Sort();
			TestTrue(TEXT("Entry points are distinct"), EntryYs[0] < EntryYs[1] && EntryYs[1] < EntryYs[2]);
		}
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutMixedHeightTest,
		"CadenceArc.Editor.Layout.MixedHeightsDoNotOverlap",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutMixedHeightTest::RunTest(const FString& Parameters)
	{
		// 同一列里一个 5 个端口的高节点夹在无端口节点之间，还有穿过这一列的长边通道：
		// 按各自实际高度压紧，互不重叠，连线也不压到它们
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 0, Layout_C());
		AddLayoutEdge(Graph, 0, Layout_D());
		AddLayoutEdge(Graph, 0, Layout_F(), Layout_InputHeavy()); // A -> F 跨两列
		for (int32 Port = 0; Port < 4; ++Port)
		{
			AddLayoutEdge(Graph, 2, Layout_E(), Port % 2 == 0 ? Layout_InputLight() : Layout_InputHeavy());
		}
		AddLayoutEdge(Graph, 2, Layout_F());
		AddLayoutEdge(Graph, 4, Layout_F());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);

		const FCadenceArcLayoutParams Params;
		TestEqual(TEXT("Tall node height follows its ports"), Layout.Nodes[2].Size.Y,
		          static_cast<double>(Params.HeaderHeight + 5 * Params.PortHeight + Params.NodeBottomPad));
		TestEqual(TEXT("Portless node is a bare title"), Layout.Nodes[1].Size.Y, static_cast<double>(Params.HeaderHeight));
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutFollowOffsetTest,
		"CadenceArc.Editor.Layout.FollowOffset",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutFollowOffsetTest::RunTest(const FString& Parameters)
	{
		constexpr double Visible = 500.0;
		constexpr double Margin = 40.0;
		constexpr double MaxOffset = 5000.0;
		// 整组已经完整可见：不滚动
		TestEqual(TEXT("Visible group stays"),
		          ComputeFollowOffset(100.0, Visible, 200.0, 300.0, 200.0, 450.0, Margin, MaxOffset), 100.0);
		// 整组放得下但在右侧视口外：只移到刚好露出整组
		TestEqual(TEXT("Group scrolls in minimally"),
		          ComputeFollowOffset(0.0, Visible, 600.0, 700.0, 600.0, 900.0, Margin, MaxOffset), 440.0);
		// 整组放不下：已提交节点必须完整可见，其余朝整组中心靠
		const double Wide = ComputeFollowOffset(0.0, Visible, 600.0, 700.0, 100.0, 1500.0, Margin, MaxOffset);
		TestEqual(TEXT("Too wide group centers within the primary's range"), Wide, 550.0);
		TestTrue(TEXT("Primary stays fully visible"), 600.0 - Margin >= Wide && 700.0 + Margin <= Wide + Visible);
		// 已提交节点本身比视口还大：对齐它的开头
		TestEqual(TEXT("Oversized primary aligns its start"),
		          ComputeFollowOffset(0.0, Visible, 300.0, 1000.0, 300.0, 1000.0, Margin, MaxOffset), 260.0);
		// 结果限制在可滚动范围内
		TestEqual(TEXT("Clamped to the end"),
		          ComputeFollowOffset(0.0, Visible, 600.0, 700.0, 600.0, 900.0, Margin, 200.0), 200.0);
		TestEqual(TEXT("Clamped to the start"),
		          ComputeFollowOffset(300.0, Visible, 10.0, 60.0, 10.0, 60.0, Margin, MaxOffset), 0.0);
		// 视口还没排布（尺寸为 0）：保持原样，等下一帧
		TestEqual(TEXT("Unmeasured viewport keeps offset"),
		          ComputeFollowOffset(123.0, 0.0, 600.0, 700.0, 600.0, 900.0, Margin, MaxOffset), 123.0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutFollowZoomTest,
		"CadenceArc.Editor.Layout.FollowZoom",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutFollowZoomTest::RunTest(const FString& Parameters)
	{
		constexpr double Margin = 40.0;
		constexpr double MinZoom = 0.6;
		// 1 倍放得下：不缩放（也不放大）
		TestEqual(TEXT("Fitting group keeps 1x"),
		          ComputeFollowZoom(1.0, 1000.0, 600.0, 800.0, 400.0, Margin, MinZoom), 1.0);
		// 放不下：缩到刚好放下，取更紧的那个方向
		TestEqual(TEXT("Wide group shrinks to fit"),
		          ComputeFollowZoom(1.0, 1000.0, 600.0, 1100.0, 300.0, Margin, MinZoom), 920.0 / 1100.0, 1.e-9);
		TestEqual(TEXT("Tall group shrinks to fit"),
		          ComputeFollowZoom(1.0, 1000.0, 600.0, 500.0, 800.0, Margin, MinZoom), 520.0 / 800.0, 1.e-9);
		// 太大的整组不会缩到读不清：停在下限
		TestEqual(TEXT("Huge group stops at the readable minimum"),
		          ComputeFollowZoom(1.0, 1000.0, 600.0, 3000.0, 300.0, Margin, MinZoom), MinZoom);
		// 视口还没排布：保持当前缩放
		TestEqual(TEXT("Unmeasured viewport keeps zoom"),
		          ComputeFollowZoom(0.8, 0.0, 600.0, 3000.0, 300.0, Margin, MinZoom), 0.8);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutOffscreenHintTest,
		"CadenceArc.Editor.Layout.OffscreenHint",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutOffscreenHintTest::RunTest(const FString& Parameters)
	{
		const FBox2D Viewport(FVector2D(0.0, 0.0), FVector2D(1000.0, 600.0));
		constexpr double Inset = 16.0;
		// 目标在视口内或部分可见：不提示
		TestFalse(TEXT("Visible target has no hint"),
		          ComputeOffscreenHint(Viewport, FBox2D(FVector2D(100.0, 100.0), FVector2D(280.0, 130.0)), Inset).IsSet());
		TestFalse(TEXT("Partly visible target has no hint"),
		          ComputeOffscreenHint(Viewport, FBox2D(FVector2D(990.0, 100.0), FVector2D(1170.0, 130.0)), Inset).IsSet());
		// 目标在右侧：提示贴在右边缘内侧，与目标同高，指向右
		const TOptional<FCadenceArcOffscreenHint> Right =
			ComputeOffscreenHint(Viewport, FBox2D(FVector2D(1200.0, 100.0), FVector2D(1380.0, 130.0)), Inset);
		if (TestTrue(TEXT("Right target has a hint"), Right.IsSet()))
		{
			TestTrue(TEXT("Right hint anchor"), Right->Anchor.Equals(FVector2D(984.0, 115.0), 1.e-9));
			TestTrue(TEXT("Right hint points right"), Right->Direction.Equals(FVector2D(1.0, 0.0), 1.e-9));
		}
		// 目标在左下方：提示夹在左下角内侧，方向指向目标中心
		const TOptional<FCadenceArcOffscreenHint> LowerLeft =
			ComputeOffscreenHint(Viewport, FBox2D(FVector2D(-300.0, 800.0), FVector2D(-120.0, 830.0)), Inset);
		if (TestTrue(TEXT("Lower-left target has a hint"), LowerLeft.IsSet()))
		{
			TestTrue(TEXT("Lower-left hint anchor"), LowerLeft->Anchor.Equals(FVector2D(16.0, 584.0), 1.e-9));
			TestTrue(TEXT("Lower-left hint points down-left"),
			         LowerLeft->Direction.X < 0.0 && LowerLeft->Direction.Y > 0.0
			         && FMath::IsNearlyEqual(LowerLeft->Direction.Size(), 1.0, 1.e-9));
		}
		return !HasAnyErrors();
	}

	// 测试图中的显示名为 Root/A/B/C/D/E；Tag 只充当稳定标识，不借用名字推断拓扑。
	static UCadenceArcGraph* MakeCompactChainGraph()
	{
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(),
			{Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 0, Layout_F(), Layout_InputHeavy());
		AddLayoutEdge(Graph, 1, Layout_C());
		AddLayoutEdge(Graph, 2, Layout_D());
		AddLayoutEdge(Graph, 3, Layout_E());
		AddLayoutEdge(Graph, 5, Layout_E());
		return Graph;
	}

	static void ExpectSameLayoutGeometry(FAutomationTestBase& Test,
		const FCadenceArcGraphLayout& A, const FCadenceArcGraphLayout& B)
	{
		Test.TestTrue(TEXT("Layout dimensions are identical"), A.Size == B.Size
			&& A.NumColumns == B.NumColumns && A.MaxRows == B.MaxRows && A.NumReturnLanes == B.NumReturnLanes);
		if (Test.TestEqual(TEXT("Node count is identical"), A.Nodes.Num(), B.Nodes.Num()))
		{
			for (int32 Index = 0; Index < A.Nodes.Num(); ++Index)
			{
				const auto& X = A.Nodes[Index];
				const auto& Y = B.Nodes[Index];
				Test.TestTrue(*FString::Printf(TEXT("Node %d snapshot is identical"), Index),
					X.NodeIndex == Y.NodeIndex && X.ActionTag == Y.ActionTag && X.Column == Y.Column
					&& X.Row == Y.Row && X.bReachable == Y.bReachable && X.NumPorts == Y.NumPorts
					&& X.Position == Y.Position && X.Size == Y.Size);
			}
		}
		if (Test.TestEqual(TEXT("Edge count is identical"), A.Edges.Num(), B.Edges.Num()))
		{
			for (int32 Index = 0; Index < A.Edges.Num(); ++Index)
			{
				const auto& X = A.Edges[Index];
				const auto& Y = B.Edges[Index];
				Test.TestTrue(*FString::Printf(TEXT("Edge %d geometry is identical point by point"), Index),
					X.SourceNodeIndex == Y.SourceNodeIndex && X.TargetNodeIndex == Y.TargetNodeIndex
					&& X.TransitionIndex == Y.TransitionIndex && X.bIsBackEdge == Y.bIsBackEdge
					&& X.ReturnLane == Y.ReturnLane && X.bIsReference == Y.bIsReference
					&& X.Path == Y.Path && X.EntryStub == Y.EntryStub && X.ReferenceBox == Y.ReferenceBox);
			}
		}
		if (Test.TestEqual(TEXT("Chain count is identical"), A.FoldedChains.Num(), B.FoldedChains.Num()))
		{
			for (int32 Index = 0; Index < A.FoldedChains.Num(); ++Index)
			{
				Test.TestTrue(TEXT("Chain order and bounds are identical"),
					A.FoldedChains[Index].NodeIndices == B.FoldedChains[Index].NodeIndices
					&& A.FoldedChains[Index].Bounds == B.FoldedChains[Index].Bounds);
			}
		}
	}

	static void ExpectCompactIdentity(FAutomationTestBase& Test, const UCadenceArcGraph& Graph,
		const FCadenceArcGraphLayout& Layout)
	{
		if (!Test.TestEqual(TEXT("Every original node remains visible"), Layout.Nodes.Num(), Graph.Nodes.Num()))
		{
			return;
		}
		int32 EdgeIndex = 0;
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			Test.TestTrue(TEXT("Node identity remains aligned with asset array"),
				Layout.Nodes[NodeIndex].NodeIndex == NodeIndex
				&& Layout.Nodes[NodeIndex].ActionTag == Graph.Nodes[NodeIndex].ActionTag);
			for (int32 TransitionIndex = 0; TransitionIndex < Graph.Nodes[NodeIndex].Transitions.Num(); ++TransitionIndex)
			{
				const FCadenceArcTransition& Transition = Graph.Nodes[NodeIndex].Transitions[TransitionIndex];
				const int32 Target = Graph.Nodes.IndexOfByPredicate([&](const auto& Node)
				{
					return Node.ActionTag == Transition.TargetActionTag;
				});
				ExpectEdge(Test, Layout, EdgeIndex, NodeIndex, TransitionIndex, Target);
				if (Layout.Edges.IsValidIndex(EdgeIndex))
				{
					Test.TestTrue(TEXT("Transition tags survive contraction"),
						Layout.Edges[EdgeIndex].Transition.InputTag == Transition.InputTag
						&& Layout.Edges[EdgeIndex].Transition.TargetActionTag == Transition.TargetActionTag);
				}
				++EdgeIndex;
			}
		}
		Test.TestEqual(TEXT("Every original transition remains an edge"), Layout.Edges.Num(), EdgeIndex);
		ExpectGeometry(Test, Layout);
		for (const auto& Chain : Layout.FoldedChains)
		{
			for (const auto& Edge : Layout.Edges)
			{
				if (!Chain.NodeIndices.Contains(Edge.SourceNodeIndex) || !Chain.NodeIndices.Contains(Edge.TargetNodeIndex))
				{
					continue;
				}
				for (const FVector2D& Point : Edge.Path)
				{
					Test.TestTrue(TEXT("Chain bounds contain the actual internal route"), Chain.Bounds.bIsValid
						&& Point.X >= Chain.Bounds.Min.X && Point.X <= Chain.Bounds.Max.X
						&& Point.Y >= Chain.Bounds.Min.Y && Point.Y <= Chain.Bounds.Max.Y);
				}
				for (const int32 Endpoint : {Edge.SourceNodeIndex, Edge.TargetNodeIndex})
				{
					const auto& Node = Layout.Nodes[Endpoint];
					// 边界上的端口与箭头终点合法；缩入矩形后检查真正进入节点内部的线段。
					const FBox2D Interior(Node.Position + FVector2D(0.1), Node.Position + Node.Size - FVector2D(0.1));
					for (int32 Point = 1; Point < Edge.Path.Num(); ++Point)
					{
						Test.TestFalse(TEXT("Internal route never re-enters an endpoint interior"),
							SegmentHitsBox(Edge.Path[Point - 1], Edge.Path[Point], Interior));
					}
				}
			}
		}
	}

	static double LayoutPathLength(const TArray<FVector2D>& Points)
	{
		double Length = 0.0;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			Length += (Points[Index] - Points[Index - 1]).Size();
		}
		return Length;
	}

	// 导出被测函数的真实点列和矩形；不在测试外重造一份相似示意图，便于人工检查走线。
	static bool SaveCompactLayoutSvg(const FCadenceArcGraphLayout& Layout, const TCHAR* Name)
	{
		FString Svg = FString::Printf(TEXT("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 %.3f %.3f\"><rect width=\"100%%\" height=\"100%%\" fill=\"#151c27\"/>\n"), Layout.Size.X, Layout.Size.Y);
		for (const auto& Chain : Layout.FoldedChains)
		{
			Svg += FString::Printf(TEXT("<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" fill=\"none\" stroke=\"#6b91bc\" stroke-dasharray=\"5 4\"/>\n"),
				Chain.Bounds.Min.X, Chain.Bounds.Min.Y, Chain.Bounds.GetSize().X, Chain.Bounds.GetSize().Y);
		}
		for (int32 Index = 0; Index < Layout.Edges.Num(); ++Index)
		{
			const auto& Edge = Layout.Edges[Index];
			Svg += FString::Printf(TEXT("<polyline id=\"edge-%d\" fill=\"none\" stroke=\"#8fc6de\" stroke-width=\"2\" points=\""), Index);
			for (const FVector2D& Point : Edge.Path)
			{
				Svg += FString::Printf(TEXT("%.3f,%.3f "), Point.X, Point.Y);
			}
			Svg += TEXT("\"/>\n");
		}
		const TCHAR* Labels[] = {TEXT("Root"), TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D"), TEXT("E")};
		for (const auto& Node : Layout.Nodes)
		{
			Svg += FString::Printf(TEXT("<rect x=\"%.3f\" y=\"%.3f\" width=\"%.3f\" height=\"%.3f\" rx=\"4\" fill=\"#27374c\" stroke=\"#a9bfd4\"/><text x=\"%.3f\" y=\"%.3f\" fill=\"white\" font-family=\"sans-serif\" font-size=\"14\">%s</text>\n"),
				Node.Position.X, Node.Position.Y, Node.Size.X, Node.Size.Y,
				Node.Position.X + 10.0, Node.Position.Y + 17.0, Labels[Node.NodeIndex]);
		}
		Svg += TEXT("</svg>\n");
		const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation/CompactChainLayout"));
		return IFileManager::Get().MakeDirectory(*Directory, true)
			&& FFileHelper::SaveStringToFile(Svg, *FPaths::Combine(Directory, Name));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcLayoutCompactCoreTest,
		"CadenceArc.Editor.Layout.CompactChains.ContractionAndGeometry",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCompactCoreTest::RunTest(const FString& Parameters)
	{
		const UCadenceArcGraph* Graph = MakeCompactChainGraph();
		const FCadenceArcGraphLayout Layered = BuildGraphLayout(*Graph);
		FCadenceArcLayoutParams Params;
		TestTrue(TEXT("Layered remains the default"), Params.Mode == ECadenceArcLayoutMode::Layered);
		Params.Mode = ECadenceArcLayoutMode::CompactChains;
		const FCadenceArcGraphLayout Compact = BuildGraphLayout(*Graph, Params);
		ExpectCompactIdentity(*this, *Graph, Compact);
		TestEqual(TEXT("Layered graph uses five columns"), Layered.NumColumns, 5);
		TestEqual(TEXT("Contracted graph uses three columns"), Compact.NumColumns, 3);
		TestEqual(TEXT("Layered has no folded groups"), Layered.FoldedChains.Num(), 0);
		if (TestEqual(TEXT("Exactly one maximal group"), Compact.FoldedChains.Num(), 1))
		{
			const auto& Chain = Compact.FoldedChains[0];
			TestTrue(TEXT("Group stores A, B, C in execution order"), Chain.NodeIndices == TArray<int32>({1, 2, 3}));
			for (const int32 Index : {1, 2, 3})
			{
				const auto& Node = Compact.Nodes[Index];
				TestEqual(TEXT("Chain nodes share display column one"), Node.Column, 1);
				TestTrue(TEXT("Group bounds enclose real node"), Chain.Bounds.bIsValid
					&& Chain.Bounds.Min.X <= Node.Position.X && Chain.Bounds.Min.Y <= Node.Position.Y
					&& Chain.Bounds.Max.X >= Node.Position.X + Node.Size.X
					&& Chain.Bounds.Max.Y >= Node.Position.Y + Node.Size.Y);
			}
		}
		TestTrue(TEXT("Input A is above B and output C"), Compact.Nodes[1].Position.Y < Compact.Nodes[2].Position.Y
			&& Compact.Nodes[2].Position.Y < Compact.Nodes[3].Position.Y);
		TestEqual(TEXT("Merge D ranks after the contracted chain"), Compact.Nodes[4].Column, 2);
		TestTrue(TEXT("Merge D moves left"), Compact.Nodes[4].Position.X < Layered.Nodes[4].Position.X);
		TestTrue(TEXT("E to D has shorter actual drawn path"), LayoutPathLength(Compact.Edges[5].Path) < LayoutPathLength(Layered.Edges[5].Path));
		for (const int32 Index : {2, 3})
		{
			ExpectRouting(*this, Compact, Index, false, INDEX_NONE);
			TestFalse(TEXT("Internal edge is not a reference"), Compact.Edges[Index].bIsReference);
			const auto& Edge = Compact.Edges[Index];
			const auto& Source = Compact.Nodes[Edge.SourceNodeIndex];
			const auto& Target = Compact.Nodes[Edge.TargetNodeIndex];
			for (const FVector2D& Point : Edge.Path)
			{
				TestTrue(TEXT("Internal edge stays in its local vertical interval"),
					Point.Y >= Source.Position.Y && Point.Y <= Target.Position.Y + Target.Size.Y);
			}
		}
		ExpectSameLayoutGeometry(*this, Compact, BuildGraphLayout(*Graph, Params));
		TestTrue(TEXT("Save tested layered SVG"), SaveCompactLayoutSvg(Layered, TEXT("layered.svg")));
		TestTrue(TEXT("Save tested compact SVG"), SaveCompactLayoutSvg(Compact, TEXT("compact.svg")));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcLayoutCompactReorderTest,
		"CadenceArc.Editor.Layout.CompactChains.ReorderedNodes",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCompactReorderTest::RunTest(const FString& Parameters)
	{
		UCadenceArcGraph* Graph = MakeCompactChainGraph();
		Graph->Nodes.Swap(1, 3);
		Graph->Nodes.Swap(0, 4);
		FCadenceArcLayoutParams Params;
		Params.Mode = ECadenceArcLayoutMode::CompactChains;
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Params);
		ExpectCompactIdentity(*this, *Graph, Layout);
		TestEqual(TEXT("Shuffling asset array preserves three display columns"), Layout.NumColumns, 3);
		if (TestEqual(TEXT("Shuffling preserves one chain"), Layout.FoldedChains.Num(), 1))
		{
			TestTrue(TEXT("Execution order wins over array order"), Layout.FoldedChains[0].NodeIndices == TArray<int32>({3, 2, 1}));
		}
		TestEqual(TEXT("Entry need not be node zero"), Layout.Nodes[4].Column, 0);
		ExpectSameLayoutGeometry(*this, Layout, BuildGraphLayout(*Graph, Params));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcLayoutCompactBarriersTest,
		"CadenceArc.Editor.Layout.CompactChains.EligibilityBarriersAndFallback",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCompactBarriersTest::RunTest(const FString& Parameters)
	{
		// 每种扰动都把三节点链切成至多一个合格节点；不能跨过障碍把它们重新拼起来。
		const TCHAR* Cases[] = {TEXT("parallel"), TEXT("broken"), TEXT("unreachable predecessor"),
			TEXT("back edge"), TEXT("self loop"), TEXT("missing entry"), TEXT("merge"), TEXT("split")};
		for (int32 Case = 0; Case < UE_ARRAY_COUNT(Cases); ++Case)
		{
			UCadenceArcGraph* Graph = MakeCompactChainGraph();
			switch (Case)
			{
			case 0: AddLayoutEdge(Graph, 1, Layout_C(), Layout_InputHeavy()); break;
			case 1: AddLayoutEdge(Graph, 2, Layout_Missing()); break;
			case 2:
				Graph->Nodes.AddDefaulted_GetRef().ActionTag = Layout_G();
				AddLayoutEdge(Graph, 6, Layout_C());
				break;
			case 3: AddLayoutEdge(Graph, 3, Layout_B()); break;
			case 4: AddLayoutEdge(Graph, 2, Layout_C()); break;
			case 5: Graph->EntryActionTag = Layout_Missing(); break;
			case 6: AddLayoutEdge(Graph, 0, Layout_C()); break;
			case 7: AddLayoutEdge(Graph, 2, Layout_F()); break;
			}
			for (const int32 ReferenceSpan : {0, 2})
			{
				FCadenceArcLayoutParams Params;
				Params.ReferenceMinSpan = ReferenceSpan;
				const FCadenceArcGraphLayout Layered = BuildGraphLayout(*Graph, Params);
				Params.Mode = ECadenceArcLayoutMode::CompactChains;
				const FCadenceArcGraphLayout Compact = BuildGraphLayout(*Graph, Params);
				TestEqual(*FString::Printf(TEXT("%s excludes chain formation (references %d)"), Cases[Case], ReferenceSpan), Compact.FoldedChains.Num(), 0);
				ExpectSameLayoutGeometry(*this, Layered, Compact);
				ExpectCompactIdentity(*this, *Graph, Compact);
			}
		}
		// 环的回边两端是障碍；中间纯前向的 B/C/D 仍然是可压缩的完整连续段。
		UCadenceArcGraph* Cycle = MakeLayoutGraph(Layout_A(),
			{Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F()});
		for (int32 Index = 0; Index < 5; ++Index)
		{
			AddLayoutEdge(Cycle, Index, Cycle->Nodes[Index + 1].ActionTag);
		}
		AddLayoutEdge(Cycle, 5, Layout_B());
		FCadenceArcLayoutParams Params;
		Params.Mode = ECadenceArcLayoutMode::CompactChains;
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Cycle, Params);
		if (TestEqual(TEXT("Forward interior of cycle forms one group"), Layout.FoldedChains.Num(), 1))
		{
			TestTrue(TEXT("Neither back-edge endpoint enters group"), Layout.FoldedChains[0].NodeIndices == TArray<int32>({2, 3, 4}));
		}
		ExpectRouting(*this, Layout, 5, true, 0);
		ExpectCompactIdentity(*this, *Cycle, Layout);
		ExpectSameLayoutGeometry(*this, Layout, BuildGraphLayout(*Cycle, Params));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcLayoutCompactBoundaryTest,
		"CadenceArc.Editor.Layout.CompactChains.MinimumAndMultipleGroups",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCompactBoundaryTest::RunTest(const FString& Parameters)
	{
		const TArray<FGameplayTag> Tags = {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F(), Layout_G()};
		FCadenceArcLayoutParams Params;
		Params.Mode = ECadenceArcLayoutMode::CompactChains;
		// 空图、单节点和仅有一个合格中间节点都不产生空分组；两节点链才开始收缩。
		for (const int32 Count : {0, 1, 3, 4})
		{
			TArray<FGameplayTag> NodeTags;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				NodeTags.Add(Tags[Index]);
			}
			UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), NodeTags);
			for (int32 Index = 0; Index + 1 < Count; ++Index)
			{
				AddLayoutEdge(Graph, Index, Tags[Index + 1]);
			}
			const FCadenceArcGraphLayout Compact = BuildGraphLayout(*Graph, Params);
			TestEqual(TEXT("Only at least two eligible nodes form a group"), Compact.FoldedChains.Num(), Count == 4 ? 1 : 0);
			if (Count < 4)
			{
				ExpectSameLayoutGeometry(*this, Compact, BuildGraphLayout(*Graph));
			}
			else if (Compact.FoldedChains.Num() == 1)
			{
				TestTrue(TEXT("Entry and terminal remain outside minimum chain"), Compact.FoldedChains[0].NodeIndices == TArray<int32>({1, 2}));
				TestEqual(TEXT("Minimum chain reduces columns by one"), Compact.NumColumns, 3);
			}
			ExpectCompactIdentity(*this, *Graph, Compact);
		}
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), Tags);
		for (int32 Index = 0; Index < 6; ++Index)
		{
			AddLayoutEdge(Graph, Index, Tags[Index + 1]);
		}
		AddLayoutEdge(Graph, 3, Layout_G(), Layout_InputHeavy());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Params);
		if (TestEqual(TEXT("Split separates two independent groups"), Layout.FoldedChains.Num(), 2))
		{
			TestTrue(TEXT("First group precedes split"), Layout.FoldedChains[0].NodeIndices == TArray<int32>({1, 2}));
			TestTrue(TEXT("Second group follows split"), Layout.FoldedChains[1].NodeIndices == TArray<int32>({4, 5}));
		}
		TestEqual(TEXT("Contracted longest path has five columns"), Layout.NumColumns, 5);
		TestEqual(TEXT("Split remains its own display column"), Layout.Nodes[3].Column, 2);
		TestEqual(TEXT("Merge follows the second group despite shortcut"), Layout.Nodes[6].Column, 4);
		ExpectCompactIdentity(*this, *Graph, Layout);
		ExpectSameLayoutGeometry(*this, Layout, BuildGraphLayout(*Graph, Params));
		// 整段从入口不可达，即使结构是直链也不能收缩。
		Graph->Nodes[0].Transitions.Reset();
		const FCadenceArcGraphLayout Disconnected = BuildGraphLayout(*Graph, Params);
		TestEqual(TEXT("Disconnected chains stay expanded"), Disconnected.FoldedChains.Num(), 0);
		ExpectSameLayoutGeometry(*this, Disconnected, BuildGraphLayout(*Graph));
		ExpectCompactIdentity(*this, *Graph, Disconnected);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcLayoutCompactReferencesTest,
		"CadenceArc.Editor.Layout.CompactChains.ReferencesUseDisplayColumns",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutCompactReferencesTest::RunTest(const FString& Parameters)
	{
		UCadenceArcGraph* Graph = MakeCompactChainGraph();
		// Root -> D 在收缩后仍跨两列；E -> D 从三列缩为一列，必须恢复为普通边。
		AddLayoutEdge(Graph, 0, Layout_E());
		FCadenceArcLayoutParams Params;
		Params.ReferenceMinSpan = 2;
		const FCadenceArcGraphLayout Layered = BuildGraphLayout(*Graph, Params);
		TestTrue(TEXT("Layered E to D is a reference"), Layered.Edges.Last().bIsReference);
		Params.Mode = ECadenceArcLayoutMode::CompactChains;
		const FCadenceArcGraphLayout Compact = BuildGraphLayout(*Graph, Params);
		TestFalse(TEXT("Compact E to D no longer qualifies for reference"), Compact.Edges.Last().bIsReference);
		TestTrue(TEXT("Root to D remains a reference across two display columns"), Compact.Edges[2].bIsReference);
		for (const int32 Span : {0, 1, 2, 3})
		{
			Params.ReferenceMinSpan = Span;
			const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Params);
			TestEqual(TEXT("Reference setting preserves compact column count"), Layout.NumColumns, 3);
			TestEqual(TEXT("Reference setting preserves chain grouping"), Layout.FoldedChains.Num(), 1);
			for (const auto& Edge : Layout.Edges)
			{
				const int32 DisplaySpan = FMath::Abs(Layout.Nodes[Edge.TargetNodeIndex].Column - Layout.Nodes[Edge.SourceNodeIndex].Column);
				TestTrue(TEXT("Reference decision uses display columns"), Edge.bIsReference == (Span > 0 && DisplaySpan >= Span));
				if (DisplaySpan == 0)
				{
					TestFalse(TEXT("Internal edges never become references"), Edge.bIsReference);
					TestEqual(TEXT("Internal edges never reserve return lanes"), Edge.ReturnLane, INDEX_NONE);
				}
			}
			ExpectCompactIdentity(*this, *Graph, Layout);
			ExpectSameLayoutGeometry(*this, Layout, BuildGraphLayout(*Graph, Params));
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutHitTestTest,
		"CadenceArc.Editor.Layout.HitTest",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutHitTestTest::RunTest(const FString& Parameters)
	{
		// A -> B：鼠标在节点上命中节点；在连线附近命中这条边；离得远什么都不命中
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B()});
		AddLayoutEdge(Graph, 0, Layout_B());
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);
		const FCadenceArcLayoutNode& B = Layout.Nodes[1];
		TestEqual(TEXT("Point inside B hits B"), HitTestNode(Layout, B.Position + B.Size * 0.5), 1);
		TestEqual(TEXT("Point far away hits no node"), HitTestNode(Layout, FVector2D(-100.0, -100.0)), INDEX_NONE);

		const TArray<FVector2D>& Path = Layout.Edges[0].Path;
		const FVector2D OnPath = (Path[Path.Num() / 2] + Path[Path.Num() / 2 + 1]) * 0.5;
		TestEqual(TEXT("Point on the drawn path hits the edge"), HitTestEdge(Layout, OnPath, 6.0), 0);
		TestEqual(TEXT("Point just within tolerance hits the edge"),
		          HitTestEdge(Layout, OnPath + FVector2D(0.0, 5.0), 6.0), 0);
		TestEqual(TEXT("Point outside tolerance misses"), HitTestEdge(Layout, OnPath + FVector2D(0.0, 60.0), 6.0),
		          INDEX_NONE);
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
