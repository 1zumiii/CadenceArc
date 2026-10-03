#include "Component/CadenceArcComponent.h"

#include "Component/CadenceArcInputContextProvider.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"

namespace
{
	// 解析原因归入"没有产生动作"还是"被拒绝"，与解析器结果类别的划分一致
	bool IsNoActionReason(const ECadenceArcResolutionReason Reason)
	{
		switch (Reason)
		{
		case ECadenceArcResolutionReason::RequestPending:
		case ECadenceArcResolutionReason::BufferWindowClosed:
		case ECadenceArcResolutionReason::NoMatchingTransition:
		case ECadenceArcResolutionReason::ConditionNotMet:
		case ECadenceArcResolutionReason::NoBufferedInput:
		case ECadenceArcResolutionReason::Expired:
		case ECadenceArcResolutionReason::WaitingForRelease:
			return true;
		default:
			return false;
		}
	}

	FCadenceArcInputResult MakeInputResult(const ECadenceArcInputStatus Status,
	                                       const ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None)
	{
		FCadenceArcInputResult Result;
		Result.Status = Status;
		Result.Reason = Reason;
		return Result;
	}

	FCadenceArcInputResult MakeFailureResult(const ECadenceArcResolutionReason Reason)
	{
		return MakeInputResult(IsNoActionReason(Reason) ? ECadenceArcInputStatus::NoAction
		                                                : ECadenceArcInputStatus::Rejected, Reason);
	}

	// 解析器的提交结果转换成输入处理结果
	FCadenceArcInputResult MakeResolutionResult(const FCadenceArcSubmitOutcome& Outcome)
	{
		switch (Outcome.GetCategory())
		{
		case ECadenceArcResolutionCategory::RequestProduced:
			return MakeInputResult(ECadenceArcInputStatus::RequestProduced);
		case ECadenceArcResolutionCategory::Buffered:
			return MakeInputResult(ECadenceArcInputStatus::Buffered);
		case ECadenceArcResolutionCategory::NoAction:
			return MakeInputResult(ECadenceArcInputStatus::NoAction, Outcome.GetReason());
		default:
			return MakeInputResult(ECadenceArcInputStatus::Rejected, Outcome.GetReason());
		}
	}
}

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
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const ECadenceArcResolverInitResult Result = Resolver->Initialize(InGraph);
	if (Result == ECadenceArcResolverInitResult::Success)
	{
		Resolver->SetContextTags(PersistentContext);
		// 初始化清空了解析器的输入槽；旧的按键配对已经没有对应的资格
		Tracker->ClearAll();
		PressedByTag.Reset();
	}
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
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

// ---- 按住资格的观察 ----

FCadenceArcHoldSnapshot UCadenceArcComponent::CaptureHold() const
{
	return Resolver ? Resolver->GetInputHoldSnapshot() : FCadenceArcHoldSnapshot{};
}

void UCadenceArcComponent::NotifyHoldEndedIfChanged(const FCadenceArcHoldSnapshot& Before,
                                                    const ECadenceArcHoldEndReason Reason)
{
	if (!Before.bHasHold)
	{
		return;
	}
	const FCadenceArcHoldSnapshot After = CaptureHold();
	if (After.bHasHold && After.Token == Before.Token)
	{
		return; // 同一份资格仍然有效
	}
	OnHoldEndedNative.Broadcast(Before.InputTag, Reason);
	OnHoldEnded.Broadcast(Before.InputTag, Reason);
}

bool UCadenceArcComponent::AdvanceAndDispatch(const double Now)
{
	if (!HasInitializedResolver())
	{
		return false;
	}
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const FCadenceArcInputAdvanceOutcome Outcome = Resolver->AdvanceInputTime(Now);
	if (!Outcome.IsAccepted())
	{
		return false;
	}
	for (const FCadenceArcInputStageChange& Change : Outcome.GetStageChanges())
	{
		OnHoldStageChangedNative.Broadcast(Before.InputTag, Change);
		OnHoldStageChanged.Broadcast(Before.InputTag, Change);
	}
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::AutoReleased);
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

// ---- 输入方式与上下文 ----

void UCadenceArcComponent::SetInputMode(const FGameplayTag& InputTag, const ECadenceArcInputMode Mode)
{
	InputModes.Add(InputTag, Mode);
}

ECadenceArcInputMode UCadenceArcComponent::GetInputMode(const FGameplayTag& InputTag) const
{
	const ECadenceArcInputMode* Mode = InputModes.Find(InputTag);
	return Mode ? *Mode : ECadenceArcInputMode::PressOnly;
}

bool UCadenceArcComponent::CurrentNodeHasReleasedEdge(const FGameplayTag& InputTag) const
{
	const UCadenceArcGraph* CurrentGraph = Resolver ? Resolver->GetGraph() : nullptr;
	const FCadenceArcNode* Node = CurrentGraph ? CurrentGraph->FindAction(Resolver->GetCurrentActionTag()) : nullptr;
	if (!Node)
	{
		return true; // 找不到当前节点时仍走按住路径，由解析器报告错误
	}
	return Node->Transitions.ContainsByPredicate([&InputTag](const FCadenceArcTransition& Edge)
	{
		return Edge.InputTag == InputTag && Edge.InputPhase == ECadenceArcInputPhase::Released;
	});
}

