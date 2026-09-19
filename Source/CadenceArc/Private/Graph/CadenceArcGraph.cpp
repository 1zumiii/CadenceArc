#include "Graph/CadenceArcGraph.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UCadenceArcGraph::ValidateGraph(TArray<FText>& OutErrors) const
{
	OutErrors.Reset();
	const auto AddError = [&OutErrors](const FString& Message)
	{
		OutErrors.Add(FText::FromString(Message));
	};
	if (Nodes.IsEmpty())
	{
		AddError(TEXT("Nodes array is empty."));
	}

	// 先收集节点，允许前向引用、环和自环；Map只用于查找，不决定诊断顺序。
	TMap<FGameplayTag, int32> TagMap;
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const FGameplayTag ActionTag = Nodes[Index].ActionTag;
		if (!ActionTag.IsValid())
		{
			AddError(FString::Printf(TEXT("Node at index %d has an invalid ActionTag."), Index));
		}
		else if (const int32* FirstIndex = TagMap.Find(ActionTag))
		{
			AddError(FString::Printf(TEXT("Duplicate ActionTag '%s' found at index %d (first found at index %d)."),
			                         *ActionTag.ToString(), Index, *FirstIndex));
		}
		else
		{
			TagMap.Add(ActionTag, Index);
		}
	}

	if (!EntryActionTag.IsValid())
	{
		AddError(TEXT("EntryActionTag is not valid."));
	}
	else if (!TagMap.Contains(EntryActionTag))
	{
		AddError(FString::Printf(TEXT("EntryActionTag '%s' does not exist in the nodes."), *EntryActionTag.ToString()));
	}
	if (MaxBufferedInputAgeSeconds < 0.0 || !FMath::IsFinite(MaxBufferedInputAgeSeconds))
	{
		AddError(TEXT("MaxBufferedInputAgeSeconds must be non-negative and finite."));
	}

	TArray<FText> NodeErrors;
	for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
	{
		const FCadenceArcNode& Node = Nodes[NodeIndex];
		// 节点负责内部区间和配置；图负责目标存在性，避免两处维护同一套规则。
		Node.IsValidTransition(&NodeErrors);
		for (const FText& Error : NodeErrors)
		{
			AddError(FString::Printf(TEXT("Node at index %d: %s"), NodeIndex, *Error.ToString()));
		}
		for (int32 EdgeIndex = 0; EdgeIndex < Node.Transitions.Num(); ++EdgeIndex)
		{
			const FGameplayTag Target = Node.Transitions[EdgeIndex].TargetActionTag;
			if (Target.IsValid() && !TagMap.Contains(Target))
			{
				AddError(FString::Printf(
					TEXT(
						"Transition at index %d in node '%s' (index %d) has a TargetActionTag '%s' that does not exist in the nodes."),
					EdgeIndex, *Node.ActionTag.ToString(), NodeIndex, *Target.ToString()));
			}
		}
	}
	return OutErrors.IsEmpty();
}

bool UCadenceArcGraph::ContainsAction(const FGameplayTag& GameplayTag) const
{
	return Nodes.ContainsByPredicate([&](const FCadenceArcNode& Node) { return Node.ActionTag == GameplayTag; });
}

const FCadenceArcNode* UCadenceArcGraph::FindAction(const FGameplayTag& ActionTag) const
{
	return Nodes.FindByPredicate([&](const FCadenceArcNode& Node) { return Node.ActionTag == ActionTag; });
}

#if WITH_EDITOR
EDataValidationResult UCadenceArcGraph::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FText> Errors;
	const bool bValid = ValidateGraph(Errors);
	for (const FText& Error : Errors)
	{
		Context.AddError(Error);
	}
	return CombineDataValidationResults(
		Super::IsDataValid(Context), bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid
	);
}
#endif
