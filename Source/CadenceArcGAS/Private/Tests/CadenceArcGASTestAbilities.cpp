#include "CadenceArcGASTestAbilities.h"

UCadenceArcTestLatentAbility::UCadenceArcTestLatentAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

UCadenceArcTestInstantAbility::UCadenceArcTestInstantAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
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
}

bool UCadenceArcTestBlockedAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                                       const FGameplayAbilityActorInfo* ActorInfo,
                                                       const FGameplayTagContainer* SourceTags,
                                                       const FGameplayTagContainer* TargetTags,
                                                       FGameplayTagContainer* OptionalRelevantTags) const
{
	return false;
}
