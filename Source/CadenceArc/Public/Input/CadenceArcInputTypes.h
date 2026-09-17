#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CadenceArcInputTypes.generated.h"

UENUM(BlueprintType)
enum class ECadenceArcInputPhase : uint8
{
	Pressed = 0,
	Released
};

UENUM(BlueprintType)
enum class ECadenceArcInputMode : uint8
{
	// 按下时直接提交输入，不等待松手来判定持续时间。
	PressOnly = 0,
	// 按下时申请保持资格，在手动或自动释放时按持续时间结算；短按也属于此模式。
	HoldRelease
};

// Resolver 中有效按住资格的观察阶段，由时间和可选蓄力配置推导。
UENUM(BlueprintType)
enum class ECadenceArcHoldStage : uint8
{
	// 没有有效的按住资格；不代表设备当前一定处于松开状态。
	None = 0,
	// 已取得资格但尚未进入蓄力；无蓄力配置时保持此阶段，允许普通输入覆盖。
	Holding,
	// 已达到蓄力起点但尚未蓄满，资格受保护，普通输入不能覆盖。
	Charging,
	// 已达到最高档门槛，资格仍受保护，等待手动释放或保持上限触发自动释放。
	Charged
};

UENUM(BlueprintType)
enum class ECadenceArcInputReleaseSource : uint8
{
	None = 0, Manual, HoldLimit
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputToken
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	FGuid SourceSession;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input")
	int64 PressId = 0;

	bool operator==(const FCadenceArcInputToken& Other) const
	{
		return SourceSession == Other.SourceSession && PressId == Other.PressId;
	}

	bool IsValid() const { return SourceSession.IsValid() && PressId > 0; }
};

// 一条边接受的“最终按住时长”，每条边有自己的一份范围
USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcHeldDurationRange
{
	GENERATED_BODY()

	// 包含的按住时长下限。
	// 普通攻击与各档蓄力分别用一条边表达；下限为0本身不决定攻击类型。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Input")
	double MinHeldDurationSeconds = 0.0;
	// 不包含的按住时长上限。
	// bHasMaxHeldDuration=false时忽略此值，表示无上限。
	// 如果为普通攻击，则为普通攻击和长按攻击达标的分界点
	// 如果为长按攻击，通常没有上限，如果又多个蓄力阶段中间的蓄力阶段，则会有上限（假设为y)，表示这个阶段的上限
	// 例如普通攻击范围 0<=Duration<x，蓄力A范围 x<=Duration<y，蓄力B范围 y<=Duration<z，蓄力C范围 z<=Duration
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Input")
	double MaxHeldDurationSecondsExclusive = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Input")
	bool bHasMaxHeldDuration = false;

	// 使用左闭右开区间；非法范围或非法时长一律不匹配。
	bool Contains(const double Duration) const
	{
		return IsValid() && FMath::IsFinite(Duration) && Duration >= MinHeldDurationSeconds
			&& (!bHasMaxHeldDuration || Duration < MaxHeldDurationSecondsExclusive);
	}

	bool IsValid() const
	{
		return MinHeldDurationSeconds >= 0.0 && FMath::IsFinite(MinHeldDurationSeconds)
			&& (!bHasMaxHeldDuration || (MaxHeldDurationSecondsExclusive > MinHeldDurationSeconds && FMath::IsFinite(
				MaxHeldDurationSecondsExclusive)));
	}

	bool operator==(const FCadenceArcHeldDurationRange& DurationRange) const
	{
		return MinHeldDurationSeconds == DurationRange.MinHeldDurationSeconds && MaxHeldDurationSecondsExclusive ==
			DurationRange.MaxHeldDurationSecondsExclusive && bHasMaxHeldDuration == DurationRange.bHasMaxHeldDuration;
	}
};
