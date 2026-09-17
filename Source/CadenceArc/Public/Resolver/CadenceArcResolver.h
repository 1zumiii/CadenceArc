#pragma once

#include "CoreMinimal.h"
#include "CadenceArcResolverTypes.h"
#include "GameplayTagContainer.h"
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
		ResolvedEvent
	};

	struct FCadenceArcInputSlot
	{
		ECadenceArcInputSlotState SlotState = ECadenceArcInputSlotState::Empty;
		FCadenceArcInputToken InputToken;
		FCadenceArcInputEvent InputEvent;
		bool bFromHold = false; // 区分按住释放产生的事件与直接提交的输入。
	};

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


	// Input Buffering
	UPROPERTY(Transient)
	bool bIsBufferWindowOpen = false;
	UPROPERTY(Transient)
	FCadenceArcInputEvent BufferedInputEvent;

	// Helper functions
	ECadenceArcResolveResult ResolveInput(
		const FGameplayTag& InInputTag,
		FCadenceArcActionRequest& OutActionRequest
	);

	ECadenceArcHandshakeResult ValidateHandshake(
		const int64 InRequestId,
		const ECadenceArcResolverState ExpectedState
	) const;

	ECadenceArcHandshakeResult SetBufferWindowState(
		const int64 InRequestId,
		const bool bShouldOpen
	);

	void TranslateResolveResult(
		const ECadenceArcResolveResult InResult,
		ECadenceArcResolutionCategory& OutCategory,
		ECadenceArcResolutionReason& OutReason
	);

	void ClearInputBuffer();

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
	FGameplayTag GetBufferedInputTag() const { return BufferedInputEvent.InputTag; }

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
