// 布局测试共用工具的定义，声明和说明见 CadenceArcLayoutTestSupport.h。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcLayoutTestSupport.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Editor::Tests
{
	FGameplayTag LayoutTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}

	FGameplayTag Layout_A() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Root")); }
	FGameplayTag Layout_B() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	FGameplayTag Layout_C() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Light02")); }
	FGameplayTag Layout_D() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Heavy01")); }
	FGameplayTag Layout_E() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Heavy02")); }
	FGameplayTag Layout_F() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher01")); }
	FGameplayTag Layout_G() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher02")); }
	// 有效但不作为任何节点出现的 Tag，用来制造坏目标和缺失入口
	FGameplayTag Layout_Missing() { return LayoutTag(TEXT("CadenceArc.Automation.Action.Finisher03")); }
	FGameplayTag Layout_InputLight() { return LayoutTag(TEXT("CadenceArc.Automation.Input.Light")); }
	FGameplayTag Layout_InputHeavy() { return LayoutTag(TEXT("CadenceArc.Automation.Input.Heavy")); }

	// 布局不做图校验，所以这里可以直接构造重复 Tag、坏目标等非法图。
	UCadenceArcGraph* MakeLayoutGraph(const FGameplayTag& Entry, const TArray<FGameplayTag>& NodeTags)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Entry;
		for (const FGameplayTag& Tag : NodeTags)
		{
			Graph->Nodes.AddDefaulted_GetRef().ActionTag = Tag;
		}
		return Graph;
	}

	void AddLayoutEdge(
		UCadenceArcGraph* Graph, const int32 SourceIndex, const FGameplayTag& Target,
		const FGameplayTag& Input)
	{
		FCadenceArcTransition& Transition = Graph->Nodes[SourceIndex].Transitions.AddDefaulted_GetRef();
		Transition.InputTag = Input;
		Transition.TargetActionTag = Target;
	}

	bool ExpectCell(
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

	bool ExpectEdge(
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

	bool ExpectExtent(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcGraphLayout& Layout,
		const int32 ExpectedColumns, const int32 ExpectedMaxRows)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s column count"), What),
		                              Layout.NumColumns, ExpectedColumns);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s max rows"), What), Layout.MaxRows, ExpectedMaxRows);
		return bPassed;
	}

	bool ExpectRouting(
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
	bool SegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
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
	void ExpectGeometry(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout)
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
				Params.NodeWidth, Params.HeaderHeight + (Edge.PortSlot + 0.5) * Params.PortHeight);
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
	void ExpectLayoutInvariants(FAutomationTestBase& Test, const FCadenceArcGraphLayout& Layout)
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
		// 端口行：每个节点的出边恰好占满 0 .. NumPorts-1 行，各占一行
		TArray<TArray<int32>> Slots;
		Slots.SetNum(Layout.Nodes.Num());
		for (const FCadenceArcLayoutEdge& Edge : Layout.Edges)
		{
			Slots[Edge.SourceNodeIndex].Add(Edge.PortSlot);
		}
		for (const FCadenceArcLayoutNode& Node : Layout.Nodes)
		{
			TArray<int32>& NodeSlots = Slots[Node.NodeIndex];
			NodeSlots.Sort();
			bool bPermutation = NodeSlots.Num() == Node.NumPorts;
			for (int32 Index = 0; Index < NodeSlots.Num() && bPermutation; ++Index)
			{
				bPermutation = NodeSlots[Index] == Index;
			}
			Test.TestTrue(*FString::Printf(TEXT("Node %d port rows are a permutation"), Node.NodeIndex), bPermutation);
		}
		ExpectGeometry(Test, Layout);
	}

	// 仿照 Sandbox 的 DA_TestComboGraphCombo（真实形状：分支树 + 共享终结技 + 一条循环回边）。
	// 布局只比较 Tag 是否相同，这里借用两个输入 Tag 充当第 9、10 个动作节点。
	UCadenceArcGraph* MakeRealComboGraph()
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
	double MaxTurnDegrees(const TArray<FVector2D>& Path)
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
