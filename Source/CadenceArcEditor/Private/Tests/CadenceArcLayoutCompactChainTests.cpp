// 紧凑链模式（CompactChains）的测试：简单链的收缩与几何、节点乱序、不能收缩的情况、多组链、引用标记用显示列。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcLayoutTestSupport.h"
#include "Layout/CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace CadenceArc::Editor::Tests
{
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
}

#endif // WITH_DEV_AUTOMATION_TESTS
