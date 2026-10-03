// UCadenceArcResolver 的按住与蓄力部分：授予资格、按住快照、手动和自动松手、推进时间、取消。
// 与 CadenceArcResolver.cpp 是同一个类，只按功能分文件；公开入口仍在 CadenceArcResolverEntryPoints.cpp。

#include "Resolver/CadenceArcResolver.h"

#include "CadenceArcHoldTiming.h"
#include "Graph/CadenceArcGraph.h"

FCadenceArcHoldOutcome UCadenceArcResolver::BeginInputHoldImpl(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& PressEvent
)
{
	FCadenceArcHoldOutcome Outcome; // 默认 Rejected / None
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}

	// 事件格式与身份和 Resolver 当前状态无关，所以排在状态判断之前。
	// 原因顺序与 SubmitInput 完全一致，由 ValidateInputEvent 单点定义。
	if (const ECadenceArcResolutionReason Invalid = ValidateInputEvent(PressEvent);
		Invalid != ECadenceArcResolutionReason::None)
	{
		Outcome.SetRejected(Invalid);
		return Outcome;
	}
	// 申请资格必须带一个有效 Token 和一个按下事件
	if (!Token.IsValid() || PressEvent.InputPhase != ECadenceArcInputPhase::Pressed)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// 与 SubmitInput 共用的准入：AwaitingStart 不申请（当前节点马上要变，资格绑不到确定的源节点），
	// Executing 必须开窗，随后才是时间倒退、漏 Advance 与蓄力保护
	if (const ECadenceArcResolutionReason Admission = CheckInputAdmission(PressEvent.TimestampSeconds);
		Admission != ECadenceArcResolutionReason::None)
	{
		Outcome.SetRejected(Admission);
		return Outcome;
	}

	// 以下全部在局部变量上完成，任何一步失败都不能动到已有的槽
	const FCadenceArcNode* SourceNode = Graph->FindAction(CurrentActionTag);
	if (!SourceNode)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::CurrentNodeNotFound);
		return Outcome;
	}
	if (!SourceNode->IsValidTransition())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidGraphConfiguration);
		return Outcome;
	}
	// 该 Tag 一条 Released 边都没有：无论按多久，松手时必然无匹配，当场拒绝
	TArray<FCadenceArcTransition> ReleasedEdges;
	SourceNode->CollectTransitions(PressEvent.InputTag, ECadenceArcInputPhase::Released, ReleasedEdges);
	if (ReleasedEdges.IsEmpty())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingTransition);
		return Outcome;
	}

	// 整体计时配置是可选的；没有配置表示有资格但不保护、不自动释放
	const FCadenceArcHoldChargeConfig* ChargeConfig = SourceNode->FindHoldChargeConfig(PressEvent.InputTag);
	double ChargeFullSeconds = 0.0;
	if (ChargeConfig)
	{
		// Initialize 之后资产仍可被编辑，所以这里按当前数据重新推导一次
		if (!CadenceArc::HoldTiming::DeriveChargeFullSeconds(ReleasedEdges, ChargeFullSeconds))
		{
			Outcome.SetRejected(ECadenceArcResolutionReason::InvalidGraphConfiguration);
			return Outcome;
		}
	}

	// 求和溢出取决于宿主传进来的时间戳，与图无关
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		PressEvent.TimestampSeconds, ChargeConfig != nullptr,
		ChargeConfig ? *ChargeConfig : FCadenceArcHoldChargeConfig{}, ChargeFullSeconds);
	if (!CadenceArc::HoldTiming::HasFiniteBounds(Timeline))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// 全部通过，一次性占槽。资格绑定已提交的源节点与当前上下文编号，不是候选目标
	FCadenceArcInputSlot NewSlot;
	NewSlot.SlotState = ECadenceArcInputSlotState::PendingHold;
	NewSlot.InputToken = Token;
	NewSlot.InputEvent = PressEvent;
	NewSlot.bFromHold = true;
	NewSlot.LastObservedTimestampSeconds = PressEvent.TimestampSeconds;
	NewSlot.SourceActionTag = CurrentActionTag;
	NewSlot.GrantedContextId = CurrentContextId;
	NewSlot.bHasChargeConfig = ChargeConfig != nullptr;
	if (ChargeConfig)
	{
		NewSlot.ChargeConfig = *ChargeConfig;
	}
	NewSlot.ReleasedEdges = MoveTemp(ReleasedEdges);
	NewSlot.MaxBufferedInputAgeSeconds = Graph->MaxBufferedInputAgeSeconds;
	NewSlot.ChargeFullSeconds = ChargeFullSeconds; // 无配置时保持 0 且不参与计算
	InputSlot = MoveTemp(NewSlot);

	CheckSlotInvariants();
	Outcome.SetGranted();
	return Outcome;
}

