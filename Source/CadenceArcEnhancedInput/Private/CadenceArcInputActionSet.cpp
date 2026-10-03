#include "CadenceArcInputActionSet.h"

#include "InputAction.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

#define LOCTEXT_NAMESPACE "CadenceArcInputActionSet"

EDataValidationResult UCadenceArcInputActionSet::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<const UInputAction*> SeenActions;
	TSet<FGameplayTag> SeenTags;
	for (int32 Index = 0; Index < InputActions.Num(); ++Index)
	{
		const FCadenceArcInputActionBinding& Binding = InputActions[Index];
		if (!Binding.IsValid())
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvalidBinding", "InputActions[{0}] needs both an Input Action and an input tag."), Index));
			Result = EDataValidationResult::Invalid;
			continue;
		}
		bool bAlreadySeen = false;
		SeenActions.Add(Binding.InputAction, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateAction", "InputActions[{0}]: Input Action {1} is already mapped."),
				Index, FText::FromString(GetNameSafe(Binding.InputAction))));
			Result = EDataValidationResult::Invalid;
		}
		SeenTags.Add(Binding.InputTag, &bAlreadySeen);
		if (bAlreadySeen)
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateTag", "InputActions[{0}]: input tag {1} is already mapped."),
				Index, FText::FromName(Binding.InputTag.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
