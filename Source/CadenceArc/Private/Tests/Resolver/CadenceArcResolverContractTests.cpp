#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	static bool TestSubmitOutcomeDefaultsAndGating(FAutomationTestBase& Test)
	{
		const FCadenceArcSubmitOutcome DefaultOutcome;
		bool bPassed = TestSubmit(Test, TEXT("Default submit outcome"), DefaultOutcome,
		                          ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::None);
		bPassed &= Test.TestFalse(TEXT("Default submit outcome has no request"), DefaultOutcome.HasActionRequest());
		bPassed &= TestEmptyRequest(Test, DefaultOutcome.GetActionRequest());

		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		const FCadenceArcSubmitOutcome Uninitialized = Resolver->SubmitInput(MakeInput(Input_Light));
		bPassed &= TestSubmit(Test, TEXT("Uninitialized submit outcome"), Uninitialized,
		                      ECadenceArcResolutionCategory::Rejected,
		                      ECadenceArcResolutionReason::NotInitialized);
		bPassed &= Test.TestFalse(TEXT("Rejected submit outcome has no request"), Uninitialized.HasActionRequest());
		bPassed &= TestEmptyRequest(Test, Uninitialized.GetActionRequest());

		Resolver->Initialize(MakeValidGraph());
		const FCadenceArcSubmitOutcome InvalidInput = Resolver->SubmitInput(MakeInput(FGameplayTag::EmptyTag));
		bPassed &= TestSubmit(Test, TEXT("Invalid-input submit outcome"), InvalidInput,
		                      ECadenceArcResolutionCategory::Rejected,
		                      ECadenceArcResolutionReason::InvalidInputTag);
		bPassed &= Test.TestFalse(TEXT("Invalid-input outcome has no request"), InvalidInput.HasActionRequest());

		const FCadenceArcSubmitOutcome Produced = Resolver->SubmitInput(MakeInput(Input_Light));
		const FCadenceArcActionRequest ProducedRequest = Produced.GetActionRequest();
		bPassed &= TestSubmit(Test, TEXT("Produced submit outcome"), Produced,
		                      ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
		bPassed &= Test.TestTrue(TEXT("Request-produced outcome exposes its request"), Produced.HasActionRequest());
		bPassed &= Test.TestTrue(TEXT("Produced request has an ID"), ProducedRequest.RequestId > 0);

		FCadenceArcActionRequest MutatedRequestCopy = Produced.GetActionRequest();
		MutatedRequestCopy.RequestId = 0;
		MutatedRequestCopy.TargetActionTag = FGameplayTag::EmptyTag;
		bPassed &= Test.TestEqual(TEXT("Mutating submit request copy does not alter outcome"),
		                          Produced.GetActionRequest().RequestId, ProducedRequest.RequestId);
		bPassed &= TestTag(Test, TEXT("Mutating submit request copy preserves outcome target"),
		                   Produced.GetActionRequest().TargetActionTag, ProducedRequest.TargetActionTag);
		bPassed &= Test.TestEqual(TEXT("Mutating submit request copy does not alter resolver"),
		                          Resolver->GetOutstandingRequest().RequestId, ProducedRequest.RequestId);

		const FCadenceArcSubmitOutcome Pending = Resolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(Test, TEXT("Pending submit outcome"), Pending,
		                      ECadenceArcResolutionCategory::NoAction,
		                      ECadenceArcResolutionReason::RequestPending);
		bPassed &= Test.TestFalse(TEXT("Pending submit outcome has no request"), Pending.HasActionRequest());

		bPassed &= TestHandshake(Test, TEXT("Start produced request"),
		                         Resolver->NotifyActionStarted(ProducedRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		const FCadenceArcSubmitOutcome ClosedWindow = Resolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(Test, TEXT("Closed-window submit outcome"), ClosedWindow,
		                      ECadenceArcResolutionCategory::NoAction,
		                      ECadenceArcResolutionReason::BufferWindowClosed);
		bPassed &= Test.TestFalse(TEXT("Closed-window submit outcome has no request"), ClosedWindow.HasActionRequest());

		bPassed &= TestHandshake(Test, TEXT("Open buffer window for buffered outcome"),
		                         Resolver->OpenBufferWindow(ProducedRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		const FCadenceArcSubmitOutcome Buffered = Resolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(Test, TEXT("Buffered submit outcome"), Buffered,
		                      ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
		bPassed &= Test.TestFalse(TEXT("Buffered submit outcome has no request"), Buffered.HasActionRequest());
		return bPassed;
	}

	static bool TestCompletionOutcomeDefaultsAndGating(FAutomationTestBase& Test)
	{
		const FCadenceArcActionCompletionOutcome DefaultOutcome;
		bool bPassed = TestHandshake(Test, TEXT("Default completion handshake"),
		                             DefaultOutcome.GetHandshakeResult(), ECadenceArcHandshakeResult::NotInitialized);
		bPassed &= TestBufferConsumption(Test, TEXT("Default completion buffer consumption"), DefaultOutcome,
		                                 ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::None);
		bPassed &= Test.
			TestFalse(TEXT("Default completion has no next request"), DefaultOutcome.HasNextActionRequest());
		bPassed &= TestEmptyRequest(Test, DefaultOutcome.GetNextActionRequest());

		UCadenceArcResolver* UninitializedResolver = NewObject<UCadenceArcResolver>();
		const FCadenceArcActionCompletionOutcome Uninitialized =
			UninitializedResolver->NotifyActionCompleted(1, RegressionTimestampSeconds);
		bPassed &= TestHandshake(Test, TEXT("Uninitialized completion handshake"), Uninitialized.GetHandshakeResult(),
		                         ECadenceArcHandshakeResult::NotInitialized);
		bPassed &= TestBufferConsumption(Test, TEXT("Uninitialized completion outcome"), Uninitialized,
		                                 ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::None);
		bPassed &= Test.
			TestFalse(TEXT("Rejected completion has no next request"), Uninitialized.HasNextActionRequest());

		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());
		const FCadenceArcSubmitOutcome FirstSubmit = Resolver->SubmitInput(MakeInput(Input_Light));
		const FCadenceArcActionRequest FirstRequest = FirstSubmit.GetActionRequest();
		bPassed &= TestHandshake(Test, TEXT("Start first action for completion outcomes"),
		                         Resolver->NotifyActionStarted(FirstRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);

		const FCadenceArcActionCompletionOutcome NoBufferedInput =
			Resolver->NotifyActionCompleted(FirstRequest.RequestId, RegressionTimestampSeconds);
		bPassed &= TestHandshake(Test, TEXT("No-buffer completion handshake"), NoBufferedInput.GetHandshakeResult(),
		                         ECadenceArcHandshakeResult::Success);
		bPassed &= TestBufferConsumption(Test, TEXT("No-buffer completion outcome"), NoBufferedInput,
		                                 ECadenceArcResolutionCategory::NoAction,
		                                 ECadenceArcResolutionReason::NoBufferedInput);
		bPassed &= Test.TestFalse(
			TEXT("No-buffer completion has no next request"), NoBufferedInput.HasNextActionRequest());

		const FCadenceArcActionRequest SecondRequest = Resolver->SubmitInput(MakeInput(Input_Light)).GetActionRequest();
		bPassed &= TestHandshake(Test, TEXT("Start second action for invalid completion"),
		                         Resolver->NotifyActionStarted(SecondRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		bPassed &= TestHandshake(Test, TEXT("Open buffer window for invalid completion"),
		                         Resolver->OpenBufferWindow(SecondRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		const FCadenceArcSubmitOutcome InvalidCompletionBuffer = Resolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(Test, TEXT("Invalid completion buffered input"), InvalidCompletionBuffer,
		                      ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
		const FCadenceArcActionCompletionOutcome InvalidCompletion =
			Resolver->NotifyActionCompleted(SecondRequest.RequestId, 0.0);
		bPassed &= TestHandshake(Test, TEXT("Invalid completion handshake"), InvalidCompletion.GetHandshakeResult(),
		                         ECadenceArcHandshakeResult::Success);
		bPassed &= TestBufferConsumption(Test, TEXT("Invalid completion outcome"), InvalidCompletion,
		                                 ECadenceArcResolutionCategory::Rejected,
		                                 ECadenceArcResolutionReason::InvalidCompletionTime);
		bPassed &= Test.TestFalse(
			TEXT("Invalid completion has no next request"), InvalidCompletion.HasNextActionRequest());

		UCadenceArcResolver* ProducedResolver = NewObject<UCadenceArcResolver>();
		ProducedResolver->Initialize(MakeValidGraph());
		const FCadenceArcActionRequest ThirdRequest = ProducedResolver->SubmitInput(MakeInput(Input_Light)).
		                                                                GetActionRequest();
		bPassed &= TestHandshake(Test, TEXT("Start third action for buffered completion"),
		                         ProducedResolver->NotifyActionStarted(ThirdRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		bPassed &= TestHandshake(Test, TEXT("Open buffer window for produced completion"),
		                         ProducedResolver->OpenBufferWindow(ThirdRequest.RequestId),
		                         ECadenceArcHandshakeResult::Success);
		const FCadenceArcSubmitOutcome ProducedCompletionBuffer =
			ProducedResolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(Test, TEXT("Produced completion buffered input"), ProducedCompletionBuffer,
		                      ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
		const FCadenceArcActionCompletionOutcome Produced =
			ProducedResolver->NotifyActionCompleted(ThirdRequest.RequestId, RegressionTimestampSeconds);
		const FCadenceArcActionRequest ProducedRequest = Produced.GetNextActionRequest();
		bPassed &= TestHandshake(Test, TEXT("Produced completion handshake"), Produced.GetHandshakeResult(),
		                         ECadenceArcHandshakeResult::Success);
		bPassed &= TestBufferConsumption(Test, TEXT("Produced completion outcome"), Produced,
		                                 ECadenceArcResolutionCategory::RequestProduced,
		                                 ECadenceArcResolutionReason::None);
		bPassed &= Test.TestTrue(TEXT("Produced completion exposes its next request"), Produced.HasNextActionRequest());

		FCadenceArcActionRequest MutatedNextRequestCopy = Produced.GetNextActionRequest();
		MutatedNextRequestCopy.RequestId = 0;
		MutatedNextRequestCopy.TargetActionTag = FGameplayTag::EmptyTag;
		bPassed &= Test.TestEqual(TEXT("Mutating completion request copy does not alter outcome"),
		                          Produced.GetNextActionRequest().RequestId, ProducedRequest.RequestId);
		bPassed &= TestTag(Test, TEXT("Mutating completion request copy preserves outcome target"),
		                   Produced.GetNextActionRequest().TargetActionTag, ProducedRequest.TargetActionTag);
		bPassed &= Test.TestEqual(TEXT("Mutating completion request copy does not alter resolver"),
		                          ProducedResolver->GetOutstandingRequest().RequestId, ProducedRequest.RequestId);
		bPassed &= TestState(Test, TEXT("Produced completion leaves resolver awaiting next start"),
		                     ProducedResolver->GetState(), ECadenceArcResolverState::AwaitingStart);
		return bPassed;
	}


	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcPublicOutcomeContractTest,
		"CadenceArc.Resolver.PublicOutcome.Contract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcPublicOutcomeContractTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		bool bPassed = TestSubmitOutcomeDefaultsAndGating(*this);
		bPassed &= TestCompletionOutcomeDefaultsAndGating(*this);
		bPassed &= TestNotEqual(TEXT("Initialize success has a distinct enum value"),
		                        static_cast<uint8>(ECadenceArcResolverInitResult::Success),
		                        static_cast<uint8>(ECadenceArcResolverInitResult::InvalidGraph));
		bPassed &= TestNotEqual(TEXT("Handshake success has a distinct enum value"),
		                        static_cast<uint8>(ECadenceArcHandshakeResult::Success),
		                        static_cast<uint8>(ECadenceArcHandshakeResult::NotInitialized));
		bPassed &= TestNotEqual(TEXT("Reset success has a distinct enum value"),
		                        static_cast<uint8>(ECadenceArcResolverResetResult::Success),
		                        static_cast<uint8>(ECadenceArcResolverResetResult::NotInitialized));
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(MakeInput(Input_Light));
		bPassed &= TestSubmit(*this, TEXT("Uninitialized submission mapping"), Outcome,
		                      ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::NotInitialized);
		bPassed &= TestEmptyRequest(*this, Outcome.GetActionRequest());
		Resolver->Initialize(MakeValidGraph());
		Outcome = Resolver->SubmitInput(MakeInput(FGameplayTag::EmptyTag));
		bPassed &= TestSubmit(*this, TEXT("Invalid input mapping"), Outcome,
		                      ECadenceArcResolutionCategory::Rejected, ECadenceArcResolutionReason::InvalidInputTag);
		Outcome = Resolver->SubmitInput(MakeInput(Input_Heavy));
		bPassed &= TestSubmit(*this, TEXT("Produced request mapping"), Outcome,
		                      ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
		bPassed &= TestTrue(TEXT("Produced request passes category gate"), Outcome.HasActionRequest());
		return bPassed && !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInitializeTest,
		"CadenceArc.Resolver.Initialize.Contract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInitializeTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestInit(*this, TEXT("Null graph is rejected"),
		         Resolver->Initialize(nullptr), ECadenceArcResolverInitResult::InvalidGraph);

		UCadenceArcGraph* InvalidEntryGraph = NewObject<UCadenceArcGraph>();
		TestInit(*this, TEXT("Invalid entry tag is rejected"),
		         Resolver->Initialize(InvalidEntryGraph), ECadenceArcResolverInitResult::InvalidEntryActionTag);

		UCadenceArcGraph* MissingEntryGraph = NewObject<UCadenceArcGraph>();
		MissingEntryGraph->EntryActionTag = Action_Root;
		AddNode(MissingEntryGraph, Action_Light01);
		TestInit(*this, TEXT("Missing entry node is rejected"),
		         Resolver->Initialize(MissingEntryGraph), ECadenceArcResolverInitResult::EntryNodeNotFound);
		TestFalse(TEXT("Failures leave resolver uninitialized"), Resolver->IsInitialized());

		TestInit(*this, TEXT("Valid graph initializes"),
		         Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Success);
		TestTrue(TEXT("Resolver reports initialized"), Resolver->IsInitialized());
		TestState(*this, TEXT("Resolver enters Ready"),
		          Resolver->GetState(), ECadenceArcResolverState::Ready);
		TestTag(*this, TEXT("Resolver starts at entry"),
		        Resolver->GetCurrentActionTag(), Action_Root);
		TestEqual(TEXT("Resolver starts without an outstanding request"),
		          Resolver->GetOutstandingRequest().RequestId, static_cast<int64>(0));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRequestCreationTest,
		"CadenceArc.Resolver.Resolve.RequestCreation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRequestCreationTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());
		FCadenceArcActionRequest Request;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Request, TEXT("Root + Light"));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcBusyStatesTest,
		"CadenceArc.Resolver.Resolve.BusyStates",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcBusyStatesTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());
		FCadenceArcActionRequest Request;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Request, TEXT("Initial request"));
		ResolveFailureAndExpect(*this, Resolver, Input_Heavy,
		                        ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                           ECadenceArcResolutionReason::RequestPending), Action_Root,
		                        ECadenceArcResolverState::AwaitingStart, Request.RequestId,
		                        TEXT("Resolve while awaiting start"));
		StartAndExpect(*this, Resolver, Request, TEXT("Initial request"));
		ResolveFailureAndExpect(*this, Resolver, Input_Heavy,
		                        ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                           ECadenceArcResolutionReason::BufferWindowClosed), Action_Light01,
		                        ECadenceArcResolverState::Executing, Request.RequestId,
		                        TEXT("Resolve while executing outside buffer window"));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcResolutionFailuresTest,
		"CadenceArc.Resolver.Resolve.FailuresPreserveState",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcResolutionFailuresTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		ResolveFailureAndExpect(*this, Resolver, Input_Light,
		                        ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                           ECadenceArcResolutionReason::NotInitialized), FGameplayTag::EmptyTag,
		                        ECadenceArcResolverState::Uninitialized, 0, TEXT("Resolve before initialization"));
		Resolver->Initialize(MakeValidGraph());
		ResolveFailureAndExpect(*this, Resolver, FGameplayTag::EmptyTag,
		                        ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                           ECadenceArcResolutionReason::InvalidInputTag), Action_Root,
		                        ECadenceArcResolverState::Ready, 0, TEXT("Resolve invalid input"));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Root, Action_Heavy01, TEXT("Root + Heavy"));
		ResolveFailureAndExpect(*this, Resolver, Input_Light,
		                        ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                           ECadenceArcResolutionReason::NoMatchingTransition), Action_Heavy01,
		                        ECadenceArcResolverState::Ready, 0, TEXT("Heavy01 + Light"));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcBrokenGraphTest,
		"CadenceArc.Resolver.Resolve.BrokenGraph",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcBrokenGraphTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcGraph* Graph = MakeValidGraph();
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(Graph);
		Graph->Nodes.RemoveAll([](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Root; });
		ResolveFailureAndExpect(*this, Resolver, Input_Light,
		                        ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                           ECadenceArcResolutionReason::CurrentNodeNotFound), Action_Root,
		                        ECadenceArcResolverState::Ready, 0, TEXT("Missing current node"));

		Graph = MakeValidGraph();
		Resolver->Initialize(Graph);
		Graph->Nodes.RemoveAll([](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Light01; });
		ResolveFailureAndExpect(*this, Resolver, Input_Light,
		                        ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                           ECadenceArcResolutionReason::TargetNodeNotFound), Action_Root,
		                        ECadenceArcResolverState::Ready, 0, TEXT("Missing target node"));
		return !HasAnyErrors();
	}


	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcRequestIdTest,
		"CadenceArc.Resolver.RequestId.Monotonic",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcRequestIdTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest First;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, First, TEXT("First request"));
		Resolver->NotifyActionRejected(First.RequestId);

		FCadenceArcActionRequest Second;
		ResolveAndExpect(*this, Resolver, Input_Heavy,
		                 Action_Root, Action_Heavy01, Second, TEXT("Second request"));
		TestTrue(TEXT("ID increases after rejection"), Second.RequestId > First.RequestId);
		StartAndExpect(*this, Resolver, Second, TEXT("Second request"));
		CompleteAndExpect(*this, Resolver, Second, TEXT("Second request"));

		Resolver->Reset();
		Resolver->Initialize(MakeValidGraph());
		FCadenceArcActionRequest Third;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Third, TEXT("Third request"));
		TestTrue(TEXT("ID increases across reset and reinitialize"), Third.RequestId > Second.RequestId);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcValidBranchesTest,
		"CadenceArc.Resolver.Resolve.ValidBranches",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcValidBranchesTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		ExecuteAndComplete(*this, Resolver, Input_Light, Action_Root, Action_Light01, TEXT("Root + Light"));
		ExecuteAndComplete(*this, Resolver, Input_Light, Action_Light01, Action_Light02, TEXT("Light01 + Light"));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Light02, Action_Finisher02, TEXT("Light02 + Heavy"));

		TestEqual(TEXT("Reset before mixed branch succeeds"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::Success));
		ExecuteAndComplete(*this, Resolver, Input_Light, Action_Root, Action_Light01, TEXT("Root + Light after reset"));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Light01, Action_Finisher01, TEXT("Light01 + Heavy"));

		TestEqual(TEXT("Reset before heavy branch succeeds"), static_cast<uint8>(Resolver->Reset()),
		          static_cast<uint8>(ECadenceArcResolverResetResult::Success));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Root, Action_Heavy01, TEXT("Root + Heavy"));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Heavy01, Action_Heavy02, TEXT("Heavy01 + Heavy"));
		ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Heavy02, Action_Finisher03, TEXT("Heavy02 + Heavy"));
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