FCadenceArcHoldSnapshot UCadenceArcResolver::GetInputHoldSnapshot() const
{
	FCadenceArcHoldSnapshot Snapshot;
	// 没有待松手资格时返回空快照；BufferedEvent 不是资格，走 GetBufferedInputTag
	if (InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold)
	{
		return Snapshot;
	}

	Snapshot.bHasHold = true;
	Snapshot.bHasChargeConfig = InputSlot.bHasChargeConfig;
	Snapshot.Token = InputSlot.InputToken;
	Snapshot.InputTag = InputSlot.InputEvent.InputTag;
	Snapshot.SourceActionTag = InputSlot.SourceActionTag;
	Snapshot.PressedTimestampSeconds = InputSlot.InputEvent.TimestampSeconds;
	Snapshot.LastObservedTimestampSeconds = InputSlot.LastObservedTimestampSeconds;

	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		InputSlot.InputEvent.TimestampSeconds, InputSlot.bHasChargeConfig,
		InputSlot.ChargeConfig, InputSlot.ChargeFullSeconds);
	// 阶段按最近一次有效观察时间推导；查询本身不推进时间
	Snapshot.Stage = CadenceArc::HoldTiming::StageAt(Timeline, InputSlot.LastObservedTimestampSeconds);

	// 无配置时这些字段保持 0 且不参与判断，必须先看 bHasChargeConfig
	if (InputSlot.bHasChargeConfig)
	{
		Snapshot.ChargeStartSeconds = InputSlot.ChargeConfig.ChargeStartSeconds;
		Snapshot.ChargeFullSeconds = InputSlot.ChargeFullSeconds;
		Snapshot.MaxChargedHoldSeconds = InputSlot.ChargeConfig.MaxChargedHoldSeconds;
		Snapshot.ChargeFullTimestampSeconds = Timeline.ChargeFullTime;
		Snapshot.AutoReleaseTimestampSeconds = Timeline.AutoReleaseTime;
	}
	return Snapshot;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::ConsumeHoldRelease(
	const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent, const double NowSeconds)
{
	FCadenceArcSubmitOutcome Outcome;
	Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized); // 悲观默认

	// 资格绑定授予时的已提交节点与上下文编号：回到同名节点的另一次执行也不能复用
	if (HoldSlot.SourceActionTag != CurrentActionTag || HoldSlot.GrantedContextId != CurrentContextId)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 年龄从释放时刻算起，用授予时复制的 MaxAge；等于上限仍然有效，0 表示禁用
	if (HoldSlot.MaxBufferedInputAgeSeconds > 0.0
		&& NowSeconds - ReleasedEvent.TimestampSeconds > HoldSlot.MaxBufferedInputAgeSeconds)
	{
		Outcome.SetNoAction(ECadenceArcResolutionReason::Expired);
		return Outcome;
	}

	// 停顿看按下时刻，不把蓄力时间算进去；跨 Completed 的按住按 0 秒处理。
	// 没有完成起点时仍保持不可用，不能把它也截断成 0 秒。
	const double PauseDurationSeconds = HoldSlot.SlotState == ECadenceArcInputSlotState::BufferedEvent
		? 0.0
		: (LastCompletionTimestampSeconds >= 0.0
			? FMath::Max(0.0, GetPauseDurationSeconds(HoldSlot.InputEvent)) : -1.0);

	// 用授予时冻结的边副本匹配：等待期间改资产不会改变本次轻重攻击的解释
	const auto Match = MatchTransition(HoldSlot.SourceActionTag, ReleasedEvent, PauseDurationSeconds,
	                                   &HoldSlot.ReleasedEdges);
	if (Match.Reason == ECadenceArcResolutionReason::None)
	{
		Outcome.SetRequestProduced(CommitRequest(ReleasedEvent.InputTag, Match.TargetActionTag));
	}
	else
	{
		Outcome = MakeFailedOutcome(Match.Reason);
	}
	return Outcome;
}

