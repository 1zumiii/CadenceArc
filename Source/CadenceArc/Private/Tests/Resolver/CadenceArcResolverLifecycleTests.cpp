#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcLifecycleTest,
		"CadenceArc.Resolver.Handshake.Lifecycle",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcLifecycleTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest RejectedRequest;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, RejectedRequest, TEXT("Rejected request"));
		TestHandshake(*this, TEXT("Reject succeeds"),
		              Resolver->NotifyActionRejected(RejectedRequest.RequestId),
		              ECadenceArcHandshakeResult::Success);
		TestState(*this, TEXT("Reject returns Ready"),
		          Resolver->GetState(), ECadenceArcResolverState::Ready);
		TestTag(*this, TEXT("Reject preserves source"),
		        Resolver->GetCurrentActionTag(), Action_Root);
		TestEqual(TEXT("Reject clears request"),
		          Resolver->GetOutstandingRequest().RequestId, static_cast<int64>(0));

		FCadenceArcActionRequest CompletedRequest;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, CompletedRequest, TEXT("Completed request"));
		StartAndExpect(*this, Resolver, CompletedRequest, TEXT("Completed request"));
		CompleteAndExpect(*this, Resolver, CompletedRequest, TEXT("Completed request"));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHandshakeErrorsTest,
		"CadenceArc.Resolver.Handshake.ErrorsPreserveState",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHandshakeErrorsTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestHandshake(*this, TEXT("Callback before initialization is rejected"),
		              Resolver->NotifyActionStarted(1), ECadenceArcHandshakeResult::NotInitialized);
		Resolver->Initialize(MakeValidGraph());
		TestHandshake(*this, TEXT("Zero request ID is rejected"),
		              Resolver->NotifyActionStarted(0), ECadenceArcHandshakeResult::InvalidRequestId);
		TestHandshake(*this, TEXT("Started in Ready is rejected"),
		              Resolver->NotifyActionStarted(1), ECadenceArcHandshakeResult::UnexpectedState);

		FCadenceArcActionRequest Request;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Request, TEXT("Handshake request"));
		TestHandshake(*this, TEXT("Wrong pending ID is rejected"),
		              Resolver->NotifyActionStarted(Request.RequestId + 1),
		              ECadenceArcHandshakeResult::RequestIdMismatch);
		TestState(*this, TEXT("Wrong ID preserves AwaitingStart"),
		          Resolver->GetState(), ECadenceArcResolverState::AwaitingStart);
		TestTag(*this, TEXT("Wrong ID preserves source"), Resolver->GetCurrentActionTag(), Action_Root);
		TestEqual(TEXT("Wrong ID preserves request"),
		          Resolver->GetOutstandingRequest().RequestId, Request.RequestId);

		StartAndExpect(*this, Resolver, Request, TEXT("Handshake request"));
		Resolver->OpenBufferWindow(Request.RequestId);
		FCadenceArcActionRequest IgnoredRequest;
		SubmitForTest(Resolver, MakeInput(Input_Heavy), IgnoredRequest);
		const FCadenceArcActionCompletionOutcome StaleCompletion =
			CompleteAt(*this, Resolver, Request.RequestId + 1, RegressionTimestampSeconds);
		TestHandshake(*this, TEXT("Stale completion ID is rejected"),
		              StaleCompletion.GetHandshakeResult(), ECadenceArcHandshakeResult::RequestIdMismatch);
		TestBufferConsume(*this, TEXT("Stale completion does not attempt consumption"),
		                  StaleCompletion,
		                  ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                     ECadenceArcResolutionReason::None));
		TestTrue(TEXT("Stale completion preserves open window"), Resolver->IsBufferWindowOpen());
		TestTag(*this, TEXT("Stale completion preserves buffered input"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);
		TestHandshake(*this, TEXT("Reject while Executing is rejected"),
		              Resolver->NotifyActionRejected(Request.RequestId),
		              ECadenceArcHandshakeResult::UnexpectedState);
		TestState(*this, TEXT("Invalid callbacks preserve Executing"),
		          Resolver->GetState(), ECadenceArcResolverState::Executing);
		TestTag(*this, TEXT("Invalid callbacks preserve committed action"),
		        Resolver->GetCurrentActionTag(), Action_Light01);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcCancelInterruptTest,
		"CadenceArc.Resolver.Handshake.CancelAndInterrupt",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcCancelInterruptTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest CancelledRequest;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, CancelledRequest, TEXT("Cancelled request"));
		StartAndExpect(*this, Resolver, CancelledRequest, TEXT("Cancelled request"));
		Resolver->OpenBufferWindow(CancelledRequest.RequestId);
		FCadenceArcActionRequest IgnoredRequest;
		TestTransition(*this, TEXT("Input is buffered before cancellation"),
		               SubmitForTest(Resolver, MakeInput(Input_Heavy), IgnoredRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));
		TestHandshake(*this, TEXT("Cancellation with wrong ID is rejected"),
		              Resolver->NotifyActionCancelled(CancelledRequest.RequestId + 1),
		              ECadenceArcHandshakeResult::RequestIdMismatch);
		TestTrue(TEXT("Rejected cancellation preserves open window"), Resolver->IsBufferWindowOpen());
		TestTag(*this, TEXT("Rejected cancellation preserves buffered input"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);
		TestHandshake(*this, TEXT("Cancel succeeds"),
		              Resolver->NotifyActionCancelled(CancelledRequest.RequestId),
		              ECadenceArcHandshakeResult::Success);
		TestState(*this, TEXT("Cancel returns Ready"),
		          Resolver->GetState(), ECadenceArcResolverState::Ready);
		TestTag(*this, TEXT("Cancel resets entry"), Resolver->GetCurrentActionTag(), Action_Root);
		TestFalse(TEXT("Cancel closes buffer window"), Resolver->IsBufferWindowOpen());
		TestFalse(TEXT("Cancel clears buffered input"), Resolver->GetBufferedInputTag().IsValid());

		FCadenceArcActionRequest InterruptedRequest;
		ResolveAndExpect(*this, Resolver, Input_Heavy,
		                 Action_Root, Action_Heavy01, InterruptedRequest, TEXT("Interrupted request"));
		StartAndExpect(*this, Resolver, InterruptedRequest, TEXT("Interrupted request"));
		Resolver->OpenBufferWindow(InterruptedRequest.RequestId);
		TestTransition(*this, TEXT("Input is buffered before interruption"),
		               SubmitForTest(Resolver, MakeInput(Input_Heavy), IgnoredRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));
		TestHandshake(*this, TEXT("Interrupt succeeds"),
		              Resolver->NotifyActionInterrupted(InterruptedRequest.RequestId),
		              ECadenceArcHandshakeResult::Success);
		TestState(*this, TEXT("Interrupt returns Ready"),
		          Resolver->GetState(), ECadenceArcResolverState::Ready);
		TestTag(*this, TEXT("Interrupt resets entry"), Resolver->GetCurrentActionTag(), Action_Root);
		TestEqual(TEXT("Interrupt clears request"),
		          Resolver->GetOutstandingRequest().RequestId, static_cast<int64>(0));
		TestFalse(TEXT("Interrupt closes buffer window"), Resolver->IsBufferWindowOpen());
		TestFalse(TEXT("Interrupt clears buffered input"), Resolver->GetBufferedInputTag().IsValid());
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcResetBusyTest,
		"CadenceArc.Resolver.State.ResetAndBusyInitialize",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcResetBusyTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestEqual(TEXT("Reset before initialization fails"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::NotInitialized));
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest Request;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Request, TEXT("Busy request"));
		TestEqual(TEXT("Reset while AwaitingStart fails"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::Busy));
		TestInit(*this, TEXT("Initialize while AwaitingStart is Busy"),
		         Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::UnexpectedState);
		StartAndExpect(*this, Resolver, Request, TEXT("Busy request"));
		TestEqual(TEXT("Reset while Executing fails"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::Busy));
		TestInit(*this, TEXT("Initialize while Executing is Busy"),
		         Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::UnexpectedState);
		CompleteAndExpect(*this, Resolver, Request, TEXT("Busy request"));
		TestEqual(TEXT("Reset while Ready succeeds"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::Success));
		TestTag(*this, TEXT("Ready reset restores entry"), Resolver->GetCurrentActionTag(), Action_Root);
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
