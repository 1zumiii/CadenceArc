#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CadenceArcResolverTypes.generated.h"

UENUM(BlueprintType)
enum class ECadenceArcResolverInitResult : uint8
{
	Success,
	InvalidGraph,
	InvalidEntryActionTag,
	EntryNodeNotFound,
	Busy
};

UENUM(BlueprintType)
enum class ECadenceArcResolverTransitionResult : uint8
{
	Success,
	NotInitialized,
	InvalidInputTag,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound,
	RequestPending,
	ActionExecuting
};

UENUM(BlueprintType)
enum class ECadenceArcResolverState : uint8
{
	Uninitialized,
	Ready,
	AwaitingStart,
	Executing,
};

UENUM(BlueprintType)
enum class ECadenceArcHandshakeResult : uint8
{
	Success,
	NotInitialized,
	InvalidRequestId,
	UnexpectedState,
	RequestIdMismatch
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcActionRequest
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	int64 RequestId = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag InputTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag SourceActionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag TargetActionTag;
};
