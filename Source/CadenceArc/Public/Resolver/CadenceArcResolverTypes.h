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

// Input
UENUM(BlueprintType)
enum class ECadenceArcInputResult : uint8
{
	Success,
	NotInitialized,
	InvalidInputTag,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound,
	RequestPending,
	Buffered,
	BufferWindowClosed,
	InvalidTimestamp
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag InputTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double TimestampSeconds = 0.0;

	bool IsValid() const
	{
		return InputTag.IsValid() && IsValidTimestamp();
	}
	
	bool IsValidTimestamp() const
	{
		return TimestampSeconds >= 0.0 && FMath::IsFinite(TimestampSeconds);
	}
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

UENUM(BlueprintType)
enum class ECadenceArcBufferConsumeResult : uint8
{
	NotAttempted,
	NoBufferedInput,
	Resolved,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound,
	UnexpectedResult,
	Expired,
	InvalidTime
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcActionCompletionOutcome
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	ECadenceArcHandshakeResult HandshakeResult = ECadenceArcHandshakeResult::NotInitialized;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	ECadenceArcBufferConsumeResult BufferConsumeResult = ECadenceArcBufferConsumeResult::NotAttempted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FCadenceArcActionRequest NextActionRequest;
};
