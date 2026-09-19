#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "CadenceArcBlueprintLibrary.generated.h"

struct FCadenceArcActionCompletionOutcome;
struct FCadenceArcSubmitOutcome;
/**
 * 
 */
UCLASS()
class CADENCEARC_API UCadenceArcBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static bool BP_HasActionRequest(const FCadenceArcSubmitOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static ECadenceArcResolutionCategory BP_GetResolutionCategory(const FCadenceArcSubmitOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static ECadenceArcResolutionReason BP_GetResolutionReason(const FCadenceArcSubmitOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static FCadenceArcActionRequest BP_GetActionRequest(const FCadenceArcSubmitOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static bool BP_HasNextActionRequest(const FCadenceArcActionCompletionOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static FCadenceArcActionRequest BP_GetNextActionRequest(const FCadenceArcActionCompletionOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static ECadenceArcResolutionCategory BP_GetBufferConsumption(const FCadenceArcActionCompletionOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static ECadenceArcResolutionReason BP_GetBufferConsumptionReason(const FCadenceArcActionCompletionOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static ECadenceArcHandshakeResult BP_GetHandshakeResult(const FCadenceArcActionCompletionOutcome& Outcome);

	// 推进结果的其余字段本身就是 BlueprintReadOnly，这里只补两个推导出来的判断。
	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static bool BP_HasRelease(const FCadenceArcInputAdvanceOutcome& Outcome);

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver|Outcome")
	static bool BP_HasReleasedActionRequest(const FCadenceArcInputAdvanceOutcome& Outcome);
};
