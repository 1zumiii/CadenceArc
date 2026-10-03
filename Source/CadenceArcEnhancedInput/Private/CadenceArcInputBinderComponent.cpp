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
	if (!InputComponent && GetOwner())
	{
		InputComponent = Cast<UEnhancedInputComponent>(GetOwner()->InputComponent);
	}
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

bool UCadenceArcInputBinderComponent::IsBound() const
{
	const UEnhancedInputComponent* InputComponent = BoundInputComponent.Get();
	if (!InputComponent || BindingHandles.IsEmpty())
	{
		return false;
	}
	// Pawn 初始化或外部代码可能清空输入绑定，而输入组件对象仍然存在。
	// 只有记录的 handle 全部还在组件中，才可以跳过下一次自动绑定。
	const auto& Bindings = InputComponent->GetActionEventBindings();
	for (const uint32 Handle : BindingHandles)
	{
		if (!Bindings.ContainsByPredicate([Handle](const auto& Binding) { return Binding->GetHandle() == Handle; }))
		{
			return false;
		}
	}
	return true;
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

void UCadenceArcInputBinderComponent::OnRegister()
{
	Super::OnRegister();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		// Pawn 在 PawnClientRestart 中创建输入组件，之后广播 Restarted
		Pawn->ReceiveRestartedDelegate.AddUniqueDynamic(this, &ThisClass::HandlePawnRestarted);
		// 取消控制时 Pawn 先销毁输入组件，按住中的键收不到 Completed
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleControllerChanged);
	}
}

void UCadenceArcInputBinderComponent::OnUnregister()
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveRestartedDelegate.RemoveDynamic(this, &ThisClass::HandlePawnRestarted);
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	}
	Super::OnUnregister();
}

void UCadenceArcInputBinderComponent::BeginPlay()
{
	Super::BeginPlay();
	TryAutoBind(); // Pawn 可能在 BeginPlay 之前就已经被控制
}

void UCadenceArcInputBinderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindInputActions();
	Super::EndPlay(EndPlayReason);
}

void UCadenceArcInputBinderComponent::TryAutoBind()
{
	const AActor* Owner = GetOwner();
	if (!bAutoBind || !ActionSet || !Owner)
	{
		return;
	}
	UEnhancedInputComponent* InputComponent = Cast<UEnhancedInputComponent>(Owner->InputComponent);
	if (!InputComponent || (IsBound() && BoundInputComponent.Get() == InputComponent))
	{
		return; // 没有玩家控制（例如 AI），或已经手动绑定到这个输入组件
	}
	BindInputActions(InputComponent);
}

void UCadenceArcInputBinderComponent::HandlePawnRestarted(APawn* Pawn)
{
	TryAutoBind();
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
