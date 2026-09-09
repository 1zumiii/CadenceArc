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
	PressOnly = 0,
	ReleaseGesture
};

UENUM(BlueprintType)
enum class ECadenceArcGestureStage : uint8
{
	None = 0,
	PendingTap,
	Charging,
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
