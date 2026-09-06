#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "NativeGameplayTags.h"
#include "Resolver/CadenceArcResolver.h"
#include <limits>
#include <type_traits>

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

	// Keep general regression setup independent of the dedicated zero-time contract test.
	static constexpr double RegressionTimestampSeconds = 0.125;

	static FCadenceArcInputEvent MakeInput(
		const FGameplayTag& InputTag, const double TimestampSeconds = RegressionTimestampSeconds)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = InputTag;
		Event.TimestampSeconds = TimestampSeconds;
		return Event;
	}

	// During the Phase 5 API migration, fail explicitly instead of invoking the old
	// one-argument completion method, which dereferences a nonexistent test World.
	// No legacy-clock fallback is allowed: these tests must inject completion time.
	template <typename TResolver = UCadenceArcResolver>
	static bool RequireExplicitCompletionTime(FAutomationTestBase& Test)
	{
		constexpr bool bAcceptsTime = std::is_invocable_r_v<FCadenceArcActionCompletionOutcome,
			decltype(&TResolver::NotifyActionCompleted), TResolver*, int64, double>;
		if constexpr (!bAcceptsTime)
		{
			Test.AddError(TEXT("Phase 5 API missing: NotifyActionCompleted must accept (int64 RequestId, double CompletionTimestampSeconds). Completion behavior was not executed; no World-clock fallback is permitted."));
		}
		return bAcceptsTime;
	}

	template <typename TResolver>
	static FCadenceArcActionCompletionOutcome CompleteAt(
		FAutomationTestBase& Test, TResolver* Resolver, const int64 RequestId, const double TimestampSeconds)
	{
		if constexpr (std::is_invocable_r_v<FCadenceArcActionCompletionOutcome,
			decltype(&TResolver::NotifyActionCompleted), TResolver*, int64, double>)
		{
			return Resolver->NotifyActionCompleted(RequestId, TimestampSeconds);
		}
		else
		{
			RequireExplicitCompletionTime<TResolver>(Test);
			return {};
		}
	}

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
			Resolver->SubmitInput(MakeInput(InputTag), OutRequest),
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
			CompleteAt(Test, Resolver, Request.RequestId, RegressionTimestampSeconds);
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
			Resolver->SubmitInput(MakeInput(InputTag), Request), ExpectedResult);
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
		Resolver->SubmitInput(MakeInput(Input_Light), OutputRequest),
		ECadenceArcInputResult::BufferWindowClosed);
	TestEqual(TEXT("Rejected input clears output request"),
		OutputRequest.RequestId, static_cast<int64>(0));
	TestFalse(TEXT("Rejected input does not populate buffer"),
		Resolver->GetBufferedInputTag().IsValid());

	Resolver->OpenBufferWindow(ExecutingRequest.RequestId);
	TestTransition(*this, TEXT("First input is buffered"),
		Resolver->SubmitInput(MakeInput(Input_Light), OutputRequest),
		ECadenceArcInputResult::Buffered);
	TestTag(*this, TEXT("Buffer stores first input"),
		Resolver->GetBufferedInputTag(), Input_Light);
	TestEqual(TEXT("Buffering does not emit an action request"),
		OutputRequest.RequestId, static_cast<int64>(0));

	TestTransition(*this, TEXT("Second input is buffered"),
		Resolver->SubmitInput(MakeInput(Input_Heavy), OutputRequest),
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
		Resolver->SubmitInput(MakeInput(FGameplayTag::EmptyTag), OutputRequest),
		ECadenceArcInputResult::InvalidInputTag);
	TestTag(*this, TEXT("Invalid input preserves buffered value"),
		Resolver->GetBufferedInputTag(), Input_Heavy);

	Resolver->CloseBufferWindow(ExecutingRequest.RequestId);
	TestFalse(TEXT("Window closes after buffering"), Resolver->IsBufferWindowOpen());
	TestTag(*this, TEXT("Closing window retains buffered input"),
		Resolver->GetBufferedInputTag(), Input_Heavy);
	TestTransition(*this, TEXT("Later input outside window is rejected"),
		Resolver->SubmitInput(MakeInput(Input_Light), OutputRequest),
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
		Resolver->SubmitInput(MakeInput(Input_Light), IgnoredRequest),
		ECadenceArcInputResult::Buffered);
	TestTransition(*this, TEXT("Heavy overwrites buffered Light"),
		Resolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest),
		ECadenceArcInputResult::Buffered);

	const FCadenceArcActionCompletionOutcome Outcome =
		CompleteAt(*this, Resolver, FirstRequest.RequestId, RegressionTimestampSeconds);
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
	if (!RequireExplicitCompletionTime(*this)) { return false; }

	UCadenceArcResolver* NoMatchResolver = NewObject<UCadenceArcResolver>();
	NoMatchResolver->Initialize(MakeValidGraph());
	FCadenceArcActionRequest NoMatchRequest;
	ResolveAndExpect(*this, NoMatchResolver, Input_Heavy,
		Action_Root, Action_Heavy01, NoMatchRequest, TEXT("No-match action"));
	StartAndExpect(*this, NoMatchResolver, NoMatchRequest, TEXT("No-match action"));
	NoMatchResolver->OpenBufferWindow(NoMatchRequest.RequestId);
	FCadenceArcActionRequest IgnoredRequest;
	NoMatchResolver->SubmitInput(MakeInput(Input_Light), IgnoredRequest);
	const FCadenceArcActionCompletionOutcome NoMatchOutcome =
		CompleteAt(*this, NoMatchResolver, NoMatchRequest.RequestId, RegressionTimestampSeconds);
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
	MissingCurrentResolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest);
	MissingCurrentGraph->Nodes.RemoveAll(
		[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Light01; });
	const FCadenceArcActionCompletionOutcome MissingCurrentOutcome =
		CompleteAt(*this, MissingCurrentResolver, MissingCurrentRequest.RequestId, RegressionTimestampSeconds);
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
	MissingTargetResolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest);
	MissingTargetGraph->Nodes.RemoveAll(
		[](const FCadenceArcNode& Node) { return Node.ActionTag == Action_Finisher01; });
	const FCadenceArcActionCompletionOutcome MissingTargetOutcome =
		CompleteAt(*this, MissingTargetResolver, MissingTargetRequest.RequestId, RegressionTimestampSeconds);
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
	if (!RequireExplicitCompletionTime(*this)) { return false; }
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
	Resolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest);
	const FCadenceArcActionCompletionOutcome StaleCompletion =
		CompleteAt(*this, Resolver, Request.RequestId + 1, RegressionTimestampSeconds);
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
		Resolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest),
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
		Resolver->SubmitInput(MakeInput(Input_Heavy), IgnoredRequest),
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
	if (!RequireExplicitCompletionTime(*this)) { return false; }
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

