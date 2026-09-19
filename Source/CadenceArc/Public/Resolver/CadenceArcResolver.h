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
		double ChargeFullSeconds = 0.0;   // bHasChargeConfig 为 false 时为 0 且不参与计算
	};

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
	
	// 返回 None 表示可以继续；否则调用方直接用这个原因拒绝
	ECadenceArcResolutionReason CheckPendingHoldConflict(double NowSeconds) const;

	// 释放已经确定之后的共用消费路径：校验上下文与年龄，再用授予时的边副本解析。
	// 手动松手与自动释放都走这里，保证"一次按下最多兑现一次"。
	FCadenceArcSubmitOutcome ConsumeHoldRelease(
		const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent, double NowSeconds
	);

	// 释放已经决定之后的共用收尾：资格一定终结，再按当前状态立即消费或存成缓冲事件。
	// ObservedTimestampSeconds 是真实观察到释放的时刻；自动释放时它可能晚于事件时刻。
	FCadenceArcSubmitOutcome FinishHoldRelease(
		const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent,
		double ObservedTimestampSeconds
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

	// 按值返回的只读查询；没有待松手资格时返回空快照（bHasHold=false、Stage=None）。
	UFUNCTION(BlueprintPure, Category="CadenceArc|Resolver")
	FCadenceArcHoldSnapshot GetInputHoldSnapshot() const;

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

	// 物理松手。校验通过后资格一定终结，无论是否解析出候选。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcInputAdvanceOutcome ReleaseInputHold(
		const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& ReleaseEvent
	);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcHoldOutcome BeginInputHold(
		const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& PressEvent
	);

	// 推进按住资格的时间：跨过的阈值按时间升序报告，到达保持上限时自动释放一次。
	// 宿主每帧先调用它并处理结果，再处理输入或 Completed。没有资格时是接受的空操作。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcInputAdvanceOutcome AdvanceInputTime(const double NowSeconds);

	// 显式丢弃按住资格（失焦、解绑、翻滚／防御打断）。不合成松手，也不撤销已提交的候选。
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcHoldOutcome CancelInputHold(const FCadenceArcInputToken& Token);
};
