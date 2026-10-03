#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "CadenceArcInputBinderComponent.generated.h"

class AController;
class APawn;
class UCadenceArcComponent;
class UCadenceArcInputActionSet;
class UEnhancedInputComponent;

/**
 * 把 Enhanced Input 的 Input Action 绑定到 UCadenceArcComponent：
 * - Started 调用 PressInput；
 * - Completed 调用 ReleaseInput；
 * - Canceled 调用 CancelInput，不当作松开。
 * 绑定时把 ActionSet 中的输入方式写入 CadenceArc 组件，输入方式只需在 ActionSet 中维护一份。
 *
 * 用法：在 Pawn 的 SetupPlayerInputComponent 中调用 BindInputActions。重复调用会先解除上一次绑定。
 * 控制器变化（例如取消控制 Pawn）时，Pawn 会销毁输入组件，收不到按住中的键的 Completed，
 * 这里会自动取消已绑定的、仍处于按下状态的输入。
 *
 * Input Action 建议不加触发器，或只加 Down 触发器，使 Started 和 Completed 对应物理按下和松开。
 * Pressed 触发器在按下的下一帧就发出 Completed，Hold、Tap、Pulse 等触发器会在松开时发出 Canceled，
 * 都会改变按住时长，按住和蓄力判断交给 CadenceArc 的图配置。
 */
UCLASS(ClassGroup=(CadenceArc), meta=(BlueprintSpawnableComponent))
class CADENCEARCENHANCEDINPUT_API UCadenceArcInputBinderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCadenceArcInputBinderComponent();

	// 要绑定的 Input Action 映射
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	TObjectPtr<UCadenceArcInputActionSet> ActionSet;

	// 绑定 ActionSet 中的所有有效条目，返回绑定成功的条目数。InActionSet 不为空时先替换 ActionSet。
	// 目标组件默认是所属 Actor 上的 UCadenceArcComponent，可以用 SetTargetComponent 指定。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	int32 BindInputActions(UEnhancedInputComponent* InputComponent, UCadenceArcInputActionSet* InActionSet = nullptr);

	// 解除绑定，并取消已绑定的、仍处于按下状态的输入。EndPlay 时自动调用。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void UnbindInputActions();

	// 取消已绑定的、仍处于按下状态的输入，保留绑定。用于失去控制、打开菜单等收不到松开的情况。
	// 其他来源提交到同一个 CadenceArc 组件的输入不受影响。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void CancelBoundInputs();

	// 指定目标 CadenceArc 组件。已经绑定时，先取消旧目标上已绑定的按下输入。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	void SetTargetComponent(UCadenceArcComponent* InTarget);

	// 指定的目标；没有指定时返回所属 Actor 上的 UCadenceArcComponent
	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	UCadenceArcComponent* GetTargetComponent() const;

	UFUNCTION(BlueprintPure, Category="CadenceArc|Input")
	bool IsBound() const { return BindingHandles.Num() > 0; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<UCadenceArcComponent> TargetComponent;

	UPROPERTY(Transient)
	TWeakObjectPtr<UEnhancedInputComponent> BoundInputComponent;

	TArray<uint32> BindingHandles;
	TArray<FGameplayTag> BoundTags;

	void HandleStarted(FGameplayTag InputTag);
	void HandleCompleted(FGameplayTag InputTag);
	void HandleCanceled(FGameplayTag InputTag);

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
};
