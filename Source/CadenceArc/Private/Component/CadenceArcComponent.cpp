#include "Component/CadenceArcComponent.h"

#include "Engine/World.h"
#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"

UCadenceArcComponent::UCadenceArcComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	Tracker = MakeUnique<FCadenceArcInputTracker>();
}

void UCadenceArcComponent::BeginPlay()
{
	Super::BeginPlay();
	if (Graph)
	{
		const ECadenceArcResolverInitResult Result = InitializeResolver(Graph);
		if (Result != ECadenceArcResolverInitResult::Success)
		{
			UE_LOG(LogCadenceArc, Warning, TEXT("%s: resolver initialization failed (%s)."),
			       *GetPathName(), *UEnum::GetValueAsString(Result));
		}
	}
}

void UCadenceArcComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelAllInputs();
	Super::EndPlay(EndPlayReason);
}

ECadenceArcResolverInitResult UCadenceArcComponent::InitializeResolver(UCadenceArcGraph* InGraph)
{
	if (!Resolver)
	{
		Resolver = NewObject<UCadenceArcResolver>(this);
	}
	const ECadenceArcResolverInitResult Result = Resolver->Initialize(InGraph);
	if (Result == ECadenceArcResolverInitResult::Success)
	{
		// 初始化清空了解析器的输入槽；旧的按键配对已经没有对应的资格
		Tracker->ClearAll();
		PressedByTag.Reset();
	}
	return Result;
}

bool UCadenceArcComponent::HasInitializedResolver() const
{
	return Resolver && Resolver->IsInitialized();
}

double UCadenceArcComponent::GetTimeSeconds() const
{
	if (TimeSource)
	{
		return TimeSource();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UCadenceArcComponent::SetTimeSource(TFunction<double()> InTimeSource)
{
	TimeSource = MoveTemp(InTimeSource);
}

void UCadenceArcComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// 引擎只会 Tick 已注册的组件；自动化测试直接调用 Tick 时组件未注册，跳过要求注册的基类逻辑
	if (IsRegistered())
	{
		Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	}
	if (HasInitializedResolver())
	{
		AdvanceAndDispatch(GetTimeSeconds());
	}
}

bool UCadenceArcComponent::AdvanceAndDispatch(const double Now)
{
	if (!HasInitializedResolver())
	{
		return false;
	}
	// 阶段变化只带阈值时间，Tag 取推进前的资格
	const FGameplayTag HeldTag = Resolver->GetInputHoldSnapshot().InputTag;
	const FCadenceArcInputAdvanceOutcome Outcome = Resolver->AdvanceInputTime(Now);
	if (!Outcome.IsAccepted())
	{
		return false;
	}
	for (const FCadenceArcInputStageChange& Change : Outcome.GetStageChanges())
	{
		OnHoldStageChangedNative.Broadcast(HeldTag, Change);
		OnHoldStageChanged.Broadcast(HeldTag, Change);
	}
	if (Outcome.HasActionRequest())
	{
		DispatchRequest(Outcome.GetResolution().GetActionRequest());
	}
	return true;
}

void UCadenceArcComponent::DispatchRequest(const FCadenceArcActionRequest& Request)
{
	OnActionRequestedNative.Broadcast(Request);
	OnActionRequested.Broadcast(Request);
}

void UCadenceArcComponent::PressInput(
	const FGameplayTag& InputTag, const ECadenceArcInputMode Mode, const FGameplayTagContainer& ContextTags)
{
	if (!HasInitializedResolver()
		|| (Mode != ECadenceArcInputMode::PressOnly && Mode != ECadenceArcInputMode::HoldRelease))
	{
		return;
	}
	// 先处理截至此刻已经到期的自动释放，再处理这次按下
	const double Now = GetTimeSeconds();
	AdvanceAndDispatch(Now);
	if (!HasInitializedResolver())
	{
		return; // 请求的处理函数可能重新初始化或销毁了解析器
	}

	const FCadenceArcInputTrackingOutcome Pressed = Tracker->Press(InputTag, Now);
	if (Pressed.GetResult() != ECadenceArcInputTrackingResult::PressedProduced)
	{
		return; // 重复按下：同一个键还没有松开
	}
	PressedByTag.Add(InputTag, FTrackedPress{Pressed.GetToken(), Mode});

	// Tracker 只负责配对和时间；上下文写进事件副本，由解析器随缓冲和资格一起保存
	FCadenceArcInputEvent PressEvent = Pressed.GetInputEvent();
	PressEvent.ContextTags = ContextTags;
	if (Mode == ECadenceArcInputMode::PressOnly)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(PressEvent);
		if (Outcome.HasActionRequest())
		{
			DispatchRequest(Outcome.GetActionRequest());
		}
	}
	else
	{
		// 申请被拒绝时仍保留按键配对，直到真实松开或取消
		Resolver->BeginInputHold(Pressed.GetToken(), PressEvent);
	}
}

