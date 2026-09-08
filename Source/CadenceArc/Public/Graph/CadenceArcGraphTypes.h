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

	bool operator ==(const FCadenceArcTransition& Others) const
	{
		return InputTag == Others.InputTag && TargetActionTag == Others.TargetActionTag;
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcNode
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag ActionTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	TArray<FCadenceArcTransition> Transitions;

	bool operator ==(const FCadenceArcNode& Others) const
	{
		return ActionTag == Others.ActionTag && Transitions == Others.Transitions;
	}
};
