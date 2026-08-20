// Fill out your copyright notice in the Description page of Project Settings.


#include "Resolver/CadenceArcResolver.h"
#include "Graph/CadenceArcGraph.h"

ECadenceArcResolverInitResult UCadenceArcResolver::Initialize(UCadenceArcGraph* InGraph)
{
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
	return ECadenceArcResolverInitResult::Success;
}

ECadenceArcResolverTransitionResult UCadenceArcResolver::TryResolveInput(
	const FGameplayTag& InInputTag,
	FGameplayTag& OutResolvedActionTag
)
{
	OutResolvedActionTag = FGameplayTag::EmptyTag;
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
	CurrentActionTag = TargetNode->ActionTag;
	OutResolvedActionTag = CurrentActionTag;
	return ECadenceArcResolverTransitionResult::Success;
}

bool UCadenceArcResolver::Reset()
{
	if (!IsInitialized())
	{
		return false;
	}
	CurrentActionTag = Graph->EntryActionTag;
	return true;
}

bool UCadenceArcResolver::IsInitialized() const
{
	return IsValid(Graph);
}

FGameplayTag UCadenceArcResolver::GetCurrentActionTag() const
{
	return CurrentActionTag;
}