namespace CadenceArc::Tests
{
	static TArray<double> InvalidTimes()
	{
		return { -1.0, std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity() };
	}

	static bool TestEmptyRequest(FAutomationTestBase& Test, const FCadenceArcActionRequest& Request)
	{
		bool bPassed = Test.TestEqual(TEXT("Empty request has no ID"), Request.RequestId, int64{0});
		bPassed &= Test.TestFalse(TEXT("Empty request has no input"), Request.InputTag.IsValid());
		bPassed &= Test.TestFalse(TEXT("Empty request has no source"), Request.SourceActionTag.IsValid());
		bPassed &= Test.TestFalse(TEXT("Empty request has no target"), Request.TargetActionTag.IsValid());
		return bPassed;
	}

	struct FResolverSnapshot
	{
		ECadenceArcResolverState State;
		FGameplayTag CurrentAction;
		FCadenceArcActionRequest Request;
		bool bWindowOpen;
		FGameplayTag BufferedTag;

		explicit FResolverSnapshot(const UCadenceArcResolver* Resolver)
			: State(Resolver->GetState()), CurrentAction(Resolver->GetCurrentActionTag()),
			  Request(Resolver->GetOutstandingRequest()), bWindowOpen(Resolver->IsBufferWindowOpen()),
			  BufferedTag(Resolver->GetBufferedInputTag()) {}

		void ExpectUnchanged(FAutomationTestBase& Test, const UCadenceArcResolver* Resolver) const
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
	};