FCadenceArcSubmitOutcome UCadenceArcResolver::FinishHoldRelease(
	const FCadenceArcInputSlot& HoldSlot, const FCadenceArcInputEvent& ReleasedEvent,
	const double ObservedTimestampSeconds)
{
	// 资格到此终结。即使下面解析不出候选，也不恢复成按住状态
	ClearInputSlot();

	FCadenceArcSubmitOutcome Resolution;
	if (State == ECadenceArcResolverState::Executing)
	{
		// 动作还在执行：存成缓冲事件，等 Completed 消费。不重新检查已经关闭的窗口
		FCadenceArcInputSlot BufferedSlot = HoldSlot;
		BufferedSlot.SlotState = ECadenceArcInputSlotState::BufferedEvent;
		BufferedSlot.InputEvent = ReleasedEvent;
		BufferedSlot.LastObservedTimestampSeconds = ObservedTimestampSeconds;
		InputSlot = MoveTemp(BufferedSlot);
		Resolution.SetBuffered();
	}
	else if (State == ECadenceArcResolverState::Ready)
	{
		Resolution = ConsumeHoldRelease(HoldSlot, ReleasedEvent, ObservedTimestampSeconds);
	}
	else
	{
		// AwaitingStart 下槽必为空，走不到这里；保留悲观结果而不是假装成功
		Resolution.SetNoAction(ECadenceArcResolutionReason::RequestPending);
	}

	CheckSlotInvariants();
	return Resolution;
}

FCadenceArcInputAdvanceOutcome UCadenceArcResolver::ReleaseInputHoldImpl(
	const FCadenceArcInputToken& Token, const FCadenceArcInputEvent& ReleaseEvent)
{
	FCadenceArcInputAdvanceOutcome Outcome; // 默认拒绝且空载荷
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}

	// 身份：无效 Token、槽里没有资格、Token 不匹配，对调用方是同一件事
	if (!Token.IsValid()
		|| InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold
		|| !(InputSlot.InputToken == Token))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 事件本身：必须是同一个 Tag 的合法 Released 事件
	if (!ReleaseEvent.IsValid()
		|| ReleaseEvent.InputPhase != ECadenceArcInputPhase::Released
		|| ReleaseEvent.InputTag != InputSlot.InputEvent.InputTag)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// 时间不能早于已观察到的时刻，否则旧时间戳可以绕过已经成立的蓄力保护
	if (ReleaseEvent.TimestampSeconds < InputSlot.LastObservedTimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// duration 必须对应本次按下，不能听调用方随便填；两边用同一次 double 运算
	if (ReleaseEvent.HeldDurationSeconds
		!= ReleaseEvent.TimestampSeconds - InputSlot.InputEvent.TimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidInputEvent);
		return Outcome;
	}

	// 校验全部通过，从这里开始改状态
	const FCadenceArcInputSlot HoldCopy = InputSlot;
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		HoldCopy.InputEvent.TimestampSeconds, HoldCopy.bHasChargeConfig,
		HoldCopy.ChargeConfig, HoldCopy.ChargeFullSeconds);

	// 物理松手晚于自动截止：按 HoldLimit 释放，事件时刻用截止时刻而不是松手时刻
	FCadenceArcInputEvent EffectiveRelease = ReleaseEvent;
	ECadenceArcInputReleaseSource ReleaseSource = ECadenceArcInputReleaseSource::Manual;
	if (Timeline.bHasCharge && ReleaseEvent.TimestampSeconds >= Timeline.AutoReleaseTime)
	{
		ReleaseSource = ECadenceArcInputReleaseSource::HoldLimit;
		EffectiveRelease.TimestampSeconds = Timeline.AutoReleaseTime;
		EffectiveRelease.HeldDurationSeconds = Timeline.AutoReleaseTime - Timeline.PressedTime;
		// 到期后才收到物理松手也属于自动释放，不能采用较晚的松手方向。
		EffectiveRelease.ContextTags = HoldCopy.InputEvent.ContextTags;
	}

	Outcome.SetAccepted(Token);

	// 补齐这段时间里跨过的阶段，截止到实际生效的释放时刻
	TArray<CadenceArc::HoldTiming::FStageCrossing> Crossings;
	CadenceArc::HoldTiming::CollectStageCrossings(
		Timeline, HoldCopy.LastObservedTimestampSeconds, EffectiveRelease.TimestampSeconds, Crossings);
	for (const CadenceArc::HoldTiming::FStageCrossing& Crossing : Crossings)
	{
		Outcome.AddStageChange(Crossing.ToStage, Crossing.EffectiveTimestampSeconds);
	}

	// 实际消费与过期判断用真实观察到的时刻，而不是可能被压回截止点的事件时刻
	const FCadenceArcSubmitOutcome Resolution =
		FinishHoldRelease(HoldCopy, EffectiveRelease, ReleaseEvent.TimestampSeconds);

	Outcome.SetRelease(EffectiveRelease, ReleaseSource, Resolution);
	return Outcome;
}


