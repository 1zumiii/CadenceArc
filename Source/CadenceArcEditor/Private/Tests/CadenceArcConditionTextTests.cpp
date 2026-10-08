// 转移条件（Phase 8 / P8-07）在调试器里的文字：画布端口行的条件标注、历史 Tab 的选边上下文与失败说明。
//
// 被测契约：
// - 端口行在原有 "Heavy P" 后面接条件和优先级："+Forward -Air pause≥0.3s #2"；默认边（无条件、优先级 0）不加任何文字。
// - UnmetConditions 与运行时判断一致：Required 按层级 HasAll，Blocked 按层级 HasAny；没有停顿起点时停顿边一律不满足。
// - ConditionNotMet 有图时逐条列出候选边差在哪个条件，没图时只给上下文概要；AmbiguousTransition 列出打平的边。
// - 成功选边的摘要带上下文；只有选中的边看了停顿时才显示停顿时长。
//
// Editor 模块不能定义原生 Tag，这里按名字取运行时测试支持里定义的 Tag。

#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Tests/CadenceArcAutomationTags.h"
#include "ViewModel/CadenceArcConditionText.h"
#include "ViewModel/CadenceArcDebugEventText.h"
#include "Widgets/GraphView/CadenceArcCanvasDrawing.h"

namespace CadenceArc::Editor::Tests
{
	static FGameplayTag CondTextTag(const TCHAR* Name)
	{
		return CadenceArc::Tests::AutomationTag(Name); // 第一次请求时由 Runtime 模块注册
	}

	static FGameplayTag CondText_Light01() { return CondTextTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	static FGameplayTag CondText_Finisher01() { return CondTextTag(TEXT("CadenceArc.Automation.Action.Finisher01")); }
	static FGameplayTag CondText_Heavy01() { return CondTextTag(TEXT("CadenceArc.Automation.Action.Heavy01")); }
	static FGameplayTag CondText_Heavy() { return CondTextTag(TEXT("CadenceArc.Automation.Input.Heavy")); }
	static FGameplayTag CondText_Forward() { return CondTextTag(TEXT("CadenceArc.Automation.Context.Forward")); }
	static FGameplayTag CondText_Air() { return CondTextTag(TEXT("CadenceArc.Automation.Context.Air")); }
	static FGameplayTag CondText_AirJump() { return CondTextTag(TEXT("CadenceArc.Automation.Context.Air.Jump")); }

	static FCadenceArcTransition MakeCondTextEdge(const FGameplayTag& Target, const int32 Priority)
	{
		FCadenceArcTransition Edge;
		Edge.InputTag = CondText_Heavy();
		Edge.TargetActionTag = Target;
		Edge.Priority = Priority;
		return Edge;
	}

	static FCadenceArcHeldDurationRange MakeCondTextRange(const double Min, const TOptional<double> Max)
	{
		FCadenceArcHeldDurationRange Range;
		Range.MinHeldDurationSeconds = Min;
		Range.bHasMaxHeldDuration = Max.IsSet();
		Range.MaxHeldDurationSecondsExclusive = Max.Get(0.0);
		return Range;
	}

	// Light01 上两条 Heavy P 边：e0 需要 Forward → Finisher01，e1 停顿 ≥0.25s → Heavy01
	static UCadenceArcGraph* MakeCondTextGraph(const int32 ForwardPriority, const int32 PausePriority)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = CondText_Light01();
		FCadenceArcNode& Light01 = Graph->Nodes.AddDefaulted_GetRef();
		Light01.ActionTag = CondText_Light01();
		FCadenceArcTransition Forward = MakeCondTextEdge(CondText_Finisher01(), ForwardPriority);
		Forward.RequiredContextTags.AddTag(CondText_Forward());
		Light01.Transitions.Add(Forward);
		FCadenceArcTransition Pause = MakeCondTextEdge(CondText_Heavy01(), PausePriority);
		Pause.bUsePauseRange = true;
		Pause.PauseRange = MakeCondTextRange(0.25, {});
		Light01.Transitions.Add(Pause);
		return Graph;
	}

