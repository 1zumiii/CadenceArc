// Fill out your copyright notice in the Description page of Project Settings.


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
	return ECadenceArcResolverInitResult::Success;
}

ECadenceArcResolverTransitionResult UCadenceArcResolver::TryResolveInput(
	const FGameplayTag& InInputTag,
	FCadenceArcActionRequest& OutActionRequest
)
{
	OutActionRequest = FCadenceArcActionRequest{};
	switch (State)
	{
	case ECadenceArcResolverState::Ready:
		break;
	case ECadenceArcResolverState::AwaitingStart:
		return ECadenceArcResolverTransitionResult::RequestPending;
	case ECadenceArcResolverState::Executing:
		return ECadenceArcResolverTransitionResult::ActionExecuting;
	case ECadenceArcResolverState::Uninitialized:
	default:
		return ECadenceArcResolverTransitionResult::NotInitialized;
	}
	return ResolveInput(InInputTag, OutActionRequest);
}

ECadenceArcResolverTransitionResult UCadenceArcResolver::ResolveInput(
	const FGameplayTag& InInputTag, FCadenceArcActionRequest& OutActionRequest
)
{
	if (!IsInitialized())
	{
		return ECadenceArcResolverTransitionResult::NotInitialized;
	}
	if (!InInputTag.IsValid())
	{
		return ECadenceArcResolverTransitionResult::InvalidInputTag;
	}
	const FCadenceArcNode* CurrentNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentActionTag; }
	);
	if (!CurrentNode)
	{
		return ECadenceArcResolverTransitionResult::CurrentNodeNotFound;
	}
	const FCadenceArcTransition* CurrentTransition = CurrentNode->Transitions.FindByPredicate(
		[&](const FCadenceArcTransition& Transition) { return Transition.InputTag == InInputTag; }
	);
	if (!CurrentTransition)
	{
		return ECadenceArcResolverTransitionResult::NoMatchingTransition;
	}
	const FCadenceArcNode* TargetNode = Graph->Nodes.FindByPredicate(
		[&](const FCadenceArcNode& Node) { return Node.ActionTag == CurrentTransition->TargetActionTag; }
	);
	if (!TargetNode)
	{
		return ECadenceArcResolverTransitionResult::TargetNodeNotFound;
	}
	OutstandingRequest = FCadenceArcActionRequest{
		NextRequestId++,
		InInputTag,
		CurrentActionTag,
		TargetNode->ActionTag
	};
	OutActionRequest = OutstandingRequest;
	State = ECadenceArcResolverState::AwaitingStart;
	return ECadenceArcResolverTransitionResult::Success;
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

FGameplayTag UCadenceArcResolver::GetCurrentActionTag() const
{
	return CurrentActionTag;
}

ECadenceArcResolverState UCadenceArcResolver::GetState() const
{
	return State;
}

FCadenceArcActionRequest UCadenceArcResolver::GetOutstandingRequest() const
{
	return OutstandingRequest;
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
	return ECadenceArcHandshakeResult::Success;
}

ECadenceArcHandshakeResult UCadenceArcResolver::NotifyActionCompleted(const int64 InRequestId)
{
	const ECadenceArcHandshakeResult HandshakeResult = ValidateHandshake(
		InRequestId, ECadenceArcResolverState::Executing);
	if (HandshakeResult != ECadenceArcHandshakeResult::Success)
	{
		return HandshakeResult;
	}
	State = ECadenceArcResolverState::Ready;
	OutstandingRequest = FCadenceArcActionRequest{};
	return ECadenceArcHandshakeResult::Success;
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
	return ECadenceArcHandshakeResult::Success;
}
