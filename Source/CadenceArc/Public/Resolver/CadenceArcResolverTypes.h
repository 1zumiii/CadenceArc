#pragma once
#include "CoreMinimal.h"
#include "CadenceArcResolverTypes.generated.h"

UENUM(BlueprintType)
enum class ECadenceArcResolverInitResult : uint8
{
	Success,
	InvalidGraph,
	InvalidEntryActionTag,
	EntryNodeNotFound
};

UENUM(BlueprintType)
enum class ECadenceArcResolverTransitionResult : uint8
{
	Success,
	NotInitialized,
	InvalidInputTag,
	CurrentNodeNotFound,
	NoMatchingTransition,
	TargetNodeNotFound
};
