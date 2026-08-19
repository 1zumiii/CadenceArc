#pragma once
#include "GameplayTagContainer.h"
#include "CadenceArcGraphTypes.generated.h"

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcTransition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag InputTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag TargetActionTag;
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcNode
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag ActionTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	TArray<FCadenceArcTransition> Transitions;
};
