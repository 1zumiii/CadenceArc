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
		case ECadenceArcResolutionReason::NoBufferedInput:
		case ECadenceArcResolutionReason::Expired:
		case ECadenceArcResolutionReason::WaitingForRelease:
			return ECadenceArcResolutionCategory::NoAction;
		default:
			return ECadenceArcResolutionCategory::Rejected; // 新增原因默认悲观
		}
	}

}

ECadenceArcResolverInitResult UCadenceArcResolver::Initialize(UCadenceArcGraph* InGraph)
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
	if (TArray<FText> ValidationErrors; !InGraph->ValidateGraph(ValidationErrors))
	{
		// print validation errors to log for debugging
		for (const FText& Error : ValidationErrors)
		{
			UE_LOG(LogCadenceArc, Error, TEXT("Graph validation error: %s"), *Error.ToString());
		}
		return ECadenceArcResolverInitResult::InvalidGraph;
	}
	Graph = InGraph;
	CurrentActionTag = InGraph->EntryActionTag;
	State = ECadenceArcResolverState::Ready;
	++CurrentContextId; // 新的执行上下文：旧资格即使残留也不再匹配
	OutstandingRequest = FCadenceArcActionRequest{};
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcResolverInitResult::Success;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::SubmitInput(const FCadenceArcInputEvent& InInputEvent)
{
	FCadenceArcSubmitOutcome Outcome;
	Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized); // 悲观默认值,每条分支都会覆盖它

	if (State == ECadenceArcResolverState::Uninitialized)
	{
		return Outcome;
	}
	if (!InInputEvent.InputTag.IsValid())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputTag);
		return Outcome;
	}
	if (!InInputEvent.IsValidTimestamp())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// Tag 和时间前面已经单独检查过，这里 IsValid() 失败只可能是 Phase 或 HeldDuration 的问题
	if (!InInputEvent.IsValid())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}
	// 松手事件必须带着按下时的 Token，通过 ReleaseInputHold 提交
	if (InInputEvent.InputPhase == ECadenceArcInputPhase::Released)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InputIdentityRequired);
		return Outcome;
	}

	// 状态原因排在资格冲突之前：本来就不接收输入的状态保持 Phase 5 的原有拒绝理由
	if (State == ECadenceArcResolverState::AwaitingStart)
	{
		Outcome.SetNoAction(ECadenceArcResolutionReason::RequestPending);
		return Outcome;
	}
	if (State == ECadenceArcResolverState::Executing && !bIsBufferWindowOpen)
	{
		Outcome.SetNoAction(ECadenceArcResolutionReason::BufferWindowClosed);
		return Outcome;
	}

	// 已有按住资格时才谈冲突：时间倒退、漏 Advance、Charging／Charged 保护
	if (const ECadenceArcResolutionReason Conflict = CheckPendingHoldConflict(InInputEvent.TimestampSeconds);
		Conflict != ECadenceArcResolutionReason::None)
	{
		Outcome.SetRejected(Conflict);
		return Outcome;
	}

	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		{
			const auto Match = CadenceArc::GraphQuery::FindUniqueTransition(*Graph, CurrentActionTag, InInputEvent);
			if (Match.Reason == ECadenceArcResolutionReason::None)
			{
				// 只有真正被接受的输入才替换槽：解析失败时 Holding 资格原样保留
				ClearInputSlot();
				Outcome.SetRequestProduced(CommitRequest(InInputEvent.InputTag, Match.TargetActionTag));
				CheckSlotInvariants();
			}
			else if (CategoryOf(Match.Reason) == ECadenceArcResolutionCategory::NoAction)
			{
				Outcome.SetNoAction(Match.Reason);
			}
			else
			{
				Outcome.SetRejected(Match.Reason);
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
                                                            const FGameplayTag& TargetActionTag)
{
	FCadenceArcActionRequest NewRequest;
	NewRequest.RequestId = NextRequestId++;
	NewRequest.InputTag = InputTag;
	NewRequest.SourceActionTag = CurrentActionTag;
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

ECadenceArcResolverResetResult UCadenceArcResolver::Reset()
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
		CurrentActionTag = Graph->EntryActionTag;
		OutstandingRequest = FCadenceArcActionRequest{};
		++CurrentContextId;
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

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionStarted(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::AwaitingStart);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	CurrentActionTag = OutstandingRequest.TargetActionTag;
	State = ECadenceArcResolverState::Executing;
	++CurrentContextId;
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionRejected(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::AwaitingStart);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcHandshakeResult::Success;
}

FCadenceArcActionCompletionOutcome UCadenceArcResolver::NotifyActionCompleted(
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
	const auto Match = CadenceArc::GraphQuery::FindUniqueTransition(*Graph, CurrentActionTag, SlotCopy.InputEvent);
	FCadenceArcActionRequest NewRequest; // 非 None 时保持空请求
	if (Match.Reason == ECadenceArcResolutionReason::None)
	{
		NewRequest = CommitRequest(SlotCopy.InputEvent.InputTag, Match.TargetActionTag); // 内部已设 AwaitingStart
	}
	Outcome.SetBufferConsumption(CategoryOf(Match.Reason), Match.Reason, NewRequest);
	return Outcome;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionCancelled(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	CurrentActionTag = Graph->EntryActionTag;
	++CurrentContextId;
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionInterrupted(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	CurrentActionTag = Graph->EntryActionTag;
	++CurrentContextId;
	ClearInputSlot();
	ResetBufferWindow();
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::OpenBufferWindow(const int64 InRequestId)
{
	return SetBufferWindowState(InRequestId, true);
}

ECadenceArcHandshakeResult UCadenceArcResolver::CloseBufferWindow(const int64 InRequestId)
{
	return SetBufferWindowState(InRequestId, false);
}

FCadenceArcHoldOutcome UCadenceArcResolver::BeginInputHold(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& PressEvent
)
{
	FCadenceArcHoldOutcome Outcome; // 默认 Rejected / None
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}

	// 身份与事件格式：这两项和 Resolver 当前状态无关，所以排在状态判断之前
	if (!Token.IsValid() || !PressEvent.IsValid() || PressEvent.InputPhase != ECadenceArcInputPhase::Pressed)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// AwaitingStart：候选请求还等着宿主答复，当前节点马上要变，资格无法绑定确定的源节点
	if (State == ECadenceArcResolverState::AwaitingStart)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::RequestPending);
		return Outcome;
	}
	// Executing 必须开窗；Ready 随时可申请
	if (State == ECadenceArcResolverState::Executing && !bIsBufferWindowOpen)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::BufferWindowClosed);
		return Outcome;
	}

	// 已有资格的冲突：时间倒退、漏 Advance、蓄力保护
	if (const ECadenceArcResolutionReason Conflict = CheckPendingHoldConflict(PressEvent.TimestampSeconds);
		Conflict != ECadenceArcResolutionReason::None)
	{
		Outcome.SetRejected(Conflict);
		return Outcome;
	}

	// 以下全部在局部变量上完成，任何一步失败都不能动到已有的槽
	const FCadenceArcNode* SourceNode = Graph->FindAction(CurrentActionTag);
	if (!SourceNode)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::CurrentNodeNotFound);
		return Outcome;
	}

	// 该 Tag 一条 Released 边都没有：无论按多久，松手时必然无匹配，当场拒绝
	TArray<FCadenceArcTransition> ReleasedEdges;
	SourceNode->CollectTransitions(PressEvent.InputTag, ECadenceArcInputPhase::Released, ReleasedEdges);
	if (ReleasedEdges.IsEmpty())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingTransition);
		return Outcome;
	}

	// 整体计时配置是可选的；没有配置表示有资格但不保护、不自动释放
	const FCadenceArcHoldChargeConfig* ChargeConfig = SourceNode->FindHoldChargeConfig(PressEvent.InputTag);
	double ChargeFullSeconds = 0.0;
	if (ChargeConfig)
	{
		// Initialize 之后资产仍可被编辑，所以这里按当前数据重新推导一次
		if (!ChargeConfig->IsValid()
			|| !CadenceArc::HoldTiming::DeriveChargeFullSeconds(ReleasedEdges, ChargeFullSeconds)
			|| !(ChargeConfig->ChargeStartSeconds < ChargeFullSeconds))
		{
			Outcome.SetRejected(ECadenceArcResolutionReason::InvalidGraphConfiguration);
			return Outcome;
		}
	}

	// 求和溢出取决于宿主传进来的时间戳，与图无关
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		PressEvent.TimestampSeconds, ChargeConfig != nullptr,
		ChargeConfig ? *ChargeConfig : FCadenceArcHoldChargeConfig{}, ChargeFullSeconds);
	if (!CadenceArc::HoldTiming::HasFiniteBounds(Timeline))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// 全部通过，一次性占槽。资格绑定已提交的源节点与当前上下文编号，不是候选目标
	FCadenceArcInputSlot NewSlot;
	NewSlot.SlotState = ECadenceArcInputSlotState::PendingHold;
	NewSlot.InputToken = Token;
	NewSlot.InputEvent = PressEvent;
	NewSlot.bFromHold = true;
	NewSlot.LastObservedTimestampSeconds = PressEvent.TimestampSeconds;
	NewSlot.SourceActionTag = CurrentActionTag;
	NewSlot.GrantedContextId = CurrentContextId;
	NewSlot.bHasChargeConfig = ChargeConfig != nullptr;
	if (ChargeConfig)
	{
		NewSlot.ChargeConfig = *ChargeConfig;
	}
	NewSlot.ReleasedEdges = MoveTemp(ReleasedEdges);
	NewSlot.MaxBufferedInputAgeSeconds = Graph->MaxBufferedInputAgeSeconds;
	NewSlot.ChargeFullSeconds = ChargeFullSeconds; // 无配置时保持 0 且不参与计算
	InputSlot = MoveTemp(NewSlot);

	CheckSlotInvariants();
	Outcome.SetGranted();
	return Outcome;
}

