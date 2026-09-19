#pragma once

#include "CoreMinimal.h"
#include "Graph/CadenceArcGraphTypes.h"   // FCadenceArcTransition / FCadenceArcHoldChargeConfig / ECadenceArcHoldStage

namespace CadenceArc::HoldTiming
{
	// 按住资格的时间轴。全部由"按下时刻 + 配置副本 + 边副本"推导，不存进槽。
	// bHasCharge 为 false 时只有 PressedTime 有意义，其余字段保持 0 且不参与任何判断。
	struct FTimeline
	{
		bool bHasCharge = false;
		double PressedTime = 0.0;
		double ChargeStartTime = 0.0;
		double ChargeFullTime = 0.0;
		double AutoReleaseTime = 0.0;
	};

	// ChargeFullSeconds 由 DeriveChargeFullSeconds 推导；bHasCharge 为 false 时忽略后两个参数。
	FTimeline MakeTimeline(
		double PressedTime, bool bHasCharge,
		const FCadenceArcHoldChargeConfig& Config,
		double ChargeFullSeconds
	);

	// 推导出的时刻是否都可用。按下时刻过大时求和可能溢出成 Infinity，调用方据此拒绝。
	bool HasFiniteBounds(const FTimeline& Timeline);

	// 阶段完全由时间推导，不存状态。端点取"大于等于"，与边的左闭右开区间一致。
	ECadenceArcHoldStage StageAt(const FTimeline& Timeline, double Time);

	// 一次跨过的阶段阈值，按时间升序。区间是 (FromExclusive, ToInclusive]：
	// 授予时刻本身不算跨过，所以 T开始 = 0 时不会补一条重复的 Charging。
	struct FStageCrossing
	{
		ECadenceArcHoldStage ToStage = ECadenceArcHoldStage::None;
		double EffectiveTimestampSeconds = 0.0;
	};

	void CollectStageCrossings(
		const FTimeline& Timeline, double FromExclusive, double ToInclusive,
		TArray<FStageCrossing>& OutCrossings
	);

	// 从该 Tag 的全部 Released 边推导最高档门槛 T满：唯一一条无上限边的下限。
	// 失败时 OutChargeFullSeconds 置 0 并返回 false，调用方返回 InvalidGraphConfiguration。
	bool DeriveChargeFullSeconds(
		const TArray<FCadenceArcTransition>& ReleasedEdges, double& OutChargeFullSeconds
	);
}