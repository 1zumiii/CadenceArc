#include "CadenceArcBlueprintLibrary.h"

#include "Resolver/CadenceArcResolverTypes.h"

bool UCadenceArcBlueprintLibrary::BP_HasActionRequest(const FCadenceArcSubmitOutcome& Outcome)
{
	return Outcome.HasActionRequest();
}

ECadenceArcResolutionCategory UCadenceArcBlueprintLibrary::BP_GetResolutionCategory(
	const FCadenceArcSubmitOutcome& Outcome
)
{
	return Outcome.GetCategory();
}

ECadenceArcResolutionReason UCadenceArcBlueprintLibrary::BP_GetResolutionReason(const FCadenceArcSubmitOutcome& Outcome)
{
	return Outcome.GetReason();
}

FCadenceArcActionRequest UCadenceArcBlueprintLibrary::BP_GetActionRequest(const FCadenceArcSubmitOutcome& Outcome)
{
	return Outcome.GetActionRequest();
}

FCadenceArcActionRequest UCadenceArcBlueprintLibrary::BP_GetNextActionRequest(
	const FCadenceArcActionCompletionOutcome& Outcome
)
{
	return Outcome.GetNextActionRequest();
}

ECadenceArcResolutionCategory UCadenceArcBlueprintLibrary::BP_GetBufferConsumption(
	const FCadenceArcActionCompletionOutcome& Outcome
)
{
	return Outcome.GetBufferConsumption();
}

ECadenceArcResolutionReason UCadenceArcBlueprintLibrary::BP_GetBufferConsumptionReason(
	const FCadenceArcActionCompletionOutcome& Outcome
)
{
	return Outcome.GetBufferConsumptionReason();
}

ECadenceArcHandshakeResult UCadenceArcBlueprintLibrary::BP_GetHandshakeResult(
	const FCadenceArcActionCompletionOutcome& Outcome
)
{
	return Outcome.GetHandshakeResult();
}

bool UCadenceArcBlueprintLibrary::BP_HasNextActionRequest(const FCadenceArcActionCompletionOutcome& Outcome)
{
	return Outcome.HasNextActionRequest();
}
