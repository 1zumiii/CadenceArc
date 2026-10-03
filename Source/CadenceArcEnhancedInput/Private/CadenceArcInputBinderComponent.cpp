#include "CadenceArcInputBinderComponent.h"

#include "CadenceArcInputActionSet.h"
#include "Component/CadenceArcComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputTriggers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCadenceArcEnhancedInput, Log, All);

UCadenceArcInputBinderComponent::UCadenceArcInputBinderComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UCadenceArcInputBinderComponent::BindInputActions(
	UEnhancedInputComponent* InputComponent, UCadenceArcInputActionSet* InActionSet)
{
	UnbindInputActions();
	if (InActionSet)
	{
		ActionSet = InActionSet;
	}

	UCadenceArcComponent* Target = GetTargetComponent();
	if (!InputComponent || !ActionSet || !Target)
	{
		UE_LOG(LogCadenceArcEnhancedInput, Warning,
		       TEXT("%s: BindInputActions needs an input component, an action set and a CadenceArc component."),
		       *GetPathNameSafe(this));
		return 0;
	}
	TargetComponent = Target;

	TSet<const UInputAction*> BoundActions;
	for (const FCadenceArcInputActionBinding& Binding : ActionSet->InputActions)
	{
		if (!Binding.IsValid())
		{
			UE_LOG(LogCadenceArcEnhancedInput, Warning, TEXT("%s: skipped an entry without Input Action or input tag."),
			       *GetPathNameSafe(ActionSet));
			continue;
		}
		if (BoundTags.Contains(Binding.InputTag) || BoundActions.Contains(Binding.InputAction))
		{
			UE_LOG(LogCadenceArcEnhancedInput, Warning, TEXT("%s: skipped duplicate mapping %s -> %s."),
			       *GetPathNameSafe(ActionSet), *GetNameSafe(Binding.InputAction), *Binding.InputTag.ToString());
			continue;
		}
		for (const UInputTrigger* Trigger : Binding.InputAction->Triggers)
		{
			if (Trigger && !Trigger->IsA<UInputTriggerDown>())
			{
				UE_LOG(LogCadenceArcEnhancedInput, Warning,
				       TEXT("%s uses trigger %s. Started/Completed may no longer match the physical press and release."),
				       *GetNameSafe(Binding.InputAction), *GetNameSafe(Trigger->GetClass()));
			}
		}

		BindingHandles.Add(InputComponent->BindAction(Binding.InputAction, ETriggerEvent::Started, this,
		                                              &ThisClass::HandleStarted, Binding.InputTag).GetHandle());
		BindingHandles.Add(InputComponent->BindAction(Binding.InputAction, ETriggerEvent::Completed, this,
		                                              &ThisClass::HandleCompleted, Binding.InputTag).GetHandle());
		BindingHandles.Add(InputComponent->BindAction(Binding.InputAction, ETriggerEvent::Canceled, this,
		                                              &ThisClass::HandleCanceled, Binding.InputTag).GetHandle());
		BoundTags.Add(Binding.InputTag);
		BoundActions.Add(Binding.InputAction);
		Target->SetInputMode(Binding.InputTag, Binding.InputMode);
	}
	BoundInputComponent = InputComponent;

	// 取消控制时 Pawn 先销毁输入组件，按住中的键收不到 Completed
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleControllerChanged);
	}
	return BoundTags.Num();
}

void UCadenceArcInputBinderComponent::UnbindInputActions()
{
	CancelBoundInputs();
	if (UEnhancedInputComponent* InputComponent = BoundInputComponent.Get())
	{
		for (const uint32 Handle : BindingHandles)
		{
			InputComponent->RemoveBindingByHandle(Handle);
		}
	}
	BindingHandles.Reset();
	BoundTags.Reset();
	BoundInputComponent.Reset();
}

void UCadenceArcInputBinderComponent::CancelBoundInputs()
{
	UCadenceArcComponent* Target = TargetComponent.Get();
	if (!Target)
	{
		return;
	}
	for (const FGameplayTag& InputTag : BoundTags)
	{
		Target->CancelInput(InputTag); // 没有按下的 Tag 是空操作
	}
}

void UCadenceArcInputBinderComponent::SetTargetComponent(UCadenceArcComponent* InTarget)
{
	if (InTarget == TargetComponent.Get())
	{
		return;
	}
	CancelBoundInputs();
	TargetComponent = InTarget;
}

UCadenceArcComponent* UCadenceArcInputBinderComponent::GetTargetComponent() const
{
	if (UCadenceArcComponent* Target = TargetComponent.Get())
	{
		return Target;
	}
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UCadenceArcComponent>() : nullptr;
}

void UCadenceArcInputBinderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInputActions();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void UCadenceArcInputBinderComponent::HandleStarted(const FGameplayTag InputTag)
{
	if (UCadenceArcComponent* Target = TargetComponent.Get())
	{
		Target->PressInput(InputTag);
	}
}

void UCadenceArcInputBinderComponent::HandleCompleted(const FGameplayTag InputTag)
{
	if (UCadenceArcComponent* Target = TargetComponent.Get())
	{
		Target->ReleaseInput(InputTag);
	}
}

void UCadenceArcInputBinderComponent::HandleCanceled(const FGameplayTag InputTag)
{
	if (UCadenceArcComponent* Target = TargetComponent.Get())
	{
		Target->CancelInput(InputTag);
	}
}

void UCadenceArcInputBinderComponent::HandleControllerChanged(
	APawn* Pawn, AController* OldController, AController* NewController)
{
	CancelBoundInputs();
}