FCadenceArcHoldSnapshot UCadenceArcResolver::GetInputHoldSnapshot() const
{
	FCadenceArcHoldSnapshot Snapshot;
	// 没有待松手资格时返回空快照；BufferedEvent 不是资格，走 GetBufferedInputTag
	if (InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold)
	{
		return Snapshot;
	}

	Snapshot.bHasHold = true;
	Snapshot.bHasChargeConfig = InputSlot.bHasChargeConfig;
	Snapshot.Token = InputSlot.InputToken;
	Snapshot.InputTag = InputSlot.InputEvent.InputTag;
	Snapshot.SourceActionTag = InputSlot.SourceActionTag;
	Snapshot.PressedTimestampSeconds = InputSlot.InputEvent.TimestampSeconds;
	Snapshot.LastObservedTimestampSeconds = InputSlot.LastObservedTimestampSeconds;

	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		InputSlot.InputEvent.TimestampSeconds, InputSlot.bHasChargeConfig,
		InputSlot.ChargeConfig, InputSlot.ChargeFullSeconds);
	// 阶段按最近一次有效观察时间推导；查询本身不推进时间
	Snapshot.Stage = CadenceArc::HoldTiming::StageAt(Timeline, InputSlot.LastObservedTimestampSeconds);

	// 无配置时这些字段保持 0 且不参与判断，必须先看 bHasChargeConfig
	if (InputSlot.bHasChargeConfig)
	{
		Snapshot.ChargeStartSeconds = InputSlot.ChargeConfig.ChargeStartSeconds;
		Snapshot.ChargeFullSeconds = InputSlot.ChargeFullSeconds;
		Snapshot.MaxChargedHoldSeconds = InputSlot.ChargeConfig.MaxChargedHoldSeconds;
		Snapshot.ChargeFullTimestampSeconds = Timeline.ChargeFullTime;
		Snapshot.AutoReleaseTimestampSeconds = Timeline.AutoReleaseTime;
	}
	return Snapshot;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::ConsumeHoldRelease(
	const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent, const double NowSeconds)
{
	FCadenceArcSubmitOutcome Outcome;
	Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized); // 悲观默认

	// 资格绑定授予时的已提交节点与上下文编号：回到同名节点的另一次执行也不能复用
	if (HoldSlot.SourceActionTag != CurrentActionTag || HoldSlot.GrantedContextId != CurrentContextId)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 年龄从释放时刻算起，用授予时复制的 MaxAge；等于上限仍然有效，0 表示禁用
	if (HoldSlot.MaxBufferedInputAgeSeconds > 0.0
		&& NowSeconds - ReleasedEvent.TimestampSeconds > HoldSlot.MaxBufferedInputAgeSeconds)
	{
		Outcome.SetNoAction(ECadenceArcResolutionReason::Expired);
		return Outcome;
	}

	// 用授予时冻结的边副本匹配：等待期间改资产不会改变本次轻重攻击的解释
	const auto Match = CadenceArc::GraphQuery::FindUniqueTransition(
		*Graph, HoldSlot.SourceActionTag, ReleasedEvent, &HoldSlot.ReleasedEdges);
	if (Match.Reason == ECadenceArcResolutionReason::None)
	{
		Outcome.SetRequestProduced(CommitRequest(ReleasedEvent.InputTag, Match.TargetActionTag));
	}
	else if (CategoryOf(Match.Reason) == ECadenceArcResolutionCategory::NoAction)
	{
		Outcome.SetNoAction(Match.Reason);
	}
	else
	{
		Outcome.SetRejected(Match.Reason);
	}
	return Outcome;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::FinishHoldRelease(
	const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent,
	const double ObservedTimestampSeconds)
{
	// 资格到此终结。即使下面解析不出候选，也不恢复成按住状态
	ClearInputSlot();

	FCadenceArcSubmitOutcome Resolution;
	if (State == ECadenceArcResolverState::Executing)
	{
		// 动作还在执行：存成缓冲事件，等 Completed 消费。不重新检查已经关闭的窗口
		FCadenceArcInputSlot BufferedSlot = HoldSlot;
		BufferedSlot.SlotState = ECadenceArcInputSlotState::BufferedEvent;
		BufferedSlot.InputEvent = ReleasedEvent;
		BufferedSlot.LastObservedTimestampSeconds = ObservedTimestampSeconds;
		InputSlot = MoveTemp(BufferedSlot);
		Resolution.SetBuffered();
	}
	else if (State == ECadenceArcResolverState::Ready)
	{
		Resolution = ConsumeHoldRelease(HoldSlot, ReleasedEvent, ObservedTimestampSeconds);
	}
	else
	{
		// AwaitingStart 下槽必为空，走不到这里；保留悲观结果而不是假装成功
		Resolution.SetNoAction(ECadenceArcResolutionReason::RequestPending);
	}

	CheckSlotInvariants();
	return Resolution;
}

