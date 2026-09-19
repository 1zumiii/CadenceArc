#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "CadenceArcGraphTypes.h"
#include "CadenceArcGraph.generated.h"

/**
 * Data asset containing the entry action and deterministic tag-driven transitions.
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

	// 0.0 means disable buffered input age check, otherwise the resolver will reject buffered inputs older than this value in seconds
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	double MaxBufferedInputAgeSeconds = 0.0;

	// Runtime和编辑器共用的只读校验入口；每次先清空OutErrors。
	bool ValidateGraph(TArray<FText>& OutErrors) const;
	
	// 检查某个节点是否存在于图中；不检查节点内部配置
	bool ContainsAction(const FGameplayTag& GameplayTag) const;
	const FCadenceArcNode* FindAction(const FGameplayTag& ActionTag) const;

	

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
