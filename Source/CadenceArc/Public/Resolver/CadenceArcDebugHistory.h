#pragma once

// Resolver 的调试历史（Phase 7B）。只在编辑器构建中存在：打包后的游戏里没有这些类型，也没有记录开销。
// 记录只读、不回调任何人：每个公开操作在返回前写一条定长记录，编辑器面板按序号主动拉取。
#if WITH_EDITOR

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Resolver/CadenceArcResolverTypes.h"

// 被记录的公开操作。查询函数不记录；每帧调用的 AdvanceInputTime 只在真正产生效果时记录。
enum class ECadenceArcDebugOperation : uint8
{
	Initialize,
	Reset,
	SubmitInput,
	BeginHold,
	ReleaseHold,
	CancelHold,
	AdvanceTime, // 只在跨过蓄力阶段、自动释放或调用被拒绝时记录
	ActionStarted,
	ActionRejected,
	ActionCompleted,
	ActionCancelled,
	ActionInterrupted,
	OpenWindow,
	CloseWindow,
	ComboReset, // 新输入因停顿超时改从入口选边，不提前提交动作节点
	FallbackToEntry, // 源节点无对应输入转移，尝试入口
};

// 一条历史记录。字段按操作类型填写，其余保持默认值；只保存值，不持有任何 UObject。
struct FCadenceArcDebugEvent
{
	uint64 Sequence = 0; // 从 1 开始，按调用顺序递增，不会重复
	ECadenceArcDebugOperation Operation = ECadenceArcDebugOperation::Initialize;
	// 这次调用没有达到目的：输入被忽略或拒绝、握手失败、松手没有出招、缓冲过期等。
	// 面板只对失败的记录显示结果和原因；"动作完成时没有缓冲输入"这类正常情况不算失败。
	bool bFailed = false;

	// ---- 调用参数 ----
	FGameplayTag InputTag;
	ECadenceArcInputPhase InputPhase = ECadenceArcInputPhase::Pressed;
	bool bHasTimestamp = false; // 有时间可显示（调用自带，或取自此前最近一次带时间的调用）
	double TimestampSeconds = 0.0;
	// 这个操作本身不带时间（Started、窗口开关等），TimestampSeconds 取自此前最近一次带时间的调用。
	// 宿主每帧都会带着当前时间调用 AdvanceInputTime，所以误差不超过一帧；Resolver 自己从不读时钟。
	bool bTimeFromLastCall = false;
	bool bHasHeldDuration = false; // 松手（手动或自动）时的按住时长
	double HeldDurationSeconds = 0.0;
	int64 CallerRequestId = 0; // 握手与窗口操作时调用方传入的请求编号
	FGameplayTagContainer InputContextTags; // 输入事件自带的上下文（不含持久上下文）；没有输入参数的操作为空

	// ---- 结果（只填与操作相关的字段） ----
	ECadenceArcResolverInitResult InitResult = ECadenceArcResolverInitResult::Success;
	ECadenceArcResolverResetResult ResetResult = ECadenceArcResolverResetResult::Success;
	ECadenceArcHandshakeResult HandshakeResult = ECadenceArcHandshakeResult::Success;
	ECadenceArcHoldResult HoldResult = ECadenceArcHoldResult::Granted;
	ECadenceArcResolutionCategory Category = ECadenceArcResolutionCategory::NoAction; // 输入解析或缓冲消费的结果
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;
	FCadenceArcActionRequest ProducedRequest; // 产生的候选请求（RequestId 为 0 表示没有）
	ECadenceArcInputReleaseSource ReleaseSource = ECadenceArcInputReleaseSource::None;
	ECadenceArcHoldStage StageReached = ECadenceArcHoldStage::None; // 本次跨过的最后一个蓄力阶段

	// ---- 选边时实际用到的条件（Phase 8）：只有这次调用真的按图选了边才填 ----
	bool bHasResolutionContext = false;
	FGameplayTagContainer ContextTags; // 持久上下文与事件上下文的并集
	bool bHasPauseDuration = false; // 没有停顿起点（还没有完成过动作）时为 false
	double PauseDurationSeconds = 0.0; // 缓冲的输入和缓冲的松手按 0
	FGameplayTag ResolutionSourceActionTag; // 实际选边源，可能与已提交节点不同
	FGameplayTag RecoverySourceActionTag;
	FGameplayTag RecoveryTargetActionTag;
	double ComboResetSeconds = 0.0;

	// ---- 调用前后 ----
	ECadenceArcResolverState StateBefore = ECadenceArcResolverState::Uninitialized;
	ECadenceArcResolverState StateAfter = ECadenceArcResolverState::Uninitialized;
	FGameplayTag CommittedBefore;
	FGameplayTag CommittedAfter;
	FCadenceArcActionRequest RequestBefore; // 调用前的待处理请求；Started 成功时就是被提交的那一个
};

// 定长环形历史。写满后覆盖最旧的记录；读取只复制，不影响后续读取。
class FCadenceArcDebugHistory
{
public:
	static constexpr int32 Capacity = 256;

	void Add(FCadenceArcDebugEvent Event)
	{
		Event.Sequence = NextSequence++;
		if (Events.Num() < Capacity)
		{
			Events.Add(MoveTemp(Event));
		}
		else
		{
			Events[Head] = MoveTemp(Event);
			Head = (Head + 1) % Capacity;
		}
	}

	// 按从旧到新的顺序追加序号大于 AfterSequence 的记录
	void CopyEventsAfter(const uint64 AfterSequence, TArray<FCadenceArcDebugEvent>& OutEvents) const
	{
		for (int32 Offset = 0; Offset < Events.Num(); ++Offset)
		{
			const FCadenceArcDebugEvent& Event = Events[(Head + Offset) % Events.Num()];
			if (Event.Sequence > AfterSequence)
			{
				OutEvents.Add(Event);
			}
		}
	}

	// 最新一条的序号；还没有记录时为 0
	uint64 GetNewestSequence() const { return NextSequence - 1; }

	// 仍保留的最旧一条的序号；还没有记录时为 0。读取方据此判断中间有没有被覆盖掉的记录
	uint64 GetOldestSequence() const { return Events.IsEmpty() ? 0 : Events[Head].Sequence; }

private:
	TArray<FCadenceArcDebugEvent> Events;
	int32 Head = 0; // 写满后指向最旧的一条
	uint64 NextSequence = 1;
};

#endif // WITH_EDITOR