	static FCadenceArcDebugEvent MakeCondTextSubmit()
	{
		FCadenceArcDebugEvent Event;
		Event.Operation = ECadenceArcDebugOperation::SubmitInput;
		Event.CommittedBefore = CondText_Light01();
		Event.CommittedAfter = CondText_Light01();
		Event.InputTag = CondText_Heavy();
		Event.bHasResolutionContext = true;
		return Event;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionTextFormatTest,
		"CadenceArc.Editor.Conditions.Format",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionTextFormatTest::RunTest(const FString& Parameters)
	{
		namespace ConditionText = CadenceArc::Editor::ConditionText;
		// 默认边：标注和改动前完全一样
		const FCadenceArcTransition Plain = MakeCondTextEdge(CondText_Heavy01(), 0);
		TestTrue(TEXT("Default edge has no condition text"), ConditionText::FormatConditions(Plain).IsEmpty());
		TestEqual(TEXT("Default port label unchanged"), CanvasDrawing::FormatTransitionLabel(Plain),
		          FString(TEXT("Heavy P")));

		FCadenceArcTransition Full = MakeCondTextEdge(CondText_Finisher01(), 2);
		Full.RequiredContextTags.AddTag(CondText_Forward());
		Full.BlockedContextTags.AddTag(CondText_Air());
		Full.bUsePauseRange = true;
		Full.PauseRange = MakeCondTextRange(0.3, {});
		TestEqual(TEXT("All conditions"), ConditionText::FormatConditions(Full),
		          FString(TEXT("+Forward -Air pause≥0.3s #2")));
		TestEqual(TEXT("Port label carries conditions"), CanvasDrawing::FormatTransitionLabel(Full),
		          FString(TEXT("Heavy P +Forward -Air pause≥0.3s #2")));

		FCadenceArcTransition Negative = MakeCondTextEdge(CondText_Heavy01(), -1);
		TestEqual(TEXT("Negative priority is shown"), ConditionText::FormatConditions(Negative), FString(TEXT("#-1")));

		TestEqual(TEXT("Pause upper bound only"), ConditionText::FormatPauseRange(MakeCondTextRange(0.0, 0.25)),
		          FString(TEXT("pause<0.25s")));
		TestEqual(TEXT("Pause both bounds"), ConditionText::FormatPauseRange(MakeCondTextRange(0.1, 0.3)),
		          FString(TEXT("pause 0.1–0.3s")));

		TestEqual(TEXT("Empty context"), ConditionText::FormatContext(FGameplayTagContainer()), FString(TEXT("none")));
		FGameplayTagContainer Context;
		Context.AddTag(CondText_Forward());
		Context.AddTag(CondText_AirJump());
		TestEqual(TEXT("Context short names"), ConditionText::FormatContext(Context), FString(TEXT("Forward, Jump")));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionTextUnmetTest,
		"CadenceArc.Editor.Conditions.UnmetConditions",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionTextUnmetTest::RunTest(const FString& Parameters)
	{
		namespace ConditionText = CadenceArc::Editor::ConditionText;
		FCadenceArcTransition Edge = MakeCondTextEdge(CondText_Finisher01(), 0);
		Edge.RequiredContextTags.AddTag(CondText_Forward());
		Edge.BlockedContextTags.AddTag(CondText_Air());
		Edge.bUsePauseRange = true;
		Edge.PauseRange = MakeCondTextRange(0.3, {});

		// 子 Tag 也算命中父 Tag：Air.Jump 触发 "blocked by Air"
		FGameplayTagContainer Jumping(CondText_AirJump());
		TestEqual(TEXT("Every unmet condition listed"), FString::Join(ConditionText::UnmetConditions(Edge, Jumping, false, 0.0), TEXT(" | ")),
		          FString(TEXT("needs Forward | blocked by Air | no pause yet (nothing has finished)")));
		TestEqual(TEXT("Short pause names the bound"),
		          FString::Join(ConditionText::UnmetConditions(Edge, FGameplayTagContainer(CondText_Forward()), true, 0.12),
		                        TEXT(" | ")),
		          FString(TEXT("pause 0.12s, needs ≥0.3s")));
		TestTrue(TEXT("Satisfied edge has nothing unmet"),
		         ConditionText::UnmetConditions(Edge, FGameplayTagContainer(CondText_Forward()), true, 0.3).IsEmpty());

		// 父 Tag 作为 Required 时，上下文里的子 Tag 满足它
		FCadenceArcTransition NeedsAir = MakeCondTextEdge(CondText_Heavy01(), 0);
		NeedsAir.RequiredContextTags.AddTag(CondText_Air());
		TestTrue(TEXT("Child tag satisfies a parent requirement"),
		         ConditionText::UnmetConditions(NeedsAir, Jumping, false, 0.0).IsEmpty());
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionTextHistoryTest,
		"CadenceArc.Editor.Conditions.HistoryText",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionTextHistoryTest::RunTest(const FString& Parameters)
	{
		const UCadenceArcGraph* Graph = MakeCondTextGraph(1, 0);
		// 条件都不满足：有图时逐条说明，没图时只给上下文概要
		{
			FCadenceArcDebugEvent Event = MakeCondTextSubmit();
			Event.bFailed = true;
			Event.Category = ECadenceArcResolutionCategory::NoAction;
			Event.Reason = ECadenceArcResolutionReason::ConditionNotMet;
			Event.ContextTags.AddTag(CondText_Air());
			Event.bHasPauseDuration = true;
			Event.PauseDurationSeconds = 0.1;
			const FCadenceArcDebugEventText WithGraph = FormatDebugEvent(Event, Graph);
			TestEqual(TEXT("Unmet summary carries context"), WithGraph.Summary,
			          FString(TEXT("Heavy P [Air] ignored at Light01")));
			TestEqual(TEXT("Unmet detail per edge"), WithGraph.FailureDetail, FString(TEXT(
				          "No edge for Heavy P at Light01 fits: Finisher01 needs Forward; Heavy01 pause 0.10s, needs ≥0.25s. "
				          "Context: Air, pause 0.10s (ConditionNotMet)")));
			TestEqual(TEXT("Unmet detail without graph"), FormatDebugEvent(Event).FailureDetail, FString(TEXT(
				          "No edge for Heavy P at Light01 fits the conditions. Context: Air, pause 0.10s (ConditionNotMet)")));
		}
		// 最高优先级打平：列出打平的边（这种图校验会报错，调试器仍要能解释）
		{
			const UCadenceArcGraph* TiedGraph = MakeCondTextGraph(1, 1);
			FCadenceArcDebugEvent Event = MakeCondTextSubmit();
			Event.bFailed = true;
			Event.Category = ECadenceArcResolutionCategory::NoAction;
			Event.Reason = ECadenceArcResolutionReason::AmbiguousTransition;
			Event.ContextTags.AddTag(CondText_Forward());
			Event.bHasPauseDuration = true;
			Event.PauseDurationSeconds = 0.5;
			TestEqual(TEXT("Tie detail"), FormatDebugEvent(Event, TiedGraph).FailureDetail, FString(TEXT(
				          "Finisher01 and Heavy01 tie at priority 1 for Heavy P at Light01; raise one priority or make "
				          "their conditions exclusive. Context: Forward, pause 0.50s (AmbiguousTransition)")));
		}
		// 成功：选中的边看了停顿才显示停顿
		{
			FCadenceArcDebugEvent Event = MakeCondTextSubmit();
			Event.Category = ECadenceArcResolutionCategory::RequestProduced;
			Event.ProducedRequest.RequestId = 9;
			Event.ProducedRequest.TargetActionTag = CondText_Heavy01();
			Event.ContextTags.AddTag(CondText_Air());
			Event.bHasPauseDuration = true;
			Event.PauseDurationSeconds = 0.42;
			TestEqual(TEXT("Pause edge shows pause"), FormatDebugEvent(Event, Graph).Summary,
			          FString(TEXT("Heavy P [Air, pause 0.42s] at Light01 → Heavy01 (request #9)")));
			TestEqual(TEXT("Without graph the pause is not shown"), FormatDebugEvent(Event).Summary,
			          FString(TEXT("Heavy P [Air] at Light01 → Heavy01 (request #9)")));

			Event.ProducedRequest.TargetActionTag = CondText_Finisher01();
			Event.ContextTags = FGameplayTagContainer(CondText_Forward());
			TestEqual(TEXT("Context edge hides the pause"), FormatDebugEvent(Event, Graph).Summary,
			          FString(TEXT("Heavy P [Forward] at Light01 → Finisher01 (request #9)")));

			Event.ContextTags.Reset();
			Event.bHasResolutionContext = false;
			TestEqual(TEXT("No context, no note"), FormatDebugEvent(Event, Graph).Summary,
			          FString(TEXT("Heavy P at Light01 → Finisher01 (request #9)")));
		}
		// 完成时消费缓冲：记录里补了缓冲输入的名字
		{
			FCadenceArcDebugEvent Event = MakeCondTextSubmit();
			Event.Operation = ECadenceArcDebugOperation::ActionCompleted;
			Event.HandshakeResult = ECadenceArcHandshakeResult::Success;
			Event.Category = ECadenceArcResolutionCategory::RequestProduced;
			Event.ProducedRequest.RequestId = 4;
			Event.ProducedRequest.TargetActionTag = CondText_Finisher01();
			Event.ContextTags.AddTag(CondText_Forward());
			Event.bHasPauseDuration = true;
			TestEqual(TEXT("Buffered completion names the input"), FormatDebugEvent(Event, Graph).Summary,
			          FString(TEXT("Light01 finished, buffered Heavy P [Forward] → Finisher01 (request #4)")));
		}
		return !HasAnyErrors();
	}
}

#endif
