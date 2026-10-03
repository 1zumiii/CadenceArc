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
 * 用法：放在 Pawn 上并指定 ActionSet。Pawn 每次被控制并创建输入组件后（Restart），组件自动绑定，C++ 和蓝图都不需要额外调用。
 * 也可以手动调用 BindInputActions，例如在 SetupPlayerInputComponent 中使用另一份 ActionSet。重复调用会先解除上一次绑定。
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

	// 所属 Pawn 每次 Restart 时，自动绑定到它的 Enhanced Input 组件。已经绑定到同一个输入组件时不重复绑定。
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	bool bAutoBind = true;

	// 绑定 ActionSet 中的所有有效条目，返回绑定成功的条目数。InputComponent 为空时使用所属 Actor 的输入组件；
	// InActionSet 不为空时先替换 ActionSet。
	// 目标组件默认是所属 Actor 上的 UCadenceArcComponent，可以用 SetTargetComponent 指定。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Input")
	int32 BindInputActions(UEnhancedInputComponent* InputComponent = nullptr, UCadenceArcInputActionSet* InActionSet = nullptr);

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
	bool IsBound() const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
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

	// bAutoBind 时，在所属 Pawn 已有 Enhanced Input 组件且尚未绑定到它时绑定
	void TryAutoBind();

	UFUNCTION()
	void HandlePawnRestarted(APawn* Pawn);

	UFUNCTION()
	void HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController);
};
