#include "CadenceArcGASTestAbilities.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace CadenceArc::Tests::GAS
{
	UE_DEFINE_GAMEPLAY_TAG(Action_Root, "CadenceArc.Automation.GAS.Action.Root");
	UE_DEFINE_GAMEPLAY_TAG(Action_A, "CadenceArc.Automation.GAS.Action.A");
	UE_DEFINE_GAMEPLAY_TAG(Action_B, "CadenceArc.Automation.GAS.Action.B");
	UE_DEFINE_GAMEPLAY_TAG(Action_Instant, "CadenceArc.Automation.GAS.Action.Instant");
	UE_DEFINE_GAMEPLAY_TAG(Action_Blocked, "CadenceArc.Automation.GAS.Action.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(Action_Missing, "CadenceArc.Automation.GAS.Action.Missing");
	UE_DEFINE_GAMEPLAY_TAG(Input_A, "CadenceArc.Automation.GAS.Input.A");
	UE_DEFINE_GAMEPLAY_TAG(Input_B, "CadenceArc.Automation.GAS.Input.B");
	UE_DEFINE_GAMEPLAY_TAG(Input_Instant, "CadenceArc.Automation.GAS.Input.Instant");
	UE_DEFINE_GAMEPLAY_TAG(Input_Blocked, "CadenceArc.Automation.GAS.Input.Blocked");
	UE_DEFINE_GAMEPLAY_TAG(Input_Missing, "CadenceArc.Automation.GAS.Input.Missing");
}
#define CADENCEARC_TEST_ASSET_TAG(Tag) SetAssetTags(FGameplayTagContainer(CadenceArc::Tests::GAS::Tag))
#else
#define CADENCEARC_TEST_ASSET_TAG(Tag)
#endif

UCadenceArcTestLatentAbility::UCadenceArcTestLatentAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UCadenceArcTestAbilityA::UCadenceArcTestAbilityA()
{
	CADENCEARC_TEST_ASSET_TAG(Action_A);
}

UCadenceArcTestAbilityB::UCadenceArcTestAbilityB()
{
	CADENCEARC_TEST_ASSET_TAG(Action_B);
}

UCadenceArcTestAbilityDuplicateA::UCadenceArcTestAbilityDuplicateA()
{
	CADENCEARC_TEST_ASSET_TAG(Action_A);
}

UCadenceArcTestInstantAbility::UCadenceArcTestInstantAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	CADENCEARC_TEST_ASSET_TAG(Action_Instant);
}

void UCadenceArcTestInstantAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                    const FGameplayAbilityActorInfo* ActorInfo,
                                                    const FGameplayAbilityActivationInfo ActivationInfo,
                                                    const FGameplayEventData* TriggerEventData)
{
	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}

UCadenceArcTestBlockedAbility::UCadenceArcTestBlockedAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	CADENCEARC_TEST_ASSET_TAG(Action_Blocked);
}

bool UCadenceArcTestBlockedAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                       const FGameplayAbilityActorInfo* ActorInfo,
                                                       const FGameplayTagContainer* SourceTags,
                                                       const FGameplayTagContainer* TargetTags,
                                                       FGameplayTagContainer* OptionalRelevantTags) const
{
	return false;
}

#undef CADENCEARC_TEST_ASSET_TAG