	static bool BeginTimedAction(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		FCadenceArcActionRequest& Request, const double MaxAge)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->MaxBufferedInputAgeSeconds = MaxAge;
		if (!TestInit(Test, TEXT("Timed graph initializes"), Resolver->Initialize(Graph),
			ECadenceArcResolverInitResult::Success)) { return false; }
		if (!ResolveAndExpect(Test, Resolver, Input_Light, Action_Root, Action_Light01, Request,
			TEXT("Timed initial action"))) { return false; }
		return StartAndExpect(Test, Resolver, Request, TEXT("Timed initial action"));
	}

	static bool BufferAt(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& Request, const FGameplayTag& InputTag, const double Timestamp)
	{
		if (!TestHandshake(Test, TEXT("Open timed buffer"), Resolver->OpenBufferWindow(Request.RequestId),
			ECadenceArcHandshakeResult::Success)) { return false; }
		FCadenceArcActionRequest Output;
		const bool bBuffered = TestTransition(Test, TEXT("Store timed input"),
			Resolver->SubmitInput(MakeInput(InputTag, Timestamp), Output), ECadenceArcInputResult::Buffered);
		return TestEmptyRequest(Test, Output) && bBuffered;
	}

	static void ExpectTimedCompletion(FAutomationTestBase& Test, UCadenceArcResolver* Resolver,
		const FCadenceArcActionRequest& CompletedRequest, const FCadenceArcActionCompletionOutcome& Outcome,
		const ECadenceArcBufferConsumeResult Expected, const FGameplayTag& ExpectedInput = Input_Heavy,
		const FGameplayTag& ExpectedTarget = Action_Finisher01)
	{
		TestHandshake(Test, TEXT("Completion handshake succeeds"), Outcome.HandshakeResult,
			ECadenceArcHandshakeResult::Success);
		TestBufferConsume(Test, TEXT("Timed consumption result"), Outcome.BufferConsumeResult, Expected);
		TestTag(Test, TEXT("Completion retains committed node"), Resolver->GetCurrentActionTag(),
			CompletedRequest.TargetActionTag);
		Test.TestFalse(TEXT("Completion closes window"), Resolver->IsBufferWindowOpen());
		Test.TestFalse(TEXT("Completion clears buffer"), Resolver->GetBufferedInputTag().IsValid());
		if (Expected == ECadenceArcBufferConsumeResult::Resolved)
		{
			TestState(Test, TEXT("Resolved buffer waits for acceptance"), Resolver->GetState(),
				ECadenceArcResolverState::AwaitingStart);
			Test.TestEqual(TEXT("Exactly one next ID is allocated"), Outcome.NextActionRequest.RequestId,
				CompletedRequest.RequestId + 1);
			Test.TestEqual(TEXT("Next request is outstanding"), Resolver->GetOutstandingRequest().RequestId,
				Outcome.NextActionRequest.RequestId);
			TestTag(Test, TEXT("Next request input"), Outcome.NextActionRequest.InputTag, ExpectedInput);
			TestTag(Test, TEXT("Next request source"), Outcome.NextActionRequest.SourceActionTag,
				CompletedRequest.TargetActionTag);
			TestTag(Test, TEXT("Next request target"), Outcome.NextActionRequest.TargetActionTag, ExpectedTarget);
		}
		else
		{
			TestState(Test, TEXT("Consumed failure or empty buffer returns Ready"), Resolver->GetState(),
				ECadenceArcResolverState::Ready);
			TestEmptyRequest(Test, Outcome.NextActionRequest);
			TestEmptyRequest(Test, Resolver->GetOutstandingRequest());
		}
	}
}

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
	TestFalse(TEXT("Missing tag is invalid even with valid time"), MakeInput(FGameplayTag::EmptyTag, 1.0).IsValid());
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
		Resolver->SubmitInput(MakeInput(Input_Light, 0.0), Request), ECadenceArcInputResult::Success))
	{
		return false;
	}
	if (!StartAndExpect(*this, Resolver, Request, TEXT("Zero-time initial action")) ||
		!BufferAt(*this, Resolver, Request, Input_Heavy, 0.0)) { return false; }
	if (!RequireExplicitCompletionTime(*this)) { return false; }
	ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, 0.0),
		ECadenceArcBufferConsumeResult::Resolved);
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
					Invalid, Mode, MaxAge), Resolver->SubmitInput(MakeInput(Input_Light, Invalid), Output),
					ECadenceArcInputResult::InvalidTimestamp);
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
	struct FCase { double MaxAge; double InputTime; double CompletionTime; ECadenceArcBufferConsumeResult Result; };
	// Binary-exact fractions make the equality boundary independent of rounding noise.
	const FCase Cases[] = {
		{0.0, 1.0, 1000000.0, ECadenceArcBufferConsumeResult::Resolved},
		{0.5, 1.0, 1.0, ECadenceArcBufferConsumeResult::Resolved},
		{0.5, 1.0, 1.25, ECadenceArcBufferConsumeResult::Resolved},
		{0.5, 1.0, 1.5, ECadenceArcBufferConsumeResult::Resolved},
		{0.5, 1.0, 1.75, ECadenceArcBufferConsumeResult::Expired}
	};
	for (const FCase& Case : Cases)
	{
		AddInfo(FString::Printf(TEXT("MaxAge=%g Input=%g Completion=%g"), Case.MaxAge, Case.InputTime, Case.CompletionTime));
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestNull(TEXT("Resolver uses no World"), Resolver->GetWorld());
		FCadenceArcActionRequest Request;
		if (!BeginTimedAction(*this, Resolver, Request, Case.MaxAge) ||
			!BufferAt(*this, Resolver, Request, Input_Heavy, Case.InputTime)) { return false; }
		const auto Outcome = CompleteAt(*this, Resolver, Request.RequestId, Case.CompletionTime);
		ExpectTimedCompletion(*this, Resolver, Request, Outcome, Case.Result);
		if (Case.Result == ECadenceArcBufferConsumeResult::Resolved)
		{
			StartAndExpect(*this, Resolver, Outcome.NextActionRequest, TEXT("Accept timed next action"));
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
			Resolver->SubmitInput(MakeInput(Input_Heavy, 2.0), Output), ECadenceArcInputResult::BufferWindowClosed);
		TestEmptyRequest(*this, Output);
		// The first event would expire. The replacement remains valid exactly at its boundary.
		const auto Outcome = CompleteAt(*this, Resolver, Request.RequestId, 2.25);
		ExpectTimedCompletion(*this, Resolver, Request, Outcome, ECadenceArcBufferConsumeResult::Resolved,
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
		Resolver->SubmitInput(MakeInput(Input_Heavy, 1.75), Output), ECadenceArcInputResult::BufferWindowClosed);
	ExpectTimedCompletion(*this, Resolver, Request, CompleteAt(*this, Resolver, Request.RequestId, 2.0),
		ECadenceArcBufferConsumeResult::Expired);
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
	struct FCase { double CompletionTime; ECadenceArcBufferConsumeResult Result; };
	const FCase Cases[] = {
		{0.5, ECadenceArcBufferConsumeResult::InvalidTime},
		{1.5, ECadenceArcBufferConsumeResult::Resolved},
		{1.75, ECadenceArcBufferConsumeResult::Expired}
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
				Resolver->SubmitInput(MakeInput(Input_Heavy, Invalid), Output), ECadenceArcInputResult::InvalidTimestamp);
			TestEmptyRequest(*this, Output);
			TestTransition(*this, TEXT("Invalid tag cannot refresh timestamp"),
				Resolver->SubmitInput(MakeInput(FGameplayTag::EmptyTag, 1.25), Output), ECadenceArcInputResult::InvalidInputTag);
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
				ECadenceArcBufferConsumeResult::InvalidTime);
			const auto Repeated = CompleteAt(*this, Resolver, Request.RequestId, 1.5);
			TestHandshake(*this, TEXT("InvalidTime already completed the old action"), Repeated.HandshakeResult,
				ECadenceArcHandshakeResult::UnexpectedState);
			TestBufferConsume(*this, TEXT("Repeated completion does not consume"), Repeated.BufferConsumeResult,
				ECadenceArcBufferConsumeResult::NotAttempted);
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
			ECadenceArcBufferConsumeResult::NoBufferedInput);
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
		TestHandshake(*this, TEXT("Handshake error takes precedence over time"), Outcome.HandshakeResult, Expected);
		TestBufferConsume(*this, TEXT("Rejected handshake does not consume"), Outcome.BufferConsumeResult,
			ECadenceArcBufferConsumeResult::NotAttempted);
		TestEmptyRequest(*this, Outcome.NextActionRequest);
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
		ECadenceArcBufferConsumeResult::Resolved);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
