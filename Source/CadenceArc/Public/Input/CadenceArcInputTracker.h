#pragma once
#include "CadenceArcInputTrackingTypes.h"

struct FGameplayTag;
struct FCadenceArcInputToken;

class CADENCEARC_API FCadenceArcInputTracker
{
public:
	// 禁用复制和拷贝赋值运算符
	UE_NONCOPYABLE(FCadenceArcInputTracker);

	FCadenceArcInputTracker();

	FGuid GetSourceSession() const;
	FCadenceArcInputTrackingOutcome Press(FGameplayTag Tag, double Now);
	FCadenceArcInputTrackingOutcome Release(const FCadenceArcInputToken& Token, double Now);
	FCadenceArcInputTrackingOutcome Cancel(const FCadenceArcInputToken& Token);
	void ClearAll();
	bool HasPressedInput(const FGameplayTag Tag) const;

private:
	FGuid SessionId;
	int64 NextPressId = 1;
	TMap<FGameplayTag, FPressedInputRecord> ActivePresses;
};