void UCadenceArcComponent::SetContextProvider(UObject* InProvider)
{
	ContextProvider = InProvider;
}

void UCadenceArcComponent::SetContextProviderFunction(
	TFunction<FGameplayTagContainer(FGameplayTag, ECadenceArcInputPhase)> InFunction)
{
	ContextProviderFunction = MoveTemp(InFunction);
}

FGameplayTagContainer UCadenceArcComponent::CollectContext(const FGameplayTag& InputTag,
                                                          const ECadenceArcInputPhase Phase) const
{
	if (ContextProviderFunction)
	{
		return ContextProviderFunction(InputTag, Phase);
	}
	UObject* Provider = ContextProvider.Get();
	if (!Provider)
	{
		Provider = GetOwner();
	}
	if (Provider && Provider->GetClass()->ImplementsInterface(UCadenceArcInputContextProvider::StaticClass()))
	{
		return ICadenceArcInputContextProvider::Execute_CollectInputContext(Provider, InputTag, Phase);
	}
	return FGameplayTagContainer();
}

void UCadenceArcComponent::SetContextTags(const FGameplayTagContainer& InContextTags)
{
	PersistentContext = InContextTags;
	if (Resolver)
	{
		Resolver->SetContextTags(InContextTags);
	}
}

FGameplayTagContainer UCadenceArcComponent::GetContextTags() const
{
	return PersistentContext;
}

// ---- 输入 ----

FCadenceArcInputResult UCadenceArcComponent::PressInput(const FGameplayTag& InputTag)
{
	return PressInputInternal(InputTag, nullptr);
}

FCadenceArcInputResult UCadenceArcComponent::PressInputWithContext(
	const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags)
{
	return PressInputInternal(InputTag, &ContextTags);
}

FCadenceArcInputResult UCadenceArcComponent::ReleaseInput(const FGameplayTag& InputTag)
{
	return ReleaseInputInternal(InputTag, nullptr);
}

FCadenceArcInputResult UCadenceArcComponent::ReleaseInputWithContext(
	const FGameplayTag& InputTag, const FGameplayTagContainer& ContextTags)
{
	return ReleaseInputInternal(InputTag, &ContextTags);
}

FCadenceArcInputResult UCadenceArcComponent::PressInputInternal(
	const FGameplayTag& InputTag, const FGameplayTagContainer* ExplicitContext)
{
	if (!HasInitializedResolver())
	{
		return MakeInputResult(ECadenceArcInputStatus::NotInitialized);
	}
	// 先处理截至此刻已经到期的自动释放，再处理这次按下
	const double Now = GetTimeSeconds();
	AdvanceAndDispatch(Now);
	if (!HasInitializedResolver())
	{
		return MakeInputResult(ECadenceArcInputStatus::NotInitialized); // 请求的处理函数重新初始化失败
	}

	const FCadenceArcInputTrackingOutcome Pressed = Tracker->Press(InputTag, Now);
	if (Pressed.GetResult() != ECadenceArcInputTrackingResult::PressedProduced)
	{
		return MakeInputResult(ECadenceArcInputStatus::AlreadyPressed);
	}

	// HoldRelease 的键只在当前节点有 Released 转移时等待松开；否则和 PressOnly 一样立即提交
	const bool bHold = GetInputMode(InputTag) == ECadenceArcInputMode::HoldRelease
		&& CurrentNodeHasReleasedEdge(InputTag);
	PressedByTag.Add(InputTag, FTrackedPress{Pressed.GetToken(), bHold});

	// Tracker 只负责配对和时间；上下文写进事件副本，由解析器随缓冲和资格一起保存
	FCadenceArcInputEvent PressEvent = Pressed.GetInputEvent();
	PressEvent.ContextTags = ExplicitContext ? *ExplicitContext : CollectContext(InputTag, ECadenceArcInputPhase::Pressed);

	const FCadenceArcHoldSnapshot Before = CaptureHold();
	if (!bHold)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(PressEvent);
		NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Replaced);
		if (Outcome.HasActionRequest())
		{
			DispatchRequest(Outcome.GetActionRequest());
		}
		return MakeResolutionResult(Outcome);
	}

	// 申请被拒绝时仍保留按键配对，直到真实松开或取消
	const FCadenceArcHoldOutcome Outcome = Resolver->BeginInputHold(Pressed.GetToken(), PressEvent);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Replaced);
	return Outcome.GetResult() == ECadenceArcHoldResult::Granted
		? MakeInputResult(ECadenceArcInputStatus::HoldGranted)
		: MakeFailureResult(Outcome.GetReason());
}

