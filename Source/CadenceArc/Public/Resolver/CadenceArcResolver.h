#pragma once

#include "CoreMinimal.h"
#include "CadenceArcResolverTypes.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "CadenceArcResolver.generated.h"

class UCadenceArcGraph;

/**
 * Resolves semantic input tags through a configured action graph and coordinates
 * action execution through an explicit request/lifecycle handshake.
 */
UCLASS(BlueprintType)
class CADENCEARC_API UCadenceArcResolver : public UObject
{
	GENERATED_BODY()

private:
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
	FGameplayTag BufferedInputTag;

	// Helper functions
	ECadenceArcInputResult ResolveInput(
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
	
	void ClearInputBuffer();

public:
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcResolverInitResult Initialize(UCadenceArcGraph* InGraph);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcInputResult SubmitInput(
		const FGameplayTag& InInputTag,
		FCadenceArcActionRequest& OutActionRequest
	);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	bool Reset();

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
	FGameplayTag GetBufferedInputTag() const { return BufferedInputTag; }

	/// Handshake Notifications
	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionStarted(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult NotifyActionRejected(const int64 InRequestId);

	UFUNCTION(BlueprintCallable, Category="CadenceArc|Resolver")
	FCadenceArcActionCompletionOutcome NotifyActionCompleted(const int64 InRequestId);

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
