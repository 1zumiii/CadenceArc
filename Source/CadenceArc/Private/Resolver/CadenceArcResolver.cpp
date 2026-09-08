#include "Resolver/CadenceArcResolver.h"
#include "Graph/CadenceArcGraph.h"

ECadenceArcResolverInitResult UCadenceArcResolver::Initialize(UCadenceArcGraph* InGraph)
{
	if (State == ECadenceArcResolverState::AwaitingStart || State == ECadenceArcResolverState::Executing)
	{
		return ECadenceArcResolverInitResult::UnexpectedState;
	}
	if (!IsValid(InGraph) || InGraph->MaxBufferedInputAgeSeconds < 0.0 || !FMath::IsFinite(
		InGraph->MaxBufferedInputAgeSeconds))
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
	Graph = InGraph;
	CurrentActionTag = InGraph->EntryActionTag;
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	ClearInputBuffer();
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
			FCadenceArcActionRequest NewRequest;
			const ECadenceArcResolveResult Result = ResolveInput(InInputEvent.InputTag, NewRequest);
			ECadenceArcResolutionCategory Category;
			ECadenceArcResolutionReason Reason;
			TranslateResolveResult(Result, Category, Reason);
			switch (Category)
			{
			case ECadenceArcResolutionCategory::RequestProduced:
				Outcome.SetRequestProduced(NewRequest);
				break;
			case ECadenceArcResolutionCategory::NoAction:
				Outcome.SetNoAction(Reason);
				break;
			default: // Rejected
				Outcome.SetRejected(Reason);
				break;
			}
			return Outcome;
		}
	case ECadenceArcResolverState::AwaitingStart:
		Outcome.SetNoAction(ECadenceArcResolutionReason::RequestPending);
		return Outcome;
	case ECadenceArcResolverState::Executing:
		if (bIsBufferWindowOpen)
		{
			BufferedInputEvent = InInputEvent;
			Outcome.SetBuffered();
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

ECadenceArcResolveResult UCadenceArcResolver::ResolveInput(
	const FGameplayTag& InInputTag, FCadenceArcActionRequest& OutActionRequest
)
{
	if (!IsInitialized())
	{
		return ECadenceArcResolveResult::NotInitialized;
	}
	if (!InInputTag.IsValid())
	{
		return ECadenceArcResolveResult::InvalidInputTag;
	}
	const FCadenceArcNode* CurrentNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentActionTag; }
	);
	if (!CurrentNode)
	{
		return ECadenceArcResolveResult::CurrentNodeNotFound;
	}
	const FCadenceArcTransition* CurrentTransition = CurrentNode->Transitions.FindByPredicate(
		[&](const FCadenceArcTransition& Transition) { return Transition.InputTag == InInputTag; }
	);
	if (!CurrentTransition)
	{
		return ECadenceArcResolveResult::NoMatchingTransition;
	}
	const FCadenceArcNode* TargetNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentTransition->TargetActionTag; }
	);
	if (!TargetNode)
	{
		return ECadenceArcResolveResult::TargetNodeNotFound;
	}
	OutstandingRequest = FCadenceArcActionRequest{
		.RequestId = NextRequestId++,
		.InputTag = InInputTag,
		.SourceActionTag = CurrentActionTag,
		.TargetActionTag = TargetNode->ActionTag
	};
	OutActionRequest = OutstandingRequest;
	State = ECadenceArcResolverState::AwaitingStart;
	return ECadenceArcResolveResult::Success;
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

void UCadenceArcResolver::ClearInputBuffer()
{
	bIsBufferWindowOpen = false;
	BufferedInputEvent.InputTag = FGameplayTag::EmptyTag;
	BufferedInputEvent.TimestampSeconds = 0.0;
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
		ClearInputBuffer();
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
	ClearInputBuffer();
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
	ClearInputBuffer();
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
	const FCadenceArcInputEvent BufferedEventCopy = BufferedInputEvent;
	const double BufferedInputAgeSeconds = CompletionTimestampSeconds - BufferedInputEvent.TimestampSeconds;
	// Clear the buffered input event and reset the outstanding request
	ClearInputBuffer();
	OutstandingRequest = FCadenceArcActionRequest{};
	bIsBufferWindowOpen = false;
	Outcome.HandshakeResult = ECadenceArcHandshakeResult::Success;
	State = ECadenceArcResolverState::Ready;

	if (!BufferedEventCopy.InputTag.IsValid())
	{
		Outcome.SetBufferConsumption(ECadenceArcResolutionCategory::NoAction,
		                             ECadenceArcResolutionReason::NoBufferedInput);
		return Outcome;
	}
	if (!BufferedEventCopy.IsValidTimestamp() ||
		!FMath::IsFinite(CompletionTimestampSeconds) ||
		CompletionTimestampSeconds < 0.0 ||
		CompletionTimestampSeconds < BufferedEventCopy.TimestampSeconds)
	{
		Outcome.SetBufferConsumption(ECadenceArcResolutionCategory::Rejected,
		                             ECadenceArcResolutionReason::InvalidCompletionTime);
		return Outcome;
	}
	if (Graph->MaxBufferedInputAgeSeconds > 0.0 && BufferedInputAgeSeconds > Graph->MaxBufferedInputAgeSeconds)
	{
		Outcome.SetBufferConsumption(ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired);
		return Outcome;
	}
	FCadenceArcActionRequest NewRequest;
	const ECadenceArcResolveResult Result = ResolveInput(BufferedEventCopy.InputTag, NewRequest);
	if (Result == ECadenceArcResolveResult::Success)
	{
		State = ECadenceArcResolverState::AwaitingStart;
	}

	ECadenceArcResolutionCategory ResolutionCategory;
	ECadenceArcResolutionReason ResolutionReason;
	TranslateResolveResult(Result, ResolutionCategory, ResolutionReason);
	Outcome.SetBufferConsumption(ResolutionCategory, ResolutionReason, NewRequest);
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
	ClearInputBuffer();
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
	ClearInputBuffer();
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

void UCadenceArcResolver::TranslateResolveResult(
	const ECadenceArcResolveResult InResult,
	ECadenceArcResolutionCategory& OutCategory,
	ECadenceArcResolutionReason& OutReason
)
{
	switch (InResult)
	{
	case ECadenceArcResolveResult::Success:
		OutCategory = ECadenceArcResolutionCategory::RequestProduced;
		OutReason = ECadenceArcResolutionReason::None;
		return;
	case ECadenceArcResolveResult::NoMatchingTransition:
		OutCategory = ECadenceArcResolutionCategory::NoAction;
		OutReason = ECadenceArcResolutionReason::NoMatchingTransition;
		return;
	case ECadenceArcResolveResult::CurrentNodeNotFound:
		OutCategory = ECadenceArcResolutionCategory::Rejected;
		OutReason = ECadenceArcResolutionReason::CurrentNodeNotFound;
		return;
	case ECadenceArcResolveResult::TargetNodeNotFound:
		OutCategory = ECadenceArcResolutionCategory::Rejected;
		OutReason = ECadenceArcResolutionReason::TargetNodeNotFound;
		return;
	case ECadenceArcResolveResult::InvalidInputTag:
		OutCategory = ECadenceArcResolutionCategory::Rejected;
		OutReason = ECadenceArcResolutionReason::InvalidInputTag;
		return;
	default: // NotInitialized / InvalidInputTag,不可达但保留防御分支
		OutCategory = ECadenceArcResolutionCategory::Rejected;
		OutReason = ECadenceArcResolutionReason::NotInitialized;
	}
}
