#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Input/CadenceArcInputTypes.h"
#include "UObject/Interface.h"
#include "CadenceArcInputContextProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UCadenceArcInputContextProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * 输入事件上下文的提供者，通常由角色实现。UCadenceArcComponent 在按下和手动松开时调用一次，
 * 把返回的 Tag 写进输入事件，例如按键时是否按住“前”。
 * 缓冲消费不会重新采集；自动松手沿用按下时采集的上下文；持久上下文（SetContextTags）仍在解析时读取。
 */
class CADENCEARC_API ICadenceArcInputContextProvider
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="CadenceArc|Input")
	FGameplayTagContainer CollectInputContext(FGameplayTag InputTag, ECadenceArcInputPhase Phase) const;
};
