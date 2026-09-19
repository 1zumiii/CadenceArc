#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "CadenceArcResolverEnums.h"
#include "Input/CadenceArcInputTypes.h"
#include "CadenceArcResolverTypes.generated.h"

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Resolver")
	FGameplayTag InputTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Resolver")
	double TimestampSeconds = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Resolver")
	ECadenceArcInputPhase InputPhase = ECadenceArcInputPhase::Pressed;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="CadenceArc|Resolver")
	double HeldDurationSeconds = 0.0;

	bool IsValid() const
	{
		if (InputTag.IsValid() && IsValidTimestamp() && IsValidHeldDuration() && IsValidInputPhase())
		{
			if (InputPhase == ECadenceArcInputPhase::Pressed)
			{
				return HeldDurationSeconds == 0.0;
			}
			if (InputPhase == ECadenceArcInputPhase::Released)
			{
				return HeldDurationSeconds >= 0.0 && TimestampSeconds >= HeldDurationSeconds;
			}
		}
		return false;
	}

	bool IsValidTimestamp() const
	{
		return TimestampSeconds >= 0.0 && FMath::IsFinite(TimestampSeconds);
	}

	bool IsValidHeldDuration() const
	{
		return HeldDurationSeconds >= 0.0 && FMath::IsFinite(HeldDurationSeconds);
	}

	bool IsValidInputPhase() const
	{
		return StaticEnum<ECadenceArcInputPhase>()->IsValidEnumValue(static_cast<int64>(InputPhase));
	}
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

