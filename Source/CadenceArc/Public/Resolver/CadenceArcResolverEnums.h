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
	RequestIdMismatch,
	// 以下用于存在待松手资格时的 Completed，拒绝发生在写入任何状态之前。
	// 完成时间非有限、为负或早于资格的最后观察时间。
	InvalidCompletionTime,
	// 自动释放已到期但尚未调用 AdvanceInputTime；先推进并处理其结果再重试。
	InputTimeAdvanceRequired
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
	Expired,
	// ---- Phase 6 Hold ----
	// 收敛原则：只有调用方需要不同处理时才新增原因；诊断细节可结合快照判断。
	// 输入事件的 Phase 或 HeldDuration 非法（Tag、时间沿用上面的原有原因；时间倒退也归入 InvalidTimestamp）。
	InvalidInputEvent,
	// HoldRelease 的 Released 事件必须通过 ReleaseInputHold 提交，不能走 SubmitInput。
	InputIdentityRequired,
	// 该 Token 在 Resolver 中没有可操作的按住资格：无效、不匹配、已释放、已被替换或已清理。
	NoMatchingHold,
	// 已有资格处于 Charging／Charged，新输入被保护期拒绝；防御类打断由宿主调用 CancelInputHold。
	HoldProtected,
	// 自动释放已到期但尚未调用 AdvanceInputTime。
	InputTimeAdvanceRequired,
	// 动作正常完成，按住资格仍保留，等待手动或自动释放。
	WaitingForRelease,
	// 运行时发现图或资格配置副本无法解释本次输入（含多条候选边）。
	InvalidGraphConfiguration
};

UENUM(BlueprintType)
enum class ECadenceArcResolverResetResult : uint8
{
	NotInitialized = 0,
	Success,
	Busy
};

UENUM(BlueprintType)
enum class ECadenceArcHoldResult : uint8
{
	// 操作被拒绝；具体原因由 Outcome 提供，默认值不表示成功。
	Rejected = 0,
	// 本次按住资格申请成功，不表示已经产生或开始执行动作。
	Granted,
	// 匹配的按住资格已取消；取消不会合成释放输入或攻击。
	Cancelled
};
