// Phase 8：转移条件、优先级与停顿区间的解析测试。
//
// 被测契约（P8-01 设计结论 D1～D6）：
// - 新字段取默认值时，解析结果与 Phase 7 完全相同；
// - 条件对“持久上下文 + 事件上下文”的并集求值，Required 用 HasAll、Blocked 用 HasAny，都按 Tag 层级匹配；
// - 先过滤，再取最高优先级；最高优先级唯一则选中，打平返回 Rejected / AmbiguousTransition；低优先级打平不影响结果；
// - 输入能对上但条件都不满足返回 NoAction / ConditionNotMet，与 NoMatchingTransition 区分；失败不改任何状态；
// - 停顿时长 = 输入时间 − 最近一次成功 Completed 的时间；缓冲的输入按 0；没有起点时带停顿区间的边不满足；
//   Reset、取消、打断清除起点，Rejected 保留起点；持久上下文任何时候都不被解析器清空；
// - 按住：松手用松手事件的上下文，自动松手（含超过截止时刻才到的物理松手）用按下事件的上下文；
//   停顿从按下时刻算，按下早于起点按 0，缓冲的松手按 0。

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/CadenceArcTestSupport.h"

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Resolver/CadenceArcResolver.h"
#include "Resolver/CadenceArcResolverTypes.h"

namespace CadenceArc::Tests
{
	// 名字都带 Cond 前缀：Unity Build 会把多个测试文件拼进同一个翻译单元，内部链接的同名 helper 会冲突。
	static FCadenceArcTransition CondEdge(
		const FGameplayTag& InputTag, const FGameplayTag& Target, const int32 Priority = 0,
		const FGameplayTagContainer& Required = {}, const FGameplayTagContainer& Blocked = {})
	{
		FCadenceArcTransition Edge;
		Edge.InputTag = InputTag;
		Edge.TargetActionTag = Target;
		Edge.Priority = Priority;
		Edge.RequiredContextTags = Required;
		Edge.BlockedContextTags = Blocked;
		return Edge;
	}

	static FCadenceArcTransition CondWithPause(
		FCadenceArcTransition Edge, const double MinSeconds, const bool bHasMax = false, const double MaxSeconds = 0.0)
	{
		Edge.bUsePauseRange = true;
		Edge.PauseRange.MinHeldDurationSeconds = MinSeconds;
		Edge.PauseRange.bHasMaxHeldDuration = bHasMax;
		Edge.PauseRange.MaxHeldDurationSecondsExclusive = MaxSeconds;
		return Edge;
	}

	static FCadenceArcTransition CondReleased(
		FCadenceArcTransition Edge, const double MinHeld, const bool bHasMax, const double MaxHeld = 0.0)
	{
		Edge.InputPhase = ECadenceArcInputPhase::Released;
		Edge.bUseDurationRange = true;
		Edge.DurationRange.MinHeldDurationSeconds = MinHeld;
		Edge.DurationRange.bHasMaxHeldDuration = bHasMax;
		Edge.DurationRange.MaxHeldDurationSecondsExclusive = MaxHeld;
		return Edge;
	}

	static FGameplayTagContainer CondTags(const FGameplayTag& Tag)
	{
		return FGameplayTagContainer(Tag);
	}

	static FCadenceArcInputEvent CondInput(
		const FGameplayTag& InputTag, const double TimestampSeconds, const FGameplayTagContainer& Context = {})
	{
		FCadenceArcInputEvent Event = MakeInput(InputTag, TimestampSeconds);
		Event.ContextTags = Context;
		return Event;
	}

	static FCadenceArcInputToken CondToken(const int64 PressId)
	{
		FCadenceArcInputToken Token;
		Token.SourceSession = FGuid(0x0C0D0E0F, 0x01020304, 0x05060708, 0x090A0B0C);
		Token.PressId = PressId;
		return Token;
	}

	static FCadenceArcInputEvent CondPress(
		const FGameplayTag& InputTag, const double TimestampSeconds, const FGameplayTagContainer& Context = {})
	{
		FCadenceArcInputEvent Event = CondInput(InputTag, TimestampSeconds, Context);
		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		return Event;
	}