FCadenceArcInputResult UCadenceArcComponent::ReleaseInputInternal(
	const FGameplayTag& InputTag, const FGameplayTagContainer* ExplicitContext)
{
	if (!HasInitializedResolver())
	{
		return MakeInputResult(ECadenceArcInputStatus::NotInitialized);
	}
	// 推进失败也要继续结束配对，否则这个键会一直处于按下状态，之后的按下都被当成重复按下
	const double Now = GetTimeSeconds();
	AdvanceAndDispatch(Now);

	const FTrackedPress* Found = PressedByTag.Find(InputTag);
	if (!Found)
	{
		return MakeInputResult(ECadenceArcInputStatus::NotPressed);
	}
	const FTrackedPress Press = *Found;
	const FCadenceArcInputTrackingOutcome Released = Tracker->Release(Press.Token, Now);
	PressedByTag.Remove(InputTag);
	if (Released.GetResult() != ECadenceArcInputTrackingResult::ReleasedProduced || !Press.bHold
		|| !HasInitializedResolver())
	{
		return MakeInputResult(ECadenceArcInputStatus::KeyReleased);
	}

	// 资格可能已经结束：申请被拒绝、已经自动释放、被其他输入替换或被取消。
	// 这时只结束配对，不再调用 ReleaseInputHold，避免调试历史里出现无意义的失败
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	if (!Before.bHasHold || !(Before.Token == Press.Token))
	{
		return MakeInputResult(ECadenceArcInputStatus::KeyReleased);
	}
	FCadenceArcInputEvent ReleaseEvent = Released.GetInputEvent();
	ReleaseEvent.ContextTags = ExplicitContext ? *ExplicitContext : CollectContext(InputTag, ECadenceArcInputPhase::Released);
	const FCadenceArcInputAdvanceOutcome Outcome = Resolver->ReleaseInputHold(Press.Token, ReleaseEvent);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Released);
	if (!Outcome.IsAccepted())
	{
		return MakeFailureResult(Outcome.GetReason());
	}
	if (Outcome.HasActionRequest())
	{
		DispatchRequest(Outcome.GetResolution().GetActionRequest());
	}
	return MakeResolutionResult(Outcome.GetResolution());
}

void UCadenceArcComponent::CancelInput(const FGameplayTag& InputTag)
{
	const FTrackedPress* Found = PressedByTag.Find(InputTag);
	if (!Found)
	{
		return;
	}
	const FCadenceArcInputToken Token = Found->Token;
	PressedByTag.Remove(InputTag);
	Tracker->Cancel(Token);
	if (HasInitializedResolver())
	{
		const FCadenceArcHoldSnapshot Before = CaptureHold();
		Resolver->CancelInputHold(Token);
		NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cancelled);
	}
}

void UCadenceArcComponent::CancelAllInputs()
{
	TArray<FCadenceArcInputToken> Tokens;
	for (const TPair<FGameplayTag, FTrackedPress>& Pair : PressedByTag)
	{
		Tokens.Add(Pair.Value.Token);
	}
	Tracker->ClearAll();
	PressedByTag.Reset();
	if (HasInitializedResolver())
	{
		const FCadenceArcHoldSnapshot Before = CaptureHold();
		for (const FCadenceArcInputToken& Token : Tokens)
		{
			Resolver->CancelInputHold(Token);
		}
		NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cancelled);
	}
}

bool UCadenceArcComponent::IsInputPressed(const FGameplayTag& InputTag) const
{
	return PressedByTag.Contains(InputTag);
}

// ---- 执行器回调 ----

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionStarted(const int64 RequestId)
{
	if (!Resolver)
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const ECadenceArcHandshakeResult Result = Resolver->NotifyActionStarted(RequestId);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
	return Result;
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
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const FCadenceArcActionCompletionOutcome Outcome = Resolver->NotifyActionCompleted(RequestId, Now);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
	if (Outcome.HasNextActionRequest())
	{
		DispatchRequest(Outcome.GetNextActionRequest());
	}
	return Outcome;
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionCancelled(const int64 RequestId)
{
	if (!Resolver)
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const ECadenceArcHandshakeResult Result = Resolver->NotifyActionCancelled(RequestId);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
	return Result;
}

ECadenceArcHandshakeResult UCadenceArcComponent::NotifyActionInterrupted(const int64 RequestId)
{
	if (!Resolver)
	{
		return ECadenceArcHandshakeResult::NotInitialized;
	}
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const ECadenceArcHandshakeResult Result = Resolver->NotifyActionInterrupted(RequestId);
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
	return Result;
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
	if (!Resolver)
	{
		return ECadenceArcResolverResetResult::NotInitialized;
	}
	const FCadenceArcHoldSnapshot Before = CaptureHold();
	const ECadenceArcResolverResetResult Result = Resolver->Reset();
	NotifyHoldEndedIfChanged(Before, ECadenceArcHoldEndReason::Cleared);
	return Result;
}