// Resolver 输出合法结果组合、调用者只消费结果
USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcActionCompletionOutcome
{
	GENERATED_BODY()

	[[nodiscard]] ECadenceArcHandshakeResult GetHandshakeResult() const { return HandshakeResult; }

	[[nodiscard]] ECadenceArcResolutionCategory GetBufferConsumption() const { return BufferConsumption; }

	[[nodiscard]] ECadenceArcResolutionReason GetBufferConsumptionReason() const { return BufferConsumptionReason; }

	[[nodiscard]] FCadenceArcActionRequest GetNextActionRequest() const { return NextActionRequest; }

	friend class UCadenceArcResolver;

public:
	// 握手失败时 BufferConsumption 字段无意义,不能单看它
	// 如果蓝图里需要调用后续需要补在Blueprint Function Library
	bool HasNextActionRequest() const
	{
		return HandshakeResult == ECadenceArcHandshakeResult::Success
			&& BufferConsumption == ECadenceArcResolutionCategory::RequestProduced;
	}

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcHandshakeResult HandshakeResult = ECadenceArcHandshakeResult::NotInitialized;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionCategory BufferConsumption = ECadenceArcResolutionCategory::Rejected;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionReason BufferConsumptionReason = ECadenceArcResolutionReason::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	FCadenceArcActionRequest NextActionRequest;

	void SetHandshakeRejected(const ECadenceArcHandshakeResult InHandshakeResult)
	{
		HandshakeResult = InHandshakeResult;
		BufferConsumption = ECadenceArcResolutionCategory::Rejected;
		BufferConsumptionReason = ECadenceArcResolutionReason::None;
		NextActionRequest = FCadenceArcActionRequest{};
	}

	void SetBufferConsumption(
		const ECadenceArcResolutionCategory InCategory,
		const ECadenceArcResolutionReason InReason,
		const FCadenceArcActionRequest& InRequest = FCadenceArcActionRequest{}
	)
	{
		HandshakeResult = ECadenceArcHandshakeResult::Success;
		BufferConsumption = InCategory;
		BufferConsumptionReason = InReason;
		NextActionRequest = InRequest;
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcSubmitOutcome
{
	GENERATED_BODY()

	[[nodiscard]] ECadenceArcResolutionCategory GetCategory() const { return Category; }

	[[nodiscard]] ECadenceArcResolutionReason GetReason() const { return Reason; }

	[[nodiscard]] FCadenceArcActionRequest GetActionRequest() const { return ActionRequest; }

	friend class UCadenceArcResolver;

public:
	// 绑定 Category,而不是检查 RequestId 是否非零
	// 如果蓝图里需要调用后续需要补在Blueprint Function Library
	bool HasActionRequest() const
	{
		return Category == ECadenceArcResolutionCategory::RequestProduced;
	}

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionCategory Category = ECadenceArcResolutionCategory::Rejected;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	FCadenceArcActionRequest ActionRequest;

	// 只有 Resolver 能调,外部(含 Blueprint)拿到的永远是这四种组合之一
	void SetRejected(const ECadenceArcResolutionReason InReason)
	{
		Category = ECadenceArcResolutionCategory::Rejected;
		Reason = InReason;
		ActionRequest = FCadenceArcActionRequest{};
	}

	void SetNoAction(const ECadenceArcResolutionReason InReason)
	{
		Category = ECadenceArcResolutionCategory::NoAction;
		Reason = InReason;
		ActionRequest = FCadenceArcActionRequest{};
	}

	void SetBuffered()
	{
		Category = ECadenceArcResolutionCategory::Buffered;
		Reason = ECadenceArcResolutionReason::None;
		ActionRequest = FCadenceArcActionRequest{};
	}

	void SetRequestProduced(const FCadenceArcActionRequest& InRequest)
	{
		Category = ECadenceArcResolutionCategory::RequestProduced;
		Reason = ECadenceArcResolutionReason::None;
		ActionRequest = InRequest;
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputStageChange
{
	GENERATED_BODY()

	// 跨过阈值之后到达的阶段。只会是 Charging 或 Charged。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	ECadenceArcHoldStage ToStage = ECadenceArcHoldStage::None;

	// 阈值本身的时刻，不是调用 Advance 的时刻。
	// 宿主要在正确的时间点播特效时用它，而不是用 Now。
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double EffectiveTimestampSeconds = 0.0;
};

// 推进结果 Resolver专用，外部只读
USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputAdvanceOutcome
{
	GENERATED_BODY()

	friend class UCadenceArcResolver;

	[[nodiscard]] bool IsAccepted() const { return bAccepted; }
	[[nodiscard]] ECadenceArcResolutionReason GetReason() const { return Reason; }
	[[nodiscard]] FCadenceArcInputToken GetToken() const { return Token; }
	[[nodiscard]] const TArray<FCadenceArcInputStageChange>& GetStageChanges() const { return StageChanges; }
	[[nodiscard]] ECadenceArcInputReleaseSource GetReleaseSource() const { return ReleaseSource; }
	[[nodiscard]] FCadenceArcInputEvent GetReleasedInput() const { return ReleasedInput; }
	[[nodiscard]] const FCadenceArcSubmitOutcome& GetResolution() const { return Resolution; }

	// 本次调用是否真的产生了一次释放
	bool HasRelease() const
	{
		return bAccepted && ReleaseSource != ECadenceArcInputReleaseSource::None;
	}

	// 有请求必须同时满足：调用被接受、确实释放了、解析产生了候选
	bool HasActionRequest() const
	{
		return HasRelease() && Resolution.HasActionRequest();
	}

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	bool bAccepted = false;                    // 默认拒绝
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	FCadenceArcInputToken Token;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	TArray<FCadenceArcInputStageChange> StageChanges;   // 按时间升序
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcInputReleaseSource ReleaseSource = ECadenceArcInputReleaseSource::None;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	FCadenceArcInputEvent ReleasedInput;               // 仅 HasRelease 时有效
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	FCadenceArcSubmitOutcome Resolution;               // 仅 HasRelease 时有意义

	// ---- 只有 Resolver 能填 ----
	void SetRejected(const ECadenceArcResolutionReason InReason)
	{
		bAccepted = false;
		Reason = InReason;
		Token = FCadenceArcInputToken{};
		StageChanges.Reset();
		ReleaseSource = ECadenceArcInputReleaseSource::None;
		ReleasedInput = FCadenceArcInputEvent{};
		Resolution = FCadenceArcSubmitOutcome{};
	}

	// 接受但尚未释放：可能带阶段变化，ReleaseSource 保持 None
	void SetAccepted(const FCadenceArcInputToken& InToken)
	{
		bAccepted = true;
		Reason = ECadenceArcResolutionReason::None;
		Token = InToken;
	}

	void AddStageChange(const ECadenceArcHoldStage ToStage, const double EffectiveTimestampSeconds)
	{
		StageChanges.Add(FCadenceArcInputStageChange{ToStage, EffectiveTimestampSeconds});
	}

	// 释放发生了才调用。资格到此终结，即使 Resolution 是 NoAction 或 Rejected。
	void SetRelease(
		const FCadenceArcInputEvent& InReleasedInput,
		const ECadenceArcInputReleaseSource InSource,
		const FCadenceArcSubmitOutcome& InResolution)
	{
		ReleasedInput = InReleasedInput;
		ReleaseSource = InSource;
		Resolution = InResolution;
	}
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcHoldSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	bool bHasHold = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	bool bHasChargeConfig = false;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FCadenceArcInputToken Token;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag InputTag;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag SourceActionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	ECadenceArcHoldStage Stage = ECadenceArcHoldStage::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double PressedTimestampSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double ChargeStartSeconds = 0.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double ChargeFullSeconds = 0.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double MaxChargedHoldSeconds = 0.0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double LastObservedTimestampSeconds = 0.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double ChargeFullTimestampSeconds = 0.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	double AutoReleaseTimestampSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcHoldOutcome
{
	GENERATED_BODY()

	friend class UCadenceArcResolver;

	[[nodiscard]] ECadenceArcHoldResult GetResult() const { return Result; }

	[[nodiscard]] ECadenceArcResolutionReason GetReason() const { return Reason; }
private:
	// 只有 Resolver 能构造结果，外部拿到的永远是合法组合
	void SetRejected(const ECadenceArcResolutionReason InReason)
	{
		Result = ECadenceArcHoldResult::Rejected;
		Reason = InReason;
	}

	void SetGranted()
	{
		Result = ECadenceArcHoldResult::Granted;
		Reason = ECadenceArcResolutionReason::None;
	}

	// 取消只清掉资格，不合成松手事件，也不产生候选动作
	void SetCancelled()
	{
		Result = ECadenceArcHoldResult::Cancelled;
		Reason = ECadenceArcResolutionReason::None;
	}

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcHoldResult Result = ECadenceArcHoldResult::Rejected;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;
};
