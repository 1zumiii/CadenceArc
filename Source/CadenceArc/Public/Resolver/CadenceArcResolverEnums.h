#pragma once

UENUM(BlueprintType)
enum class ECadenceArcResolverInitResult : uint8
{
	InvalidGraph = 0,
	Success,
	InvalidEntryActionTag,
	EntryNodeNotFound,
	UnexpectedState // 由于和ECadenceArcHandshakeResult::UnexpectedState语义重复，已从Busy更名
};

UENUM(BlueprintType)
enum class ECadenceArcResolverState : uint8
{
	Uninitialized = 0,
	Ready,
	AwaitingStart,
	Executing,
};

UENUM(BlueprintType)
enum class ECadenceArcHandshakeResult : uint8
{
	NotInitialized = 0,
	Success,
	InvalidRequestId,
	UnexpectedState,
	RequestIdMismatch
};

// 私有内部类型,不需要 UENUM/反射,因为 ResolveInput 本身是 private
enum class ECadenceArcResolveResult : uint8
{
	NotInitialized = 0, // 防御性;当前两个调用点都已提前校验,理论不可达
	InvalidInputTag, // 同上
	Success,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound
};

UENUM(BlueprintType)
enum class ECadenceArcResolutionCategory : uint8
{
	Rejected = 0,
	NoAction,
	Buffered,
	RequestProduced
};


UENUM(BlueprintType)
enum class ECadenceArcResolutionReason : uint8
{
	None = 0,
	NotInitialized,
	InvalidInputTag,
	InvalidTimestamp,
	RequestPending,
	BufferWindowClosed,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound,
	NoBufferedInput,
	InvalidCompletionTime,
	Expired
};

UENUM(BlueprintType)
enum class ECadenceArcResolverResetResult : uint8
{
	NotInitialized = 0,
	Success,
	Busy
};

UENUM(BlueprintType)
enum class ECadenceArcGestureResult : uint8
{
	Rejected = 0,
	Granted,
	Cancelled
};