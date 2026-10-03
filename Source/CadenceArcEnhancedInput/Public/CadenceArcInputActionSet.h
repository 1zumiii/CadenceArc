#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Input/CadenceArcInputTypes.h"
#include "CadenceArcInputActionSet.generated.h"

class UInputAction;

// 一个 Input Action 对应的 CadenceArc 输入 Tag 和输入方式
USTRUCT(BlueprintType)
struct CADENCEARCENHANCEDINPUT_API FCadenceArcInputActionBinding
{
	GENERATED_BODY()

	// 建议不加触发器，或只加 Down 触发器，见 UCadenceArcInputBinderComponent 的说明
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	TObjectPtr<UInputAction> InputAction;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	ECadenceArcInputMode InputMode = ECadenceArcInputMode::PressOnly;

	bool IsValid() const { return InputAction != nullptr && InputTag.IsValid(); }
};

/**
 * 一组 Input Action 到 CadenceArc 输入 Tag 的映射，由 UCadenceArcInputBinderComponent 绑定。
 * 项目自己的输入配置资产可以继承这个类，再加上 Mapping Context、移动等其他输入。
 */
UCLASS(BlueprintType)
class CADENCEARCENHANCEDINPUT_API UCadenceArcInputActionSet : public UDataAsset
{
	GENERATED_BODY()

public:
	// 每个 Input Action 和每个输入 Tag 只能出现一次
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input", meta=(TitleProperty="InputTag"))
	TArray<FCadenceArcInputActionBinding> InputActions;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