FCadenceArcInputAdvanceOutcome UCadenceArcResolver::ReleaseInputHold(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& ReleaseEvent)
{
	FCadenceArcInputAdvanceOutcome Outcome; // 默认拒绝且空载荷
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}

	// 身份：无效 Token、槽里没有资格、Token 不匹配，对调用方是同一件事
	if (!Token.IsValid()
		|| InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold
		|| !(InputSlot.InputToken == Token))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 事件本身：必须是同一个 Tag 的合法 Released 事件
	if (!ReleaseEvent.IsValid()
		|| ReleaseEvent.InputPhase != ECadenceArcInputPhase::Released
		|| ReleaseEvent.InputTag != InputSlot.InputEvent.InputTag)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// 时间不能早于已观察到的时刻，否则旧时间戳可以绕过已经成立的蓄力保护
	if (ReleaseEvent.TimestampSeconds < InputSlot.LastObservedTimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// duration 必须对应本次按下，不能听调用方随便填；两边用同一次 double 运算
	if (ReleaseEvent.HeldDurationSeconds
		!= ReleaseEvent.TimestampSeconds - InputSlot.InputEvent.TimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// 校验全部通过，从这里开始改状态
	const FCadenceArcInputSlot HoldCopy = InputSlot;
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		HoldCopy.InputEvent.TimestampSeconds, HoldCopy.bHasChargeConfig,
		HoldCopy.ChargeConfig, HoldCopy.ChargeFullSeconds);

	// 物理松手晚于自动截止：按 HoldLimit 释放，事件时刻用截止时刻而不是松手时刻
	FCadenceArcInputEvent EffectiveRelease = ReleaseEvent;
	ECadenceArcInputReleaseSource ReleaseSource = ECadenceArcInputReleaseSource::Manual;
	if (Timeline.bHasCharge && ReleaseEvent.TimestampSeconds >= Timeline.AutoReleaseTime)
	{
		ReleaseSource = ECadenceArcInputReleaseSource::HoldLimit;
		EffectiveRelease.TimestampSeconds = Timeline.AutoReleaseTime;
		EffectiveRelease.HeldDurationSeconds = Timeline.AutoReleaseTime - Timeline.PressedTime;
	}

	Outcome.SetAccepted(Token);

	// 补齐这段时间里跨过的阶段，截止到实际生效的释放时刻
	TArray<CadenceArc::HoldTiming::FStageCrossing> Crossings;
	CadenceArc::HoldTiming::CollectStageCrossings(
		Timeline, HoldCopy.LastObservedTimestampSeconds, EffectiveRelease.TimestampSeconds, Crossings);
	for (const CadenceArc::HoldTiming::FStageCrossing& Crossing : Crossings)
	{
		Outcome.AddStageChange(Crossing.ToStage, Crossing.EffectiveTimestampSeconds);
	}

	// 实际消费与过期判断用真实观察到的时刻，而不是可能被压回截止点的事件时刻
	const FCadenceArcSubmitOutcome Resolution =
		FinishHoldRelease(HoldCopy, EffectiveRelease, ReleaseEvent.TimestampSeconds);

	Outcome.SetRelease(EffectiveRelease, ReleaseSource, Resolution);
	return Outcome;
}


