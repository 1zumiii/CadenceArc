// 走线方式的测试：直角折线（EdgeStyle = Orthogonal）和端口重排（bSortPorts）。
//
// 被测契约：
// - 直角走线只改变中间形状：每条边的起点（端口）和终点（接入点）与曲线画法完全相同；
// - 圆角为 0 时每一小段都是水平或竖直的；同一空隙里不同边的竖线不会共线重叠；
// - 端口重排只改 PortSlot（显示行），边的数量、顺序、TransitionIndex 都不变；每个节点的行号仍是 0..N-1 的排列；
// - 重排后前向边在前，然后是引用边和坏目标，再是回边，自环在最下面；
// - 在真实形状的连招图上，重排后连线之间的交叉比不重排少；
// - 各种组合下几何仍然合法（ExpectLayoutInvariants）。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcLayoutTestSupport.h"
#include "Layout/CadenceArcGraphLayout.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Editor::Tests
{
	// 两条线段是否真正相交（端点落在另一条线上不算，共线不算）
	static bool SegmentsCross(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FVector2D& D)
	{
		const auto Orientation = [](const FVector2D& P, const FVector2D& Q, const FVector2D& R)
		{
			return FVector2D::CrossProduct(Q - P, R - P);
		};
		constexpr double Epsilon = 1.e-6;
		const double D1 = Orientation(C, D, A);
		const double D2 = Orientation(C, D, B);
		const double D3 = Orientation(A, B, C);
		const double D4 = Orientation(A, B, D);
		return ((D1 > Epsilon && D2 < -Epsilon) || (D1 < -Epsilon && D2 > Epsilon))
			&& ((D3 > Epsilon && D4 < -Epsilon) || (D3 < -Epsilon && D4 > Epsilon));
	}

	// 不同边的绘制路径之间的交叉次数
	static int32 CountEdgeCrossings(const FCadenceArcGraphLayout& Layout)
	{
		int32 Crossings = 0;
		for (int32 First = 0; First < Layout.Edges.Num(); ++First)
		{
			for (int32 Second = First + 1; Second < Layout.Edges.Num(); ++Second)
			{
				const TArray<FVector2D>& P = Layout.Edges[First].Path;
				const TArray<FVector2D>& Q = Layout.Edges[Second].Path;
				for (int32 I = 1; I < P.Num(); ++I)
				{
					for (int32 J = 1; J < Q.Num(); ++J)
					{
						Crossings += SegmentsCross(P[I - 1], P[I], Q[J - 1], Q[J]) ? 1 : 0;
					}
				}
			}
		}
		return Crossings;
	}

	// 普通前向边：目标在右侧的列，按曲线或直角折线画
	static bool IsPlainForward(const FCadenceArcGraphLayout& Layout, const FCadenceArcLayoutEdge& Edge)
	{
		return !Edge.IsBrokenTarget() && !Edge.bIsReference && Edge.ReturnLane == INDEX_NONE
			&& Layout.Nodes[Edge.TargetNodeIndex].Column > Layout.Nodes[Edge.SourceNodeIndex].Column;
	}

	// 一条长链 + 跨过它的长边 + 分支，紧凑模式下会有链可收
	static UCadenceArcGraph* MakeChainAndBranchGraph()
	{
		UCadenceArcGraph* Graph = MakeLayoutGraph(
			Layout_A(), {Layout_A(), Layout_B(), Layout_C(), Layout_D(), Layout_E(), Layout_F(), Layout_G()});
		AddLayoutEdge(Graph, 0, Layout_B());
		AddLayoutEdge(Graph, 0, Layout_E(), Layout_InputHeavy()); // 跨过整条链
		AddLayoutEdge(Graph, 0, Layout_F(), Layout_InputHeavy());
		AddLayoutEdge(Graph, 1, Layout_C());
		AddLayoutEdge(Graph, 2, Layout_D());
		AddLayoutEdge(Graph, 3, Layout_E());
		AddLayoutEdge(Graph, 5, Layout_G());
		AddLayoutEdge(Graph, 5, Layout_C(), Layout_InputHeavy()); // 从另一个分支汇入链中间
		return Graph;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutOrthogonalTest,
		"CadenceArc.Editor.Layout.OrthogonalEdges",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutOrthogonalTest::RunTest(const FString& Parameters)
	{
		const TArray<UCadenceArcGraph*> Graphs = {MakeRealComboGraph(), MakeChainAndBranchGraph()};
		for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
		{
			for (const ECadenceArcLayoutMode Mode : {ECadenceArcLayoutMode::Layered, ECadenceArcLayoutMode::CompactChains})
			{
				for (const bool bSortPorts : {false, true})
				{
					for (const int32 ReferenceMinSpan : {0, 3})
					{
						const FString What = FString::Printf(TEXT("Graph %d, %s, sort %d, references %d"), GraphIndex,
						                                     Mode == ECadenceArcLayoutMode::Layered ? TEXT("layered") : TEXT("compact"),
						                                     bSortPorts ? 1 : 0, ReferenceMinSpan);
						FCadenceArcLayoutParams Curved;
						Curved.Mode = Mode;
						Curved.bSortPorts = bSortPorts;
						Curved.ReferenceMinSpan = ReferenceMinSpan;
						FCadenceArcLayoutParams Orthogonal = Curved;
						Orthogonal.EdgeStyle = ECadenceArcEdgeStyle::Orthogonal;
						FCadenceArcLayoutParams Sharp = Orthogonal;
						Sharp.CornerRadius = 0.f;
						const FCadenceArcGraphLayout CurvedLayout = BuildGraphLayout(*Graphs[GraphIndex], Curved);
						const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graphs[GraphIndex], Orthogonal);
						const FCadenceArcGraphLayout SharpLayout = BuildGraphLayout(*Graphs[GraphIndex], Sharp);
						ExpectLayoutInvariants(*this, Layout);
						ExpectLayoutInvariants(*this, SharpLayout);

						for (int32 EdgeIndex = 0; EdgeIndex < Layout.Edges.Num(); ++EdgeIndex)
						{
							// 端点不变：只有中间的形状不同
							const TArray<FVector2D>& Path = Layout.Edges[EdgeIndex].Path;
							const TArray<FVector2D>& CurvedPath = CurvedLayout.Edges[EdgeIndex].Path;
							TestTrue(*FString::Printf(TEXT("%s: edge %d keeps its endpoints"), *What, EdgeIndex),
							         Path[0].Equals(CurvedPath[0], 1.e-9) && Path.Last().Equals(CurvedPath.Last(), 1.e-9));
							// 没有圆角时每一小段都是水平或竖直的
							const TArray<FVector2D>& SharpPath = SharpLayout.Edges[EdgeIndex].Path;
							bool bAxisAligned = true;
							for (int32 Point = 1; Point < SharpPath.Num(); ++Point)
							{
								const FVector2D Delta = SharpPath[Point] - SharpPath[Point - 1];
								bAxisAligned &= FMath::Abs(Delta.X) <= 0.51 || FMath::Abs(Delta.Y) <= 0.51;
							}
							TestTrue(*FString::Printf(TEXT("%s: edge %d is axis-aligned"), *What, EdgeIndex), bAxisAligned);
						}

						// 同一条竖线上不能有两条不同的前向边重叠
						struct FVertical
						{
							int32 EdgeIndex;
							double X;
							double MinY;
							double MaxY;
						};
						TArray<FVertical> Verticals;
						for (int32 EdgeIndex = 0; EdgeIndex < SharpLayout.Edges.Num(); ++EdgeIndex)
						{
							const FCadenceArcLayoutEdge& Edge = SharpLayout.Edges[EdgeIndex];
							if (!IsPlainForward(SharpLayout, Edge))
							{
								continue;
							}
							for (int32 Point = 1; Point < Edge.Path.Num(); ++Point)
							{
								const FVector2D& A = Edge.Path[Point - 1];
								const FVector2D& B = Edge.Path[Point];
								if (FMath::Abs(A.X - B.X) <= 0.51 && FMath::Abs(A.Y - B.Y) > 0.51)
								{
									Verticals.Add({EdgeIndex, A.X, FMath::Min(A.Y, B.Y), FMath::Max(A.Y, B.Y)});
								}
							}
						}
						for (int32 First = 0; First < Verticals.Num(); ++First)
						{
							for (int32 Second = First + 1; Second < Verticals.Num(); ++Second)
							{
								const FVertical& A = Verticals[First];
								const FVertical& B = Verticals[Second];
								const bool bOverlap = A.EdgeIndex != B.EdgeIndex && FMath::Abs(A.X - B.X) < 1.0
									&& FMath::Min(A.MaxY, B.MaxY) - FMath::Max(A.MinY, B.MinY) > 1.0;
								TestFalse(*FString::Printf(TEXT("%s: edges %d and %d share a vertical track"), *What,
								                           A.EdgeIndex, B.EdgeIndex), bOverlap);
							}
						}
					}
				}
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLayoutSortedPortsTest,
		"CadenceArc.Editor.Layout.SortedPortsReduceCrossings",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLayoutSortedPortsTest::RunTest(const FString& Parameters)
	{
		// 不重排：显示行就是 Transition 顺序
		const UCadenceArcGraph* Combo = MakeRealComboGraph();
		const FCadenceArcGraphLayout Plain = BuildGraphLayout(*Combo);
		for (const FCadenceArcLayoutEdge& Edge : Plain.Edges)
		{
			TestEqual(TEXT("Without sorting the port row is the transition index"), Edge.PortSlot, Edge.TransitionIndex);
		}

		// 重排：边本身不变，只是行号变了；交叉变少（两种画法都是）
		for (const ECadenceArcEdgeStyle Style : {ECadenceArcEdgeStyle::Curved, ECadenceArcEdgeStyle::Orthogonal})
		{
			FCadenceArcLayoutParams Unsorted;
			Unsorted.EdgeStyle = Style;
			FCadenceArcLayoutParams Sorted = Unsorted;
			Sorted.bSortPorts = true;
			const FCadenceArcGraphLayout Before = BuildGraphLayout(*Combo, Unsorted);
			const FCadenceArcGraphLayout After = BuildGraphLayout(*Combo, Sorted);
			TestEqual(TEXT("Sorting keeps every edge"), After.Edges.Num(), Before.Edges.Num());
			for (int32 EdgeIndex = 0; EdgeIndex < After.Edges.Num(); ++EdgeIndex)
			{
				ExpectEdge(*this, After, EdgeIndex, Before.Edges[EdgeIndex].SourceNodeIndex,
				           Before.Edges[EdgeIndex].TransitionIndex, Before.Edges[EdgeIndex].TargetNodeIndex);
			}
			ExpectLayoutInvariants(*this, After);
			const int32 CrossingsBefore = CountEdgeCrossings(Before);
			const int32 CrossingsAfter = CountEdgeCrossings(After);
			AddInfo(FString::Printf(TEXT("%s crossings: %d unsorted, %d sorted"),
			                        Style == ECadenceArcEdgeStyle::Curved ? TEXT("Curved") : TEXT("Orthogonal"),
			                        CrossingsBefore, CrossingsAfter));
			TestTrue(TEXT("Sorting ports removes crossings on the combo graph"), CrossingsAfter < CrossingsBefore);
		}

		// 分组顺序：前向边在上，回边其次，自环最下面。B 的 Transition 顺序故意反过来写。
		UCadenceArcGraph* Graph = MakeLayoutGraph(Layout_A(), {Layout_A(), Layout_B(), Layout_C()});
		AddLayoutEdge(Graph, 0, Layout_B());                         // e0
		AddLayoutEdge(Graph, 1, Layout_B(), Layout_InputHeavy());    // e1 自环
		AddLayoutEdge(Graph, 1, Layout_A(), Layout_InputHeavy());    // e2 回边
		AddLayoutEdge(Graph, 1, Layout_C());                         // e3 前向
		FCadenceArcLayoutParams Sorted;
		Sorted.bSortPorts = true;
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph, Sorted);
		TestEqual(TEXT("Forward edge goes to the top row"), Layout.Edges[3].PortSlot, 0);
		TestEqual(TEXT("Back edge goes below forward edges"), Layout.Edges[2].PortSlot, 1);
		TestEqual(TEXT("Self loop goes to the bottom row"), Layout.Edges[1].PortSlot, 2);
		ExpectLayoutInvariants(*this, Layout);
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
