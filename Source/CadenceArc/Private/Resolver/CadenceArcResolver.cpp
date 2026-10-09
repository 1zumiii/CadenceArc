#include "Resolver/CadenceArcResolver.h"

#include "CadenceArcHoldTiming.h"
#include "Graph/CadenceArcGraph.h"
#include "Graph/CadenceArcGraphQuery.h"

DEFINE_LOG_CATEGORY(LogCadenceArc);

namespace
{
	ECadenceArcResolutionCategory CategoryOf(const ECadenceArcResolutionReason Reason)
	{
		switch (Reason)
		{
		case ECadenceArcResolutionReason::None:
			return ECadenceArcResolutionCategory::RequestProduced;
		case ECadenceArcResolutionReason::RequestPending:
		case ECadenceArcResolutionReason::BufferWindowClosed:
		case ECadenceArcResolutionReason::NoMatchingTransition:
		case ECadenceArcResolutionReason::ConditionNotMet:
		case ECadenceArcResolutionReason::NoBufferedInput:
		case ECadenceArcResolutionReason::Expired:
		case ECadenceArcResolutionReason::WaitingForRelease:
			return ECadenceArcResolutionCategory::NoAction;
		default:
			return ECadenceArcResolutionCategory::Rejected; // 新增原因默认悲观
		}
	}
}

FGameplayTagContainer UCadenceArcResolver::MakeResolutionContext(const FCadenceArcInputEvent& Event) const
{
	FGameplayTagContainer MergedTags = ContextTags;
	MergedTags.AppendTags(Event.ContextTags);
	return MergedTags;
}

UCadenceArcResolver::FEdgeMatch UCadenceArcResolver::MatchTransition(
	const FGameplayTag& SourceActionTag, const FCadenceArcInputEvent& Event, const double PauseDurationSeconds,
	const TArray<FCadenceArcTransition>* EdgesOverride)
{
	const FGameplayTagContainer Context = MakeResolutionContext(Event);
#if WITH_EDITOR
	bDebugHasResolution = true;
	DebugResolutionContext = Context;
	DebugResolutionPause = PauseDurationSeconds;
	DebugResolutionEvent = Event;
	DebugResolutionSource = SourceActionTag;
#endif
	using CadenceArc::GraphQuery::EMatchResult;
	const CadenceArc::GraphQuery::FTransitionMatch Match = CadenceArc::GraphQuery::FindUniqueTransition(
		*Graph, SourceActionTag, Event, Context, PauseDurationSeconds, EdgesOverride);
	FEdgeMatch Result;
	Result.SourceActionTag = SourceActionTag;
	Result.TargetActionTag = Match.TargetActionTag;
	switch (Match.Result)
	{
	case EMatchResult::Found: Result.Reason = ECadenceArcResolutionReason::None; break;
	case EMatchResult::SourceNodeNotFound: Result.Reason = ECadenceArcResolutionReason::CurrentNodeNotFound; break;
	case EMatchResult::NoMatchingInput: Result.Reason = ECadenceArcResolutionReason::NoMatchingTransition; break;
	case EMatchResult::ConditionNotMet: Result.Reason = ECadenceArcResolutionReason::ConditionNotMet; break;
	case EMatchResult::Ambiguous: Result.Reason = ECadenceArcResolutionReason::AmbiguousTransition; break;
	case EMatchResult::TargetNodeNotFound: Result.Reason = ECadenceArcResolutionReason::TargetNodeNotFound; break;
	}
	return Result;
}

double UCadenceArcResolver::GetPauseDurationSeconds(const FCadenceArcInputEvent& Event) const
{
	return LastCompletionTimestampSeconds >= 0.0
		? Event.TimestampSeconds - LastCompletionTimestampSeconds : -1.0;
}

FGameplayTag UCadenceArcResolver::GetFreshInputSourceActionTag(const double NowSeconds) const
{
	if (IsInitialized() && State == ECadenceArcResolverState::Ready
		&& FMath::IsFinite(NowSeconds) && NowSeconds >= 0.0
		&& FMath::IsFinite(Graph->ComboResetSeconds) && Graph->ComboResetSeconds > 0.0
		&& LastCompletionTimestampSeconds >= 0.0
		&& NowSeconds - LastCompletionTimestampSeconds >= Graph->ComboResetSeconds)
	{
		return Graph->EntryActionTag;
	}
	return CurrentActionTag;
}

