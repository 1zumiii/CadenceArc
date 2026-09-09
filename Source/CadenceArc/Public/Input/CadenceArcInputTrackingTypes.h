#pragma once

#include "CoreMinimal.h"
#include "CadenceArcInputTypes.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "CadenceArcInputTrackingTypes.generated.h"

// 结果类别：默认 Rejected，本身就是悲观值，不需要像 HandshakeResult 那样挪位置
UENUM(BlueprintType)
enum class ECadenceArcInputTrackingResult : uint8
{
	Rejected = 0,
	PressedProduced,
	ReleasedProduced,
	PairCancelled
};

UENUM(BlueprintType)
enum class ECadenceArcInputTrackingReason : uint8
{
	None = 0,
	InvalidInputTag,
	InvalidTimestamp,
	TimeWentBackwards,
	AlreadyPressed,
	NoMatchingPress,
	InvalidToken,
	TokenMismatch,
	IdExhausted
};

USTRUCT(BlueprintType)
struct CADENCEARC_API FCadenceArcInputTrackingOutcome
{
	[[nodiscard]] ECadenceArcInputTrackingResult GetResult() const
	{
		return Result;
	}

	[[nodiscard]] ECadenceArcInputTrackingReason GetReason() const
	{
		return Reason;
	}

	[[nodiscard]] FCadenceArcInputToken GetToken() const
	{
		return Token;
	}

	[[nodiscard]] FCadenceArcInputEvent GetInputEvent() const
	{
		return InputEvent;
	}

	GENERATED_BODY()
	friend class FCadenceArcInputTracker;

public:
	// 只有 PressedProduced / ReleasedProduced 才代表真的产生了事件
	bool HasInput() const
	{
		return Result == ECadenceArcInputTrackingResult::PressedProduced
			|| Result == ECadenceArcInputTrackingResult::ReleasedProduced;
	}

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking",
		meta=(AllowPrivateAccess="true"))
	ECadenceArcInputTrackingResult Result = ECadenceArcInputTrackingResult::Rejected;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking",
		meta=(AllowPrivateAccess="true"))
	ECadenceArcInputTrackingReason Reason = ECadenceArcInputTrackingReason::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking",
		meta=(AllowPrivateAccess="true"))
	FCadenceArcInputToken Token;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking",
		meta=(AllowPrivateAccess="true"))
	FCadenceArcInputEvent InputEvent;

	void SetRejected(const ECadenceArcInputTrackingReason InReason)
	{
		Result = ECadenceArcInputTrackingResult::Rejected;
		Reason = InReason;
		Token = FCadenceArcInputToken{};
		InputEvent = FCadenceArcInputEvent{};
	}

	void SetPressedProduced(const FCadenceArcInputToken& InToken, const FCadenceArcInputEvent& InEvent)
	{
		Result = ECadenceArcInputTrackingResult::PressedProduced;
		Reason = ECadenceArcInputTrackingReason::None;
		Token = InToken;
		InputEvent = InEvent;
	}

	void SetReleasedProduced(const FCadenceArcInputToken& InToken, const FCadenceArcInputEvent& InEvent)
	{
		Result = ECadenceArcInputTrackingResult::ReleasedProduced;
		Reason = ECadenceArcInputTrackingReason::None;
		Token = InToken;
		InputEvent = InEvent;
	}

	void SetPairCancelled()
	{
		Result = ECadenceArcInputTrackingResult::PairCancelled;
		Reason = ECadenceArcInputTrackingReason::None;
		Token = FCadenceArcInputToken{};
		InputEvent = FCadenceArcInputEvent{};
	}
};

USTRUCT(BlueprintType)
struct FPressedInputRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking")
	FCadenceArcInputToken Token;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="CadenceArc|Input|Tracking")
	double PressedTimestampSeconds = 0.0;
};
