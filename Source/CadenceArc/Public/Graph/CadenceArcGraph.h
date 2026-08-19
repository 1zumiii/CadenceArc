// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CadenceArcGraphTypes.h"
#include "CadenceArcGraph.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class CADENCEARC_API UCadenceArcGraph : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	FGameplayTag EntryActionTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	TArray<FCadenceArcNode> Nodes;
};
