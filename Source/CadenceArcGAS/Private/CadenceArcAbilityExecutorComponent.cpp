#include "CadenceArcAbilityExecutorComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/AnimSequenceBase.h"
#include "Component/CadenceArcComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCadenceArcGAS, Log, All);

UCadenceArcAbilityExecutorComponent::UCadenceArcAbilityExecutorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCadenceArcAbilityExecutorComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!CadenceArc.IsValid())
	{
		const AActor* Owner = GetOwner();
		CadenceArc = Owner ? Owner->FindComponentByClass<UCadenceArcComponent>() : nullptr;
	}
	if (!CadenceArc.IsValid())
	{
		UE_LOG(LogCadenceArcGAS, Warning, TEXT("%s: no CadenceArc component to execute for."), *GetPathNameSafe(this));
		return;
	}
	BindCadenceArc();
}

void UCadenceArcAbilityExecutorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindCadenceArc();
	BindAbilitySystem(nullptr);
	Active = FActiveAction();
	WindowRequestIds.Reset();
	Super::EndPlay(EndPlayReason);
}

void UCadenceArcAbilityExecutorComponent::SetCadenceArcComponent(UCadenceArcComponent* InCadenceArc)
{
	if (InCadenceArc == CadenceArc.Get())
	{
		return;
	}
	const bool bWasBound = RequestedHandle.IsValid();
	UnbindCadenceArc();
	CadenceArc = InCadenceArc;
	if (bWasBound || HasBegunPlay())
	{
		BindCadenceArc();
	}
}

void UCadenceArcAbilityExecutorComponent::BindCadenceArc()
{
	if (UCadenceArcComponent* Arc = CadenceArc.Get())
	{
		RequestedHandle = Arc->OnActionRequestedNative.AddUObject(this, &ThisClass::HandleActionRequested);
	}
}

void UCadenceArcAbilityExecutorComponent::UnbindCadenceArc()
{
	if (UCadenceArcComponent* Arc = CadenceArc.Get())
	{
		Arc->OnActionRequestedNative.Remove(RequestedHandle);
	}
	RequestedHandle.Reset();
}

void UCadenceArcAbilityExecutorComponent::SetAbilitySystemComponent(UAbilitySystemComponent* InAbilitySystem)
{
	ExplicitAbilitySystem = InAbilitySystem;
}

UAbilitySystemComponent* UCadenceArcAbilityExecutorComponent::GetAbilitySystemComponent() const
{
	if (UAbilitySystemComponent* AbilitySystem = ExplicitAbilitySystem.Get())
	{
		return AbilitySystem;
	}
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}

UAbilitySystemComponent* UCadenceArcAbilityExecutorComponent::ResolveAbilitySystem()
{
	UAbilitySystemComponent* AbilitySystem = GetAbilitySystemComponent();
	if (AbilitySystem != BoundAbilitySystem.Get())
	{
		BindAbilitySystem(AbilitySystem);
	}
	return AbilitySystem;
}

void UCadenceArcAbilityExecutorComponent::BindAbilitySystem(UAbilitySystemComponent* AbilitySystem)
{
	if (UAbilitySystemComponent* Old = BoundAbilitySystem.Get())
	{
		Old->OnAbilityEnded.Remove(AbilityEndedHandle);
	}
	AbilityEndedHandle.Reset();
	BoundAbilitySystem = AbilitySystem;
	if (AbilitySystem)
	{
		AbilityEndedHandle = AbilitySystem->OnAbilityEnded.AddUObject(this, &ThisClass::HandleAbilityEnded);
	}
}

int32 UCadenceArcAbilityExecutorComponent::FindAbilities(
	const UAbilitySystemComponent& AbilitySystem, const FGameplayTag& ActionTag, FGameplayAbilitySpecHandle& OutHandle) const
{
	int32 Count = 0;
	for (const FGameplayAbilitySpec& Spec : AbilitySystem.GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTagExact(ActionTag))
		{
			OutHandle = Spec.Handle;
			++Count;
		}
	}
	return Count;
}

void UCadenceArcAbilityExecutorComponent::HandleActionRequested(const FCadenceArcActionRequest& Request)
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	if (!Arc)
	{
		return;
	}
	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	if (!AbilitySystem)
	{
		UE_LOG(LogCadenceArcGAS, Warning, TEXT("%s: no Ability System component; request for %s rejected."),
		       *GetPathNameSafe(this), *Request.TargetActionTag.ToString());
		Arc->NotifyActionRejected(Request.RequestId);
		return;
	}

	FGameplayAbilitySpecHandle AbilityHandle;
	const int32 Matches = FindAbilities(*AbilitySystem, Request.TargetActionTag, AbilityHandle);
	if (Matches != 1)
	{
		UE_LOG(LogCadenceArcGAS, Warning, TEXT("%s: %d granted abilities have asset tag %s; request rejected."),
		       *GetPathNameSafe(this), Matches, *Request.TargetActionTag.ToString());
		Arc->NotifyActionRejected(Request.RequestId);
		return;
	}

	Active = FActiveAction{Request.RequestId, AbilityHandle};
	bActivating = true;
	bEndedDuringActivation = false;
	const bool bActivated = AbilitySystem->TryActivateAbility(AbilityHandle);
	bActivating = false;
	if (!bActivated)
	{
		Active = FActiveAction();
		Arc->NotifyActionRejected(Request.RequestId);
		return;
	}

	Arc->NotifyActionStarted(Request.RequestId);
	if (bEndedDuringActivation)
	{
		FinishActive(bEndedCancelled);
	}
}

void UCadenceArcAbilityExecutorComponent::HandleAbilityEnded(const FAbilityEndedData& Data)
{
	if (Active.RequestId == 0 || Data.AbilitySpecHandle != Active.AbilityHandle)
	{
		return;
	}
	if (bActivating)
	{
		bEndedDuringActivation = true;
		bEndedCancelled = Data.bWasCancelled;
		return;
	}
	FinishActive(Data.bWasCancelled);
}

void UCadenceArcAbilityExecutorComponent::FinishActive(const bool bCancelled)
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	const int64 RequestId = Active.RequestId;
	// 先清空：完成回调消费缓冲时，会同步发出下一个请求并重新进入 HandleActionRequested
	Active = FActiveAction();
	if (!Arc)
	{
		return;
	}
	if (bCancelled)
	{
		Arc->NotifyActionInterrupted(RequestId);
	}
	else
	{
		Arc->NotifyActionCompleted(RequestId);
	}
}

ECadenceArcHandshakeResult UCadenceArcAbilityExecutorComponent::OpenBufferWindow()
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	return Arc ? Arc->OpenBufferWindow(Active.RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcAbilityExecutorComponent::CloseBufferWindow()
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	return Arc ? Arc->CloseBufferWindow(Active.RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcAbilityExecutorComponent::OpenBufferWindowForAnimation(
	const UAnimSequenceBase* Animation, const int32 MontageInstanceId)
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	if (!Arc)
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	if (Active.RequestId != 0)
	{
		WindowRequestIds.Add({Animation, MontageInstanceId}, Active.RequestId);
	}
	return Arc->OpenBufferWindow(Active.RequestId);
}

ECadenceArcHandshakeResult UCadenceArcAbilityExecutorComponent::CloseBufferWindowForAnimation(
	const UAnimSequenceBase* Animation, const int32 MontageInstanceId)
{
	UCadenceArcComponent* Arc = CadenceArc.Get();
	if (!Arc)
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	int64 RequestId = 0;
	WindowRequestIds.RemoveAndCopyValue({Animation, MontageInstanceId}, RequestId);
	return Arc->CloseBufferWindow(RequestId);
}
