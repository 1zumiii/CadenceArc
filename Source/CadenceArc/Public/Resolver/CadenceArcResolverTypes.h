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
struct CADENCEARC_API FCadenceArcGestureSnapshot
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	bool bHasGesture = false;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FCadenceArcInputToken Token;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag InputTag;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	FGameplayTag SourceActionTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver")
	ECadenceArcGestureStage Stage = ECadenceArcGestureStage::None;

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
struct CADENCEARC_API FCadenceArcGestureOutcome
{
	GENERATED_BODY()

	friend class UCadenceArcResolver;

	[[nodiscard]] ECadenceArcGestureResult GetResult() const { return Result; }

	[[nodiscard]] ECadenceArcResolutionReason GetReason() const { return Reason; }

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcGestureResult Result = ECadenceArcGestureResult::Rejected;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Resolver", meta=(AllowPrivateAccess="true"))
	ECadenceArcResolutionReason Reason = ECadenceArcResolutionReason::None;
};