	static FCadenceArcInputEvent CondRelease(
		const FGameplayTag& InputTag, const double PressedAt, const double ReleasedAt,
		const FGameplayTagContainer& Context = {})
	{
		FCadenceArcInputEvent Event = CondInput(InputTag, ReleasedAt, Context);
		Event.InputPhase = ECadenceArcInputPhase::Released;
		Event.HeldDurationSeconds = ReleasedAt - PressedAt;
		return Event;
	}

	// 只建一个只有若干终止节点的图：Root 的出边由调用方给出
	static UCadenceArcGraph* CondGraph(const TArray<FCadenceArcTransition>& RootEdges, const TArray<FGameplayTag>& Leaves)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		AddNode(Graph, Action_Root).Transitions = RootEdges;
		for (const FGameplayTag& Leaf : Leaves)
		{
			AddNode(Graph, Leaf);
		}
		return Graph;
	}

	static UCadenceArcResolver* CondResolver(FAutomationTestBase& Test, UCadenceArcGraph* Graph)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestInit(Test, TEXT("Condition graph initializes"), Resolver->Initialize(Graph),
		         ECadenceArcResolverInitResult::Success);
		return Resolver;
	}

	static bool CondExpectTarget(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcSubmitOutcome& Outcome, const FGameplayTag& Target)
	{
		bool bPassed = TestSubmit(Test, What, Outcome,
		                          ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s target"), What),
		                   Outcome.GetActionRequest().TargetActionTag, Target);
		return bPassed;
	}

	// 解析失败：结果符合预期，而且解析器状态完全没变
	static void CondExpectFailure(
		FAutomationTestBase& Test, const TCHAR* What, UCadenceArcResolver* Resolver, const FCadenceArcInputEvent& Event,
		const ECadenceArcResolutionCategory Category, const ECadenceArcResolutionReason Reason)
	{
		const FResolverSnapshot Before(Resolver);
		TestSubmit(Test, What, Resolver->SubmitInput(Event), Category, Reason);
		Before.ExpectUnchanged(Test, Resolver);
	}

	// ---------------------------------------------------------------- 默认值等价

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionDefaultsTest,
		"CadenceArc.Resolver.Condition.DefaultsMatchPreviousBehavior",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionDefaultsTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcTransition Default;
		TestEqual(TEXT("Default priority is 0"), Default.Priority, 0);
		TestTrue(TEXT("Default edge has no context condition"),
		         Default.RequiredContextTags.IsEmpty() && Default.BlockedContextTags.IsEmpty());
		TestFalse(TEXT("Default edge ignores pauses"), Default.bUsePauseRange);
		TestTrue(TEXT("Default input event has no context"), FCadenceArcInputEvent().ContextTags.IsEmpty());

		// 旧图：完成一个动作（产生停顿起点）之后，带任意上下文、任意停顿的输入，结果都与以前一样
		UCadenceArcResolver* Resolver = CondResolver(*this, MakeValidGraph());
		if (!ExecuteAndComplete(*this, Resolver, Input_Light, Action_Root, Action_Light01, TEXT("Old graph first step")))
		{
			return false;
		}
		Resolver->SetContextTags(CondTags(Context_Air));
		CondExpectTarget(*this, TEXT("Old graph ignores context and pause"),
		                 Resolver->SubmitInput(CondInput(Input_Heavy, 5.0, CondTags(Context_Forward))), Action_Finisher01);
		return !HasAnyErrors();
	}

	// ---------------------------------------------------------------- 上下文与优先级

	// P8-01 的贯穿例子（优先级按 D4 调整为互不相同）：
	// ② 空中 3 → Heavy02；① 前 2 → Heavy01；④ 停顿 ≥ 0.25s 1 → Finisher02；③ 无条件 0 → Finisher01
	static UCadenceArcGraph* CondExampleGraph()
	{
		return CondGraph({
			                 CondEdge(Input_Heavy, Action_Heavy01, 2, CondTags(Context_Forward)),
			                 CondEdge(Input_Heavy, Action_Heavy02, 3, CondTags(Context_Air)),
			                 CondEdge(Input_Heavy, Action_Finisher01, 0),
			                 CondWithPause(CondEdge(Input_Heavy, Action_Finisher02, 1), 0.25),
		                 }, {Action_Heavy01, Action_Heavy02, Action_Finisher01, Action_Finisher02});
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionContextPriorityTest,
		"CadenceArc.Resolver.Condition.ContextAndPriority",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionContextPriorityTest::RunTest(const FString& Parameters)
	{
		// 刚初始化没有停顿起点：④ 不参与
		CondExpectTarget(*this, TEXT("No context selects the unconditional edge"),
		                 CondResolver(*this, CondExampleGraph())->SubmitInput(CondInput(Input_Heavy, 1.0)),
		                 Action_Finisher01);
		CondExpectTarget(*this, TEXT("Event context Forward selects the forward edge"),
		                 CondResolver(*this, CondExampleGraph())->SubmitInput(
			                 CondInput(Input_Heavy, 1.0, CondTags(Context_Forward))), Action_Heavy01);
		{
			// 持久上下文与事件上下文取并集：空中（3）高于前（2）
			UCadenceArcResolver* Resolver = CondResolver(*this, CondExampleGraph());
			Resolver->SetContextTags(CondTags(Context_Air));
			CondExpectTarget(*this, TEXT("Persistent Air outranks event Forward"),
			                 Resolver->SubmitInput(CondInput(Input_Heavy, 1.0, CondTags(Context_Forward))), Action_Heavy02);
		}
		{
			// Required 按层级匹配：上下文里的子 Tag 满足对父 Tag 的要求
			UCadenceArcResolver* Resolver = CondResolver(*this, CondExampleGraph());
			Resolver->SetContextTags(CondTags(Context_AirJump));
			CondExpectTarget(*this, TEXT("Child tag satisfies a required parent tag"),
			                 Resolver->SubmitInput(CondInput(Input_Heavy, 1.0)), Action_Heavy02);
		}
		{
			// SetContextTags 只换上下文；Reset 不清空它
			UCadenceArcResolver* Resolver = CondResolver(*this, CondExampleGraph());
			const FResolverSnapshot Before(Resolver);
			Resolver->SetContextTags(CondTags(Context_Air));
			Before.ExpectUnchanged(*this, Resolver);
			TestTrue(TEXT("Context tags read back"), Resolver->GetContextTags().HasTagExact(Context_Air));
			TestEqual(TEXT("Reset succeeds"), static_cast<int32>(Resolver->Reset()),
			          static_cast<int32>(ECadenceArcResolverResetResult::Success));
			TestTrue(TEXT("Reset keeps persistent context"), Resolver->GetContextTags().HasTagExact(Context_Air));
			TestTrue(TEXT("Initialize keeps persistent context"),
			         Resolver->Initialize(CondExampleGraph()) == ECadenceArcResolverInitResult::Success
			         && Resolver->GetContextTags().HasTagExact(Context_Air));
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionNotMetTest,
		"CadenceArc.Resolver.Condition.ConditionNotMetAndBlocked",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionNotMetTest::RunTest(const FString& Parameters)
	{
		// Light 只能“前 + Light”；Heavy 在空中被禁止
		const auto MakeGraph = []()
		{
			return CondGraph({
				                 CondEdge(Input_Light, Action_Light01, 0, CondTags(Context_Forward)),
				                 CondEdge(Input_Heavy, Action_Heavy01, 0, {}, CondTags(Context_Air)),
			                 }, {Action_Light01, Action_Heavy01});
		};
		UCadenceArcResolver* Resolver = CondResolver(*this, MakeGraph());
		CondExpectFailure(*this, TEXT("Missing required context"), Resolver, CondInput(Input_Light, 1.0),
		                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		Resolver->SetContextTags(CondTags(Context_AirJump));
		CondExpectFailure(*this, TEXT("Child tag hits a blocked parent tag"), Resolver, CondInput(Input_Heavy, 1.1),
		                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		// 这个节点上根本没有这个输入的边：仍是 NoMatchingTransition
		CondExpectFailure(*this, TEXT("Input without any edge"), Resolver, CondInput(Context_Forward, 1.2),
		                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::NoMatchingTransition);
		Resolver->SetContextTags({});
		CondExpectTarget(*this, TEXT("Blocked edge passes once Air is gone"),
		                 Resolver->SubmitInput(CondInput(Input_Heavy, 1.3)), Action_Heavy01);
		CondExpectTarget(*this, TEXT("Required edge passes with Forward"),
		                 CondResolver(*this, MakeGraph())->SubmitInput(CondInput(Input_Light, 1.0, CondTags(Context_Forward))),
		                 Action_Light01);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionAmbiguityTest,
		"CadenceArc.Resolver.Condition.RuntimeAmbiguity",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionAmbiguityTest::RunTest(const FString& Parameters)
	{
		// 合法的图初始化之后被改：校验拦不住，运行时打平就拒绝，不按数组顺序猜
		UCadenceArcGraph* Graph = CondGraph({
			                                    CondEdge(Input_Heavy, Action_Heavy01, 1, CondTags(Context_Forward)),
			                                    CondEdge(Input_Heavy, Action_Finisher01, 0),
		                                    }, {Action_Heavy01, Action_Heavy02, Action_Finisher01});
		UCadenceArcResolver* Resolver = CondResolver(*this, Graph);

		// 低优先级打平不影响结果：再加一条优先级 0 的无条件边，按着前仍然选优先级 1 的边
		Graph->Nodes[0].Transitions.Add(CondEdge(Input_Heavy, Action_Heavy02, 0));
		CondExpectTarget(*this, TEXT("A tie below the top priority is ignored"),
		                 Resolver->SubmitInput(CondInput(Input_Heavy, 1.0, CondTags(Context_Forward))), Action_Heavy01);

		Resolver = CondResolver(*this, Graph = CondGraph({
			                                                 CondEdge(Input_Heavy, Action_Heavy01, 1, CondTags(Context_Forward)),
			                                                 CondEdge(Input_Heavy, Action_Finisher01, 0),
		                                                 }, {Action_Heavy01, Action_Finisher01}));
		Graph->Nodes[0].Transitions[0].Priority = 0;
		CondExpectFailure(*this, TEXT("Top priority tie is rejected"), Resolver,
		                  CondInput(Input_Heavy, 1.0, CondTags(Context_Forward)),
		                  ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::AmbiguousTransition);
		return !HasAnyErrors();
	}

	// ---------------------------------------------------------------- 停顿区间

	// Root --Light--> Light01；Root 上还有一条停顿 [0, ∞) 的 Heavy 边，用来观察“有没有停顿起点”。
	// Light01：Light 无条件 0 → Light02；Light 停顿 ≥ 0.25 优先级 1 → Finisher01；
	//          Heavy 停顿 [0, 0.25) → Heavy01；Heavy 停顿 [0.25, ∞) → Heavy02（同优先级、区间互补）。
	static UCadenceArcGraph* CondPauseGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(8);
		FCadenceArcNode& Root = AddNode(Graph, Action_Root);
		Root.Transitions.Add(CondEdge(Input_Light, Action_Light01));
		Root.Transitions.Add(CondWithPause(CondEdge(Input_Heavy, Action_Finisher02), 0.0));
		FCadenceArcNode& Light01 = AddNode(Graph, Action_Light01);
		Light01.Transitions.Add(CondEdge(Input_Light, Action_Light02));
		Light01.Transitions.Add(CondWithPause(CondEdge(Input_Light, Action_Finisher01, 1), 0.25));
		Light01.Transitions.Add(CondWithPause(CondEdge(Input_Heavy, Action_Heavy01), 0.0, true, 0.25));
		Light01.Transitions.Add(CondWithPause(CondEdge(Input_Heavy, Action_Heavy02), 0.25));
		for (const FGameplayTag& Leaf : TArray<FGameplayTag>{
			     Action_Light02, Action_Finisher01, Action_Finisher02, Action_Heavy01, Action_Heavy02
		     })
		{
			AddNode(Graph, Leaf);
		}
		return Graph;
	}

	// Root 按 Light 进入 Light01，在 CompletedAt 完成：停顿起点 = CompletedAt
	static UCadenceArcResolver* CondLight01Completed(FAutomationTestBase& Test, const double CompletedAt = 1.0)
	{
		UCadenceArcResolver* Resolver = CondResolver(Test, CondPauseGraph());
		const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(CondInput(Input_Light, 0.5));
		Resolver->NotifyActionStarted(Submit.GetActionRequest().RequestId);
		CompleteAt(Test, Resolver, Submit.GetActionRequest().RequestId, CompletedAt);
		TestTag(Test, TEXT("Setup reaches Light01"), Resolver->GetCurrentActionTag(), Action_Light01);
		return Resolver;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionPauseTest,
		"CadenceArc.Resolver.Condition.PauseRange",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionPauseTest::RunTest(const FString& Parameters)
	{
		// 停顿 = 输入时间 − 完成时间；下限包含，上限不包含
		CondExpectTarget(*this, TEXT("Short pause takes the plain edge"),
		                 CondLight01Completed(*this)->SubmitInput(CondInput(Input_Light, 1.2)), Action_Light02);
		CondExpectTarget(*this, TEXT("Pause exactly at the minimum takes the pause edge"),
		                 CondLight01Completed(*this)->SubmitInput(CondInput(Input_Light, 1.25)), Action_Finisher01);
		CondExpectTarget(*this, TEXT("Just below the upper bound stays in the short range"),
		                 CondLight01Completed(*this)->SubmitInput(CondInput(Input_Heavy, 1.2499)), Action_Heavy01);
		CondExpectTarget(*this, TEXT("Upper bound is exclusive"),
		                 CondLight01Completed(*this)->SubmitInput(CondInput(Input_Heavy, 1.25)), Action_Heavy02);

		// 缓冲的输入按停顿 0 解析：不走停顿边，但能走包含 0 的停顿区间
		{
			// 在 Light01 执行中缓冲：完成时停顿按 0，“前一个动作里按的”不会被当成停顿
			UCadenceArcResolver* Resolver = CondResolver(*this, CondPauseGraph());
			const FCadenceArcSubmitOutcome First = Resolver->SubmitInput(CondInput(Input_Light, 0.5));
			const int64 FirstId = First.GetActionRequest().RequestId;
			Resolver->NotifyActionStarted(FirstId);
			Resolver->OpenBufferWindow(FirstId);
			Resolver->SubmitInput(CondInput(Input_Heavy, 0.8));
			const FCadenceArcActionCompletionOutcome Completion = CompleteAt(*this, Resolver, FirstId, 5.0);
			TestBufferConsumption(*this, TEXT("Buffered Heavy resolves at completion"), Completion,
			                      ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
			TestTag(*this, TEXT("Buffered input takes the range containing 0"),
			        Completion.GetNextActionRequest().TargetActionTag, Action_Heavy01);
		}
		{
			UCadenceArcResolver* Resolver = CondResolver(*this, CondPauseGraph());
			const FCadenceArcSubmitOutcome First = Resolver->SubmitInput(CondInput(Input_Light, 0.5));
			const int64 FirstId = First.GetActionRequest().RequestId;
			Resolver->NotifyActionStarted(FirstId);
			Resolver->OpenBufferWindow(FirstId);
			Resolver->SubmitInput(CondInput(Input_Light, 0.8));
			const FCadenceArcActionCompletionOutcome Completion = CompleteAt(*this, Resolver, FirstId, 5.0);
			TestTag(*this, TEXT("Buffered input never takes a pause edge"),
			        Completion.GetNextActionRequest().TargetActionTag, Action_Light02);
		}

		// 没有停顿起点：带停顿区间的边不满足，即使区间是 [0, ∞)
		CondExpectFailure(*this, TEXT("No completion yet"), CondResolver(*this, CondPauseGraph()),
		                  CondInput(Input_Heavy, 3.0),
		                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		{
			UCadenceArcResolver* Resolver = CondLight01Completed(*this);
			Resolver->Reset();
			CondExpectFailure(*this, TEXT("Reset clears the pause anchor"), Resolver, CondInput(Input_Heavy, 3.0),
			                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		}
		{
			UCadenceArcResolver* Resolver = CondLight01Completed(*this);
			const FCadenceArcSubmitOutcome Next = Resolver->SubmitInput(CondInput(Input_Light, 1.1));
			Resolver->NotifyActionStarted(Next.GetActionRequest().RequestId);
			Resolver->NotifyActionCancelled(Next.GetActionRequest().RequestId);
			CondExpectFailure(*this, TEXT("Cancel clears the pause anchor"), Resolver, CondInput(Input_Heavy, 3.0),
			                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		}
		{
			UCadenceArcResolver* Resolver = CondLight01Completed(*this);
			const FCadenceArcSubmitOutcome Next = Resolver->SubmitInput(CondInput(Input_Light, 1.1));
			Resolver->NotifyActionStarted(Next.GetActionRequest().RequestId);
			Resolver->NotifyActionInterrupted(Next.GetActionRequest().RequestId);
			CondExpectFailure(*this, TEXT("Interrupt clears the pause anchor"), Resolver, CondInput(Input_Heavy, 3.0),
			                  ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		}
		{
			// 执行器拒绝：仍停在 Light01，起点保留
			UCadenceArcResolver* Resolver = CondLight01Completed(*this);
			const FCadenceArcSubmitOutcome Next = Resolver->SubmitInput(CondInput(Input_Light, 1.1));
			Resolver->NotifyActionRejected(Next.GetActionRequest().RequestId);
			CondExpectTarget(*this, TEXT("Rejected keeps the pause anchor"),
			                 Resolver->SubmitInput(CondInput(Input_Light, 1.5)), Action_Finisher01);
		}
		return !HasAnyErrors();
	}

	// ---------------------------------------------------------------- 按住

	// Root：Heavy 松手 [0, 0.5) → Light01；[0.5, ∞) 且前 优先级 1 → Heavy01；[0.5, ∞) 优先级 0 → Heavy02。
	// 蓄力配置：0.2s 开始蓄力，满蓄后再保持 0.5s 自动松手（按下后 1.0s）。
	static UCadenceArcGraph* CondChargeGraph()
	{
		UCadenceArcGraph* Graph = CondGraph({
			                                    CondReleased(CondEdge(Input_Heavy, Action_Light01), 0.0, true, 0.5),
			                                    CondReleased(CondEdge(Input_Heavy, Action_Heavy01, 1, CondTags(Context_Forward)), 0.5, false),
			                                    CondReleased(CondEdge(Input_Heavy, Action_Heavy02), 0.5, false),
		                                    }, {Action_Light01, Action_Heavy01, Action_Heavy02});
		FCadenceArcHoldChargeConfig& Config = Graph->Nodes[0].HoldChargeConfigs.AddDefaulted_GetRef();
		Config.InputTag = Input_Heavy;
		Config.ChargeStartSeconds = 0.2;
		Config.MaxChargedHoldSeconds = 0.5;
		return Graph;
	}

	static bool CondGrant(FAutomationTestBase& Test, UCadenceArcResolver* Resolver, const FCadenceArcInputToken& Token,
	                      const FCadenceArcInputEvent& Press)
	{
		const FCadenceArcHoldOutcome Outcome = Resolver->BeginInputHold(Token, Press);
		return Test.TestEqual(TEXT("Hold is granted"), static_cast<int32>(Outcome.GetResult()),
		                      static_cast<int32>(ECadenceArcHoldResult::Granted));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionHoldContextTest,
		"CadenceArc.Resolver.Condition.HoldContext",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionHoldContextTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = CondToken(1);
		{
			// 授予时不看条件；手动松手用松手事件的上下文
			UCadenceArcResolver* Resolver = CondResolver(*this, CondChargeGraph());
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.0))) { return false; }
			Resolver->AdvanceInputTime(1.8);
			CondExpectTarget(*this, TEXT("Release context Forward selects the forward tier"),
			                 Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 1.0, 1.8, CondTags(Context_Forward)))
			                 .GetResolution(), Action_Heavy01);
		}
		{
			UCadenceArcResolver* Resolver = CondResolver(*this, CondChargeGraph());
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.0, CondTags(Context_Forward)))) { return false; }
			Resolver->AdvanceInputTime(1.8);
			CondExpectTarget(*this, TEXT("Manual release ignores the press context"),
			                 Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 1.0, 1.8)).GetResolution(),
			                 Action_Heavy02);
		}
		{
			// 自动松手没有松手事件：用按下时的上下文
			UCadenceArcResolver* Resolver = CondResolver(*this, CondChargeGraph());
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.0, CondTags(Context_Forward)))) { return false; }
			const FCadenceArcInputAdvanceOutcome Advance = Resolver->AdvanceInputTime(2.1);
			TestEqual(TEXT("Advance releases automatically"), static_cast<int32>(Advance.GetReleaseSource()),
			          static_cast<int32>(ECadenceArcInputReleaseSource::HoldLimit));
			CondExpectTarget(*this, TEXT("Automatic release keeps the press context"), Advance.GetResolution(), Action_Heavy01);
		}
		{
			// 截止时刻之后才到的物理松手也按自动松手处理：用按下时的上下文，不用较晚的松手上下文
			UCadenceArcResolver* Resolver = CondResolver(*this, CondChargeGraph());
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.0, CondTags(Context_Forward)))) { return false; }
			const FCadenceArcInputAdvanceOutcome Late =
				Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 1.0, 2.5));
			TestEqual(TEXT("Late release counts as the hold limit"), static_cast<int32>(Late.GetReleaseSource()),
			          static_cast<int32>(ECadenceArcInputReleaseSource::HoldLimit));
			CondExpectTarget(*this, TEXT("Late release keeps the press context"), Late.GetResolution(), Action_Heavy01);
		}
		return !HasAnyErrors();
	}

	// Root --Light--> Light01。Light01：Heavy 松手 停顿 ≥ 0.25 优先级 1 → Finisher01；Heavy 松手 无条件 → Light02。
	static UCadenceArcGraph* CondHoldPauseGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(4);
		AddNode(Graph, Action_Root).Transitions.Add(CondEdge(Input_Light, Action_Light01));
		FCadenceArcNode& Light01 = AddNode(Graph, Action_Light01);
		FCadenceArcTransition PauseEdge = CondWithPause(CondEdge(Input_Heavy, Action_Finisher01, 1), 0.25);
		PauseEdge.InputPhase = ECadenceArcInputPhase::Released;
		FCadenceArcTransition PlainEdge = CondEdge(Input_Heavy, Action_Light02);
		PlainEdge.InputPhase = ECadenceArcInputPhase::Released;
		Light01.Transitions = {PauseEdge, PlainEdge};
		AddNode(Graph, Action_Light02);
		AddNode(Graph, Action_Finisher01);
		return Graph;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionHoldPauseTest,
		"CadenceArc.Resolver.Condition.HoldPause",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionHoldPauseTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = CondToken(2);
		// Light01 在 1.0 完成之后的按住
		const auto Completed = [this]()
		{
			UCadenceArcResolver* Resolver = CondResolver(*this, CondHoldPauseGraph());
			const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(CondInput(Input_Light, 0.5));
			Resolver->NotifyActionStarted(Submit.GetActionRequest().RequestId);
			CompleteAt(*this, Resolver, Submit.GetActionRequest().RequestId, 1.0);
			return Resolver;
		};
		{
			// 停顿看按下时刻（1.5 − 1.0 = 0.5），与按了多久无关
			UCadenceArcResolver* Resolver = Completed();
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.5))) { return false; }
			CondExpectTarget(*this, TEXT("Pause measured at press selects the pause edge"),
			                 Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 1.5, 3.5)).GetResolution(),
			                 Action_Finisher01);
		}
		{
			// 按下时只停了 0.1s：按住再久松手也不算停顿
			UCadenceArcResolver* Resolver = Completed();
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 1.1))) { return false; }
			CondExpectTarget(*this, TEXT("A long hold is not a pause"),
			                 Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 1.1, 3.0)).GetResolution(),
			                 Action_Light02);
		}
		const auto ExecutingLight01 = [this](int64& OutRequestId)
		{
			UCadenceArcResolver* Resolver = CondResolver(*this, CondHoldPauseGraph());
			const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(CondInput(Input_Light, 0.5));
			OutRequestId = Submit.GetActionRequest().RequestId;
			Resolver->NotifyActionStarted(OutRequestId);
			Resolver->OpenBufferWindow(OutRequestId);
			return Resolver;
		};
		{
			// 在前一个动作里按下、完成之后才松手：按下早于起点，停顿按 0
			int64 RequestId = 0;
			UCadenceArcResolver* Resolver = ExecutingLight01(RequestId);
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 0.8))) { return false; }
			TestBufferConsumption(*this, TEXT("Completion waits for the release"),
			                      CompleteAt(*this, Resolver, RequestId, 1.0),
			                      ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::WaitingForRelease);
			CondExpectTarget(*this, TEXT("Hold pressed before completion is not a pause"),
			                 Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 0.8, 2.0)).GetResolution(),
			                 Action_Light02);
		}
		{
			// 动作执行中就松手：松手进入缓冲，完成时停顿按 0
			int64 RequestId = 0;
			UCadenceArcResolver* Resolver = ExecutingLight01(RequestId);
			if (!CondGrant(*this, Resolver, Token, CondPress(Input_Heavy, 0.8))) { return false; }
			Resolver->ReleaseInputHold(Token, CondRelease(Input_Heavy, 0.8, 0.9));
			const FCadenceArcActionCompletionOutcome Completion = CompleteAt(*this, Resolver, RequestId, 3.0);
			TestTag(*this, TEXT("Buffered release is not a pause"),
			        Completion.GetNextActionRequest().TargetActionTag, Action_Light02);
		}
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
