#include "Resolver/CadenceArcResolver.h"

// 公开操作的入口：调用对应的 Impl，并在编辑器构建中于返回前记录一条调试历史（Phase 7B）。
// 记录只读取调用前后的公开状态和返回值，不参与、也不改变任何业务结果；打包后的游戏里这里只剩一次转调。
// 记录原则是"策划看了有用"：每帧调用的 AdvanceInputTime 只在真正产生效果时记录，查询函数从不记录。

#if WITH_EDITOR
namespace
{
	bool IsProducedOrBuffered(const ECadenceArcResolutionCategory Category)
	{
		return Category == ECadenceArcResolutionCategory::RequestProduced
			|| Category == ECadenceArcResolutionCategory::Buffered;
	}

	void RecordInput(FCadenceArcDebugEvent& Record, const FCadenceArcInputEvent& Event)
	{
		Record.InputTag = Event.InputTag;
		Record.InputPhase = Event.InputPhase;
		Record.bHasTimestamp = true;
		Record.TimestampSeconds = Event.TimestampSeconds;
		Record.InputContextTags = Event.ContextTags;
		if (Event.InputPhase == ECadenceArcInputPhase::Released)
		{
			Record.bHasHeldDuration = true;
			Record.HeldDurationSeconds = Event.HeldDurationSeconds;
		}
	}

	// 输入解析的结果：产生候选或存入缓冲算成功，其余（被拒绝、没有匹配的边、窗口已关等）都是"输入没起作用"
	void RecordResolution(FCadenceArcDebugEvent& Record, const FCadenceArcSubmitOutcome& Outcome)
	{
		Record.Category = Outcome.GetCategory();
		Record.Reason = Outcome.GetReason();
		Record.ProducedRequest = Outcome.GetActionRequest();
		Record.bFailed = !IsProducedOrBuffered(Record.Category);
	}

	// 松手或推进的结果：跨过的蓄力阶段、释放来源和释放后的解析
	void RecordAdvance(FCadenceArcDebugEvent& Record, const FCadenceArcInputAdvanceOutcome& Outcome)
	{
		if (!Outcome.IsAccepted())
		{
			Record.bFailed = true;
			Record.Category = ECadenceArcResolutionCategory::Rejected;
			Record.Reason = Outcome.GetReason();
			return;
		}
		if (!Outcome.GetStageChanges().IsEmpty())
		{
			Record.StageReached = Outcome.GetStageChanges().Last().ToStage;
		}
		if (Outcome.HasRelease())
		{
			Record.ReleaseSource = Outcome.GetReleaseSource();
			Record.bHasHeldDuration = true;
			Record.HeldDurationSeconds = Outcome.GetReleasedInput().HeldDurationSeconds; // 自动释放时是截止时刻的时长
			Record.InputContextTags = Outcome.GetReleasedInput().ContextTags; // 自动释放沿用按下时的上下文
			RecordResolution(Record, Outcome.GetResolution());
		}
	}

	void RecordHandshake(FCadenceArcDebugEvent& Record, const int64 RequestId, const ECadenceArcHandshakeResult Result)
	{
		Record.CallerRequestId = RequestId;
		Record.HandshakeResult = Result;
		Record.bFailed = Result != ECadenceArcHandshakeResult::Success;
	}
}

FCadenceArcDebugEvent UCadenceArcResolver::BeginDebugRecord(const ECadenceArcDebugOperation Operation) const
{
	FCadenceArcDebugEvent Record;
	Record.Operation = Operation;
	Record.StateBefore = State;
	Record.CommittedBefore = CurrentActionTag;
	Record.RequestBefore = OutstandingRequest;
	return Record;
}

void UCadenceArcResolver::NoteDebugHostTime(const double Seconds)
{
	if (FMath::IsFinite(Seconds) && Seconds >= 0.0)
	{
		DebugLastHostTime = Seconds;
	}
}