FCadenceArcInputAdvanceOutcome UCadenceArcResolver::AdvanceInputTime(const double NowSeconds)
{
	FCadenceArcInputAdvanceOutcome Outcome; // 默认拒绝且空载荷
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// 没有待松手资格：接受的空操作。不创建输入，也不动普通缓存和窗口
	if (InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold)
	{
		Outcome.SetAccepted(FCadenceArcInputToken{});
		return Outcome;
	}

	// 时间倒退不推进、不产生阶段变化，也不终结资格
	if (NowSeconds < InputSlot.LastObservedTimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	const FCadenceArcInputSlot HoldCopy = InputSlot;
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		HoldCopy.InputEvent.TimestampSeconds, HoldCopy.bHasChargeConfig,
		HoldCopy.ChargeConfig, HoldCopy.ChargeFullSeconds);

	Outcome.SetAccepted(HoldCopy.InputToken);

	// 无整体计时配置时 bHasCharge 为假：既没有阈值可跨，也没有自动截止
	const bool bAutoRelease = Timeline.bHasCharge && NowSeconds >= Timeline.AutoReleaseTime;
	// 一次跨过多个阈值时按时间升序全部报告，但不越过实际生效的释放时刻
	TArray<CadenceArc::HoldTiming::FStageCrossing> Crossings;
	CadenceArc::HoldTiming::CollectStageCrossings(
		Timeline, HoldCopy.LastObservedTimestampSeconds,
		bAutoRelease ? Timeline.AutoReleaseTime : NowSeconds, Crossings);
	for (const CadenceArc::HoldTiming::FStageCrossing& Crossing : Crossings)
	{
		Outcome.AddStageChange(Crossing.ToStage, Crossing.EffectiveTimestampSeconds);
	}

	if (!bAutoRelease)
	{
		// 资格继续有效，只把观察时间推到 Now
		InputSlot.LastObservedTimestampSeconds = NowSeconds;
		CheckSlotInvariants();
		return Outcome;
	}

	// 到达保持上限：合成一次释放，事件时刻用截止时刻，duration = 截止 − 按下
	FCadenceArcInputEvent AutoRelease;
	AutoRelease.InputTag = HoldCopy.InputEvent.InputTag;
	AutoRelease.InputPhase = ECadenceArcInputPhase::Released;
	AutoRelease.TimestampSeconds = Timeline.AutoReleaseTime;
	AutoRelease.HeldDurationSeconds = Timeline.AutoReleaseTime - Timeline.PressedTime;

	// 实际消费与缓存年龄用调用时刻 Now：晚调用一帧就该按晚了一帧算过期
	const FCadenceArcSubmitOutcome Resolution = FinishHoldRelease(HoldCopy, AutoRelease, NowSeconds);
	Outcome.SetRelease(AutoRelease, ECadenceArcInputReleaseSource::HoldLimit, Resolution);
	return Outcome;
}

FCadenceArcHoldOutcome UCadenceArcResolver::CancelInputHold(const FCadenceArcInputToken& Token)
{
	FCadenceArcHoldOutcome Outcome; // 默认 Rejected / None

	// 可取消的只有按住资格本身，以及它释放后尚未消费的缓冲事件；
	// 普通 Pressed 缓存不带身份，不能被取消调用清掉
	const bool bHasCancellableHold =
		InputSlot.SlotState == ECadenceArcInputSlotState::PendingHold
		|| (InputSlot.SlotState == ECadenceArcInputSlotState::BufferedEvent && InputSlot.bFromHold);

	// 无效 Token、没有资格、Token 不匹配，对调用方是同一件事：这个身份没有可取消的资格
	if (!Token.IsValid() || !bHasCancellableHold || !(InputSlot.InputToken == Token))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 取消只清槽：不合成松手事件、不产生候选，也不撤销 AwaitingStart 里已提交的请求
	ClearInputSlot();
	CheckSlotInvariants();
	Outcome.SetCancelled();
	return Outcome;
}
