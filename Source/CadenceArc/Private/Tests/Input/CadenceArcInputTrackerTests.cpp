#include "Input/CadenceArcInputTracker.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	static void ExpectOutcome(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FCadenceArcInputTrackingOutcome& Actual,
		const ECadenceArcInputTrackingResult ExpectedResult,
		const ECadenceArcInputTrackingReason ExpectedReason)
	{
		Test.TestEqual(*FString::Printf(TEXT("%s result"), What),
		               static_cast<uint8>(Actual.GetResult()), static_cast<uint8>(ExpectedResult));
		Test.TestEqual(*FString::Printf(TEXT("%s reason"), What),
		               static_cast<uint8>(Actual.GetReason()), static_cast<uint8>(ExpectedReason));
	}

	static void ExpectEmptyPayload(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcInputTrackingOutcome& Actual)
	{
		Test.TestFalse(*FString::Printf(TEXT("%s has no input"), What), Actual.HasInput());
		Test.TestFalse(*FString::Printf(TEXT("%s has no token"), What), Actual.GetToken().IsValid());
		Test.TestFalse(*FString::Printf(TEXT("%s event has no tag"), What), Actual.GetInputEvent().InputTag.IsValid());
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputEventPhaseValidityTest,
		"CadenceArc.Input.Event.PhaseValidity",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputEventPhaseValidityTest::RunTest(const FString& Parameters)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = Input_Light;
		Event.TimestampSeconds = 0.0;
		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		Event.HeldDurationSeconds = 0.0;
		TestTrue(TEXT("Pressed at zero with zero duration is valid"), Event.IsValid());

		Event.InputPhase = ECadenceArcInputPhase::Released;
		TestTrue(TEXT("Released at zero with zero duration is valid"), Event.IsValid());
		Event.TimestampSeconds = 2.0;
		Event.HeldDurationSeconds = 2.0;
		TestTrue(TEXT("Released timestamp equal to duration is valid"), Event.IsValid());
		Event.HeldDurationSeconds = 2.25;
		TestFalse(TEXT("Released timestamp before duration is invalid"), Event.IsValid());

		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		Event.HeldDurationSeconds = 0.25;
		TestFalse(TEXT("Pressed with non-zero duration is invalid"), Event.IsValid());
		Event.HeldDurationSeconds = 0.0;
		Event.InputPhase = static_cast<ECadenceArcInputPhase>(2);
		TestFalse(TEXT("Only the two declared input phases are valid"), Event.IsValid());
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTrackingOutcomeDefaultsTest,
		"CadenceArc.Input.Tracker.DefaultOutcomes",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTrackingOutcomeDefaultsTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputTrackingOutcome DefaultOutcome;
		ExpectOutcome(*this, TEXT("Default outcome"), DefaultOutcome,
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::None);
		ExpectEmptyPayload(*this, TEXT("Default outcome"), DefaultOutcome);

		FCadenceArcInputTracker Tracker;
		TestTrue(TEXT("Tracker creates a valid source session"), Tracker.GetSourceSession().IsValid());
		TestFalse(TEXT("Fresh tracker has no active light press"), Tracker.HasPressedInput(Input_Light));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTrackerRoundTripTest,
		"CadenceArc.Input.Tracker.PressReleaseRoundTrip",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTrackerRoundTripTest::RunTest(const FString& Parameters)
	{
		FCadenceArcInputTracker Tracker;
		const FCadenceArcInputTrackingOutcome Pressed = Tracker.Press(Input_Light, 1.25);
		ExpectOutcome(*this, TEXT("Press"), Pressed,
		              ECadenceArcInputTrackingResult::PressedProduced, ECadenceArcInputTrackingReason::None);
		TestTrue(TEXT("Press produces an input"), Pressed.HasInput());
		TestEqual(TEXT("First press ID is one"), Pressed.GetToken().PressId, static_cast<int64>(1));
		TestEqual(TEXT("Pressed event phase"), static_cast<uint8>(Pressed.GetInputEvent().InputPhase),
		          static_cast<uint8>(ECadenceArcInputPhase::Pressed));
		TestEqual(TEXT("Pressed event duration"), Pressed.GetInputEvent().HeldDurationSeconds, 0.0);

		const FCadenceArcInputTrackingOutcome Released = Tracker.Release(Pressed.GetToken(), 2.0);
		ExpectOutcome(*this, TEXT("Release"), Released,
		              ECadenceArcInputTrackingResult::ReleasedProduced, ECadenceArcInputTrackingReason::None);
		TestTrue(TEXT("Release produces an input"), Released.HasInput());
		TestEqual(TEXT("Release preserves token"), Released.GetToken().PressId, Pressed.GetToken().PressId);
		TestEqual(TEXT("Released event phase"), static_cast<uint8>(Released.GetInputEvent().InputPhase),
		          static_cast<uint8>(ECadenceArcInputPhase::Released));
		TestEqual(TEXT("Released event duration"), Released.GetInputEvent().HeldDurationSeconds, 0.75);
		TestTrue(TEXT("Released event is valid"), Released.GetInputEvent().IsValid());
		TestFalse(TEXT("Release removes active press"), Tracker.HasPressedInput(Input_Light));
		TestTrue(TEXT("Release preserves complete token"), Released.GetToken() == Pressed.GetToken());
		TestEqual(TEXT("Released event preserves tag"), Released.GetInputEvent().InputTag, FGameplayTag(Input_Light));
		TestEqual(TEXT("Released event timestamp is release time"), Released.GetInputEvent().TimestampSeconds, 2.0);
		const auto InstantPress = Tracker.Press(Input_Heavy, 3.0);
		const auto InstantRelease = Tracker.Release(InstantPress.GetToken(), 3.0);
		ExpectOutcome(*this, TEXT("Same-time release"), InstantRelease,
		              ECadenceArcInputTrackingResult::ReleasedProduced, ECadenceArcInputTrackingReason::None);
		TestEqual(TEXT("Same-time release phase"), static_cast<uint8>(InstantRelease.GetInputEvent().InputPhase),
		          static_cast<uint8>(ECadenceArcInputPhase::Released));
		TestEqual(TEXT("Same-time duration is zero"), InstantRelease.GetInputEvent().HeldDurationSeconds, 0.0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTrackerPressRejectionTest,
		"CadenceArc.Input.Tracker.PressRejectionsAreAtomic",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTrackerPressRejectionTest::RunTest(const FString& Parameters)
	{
		FCadenceArcInputTracker InvalidTagTracker;
		const auto BadTag = InvalidTagTracker.Press(FGameplayTag::EmptyTag, 1.0);
		ExpectOutcome(*this, TEXT("Invalid tag"), BadTag,
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::InvalidInputTag);
		ExpectEmptyPayload(*this, TEXT("Invalid tag"), BadTag);
		TestFalse(TEXT("Invalid tag was not stored"), InvalidTagTracker.HasPressedInput(FGameplayTag::EmptyTag));
		TestEqual(TEXT("Invalid tag does not consume ID"), InvalidTagTracker.Press(Input_Light, 1.0).GetToken().PressId, static_cast<int64>(1));
		for (const double InvalidTime : InvalidTimes())
		{
			FCadenceArcInputTracker InvalidTimeTracker;
			const FCadenceArcInputTrackingOutcome Rejected = InvalidTimeTracker.Press(Input_Light, InvalidTime);
			ExpectOutcome(*this, TEXT("Invalid timestamp"), Rejected,
			              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::InvalidTimestamp);
			ExpectEmptyPayload(*this, TEXT("Invalid timestamp"), Rejected);
			TestFalse(TEXT("Rejected press leaves tag inactive"), InvalidTimeTracker.HasPressedInput(Input_Light));
			const auto Valid = InvalidTimeTracker.Press(Input_Light, 1.0);
			ExpectOutcome(*this, TEXT("Valid press after rejection"), Valid,
			              ECadenceArcInputTrackingResult::PressedProduced, ECadenceArcInputTrackingReason::None);
			TestEqual(TEXT("Rejected time does not consume ID"), Valid.GetToken().PressId, static_cast<int64>(1));
		}

		FCadenceArcInputTracker Tracker;
		const FCadenceArcInputTrackingOutcome First = Tracker.Press(Input_Light, 1.0);
		const FCadenceArcInputTrackingOutcome Duplicate = Tracker.Press(Input_Light, 2.0);
		ExpectOutcome(*this, TEXT("Duplicate press"), Duplicate,
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::AlreadyPressed);
		TestTrue(TEXT("Duplicate rejection preserves original press"), Tracker.HasPressedInput(Input_Light));
		const FCadenceArcInputTrackingOutcome Heavy = Tracker.Press(Input_Heavy, 2.0);
		TestEqual(TEXT("Invalid and duplicate presses do not consume IDs"), Heavy.GetToken().PressId, static_cast<int64>(2));
		TestTrue(TEXT("Both tags have independent pairs"), Tracker.HasPressedInput(Input_Light) && Tracker.HasPressedInput(Input_Heavy));
		const auto LightRelease = Tracker.Release(First.GetToken(), 3.0);
		TestEqual(TEXT("Duplicate press did not change press time"), LightRelease.GetInputEvent().HeldDurationSeconds, 2.0);
		TestTrue(TEXT("Releasing light preserves heavy"), Tracker.HasPressedInput(Input_Heavy));
		const auto HeavyRelease = Tracker.Release(Heavy.GetToken(), 2.5);
		TestEqual(TEXT("Heavy uses its own press time"), HeavyRelease.GetInputEvent().HeldDurationSeconds, 0.5);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTrackerReleaseValidationTest,
		"CadenceArc.Input.Tracker.ReleaseValidationAndTokens",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTrackerReleaseValidationTest::RunTest(const FString& Parameters)
	{
		FCadenceArcInputTracker Tracker;
		const FCadenceArcInputTrackingOutcome Pressed = Tracker.Press(Input_Light, 5.0);
		ExpectOutcome(*this, TEXT("Earlier release"), Tracker.Release(Pressed.GetToken(), 4.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::TimeWentBackwards);
		TestTrue(TEXT("Earlier release preserves active pair"), Tracker.HasPressedInput(Input_Light));
		for (const double InvalidTime : InvalidTimes())
		{
			FCadenceArcInputTracker InvalidTimeTracker;
			const auto Pair = InvalidTimeTracker.Press(Input_Light, 5.0);
			const auto Rejected = InvalidTimeTracker.Release(Pair.GetToken(), InvalidTime);
			ExpectOutcome(*this, TEXT("Invalid release time"), Rejected,
			              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::InvalidTimestamp);
			ExpectEmptyPayload(*this, TEXT("Invalid release time"), Rejected);
			TestTrue(TEXT("Invalid release preserves active pair"), InvalidTimeTracker.HasPressedInput(Input_Light));
			ExpectOutcome(*this, TEXT("Valid release after invalid time"), InvalidTimeTracker.Release(Pair.GetToken(), 6.0),
			              ECadenceArcInputTrackingResult::ReleasedProduced, ECadenceArcInputTrackingReason::None);
		}

		FCadenceArcInputToken InvalidToken;
		ExpectOutcome(*this, TEXT("Invalid token"), Tracker.Release(InvalidToken, 6.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::InvalidToken);
		FCadenceArcInputToken ForeignToken = Pressed.GetToken();
		ForeignToken.SourceSession = FGuid::NewGuid();
		ExpectOutcome(*this, TEXT("Foreign token"), Tracker.Release(ForeignToken, 6.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::TokenMismatch);
		FCadenceArcInputToken UnknownToken = Pressed.GetToken();
		UnknownToken.PressId++;
		ExpectOutcome(*this, TEXT("Unknown same-session token"), Tracker.Release(UnknownToken, 6.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::NoMatchingPress);

		const FCadenceArcInputTrackingOutcome Released = Tracker.Release(Pressed.GetToken(), 6.0);
		ExpectOutcome(*this, TEXT("Valid release after errors"), Released,
		              ECadenceArcInputTrackingResult::ReleasedProduced, ECadenceArcInputTrackingReason::None);
		ExpectOutcome(*this, TEXT("Stale token"), Tracker.Release(Pressed.GetToken(), 7.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::NoMatchingPress);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTrackerCancellationTest,
		"CadenceArc.Input.Tracker.CancelClearAndSessionIsolation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTrackerCancellationTest::RunTest(const FString& Parameters)
	{
		FCadenceArcInputTracker Tracker;
		const FCadenceArcInputTrackingOutcome First = Tracker.Press(Input_Light, 1.0);
		ExpectOutcome(*this, TEXT("Cancel invalid token"), Tracker.Cancel(FCadenceArcInputToken{}),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::InvalidToken);
		auto Foreign = First.GetToken();
		Foreign.SourceSession = FGuid::NewGuid();
		ExpectOutcome(*this, TEXT("Cancel foreign token"), Tracker.Cancel(Foreign),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::TokenMismatch);
		TestTrue(TEXT("Rejected cancels preserve pair"), Tracker.HasPressedInput(Input_Light));
		const FCadenceArcInputTrackingOutcome Cancelled = Tracker.Cancel(First.GetToken());
		ExpectOutcome(*this, TEXT("Cancel"), Cancelled,
		              ECadenceArcInputTrackingResult::PairCancelled, ECadenceArcInputTrackingReason::None);
		ExpectEmptyPayload(*this, TEXT("Cancel"), Cancelled);
		TestFalse(TEXT("Cancel removes active press"), Tracker.HasPressedInput(Input_Light));
		const FCadenceArcInputTrackingOutcome Second = Tracker.Press(Input_Light, 2.0);
		TestEqual(TEXT("Cancel preserves ID continuity"), Second.GetToken().PressId, static_cast<int64>(2));
		ExpectOutcome(*this, TEXT("Old cancel cannot clear new press"), Tracker.Cancel(First.GetToken()),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::NoMatchingPress);
		TestTrue(TEXT("Stale cancel preserves replacement"), Tracker.HasPressedInput(Input_Light));

		Tracker.ClearAll();
		TestFalse(TEXT("Clear removes active press"), Tracker.HasPressedInput(Input_Light));
		ExpectOutcome(*this, TEXT("Cleared token"), Tracker.Release(Second.GetToken(), 3.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::NoMatchingPress);
		const FCadenceArcInputTrackingOutcome Third = Tracker.Press(Input_Heavy, 3.0);
		TestEqual(TEXT("Clear preserves ID continuity"), Third.GetToken().PressId, static_cast<int64>(3));
		TestTrue(TEXT("Clear preserves session"), Third.GetToken().SourceSession == First.GetToken().SourceSession);

		FCadenceArcInputTracker RebuiltTracker;
		TestNotEqual(TEXT("Rebuilt tracker uses a new session"), RebuiltTracker.GetSourceSession(), Tracker.GetSourceSession());
		ExpectOutcome(*this, TEXT("Rebuilt tracker rejects old session token"), RebuiltTracker.Release(Third.GetToken(), 4.0),
		              ECadenceArcInputTrackingResult::Rejected, ECadenceArcInputTrackingReason::TokenMismatch);
		return !HasAnyErrors();
	}
}

#endif
