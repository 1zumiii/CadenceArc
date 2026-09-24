#include "CadenceArcHoldTiming.h"

#include <cmath>
#include <limits>

namespace CadenceArc::HoldTiming
{
	// 返回第一个满足 (T - Pressed) >= Seconds 的时刻。
	// 阶段与自动截止在绝对时间上比较，选边却比较 Released 事件的时长 (T - Pressed)。
	// 直接用 Pressed + Seconds 可能因为舍入早一个 ulp，导致"阶段已 Charged、时长却不够长按档"；
	// 例如 (10.0 + 0.2) - 10.0 = 0.19999999999999929 < 0.2。这里让两种比较在阈值两侧严格一致。
	//
	// UE 默认以 /fp:fast 编译，允许把 (Pressed + Seconds) - Pressed 化简成 Seconds，
	// 那样下面的循环条件恒为假、整段修正被优化掉。所以这个函数必须强制 IEEE 精确语义。
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(precise, on, push)
#endif
	static double FirstInstantReaching(const double Pressed, const double Seconds)
	{
		double T = Pressed + Seconds;
		if (!FMath::IsFinite(T)) { return T; } // 溢出交给 HasFiniteBounds 拒绝，否则下面会一步步往回挪
		constexpr double Inf = std::numeric_limits<double>::infinity();
		while (T - Pressed < Seconds) { T = std::nextafter(T, Inf); } // 舍入偏早：往后挪
		for (double Prev = std::nextafter(T, -Inf); Prev - Pressed >= Seconds; // 舍入偏晚：往前挪到最早
		     Prev = std::nextafter(T, -Inf)) { T = Prev; }
		return T;
	}
#if defined(_MSC_VER) || defined(__clang__)
#pragma float_control(pop)
#endif

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
		// 三个阈值都取"按住时长刚好达到"的最早时刻，与 Released 边的左闭右开时长比较一致
		Timeline.ChargeStartTime = FirstInstantReaching(PressedTime, Config.ChargeStartSeconds);
		Timeline.ChargeFullTime = FirstInstantReaching(PressedTime, ChargeFullSeconds);
		// 保持上限从蓄满开始算；为 0 表示蓄满立即自动释放。按总时长求截止，
		// 合成的释放时长 (AutoReleaseTime - PressedTime) 因而一定 >= T满，必然落在最高档。
		Timeline.AutoReleaseTime =
			FirstInstantReaching(PressedTime, ChargeFullSeconds + Config.MaxChargedHoldSeconds);
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
