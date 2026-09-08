#include "CadenceArcTestSupport.h"

#include "Graph/CadenceArcGraph.h"
#include "Resolver/CadenceArcResolver.h"
#include "Resolver/CadenceArcResolverTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

namespace CadenceArc::Tests
{
	UE_DEFINE_GAMEPLAY_TAG(Action_Root, "CadenceArc.Automation.Action.Root");
	UE_DEFINE_GAMEPLAY_TAG(Action_Light01, "CadenceArc.Automation.Action.Light01");
	UE_DEFINE_GAMEPLAY_TAG(Action_Light02, "CadenceArc.Automation.Action.Light02");
	UE_DEFINE_GAMEPLAY_TAG(Action_Heavy01, "CadenceArc.Automation.Action.Heavy01");
	UE_DEFINE_GAMEPLAY_TAG(Action_Heavy02, "CadenceArc.Automation.Action.Heavy02");
	UE_DEFINE_GAMEPLAY_TAG(Action_Finisher01, "CadenceArc.Automation.Action.Finisher01");
	UE_DEFINE_GAMEPLAY_TAG(Action_Finisher02, "CadenceArc.Automation.Action.Finisher02");
	UE_DEFINE_GAMEPLAY_TAG(Action_Finisher03, "CadenceArc.Automation.Action.Finisher03");
	UE_DEFINE_GAMEPLAY_TAG(Input_Light, "CadenceArc.Automation.Input.Light");
	UE_DEFINE_GAMEPLAY_TAG(Input_Heavy, "CadenceArc.Automation.Input.Heavy");

	FCadenceArcNode& AddNode(UCadenceArcGraph* Graph, const FGameplayTag& ActionTag)
	{
		FCadenceArcNode& Node = Graph->Nodes.AddDefaulted_GetRef();
		Node.ActionTag = ActionTag;
		return Node;
	}

	void AddTransition(FCadenceArcNode& Node, const FGameplayTag& InputTag, const FGameplayTag& TargetActionTag)
	{
		FCadenceArcTransition& Transition = Node.Transitions.AddDefaulted_GetRef();
		Transition.InputTag = InputTag;
		Transition.TargetActionTag = TargetActionTag;
	}

	UCadenceArcGraph* MakeValidGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(8);