FGameplayTag UCadenceArcResolver::GetEffectiveActionTag(const double NowSeconds) const
{
	// 资格一旦授予，松手使用冻结的源；查询不能让 UI 把蓄力途中到期误报成重置。
	return InputSlot.SlotState == ECadenceArcInputSlotState::PendingHold
		? InputSlot.SourceActionTag : GetFreshInputSourceActionTag(NowSeconds);
}

double UCadenceArcResolver::GetComboResetRemainingSeconds(const double NowSeconds) const
{
	if (!IsInitialized() || State != ECadenceArcResolverState::Ready
		|| CurrentActionTag == Graph->EntryActionTag
		|| InputSlot.SlotState == ECadenceArcInputSlotState::PendingHold
		|| !FMath::IsFinite(NowSeconds) || NowSeconds < 0.0
		|| LastCompletionTimestampSeconds < 0.0 || NowSeconds < LastCompletionTimestampSeconds
		|| !FMath::IsFinite(Graph->ComboResetSeconds) || Graph->ComboResetSeconds <= 0.0)
	{
		return -1.0;
	}
	return FMath::Max(0.0, Graph->ComboResetSeconds - (NowSeconds - LastCompletionTimestampSeconds));
}

bool UCadenceArcResolver::HasReleasedTransitionForInput(const FGameplayTag& InputTag, const double NowSeconds) const
{
	if (!IsInitialized())
	{
		return false;
	}
	const FGameplayTag Source = GetFreshInputSourceActionTag(NowSeconds);
	const FCadenceArcNode* Node = Graph->FindAction(Source);
	if (!Node || !Node->IsValidTransition())
	{
		return false;
	}
	const auto HasEdge = [&InputTag](const FCadenceArcNode& Candidate)
	{
		return Candidate.Transitions.ContainsByPredicate([&InputTag](const FCadenceArcTransition& Edge)
		{
			return Edge.InputTag == InputTag && Edge.InputPhase == ECadenceArcInputPhase::Released;
		});
	};
	if (HasEdge(*Node))
	{
		return true;
	}
	const FCadenceArcNode* Entry = Graph->FindAction(Graph->EntryActionTag);
	return Graph->bFallbackToEntryOnNoMatch && Source != Graph->EntryActionTag
		&& Entry && Entry->IsValidTransition() && HasEdge(*Entry);
}

UCadenceArcResolver::FEdgeMatch UCadenceArcResolver::MatchInputWithFallback(
	const FGameplayTag& SourceActionTag, const FCadenceArcInputEvent& Event, const double PauseDurationSeconds)
{
	FEdgeMatch Match = MatchTransition(SourceActionTag, Event, PauseDurationSeconds);
	// 只回退一次。条件失败、歧义或坏图都不能借入口分支绕过。
	if (Match.Reason == ECadenceArcResolutionReason::NoMatchingTransition
		&& Graph->bFallbackToEntryOnNoMatch && SourceActionTag != Graph->EntryActionTag)
	{
		RecordEntryRecovery(false, SourceActionTag, Event, PauseDurationSeconds);
		Match = MatchTransition(Graph->EntryActionTag, Event, PauseDurationSeconds);
	}
	return Match;
}

void UCadenceArcResolver::RecordEntryRecovery(const bool bTimeout, const FGameplayTag& SourceActionTag,
	const FCadenceArcInputEvent& Event, const double PauseDurationSeconds)
{
#if WITH_EDITOR
	// 这是选边源变更的诊断，不是动作提交；直接写入，避免消耗外层调用的选边记录。
	FCadenceArcDebugEvent Record = BeginDebugRecord(bTimeout
		? ECadenceArcDebugOperation::ComboReset : ECadenceArcDebugOperation::FallbackToEntry);
	Record.StateAfter = State;
	Record.CommittedAfter = CurrentActionTag;
	Record.InputTag = Event.InputTag;
	Record.InputPhase = Event.InputPhase;
	Record.bHasTimestamp = true;
	Record.TimestampSeconds = Event.TimestampSeconds;
	Record.bHasPauseDuration = PauseDurationSeconds >= 0.0;
	Record.PauseDurationSeconds = PauseDurationSeconds;
	Record.ComboResetSeconds = Graph->ComboResetSeconds;
	Record.RecoverySourceActionTag = SourceActionTag;
	Record.RecoveryTargetActionTag = Graph->EntryActionTag;
	DebugHistory.Add(MoveTemp(Record));
#endif
}