void UCadenceArcResolver::EndDebugRecord(FCadenceArcDebugEvent& Record)
{
	// 不带时间的操作取此前最近一次带时间的调用：同一帧里 Submit 之后的 Started 拿到的就是这一帧的时间
	if (!Record.bHasTimestamp && DebugLastHostTime >= 0.0)
	{
		Record.bHasTimestamp = true;
		Record.bTimeFromLastCall = true;
		Record.TimestampSeconds = DebugLastHostTime;
	}
	Record.StateAfter = State;
	Record.CommittedAfter = CurrentActionTag;
	if (bDebugHasResolution)
	{
		Record.bHasResolutionContext = true;
		Record.ContextTags = DebugResolutionContext;
		Record.ResolutionSourceActionTag = DebugResolutionSource;
		Record.bHasPauseDuration = DebugResolutionPause >= 0.0;
		Record.PauseDurationSeconds = FMath::Max(DebugResolutionPause, 0.0);
		if (!Record.InputTag.IsValid())
		{
			// 完成时消费缓冲：调用本身不带输入，记下被解析的那个输入
			Record.InputTag = DebugResolutionEvent.InputTag;
			Record.InputPhase = DebugResolutionEvent.InputPhase;
			if (DebugResolutionEvent.InputPhase == ECadenceArcInputPhase::Released)
			{
				Record.bHasHeldDuration = true;
				Record.HeldDurationSeconds = DebugResolutionEvent.HeldDurationSeconds;
			}
		}
		bDebugHasResolution = false; // 只属于这一次公开调用
		DebugResolutionContext.Reset();
	}
	DebugHistory.Add(MoveTemp(Record));
}
#endif

ECadenceArcResolverInitResult UCadenceArcResolver::Initialize(UCadenceArcGraph* InGraph)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::Initialize);
#endif
	const ECadenceArcResolverInitResult Result = InitializeImpl(InGraph);
#if WITH_EDITOR
	Record.InitResult = Result;
	Record.bFailed = Result != ECadenceArcResolverInitResult::Success;
	EndDebugRecord(Record);
#endif
	return Result;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::SubmitInput(const FCadenceArcInputEvent& InInputEvent)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::SubmitInput);
	RecordInput(Record, InInputEvent);
	NoteDebugHostTime(InInputEvent.TimestampSeconds);
#endif
	FCadenceArcSubmitOutcome Outcome = SubmitInputImpl(InInputEvent);
#if WITH_EDITOR
	RecordResolution(Record, Outcome);
	EndDebugRecord(Record);
#endif
	return Outcome;
}

ECadenceArcResolverResetResult UCadenceArcResolver::Reset()
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::Reset);
#endif
	const ECadenceArcResolverResetResult Result = ResetImpl();
#if WITH_EDITOR
	Record.ResetResult = Result;
	Record.bFailed = Result != ECadenceArcResolverResetResult::Success;
	EndDebugRecord(Record);
#endif
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionStarted(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ActionStarted);
#endif
	const ECadenceArcHandshakeResult Result = NotifyActionStartedImpl(InRequestId);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionRejected(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ActionRejected);
#endif
	const ECadenceArcHandshakeResult Result = NotifyActionRejectedImpl(InRequestId);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

FCadenceArcActionCompletionOutcome UCadenceArcResolver::NotifyActionCompleted(
	const int64 InRequestId, const double CompletionTimestampSeconds)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ActionCompleted);
	Record.bHasTimestamp = true;
	Record.TimestampSeconds = CompletionTimestampSeconds;
	NoteDebugHostTime(CompletionTimestampSeconds);
#endif
	FCadenceArcActionCompletionOutcome Outcome = NotifyActionCompletedImpl(InRequestId, CompletionTimestampSeconds);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Outcome.GetHandshakeResult());
	if (!Record.bFailed)
	{
		// 动作正常结束。缓冲消费出了候选，或者本来就没有缓冲、还在等松手，都算正常；
		// 缓冲的输入过期、没有匹配的边等才是"按了但没接上"
		Record.Category = Outcome.GetBufferConsumption();
		Record.Reason = Outcome.GetBufferConsumptionReason();
		Record.ProducedRequest = Outcome.GetNextActionRequest();
		Record.bFailed = Record.Category != ECadenceArcResolutionCategory::RequestProduced
			&& Record.Reason != ECadenceArcResolutionReason::NoBufferedInput
			&& Record.Reason != ECadenceArcResolutionReason::WaitingForRelease;
	}
	EndDebugRecord(Record);
