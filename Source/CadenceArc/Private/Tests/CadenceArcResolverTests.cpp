#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "NativeGameplayTags.h"
#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Tests
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Root, "CadenceArc.Automation.Action.Root");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Light01, "CadenceArc.Automation.Action.Light01");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Light02, "CadenceArc.Automation.Action.Light02");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Heavy01, "CadenceArc.Automation.Action.Heavy01");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Heavy02, "CadenceArc.Automation.Action.Heavy02");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Finisher01, "CadenceArc.Automation.Action.Finisher01");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Finisher02, "CadenceArc.Automation.Action.Finisher02");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Action_Finisher03, "CadenceArc.Automation.Action.Finisher03");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Input_Light, "CadenceArc.Automation.Input.Light");
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Input_Heavy, "CadenceArc.Automation.Input.Heavy");

	static FCadenceArcNode& AddNode(UCadenceArcGraph* Graph, const FGameplayTag& ActionTag)
	{
		FCadenceArcNode& Node = Graph->Nodes.AddDefaulted_GetRef();
		Node.ActionTag = ActionTag;
		return Node;
	}

	static void AddTransition(
		FCadenceArcNode& Node,
		const FGameplayTag& InputTag,
		const FGameplayTag& TargetActionTag)
	{
		FCadenceArcTransition& Transition = Node.Transitions.AddDefaulted_GetRef();
		Transition.InputTag = InputTag;
		Transition.TargetActionTag = TargetActionTag;
	}

	static UCadenceArcGraph* MakeValidGraph()
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

	static bool TestInit(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverInitResult Actual,
		const ECadenceArcResolverInitResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestTransition(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcInputResult Actual,
		const ECadenceArcInputResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestHandshake(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcHandshakeResult Actual,
		const ECadenceArcHandshakeResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestBufferConsume(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcBufferConsumeResult Actual,
		const ECadenceArcBufferConsumeResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestState(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverState Actual,
		const ECadenceArcResolverState Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestTag(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const FGameplayTag& Actual,
		const FGameplayTag& Expected)
	{
		return Test.TestEqual(What, Actual.ToString(), Expected.ToString());
	}

	static bool ResolveAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const FGameplayTag& SourceTag,
		const FGameplayTag& TargetTag,
		FCadenceArcActionRequest& OutRequest,
		const TCHAR* Step)
	{
		bool bPassed = TestTransition(
			Test, *FString::Printf(TEXT("%s resolves"), Step),
			Resolver->SubmitInput(InputTag, OutRequest),
			ECadenceArcInputResult::Success);
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

	static bool StartAndExpect(
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

	static bool CompleteAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request,
		const TCHAR* Step)
	{
		const FCadenceArcActionCompletionOutcome Outcome =
			Resolver->NotifyActionCompleted(Request.RequestId);
		bool bPassed = TestHandshake(
			Test, *FString::Printf(TEXT("%s completes"), Step),
			Outcome.HandshakeResult,
			ECadenceArcHandshakeResult::Success);
		bPassed &= TestBufferConsume(
			Test, *FString::Printf(TEXT("%s reports no buffered input"), Step),
			Outcome.BufferConsumeResult,
			ECadenceArcBufferConsumeResult::NoBufferedInput);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s does not emit a next request"), Step),
			Outcome.NextActionRequest.RequestId, static_cast<int64>(0));
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s returns Ready"), Step),
			Resolver->GetState(), ECadenceArcResolverState::Ready);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s preserves target"), Step),
			Resolver->GetCurrentActionTag(), Request.TargetActionTag);
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("%s clears request"), Step),
			Resolver->GetOutstandingRequest().RequestId, static_cast<int64>(0));
		return bPassed;
	}

	static bool ExecuteAndComplete(
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

	static bool ResolveFailureAndExpect(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const ECadenceArcInputResult ExpectedResult,
		const FGameplayTag& ExpectedCurrentTag,
		const ECadenceArcResolverState ExpectedState,
		const int64 ExpectedOutstandingId,
		const TCHAR* Step)
	{
		FCadenceArcActionRequest Request;
		Request.RequestId = 999;
		Request.TargetActionTag = Action_Finisher03;
		bool bPassed = TestTransition(
			Test, *FString::Printf(TEXT("%s returns expected failure"), Step),
			Resolver->SubmitInput(InputTag, Request), ExpectedResult);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s clears output ID"), Step),
			Request.RequestId, static_cast<int64>(0));
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s clears output target"), Step),
			Request.TargetActionTag.IsValid());
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s preserves current action"), Step),
			Resolver->GetCurrentActionTag(), ExpectedCurrentTag);
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s preserves state"), Step),
			Resolver->GetState(), ExpectedState);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s preserves outstanding request"), Step),
			Resolver->GetOutstandingRequest().RequestId, ExpectedOutstandingId);
		return bPassed;
	}
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
		ECadenceArcInputResult::RequestPending, Action_Root,
		ECadenceArcResolverState::AwaitingStart, Request.RequestId,
		TEXT("Resolve while awaiting start"));
	StartAndExpect(*this, Resolver, Request, TEXT("Initial request"));
	ResolveFailureAndExpect(*this, Resolver, Input_Heavy,
		ECadenceArcInputResult::BufferWindowClosed, Action_Light01,
		ECadenceArcResolverState::Executing, Request.RequestId,
		TEXT("Resolve while executing outside buffer window"));
	return !HasAnyErrors();
}

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
		Resolver->CloseBufferWindow(Request.RequestId + 1), ECadenceArcHandshakeResult::RequestIdMismatch);
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
		Resolver->SubmitInput(Input_Light, OutputRequest),
		ECadenceArcInputResult::BufferWindowClosed);
	TestEqual(TEXT("Rejected input clears output request"),
		OutputRequest.RequestId, static_cast<int64>(0));
	TestFalse(TEXT("Rejected input does not populate buffer"),
		Resolver->GetBufferedInputTag().IsValid());

	Resolver->OpenBufferWindow(ExecutingRequest.RequestId);
	TestTransition(*this, TEXT("First input is buffered"),
		Resolver->SubmitInput(Input_Light, OutputRequest),
		ECadenceArcInputResult::Buffered);
	TestTag(*this, TEXT("Buffer stores first input"),
		Resolver->GetBufferedInputTag(), Input_Light);
	TestEqual(TEXT("Buffering does not emit an action request"),
		OutputRequest.RequestId, static_cast<int64>(0));

	TestTransition(*this, TEXT("Second input is buffered"),
		Resolver->SubmitInput(Input_Heavy, OutputRequest),
		ECadenceArcInputResult::Buffered);
	TestTag(*this, TEXT("Later input overwrites earlier input"),
		Resolver->GetBufferedInputTag(), Input_Heavy);
	TestState(*this, TEXT("Buffering preserves Executing state"),
		Resolver->GetState(), ECadenceArcResolverState::Executing);
	TestTag(*this, TEXT("Buffering preserves current action"),
		Resolver->GetCurrentActionTag(), Action_Light01);
	TestEqual(TEXT("Buffering preserves outstanding request"),
		Resolver->GetOutstandingRequest().RequestId, ExecutingRequest.RequestId);

	TestTransition(*this, TEXT("Invalid input is rejected while window is open"),
		Resolver->SubmitInput(FGameplayTag::EmptyTag, OutputRequest),
		ECadenceArcInputResult::InvalidInputTag);
	TestTag(*this, TEXT("Invalid input preserves buffered value"),
		Resolver->GetBufferedInputTag(), Input_Heavy);

	Resolver->CloseBufferWindow(ExecutingRequest.RequestId);
	TestFalse(TEXT("Window closes after buffering"), Resolver->IsBufferWindowOpen());
	TestTag(*this, TEXT("Closing window retains buffered input"),
		Resolver->GetBufferedInputTag(), Input_Heavy);
	TestTransition(*this, TEXT("Later input outside window is rejected"),
		Resolver->SubmitInput(Input_Light, OutputRequest),
		ECadenceArcInputResult::BufferWindowClosed);
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
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	Resolver->Initialize(MakeValidGraph());

	FCadenceArcActionRequest FirstRequest;
	ResolveAndExpect(*this, Resolver, Input_Light,
		Action_Root, Action_Light01, FirstRequest, TEXT("First action"));
	StartAndExpect(*this, Resolver, FirstRequest, TEXT("First action"));
	Resolver->OpenBufferWindow(FirstRequest.RequestId);

	FCadenceArcActionRequest IgnoredRequest;
	TestTransition(*this, TEXT("Light is buffered first"),
		Resolver->SubmitInput(Input_Light, IgnoredRequest),
		ECadenceArcInputResult::Buffered);
	TestTransition(*this, TEXT("Heavy overwrites buffered Light"),
		Resolver->SubmitInput(Input_Heavy, IgnoredRequest),
		ECadenceArcInputResult::Buffered);

	const FCadenceArcActionCompletionOutcome Outcome =
		Resolver->NotifyActionCompleted(FirstRequest.RequestId);
	TestHandshake(*this, TEXT("Buffered completion succeeds"),
		Outcome.HandshakeResult, ECadenceArcHandshakeResult::Success);
	TestBufferConsume(*this, TEXT("Buffered input resolves"),
		Outcome.BufferConsumeResult, ECadenceArcBufferConsumeResult::Resolved);
	TestTrue(TEXT("Completion emits a newer request ID"),
		Outcome.NextActionRequest.RequestId > FirstRequest.RequestId);
	TestTag(*this, TEXT("Next request uses last buffered input"),
		Outcome.NextActionRequest.InputTag, Input_Heavy);
	TestTag(*this, TEXT("Next request starts from completed action"),
		Outcome.NextActionRequest.SourceActionTag, Action_Light01);
	TestTag(*this, TEXT("Last buffered input selects heavy finisher"),
		Outcome.NextActionRequest.TargetActionTag, Action_Finisher01);
	TestState(*this, TEXT("Resolved completion enters AwaitingStart"),
		Resolver->GetState(), ECadenceArcResolverState::AwaitingStart);
	TestTag(*this, TEXT("Next action is not committed before start"),
		Resolver->GetCurrentActionTag(), Action_Light01);
	TestEqual(TEXT("Resolver exposes next outstanding request"),
		Resolver->GetOutstandingRequest().RequestId, Outcome.NextActionRequest.RequestId);
	TestFalse(TEXT("Completion closes buffer window"), Resolver->IsBufferWindowOpen());
	TestFalse(TEXT("Completion clears consumed input"), Resolver->GetBufferedInputTag().IsValid());

	StartAndExpect(*this, Resolver, Outcome.NextActionRequest, TEXT("Buffered next action"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcBufferConsumeFailuresTest,
	"CadenceArc.Resolver.Buffer.ConsumeFailuresPreserveState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcBufferConsumeFailuresTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;

	UCadenceArcResolver* NoMatchResolver = NewObject<UCadenceArcResolver>();
	NoMatchResolver->Initialize(MakeValidGraph());
	FCadenceArcActionRequest NoMatchRequest;
	ResolveAndExpect(*this, NoMatchResolver, Input_Heavy,
		Action_Root, Action_Heavy01, NoMatchRequest, TEXT("No-match action"));
	StartAndExpect(*this, NoMatchResolver, NoMatchRequest, TEXT("No-match action"));
	NoMatchResolver->OpenBufferWindow(NoMatchRequest.RequestId);
	FCadenceArcActionRequest IgnoredRequest;
	NoMatchResolver->SubmitInput(Input_Light, IgnoredRequest);
	const FCadenceArcActionCompletionOutcome NoMatchOutcome =
		NoMatchResolver->NotifyActionCompleted(NoMatchRequest.RequestId);
	TestHandshake(*this, TEXT("No-match completion succeeds"),
		NoMatchOutcome.HandshakeResult, ECadenceArcHandshakeResult::Success);
	TestBufferConsume(*this, TEXT("Missing transition is reported"),
		NoMatchOutcome.BufferConsumeResult, ECadenceArcBufferConsumeResult::NoMatchingTransition);
	TestState(*this, TEXT("Missing transition leaves resolver Ready"),
		NoMatchResolver->GetState(), ECadenceArcResolverState::Ready);
	TestTag(*this, TEXT("Missing transition preserves completed action"),
		NoMatchResolver->GetCurrentActionTag(), Action_Heavy01);
	TestEqual(TEXT("Missing transition emits no next request"),
		NoMatchOutcome.NextActionRequest.RequestId, static_cast<int64>(0));

	UCadenceArcGraph* MissingCurrentGraph = MakeValidGraph();
	UCadenceArcResolver* MissingCurrentResolver = NewObject<UCadenceArcResolver>();
	MissingCurrentResolver->Initialize(MissingCurrentGraph);
	FCadenceArcActionRequest MissingCurrentRequest;
	ResolveAndExpect(*this, MissingCurrentResolver, Input_Light,
		Action_Root, Action_Light01, MissingCurrentRequest, TEXT("Missing-current action"));
	StartAndExpect(*this, MissingCurrentResolver, MissingCurrentRequest, TEXT("Missing-current action"));
	MissingCurrentResolver->OpenBufferWindow(MissingCurrentRequest.RequestId);
	MissingCurrentResolver->SubmitInput(Input_Heavy, IgnoredRequest);
	MissingCurrentGraph->Nodes.RemoveAll(
		[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Light01; });
	const FCadenceArcActionCompletionOutcome MissingCurrentOutcome =
		MissingCurrentResolver->NotifyActionCompleted(MissingCurrentRequest.RequestId);
	TestBufferConsume(*this, TEXT("Missing current node is reported during consumption"),
		MissingCurrentOutcome.BufferConsumeResult, ECadenceArcBufferConsumeResult::CurrentNodeNotFound);
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
	MissingTargetResolver->SubmitInput(Input_Heavy, IgnoredRequest);
	MissingTargetGraph->Nodes.RemoveAll(
		[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Finisher01; });
	const FCadenceArcActionCompletionOutcome MissingTargetOutcome =
		MissingTargetResolver->NotifyActionCompleted(MissingTargetRequest.RequestId);
	TestBufferConsume(*this, TEXT("Missing target node is reported during consumption"),
		MissingTargetOutcome.BufferConsumeResult, ECadenceArcBufferConsumeResult::TargetNodeNotFound);
	TestState(*this, TEXT("Missing target node leaves resolver Ready"),
		MissingTargetResolver->GetState(), ECadenceArcResolverState::Ready);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcLifecycleTest,
	"CadenceArc.Resolver.Handshake.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
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
	FCadenceArcValidBranchesTest,
	"CadenceArc.Resolver.Resolve.ValidBranches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcValidBranchesTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	Resolver->Initialize(MakeValidGraph());

	ExecuteAndComplete(*this, Resolver, Input_Light, Action_Root, Action_Light01, TEXT("Root + Light"));
	ExecuteAndComplete(*this, Resolver, Input_Light, Action_Light01, Action_Light02, TEXT("Light01 + Light"));
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Light02, Action_Finisher02, TEXT("Light02 + Heavy"));

	TestTrue(TEXT("Reset before mixed branch succeeds"), Resolver->Reset());
	ExecuteAndComplete(*this, Resolver, Input_Light, Action_Root, Action_Light01, TEXT("Root + Light after reset"));
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Light01, Action_Finisher01, TEXT("Light01 + Heavy"));

	TestTrue(TEXT("Reset before heavy branch succeeds"), Resolver->Reset());
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Root, Action_Heavy01, TEXT("Root + Heavy"));
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Heavy01, Action_Heavy02, TEXT("Heavy01 + Heavy"));
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Heavy02, Action_Finisher03, TEXT("Heavy02 + Heavy"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResolutionFailuresTest,
	"CadenceArc.Resolver.Resolve.FailuresPreserveState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResolutionFailuresTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	ResolveFailureAndExpect(*this, Resolver, Input_Light,
		ECadenceArcInputResult::NotInitialized, FGameplayTag::EmptyTag,
		ECadenceArcResolverState::Uninitialized, 0, TEXT("Resolve before initialization"));
	Resolver->Initialize(MakeValidGraph());
	ResolveFailureAndExpect(*this, Resolver, FGameplayTag::EmptyTag,
		ECadenceArcInputResult::InvalidInputTag, Action_Root,
		ECadenceArcResolverState::Ready, 0, TEXT("Resolve invalid input"));
	ExecuteAndComplete(*this, Resolver, Input_Heavy, Action_Root, Action_Heavy01, TEXT("Root + Heavy"));
	ResolveFailureAndExpect(*this, Resolver, Input_Light,
		ECadenceArcInputResult::NoMatchingTransition, Action_Heavy01,
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
		ECadenceArcInputResult::CurrentNodeNotFound, Action_Root,
		ECadenceArcResolverState::Ready, 0, TEXT("Missing current node"));

	Graph = MakeValidGraph();
	Resolver->Initialize(Graph);
	Graph->Nodes.RemoveAll([](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Light01; });
	ResolveFailureAndExpect(*this, Resolver, Input_Light,
		ECadenceArcInputResult::TargetNodeNotFound, Action_Root,
		ECadenceArcResolverState::Ready, 0, TEXT("Missing target node"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHandshakeErrorsTest,
	"CadenceArc.Resolver.Handshake.ErrorsPreserveState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHandshakeErrorsTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
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
	Resolver->SubmitInput(Input_Heavy, IgnoredRequest);
	const FCadenceArcActionCompletionOutcome StaleCompletion =
		Resolver->NotifyActionCompleted(Request.RequestId + 1);
	TestHandshake(*this, TEXT("Stale completion ID is rejected"),
		StaleCompletion.HandshakeResult, ECadenceArcHandshakeResult::RequestIdMismatch);
	TestBufferConsume(*this, TEXT("Stale completion does not attempt consumption"),
		StaleCompletion.BufferConsumeResult, ECadenceArcBufferConsumeResult::NotAttempted);
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
		Resolver->SubmitInput(Input_Heavy, IgnoredRequest),
		ECadenceArcInputResult::Buffered);
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
		Resolver->SubmitInput(Input_Heavy, IgnoredRequest),
		ECadenceArcInputResult::Buffered);
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
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestFalse(TEXT("Reset before initialization fails"), Resolver->Reset());
	Resolver->Initialize(MakeValidGraph());

	FCadenceArcActionRequest Request;
	ResolveAndExpect(*this, Resolver, Input_Light,
		Action_Root, Action_Light01, Request, TEXT("Busy request"));
	TestFalse(TEXT("Reset while AwaitingStart fails"), Resolver->Reset());
	TestInit(*this, TEXT("Initialize while AwaitingStart is Busy"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Busy);
	StartAndExpect(*this, Resolver, Request, TEXT("Busy request"));
	TestFalse(TEXT("Reset while Executing fails"), Resolver->Reset());
	TestInit(*this, TEXT("Initialize while Executing is Busy"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Busy);
	CompleteAndExpect(*this, Resolver, Request, TEXT("Busy request"));
	TestTrue(TEXT("Reset while Ready succeeds"), Resolver->Reset());
	TestTag(*this, TEXT("Ready reset restores entry"), Resolver->GetCurrentActionTag(), Action_Root);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcRequestIdTest,
	"CadenceArc.Resolver.RequestId.Monotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcRequestIdTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
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

#endif // WITH_DEV_AUTOMATION_TESTS
