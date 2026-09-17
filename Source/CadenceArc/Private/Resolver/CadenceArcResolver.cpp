#include "Resolver/CadenceArcResolver.h"
#include "Graph/CadenceArcGraph.h"

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

	bool EdgeMatches(const FCadenceArcTransition& Edge, const FCadenceArcInputEvent& Event)
	{
		if (Edge.InputTag != Event.InputTag || Edge.InputPhase != Event.InputPhase)
		{
			return false;
		}
		// Pressed 边不看范围（图校验已禁止 Pressed 启用范围）
		return Event.InputPhase == ECadenceArcInputPhase::Pressed
			|| !Edge.bUseDurationRange
			|| Edge.DurationRange.Contains(Event.HeldDurationSeconds);
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

	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		{
			const FTransitionMatch Match = FindUniqueTransition(CurrentActionTag, InInputEvent);
			if (Match.Reason == ECadenceArcResolutionReason::None)
			{
				Outcome.SetRequestProduced(CommitRequest(InInputEvent.InputTag, Match.TargetActionTag));
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
	case ECadenceArcResolverState::AwaitingStart:
		Outcome.SetNoAction(ECadenceArcResolutionReason::RequestPending);
		return Outcome;
	case ECadenceArcResolverState::Executing:
		if (bIsBufferWindowOpen)
		{
			InputSlot = FCadenceArcInputSlot{};
			InputSlot.SlotState = ECadenceArcInputSlotState::BufferedEvent;
			InputSlot.InputEvent = InInputEvent;
			Outcome.SetBuffered();
			CheckSlotInvariants();
		}
		else
		{
			Outcome.SetNoAction(ECadenceArcResolutionReason::BufferWindowClosed);
		}
		return Outcome;
	default:
		return Outcome; // 理论不可达,保留悲观默认
	}
}

UCadenceArcResolver::FTransitionMatch UCadenceArcResolver::FindUniqueTransition(
	const FGameplayTag& SourceActionTag,
	const FCadenceArcInputEvent& Event,
	const TArray<FCadenceArcTransition>* EdgesOverride) const
{
	FTransitionMatch Result; // 默认 NoMatchingTransition

	// 1. 决定用哪组边：按住资格用副本，普通输入读当前图
	const TArray<FCadenceArcTransition>* Edges = EdgesOverride;
	if (!Edges)
	{
		const FCadenceArcNode* SourceNode = Graph->Nodes.FindByPredicate(
			[&](const FCadenceArcNode& Node) { return Node.ActionTag == SourceActionTag; });
		if (!SourceNode)
		{
			Result.Reason = ECadenceArcResolutionReason::CurrentNodeNotFound;
			return Result;
		}
		Edges = &SourceNode->Transitions;
	}

	// 2. 统计候选，不 break
	const FCadenceArcTransition* Matched = nullptr;
	int32 MatchCount = 0;
	for (const FCadenceArcTransition& Edge : *Edges)
	{
		if (EdgeMatches(Edge, Event))
		{
			Matched = &Edge;
			++MatchCount;
		}
	}
	if (MatchCount == 0)
	{
		return Result; // NoMatchingTransition
	}
	if (MatchCount > 1)
	{
		Result.Reason = ECadenceArcResolutionReason::InvalidGraphConfiguration;
		return Result;
	}

	// 3. 目标必须存在于当前图
	const bool bTargetExists = Graph->Nodes.ContainsByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == Matched->TargetActionTag; });
	if (!bTargetExists)
	{
		Result.Reason = ECadenceArcResolutionReason::TargetNodeNotFound;
		return Result;
	}

	Result.Reason = ECadenceArcResolutionReason::None;
	Result.TargetActionTag = Matched->TargetActionTag;
	return Result;
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
	if (Graph->MaxBufferedInputAgeSeconds > 0.0 && BufferedInputAgeSeconds > Graph->MaxBufferedInputAgeSeconds)
	{
		Outcome.SetBufferConsumption(ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired);
		return Outcome;
	}
	const FTransitionMatch Match = FindUniqueTransition(CurrentActionTag, SlotCopy.InputEvent);
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
