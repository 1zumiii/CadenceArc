#if WITH_EDITOR
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraph.h"
#include "Misc/DataValidation.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	static TArray<FString> ValidationMessages(const FDataValidationContext& Context)
	{
		TArray<FString> Messages;
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			Messages.Add(Issue.Message.ToString());
		}
		return Messages;
	}

	static bool HasValidationMessage(const TArray<FString>& Messages, const FString& Fragment)
	{
		return Messages.ContainsByPredicate([&Fragment](const FString& Message)
		{
			return Message.Contains(Fragment);
		});
	}

	static FString GraphValidationSnapshot(const UCadenceArcGraph* Graph)
	{
		FString Snapshot = FString::Printf(TEXT("Entry=%s;Age=%.17g;Nodes=%d;"),
		                                   *Graph->EntryActionTag.ToString(), Graph->MaxBufferedInputAgeSeconds,
		                                   Graph->Nodes.Num());
		for (const FCadenceArcNode& Node : Graph->Nodes)
		{
			Snapshot += FString::Printf(TEXT("Node=%s;Transitions=%d;"),
			                            *Node.ActionTag.ToString(), Node.Transitions.Num());
			for (const FCadenceArcTransition& Transition : Node.Transitions)
			{
				Snapshot += FString::Printf(TEXT("%s>%s;"), *Transition.InputTag.ToString(),
				                            *Transition.TargetActionTag.ToString());
			}
		}
		return Snapshot;
	}

	static EDataValidationResult ValidateGraph(UCadenceArcGraph* Graph, TArray<FString>& OutMessages)
	{
		FDataValidationContext Context;
		const EDataValidationResult Result = Graph->IsDataValid(Context);
		OutMessages = ValidationMessages(Context);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationValidTopologyTest,
	"CadenceArc.Graph.Validation.ValidTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationValidTopologyTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	// Forward references, cycles, self-loops, and terminal nodes are all valid graph shapes.
	Graph->Nodes.Reset();
	Graph->EntryActionTag = Action_Root;
	FCadenceArcNode& Root = AddNode(Graph, Action_Root);
	AddTransition(Root, Input_Light, Action_Light01);
	AddTransition(Root, Input_Heavy, Action_Heavy01);
	FCadenceArcNode& Light = AddNode(Graph, Action_Light01);
	AddTransition(Light, Input_Light, Action_Light01);
	AddTransition(Light, Input_Heavy, Action_Heavy01);
	FCadenceArcNode& Heavy = AddNode(Graph, Action_Heavy01);
	AddTransition(Heavy, Input_Light, Action_Root);
	AddNode(Graph, Action_Finisher01);

	TArray<FString> Messages;
	TestEqual(TEXT("Valid topology returns Valid"), static_cast<uint8>(ValidateGraph(Graph, Messages)),
	          static_cast<uint8>(EDataValidationResult::Valid));
	TestEqual(TEXT("Valid topology reports no errors"), Messages.Num(), 0);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationNodeAndEntryTest,
	"CadenceArc.Graph.Validation.NodesAndEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationNodeAndEntryTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(UCadenceArcGraph*)> Configure;
		FString Fragment;
	};
	const FCase Cases[] = {
		{TEXT("Empty nodes"), [](UCadenceArcGraph* Graph) { Graph->Nodes.Reset(); }, TEXT("Nodes")},
		{
			TEXT("Invalid node tag"),
			[](UCadenceArcGraph* Graph) { Graph->Nodes[0].ActionTag = FGameplayTag::EmptyTag; }, TEXT("index 0")
		},
		{
			TEXT("Duplicate node tag"), [](UCadenceArcGraph* Graph) { AddNode(Graph, Action_Root); },
			Action_Root.GetTag().ToString()
		},
		{
			TEXT("Invalid entry"), [](UCadenceArcGraph* Graph) { Graph->EntryActionTag = FGameplayTag::EmptyTag; },
			TEXT("EntryActionTag")
		},
		{
			TEXT("Missing entry"), [](UCadenceArcGraph* Graph) { Graph->EntryActionTag = Input_Light; },
			Input_Light.GetTag().ToString()
		},
	};
	for (const FCase& Case : Cases)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Case.Configure(Graph);
		const FString Before = GraphValidationSnapshot(Graph);
		TArray<FString> Messages;
		TestEqual(Case.Name, static_cast<uint8>(ValidateGraph(Graph, Messages)),
		          static_cast<uint8>(EDataValidationResult::Invalid));
		TestTrue(*FString::Printf(TEXT("%s includes location or tag"), Case.Name),
		         HasValidationMessage(Messages, Case.Fragment));
		TestEqual(*FString::Printf(TEXT("%s does not mutate the graph"), Case.Name),
		          GraphValidationSnapshot(Graph), Before);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationAgeTest,
	"CadenceArc.Graph.Validation.BufferedInputAge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationAgeTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();
	struct FCase
	{
		double Age;
		bool bValid;
	};
	const FCase Cases[] = {
		{0.0, true}, {0.25, true}, {-0.25, false}, {NaN, false}, {Infinity, false}, {-Infinity, false}
	};
	for (const FCase& Case : Cases)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->MaxBufferedInputAgeSeconds = Case.Age;
		TArray<FString> Messages;
		TestEqual(*FString::Printf(TEXT("Age %g result"), Case.Age),
		          static_cast<uint8>(ValidateGraph(Graph, Messages)),
		          static_cast<uint8>(Case.bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid));
		if (!Case.bValid)
		{
			TestTrue(*FString::Printf(TEXT("Age %g identifies property"), Case.Age),
			         HasValidationMessage(Messages, TEXT("MaxBufferedInputAgeSeconds")));
		}
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationTransitionTest,
	"CadenceArc.Graph.Validation.Transitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationTransitionTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	struct FCase
	{
		const TCHAR* Name;
		TFunction<void(UCadenceArcGraph*)> Configure;
		FString Fragment;
	};
	const FCase Cases[] = {
		{
			TEXT("Invalid input"),
			[](UCadenceArcGraph* Graph) { Graph->Nodes[0].Transitions[0].InputTag = FGameplayTag::EmptyTag; },
			TEXT("index 0")
		},
		{
			TEXT("Invalid target"),
			[](UCadenceArcGraph* Graph) { Graph->Nodes[0].Transitions[0].TargetActionTag = FGameplayTag::EmptyTag; },
			TEXT("index 0")
		},
		{
			TEXT("Missing target"),
			[](UCadenceArcGraph* Graph) { Graph->Nodes[0].Transitions[0].TargetActionTag = Input_Light; },
			Input_Light.GetTag().ToString()
		},
	};
	for (const FCase& Case : Cases)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Case.Configure(Graph);
		TArray<FString> Messages;
		TestEqual(Case.Name, static_cast<uint8>(ValidateGraph(Graph, Messages)),
		          static_cast<uint8>(EDataValidationResult::Invalid));
		TestTrue(*FString::Printf(TEXT("%s diagnostic identifies edge"), Case.Name),
		         HasValidationMessage(Messages, Case.Fragment));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationDuplicateInputTest,
	"CadenceArc.Graph.Validation.DuplicateInputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationDuplicateInputTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	const FGameplayTag Targets[] = {Action_Light01, Action_Heavy01};
	for (const FGameplayTag& Target : Targets)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		AddTransition(Graph->Nodes[0], Input_Light, Target);
		TArray<FString> Messages;
		TestEqual(*FString::Printf(TEXT("Duplicate input to %s is invalid"), *Target.ToString()),
		          static_cast<uint8>(ValidateGraph(Graph, Messages)),
		          static_cast<uint8>(EDataValidationResult::Invalid));
		TestTrue(TEXT("Duplicate diagnostic identifies source and input"),
		         HasValidationMessage(Messages, Action_Root.GetTag().ToString()) && HasValidationMessage(
			         Messages, Input_Light.GetTag().ToString()));
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcGraphValidationDiagnosticsDeterminismTest,
	"CadenceArc.Graph.Validation.DiagnosticsDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcGraphValidationDiagnosticsDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	Graph->EntryActionTag = FGameplayTag::EmptyTag;
	Graph->MaxBufferedInputAgeSeconds = -1.0;
	Graph->Nodes[0].ActionTag = FGameplayTag::EmptyTag;
	Graph->Nodes[0].Transitions[0].InputTag = FGameplayTag::EmptyTag;
	Graph->Nodes[0].Transitions[0].TargetActionTag = FGameplayTag::EmptyTag;
	const FString Before = GraphValidationSnapshot(Graph);
	TArray<FString> FirstMessages;
	TArray<FString> SecondMessages;
	TestEqual(TEXT("First invalid validation result"), static_cast<uint8>(ValidateGraph(Graph, FirstMessages)),
	          static_cast<uint8>(EDataValidationResult::Invalid));
	TestEqual(TEXT("Second invalid validation result"), static_cast<uint8>(ValidateGraph(Graph, SecondMessages)),
	          static_cast<uint8>(EDataValidationResult::Invalid));
	TestTrue(TEXT("Multiple independent errors are collected"), FirstMessages.Num() >= 5);
	TestEqual(TEXT("Diagnostics are deterministic across repeat validation"),
	          FString::Join(FirstMessages, TEXT("\n")), FString::Join(SecondMessages, TEXT("\n")));
	TestEqual(TEXT("Validation does not mutate graph configuration"), GraphValidationSnapshot(Graph), Before);
	return !HasAnyErrors();
}
#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
