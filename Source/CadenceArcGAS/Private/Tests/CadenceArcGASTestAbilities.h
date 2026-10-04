#pragma once

// CadenceArc.GAS.* 自动化测试使用的 Ability。UHT 不支持把 UCLASS 放在测试宏中，所以类总会编译；
// 资产 Tag 只在 WITH_DEV_AUTOMATION_TESTS 下设置，其他构建中这些类不会匹配任何请求。

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "NativeGameplayTags.h"
#include "CadenceArcGASTestAbilities.generated.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CadenceArc::Tests::GAS
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Root);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_A);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_B);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Instant);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Blocked);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Action_Missing);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_A);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_B);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Instant);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Blocked);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Missing);
}
#endif

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

public:
	UCadenceArcTestAbilityA();
};

UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestAbilityB : public UCadenceArcTestLatentAbility
{
	GENERATED_BODY()

public:
	UCadenceArcTestAbilityB();
};

// 与 A 使用同一个资产 Tag，用来制造“匹配到多个 Ability”
UCLASS(NotBlueprintable, HideDropdown)
class UCadenceArcTestAbilityDuplicateA : public UCadenceArcTestLatentAbility
{
	GENERATED_BODY()

public:
	UCadenceArcTestAbilityDuplicateA();
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
