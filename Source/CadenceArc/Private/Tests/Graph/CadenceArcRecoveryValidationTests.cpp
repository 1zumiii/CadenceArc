#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "Misc/DataValidation.h"
#include "Tests/CadenceArcTestSupport.h"
#include <limits>

namespace CadenceArc::Tests::RecoveryValidation
{
	static FString Messages(const TArray<FText>& Values)
	{
		TArray<FString> Strings;
		for (const FText& Value : Values)
		{
			Strings.Add(Value.ToString());
		}
		return FString::Join(Strings, TEXT("\n"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcRecoveryValidationDefaultsTest,
	"CadenceArc.Graph.Validation.Recovery.DefaultsAndResetValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcRecoveryValidationDefaultsTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	TestEqual(TEXT("Reset defaults to disabled"), Graph->ComboResetSeconds, 0.0);
	TestFalse(TEXT("Fallback defaults to disabled"), Graph->bFallbackToEntryOnNoMatch);
	const double Values[] = {0.0, 0.5, -0.5, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Values); ++Index)
	{
		Graph->ComboResetSeconds = Values[Index];
		TArray<FText> Errors;
		TArray<FText> Warnings;
		TestEqual(*FString::Printf(TEXT("Reset case %d validity"), Index), Graph->ValidateGraph(Errors, &Warnings), Index < 2);
		TestEqual(TEXT("Reset diagnostic count"), Errors.Num(), Index < 2 ? 0 : 1);
		if (Index >= 2)
		{
			TestTrue(TEXT("Diagnostic names reset property"), RecoveryValidation::Messages(Errors).Contains(TEXT("ComboResetSeconds")));
		}
		TestEqual(TEXT("No spurious warnings"), Warnings.Num(), 0);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcRecoveryValidationPauseRangeTest,
	"CadenceArc.Graph.Validation.Recovery.PauseRangeBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcRecoveryValidationPauseRangeTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	struct FCase
	{
		const TCHAR* Name;
		double Reset;
		double Min;
		double Max;
		bool bHasMax;
		bool bEntry;
		bool bEnabled;
		int32 ErrorCount;
		int32 WarningCount;
	};
	const FCase Cases[] = {
		{TEXT("Minimum equal reset"), 1.0, 1.0, 2.0, true, false, true, 1, 0},
		{TEXT("Minimum above reset"), 1.0, 1.5, 2.0, true, false, true, 1, 0},
		{TEXT("Maximum equal reset"), 1.0, 0.25, 1.0, true, false, true, 0, 0},
		{TEXT("Maximum below reset"), 1.0, 0.25, 0.5, true, false, true, 0, 0},
		{TEXT("Maximum above reset"), 1.0, 0.25, 2.0, true, false, true, 0, 1},
		{TEXT("Unbounded maximum"), 1.0, 0.25, 0.0, false, false, true, 0, 1},
		{TEXT("Entry minimum exempt"), 1.0, 1.0, 2.0, true, true, true, 0, 0},
		{TEXT("Entry maximum exempt"), 1.0, 0.25, 2.0, true, true, true, 0, 0},
		{TEXT("Reset disabled"), 0.0, 1.0, 2.0, true, false, true, 0, 0},
		{TEXT("Pause range disabled"), 1.0, 1.0, 2.0, true, false, false, 0, 0},
		{TEXT("Invalid pause range keeps node error only"), 1.0, 2.0, 1.0, true, false, true, 1, 0},
	};
	for (const FCase& Case : Cases)
	{
		UCadenceArcGraph* Graph = MakeValidGraph();
		Graph->ComboResetSeconds = Case.Reset;
		FCadenceArcTransition& Edge = Graph->Nodes[Case.bEntry ? 0 : 1].Transitions[0];
		Edge.bUsePauseRange = Case.bEnabled;
		Edge.PauseRange.MinHeldDurationSeconds = Case.Min;
		Edge.PauseRange.MaxHeldDurationSecondsExclusive = Case.Max;
		Edge.PauseRange.bHasMaxHeldDuration = Case.bHasMax;
		const TArray<FCadenceArcNode> Before = Graph->Nodes;
		TArray<FText> Errors;
		TArray<FText> Warnings;
		TestEqual(Case.Name, Graph->ValidateGraph(Errors, &Warnings), Case.ErrorCount == 0);
		TestEqual(*FString::Printf(TEXT("%s errors"), Case.Name), Errors.Num(), Case.ErrorCount);
		TestEqual(*FString::Printf(TEXT("%s warnings"), Case.Name), Warnings.Num(), Case.WarningCount);
		const FString FirstErrors = RecoveryValidation::Messages(Errors);
		const FString FirstWarnings = RecoveryValidation::Messages(Warnings);
		if (Case.WarningCount > 0)
		{
			TestTrue(TEXT("Warning reports effective half-open range"), FirstWarnings.Contains(TEXT("[0.25, 1)")));
			TestTrue(TEXT("Warning identifies edge and node"), FirstWarnings.Contains(TEXT("Transition at index 0"))
				&& FirstWarnings.Contains(Action_Light01.GetTag().ToString()));
		}
		Graph->ValidateGraph(Errors, &Warnings);
		TestEqual(TEXT("Repeated errors stable and cleared"), RecoveryValidation::Messages(Errors), FirstErrors);
		TestEqual(TEXT("Repeated warnings stable and cleared"), RecoveryValidation::Messages(Warnings), FirstWarnings);
		TestTrue(TEXT("Validation preserves all nodes and ranges"), Graph->Nodes == Before);
		TestEqual(TEXT("Validation preserves reset"), Graph->ComboResetSeconds, Case.Reset);
		TestFalse(TEXT("Validation preserves fallback"), Graph->bFallbackToEntryOnNoMatch);
		TestEqual(TEXT("Warnings optional without changing validity"), Graph->ValidateGraph(Errors), Case.ErrorCount == 0);
		FDataValidationContext Context;
		TestEqual(TEXT("Editor uses shared validation result"), static_cast<uint8>(Graph->IsDataValid(Context)),
			static_cast<uint8>(Case.ErrorCount == 0 ? EDataValidationResult::Valid : EDataValidationResult::Invalid));
		TestEqual(TEXT("Editor relays all diagnostics"), Context.GetIssues().Num(), Case.ErrorCount + Case.WarningCount);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcRecoveryValidationTopologyTest,
	"CadenceArc.Graph.Validation.Recovery.TerminalAndDisconnected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcRecoveryValidationTopologyTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	Graph->bFallbackToEntryOnNoMatch = true;
	TArray<FText> Errors;
	TArray<FText> Warnings;
	TestTrue(TEXT("Fallback accepts reachable terminal nodes"), Graph->ValidateGraph(Errors, &Warnings));
	TestEqual(TEXT("Reachable terminal nodes do not warn"), Warnings.Num(), 0);
	// 删除进入 Finisher01 的唯一边；回退入口不会令断开的节点变成拓扑可达。
	Graph->Nodes[1].Transitions.RemoveAt(1);
	TestTrue(TEXT("Disconnected node remains warning only"), Graph->ValidateGraph(Errors, &Warnings));
	TestEqual(TEXT("Disconnected node still warns with fallback"), Warnings.Num(), 1);
	TestTrue(TEXT("Warning identifies disconnected node"), RecoveryValidation::Messages(Warnings).Contains(
		TEXT("Node 'CadenceArc.Automation.Action.Finisher01' at index 5 is unreachable")));
	TestTrue(TEXT("Validation preserves enabled fallback"), Graph->bFallbackToEntryOnNoMatch);
	return !HasAnyErrors();
}

#endif // WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
