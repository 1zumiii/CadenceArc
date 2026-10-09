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

	// Completed 后空闲达到此秒数时恢复入口；0 表示禁用，必须为有限非负值。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	double ComboResetSeconds = 1.0;

	// 非入口节点没有匹配转移时，允许用同一次输入尝试入口；默认开启。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Graph")
	bool bFallbackToEntryOnNoMatch = true;

	// Runtime和编辑器共用的只读校验入口；每次清空输出，警告不影响返回的合法性。
	bool ValidateGraph(TArray<FText>& OutErrors, TArray<FText>* OutWarnings = nullptr) const;
	
	// 检查某个节点是否存在于图中；不检查节点内部配置
	bool ContainsAction(const FGameplayTag& GameplayTag) const;
	const FCadenceArcNode* FindAction(const FGameplayTag& ActionTag) const;

	

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
