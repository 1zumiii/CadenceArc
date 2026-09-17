#pragma once

#include "CoreMinimal.h"
#include "CadenceArcResolverTypes.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"
#include "UObject/Object.h"
#include "CadenceArcResolver.generated.h"

class UCadenceArcGraph;

DECLARE_LOG_CATEGORY_EXTERN(LogCadenceArc, Log, All);

/**
 * Resolves semantic input tags through a configured action graph and coordinates
 * action execution through an explicit request/lifecycle handshake.
 */
UCLASS(BlueprintType)
class CADENCEARC_API UCadenceArcResolver : public UObject
{
	GENERATED_BODY()

private:
	// 内部使用
	enum class ECadenceArcInputSlotState :uint8
	{
		// 槽内没有待处理输入。
		Empty = 0,
		// 持有按住资格，等待手动或自动释放后确定最终输入。
		PendingHold,
		// 最终输入事件已经确定，等待解析或消费。
		BufferedEvent
	};

	struct FCadenceArcInputSlot
	{
		ECadenceArcInputSlotState SlotState = ECadenceArcInputSlotState::Empty;
		FCadenceArcInputToken InputToken;
		FCadenceArcInputEvent InputEvent;
		bool bFromHold = false; // 区分按住释放产生的事件与直接提交的输入
		double LastObservedTimestampSeconds = 0.0;
		FGameplayTag SourceActionTag;
		int64 GrantedContextId = 0; // 授予资格时记下的编号      
		bool bHasChargeConfig = false;
		FCadenceArcHoldChargeConfig ChargeConfig;
		TArray<FCadenceArcTransition> ReleasedEdges; // 按住释放时的边集合,用于在 ResolveInput 时进行匹配
		double MaxBufferedInputAgeSeconds = 0.0;
	};

	// 取缔原有的 Category-Reason 翻译层
	struct FTransitionMatch
	{
		ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::NoMatchingTransition;
		FGameplayTag TargetActionTag; // 仅当 Reason == None 时有效
	};

	FTransitionMatch FindUniqueTransition(
		const FGameplayTag& SourceActionTag,
		const FCadenceArcInputEvent& Event,
		const TArray<FCadenceArcTransition>* EdgesOverride = nullptr // 按住资格传入边副本
	) const;

	FCadenceArcActionRequest CommitRequest(
		const FGameplayTag& InputTag, const FGameplayTag& TargetActionTag
	);

	UPROPERTY(Transient)
	TObjectPtr<UCadenceArcGraph> Graph;
	UPROPERTY(Transient)
	FGameplayTag CurrentActionTag;
	UPROPERTY(Transient)
	ECadenceArcResolverState State = ECadenceArcResolverState::Uninitialized;
	UPROPERTY(Transient)
	FCadenceArcActionRequest OutstandingRequest;
	UPROPERTY(Transient)
	int64 NextRequestId = 1;
	UPROPERTY(Transient)
	int64 CurrentContextId = 0; // 当前执行上下文的编号，成功 Started/Reset/Initialize/Cancelled/Interrupted 时 ++
	// 消费时：
	//if (Slot.GrantedContextId != CurrentContextId) { /* 资格已失效 */ }

	// Input Buffering
	UPROPERTY(Transient)
	bool bIsBufferWindowOpen = false;

	//UPROPERTY(Transient)
	// FCadenceArcInputEvent BufferedInputEvent; // 已弃用，被InputSlot替代
	FCadenceArcInputSlot InputSlot;

	ECadenceArcHandshakeResult ValidateHandshake(
		const int64 InRequestId,
		const ECadenceArcResolverState ExpectedState
	) const;

	ECadenceArcHandshakeResult SetBufferWindowState(
		const int64 InRequestId,
		const bool bShouldOpen
	);

	void ClearInputSlot();
	void ResetBufferWindow();
	void CheckSlotInvariants() const;

public:
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcResolverInitResult Initialize(UCadenceArcGraph* InGraph);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcSubmitOutcome SubmitInput(const FCadenceArcInputEvent& InInputEvent);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcResolverResetResult Reset();

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	bool IsInitialized() const;

	/// Getters
	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	FGameplayTag GetCurrentActionTag() const { return CurrentActionTag; }

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	ECadenceArcResolverState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	FCadenceArcActionRequest GetOutstandingRequest() const { return OutstandingRequest; }

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	bool IsBufferWindowOpen() const { return bIsBufferWindowOpen; }

	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	FGameplayTag GetBufferedInputTag() const;

	/// Handshake Notifications
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionStarted(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionRejected(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcActionCompletionOutcome NotifyActionCompleted(
		const int64 InRequestId,
		const double CompletionTimestampSeconds
	);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionCancelled(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionInterrupted(const int64 InRequestId);

	// Buffering
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult OpenBufferWindow(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult CloseBufferWindow(const int64 InRequestId);
};