FCadenceArcInputAdvanceOutcome UCadenceArcResolver::AdvanceInputTimeImpl(const double NowSeconds)
{
	FCadenceArcInputAdvanceOutcome Outcome; // 默认拒绝且空载荷
	if (!IsInitialized())
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NotInitialized);
		return Outcome;
	}
	if (!FMath::IsFinite(NowSeconds) || NowSeconds < 0.0)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	// 没有待松手资格：接受的空操作。不创建输入，也不动普通缓存和窗口
	if (InputSlot.SlotState != ECadenceArcInputSlotState::PendingHold)
	{
		Outcome.SetAccepted(FCadenceArcInputToken{});
		return Outcome;
	}

	// 时间倒退不推进、不产生阶段变化，也不终结资格
	if (NowSeconds < InputSlot.LastObservedTimestampSeconds)
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::InvalidTimestamp);
		return Outcome;
	}

	const FCadenceArcInputSlot HoldCopy = InputSlot;
	const CadenceArc::HoldTiming::FTimeline Timeline = CadenceArc::HoldTiming::MakeTimeline(
		HoldCopy.InputEvent.TimestampSeconds, HoldCopy.bHasChargeConfig,
		HoldCopy.ChargeConfig, HoldCopy.ChargeFullSeconds);

	Outcome.SetAccepted(HoldCopy.InputToken);

	// 无整体计时配置时 bHasCharge 为假：既没有阈值可跨，也没有自动截止
	const bool bAutoRelease = Timeline.bHasCharge && NowSeconds >= Timeline.AutoReleaseTime;
	// 一次跨过多个阈值时按时间升序全部报告，但不越过实际生效的释放时刻
	TArray<CadenceArc::HoldTiming::FStageCrossing> Crossings;
	CadenceArc::HoldTiming::CollectStageCrossings(
		Timeline, HoldCopy.LastObservedTimestampSeconds,
		bAutoRelease ? Timeline.AutoReleaseTime : NowSeconds, Crossings);
	for (const CadenceArc::HoldTiming::FStageCrossing& Crossing : Crossings)
	{
		Outcome.AddStageChange(Crossing.ToStage, Crossing.EffectiveTimestampSeconds);
	}

	if (!bAutoRelease)
	{
		// 资格继续有效，只把观察时间推到 Now
		InputSlot.LastObservedTimestampSeconds = NowSeconds;
		CheckSlotInvariants();
		return Outcome;
	}

	// 到达保持上限：合成一次释放，事件时刻用截止时刻，duration = 截止 − 按下
	FCadenceArcInputEvent AutoRelease;
	AutoRelease.InputTag = HoldCopy.InputEvent.InputTag;
	AutoRelease.ContextTags = HoldCopy.InputEvent.ContextTags;
	AutoRelease.InputPhase = ECadenceArcInputPhase::Released;
	AutoRelease.TimestampSeconds = Timeline.AutoReleaseTime;
	AutoRelease.HeldDurationSeconds = Timeline.AutoReleaseTime - Timeline.PressedTime;

	// 实际消费与缓存年龄用调用时刻 Now：晚调用一帧就该按晚了一帧算过期
	const FCadenceArcSubmitOutcome Resolution = FinishHoldRelease(HoldCopy, AutoRelease, NowSeconds);
	Outcome.SetRelease(AutoRelease, ECadenceArcInputReleaseSource::HoldLimit, Resolution);
	return Outcome;
}

FCadenceArcHoldOutcome UCadenceArcResolver::CancelInputHoldImpl(const FCadenceArcInputToken& Token)
{
	FCadenceArcHoldOutcome Outcome; // 默认 Rejected / None

	// 可取消的只有按住资格本身，以及它释放后尚未消费的缓冲事件；
	// 普通 Pressed 缓存不带身份，不能被取消调用清掉
	const bool bHasCancellableHold =
		InputSlot.SlotState == ECadenceArcInputSlotState::PendingHold
		|| (InputSlot.SlotState == ECadenceArcInputSlotState::BufferedEvent && InputSlot.bFromHold);

	// 无效 Token、没有资格、Token 不匹配，对调用方是同一件事：这个身份没有可取消的资格
	if (!Token.IsValid() || !bHasCancellableHold || !(InputSlot.InputToken == Token))
	{
		Outcome.SetRejected(ECadenceArcResolutionReason::NoMatchingHold);
		return Outcome;
	}

	// 取消只清槽：不合成松手事件、不产生候选，也不撤销 AwaitingStart 里已提交的请求
	ClearInputSlot();
	CheckSlotInvariants();
	Outcome.SetCancelled();
	return Outcome;
}
