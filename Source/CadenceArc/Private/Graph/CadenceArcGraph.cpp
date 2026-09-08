#include "Graph/CadenceArcGraph.h"


#if WITH_EDITOR
#include "Misc/DataValidation.h"

EDataValidationResult UCadenceArcGraph::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = CombineDataValidationResults(
		Super::IsDataValid(Context), EDataValidationResult::Valid
	);

	if (Nodes.Num() == 0)
	{
		Context.AddError(FText::FromString(TEXT("Nodes array is empty.")));
		Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
	}

	// 构建 TagMap 并校验节点本身的 ActionTag
	TMap<FGameplayTag, int64> TagMap;
	for (int64 i = 0; i < Nodes.Num(); ++i)
	{
		if (const auto& [ActionTag, Transitions] = Nodes[i]; !ActionTag.IsValid())
		{
			Context.AddError(
				FText::FromString(FString::Printf(TEXT("Node at index %lld has an invalid ActionTag."), i)));
			Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
		}
		else if (const int64* FirstIndex = TagMap.Find(ActionTag))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("Duplicate ActionTag '%s' found at index %lld (first found at index %lld)."),
				*ActionTag.ToString(), i, *FirstIndex)));
			Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
		}
		else
		{
			TagMap.Add(ActionTag, i);
		}
	}

	// 校验图表级属性
	if (!EntryActionTag.IsValid())
	{
		Context.AddError(FText::FromString(TEXT("EntryActionTag is not valid.")));
		Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
	}
	else if (!TagMap.Contains(EntryActionTag))
	{
		Context.AddError(FText::FromString(FString::Printf(
			TEXT("EntryActionTag '%s' does not exist in the nodes."),
			*EntryActionTag.ToString())));
		Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
	}

	if (MaxBufferedInputAgeSeconds < 0.0 || !FMath::IsFinite(MaxBufferedInputAgeSeconds))
	{
		Context.AddError(FText::FromString(TEXT("MaxBufferedInputAgeSeconds must be non-negative and finite.")));
		Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
	}

	// 合并后的 Transitions 校验
	TSet<FGameplayTag> LocalInputTags; // 内存分配移至外层，避免循环内反复申请内存
	for (int64 i = 0; i < Nodes.Num(); ++i)
	{
		const auto& [ActionTag, Transitions] = Nodes[i];
		LocalInputTags.Reset(); // 清空容器但保留底层内存容量

		for (int64 j = 0; j < Transitions.Num(); ++j)
		{
			const auto& [InputTag, TargetActionTag] = Transitions[j];
			if (!InputTag.IsValid())
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Transition at index %lld in node '%s' (index %lld) has an invalid InputTag."),
					j, *ActionTag.ToString(), i)));
				Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
			}
			else
			{
				bool bIsAlreadyInSet = false;
				LocalInputTags.Add(InputTag, &bIsAlreadyInSet);
				if (bIsAlreadyInSet)
				{
					Context.AddError(FText::FromString(FString::Printf(
						TEXT("Node '%s' (index %lld) has multiple transitions with the same InputTag '%s'."),
						*ActionTag.ToString(), i, *InputTag.ToString())));
					Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
				}
			}

			if (!TargetActionTag.IsValid())
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT("Transition at index %lld in node '%s' (index %lld) has an invalid TargetActionTag."),
					j, *ActionTag.ToString(), i)));
				Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
			}
			else if (!TagMap.Contains(TargetActionTag))
			{
				Context.AddError(FText::FromString(FString::Printf(
					TEXT(
						"Transition at index %lld in node '%s' (index %lld) has a TargetActionTag '%s' that does not exist in the nodes."),
					j, *ActionTag.ToString(), i, *TargetActionTag.ToString())));
				Result = CombineDataValidationResults(Result, EDataValidationResult::Invalid);
			}
		}
	}

	return Result;
}
#endif
