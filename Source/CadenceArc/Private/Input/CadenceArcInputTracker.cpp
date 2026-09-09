#include "Input/CadenceArcInputTracker.h"

FCadenceArcInputTracker::FCadenceArcInputTracker()
{
	SessionId = FGuid::NewGuid();
}

FGuid FCadenceArcInputTracker::GetSourceSession() const
{
	return SessionId;
}

FCadenceArcInputTrackingOutcome FCadenceArcInputTracker::Press(FGameplayTag Tag, double Now)
{
	FCadenceArcInputTrackingOutcome Out;
	if (!Tag.IsValid())
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::InvalidInputTag);
		return Out;
	}
	if (Now < 0 || !FMath::IsFinite(Now))
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::InvalidTimestamp);
		return Out;
	}
	if (NextPressId == INT64_MAX)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::IdExhausted);
		return Out;
	}
	if (ActivePresses.Contains(Tag))
	{
		// AlreadyPressed
		Out.SetRejected(ECadenceArcInputTrackingReason::AlreadyPressed);
		return Out;
	}
	Out.SetPressedProduced(
		FCadenceArcInputToken{
			.SourceSession = SessionId, .PressId = NextPressId++
		},
		FCadenceArcInputEvent(Tag, Now)
	);
	ActivePresses.Add(Tag, FPressedInputRecord{.Token = Out.Token, .PressedTimestampSeconds = Now});
	return Out;
}

FCadenceArcInputTrackingOutcome FCadenceArcInputTracker::Release(const FCadenceArcInputToken& Token, double Now)
{
	FCadenceArcInputTrackingOutcome Out;

	if (!Token.IsValid())
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::InvalidToken);
		return Out;
	}
	
	if (Token.SourceSession != SessionId)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::TokenMismatch);
		return Out;
	}

	FGameplayTag MatchedTag;
	double PressedTimestampSeconds = 0.0;
	bool bFound = false;

	for (const auto& Pair : ActivePresses)
	{
		if (Pair.Value.Token == Token)
		{
			MatchedTag = Pair.Key;
			PressedTimestampSeconds = Pair.Value.PressedTimestampSeconds;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::NoMatchingPress);
		return Out;
	}
	if (!FMath::IsFinite(Now) || Now < 0.0)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::InvalidTimestamp);
		return Out;
	}
	if (Now < PressedTimestampSeconds)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::TimeWentBackwards);
		return Out;
	}
	ActivePresses.Remove(MatchedTag);
	const double HeldDurationSeconds = Now - PressedTimestampSeconds;

	FCadenceArcInputEvent ReleasedEvent;
	ReleasedEvent.InputTag = MatchedTag;
	ReleasedEvent.TimestampSeconds = Now;
	ReleasedEvent.InputPhase = ECadenceArcInputPhase::Released;
	ReleasedEvent.HeldDurationSeconds = HeldDurationSeconds;

	Out.SetReleasedProduced(Token, ReleasedEvent);
	return Out;
}

FCadenceArcInputTrackingOutcome FCadenceArcInputTracker::Cancel(const FCadenceArcInputToken& Token)
{
	FCadenceArcInputTrackingOutcome Out;

	if (!Token.IsValid())
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::InvalidToken);
		return Out;
	}
	
	if (Token.SourceSession != SessionId)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::TokenMismatch);
		return Out;
	}

	FGameplayTag MatchedTag;
	bool bFound = false;

	for (const auto& Pair : ActivePresses)
	{
		if (Pair.Value.Token == Token)
		{
			MatchedTag = Pair.Key;
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		Out.SetRejected(ECadenceArcInputTrackingReason::NoMatchingPress);
		return Out;
	}
	ActivePresses.Remove(MatchedTag);
	Out.SetPairCancelled();
	return Out;
}

void FCadenceArcInputTracker::ClearAll()
{
	ActivePresses.Empty();
}

bool FCadenceArcInputTracker::HasPressedInput(const FGameplayTag Tag) const
{
	return ActivePresses.Contains(Tag);
}
