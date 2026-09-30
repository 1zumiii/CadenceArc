// 布局几何的测试：长边通道与光滑曲线、引用标记、同一目标的入边错开、不同高度节点不重叠。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcLayoutTestSupport.h"
#include "Layout/CadenceArcGraphLayout.h"
#include "Layout/CadenceArcLayoutHitTest.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Editor::Tests
{
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
