#include "Resolver/CadenceArcResolver.h"
#include "Graph/CadenceArcGraph.h"

ECadenceArcResolverInitResult UCadenceArcResolver::Initialize(UCadenceArcGraph* InGraph)
{
	if (State == ECadenceArcResolverState::AwaitingStart || State == ECadenceArcResolverState::Executing)
	{
		return ECadenceArcResolverInitResult::Busy;
	}
	if (!IsValid(InGraph))
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

ECadenceArcInputResult UCadenceArcResolver::SubmitInput(
	const FGameplayTag& InInputTag,
	FCadenceArcActionRequest& OutActionRequest
)
{
	OutActionRequest = FCadenceArcActionRequest{};
	if (State == ECadenceArcResolverState::Uninitialized)
	{
		return ECadenceArcInputResult::NotInitialized;
	}

	if (!InInputTag.IsValid())
	{
		return ECadenceArcInputResult::InvalidInputTag;
	}

	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		return ResolveInput(InInputTag, OutActionRequest);
	case ECadenceArcResolverState::AwaitingStart:
		return ECadenceArcInputResult::RequestPending;
	case ECadenceArcResolverState::Executing:
		if (bIsBufferWindowOpen)
		{
			BufferedInputTag = InInputTag;
			return ECadenceArcInputResult::Buffered;
		}
		return ECadenceArcInputResult::BufferWindowClosed;
	default:
		return ECadenceArcInputResult::NotInitialized;
	}
}

ECadenceArcInputResult UCadenceArcResolver::ResolveInput(
	const FGameplayTag& InInputTag, FCadenceArcActionRequest& OutActionRequest
)
{
	if (!IsInitialized())
	{
		return ECadenceArcInputResult::NotInitialized;
	}
	if (!InInputTag.IsValid())
	{
		return ECadenceArcInputResult::InvalidInputTag;
	}
	const FCadenceArcNode* CurrentNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentActionTag; }
	);
	if (!CurrentNode)
	{
		return ECadenceArcInputResult::CurrentNodeNotFound;
	}
	const FCadenceArcTransition* CurrentTransition = CurrentNode->Transitions.FindByPredicate(
		[&](const FCadenceArcTransition& Transition) { return Transition.InputTag == InInputTag; }
	);
	if (!CurrentTransition)
	{
		return ECadenceArcInputResult::NoMatchingTransition;
	}
	const FCadenceArcNode* TargetNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentTransition->TargetActionTag; }
	);
	if (!TargetNode)
	{
		return ECadenceArcInputResult::TargetNodeNotFound;
	}
	OutstandingRequest = FCadenceArcActionRequest{
		NextRequestId++,
		InInputTag,
		CurrentActionTag,
		TargetNode->ActionTag
	};
	OutActionRequest = OutstandingRequest;
	State = ECadenceArcResolverState::AwaitingStart;
	return ECadenceArcInputResult::Success;
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
	BufferedInputTag = FGameplayTag::EmptyTag;
}


bool UCadenceArcResolver::Reset()
{
	if (!IsInitialized())
	{
		return false;
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
		return false;
	}
	return true;
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

FCadenceArcActionCompletionOutcome UCadenceArcResolver::NotifyActionCompleted(const int64 InRequestId)
{
	FCadenceArcActionCompletionOutcome Outcome;
	const FGameplayTag InputToConsume = GetBufferedInputTag();
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		Outcome.HandshakeResult = HandshakeResult;
		return Outcome;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	ClearInputBuffer();
	if (!InputToConsume.IsValid())
	{
		Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::NoBufferedInput;
		Outcome.HandshakeResult = ECadenceArcHandshakeResult::Success;
		return Outcome;
	}
	const ECadenceArcInputResult Result = ResolveInput(InputToConsume, Outcome.NextActionRequest);
	if (Result == ECadenceArcInputResult::Success)
	{
		Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::Resolved;
		State = ECadenceArcResolverState::AwaitingStart;
	}
	else
	{
		switch (Result)
		{
		case ECadenceArcInputResult::CurrentNodeNotFound:
			Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::CurrentNodeNotFound;
			break;
		case ECadenceArcInputResult::NoMatchingTransition:
			Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::NoMatchingTransition;
			break;
		case ECadenceArcInputResult::TargetNodeNotFound:
			Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::TargetNodeNotFound;
			break;
		default:
			Outcome.BufferConsumeResult = ECadenceArcBufferConsumeResult::UnexpectedResult;
			break;
		}
	}
	Outcome.HandshakeResult = ECadenceArcHandshakeResult::Success;
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