void UCadenceArcResolver::RecordCompletionTimestamp(const double CompletionTimestampSeconds)
{
	// 普通缓冲保留原有“握手成功、消费失败”的时间错误契约；非法时间不能成为停顿起点。
	LastCompletionTimestampSeconds = FMath::IsFinite(CompletionTimestampSeconds)
		&& CompletionTimestampSeconds >= 0.0 ? CompletionTimestampSeconds : -1.0;
}

ECadenceArcResolverInitResult UCadenceArcResolver::InitializeImpl(UCadenceArcGraph* InGraph)
{
	if (State == ECadenceArcResolverState::AwaitingStart || State == ECadenceArcResolverState::Executing)
	{
		return ECadenceArcResolverInitResult::UnexpectedState;
	}
	if (!IsValid(InGraph) || InGraph->MaxBufferedInputAgeSeconds < 0.0
		|| !FMath::IsFinite(InGraph->MaxBufferedInputAgeSeconds))
	{
		return ECadenceArcResolverInitResult::InvalidGraph;
	}
	if (!InGraph->EntryActionTag.IsValid())
	{
		return ECadenceArcResolverInitResult::InvalidEntryActionTag;
	}
	if (!InGraph->Nodes.ContainsByPredicate(
			[&](const FCadenceArcNode& Node) { return Node.ActionTag == InGraph->EntryActionTag; })
	)
	{
		return ECadenceArcResolverInitResult::EntryNodeNotFound;
	}
	TArray<FText> ValidationErrors;
	TArray<FText> ValidationWarnings;
	const bool bValidGraph = InGraph->ValidateGraph(ValidationErrors, &ValidationWarnings);
	for (const FText& Warning : ValidationWarnings)
	{
		UE_LOG(LogCadenceArc, Warning, TEXT("Graph validation warning: %s"), *Warning.ToString());
	}
	if (!bValidGraph)
	{
		// print validation errors to log for debugging
		for (const FText& Error : ValidationErrors)
		{
			UE_LOG(LogCadenceArc, Error, TEXT("Graph validation error: %s"), *Error.ToString());
		}
		return ECadenceArcResolverInitResult::InvalidGraph;
	}
	Graph = InGraph;
	LastCompletionTimestampSeconds = -1.0;
	State = ECadenceArcResolverState::Ready;
	CommitNode(InGraph->EntryActionTag); // 新的执行上下文：旧资格即使残留也不再匹配
	OutstandingRequest = FCadenceArcActionRequest{};
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcResolverInitResult::Success;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::SubmitInputImpl(const FCadenceArcInputEvent& InInputEvent)
{
	FCadenceArcSubmitOutcome Outcome;
	Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized); // 悲观默认值,每条分支都会覆盖它

	if (!IsInitialized())
	{
		return Outcome;
	}
	if (const ECadenceArcResolutionReason Invalid = ValidateInputEvent(InInputEvent);
		Invalid != ECadenceArcResolutionReason::None)
	{
		Outcome.SetRejected(Invalid);
		return Outcome;
	}
	// 松手事件必须带着按下时的 Token，通过 ReleaseInputHold 提交
	if (InInputEvent.InputPhase == ECadenceArcInputPhase::Released)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InputIdentityRequired);
		return Outcome;
	}
	// 状态与已有资格的准入检查；本来就不接收输入的状态保持 Phase 5 的原有拒绝理由
	if (const ECadenceArcResolutionReason Admission = CheckInputAdmission(InInputEvent.TimestampSeconds);
		Admission != ECadenceArcResolutionReason::None)
	{
		return MakeFailedOutcome(Admission);
	}

	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		{
			const FGameplayTag Source = GetFreshInputSourceActionTag(InInputEvent.TimestampSeconds);
			const double Pause = GetPauseDurationSeconds(InInputEvent);
			if (Source != CurrentActionTag)
			{
				RecordEntryRecovery(true, CurrentActionTag, InInputEvent, Pause);
			}
			const auto Match = MatchInputWithFallback(Source, InInputEvent, Pause);
			if (Match.Reason == ECadenceArcResolutionReason::None)
			{
				// 只有真正被接受的输入才替换槽：解析失败时 Holding 资格原样保留
				ClearInputSlot();
				Outcome.SetRequestProduced(CommitRequest(InInputEvent.InputTag, Match.TargetActionTag, Match.SourceActionTag));
				CheckSlotInvariants();
			}
			else
			{
				Outcome = MakeFailedOutcome(Match.Reason);
			}
			return Outcome;
		}
	case ECadenceArcResolverState::Executing:
		// 开窗已在上面确认；存入缓冲即视为被接受，可以替换 Holding 资格或上一条缓存
		InputSlot = FCadenceArcInputSlot{};
		InputSlot.SlotState = ECadenceArcInputSlotState::BufferedEvent;
		InputSlot.InputEvent = InInputEvent;
		Outcome.SetBuffered();
		CheckSlotInvariants();
		return Outcome;
	default:
		return Outcome; // 理论不可达,保留悲观默认
	}
}

