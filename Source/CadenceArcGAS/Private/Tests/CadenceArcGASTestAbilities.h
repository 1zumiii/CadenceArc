#pragma once

// CadenceArc.GAS.* 自动化测试使用的 Ability。UHT 不支持把 UCLASS 放在测试宏中，所以类总会编译。
// 构造函数中不设置资产 Tag：测试开始时才给 CDO 写入 Tag，避免测试 Tag 在模块加载时注册，
// 出现在使用插件的项目的 Tag 选择器中。其他时候这些类不会匹配任何请求。

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "CadenceArcGASTestAbilities.generated.h"

// 激活后保持运行，直到测试调用 FinishForTest 或取消
UCLASS(Abstract, NotBlueprintable, HideDropdown)
class UCadenceArcTestLatentAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCadenceArcTestLatentAbility();

	void FinishForTest()
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
	}
};

UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestAbilityA : public UCadenceArcTestLatentAbility
{
	GENERATED_BODY()
};

UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestAbilityB : public UCadenceArcTestLatentAbility
{
	GENERATED_BODY()
};

// 与 A 使用同一个资产 Tag，用来制造“匹配到多个 Ability”
UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestAbilityDuplicateA : public UCadenceArcTestLatentAbility
{
	GENERATED_BODY()
};

// 在 ActivateAbility 中立即结束
UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestInstantAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCadenceArcTestInstantAbility();

	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;
};

// CanActivateAbility 总是返回 false，模拟冷却、消耗不足或 Tag 阻挡
UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestBlockedAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UCadenceArcTestBlockedAbility();

	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	                                const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
	                                FGameplayTagContainer* OptionalRelevantTags) const override;
};
