#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayAbilitySpecHandle.h"
#include "Resolver/CadenceArcResolverEnums.h"
#include "CadenceArcAbilityExecutorComponent.generated.h"

class UAbilitySystemComponent;
class UAnimSequenceBase;
class UCadenceArcComponent;
struct FAbilityEndedData;
struct FCadenceArcActionRequest;
struct FGameplayTag;

/**
 * 用 Gameplay Ability System 执行 CadenceArc 的动作请求：
 * - 收到请求后，激活 AssetTags 中包含请求 TargetActionTag 的已授予 Ability；
 * - 激活成功调用 NotifyActionStarted，激活失败（冷却、消耗、Tag 阻挡等）调用 NotifyActionRejected；
 * - Ability 正常结束调用 NotifyActionCompleted，被取消时调用 NotifyActionInterrupted；
 * - 缓冲窗口由蒙太奇中的 CadenceArc Buffer Window 通知开关，不用蒙太奇时调用 OpenBufferWindow / CloseBufferWindow。
 *
 * Ability 的授予由项目负责。没有匹配的 Ability 或匹配到多个时，请求被拒绝并输出警告。
 * Ability 在激活过程中就结束时（瞬发技能），先确认开始，再报告结束。
 * 一个 CadenceArc 组件只能有一个执行器；使用本组件时，不要再让其他系统处理 OnActionRequested。
 */
UCLASS(ClassGroup=(CadenceArc), meta=(BlueprintSpawnableComponent))
class CADENCEARCGAS_API UCadenceArcAbilityExecutorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCadenceArcAbilityExecutorComponent();

	// 打开当前动作的缓冲窗口。Ability 不使用蒙太奇通知时手动调用。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|GAS")
	ECadenceArcHandshakeResult OpenBufferWindow();

	UFUNCTION(BlueprintCallable, Category="CadenceArc|GAS")
	ECadenceArcHandshakeResult CloseBufferWindow();

	// 供蒙太奇通知使用：开始时按“动画 + 蒙太奇播放实例”记下当前请求编号，结束时用同一个编号关闭。
	// 旧动作的通知迟到时，解析器会按过期回调拒绝，不会关闭新动作的窗口；
	// 两个动作共用同一个蒙太奇资产时，靠播放实例 ID 区分。不在蒙太奇中时传入 INDEX_NONE。
	ECadenceArcHandshakeResult OpenBufferWindowForAnimation(const UAnimSequenceBase* Animation, int32 MontageInstanceId = INDEX_NONE);
	ECadenceArcHandshakeResult CloseBufferWindowForAnimation(const UAnimSequenceBase* Animation, int32 MontageInstanceId = INDEX_NONE);

	// 正在执行的请求编号；没有时为 0
	UFUNCTION(BlueprintPure, Category="CadenceArc|GAS")
	int64 GetActiveRequestId() const { return Active.RequestId; }

	// 指定 Ability System 组件。默认通过 IAbilitySystemInterface 或组件查找从所属 Actor 获取，
	// 每次收到请求时重新获取，以支持 ASC 放在 PlayerState 上、在控制后才初始化的项目。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|GAS")
	void SetAbilitySystemComponent(UAbilitySystemComponent* InAbilitySystem);

	UFUNCTION(BlueprintPure, Category="CadenceArc|GAS")
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	// 指定 CadenceArc 组件。默认使用所属 Actor 上的 UCadenceArcComponent，在 BeginPlay 时绑定。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|GAS")
	void SetCadenceArcComponent(UCadenceArcComponent* InCadenceArc);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FActiveAction
	{
		int64 RequestId = 0;
		FGameplayAbilitySpecHandle AbilityHandle;
	};

	UPROPERTY(Transient)
	TWeakObjectPtr<UCadenceArcComponent> CadenceArc;

	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> ExplicitAbilitySystem;

	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> BoundAbilitySystem;

	FDelegateHandle RequestedHandle;
	FDelegateHandle AbilityEndedHandle;
	FActiveAction Active;

	// TryActivateAbility 执行期间结束的 Ability 先记下，确认开始之后再报告
	bool bActivating = false;
	bool bEndedDuringActivation = false;
	bool bEndedCancelled = false;

	// 键为（动画，蒙太奇播放实例 ID），同一个蒙太奇资产的两次播放各自记录
	TMap<TPair<TWeakObjectPtr<const UAnimSequenceBase>, int32>, int64> WindowRequestIds;

	void BindCadenceArc();
	void UnbindCadenceArc();
	UAbilitySystemComponent* ResolveAbilitySystem();
	void BindAbilitySystem(UAbilitySystemComponent* AbilitySystem);
	int32 FindAbilities(const UAbilitySystemComponent& AbilitySystem, const FGameplayTag& ActionTag,
	                    FGameplayAbilitySpecHandle& OutHandle) const;

	void HandleActionRequested(const FCadenceArcActionRequest& Request);
	void HandleAbilityEnded(const FAbilityEndedData& Data);
	void FinishActive(bool bCancelled);
};
