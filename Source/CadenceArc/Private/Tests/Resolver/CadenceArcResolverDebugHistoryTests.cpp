#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "Tests/CadenceArcTestSupport.h"

// 调试历史只在编辑器构建中存在
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

// 被测契约（Phase 7B / P7-03）：
// - 每个会改变或拒绝改变状态的公开操作记录一条，序号从 1 递增；查询不记录。
// - 每帧调用的 AdvanceInputTime 只在跨过蓄力阶段、自动释放或被拒绝时记录。
// - bFailed 标出"没有达到目的"的调用；动作完成时没有缓冲输入这类正常情况不算失败。
// - 记录调用前后的状态、已提交节点和待处理请求；产生的候选原样保存。
// - 环形容量 256，写满后保留最新的记录；读取只复制。
// - 记录不改变任何业务结果：由其余全部 Resolver 测试在同一构建中照常通过来保证。

namespace CadenceArc::Tests
{
	static TArray<FCadenceArcDebugEvent> ReadHistory(const UCadenceArcResolver* Resolver)
	{
		TArray<FCadenceArcDebugEvent> Events;
		Resolver->GetDebugHistory().CopyEventsAfter(0, Events);
		return Events;
	}

	static bool ExpectOperations(
		FAutomationTestBase& Test, const TCHAR* What, const TArray<FCadenceArcDebugEvent>& Events,
		const TArray<ECadenceArcDebugOperation>& Expected)
	{
		if (!Test.TestEqual(*FString::Printf(TEXT("%s: record count"), What), Events.Num(), Expected.Num()))
		{
			return false;
		}
		bool bPassed = true;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(*FString::Printf(TEXT("%s: record %d operation"), What, Index),
			                         Events[Index].Operation == Expected[Index]);
			bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s: record %d sequence"), What, Index),
			                          Events[Index].Sequence, static_cast<uint64>(Index + 1));
		}
		return bPassed;
	}

	// Root 上 Heavy 松手分两档：[0, 0.8) -> Light01，[0.8, 无上限) -> Light02。
	// 蓄力配置：0.2s 开始蓄力，0.8s 蓄满，蓄满后最多再按 1.0s，即按下后 1.8s 自动释放。
	static UCadenceArcGraph* MakeChargeHistoryGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		FCadenceArcNode& Root = AddNode(Graph, Action_Root);
		for (const TPair<double, TOptional<double>>& Tier :
		     {TPair<double, TOptional<double>>(0.0, TOptional<double>(0.8)),
		      TPair<double, TOptional<double>>(0.8, TOptional<double>())})
		{
			FCadenceArcTransition& Transition = Root.Transitions.AddDefaulted_GetRef();
			Transition.InputTag = Input_Heavy;
			Transition.TargetActionTag = Tier.Value.IsSet() ? Action_Light01 : Action_Light02;
			Transition.InputPhase = ECadenceArcInputPhase::Released;
			Transition.bUseDurationRange = true;
			Transition.DurationRange.MinHeldDurationSeconds = Tier.Key;
			Transition.DurationRange.bHasMaxHeldDuration = Tier.Value.IsSet();
			Transition.DurationRange.MaxHeldDurationSecondsExclusive = Tier.Value.Get(0.0);
		}
		FCadenceArcHoldChargeConfig& Charge = Root.HoldChargeConfigs.AddDefaulted_GetRef();
		Charge.InputTag = Input_Heavy;
		Charge.ChargeStartSeconds = 0.2;
		Charge.MaxChargedHoldSeconds = 1.0;
		AddNode(Graph, Action_Light01);
		AddNode(Graph, Action_Light02);
		return Graph;
	}

	static FCadenceArcInputToken MakeHistoryToken()
	{
		FCadenceArcInputToken Token;
		Token.SourceSession = FGuid(9, 0x0A0B0C0D, 0x11223344, 0x55667788);
		Token.PressId = 1;
		return Token;
	}

	static FCadenceArcInputEvent MakeHeavyPress(const double Timestamp)
	{
		FCadenceArcInputEvent Event = MakeInput(Input_Heavy, Timestamp);
		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		return Event;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugHistoryComboFlowTest,
		"CadenceArc.Resolver.DebugHistory.RecordsComboFlow",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugHistoryComboFlowTest::RunTest(const FString& Parameters)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestEqual(TEXT("A fresh resolver has no history"), Resolver->GetDebugHistory().GetNewestSequence(), uint64{0});
		Resolver->Initialize(MakeValidGraph());
		const FCadenceArcActionRequest First = Resolver->SubmitInput(MakeInput(Input_Light, 1.0)).GetActionRequest();
		Resolver->NotifyActionStarted(First.RequestId);
		Resolver->OpenBufferWindow(First.RequestId);
		Resolver->SubmitInput(MakeInput(Input_Light, 1.2)); // 存入缓冲
		Resolver->CloseBufferWindow(First.RequestId);
		// 查询函数不留记录
		Resolver->GetState();
		Resolver->GetInputHoldSnapshot();
		const FCadenceArcActionCompletionOutcome Completed = Resolver->NotifyActionCompleted(First.RequestId, 1.5);

		const TArray<FCadenceArcDebugEvent> Events = ReadHistory(Resolver);
		if (!ExpectOperations(*this, TEXT("Combo flow"), Events, {
			ECadenceArcDebugOperation::Initialize, ECadenceArcDebugOperation::SubmitInput,
			ECadenceArcDebugOperation::ActionStarted, ECadenceArcDebugOperation::OpenWindow,
			ECadenceArcDebugOperation::SubmitInput, ECadenceArcDebugOperation::CloseWindow,
			ECadenceArcDebugOperation::ActionCompleted}))
		{
			return false;
		}
		for (const FCadenceArcDebugEvent& Event : Events)
		{
			TestFalse(*FString::Printf(TEXT("Record %llu is not a failure"), static_cast<unsigned long long>(Event.Sequence)),
			          Event.bFailed);
		}
		TestTag(*this, TEXT("Initialize lands on the entry"), Events[0].CommittedAfter, Action_Root);

		// 输入产生候选：请求原样保存；此时还没有提交，已提交节点仍是 Root
		const FCadenceArcDebugEvent& Submit = Events[1];
		TestTrue(TEXT("Submit produced a request"), Submit.Category == ECadenceArcResolutionCategory::RequestProduced);
		TestEqual(TEXT("Submit keeps the produced request id"), Submit.ProducedRequest.RequestId, First.RequestId);
		TestTag(*this, TEXT("Submit target"), Submit.ProducedRequest.TargetActionTag, Action_Light01);
		TestTrue(TEXT("Submit input tag and time"), Submit.InputTag == Input_Light && Submit.bHasTimestamp
		         && Submit.TimestampSeconds == 1.0);
		TestState(*this, TEXT("Submit leaves AwaitingStart"), Submit.StateAfter, ECadenceArcResolverState::AwaitingStart);
		TestTag(*this, TEXT("Submit does not commit"), Submit.CommittedAfter, Action_Root);

		// Started 才提交：记录调用前的请求和前后的已提交节点
		const FCadenceArcDebugEvent& Started = Events[2];
		TestEqual(TEXT("Started carries the caller's request id"), Started.CallerRequestId, First.RequestId);
		TestEqual(TEXT("Started sees the outstanding request"), Started.RequestBefore.RequestId, First.RequestId);
		TestTag(*this, TEXT("Started commits from Root"), Started.CommittedBefore, Action_Root);
		TestTag(*this, TEXT("Started commits to Light01"), Started.CommittedAfter, Action_Light01);

		TestTrue(TEXT("Second submit is buffered"), Events[4].Category == ECadenceArcResolutionCategory::Buffered);

		// 完成时消费缓冲产生下一段候选
		const FCadenceArcDebugEvent& Finish = Events[6];
		TestTrue(TEXT("Completion consumed the buffer"), Finish.Category == ECadenceArcResolutionCategory::RequestProduced);
		TestEqual(TEXT("Completion keeps the next request"), Finish.ProducedRequest.RequestId,
		          Completed.GetNextActionRequest().RequestId);
		TestTrue(TEXT("Completion time"), Finish.bHasTimestamp && Finish.TimestampSeconds == 1.5);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugHistoryFailureTest,
		"CadenceArc.Resolver.DebugHistory.MarksFailures",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugHistoryFailureTest::RunTest(const FString& Parameters)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->MaxBufferedInputAgeSeconds = 0.5;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(Graph);
		const FCadenceArcActionRequest First = Resolver->SubmitInput(MakeInput(Input_Light, 1.0)).GetActionRequest();
		Resolver->SubmitInput(MakeInput(Input_Heavy, 1.1)); // 上一个请求还没开始：输入被忽略
		Resolver->NotifyActionStarted(First.RequestId + 7); // 过期或错误的回调
		Resolver->NotifyActionStarted(First.RequestId);
		Resolver->Reset(); // 执行中不能 Reset
		Resolver->OpenBufferWindow(First.RequestId);
		Resolver->SubmitInput(MakeInput(Input_Light, 1.2));
		Resolver->NotifyActionCompleted(First.RequestId, 2.0); // 缓冲已超过 0.5s：丢弃

		const TArray<FCadenceArcDebugEvent> Events = ReadHistory(Resolver);
		if (!TestEqual(TEXT("Record count"), Events.Num(), 9))
		{
			return false;
		}
		const FCadenceArcDebugEvent& Ignored = Events[2];
		TestTrue(TEXT("Input while a request is pending is a failure"), Ignored.bFailed);
		TestTrue(TEXT("Ignored input reason"), Ignored.Reason == ECadenceArcResolutionReason::RequestPending);

		const FCadenceArcDebugEvent& Stale = Events[3];
		TestTrue(TEXT("Stale Started is a failure"), Stale.bFailed);
		TestTrue(TEXT("Stale Started result"), Stale.HandshakeResult == ECadenceArcHandshakeResult::RequestIdMismatch);
		TestTag(*this, TEXT("Stale Started commits nothing"), Stale.CommittedAfter, Action_Root);

		TestFalse(TEXT("Valid Started is not a failure"), Events[4].bFailed);
		TestTrue(TEXT("Reset while executing is a failure"), Events[5].bFailed
		         && Events[5].ResetResult == ECadenceArcResolverResetResult::Busy);

		const FCadenceArcDebugEvent& Dropped = Events[8];
		TestTrue(TEXT("Expired buffer on completion is a failure"), Dropped.bFailed);
		TestTrue(TEXT("Dropped reason"), Dropped.Reason == ECadenceArcResolutionReason::Expired);
		TestTrue(TEXT("Completion handshake itself succeeded"), Dropped.HandshakeResult == ECadenceArcHandshakeResult::Success);

		// 正常结束、没有缓冲：不是失败
		UCadenceArcResolver* Plain = NewObject<UCadenceArcResolver>();
		Plain->Initialize(MakeValidGraph());
		const FCadenceArcActionRequest Request = Plain->SubmitInput(MakeInput(Input_Light, 1.0)).GetActionRequest();
		Plain->NotifyActionStarted(Request.RequestId);
		Plain->NotifyActionCompleted(Request.RequestId, 1.5);
		const TArray<FCadenceArcDebugEvent> PlainEvents = ReadHistory(Plain);
		if (TestEqual(TEXT("Plain record count"), PlainEvents.Num(), 4))
		{
			TestFalse(TEXT("Finishing without a buffered input is not a failure"), PlainEvents[3].bFailed);
			TestTrue(TEXT("Plain completion reason"), PlainEvents[3].Reason == ECadenceArcResolutionReason::NoBufferedInput);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugHistoryAdvanceTest,
		"CadenceArc.Resolver.DebugHistory.AdvanceOnlyWhenSomethingHappens",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugHistoryAdvanceTest::RunTest(const FString& Parameters)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		if (!TestTrue(TEXT("Charge graph initializes"),
		              Resolver->Initialize(MakeChargeHistoryGraph()) == ECadenceArcResolverInitResult::Success))
		{
			return false;
		}
		Resolver->AdvanceInputTime(0.5); // 没有按住：什么都没发生
		Resolver->BeginInputHold(MakeHistoryToken(), MakeHeavyPress(1.0));
		Resolver->AdvanceInputTime(1.1); // 还没到 1.2 的蓄力起点
		Resolver->AdvanceInputTime(1.3); // 跨过蓄力起点
		Resolver->AdvanceInputTime(1.5);
		Resolver->AdvanceInputTime(1.9); // 跨过 1.8 的蓄满时刻
		Resolver->AdvanceInputTime(2.5);
		Resolver->AdvanceInputTime(3.0); // 到达 2.8 的自动释放
		Resolver->AdvanceInputTime(3.1); // 资格已经结束：什么都没发生

		const TArray<FCadenceArcDebugEvent> Events = ReadHistory(Resolver);
		if (!ExpectOperations(*this, TEXT("Advance"), Events, {
			ECadenceArcDebugOperation::Initialize, ECadenceArcDebugOperation::BeginHold,
			ECadenceArcDebugOperation::AdvanceTime, ECadenceArcDebugOperation::AdvanceTime,
			ECadenceArcDebugOperation::AdvanceTime}))
		{
			return false;
		}
		TestTrue(TEXT("Hold granted"), !Events[1].bFailed && Events[1].HoldResult == ECadenceArcHoldResult::Granted);
		TestTrue(TEXT("First recorded advance reaches Charging"), Events[2].StageReached == ECadenceArcHoldStage::Charging);
		TestTrue(TEXT("Second recorded advance reaches Charged"), Events[3].StageReached == ECadenceArcHoldStage::Charged);
		const FCadenceArcDebugEvent& AutoRelease = Events[4];
		TestTrue(TEXT("Auto release source"), AutoRelease.ReleaseSource == ECadenceArcInputReleaseSource::HoldLimit);
		TestTrue(TEXT("Auto release held for the capped duration"), AutoRelease.bHasHeldDuration
		         && FMath::IsNearlyEqual(AutoRelease.HeldDurationSeconds, 1.8, 1.e-9));
		TestTrue(TEXT("Auto release produced the charged attack"),
		         AutoRelease.Category == ECadenceArcResolutionCategory::RequestProduced
		         && AutoRelease.ProducedRequest.TargetActionTag == Action_Light02);
		TestFalse(TEXT("Auto release is not a failure"), AutoRelease.bFailed);
		TestTrue(TEXT("Advance records the input being held"), AutoRelease.InputTag == Input_Heavy);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugHistoryHoldTest,
		"CadenceArc.Resolver.DebugHistory.HoldReleaseAndCancel",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugHistoryHoldTest::RunTest(const FString& Parameters)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeChargeHistoryGraph());
		const FCadenceArcInputToken Token = MakeHistoryToken();
		Resolver->BeginInputHold(Token, MakeHeavyPress(1.0));
		Resolver->CancelInputHold(Token); // 取消：记录取消前按住的输入
		Resolver->CancelInputHold(Token); // 已经没有资格：失败
		Resolver->BeginInputHold(Token, MakeHeavyPress(2.0));
		FCadenceArcInputEvent Release = MakeInput(Input_Heavy, 2.5);
		Release.InputPhase = ECadenceArcInputPhase::Released;
		Release.HeldDurationSeconds = 0.5;
		Resolver->ReleaseInputHold(Token, Release);

		const TArray<FCadenceArcDebugEvent> Events = ReadHistory(Resolver);
		if (!ExpectOperations(*this, TEXT("Hold"), Events, {
			ECadenceArcDebugOperation::Initialize, ECadenceArcDebugOperation::BeginHold,
			ECadenceArcDebugOperation::CancelHold, ECadenceArcDebugOperation::CancelHold,
			ECadenceArcDebugOperation::BeginHold, ECadenceArcDebugOperation::ReleaseHold}))
		{
			return false;
		}
		TestTrue(TEXT("Cancel records the held input"), !Events[2].bFailed && Events[2].InputTag == Input_Heavy
		         && Events[2].HoldResult == ECadenceArcHoldResult::Cancelled);
		TestTrue(TEXT("Cancel without a hold is a failure"), Events[3].bFailed
		         && Events[3].Reason == ECadenceArcResolutionReason::NoMatchingHold);
		const FCadenceArcDebugEvent& Released = Events[5];
		TestTrue(TEXT("Manual release"), Released.ReleaseSource == ECadenceArcInputReleaseSource::Manual);
		TestTrue(TEXT("Released after 0.5s"), Released.bHasHeldDuration && Released.HeldDurationSeconds == 0.5);
		TestTrue(TEXT("Short release picks the first tier"),
		         Released.Category == ECadenceArcResolutionCategory::RequestProduced
		         && Released.ProducedRequest.TargetActionTag == Action_Light01);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugHistoryCapacityTest,
		"CadenceArc.Resolver.DebugHistory.RingKeepsNewest",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugHistoryCapacityTest::RunTest(const FString& Parameters)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());
		Resolver->SubmitInput(MakeInput(Input_Light, 1.0));
		for (int32 Index = 0; Index < 300; ++Index)
		{
			Resolver->SubmitInput(MakeInput(Input_Light, 1.0)); // 请求未开始，全部被忽略
		}
		const FCadenceArcDebugHistory& History = Resolver->GetDebugHistory();
		constexpr uint64 Newest = 302;
		constexpr uint64 Oldest = Newest - FCadenceArcDebugHistory::Capacity + 1;
		TestEqual(TEXT("Newest sequence"), History.GetNewestSequence(), Newest);
		TestEqual(TEXT("Oldest kept sequence"), History.GetOldestSequence(), Oldest);
		const TArray<FCadenceArcDebugEvent> Events = ReadHistory(Resolver);
		if (TestEqual(TEXT("Only the capacity is kept"), Events.Num(), FCadenceArcDebugHistory::Capacity))
		{
			TestEqual(TEXT("Copied oldest first"), Events[0].Sequence, Oldest);
			TestEqual(TEXT("Copied newest last"), Events.Last().Sequence, Newest);
		}
		// 按序号增量读取：只拿到之后的记录
		TArray<FCadenceArcDebugEvent> Tail;
		History.CopyEventsAfter(Newest - 3, Tail);
		TestEqual(TEXT("Incremental read"), Tail.Num(), 3);
		return !HasAnyErrors();
	}
}

#endif