#endif
	return Outcome;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionCancelled(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ActionCancelled);
#endif
	const ECadenceArcHandshakeResult Result = NotifyActionCancelledImpl(InRequestId);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionInterrupted(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ActionInterrupted);
#endif
	const ECadenceArcHandshakeResult Result = NotifyActionInterruptedImpl(InRequestId);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcResolver::OpenBufferWindow(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::OpenWindow);
#endif
	const ECadenceArcHandshakeResult Result = SetBufferWindowState(InRequestId, true);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcResolver::CloseBufferWindow(const int64 InRequestId)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::CloseWindow);
#endif
	const ECadenceArcHandshakeResult Result = SetBufferWindowState(InRequestId, false);
#if WITH_EDITOR
	RecordHandshake(Record, InRequestId, Result);
	EndDebugRecord(Record);
#endif
	return Result;
}

FCadenceArcHoldOutcome UCadenceArcResolver::BeginInputHold(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& PressEvent)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::BeginHold);
	RecordInput(Record, PressEvent);
	NoteDebugHostTime(PressEvent.TimestampSeconds);
#endif
	FCadenceArcHoldOutcome Outcome = BeginInputHoldImpl(Token, PressEvent);
#if WITH_EDITOR
	Record.HoldResult = Outcome.GetResult();
	Record.Reason = Outcome.GetReason();
	Record.bFailed = Outcome.GetResult() == ECadenceArcHoldResult::Rejected;
	EndDebugRecord(Record);
#endif
	return Outcome;
}

FCadenceArcInputAdvanceOutcome UCadenceArcResolver::ReleaseInputHold(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& ReleaseEvent)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::ReleaseHold);
	RecordInput(Record, ReleaseEvent);
	NoteDebugHostTime(ReleaseEvent.TimestampSeconds);
#endif
	FCadenceArcInputAdvanceOutcome Outcome = ReleaseInputHoldImpl(Token, ReleaseEvent);
#if WITH_EDITOR
	RecordAdvance(Record, Outcome);
	EndDebugRecord(Record);
#endif
	return Outcome;
}

FCadenceArcInputAdvanceOutcome UCadenceArcResolver::AdvanceInputTime(const double NowSeconds)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::AdvanceTime);
	Record.bHasTimestamp = true;
	Record.TimestampSeconds = NowSeconds;
	Record.InputTag = InputSlot.InputEvent.InputTag; // 推进的是哪个按住资格；没有资格时为空
	NoteDebugHostTime(NowSeconds); // 即使这次推进不记录，也用它给之后不带时间的记录提供时间
#endif
	FCadenceArcInputAdvanceOutcome Outcome = AdvanceInputTimeImpl(NowSeconds);
#if WITH_EDITOR
	// 宿主每帧都会调用；什么都没发生的推进不记录，否则真正有用的记录几秒就被冲掉
	if (!Outcome.IsAccepted() || !Outcome.GetStageChanges().IsEmpty() || Outcome.HasRelease())
	{
		RecordAdvance(Record, Outcome);
		EndDebugRecord(Record);
	}
#endif
	return Outcome;
}

FCadenceArcHoldOutcome UCadenceArcResolver::CancelInputHold(const FCadenceArcInputToken& Token)
{
#if WITH_EDITOR
	FCadenceArcDebugEvent Record = BeginDebugRecord(ECadenceArcDebugOperation::CancelHold);
	Record.InputTag = InputSlot.InputEvent.InputTag; // 取消前槽里的输入；取消后就清空了
#endif
	FCadenceArcHoldOutcome Outcome = CancelInputHoldImpl(Token);
#if WITH_EDITOR
	Record.HoldResult = Outcome.GetResult();
	Record.Reason = Outcome.GetReason();
	Record.bFailed = Outcome.GetResult() == ECadenceArcHoldResult::Rejected;
	EndDebugRecord(Record);
#endif
	return Outcome;
}