FCadenceArcActionRequest UCadenceArcResolver::CommitRequest(const FGameplayTag& InputTag,
                                                            const FGameplayTag& TargetActionTag,
                                                            const FGameplayTag& SourceActionTag)
{
	FCadenceArcActionRequest NewRequest;
	NewRequest.RequestId = NextRequestId++;
	NewRequest.InputTag = InputTag;
	NewRequest.SourceActionTag = SourceActionTag;
	NewRequest.TargetActionTag = TargetActionTag;
	OutstandingRequest = NewRequest;
	State = ECadenceArcResolverState::AwaitingStart;
	return NewRequest;
}

ECadenceArcHandshakeResult UCadenceArcResolver::ValidateHandshake(
	const int64 InRequestId,
	const ECadenceArcResolverState ExpectedState
) const
{
	if (!IsInitialized())
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	if (InRequestId <= 0)
	{
		return ECadenceArcHandshakeResult::InvalidRequestId;
	}
	if (State != ExpectedState)
	{
		return ECadenceArcHandshakeResult::UnexpectedState;
	}
	if (OutstandingRequest.RequestId != InRequestId)
	{
		return ECadenceArcHandshakeResult::RequestIdMismatch;
	}
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::SetBufferWindowState(const int64 InRequestId, const bool bShouldOpen)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	bIsBufferWindowOpen = bShouldOpen;
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcResolutionReason UCadenceArcResolver::ValidateInputEvent(const FCadenceArcInputEvent& Event)
{
	if (!Event.InputTag.IsValid())
	{
		return ECadenceArcResolutionReason::InvalidInputTag;
	}
	if (!Event.IsValidTimestamp())
	{
		return ECadenceArcResolutionReason::InvalidTimestamp;
	}
	// Tag 和时间已经单独检查过，这里 IsValid() 失败只可能是 Phase 或 HeldDuration 的问题
	if (!Event.IsValid())
	{
		return ECadenceArcResolutionReason::InvalidInputEvent;
	}
	return ECadenceArcResolutionReason::None;
}

// 返回 None 表示这个状态可以接收输入；否则调用方直接用这个原因拒绝
ECadenceArcResolutionReason UCadenceArcResolver::CheckInputAdmission(const double NowSeconds) const
{
	if (!IsInitialized())
	{
		return ECadenceArcResolutionReason::NotInitialized;
	}
	// AwaitingStart：候选请求还等着宿主答复，当前节点马上要变
	if (State == ECadenceArcResolverState::AwaitingStart)
	{
		return ECadenceArcResolutionReason::RequestPending;
	}
	// Executing 必须开窗；Ready 随时可以接收
	if (State == ECadenceArcResolverState::Executing && !bIsBufferWindowOpen)
	{
		return ECadenceArcResolutionReason::BufferWindowClosed;
	}
	// 已有按住资格时才谈冲突：时间倒退、漏 Advance、Charging／Charged 保护
	return CheckPendingHoldConflict(NowSeconds);
}

FCadenceArcSubmitOutcome UCadenceArcResolver::MakeFailedOutcome(const ECadenceArcResolutionReason Reason)
{
	FCadenceArcSubmitOutcome Outcome;
	if (CategoryOf(Reason) == ECadenceArcResolutionCategory::NoAction)
	{
		Outcome.SetNoAction(Reason);
	}
	else
	{
		Outcome.SetRejected(Reason);
	}
	return Outcome;
}

void UCadenceArcResolver::CommitNode(const FGameplayTag& NewActionTag)
{
	CurrentActionTag = NewActionTag;
	// 节点一变就是一次新执行：旧资格即使还留在槽里，上下文编号也已经对不上了
	++CurrentContextId;
}

ECadenceArcHandshakeResult UCadenceArcResolver::EndAction(
	const int64 InRequestId, const ECadenceArcResolverState ExpectedState, const bool bReturnToEntry)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(InRequestId, ExpectedState);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	// 退回入口是一次真正的节点变更，所以也换上下文；Rejected 保留原节点和原上下文
	if (bReturnToEntry)
	{
		CommitNode(Graph->EntryActionTag);
		LastCompletionTimestampSeconds = -1.0;
	}
	ClearInputSlot();
	ResetBufferWindow();
	CheckSlotInvariants();
	return ECadenceArcHandshakeResult::Success;
}

