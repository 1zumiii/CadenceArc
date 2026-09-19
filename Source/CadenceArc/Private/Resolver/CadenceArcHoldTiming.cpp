#include "CadenceArcHoldTiming.h"

namespace CadenceArc::HoldTiming
{
	FTimeline MakeTimeline(
		const double PressedTime, const bool bHasCharge,
		const FCadenceArcHoldChargeConfig& Config, const double ChargeFullSeconds)
	{
		FTimeline Timeline;
		Timeline.PressedTime = PressedTime;
		Timeline.bHasCharge = bHasCharge;
		if (!bHasCharge)
		{
			// 无整体计时配置：保留待松手资格，但没有蓄力保护，也没有自动截止。
			return Timeline;
		}
		Timeline.ChargeStartTime = PressedTime + Config.ChargeStartSeconds;
		Timeline.ChargeFullTime = PressedTime + ChargeFullSeconds;
		// 保持上限从"蓄满时刻"开始算；为 0 表示蓄满立即自动释放。
		Timeline.AutoReleaseTime = Timeline.ChargeFullTime + Config.MaxChargedHoldSeconds;
		return Timeline;
	}

	bool HasFiniteBounds(const FTimeline& Timeline)
	{
		if (!FMath::IsFinite(Timeline.PressedTime))
		{
			return false;
		}
		if (!Timeline.bHasCharge)
		{
			return true;
		}
		return FMath::IsFinite(Timeline.ChargeStartTime)
			&& FMath::IsFinite(Timeline.ChargeFullTime)
			&& FMath::IsFinite(Timeline.AutoReleaseTime);
	}

	ECadenceArcHoldStage StageAt(const FTimeline& Timeline, const double Time)
	{
		// 无配置恒为 Holding：有效按住资格，尚未进入蓄力，可被真正接受的新输入替换。
		if (!Timeline.bHasCharge)
		{
			return ECadenceArcHoldStage::Holding;
		}
		// 从高到低判断。Time 为 NaN 时两个比较都为假，返回 Holding；调用方应先校验时间。
		if (Time >= Timeline.ChargeFullTime)
		{
			return ECadenceArcHoldStage::Charged;
		}
		if (Time >= Timeline.ChargeStartTime)
		{
			return ECadenceArcHoldStage::Charging;
		}
		return ECadenceArcHoldStage::Holding;
	}

	void CollectStageCrossings(
		const FTimeline& Timeline, const double FromExclusive, const double ToInclusive,
		TArray<FStageCrossing>& OutCrossings)
	{
		OutCrossings.Reset();
		if (!Timeline.bHasCharge)
		{
			return; // 无整体计时配置不产生阶段变化
		}
		// 固定 Charging -> Charged 的时间顺序；一次跨过两个阈值时两条都要报告
		if (Timeline.ChargeStartTime > FromExclusive && Timeline.ChargeStartTime <= ToInclusive)
		{
			OutCrossings.Add(FStageCrossing{ECadenceArcHoldStage::Charging, Timeline.ChargeStartTime});
		}
		if (Timeline.ChargeFullTime > FromExclusive && Timeline.ChargeFullTime <= ToInclusive)
		{
			OutCrossings.Add(FStageCrossing{ECadenceArcHoldStage::Charged, Timeline.ChargeFullTime});
		}
	}

	bool DeriveChargeFullSeconds(
		const TArray<FCadenceArcTransition>& ReleasedEdges, double& OutChargeFullSeconds)
	{
		OutChargeFullSeconds = 0.0;

		double Candidate = 0.0;
		int32 UnboundedCount = 0;
		for (const FCadenceArcTransition& Edge : ReleasedEdges)
		{
			// 调用方只应传入同一个 Tag 的 Released 边；混入其他边说明取边的逻辑有问题。
			if (Edge.InputPhase != ECadenceArcInputPhase::Released)
			{
				return false;
			}
			if (!Edge.bUseDurationRange)
			{
				// 未启用范围等价于 [0, 无上限)：算作无上限档，但下限为 0。
				// 启用整体计时配置时这种边不合法，下面的 Candidate > 0 检查会拦住它。
				++UnboundedCount;
				Candidate = 0.0;
				continue;
			}
			if (!Edge.DurationRange.IsValid())
			{
				return false;
			}
			if (!Edge.DurationRange.bHasMaxHeldDuration)
			{
				++UnboundedCount;
				Candidate = Edge.DurationRange.MinHeldDurationSeconds;
			}
		}

		// 最高档必须恰好一条，且门槛为正：否则"蓄满"没有确定含义。
		if (UnboundedCount != 1 || !(Candidate > 0.0) || !FMath::IsFinite(Candidate))
		{
			return false;
		}
		OutChargeFullSeconds = Candidate;
		return true;
	}
}
