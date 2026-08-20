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

	static bool TestInitResult(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverInitResult Actual,
		const ECadenceArcResolverInitResult Expected)
	{
		return Test.TestEqual(What, static_cast<int32>(Actual), static_cast<int32>(Expected));
	}

	static bool TestTransitionResult(
		FAutomationTestBase& Test,
		const TCHAR* What,
		const ECadenceArcResolverTransitionResult Actual,
		const ECadenceArcResolverTransitionResult Expected)
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
		const FGameplayTag& ExpectedActionTag,
		const TCHAR* StepName)
	{
		FGameplayTag ResolvedActionTag;
		const ECadenceArcResolverTransitionResult Result =
			Resolver->TryResolveInput(InputTag, ResolvedActionTag);

		bool bPassed = TestTransitionResult(
			Test, *FString::Printf(TEXT("%s returns Success"), StepName),
			Result, ECadenceArcResolverTransitionResult::Success);
		bPassed &= TestTag(
			Test, *FString::Printf(TEXT("%s resolves expected action"), StepName),
			ResolvedActionTag, ExpectedActionTag);
		bPassed &= TestTag(
			Test, *FString::Printf(TEXT("%s commits expected current action"), StepName),
			Resolver->GetCurrentActionTag(), ExpectedActionTag);
		return bPassed;
	}

	static bool ResolveFailureAndExpectUnchangedState(
		FAutomationTestBase& Test,
		UCadenceArcResolver* Resolver,
		const FGameplayTag& InputTag,
		const ECadenceArcResolverTransitionResult ExpectedResult,
		const FGameplayTag& ExpectedCurrentActionTag,
		const TCHAR* StepName)
	{
		FGameplayTag ResolvedActionTag = Action_Finisher03;
		const ECadenceArcResolverTransitionResult Result =
			Resolver->TryResolveInput(InputTag, ResolvedActionTag);

		bool bPassed = TestTransitionResult(
			Test, *FString::Printf(TEXT("%s returns expected failure"), StepName),
			Result, ExpectedResult);
		bPassed &= Test.TestFalse(
			*FString::Printf(TEXT("%s clears output action"), StepName),
			ResolvedActionTag.IsValid());
		bPassed &= TestTag(
			Test, *FString::Printf(TEXT("%s preserves current action"), StepName),
			Resolver->GetCurrentActionTag(), ExpectedCurrentActionTag);
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcInitializeValidGraphTest,
	"CadenceArc.Resolver.Initialize.ValidGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcInitializeValidGraphTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();

	TestInitResult(*this, TEXT("Valid graph initializes"),
		Resolver->Initialize(Graph), ECadenceArcResolverInitResult::Success);
	TestTrue(TEXT("Resolver reports initialized"), Resolver->IsInitialized());
	TestTag(*this, TEXT("Current action starts at entry"),
		Resolver->GetCurrentActionTag(), Action_Root);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcInitializeInvalidGraphTest,
	"CadenceArc.Resolver.Initialize.InvalidGraph",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcInitializeInvalidGraphTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();

	TestInitResult(*this, TEXT("Null graph is rejected"),
		Resolver->Initialize(nullptr), ECadenceArcResolverInitResult::InvalidGraph);
	TestFalse(TEXT("Resolver remains uninitialized"), Resolver->IsInitialized());
	TestFalse(TEXT("Current action remains invalid"), Resolver->GetCurrentActionTag().IsValid());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcInitializeInvalidEntryTagTest,
	"CadenceArc.Resolver.Initialize.InvalidEntryActionTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcInitializeInvalidEntryTagTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();

	TestInitResult(*this, TEXT("Invalid entry action tag is rejected"),
		Resolver->Initialize(Graph), ECadenceArcResolverInitResult::InvalidEntryActionTag);
	TestFalse(TEXT("Resolver remains uninitialized"), Resolver->IsInitialized());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcInitializeEntryNodeNotFoundTest,
	"CadenceArc.Resolver.Initialize.EntryNodeNotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcInitializeEntryNodeNotFoundTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
	Graph->EntryActionTag = Action_Root;
	AddNode(Graph, Action_Light01);
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();

	TestInitResult(*this, TEXT("Missing entry node is rejected"),
		Resolver->Initialize(Graph), ECadenceArcResolverInitResult::EntryNodeNotFound);
	TestFalse(TEXT("Resolver remains uninitialized"), Resolver->IsInitialized());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcInitializeFailurePreservesStateTest,
	"CadenceArc.Resolver.Initialize.FailurePreservesState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcInitializeFailurePreservesStateTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestInitResult(*this, TEXT("Initial valid graph initializes"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Success);
	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, TEXT("Root + Light"));

	TestInitResult(*this, TEXT("Invalid replacement graph is rejected"),
		Resolver->Initialize(nullptr), ECadenceArcResolverInitResult::InvalidGraph);
	TestTrue(TEXT("Resolver remains initialized after rejected replacement"), Resolver->IsInitialized());
	TestTag(*this, TEXT("Rejected replacement preserves current action"),
		Resolver->GetCurrentActionTag(), Action_Light01);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResolveValidBranchesTest,
	"CadenceArc.Resolver.Resolve.ValidBranches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResolveValidBranchesTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestInitResult(*this, TEXT("Graph initializes"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Success);

	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, TEXT("Root + Light"));
	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light02, TEXT("Light01 + Light"));
	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Finisher02, TEXT("Light02 + Heavy"));

	TestTrue(TEXT("Reset before mixed branch succeeds"), Resolver->Reset());
	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, TEXT("Root + Light after reset"));
	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Finisher01, TEXT("Light01 + Heavy"));

	TestTrue(TEXT("Reset before heavy branch succeeds"), Resolver->Reset());
	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Heavy01, TEXT("Root + Heavy"));
	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Heavy02, TEXT("Heavy01 + Heavy"));
	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Finisher03, TEXT("Heavy02 + Heavy"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResolveBasicFailuresTest,
	"CadenceArc.Resolver.Resolve.BasicFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResolveBasicFailuresTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();

	ResolveFailureAndExpectUnchangedState(
		*this, Resolver, Input_Light, ECadenceArcResolverTransitionResult::NotInitialized,
		FGameplayTag::EmptyTag, TEXT("Resolve before initialization"));

	TestInitResult(*this, TEXT("Graph initializes"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Success);
	ResolveFailureAndExpectUnchangedState(
		*this, Resolver, FGameplayTag::EmptyTag, ECadenceArcResolverTransitionResult::InvalidInputTag,
		Action_Root, TEXT("Resolve invalid input"));

	ResolveAndExpect(*this, Resolver, Input_Heavy, Action_Heavy01, TEXT("Root + Heavy"));
	ResolveFailureAndExpectUnchangedState(
		*this, Resolver, Input_Light, ECadenceArcResolverTransitionResult::NoMatchingTransition,
		Action_Heavy01, TEXT("Heavy01 + Light"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResolveCurrentNodeNotFoundTest,
	"CadenceArc.Resolver.Resolve.CurrentNodeNotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResolveCurrentNodeNotFoundTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestInitResult(*this, TEXT("Graph initializes"),
		Resolver->Initialize(Graph), ECadenceArcResolverInitResult::Success);

	Graph->Nodes.RemoveAll([](const FCadenceArcNode& Node)
	{
		return Node.ActionTag == Action_Root;
	});
	ResolveFailureAndExpectUnchangedState(
		*this, Resolver, Input_Light, ECadenceArcResolverTransitionResult::CurrentNodeNotFound,
		Action_Root, TEXT("Resolve after current node removal"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResolveTargetNodeNotFoundTest,
	"CadenceArc.Resolver.Resolve.TargetNodeNotFound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResolveTargetNodeNotFoundTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestInitResult(*this, TEXT("Graph initializes"),
		Resolver->Initialize(Graph), ECadenceArcResolverInitResult::Success);

	Graph->Nodes.RemoveAll([](const FCadenceArcNode& Node)
	{
		return Node.ActionTag == Action_Light01;
	});
	ResolveFailureAndExpectUnchangedState(
		*this, Resolver, Input_Light, ECadenceArcResolverTransitionResult::TargetNodeNotFound,
		Action_Root, TEXT("Resolve dangling target"));
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcResetTest,
	"CadenceArc.Resolver.Reset.Behavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcResetTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
	TestFalse(TEXT("Reset before initialization fails"), Resolver->Reset());
	TestFalse(TEXT("Failed reset leaves current action invalid"), Resolver->GetCurrentActionTag().IsValid());

	TestInitResult(*this, TEXT("Graph initializes"),
		Resolver->Initialize(MakeValidGraph()), ECadenceArcResolverInitResult::Success);
	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light01, TEXT("Root + Light"));
	ResolveAndExpect(*this, Resolver, Input_Light, Action_Light02, TEXT("Light01 + Light"));

	TestTrue(TEXT("Reset after resolution succeeds"), Resolver->Reset());
	TestTag(*this, TEXT("Reset restores entry action"),
		Resolver->GetCurrentActionTag(), Action_Root);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