// 返回 None 表示可以继续；否则调用方直接用这个原因拒绝
ECadenceArcResolutionReason UCadenceArcResolver::CheckPendingHoldConflict(const double NowSeconds) const
{
	// 只有待松手资格才谈得上冲突。Empty 无输入；BufferedEvent 按 Last Input Wins 允许被替换。
	if (InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold)
	{
		return ECadenceArcResolutionReason::None;
	}

	// 时间倒退先拦：用过时的时刻判断保护，会让旧时间戳绕过已经观察到的 Charging。
	if (NowSeconds < InputSlot.LastObservedTimestampSeconds)
	{
		return ECadenceArcResolutionReason::InvalidTimestamp;
	}

	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		InputSlot.InputEvent.TimestampSeconds, InputSlot.bHasChargeConfig,
		InputSlot.ChargeConfig, InputSlot.ChargeFullSeconds);

	// 无整体计时配置：有资格但不保护、不到期，新输入可以直接替换它。
	if (!Timeline.bHasCharge)
	{
		return ECadenceArcResolutionReason::None;
	}

	// 到期优先于保护：有一次自动释放还没被处理，宿主必须先 Advance 并处理它的结果。
	if (NowSeconds >= Timeline.AutoReleaseTime)
	{
		return ECadenceArcResolutionReason::InputTimeAdvanceRequired;
	}

	// Charging 或 Charged 都受保护；Holding 允许被真正接受的新输入替换。
	if (CadenceArc::HoldTiming::StageAt(Timeline, NowSeconds) != ECadenceArcHoldStage::Holding)
	{
		return ECadenceArcResolutionReason::HoldProtected;
	}

	return ECadenceArcResolutionReason::None;
}

void UCadenceArcResolver::ClearInputSlot()
{
	InputSlot = FCadenceArcInputSlot{};
}

void UCadenceArcResolver::ResetBufferWindow()
{
	bIsBufferWindowOpen = false;
}

void UCadenceArcResolver::CheckSlotInvariants() const
{
	// BufferedEvent 只会在 Executing 期间存在；Ready 下的输入都在调用内同步消费完
	ensureMsgf(InputSlot.SlotState != ECadenceArcInputSlotState::BufferedEvent
	           || State == ECadenceArcResolverState::Executing,
	           TEXT("Buffered event exists outside Executing (State=%d)"), static_cast<int32>(State));

	// AwaitingStart 时槽必然为空
	ensureMsgf(State != ECadenceArcResolverState::AwaitingStart
	           || InputSlot.SlotState == ECadenceArcInputSlotState::Empty,
	           TEXT("Input slot is not empty while AwaitingStart"));
}

