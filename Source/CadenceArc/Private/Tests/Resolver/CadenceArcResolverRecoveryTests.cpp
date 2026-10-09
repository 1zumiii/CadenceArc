// 连招恢复：停顿超时重置（ComboResetSeconds）和无匹配时回退入口（bFallbackToEntryOnNoMatch）。
//
// 被测契约：
// - Ready 时，距上次完成的停顿达到 ComboResetSeconds，新输入从入口选边；阈值本身算作到期，0 表示关闭。
// - 重置只改变选边源，不提前提交节点：请求的 SourceActionTag 为入口，Started 之前 CurrentActionTag 不变，被拒绝时也不变。
// - 缓冲在完成时消费，停顿为 0，不会触发重置；动作执行中不计时。
// - 按住在授予资格前判断重置，授予后源节点冻结，松手晚于重置时间也不改变。
// - 回退只针对 NoMatchingTransition；ConditionNotMet 不回退。直接输入、缓冲消费和按住申请都适用。
// - GetEffectiveActionTag 与 GetComboResetRemainingSeconds 只查询，不修改状态。
// - 调试历史记录 ComboReset 和 FallbackToEntry（仅编辑器构建）。

#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"

namespace CadenceArc::Tests::Recovery
{
	constexpr double ResetSeconds = 1.0;
	constexpr double FirstCompletion = 2.0; // Light01 在 2.0 完成，重置到期时间为 3.0

	static FCadenceArcInputEvent At(const FGameplayTag& InputTag, const double Time,
	                                const ECadenceArcInputPhase Phase = ECadenceArcInputPhase::Pressed)
	{
		FCadenceArcInputEvent Event = MakeInput(InputTag, Time);
		Event.InputPhase = Phase;
		return Event;
	}

	static FCadenceArcInputEvent ReleaseAt(const FGameplayTag& InputTag, const double PressTime, const double Time)
	{
		FCadenceArcInputEvent Event = At(InputTag, Time, ECadenceArcInputPhase::Released);
		Event.HeldDurationSeconds = Time - PressTime;
		return Event;
	}

	static FCadenceArcInputToken Token(const int64 PressId)
	{
		FCadenceArcInputToken Result;
		Result.SourceSession = FGuid(7, 0x0A0B0C0D, 0x11223344, 0x55667788);
		Result.PressId = PressId;
		return Result;
	}

	static UCadenceArcResolver* MakeResolver(FAutomationTestBase& Test, UCadenceArcGraph* Graph)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestInit(Test, TEXT("Recovery graph initializes"), Resolver->Initialize(Graph),
		         ECadenceArcResolverInitResult::Success);
		return Resolver;
	}

	// 提交、开始并在给定时间完成一个动作，返回请求
	static FCadenceArcActionRequest Run(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
	                                    const FGameplayTag& InputTag, const double PressTime, const double CompleteTime)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(InputTag, PressTime));
		const FCadenceArcActionRequest Request = Outcome.GetActionRequest();
		Test.TestTrue(TEXT("Setup input produces a request"), Outcome.HasActionRequest());
		Resolver->NotifyActionStarted(Request.RequestId);
		Resolver->NotifyActionCompleted(Request.RequestId, CompleteTime);
		return Request;
	}

	// Root -Light-> Light01，在 FirstCompletion 完成，停在 Light01 的 Ready
	static UCadenceArcResolver* AtLight01(FAutomationTestBase& Test, UCadenceArcGraph* Graph)
	{
		UCadenceArcResolver* Resolver = MakeResolver(Test, Graph);
		Run(Test, Resolver, Input_Light, 1.0, FirstCompletion);
		TestTag(Test, TEXT("Setup reaches Light01"), Resolver->GetCurrentActionTag(), Action_Light01);
		return Resolver;
	}

	static UCadenceArcGraph* ResetGraph(const double Reset = ResetSeconds)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->ComboResetSeconds = Reset;
		return Graph;
	}

	// Root 与 Light01 上的 Heavy 都只有 Released 边：入口出 Heavy01，Light01 出 Finisher01
	static UCadenceArcGraph* HoldGraph(const double Reset, const bool bFallback)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->ComboResetSeconds = Reset;
		Graph->bFallbackToEntryOnNoMatch = bFallback;
		for (FCadenceArcNode& Node : Graph->Nodes)
		{
			for (FCadenceArcTransition& Edge : Node.Transitions)
			{
				if (Edge.InputTag == Input_Heavy)
				{
					Edge.InputPhase = ECadenceArcInputPhase::Released;
				}
			}
		}
		return Graph;
	}
}

namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryResetThresholdTest,
		"CadenceArc.Resolver.Recovery.ResetAtThreshold",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryResetThresholdTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 停顿 0.5 未到期：照常从 Light01 续招
		{
			UCadenceArcResolver* Resolver = AtLight01(*this, ResetGraph());
			const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(Input_Light, 2.5));
			TestTag(*this, TEXT("Before the deadline continues the combo"), Outcome.GetActionRequest().TargetActionTag,
			        Action_Light02);
		}

		// 停顿正好等于阈值：到期，从入口选边
		{
			UCadenceArcResolver* Resolver = AtLight01(*this, ResetGraph());
			const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(Input_Light, FirstCompletion + ResetSeconds));
			TestTag(*this, TEXT("At the deadline resolves from the entry"), Outcome.GetActionRequest().TargetActionTag,
			        Action_Light01);
			TestTag(*this, TEXT("Request source is the entry"), Outcome.GetActionRequest().SourceActionTag, Action_Root);
			TestTag(*this, TEXT("Reset does not commit before Started"), Resolver->GetCurrentActionTag(), Action_Light01);

			// 执行器拒绝：已提交节点保持不变，下一次输入仍然从入口选边
			Resolver->NotifyActionRejected(Outcome.GetActionRequest().RequestId);
			TestTag(*this, TEXT("Rejected reset request keeps the committed node"), Resolver->GetCurrentActionTag(),
			        Action_Light01);
			TestTag(*this, TEXT("Effective source stays the entry"), Resolver->GetEffectiveActionTag(3.5), Action_Root);

			const FCadenceArcSubmitOutcome Again = Resolver->SubmitInput(At(Input_Light, 3.5));
			Resolver->NotifyActionStarted(Again.GetActionRequest().RequestId);
			TestTag(*this, TEXT("Started commits the entry branch"), Resolver->GetCurrentActionTag(), Action_Light01);
			TestTag(*this, TEXT("Committed request came from the entry"), Again.GetActionRequest().SourceActionTag,
			        Action_Root);
		}

		// 关闭重置：停顿再长也续招
		{
			UCadenceArcResolver* Resolver = AtLight01(*this, ResetGraph(0.0));
			const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(Input_Light, 100.0));
			TestTag(*this, TEXT("Disabled reset never applies"), Outcome.GetActionRequest().TargetActionTag,
			        Action_Light02);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryQueriesTest,
		"CadenceArc.Resolver.Recovery.EffectiveSourceAndCountdown",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryQueriesTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 初始化后还没有完成过动作：没有计时，当前就是入口
		UCadenceArcResolver* Fresh = MakeResolver(*this, ResetGraph());
		TestEqual(TEXT("No completion yet has no countdown"), Fresh->GetComboResetRemainingSeconds(5.0), -1.0);
		TestTag(*this, TEXT("Fresh resolver is at the entry"), Fresh->GetEffectiveActionTag(5.0), Action_Root);

		UCadenceArcResolver* Resolver = AtLight01(*this, ResetGraph());
		const FResolverSnapshot Before(Resolver);
		TestEqual(TEXT("Countdown before the deadline"), Resolver->GetComboResetRemainingSeconds(2.25), 0.75);
		TestEqual(TEXT("Countdown reaches zero at the deadline"), Resolver->GetComboResetRemainingSeconds(3.0), 0.0);
		TestEqual(TEXT("Countdown stays at zero after the deadline"), Resolver->GetComboResetRemainingSeconds(9.0), 0.0);
		TestTag(*this, TEXT("Effective source before the deadline"), Resolver->GetEffectiveActionTag(2.5), Action_Light01);
		TestTag(*this, TEXT("Effective source after the deadline"), Resolver->GetEffectiveActionTag(3.0), Action_Root);
		Before.ExpectUnchanged(*this, Resolver);

		// 动作执行中不计时
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(Input_Light, 2.5));
		Resolver->NotifyActionStarted(Outcome.GetActionRequest().RequestId);
		TestEqual(TEXT("Executing has no countdown"), Resolver->GetComboResetRemainingSeconds(10.0), -1.0);
		TestTag(*this, TEXT("Executing uses the committed node"), Resolver->GetEffectiveActionTag(10.0), Action_Light02);

		// 关闭重置：没有计时
		UCadenceArcResolver* Disabled = AtLight01(*this, ResetGraph(0.0));
		TestEqual(TEXT("Disabled reset has no countdown"), Disabled->GetComboResetRemainingSeconds(2.5), -1.0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryBufferTest,
		"CadenceArc.Resolver.Recovery.BufferedInputNeverResets",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryBufferTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 动作比重置时间长得多：缓冲在完成时消费，停顿按 0，从 Light01 续招
		UCadenceArcResolver* Resolver = MakeResolver(*this, ResetGraph());
		const FCadenceArcSubmitOutcome First = Resolver->SubmitInput(At(Input_Light, 1.0));
		const int64 FirstId = First.GetActionRequest().RequestId;
		Resolver->NotifyActionStarted(FirstId);
		Resolver->OpenBufferWindow(FirstId);
		TestSubmit(*this, TEXT("Light buffered"), Resolver->SubmitInput(At(Input_Light, 5.0)),
		           ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
		const FCadenceArcActionCompletionOutcome Completion = Resolver->NotifyActionCompleted(FirstId, 10.0);
		TestTrue(TEXT("Buffer consumed"), Completion.HasNextActionRequest());
		TestTag(*this, TEXT("Buffered input continues from the committed node"),
		        Completion.GetNextActionRequest().TargetActionTag, Action_Light02);
		TestTag(*this, TEXT("Buffered request source"), Completion.GetNextActionRequest().SourceActionTag, Action_Light01);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryHoldTest,
		"CadenceArc.Resolver.Recovery.HoldDecidesSourceAtGrant",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryHoldTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 按下时已到期：从入口授予资格，松手出入口的 Heavy01
		{
			UCadenceArcResolver* Resolver = AtLight01(*this, HoldGraph(ResetSeconds, false));
			TestEqual(TEXT("Expired press is granted"), static_cast<int32>(
				          Resolver->BeginInputHold(Token(1), At(Input_Heavy, 3.5)).GetResult()),
			          static_cast<int32>(ECadenceArcHoldResult::Granted));
			const FCadenceArcInputAdvanceOutcome Release = Resolver->ReleaseInputHold(Token(1), ReleaseAt(Input_Heavy, 3.5, 3.6));
			TestTrue(TEXT("Expired hold releases"), Release.HasActionRequest());
			TestTag(*this, TEXT("Expired hold resolves from the entry"),
			        Release.GetResolution().GetActionRequest().TargetActionTag, Action_Heavy01);
			TestTag(*this, TEXT("Expired hold request source"),
			        Release.GetResolution().GetActionRequest().SourceActionTag, Action_Root);
		}

		// 按下时未到期、松手时已过期：资格的源节点在授予时冻结，仍从 Light01 出招
		{
			UCadenceArcResolver* Resolver = AtLight01(*this, HoldGraph(ResetSeconds, false));
			Resolver->BeginInputHold(Token(1), At(Input_Heavy, 2.5));
			TestTag(*this, TEXT("Pending hold reports its frozen source"), Resolver->GetEffectiveActionTag(4.0),
			        Action_Light01);
			TestEqual(TEXT("Pending hold has no countdown"), Resolver->GetComboResetRemainingSeconds(2.75), -1.0);
			const FCadenceArcInputAdvanceOutcome Release = Resolver->ReleaseInputHold(Token(1), ReleaseAt(Input_Heavy, 2.5, 4.0));
			TestTag(*this, TEXT("Frozen source survives the deadline"),
			        Release.GetResolution().GetActionRequest().TargetActionTag, Action_Finisher01);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryFallbackTest,
		"CadenceArc.Resolver.Recovery.FallbackOnlyOnNoMatch",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryFallbackTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 到达 Light02：它只有 Heavy 边。按 Light 没有匹配
		const auto AtLight02 = [this](const bool bFallback, const bool bConditionalLight)
		{
			UCadenceArcGraph* Graph = MakeValidGraph();
			Graph->bFallbackToEntryOnNoMatch = bFallback;
			if (bConditionalLight)
			{
				FCadenceArcTransition& Edge = Graph->Nodes[2].Transitions.AddDefaulted_GetRef();
				Edge.InputTag = Input_Light;
				Edge.TargetActionTag = Action_Finisher02;
				Edge.RequiredContextTags.AddTag(Context_Forward);
			}
			UCadenceArcResolver* Resolver = MakeResolver(*this, Graph);
			Run(*this, Resolver, Input_Light, 1.0, 2.0);
			Run(*this, Resolver, Input_Light, 2.1, 3.0);
			TestTag(*this, TEXT("Setup reaches Light02"), Resolver->GetCurrentActionTag(), Action_Light02);
			return Resolver;
		};

		{
			UCadenceArcResolver* Resolver = AtLight02(true, false);
			const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(At(Input_Light, 3.1));
			TestTag(*this, TEXT("No match falls back to the entry"), Outcome.GetActionRequest().TargetActionTag,
			        Action_Light01);
			TestTag(*this, TEXT("Fallback request source"), Outcome.GetActionRequest().SourceActionTag, Action_Root);
			TestTag(*this, TEXT("Fallback does not commit before Started"), Resolver->GetCurrentActionTag(), Action_Light02);
		}
		{
			UCadenceArcResolver* Resolver = AtLight02(false, false);
			const FResolverSnapshot Before(Resolver);
			TestSubmit(*this, TEXT("Disabled fallback reports no match"), Resolver->SubmitInput(At(Input_Light, 3.1)),
			           ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::NoMatchingTransition);
			Before.ExpectUnchanged(*this, Resolver);
		}
		{
			// Light02 有 Light 边但条件不满足：ConditionNotMet，不借入口绕过条件
			UCadenceArcResolver* Resolver = AtLight02(true, true);
			TestSubmit(*this, TEXT("Unmet condition does not fall back"), Resolver->SubmitInput(At(Input_Light, 3.1)),
			           ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::ConditionNotMet);
		}
		{
			// 缓冲消费也回退：Light02 执行中缓冲 Light，完成时从入口出 Light01
			UCadenceArcGraph* Graph = MakeValidGraph();
			Graph->bFallbackToEntryOnNoMatch = true;
			UCadenceArcResolver* Resolver = MakeResolver(*this, Graph);
			Run(*this, Resolver, Input_Light, 1.0, 2.0);
			const FCadenceArcSubmitOutcome Second = Resolver->SubmitInput(At(Input_Light, 2.1));
			const int64 SecondId = Second.GetActionRequest().RequestId;
			Resolver->NotifyActionStarted(SecondId);
			Resolver->OpenBufferWindow(SecondId);
			Resolver->SubmitInput(At(Input_Light, 2.5));
			const FCadenceArcActionCompletionOutcome Completion = Resolver->NotifyActionCompleted(SecondId, 3.0);
			TestTag(*this, TEXT("Buffered input falls back to the entry"),
			        Completion.GetNextActionRequest().TargetActionTag, Action_Light01);
			TestTag(*this, TEXT("Buffered fallback source"), Completion.GetNextActionRequest().SourceActionTag, Action_Root);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryHoldFallbackTest,
		"CadenceArc.Resolver.Recovery.HoldFallsBackToEntryReleasedEdges",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryHoldFallbackTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		// 经 Light01 按住 Heavy 到达 Finisher01；Finisher01 没有任何出边
		const auto AtFinisher01 = [this](const bool bFallback)
		{
			UCadenceArcResolver* Resolver = MakeResolver(*this, HoldGraph(0.0, bFallback));
			Run(*this, Resolver, Input_Light, 1.0, 2.0);
			Resolver->BeginInputHold(Token(1), At(Input_Heavy, 2.1));
			const FCadenceArcInputAdvanceOutcome Release = Resolver->ReleaseInputHold(Token(1), ReleaseAt(Input_Heavy, 2.1, 2.2));
			const int64 Id = Release.GetResolution().GetActionRequest().RequestId;
			Resolver->NotifyActionStarted(Id);
			Resolver->NotifyActionCompleted(Id, 3.0);
			TestTag(*this, TEXT("Setup reaches Finisher01"), Resolver->GetCurrentActionTag(), Action_Finisher01);
			return Resolver;
		};

		{
			// Finisher01 没有出边：按住申请改查入口的 Released 边
			UCadenceArcResolver* Resolver = AtFinisher01(true);
			TestEqual(TEXT("Hold falls back and is granted"), static_cast<int32>(
				          Resolver->BeginInputHold(Token(2), At(Input_Heavy, 3.1)).GetResult()),
			          static_cast<int32>(ECadenceArcHoldResult::Granted));
			const FCadenceArcInputAdvanceOutcome Release = Resolver->ReleaseInputHold(Token(2), ReleaseAt(Input_Heavy, 3.1, 3.2));
			TestTag(*this, TEXT("Fallback hold resolves from the entry"),
			        Release.GetResolution().GetActionRequest().TargetActionTag, Action_Heavy01);
		}
		{
			UCadenceArcResolver* Resolver = AtFinisher01(false);
			const FCadenceArcHoldOutcome Refused = Resolver->BeginInputHold(Token(2), At(Input_Heavy, 3.1));
			TestEqual(TEXT("Without fallback the hold is refused"), static_cast<int32>(Refused.GetReason()),
			          static_cast<int32>(ECadenceArcResolutionReason::NoMatchingTransition));
		}
		return !HasAnyErrors();
	}

#if WITH_EDITOR
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRecoveryHistoryTest,
		"CadenceArc.Resolver.Recovery.DebugHistoryRecordsRecovery",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCadenceArcRecoveryTickHistoryTest,
		"CadenceArc.Resolver.Recovery.TimeoutRecordedOnceOnAdvance",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRecoveryTickHistoryTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;
		const auto CountResets = [](const UCadenceArcResolver* Resolver)
		{
			TArray<FCadenceArcDebugEvent> Events;
			Resolver->GetDebugHistory().CopyEventsAfter(0, Events);
			return Events.FilterByPredicate([](const FCadenceArcDebugEvent& Event)
			{
				return Event.Operation == ECadenceArcDebugOperation::ComboReset;
			}).Num();
		};
		UCadenceArcResolver* Resolver = AtLight01(*this, ResetGraph());
		Resolver->AdvanceInputTime(2.75);
		TestEqual(TEXT("No record before deadline"), CountResets(Resolver), 0);
		const FResolverSnapshot Before(Resolver);
		Resolver->AdvanceInputTime(3.0);
		Before.ExpectUnchanged(*this, Resolver);
		TestEqual(TEXT("Deadline records without a keypress"), CountResets(Resolver), 1);
		TArray<FCadenceArcDebugEvent> Events;
		Resolver->GetDebugHistory().CopyEventsAfter(0, Events);
		TestEqual(TEXT("Record is timestamped at observation"), Events.Last().TimestampSeconds, 3.0);
		TestEqual(TEXT("Record includes elapsed pause"), Events.Last().PauseDurationSeconds, 1.0);
		TestFalse(TEXT("Observation does not invent an input"), Events.Last().InputTag.IsValid());
		Resolver->AdvanceInputTime(3.25);
		Resolver->GetEffectiveActionTag(3.5);
		const auto Request = Resolver->SubmitInput(At(Input_Light, 3.5)).GetActionRequest();
		Resolver->NotifyActionRejected(Request.RequestId);
		Resolver->AdvanceInputTime(4.0);
		TestEqual(TEXT("Ticks, query, input and rejection do not duplicate"), CountResets(Resolver), 1);
		Run(*this, Resolver, Input_Light, 4.0, 5.0);
		Resolver->AdvanceInputTime(6.0);
		TestEqual(TEXT("New completed action may expire again"), CountResets(Resolver), 2);

		UCadenceArcResolver* Disabled = AtLight01(*this, ResetGraph(0.0));
		Disabled->AdvanceInputTime(100.0);
		TestEqual(TEXT("Disabled timeout does not record"), CountResets(Disabled), 0);
		UCadenceArcResolver* Holding = AtLight01(*this, HoldGraph(1.0, false));
		Holding->BeginInputHold(Token(1), At(Input_Heavy, 2.25));
		Holding->AdvanceInputTime(3.0);
		TestEqual(TEXT("Frozen hold does not report timeout"), CountResets(Holding), 0);
		Holding->CancelInputHold(Token(1));
		Holding->AdvanceInputTime(3.25);
		TestEqual(TEXT("After hold cancellation expiry is observable"), CountResets(Holding), 1);
		return !HasAnyErrors();
	}

	bool FCadenceArcRecoveryHistoryTest::RunTest(const FString& Parameters)
	{
		using namespace Recovery;

		UCadenceArcGraph* Graph = ResetGraph();
		Graph->bFallbackToEntryOnNoMatch = true;
		UCadenceArcResolver* Resolver = AtLight01(*this, Graph);
		Resolver->SubmitInput(At(Input_Light, 3.5));

		TArray<FCadenceArcDebugEvent> Events;
		Resolver->GetDebugHistory().CopyEventsAfter(0, Events);
		const FCadenceArcDebugEvent* Reset = Events.FindByPredicate([](const FCadenceArcDebugEvent& Event)
		{
			return Event.Operation == ECadenceArcDebugOperation::ComboReset;
		});
		if (TestNotNull(TEXT("ComboReset recorded"), Reset))
		{
			TestEqual(TEXT("Reset records the pause"), Reset->PauseDurationSeconds, 1.5);
			TestEqual(TEXT("Reset records the threshold"), Reset->ComboResetSeconds, ResetSeconds);
			TestTag(*this, TEXT("Reset source"), Reset->RecoverySourceActionTag, Action_Light01);
			TestTag(*this, TEXT("Reset target"), Reset->RecoveryTargetActionTag, Action_Root);
			TestTag(*this, TEXT("Reset leaves the committed node"), Reset->CommittedAfter, Action_Light01);
		}
		TestFalse(TEXT("Entry resolves directly, no fallback record"), Events.ContainsByPredicate(
			          [](const FCadenceArcDebugEvent& Event) { return Event.Operation == ECadenceArcDebugOperation::FallbackToEntry; }));
		return !HasAnyErrors();
	}
#endif
}

#endif
