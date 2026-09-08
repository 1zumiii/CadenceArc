#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcBufferWindowTest,
		"CadenceArc.Resolver.Buffer.WindowContract",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcBufferWindowTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestHandshake(*this, TEXT("Opening before initialization is rejected"),
		              Resolver->OpenBufferWindow(1), ECadenceArcHandshakeResult::NotInitialized);

		Resolver->Initialize(MakeValidGraph());
		TestHandshake(*this, TEXT("Opening while Ready is rejected"),
		              Resolver->OpenBufferWindow(1), ECadenceArcHandshakeResult::UnexpectedState);
		TestFalse(TEXT("Rejected opening preserves closed window"), Resolver->IsBufferWindowOpen());

		FCadenceArcActionRequest Request;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, Request, TEXT("Window request"));
		TestHandshake(*this, TEXT("Opening while AwaitingStart is rejected"),
		              Resolver->OpenBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::UnexpectedState);
		StartAndExpect(*this, Resolver, Request, TEXT("Window request"));

		TestHandshake(*this, TEXT("Opening with wrong ID is rejected"),
		              Resolver->OpenBufferWindow(Request.RequestId + 1), ECadenceArcHandshakeResult::RequestIdMismatch);
		TestFalse(TEXT("Wrong ID preserves closed window"), Resolver->IsBufferWindowOpen());
		TestHandshake(*this, TEXT("Opening with current ID succeeds"),
		              Resolver->OpenBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::Success);
		TestTrue(TEXT("Window reports open"), Resolver->IsBufferWindowOpen());
		TestHandshake(*this, TEXT("Opening an open window is idempotent"),
		              Resolver->OpenBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::Success);

		TestHandshake(*this, TEXT("Closing with wrong ID is rejected"),
		              Resolver->CloseBufferWindow(Request.RequestId + 1),
		              ECadenceArcHandshakeResult::RequestIdMismatch);
		TestTrue(TEXT("Wrong ID preserves open window"), Resolver->IsBufferWindowOpen());
		TestHandshake(*this, TEXT("Closing with current ID succeeds"),
		              Resolver->CloseBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::Success);
		TestFalse(TEXT("Window reports closed"), Resolver->IsBufferWindowOpen());
		TestHandshake(*this, TEXT("Closing a closed window is idempotent"),
		              Resolver->CloseBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::Success);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcSingleInputBufferTest,
		"CadenceArc.Resolver.Buffer.SingleSlotLastInputWins",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcSingleInputBufferTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest ExecutingRequest;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, ExecutingRequest, TEXT("Buffer request"));
		StartAndExpect(*this, Resolver, ExecutingRequest, TEXT("Buffer request"));

		FCadenceArcActionRequest OutputRequest;
		OutputRequest.RequestId = 999;
		TestTransition(*this, TEXT("Input outside window is rejected"),
		               SubmitForTest(Resolver, MakeInput(Input_Light), OutputRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                  ECadenceArcResolutionReason::BufferWindowClosed));
		TestEqual(TEXT("Rejected input clears output request"),
		          OutputRequest.RequestId, static_cast<int64>(0));
		TestFalse(TEXT("Rejected input does not populate buffer"),
		          Resolver->GetBufferedInputTag().IsValid());

		Resolver->OpenBufferWindow(ExecutingRequest.RequestId);
		TestTransition(*this, TEXT("First input is buffered"),
		               SubmitForTest(Resolver, MakeInput(Input_Light), OutputRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));
		TestTag(*this, TEXT("Buffer stores first input"),
		        Resolver->GetBufferedInputTag(), Input_Light);
		TestEqual(TEXT("Buffering does not emit an action request"),
		          OutputRequest.RequestId, static_cast<int64>(0));

		TestTransition(*this, TEXT("Second input is buffered"),
		               SubmitForTest(Resolver, MakeInput(Input_Heavy), OutputRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));
		TestTag(*this, TEXT("Later input overwrites earlier input"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);
		TestState(*this, TEXT("Buffering preserves Executing state"),
		          Resolver->GetState(), ECadenceArcResolverState::Executing);
		TestTag(*this, TEXT("Buffering preserves current action"),
		        Resolver->GetCurrentActionTag(), Action_Light01);
		TestEqual(TEXT("Buffering preserves outstanding request"),
		          Resolver->GetOutstandingRequest().RequestId, ExecutingRequest.RequestId);

		TestTransition(*this, TEXT("Invalid input is rejected while window is open"),
		               SubmitForTest(Resolver, MakeInput(FGameplayTag::EmptyTag), OutputRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                  ECadenceArcResolutionReason::InvalidInputTag));
		TestTag(*this, TEXT("Invalid input preserves buffered value"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);

		Resolver->CloseBufferWindow(ExecutingRequest.RequestId);
		TestFalse(TEXT("Window closes after buffering"), Resolver->IsBufferWindowOpen());
		TestTag(*this, TEXT("Closing window retains buffered input"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);
		TestTransition(*this, TEXT("Later input outside window is rejected"),
		               SubmitForTest(Resolver, MakeInput(Input_Light), OutputRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                  ECadenceArcResolutionReason::BufferWindowClosed));
		TestTag(*this, TEXT("Rejected later input preserves buffered value"),
		        Resolver->GetBufferedInputTag(), Input_Heavy);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcBufferedCompletionTest,
		"CadenceArc.Resolver.Buffer.CompletionResolvesNextRequest",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcBufferedCompletionTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Resolver->Initialize(MakeValidGraph());

		FCadenceArcActionRequest FirstRequest;
		ResolveAndExpect(*this, Resolver, Input_Light,
		                 Action_Root, Action_Light01, FirstRequest, TEXT("First action"));
		StartAndExpect(*this, Resolver, FirstRequest, TEXT("First action"));
		Resolver->OpenBufferWindow(FirstRequest.RequestId);

		FCadenceArcActionRequest IgnoredRequest;
		TestTransition(*this, TEXT("Light is buffered first"),
		               SubmitForTest(Resolver, MakeInput(Input_Light), IgnoredRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));
		TestTransition(*this, TEXT("Heavy overwrites buffered Light"),
		               SubmitForTest(Resolver, MakeInput(Input_Heavy), IgnoredRequest),
		               ExpectedResolution(ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None));

		const FCadenceArcActionCompletionOutcome Outcome =
			CompleteAt(*this, Resolver, FirstRequest.RequestId, RegressionTimestampSeconds);
		TestHandshake(*this, TEXT("Buffered completion succeeds"),
		              Outcome.GetHandshakeResult(), ECadenceArcHandshakeResult::Success);
		TestBufferConsumption(*this, TEXT("Buffered input resolves"), Outcome,
		                      ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
		TestTrue(TEXT("Buffered completion exposes its produced request"), Outcome.HasNextActionRequest());
		TestTrue(TEXT("Completion emits a newer request ID"),
		         Outcome.GetNextActionRequest().RequestId > FirstRequest.RequestId);
		TestTag(*this, TEXT("Next request uses last buffered input"),
		        Outcome.GetNextActionRequest().InputTag, Input_Heavy);
		TestTag(*this, TEXT("Next request starts from completed action"),
		        Outcome.GetNextActionRequest().SourceActionTag, Action_Light01);
		TestTag(*this, TEXT("Last buffered input selects heavy finisher"),
		        Outcome.GetNextActionRequest().TargetActionTag, Action_Finisher01);
		TestState(*this, TEXT("Resolved completion enters AwaitingStart"),
		          Resolver->GetState(), ECadenceArcResolverState::AwaitingStart);
		TestTag(*this, TEXT("Next action is not committed before start"),
		        Resolver->GetCurrentActionTag(), Action_Light01);
		TestEqual(TEXT("Resolver exposes next outstanding request"),
		          Resolver->GetOutstandingRequest().RequestId, Outcome.GetNextActionRequest().RequestId);
		const FCadenceArcActionRequest Outstanding = Resolver->GetOutstandingRequest();
		TestEqual(TEXT("Completion request retains outstanding ID"), Outcome.GetNextActionRequest().RequestId,
		          Outstanding.RequestId);
		TestTag(*this, TEXT("Completion request retains outstanding input"), Outcome.GetNextActionRequest().InputTag,
		        Outstanding.InputTag);
		TestTag(*this, TEXT("Completion request retains outstanding source"),
		        Outcome.GetNextActionRequest().SourceActionTag,
		        Outstanding.SourceActionTag);
		TestTag(*this, TEXT("Completion request retains outstanding target"),
		        Outcome.GetNextActionRequest().TargetActionTag,
		        Outstanding.TargetActionTag);
		TestFalse(TEXT("Completion closes buffer window"), Resolver->IsBufferWindowOpen());
		TestFalse(TEXT("Completion clears consumed input"), Resolver->GetBufferedInputTag().IsValid());

		StartAndExpect(*this, Resolver, Outcome.GetNextActionRequest(), TEXT("Buffered next action"));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcBufferConsumeFailuresTest,
		"CadenceArc.Resolver.Buffer.ConsumeFailuresPreserveState",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcBufferConsumeFailuresTest::RunTest(const FString& Parameters)
	{
		using namespace CadenceArc::Tests;
		if (!RequireExplicitCompletionTime(*this)) { return false; }

		UCadenceArcResolver* NoMatchResolver = NewObject<UCadenceArcResolver>();
		NoMatchResolver->Initialize(MakeValidGraph());
		FCadenceArcActionRequest NoMatchRequest;
		ResolveAndExpect(*this, NoMatchResolver, Input_Heavy,
		                 Action_Root, Action_Heavy01, NoMatchRequest, TEXT("No-match action"));
		StartAndExpect(*this, NoMatchResolver, NoMatchRequest, TEXT("No-match action"));
		NoMatchResolver->OpenBufferWindow(NoMatchRequest.RequestId);
		FCadenceArcActionRequest IgnoredRequest;
		SubmitForTest(NoMatchResolver, MakeInput(Input_Light), IgnoredRequest);
		const FCadenceArcActionCompletionOutcome NoMatchOutcome =
			CompleteAt(*this, NoMatchResolver, NoMatchRequest.RequestId, RegressionTimestampSeconds);
		TestHandshake(*this, TEXT("No-match completion succeeds"),
		              NoMatchOutcome.GetHandshakeResult(), ECadenceArcHandshakeResult::Success);
		TestBufferConsume(*this, TEXT("Missing transition is reported"),
		                  NoMatchOutcome, ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
		                                                     ECadenceArcResolutionReason::NoMatchingTransition));
		TestState(*this, TEXT("Missing transition leaves resolver Ready"),
		          NoMatchResolver->GetState(), ECadenceArcResolverState::Ready);
		TestTag(*this, TEXT("Missing transition preserves completed action"),
		        NoMatchResolver->GetCurrentActionTag(), Action_Heavy01);
		TestEqual(TEXT("Missing transition emits no next request"),
		          NoMatchOutcome.GetNextActionRequest().RequestId, static_cast<int64>(0));

		UCadenceArcGraph* MissingCurrentGraph = MakeValidGraph();
		UCadenceArcResolver* MissingCurrentResolver = NewObject<UCadenceArcResolver>();
		MissingCurrentResolver->Initialize(MissingCurrentGraph);
		FCadenceArcActionRequest MissingCurrentRequest;
		ResolveAndExpect(*this, MissingCurrentResolver, Input_Light,
		                 Action_Root, Action_Light01, MissingCurrentRequest, TEXT("Missing-current action"));
		StartAndExpect(*this, MissingCurrentResolver, MissingCurrentRequest, TEXT("Missing-current action"));
		MissingCurrentResolver->OpenBufferWindow(MissingCurrentRequest.RequestId);
		SubmitForTest(MissingCurrentResolver, MakeInput(Input_Heavy), IgnoredRequest);
		MissingCurrentGraph->Nodes.RemoveAll(
			[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Light01; });
		const FCadenceArcActionCompletionOutcome MissingCurrentOutcome =
			CompleteAt(*this, MissingCurrentResolver, MissingCurrentRequest.RequestId, RegressionTimestampSeconds);
		TestBufferConsume(*this, TEXT("Missing current node is reported during consumption"),
		                  MissingCurrentOutcome, ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                                            ECadenceArcResolutionReason::CurrentNodeNotFound));
		TestState(*this, TEXT("Missing current node leaves resolver Ready"),
		          MissingCurrentResolver->GetState(), ECadenceArcResolverState::Ready);

		UCadenceArcGraph* MissingTargetGraph = MakeValidGraph();
		UCadenceArcResolver* MissingTargetResolver = NewObject<UCadenceArcResolver>();
		MissingTargetResolver->Initialize(MissingTargetGraph);
		FCadenceArcActionRequest MissingTargetRequest;
		ResolveAndExpect(*this, MissingTargetResolver, Input_Light,
		                 Action_Root, Action_Light01, MissingTargetRequest, TEXT("Missing-target action"));
		StartAndExpect(*this, MissingTargetResolver, MissingTargetRequest, TEXT("Missing-target action"));
		MissingTargetResolver->OpenBufferWindow(MissingTargetRequest.RequestId);
		SubmitForTest(MissingTargetResolver, MakeInput(Input_Heavy), IgnoredRequest);
		MissingTargetGraph->Nodes.RemoveAll(
			[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Finisher01; });
		const FCadenceArcActionCompletionOutcome MissingTargetOutcome =
			CompleteAt(*this, MissingTargetResolver, MissingTargetRequest.RequestId, RegressionTimestampSeconds);
		TestBufferConsume(*this, TEXT("Missing target node is reported during consumption"),
		                  MissingTargetOutcome, ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
		                                                           ECadenceArcResolutionReason::TargetNodeNotFound));
		TestState(*this, TEXT("Missing target node leaves resolver Ready"),
		          MissingTargetResolver->GetState(), ECadenceArcResolverState::Ready);
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
