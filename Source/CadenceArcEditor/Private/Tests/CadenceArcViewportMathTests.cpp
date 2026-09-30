// 视口计算与命中测试：跟随滚动、跟随缩放、视口外提示、鼠标命中节点和连线。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcLayoutTestSupport.h"
#include "Layout/CadenceArcViewportMath.h"
#include "Layout/CadenceArcLayoutHitTest.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Editor::Tests
{
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