ECadenceArcResolverResetResult UCadenceArcResolver::ResetImpl()
{
	if (!IsInitialized())
	{
		return ECadenceArcResolverResetResult::NotInitialized;
	}
	// Reset Can't interrupt an ongoing action, so only allow reset when the resolver is ready.
	// otherwise the current action will be lost and the resolver will be in an inconsistent state.
	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		OutstandingRequest = FCadenceArcActionRequest{};
		LastCompletionTimestampSeconds = -1.0;
		CommitNode(Graph->EntryActionTag);
		ClearInputSlot();
		ResetBufferWindow();
		break;
	case ECadenceArcResolverState::AwaitingStart:
	case ECadenceArcResolverState::Executing:
	default:
		return ECadenceArcResolverResetResult::Busy;
	}
	return ECadenceArcResolverResetResult::Success;
}

bool UCadenceArcResolver::IsInitialized() const
{
	return IsValid(Graph) && State != ECadenceArcResolverState::Uninitialized;
}

FGameplayTag UCadenceArcResolver::GetBufferedInputTag() const
{
	return InputSlot.SlotState == ECadenceArcInputSlotState::BufferedEvent
		       ? InputSlot.InputEvent.InputTag
		       : FGameplayTag::EmptyTag;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionStartedImpl(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::AwaitingStart);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Executing;
	CommitNode(OutstandingRequest.TargetActionTag);
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionRejectedImpl(const int64 InRequestId)
{
	// 候选没被接受，已提交节点从头到尾没变过，所以不换上下文
	return EndAction(InRequestId, ECadenceArcResolverState::AwaitingStart, /*bReturnToEntry=*/false);
}

FCadenceArcActionCompletionOutcome UCadenceArcResolver::NotifyActionCompletedImpl(
	const int64 InRequestId, const double CompletionTimestampSeconds
)
{
	FCadenceArcActionCompletionOutcome Outcome;
	// Validate handshake 
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		Outcome.SetHandshakeRejected(HandshakeResult);
		return Outcome;
	}

	// 待松手资格跨 Completed 保留。这条分支的所有拒绝都发生在写入任何状态之前，
	// 不能借"握手成功但消费失败"来掩盖已经改掉的状态。
	if (InputSlot.SlotState == ECadenceArcInputSlotState::PendingHold)
	{
		if (!FMath::IsFinite(CompletionTimestampSeconds) || CompletionTimestampSeconds < 0.0
			|| CompletionTimestampSeconds < InputSlot.LastObservedTimestampSeconds)
		{
			Outcome.SetHandshakeRejected(ECadenceArcHandshakeResult::InvalidCompletionTime);
			return Outcome;
		}

		const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
			InputSlot.InputEvent.TimestampSeconds, InputSlot.bHasChargeConfig,
			InputSlot.ChargeConfig, InputSlot.ChargeFullSeconds);
		// 还有一次到期释放没被处理：先 AdvanceInputTime 并处理它的结果再重试，
		// 否则这次 Completed 会把本该产生的蓄力攻击悄悄吞掉。
		if (Timeline.bHasCharge && CompletionTimestampSeconds >= Timeline.AutoReleaseTime)
		{
			Outcome.SetHandshakeRejected(ECadenceArcHandshakeResult::InputTimeAdvanceRequired);
			return Outcome;
		}

		// 结束旧执行但保留资格：正常 Completed 不递增上下文编号，蓄力继续计时
		ResetBufferWindow();
		OutstandingRequest = FCadenceArcActionRequest{};
		State = ECadenceArcResolverState::Ready;
		InputSlot.LastObservedTimestampSeconds = CompletionTimestampSeconds;
		RecordCompletionTimestamp(CompletionTimestampSeconds);
		Outcome.SetBufferConsumption(
			ECadenceArcResolutionCategory::NoAction,
			ECadenceArcResolutionReason::WaitingForRelease);
		CheckSlotInvariants();
		return Outcome;
	}

	// 按住释放产生的缓冲事件：完成时间错误必须在写状态之前拒绝，不能借
	// "握手成功但消费失败"让宿主以为动作还没结束。普通 Pressed 缓存保留 Phase 5 原语义。
	if (InputSlot.SlotState == ECadenceArcInputSlotState::BufferedEvent && InputSlot.bFromHold
		&& (!FMath::IsFinite(CompletionTimestampSeconds) || CompletionTimestampSeconds < 0.0
			|| CompletionTimestampSeconds < InputSlot.LastObservedTimestampSeconds))
	{
		Outcome.SetHandshakeRejected(ECadenceArcHandshakeResult::InvalidCompletionTime);
		return Outcome;
	}

	// Save a copy of the buffered input event before clearing it
	const FCadenceArcInputSlot SlotCopy = InputSlot;
	const double BufferedInputAgeSeconds = CompletionTimestampSeconds - InputSlot.InputEvent.TimestampSeconds;
	// Clear the buffered input event and reset the outstanding request
	ClearInputSlot();
	ResetBufferWindow();
	OutstandingRequest = FCadenceArcActionRequest{};
	Outcome.HandshakeResult = ECadenceArcHandshakeResult::Success;
	State = ECadenceArcResolverState::Ready;
	RecordCompletionTimestamp(CompletionTimestampSeconds);

	// 判断是否存在缓冲
	if (SlotCopy.SlotState != ECadenceArcInputSlotState::BufferedEvent)
	{
		Outcome.SetBufferConsumption(
			ECadenceArcResolutionCategory::NoAction,
			ECadenceArcResolutionReason::NoBufferedInput);
		return Outcome;
	}
	if (!SlotCopy.InputEvent.IsValidTimestamp() ||
		!FMath::IsFinite(CompletionTimestampSeconds) ||
		CompletionTimestampSeconds < 0.0 ||
		CompletionTimestampSeconds < SlotCopy.InputEvent.TimestampSeconds)
	{
		Outcome.SetBufferConsumption(
			ECadenceArcResolutionCategory::Rejected,
			ECadenceArcResolutionReason::InvalidCompletionTime);
		return Outcome;
	}
	// 按住松手产生的事件：用授予时冻结的边副本、上下文与 MaxAge 消费，不按当前图选边
	if (SlotCopy.bFromHold)
	{
		const FCadenceArcSubmitOutcome Resolution =
			ConsumeHoldRelease(SlotCopy, SlotCopy.InputEvent, CompletionTimestampSeconds);
		Outcome.SetBufferConsumption(
			Resolution.GetCategory(), Resolution.GetReason(), Resolution.GetActionRequest());
		return Outcome;
	}
	if (Graph->MaxBufferedInputAgeSeconds > 0.0 && BufferedInputAgeSeconds > Graph->MaxBufferedInputAgeSeconds)
	{
		Outcome.SetBufferConsumption(ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired);
		return Outcome;
	}
	const auto Match = MatchInputWithFallback(CurrentActionTag, SlotCopy.InputEvent, 0.0);
	FCadenceArcActionRequest NewRequest; // 非 None 时保持空请求
	if (Match.Reason == ECadenceArcResolutionReason::None)
	{
		NewRequest = CommitRequest(SlotCopy.InputEvent.InputTag, Match.TargetActionTag, Match.SourceActionTag); // 内部已设 AwaitingStart
	}
	Outcome.SetBufferConsumption(CategoryOf(Match.Reason), Match.Reason, NewRequest);
	return Outcome;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionCancelledImpl(const int64 InRequestId)
{
	// 连招被打断，退回入口重新开始，旧资格随上下文一起作废
	return EndAction(InRequestId, ECadenceArcResolverState::Executing, /*bReturnToEntry=*/true);
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionInterruptedImpl(const int64 InRequestId)
{
	// 与 Cancelled 的对外行为相同；保留两个入口是为了让宿主表达不同的语义来源
	return EndAction(InRequestId, ECadenceArcResolverState::Executing, /*bReturnToEntry=*/true);
}