		FCadenceArcNode& Root = AddNode(Graph, Action_Root);
		AddTransition(Root, Input_Light, Action_Light01);
		AddTransition(Root, Input_Heavy, Action_Heavy01);
		FCadenceArcNode& Light01 = AddNode(Graph, Action_Light01);
		AddTransition(Light01, Input_Light, Action_Light02);
		AddTransition(Light01, Input_Heavy, Action_Finisher01);
		FCadenceArcNode& Light02 = AddNode(Graph, Action_Light02);
		AddTransition(Light02, Input_Heavy, Action_Finisher02);
		FCadenceArcNode& Heavy01 = AddNode(Graph, Action_Heavy01);
		AddTransition(Heavy01, Input_Heavy, Action_Heavy02);
		FCadenceArcNode& Heavy02 = AddNode(Graph, Action_Heavy02);
		AddTransition(Heavy02, Input_Heavy, Action_Finisher03);
		AddNode(Graph, Action_Finisher01);
		AddNode(Graph, Action_Finisher02);
		AddNode(Graph, Action_Finisher03);
		return Graph;
	}

	FExpectedResolution ExpectedResolution(
		const ECadenceArcResolutionCategory Category, const ECadenceArcResolutionReason Reason)
	{
		return {Category, Reason};
	}

	TArray<double> InvalidTimes()
	{
		return {
			-1.0, std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()
		};
	}

	FResolverSnapshot::FResolverSnapshot(const UCadenceArcResolver* Resolver)
		: State(Resolver->GetState()), CurrentAction(Resolver->GetCurrentActionTag()),
		  Request(Resolver->GetOutstandingRequest()), bWindowOpen(Resolver->IsBufferWindowOpen()),
		  BufferedTag(Resolver->GetBufferedInputTag())
	{
	}

	void FResolverSnapshot::ExpectUnchanged(FAutomationTestBase& Test, const UCadenceArcResolver* Resolver) const
	{
		TestState(Test, TEXT("Failure preserves state"), Resolver->GetState(), State);
		TestTag(Test, TEXT("Failure preserves committed node"), Resolver->GetCurrentActionTag(), CurrentAction);
		const FCadenceArcActionRequest Actual = Resolver->GetOutstandingRequest();
		Test.TestEqual(TEXT("Failure preserves request ID"), Actual.RequestId, Request.RequestId);
		TestTag(Test, TEXT("Failure preserves request input"), Actual.InputTag, Request.InputTag);
		TestTag(Test, TEXT("Failure preserves request source"), Actual.SourceActionTag, Request.SourceActionTag);
		TestTag(Test, TEXT("Failure preserves request target"), Actual.TargetActionTag, Request.TargetActionTag);
		Test.TestEqual(TEXT("Failure preserves window"), Resolver->IsBufferWindowOpen(), bWindowOpen);
		TestTag(Test, TEXT("Failure preserves buffered tag"), Resolver->GetBufferedInputTag(), BufferedTag);
	}

	bool TestInit(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverInitResult Actual,
		const ECadenceArcResolverInitResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	bool TestSubmit(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FCadenceArcSubmitOutcome& Actual,
		const ECadenceArcResolutionCategory ExpectedCategory,
		const ECadenceArcResolutionReason ExpectedReason)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s category"), What),
		                              static_cast<uint8>(Actual.GetCategory()), static_cast<uint8>(ExpectedCategory));
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reason"), What),
		                          static_cast<uint8>(Actual.GetReason()), static_cast<uint8>(ExpectedReason));
		return bPassed;
	}

	bool TestHandshake(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcHandshakeResult Actual,
		const ECadenceArcHandshakeResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	bool TestBufferConsumption(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FCadenceArcActionCompletionOutcome& Actual,
		const ECadenceArcResolutionCategory ExpectedCategory,
		const ECadenceArcResolutionReason ExpectedReason)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s category"), What),
		                              static_cast<uint8>(Actual.GetBufferConsumption()),
		                              static_cast<uint8>(ExpectedCategory));
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reason"), What),
		                          static_cast<uint8>(Actual.GetBufferConsumptionReason()),
		                          static_cast<uint8>(ExpectedReason));
		return bPassed;
	}

	bool TestTransition(FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcSubmitOutcome& Actual,
	                           const FExpectedResolution Expected)
	{
		return TestSubmit(Test, What, Actual, Expected.Category, Expected.Reason);
	}

	bool TestBufferConsume(FAutomationTestBase& Test, const TCHAR* What,
	                              const FCadenceArcActionCompletionOutcome& Actual, const FExpectedResolution Expected)
	{
		return TestBufferConsumption(Test, What, Actual, Expected.Category, Expected.Reason);
	}

	bool TestState(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverState Actual,
		const ECadenceArcResolverState Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	bool TestTag(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FGameplayTag& Actual,
		const FGameplayTag& Expected)
	{
		return Test.TestEqual(What, Actual.ToString(), Expected.ToString());
	}

	bool TestEmptyRequest(FAutomationTestBase& Test, const FCadenceArcActionRequest& Request)
	{
		bool bPassed = Test.TestEqual(TEXT("Empty request has no ID"), Request.RequestId, int64{0});
		bPassed &= Test.TestFalse(TEXT("Empty request has no input"), Request.InputTag.IsValid());
		bPassed &= Test.TestFalse(TEXT("Empty request has no source"), Request.SourceActionTag.IsValid());
		bPassed &= Test.TestFalse(TEXT("Empty request has no target"), Request.TargetActionTag.IsValid());
		return bPassed;
	}

	FCadenceArcSubmitOutcome SubmitForTest(
		UCadenceArcResolver* Resolver,
		const FCadenceArcInputEvent& Event,
		FCadenceArcActionRequest& OutRequest
	)
	{
		FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(Event);
		OutRequest = Outcome.GetActionRequest();
		return Outcome;
	}

	bool ResolveAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FGameplayTag& SourceTag,
		const FGameplayTag& TargetTag,
		FCadenceArcActionRequest& OutRequest,
		const TCHAR* Step)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(MakeInput(InputTag));
		OutRequest = Outcome.GetActionRequest();
		bool bPassed = TestSubmit(Test, *FString::Printf(TEXT("%s resolves"), Step), Outcome,
		                          ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None);
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("%s gates its request by category"), Step),
		                         Outcome.HasActionRequest());
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s receives a positive request ID"), Step),
			OutRequest.RequestId > 0);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s records input"), Step),
		                   OutRequest.InputTag, InputTag);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s records source"), Step),
		                   OutRequest.SourceActionTag, SourceTag);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s records target"), Step),
		                   OutRequest.TargetActionTag, TargetTag);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s does not commit early"), Step),
		                   Resolver->GetCurrentActionTag(), SourceTag);
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s enters AwaitingStart"), Step),
		                     Resolver->GetState(), ECadenceArcResolverState::AwaitingStart);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s exposes the outstanding request"), Step),
			Resolver->GetOutstandingRequest().RequestId, OutRequest.RequestId);
		return bPassed;
	}

	bool StartAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request,
		const TCHAR* Step)
	{
		bool bPassed = TestHandshake(
			Test, *FString::Printf(TEXT("%s starts"), Step),
			Resolver->NotifyActionStarted(Request.RequestId),
			ECadenceArcHandshakeResult::Success);
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s enters Executing"), Step),
		                     Resolver->GetState(), ECadenceArcResolverState::Executing);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s commits target"), Step),
		                   Resolver->GetCurrentActionTag(), Request.TargetActionTag);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s keeps request while executing"), Step),
			Resolver->GetOutstandingRequest().RequestId, Request.RequestId);
		return bPassed;
	}

	bool CompleteAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request,
		const TCHAR* Step)
	{
		const FCadenceArcActionCompletionOutcome Outcome =
			CompleteAt(Test, Resolver, Request.RequestId, RegressionTimestampSeconds);
		bool bPassed = TestHandshake(
			Test, *FString::Printf(TEXT("%s completes"), Step),
			Outcome.GetHandshakeResult(),
			ECadenceArcHandshakeResult::Success);
		bPassed &= TestBufferConsumption(
			Test, *FString::Printf(TEXT("%s reports no buffered input"), Step),
			Outcome, ECadenceArcResolutionCategory::NoAction,
			ECadenceArcResolutionReason::NoBufferedInput);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s does not emit a next request"), Step),
			Outcome.GetNextActionRequest().RequestId, static_cast<int64>(0));
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s returns Ready"), Step),
		                     Resolver->GetState(), ECadenceArcResolverState::Ready);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s preserves target"), Step),
		                   Resolver->GetCurrentActionTag(), Request.TargetActionTag);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s clears request"), Step),
			Resolver->GetOutstandingRequest().RequestId, static_cast<int64>(0));
		return bPassed;
	}

	bool ExecuteAndComplete(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FGameplayTag& SourceTag,
		const FGameplayTag& TargetTag,
		const TCHAR* Step)
	{
		FCadenceArcActionRequest Request;
		bool bPassed = ResolveAndExpect(Test, Resolver, InputTag, SourceTag, TargetTag, Request, Step);
		bPassed &= StartAndExpect(Test, Resolver, Request, Step);
		bPassed &= CompleteAndExpect(Test, Resolver, Request, Step);
		return bPassed;
	}

	bool ResolveFailureAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FExpectedResolution Expected,
		const FGameplayTag& ExpectedCurrentTag,
		const ECadenceArcResolverState ExpectedState,
		const int64 ExpectedOutstandingId,
		const TCHAR* Step)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(MakeInput(InputTag));
		bool bPassed = TestSubmit(Test, *FString::Printf(TEXT("%s returns expected failure"), Step),
		                          Outcome, Expected.Category, Expected.Reason);
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s gates the empty request"), Step),
		                          Outcome.HasActionRequest());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s clears output ID"), Step),
		                          Outcome.GetActionRequest().RequestId, static_cast<int64>(0));
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s clears output target"), Step),
		                          Outcome.GetActionRequest().TargetActionTag.IsValid());
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s preserves current action"), Step),
		                   Resolver->GetCurrentActionTag(), ExpectedCurrentTag);
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s preserves state"), Step),
		                     Resolver->GetState(), ExpectedState);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s preserves outstanding request"), Step),
		                          Resolver->GetOutstandingRequest().RequestId, ExpectedOutstandingId);
		return bPassed;
	}

	bool BeginTimedAction(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
	                             FCadenceArcActionRequest& Request, const double MaxAge)
	{
		UCadenceArcGraph* Graph = CadenceArc::Tests::MakeValidGraph();
		Graph->MaxBufferedInputAgeSeconds = MaxAge;
		if (!TestInit(Test, TEXT("Timed graph initializes"), Resolver->Initialize(Graph),
		              ECadenceArcResolverInitResult::Success)) { return false; }
		if (!ResolveAndExpect(Test, Resolver, Input_Light, Action_Root, Action_Light01, Request,
		                      TEXT("Timed initial action"))) { return false; }
		return StartAndExpect(Test, Resolver, Request, TEXT("Timed initial action"));
	}

	bool BufferAt(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
	                     const FCadenceArcActionRequest& Request, const FGameplayTag& InputTag, const double Timestamp)
	{
		if (!TestHandshake(Test, TEXT("Open timed buffer"), Resolver->OpenBufferWindow(Request.RequestId),
		                   ECadenceArcHandshakeResult::Success)) { return false; }
		FCadenceArcActionRequest Output;
		const bool bBuffered = TestTransition(Test, TEXT("Store timed input"),
		                                      SubmitForTest(Resolver, MakeInput(InputTag, Timestamp), Output),
		                                      ExpectedResolution(ECadenceArcResolutionCategory::Buffered,
		                                                         ECadenceArcResolutionReason::None));
		return TestEmptyRequest(Test, Output) && bBuffered;
	}

	FCadenceArcInputEvent MakeInput(
	const FGameplayTag& InputTag, const double TimestampSeconds)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = InputTag;
		Event.TimestampSeconds = TimestampSeconds;
		return Event;
	}

	void ExpectTimedCompletion(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& CompletedRequest,
		const FCadenceArcActionCompletionOutcome& Outcome,
		const FExpectedResolution Expected,
		const FGameplayTag& ExpectedInput,
		const FGameplayTag& ExpectedTarget)
	{
		TestHandshake(Test, TEXT("Completion handshake succeeds"), Outcome.GetHandshakeResult(),
		              ECadenceArcHandshakeResult::Success);
		TestBufferConsume(Test, TEXT("Timed consumption result"), Outcome, Expected);
		TestTag(Test, TEXT("Completion retains committed node"), Resolver->GetCurrentActionTag(),
		        CompletedRequest.TargetActionTag);
		Test.TestFalse(TEXT("Completion closes window"), Resolver->IsBufferWindowOpen());
		Test.TestFalse(TEXT("Completion clears buffer"), Resolver->GetBufferedInputTag().IsValid());
		if (Expected.Category == ECadenceArcResolutionCategory::RequestProduced)
		{
			TestState(Test, TEXT("Resolved buffer waits for acceptance"), Resolver->GetState(),
			          ECadenceArcResolverState::AwaitingStart);
			Test.TestEqual(TEXT("Exactly one next ID is allocated"), Outcome.GetNextActionRequest().RequestId,
			               CompletedRequest.RequestId + 1);
			Test.TestEqual(TEXT("Next request is outstanding"), Resolver->GetOutstandingRequest().RequestId,
			               Outcome.GetNextActionRequest().RequestId);
			TestTag(Test, TEXT("Next request input"), Outcome.GetNextActionRequest().InputTag, ExpectedInput);
			TestTag(Test, TEXT("Next request source"), Outcome.GetNextActionRequest().SourceActionTag,
			        CompletedRequest.TargetActionTag);
			TestTag(Test, TEXT("Next request target"), Outcome.GetNextActionRequest().TargetActionTag, ExpectedTarget);
		}
		else
		{
			TestState(Test, TEXT("Consumed failure or empty buffer returns Ready"), Resolver->GetState(),
			          ECadenceArcResolverState::Ready);
			TestEmptyRequest(Test, Outcome.GetNextActionRequest());
			TestEmptyRequest(Test, Resolver->GetOutstandingRequest());
		}
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