void UCadenceArcComponent::ReleaseInput(const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags)
{
	if (!HasInitializedResolver())
	{
		return;
	}
	// 推进失败也要继续结束配对，否则这个键会一直处于按下状态，之后的按下都被当成重复按下
	const double Now = GetTimeSeconds();
	AdvanceAndDispatch(Now);

	const FTrackedPress* Found = PressedByTag.Find(InputTag);
	if (!Found)
	{
		return;
	}
	const FTrackedPress Press = *Found;
	const FCadenceArcInputTrackingOutcome Released = Tracker->Release(Press.Token, Now);
	if (Released.GetResult() != ECadenceArcInputTrackingResult::ReleasedProduced)
	{
		return;
	}
	PressedByTag.Remove(InputTag);
	if (Press.Mode != ECadenceArcInputMode::HoldRelease || !HasInitializedResolver())
	{
		return;
	}

	// 资格可能已经结束：申请被拒绝、已经自动释放、被其他输入替换或被取消。
	// 这时只结束配对，不再调用 ReleaseInputHold，避免调试历史里出现无意义的失败
	const FCadenceArcHoldSnapshot Hold = Resolver->GetInputHoldSnapshot();
	if (!Hold.bHasHold || !(Hold.Token == Press.Token))
	{
		return;
	}
	FCadenceArcInputEvent ReleaseEvent = Released.GetInputEvent();
	ReleaseEvent.ContextTags = ContextTags;
	const FCadenceArcInputAdvanceOutcome Outcome = Resolver->ReleaseInputHold(Press.Token, ReleaseEvent);
	if (Outcome.HasActionRequest())
	{
		DispatchRequest(Outcome.GetResolution().GetActionRequest());
	}
}

void UCadenceArcComponent::CancelInput(const FGameplayTag& InputTag)
{
	const FTrackedPress* Found = PressedByTag.Find(InputTag);
	if (!Found)
	{
		return;
	}
	if (HasInitializedResolver())
	{
		Resolver->CancelInputHold(Found->Token);
	}
	Tracker->Cancel(Found->Token);
	PressedByTag.Remove(InputTag);
}

void UCadenceArcComponent::CancelAllInputs()
{
	if (HasInitializedResolver())
	{
		for (const TPair<FGameplayTag, FTrackedPress>& Pair : PressedByTag)
		{
			Resolver->CancelInputHold(Pair.Value.Token);
		}
	}
	Tracker->ClearAll();
	PressedByTag.Reset();
}

bool UCadenceArcComponent::IsInputPressed(const FGameplayTag& InputTag) const
{
	return PressedByTag.Contains(InputTag);
}

void UCadenceArcComponent::SetContextTags(const FGameplayTagContainer& InContextTags)
{
	if (Resolver)
	{
		Resolver->SetContextTags(InContextTags);
	}
}

FGameplayTagContainer UCadenceArcComponent::GetContextTags() const
{
	return Resolver ? Resolver->GetContextTags() : FGameplayTagContainer();
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionStarted(const int64 RequestId)
{
	return Resolver ? Resolver->NotifyActionStarted(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionRejected(const int64 RequestId)
{
	return Resolver ? Resolver->NotifyActionRejected(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

FCadenceArcActionCompletionOutcome UCadenceArcComponent::NotifyActionCompleted(const int64 RequestId)
{
	if (!Resolver)
	{
		return FCadenceArcActionCompletionOutcome{}; // 默认握手结果为 NotInitialized
	}
	// 同一个时间先推进再完成：到期的自动释放必须先处理，否则完成回调返回 InputTimeAdvanceRequired
	const double Now = GetTimeSeconds();
	AdvanceAndDispatch(Now);
	const FCadenceArcActionCompletionOutcome Outcome = Resolver->NotifyActionCompleted(RequestId, Now);
	if (Outcome.HasNextActionRequest())
	{
		DispatchRequest(Outcome.GetNextActionRequest());
	}
	return Outcome;
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionCancelled(const int64 RequestId)
{
	return Resolver ? Resolver->NotifyActionCancelled(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionInterrupted(const int64 RequestId)
{
	return Resolver ? Resolver->NotifyActionInterrupted(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcComponent::OpenBufferWindow(const int64 RequestId)
{
	return Resolver ? Resolver->OpenBufferWindow(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcHandshakeResult UCadenceArcComponent::CloseBufferWindow(const int64 RequestId)
{
	return Resolver ? Resolver->CloseBufferWindow(RequestId) : ECadenceArcHandshakeResult::NotInitialized;
}

ECadenceArcResolverResetResult UCadenceArcComponent::ResetCombo()
{
	return Resolver ? Resolver->Reset() : ECadenceArcResolverResetResult::NotInitialized;
}
