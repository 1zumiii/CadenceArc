#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"
#include <limits>
#include <type_traits>

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputEventValidityTest,
		"CadenceArc.Resolver.Time.InputEventValidity",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputEventValidityTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		TestFalse(TEXT("Default event has no valid tag"), FCadenceArcInputEvent{}.IsValid());
		TestTrue(TEXT("Zero is a valid timestamp"), MakeInput(Input_Light, 0.0).IsValid());
		TestTrue(TEXT("Positive finite timestamp is valid"), MakeInput(Input_Light, 1.25).IsValid());
		TestFalse(
			TEXT("Missing tag is invalid even with valid time"), MakeInput(FGameplayTag::EmptyTag, 1.0).IsValid());
		for (const double Invalid : InvalidTimes())
		{
			TestFalse(*FString::Printf(TEXT("Invalid timestamp %g fails event validity"), Invalid),
			          MakeInput(Input_Light, Invalid).IsValid());
		}
		// Exercise zero through the public API as well as the event helper. This remains
		// mandatory even though other tests use a positive timestamp for their setup.
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->MaxBufferedInputAgeSeconds = 0.5;
		Resolver->Initialize(Graph);
		FCadenceArcActionRequest Request;
		if (!TestTransition(*this, TEXT("Ready accepts an input at time zero"),
		                    SubmitForTest(Resolver, MakeInput(Input_Light, 0.0), Request),
		                    ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
		                                       ECadenceArcResolutionReason::None)))
		{
			return false;
		}
		if (!StartAndExpect(*this, Resolver, Request, TEXT("Zero-time initial action")) ||
			!BufferAt(*this, Resolver, Request, Input_Heavy, 0.0)) { return false; }
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, 0.0),
		                      ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
		                                         ECadenceArcResolutionReason::None));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputTimestampTest,
		"CadenceArc.Resolver.Time.InputTimestampValidation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputTimestampTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		// Validate input regardless of the age-limit policy and before accepting or replacing it.
		for (const double MaxAge : {0.0, 0.5})
		{
			for (int32 Mode = 0; Mode < 4; ++Mode) // Ready, AwaitingStart, Executing closed/open.
			{
				for (const double Invalid : InvalidTimes())
				{
					UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
					UCadenceArcGraph* Graph = MakeValidGraph();
					Graph->MaxBufferedInputAgeSeconds = MaxAge;
					Resolver->Initialize(Graph);
					FCadenceArcActionRequest Initial;
					if (Mode > 0 && !ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01,
					                                  Initial, TEXT("Input validation setup"))) { return false; }
					if (Mode > 1)
					{
						if (!StartAndExpect(*this, Resolver, Initial, TEXT("Input validation setup"))) { return false; }
						if (!BufferAt(*this, Resolver, Initial, Input_Heavy, 1.0)) { return false; }
						if (Mode == 2) { Resolver->CloseBufferWindow(Initial.RequestId); }
					}
					const FResolverSnapshot Before(Resolver);
					FCadenceArcActionRequest Output = {999, Input_Heavy, Action_Root, Action_Heavy01};
					TestTransition(*this, *FString::Printf(TEXT("Reject timestamp %g in mode %d with max age %g"),
					                                       Invalid, Mode, MaxAge),
					               SubmitForTest(Resolver, MakeInput(Input_Light, Invalid), Output),
					               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
					                                  ECadenceArcResolutionReason::InvalidTimestamp));
					TestEmptyRequest(*this, Output);
					Before.ExpectUnchanged(*this, Resolver);
				}
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcAgeConfigurationTest,
		"CadenceArc.Resolver.Time.AgeConfigurationAtomicity",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcAgeConfigurationTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		TestEqual(TEXT("Existing assets default to no expiry"), MakeValidGraph()->MaxBufferedInputAgeSeconds, 0.0);
		for (const double Invalid : InvalidTimes())
		{
			UCadenceArcGraph* InvalidGraph = MakeValidGraph();
			InvalidGraph->EntryActionTag = Action_Heavy01;
			InvalidGraph->MaxBufferedInputAgeSeconds = Invalid;
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			const FResolverSnapshot Uninitialized(Resolver);
			TestTrue(*FString::Printf(TEXT("Initial invalid max age %g is rejected"), Invalid),
			         Resolver->Initialize(InvalidGraph) != ECadenceArcResolverInitResult::Success);
			Uninitialized.ExpectUnchanged(*this, Resolver);
			TestFalse(TEXT("Invalid policy does not initialize resolver"), Resolver->IsInitialized());

			Resolver->Initialize(MakeValidGraph());
			FCadenceArcActionRequest First;
			if (!ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01, First,
			                      TEXT("Original graph request"))) { return false; }
			Resolver->NotifyActionRejected(First.RequestId);
			const FResolverSnapshot Ready(Resolver);
			TestTrue(*FString::Printf(TEXT("Reinitialize with invalid max age %g is rejected"), Invalid),
			         Resolver->Initialize(InvalidGraph) != ECadenceArcResolverInitResult::Success);
			Ready.ExpectUnchanged(*this, Resolver);
			FCadenceArcActionRequest Next;
			ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01, Next,
			                 TEXT("Failed reinitialize retains original graph"));
			TestEqual(TEXT("Failed reinitialize preserves ID sequence"), Next.RequestId, First.RequestId + 1);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcExpiryBoundariesTest,
		"CadenceArc.Resolver.Time.ExpiryBoundaries",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcExpiryBoundariesTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		struct FCase
		{
			double MaxAge;
			double InputTime;
			double CompletionTime;
			FExpectedResolution Result;
		};
		// Binary-exact fractions make the equality boundary independent of rounding noise.
		const FCase Cases[] = {
			{
				0.0, 1.0, 1000000.0,
				ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None)
			},
			{
				0.5, 1.0, 1.0,
				ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None)
			},
			{
				0.5, 1.0, 1.25,
				ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None)
			},
			{
				0.5, 1.0, 1.5,
				ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None)
			},
			{
				0.5, 1.0, 1.75,
				ExpectedResolution(ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired)
			}
		};
		for (const FCase& Case : Cases)
		{
			AddInfo(FString::Printf(TEXT("MaxAge=%g Input=%g Completion=%g"), Case.MaxAge, Case.InputTime,
			                        Case.CompletionTime));
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			TestNull(TEXT("Resolver uses no World"), Resolver->GetWorld());
			FCadenceArcActionRequest Request;
			if (!BeginTimedAction(*this, Resolver, Request, Case.MaxAge) ||
				!BufferAt(*this, Resolver, Request, Input_Heavy, Case.InputTime)) { return false; }
			const auto Outcome = CompleteAt(*this, Resolver, Request.RequestId, Case.CompletionTime);
			ExpectTimedCompletion(*this, Resolver, Request, Outcome, Case.Result);
			if (Case.Result.Category == ECadenceArcResolutionCategory::RequestProduced)
			{
				StartAndExpect(*this, Resolver, Outcome.GetNextActionRequest(), TEXT("Accept timed next action"));
			}
			else
			{
				FCadenceArcActionRequest Next;
				ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, Action_Light02, Next,
				                 TEXT("New input after expiry"));
				TestEqual(TEXT("Expired input allocated no hidden request"), Next.RequestId, Request.RequestId + 1);
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcTimedReplacementTest,
		"CadenceArc.Resolver.Time.LastInputReplacesTimestamp",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcTimedReplacementTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		for (const bool bSameTag : {false, true})
		{
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			FCadenceArcActionRequest Request;
			if (!BeginTimedAction(*this, Resolver, Request, 0.5) ||
				!BufferAt(*this, Resolver, Request, Input_Light, 0.25)) { return false; }
			const FGameplayTag LastTag = bSameTag ? Input_Light : Input_Heavy;
			if (!BufferAt(*this, Resolver, Request, LastTag, 1.75)) { return false; }
			Resolver->CloseBufferWindow(Request.RequestId);
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Closed window rejects later valid input"),
			               SubmitForTest(Resolver, MakeInput(Input_Heavy, 2.0), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
			                                  ECadenceArcResolutionReason::BufferWindowClosed));
			TestEmptyRequest(*this, Output);
			// The first event would expire. The replacement remains valid exactly at its boundary.
			const auto Outcome = CompleteAt(*this, Resolver, Request.RequestId, 2.25);
			ExpectTimedCompletion(*this, Resolver, Request, Outcome,
			                      ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
			                                         ECadenceArcResolutionReason::None),
			                      LastTag, bSameTag ? Action_Light02 : Action_Finisher01);
		}
		// A rejected same-tag input must not refresh the stored timestamp.
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		FCadenceArcActionRequest Request;
		if (!BeginTimedAction(*this, Resolver, Request, 0.5) ||
			!BufferAt(*this, Resolver, Request, Input_Heavy, 1.0)) { return false; }
		Resolver->CloseBufferWindow(Request.RequestId);
		FCadenceArcActionRequest Output;
		TestTransition(*this, TEXT("Closed window cannot refresh timestamp"),
		               SubmitForTest(Resolver, MakeInput(Input_Heavy, 1.75), Output),
		               ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                  ECadenceArcResolutionReason::BufferWindowClosed));
		ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, 2.0),
		                      ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                         ECadenceArcResolutionReason::Expired));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInvalidInputRetainsAgeTest,
		"CadenceArc.Resolver.Time.InvalidInputPreservesBufferedAge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInvalidInputRetainsAgeTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		struct FCase
		{
			double CompletionTime;
			FExpectedResolution Result;
		};
		const FCase Cases[] = {
			{
				0.5,
				ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
				                   ECadenceArcResolutionReason::InvalidCompletionTime)
			},
			{
				1.5,
				ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None)
			},
			{1.75, ExpectedResolution(ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired)}
		};
		for (const double Invalid : InvalidTimes())
		{
			for (const FCase& Case : Cases)
			{
				UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
				FCadenceArcActionRequest Request;
				if (!BeginTimedAction(*this, Resolver, Request, 0.5) ||
					!BufferAt(*this, Resolver, Request, Input_Heavy, 1.0)) { return false; }
				const FResolverSnapshot Before(Resolver);
				FCadenceArcActionRequest Output;
				TestTransition(*this, TEXT("Invalid same-tag input cannot replace timestamp"),
				               SubmitForTest(Resolver, MakeInput(Input_Heavy, Invalid), Output),
				               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
				                                  ECadenceArcResolutionReason::InvalidTimestamp));
				TestEmptyRequest(*this, Output);
				TestTransition(*this, TEXT("Invalid tag cannot refresh timestamp"),
				               SubmitForTest(Resolver, MakeInput(FGameplayTag::EmptyTag, 1.25), Output),
				               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
				                                  ECadenceArcResolutionReason::InvalidInputTag));
				TestEmptyRequest(*this, Output);
				Before.ExpectUnchanged(*this, Resolver);
				// Check both sides of the original lifetime, plus backward time. A tag-only
				// snapshot would miss a rejected event silently replacing the timestamp.
				ExpectTimedCompletion(*this, Resolver, Request,
				                      CompleteAt(*this, Resolver, Request.RequestId, Case.CompletionTime), Case.Result);
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInvalidCompletionTimeTest,
		"CadenceArc.Resolver.Time.InvalidCompletionRecovery",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInvalidCompletionTimeTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		TArray<double> Times = InvalidTimes();
		Times.Add(0.75); // Finite, nonnegative, but earlier than the buffered event.
		for (const double MaxAge : {0.0, 0.5})
		{
			for (const double Invalid : Times)
			{
				AddInfo(FString::Printf(TEXT("Invalid completion=%g MaxAge=%g"), Invalid, MaxAge));
				UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
				FCadenceArcActionRequest Request;
				if (!BeginTimedAction(*this, Resolver, Request, MaxAge) ||
					!BufferAt(*this, Resolver, Request, Input_Heavy, 1.0)) { return false; }
				ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, Invalid),
				                      ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
				                                         ECadenceArcResolutionReason::InvalidCompletionTime));
				const auto Repeated = CompleteAt(*this, Resolver, Request.RequestId, 1.5);
				TestHandshake(*this, TEXT("InvalidTime already completed the old action"),
				              Repeated.GetHandshakeResult(),
				              ECadenceArcHandshakeResult::UnexpectedState);
				TestBufferConsume(*this, TEXT("Repeated completion does not consume"), Repeated,
				                  ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
				                                     ECadenceArcResolutionReason::None));
				FCadenceArcActionRequest Next;
				ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, Action_Light02, Next,
				                 TEXT("Input after invalid completion time"));
				TestEqual(TEXT("InvalidTime allocated no hidden request"), Next.RequestId, Request.RequestId + 1);
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcEmptyTimedCompletionTest,
		"CadenceArc.Resolver.Time.NoBufferedInput",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcEmptyTimedCompletionTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		TArray<double> Times = InvalidTimes();
		Times.Add(10000.0);
		for (const double Completion : Times)
		{
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			FCadenceArcActionRequest Request;
			if (!BeginTimedAction(*this, Resolver, Request, 0.5)) { return false; }
			Resolver->OpenBufferWindow(Request.RequestId);
			// With no buffered event there is no age to validate or expire.
			ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, Completion),
			                      ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
			                                         ECadenceArcResolutionReason::NoBufferedInput));
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcTimedHandshakeAtomicityTest,
		"CadenceArc.Resolver.Time.HandshakeBeforeTimeValidation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcTimedHandshakeAtomicityTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		const double NaN = std::numeric_limits<double>::quiet_NaN();
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		const auto ExpectRejected = [&](const int64 Id, const double Time, const ECadenceArcHandshakeResult Expected)
		{
			const FResolverSnapshot Before(Resolver);
			const auto Outcome = CompleteAt(*this, Resolver, Id, Time);
			TestHandshake(*this, TEXT("Handshake error takes precedence over time"), Outcome.GetHandshakeResult(),
			              Expected);
			TestBufferConsume(*this, TEXT("Rejected handshake does not consume"), Outcome,
			                  ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                     ECadenceArcResolutionReason::None));
			TestEmptyRequest(*this, Outcome.GetNextActionRequest());
			Before.ExpectUnchanged(*this, Resolver);
		};
		ExpectRejected(1, NaN, ECadenceArcHandshakeResult::NotInitialized);
		Resolver->Initialize(MakeValidGraph());
		ExpectRejected(1, NaN, ECadenceArcHandshakeResult::UnexpectedState);
		FCadenceArcActionRequest Old;
		ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01, Old, TEXT("Old timed request"));
		ExpectRejected(Old.RequestId, NaN, ECadenceArcHandshakeResult::UnexpectedState);
		StartAndExpect(*this, Resolver, Old, TEXT("Old timed request"));
		Resolver->NotifyActionCancelled(Old.RequestId);
		FCadenceArcActionRequest Current;
		if (!BeginTimedAction(*this, Resolver, Current, 0.5) ||
			!BufferAt(*this, Resolver, Current, Input_Heavy, 1.0)) { return false; }
		TestTrue(TEXT("Reinitialize cannot recycle stale ID"), Current.RequestId > Old.RequestId);
		ExpectRejected(0, NaN, ECadenceArcHandshakeResult::InvalidRequestId);
		ExpectRejected(-1, NaN, ECadenceArcHandshakeResult::InvalidRequestId);
		ExpectRejected(Old.RequestId, NaN, ECadenceArcHandshakeResult::RequestIdMismatch);
		ExpectRejected(Old.RequestId, 10000.0, ECadenceArcHandshakeResult::RequestIdMismatch);
		// An accepted completion still uses the original timestamp, despite stale callbacks.
		ExpectTimedCompletion(*this, Resolver, Current, CompleteAt(*this, Resolver, Current.RequestId, 1.5),
		                      ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
		                                         ECadenceArcResolutionReason::None));
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
