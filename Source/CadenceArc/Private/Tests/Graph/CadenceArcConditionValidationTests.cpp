// Phase 8：条件、优先级和停顿区间的图校验测试（D4）。
//
// 被测契约：
// - 同源、同 Tag、同阶段、时长重叠的两条边，只有“优先级相同 + 停顿区间重叠 + 条件可能同时成立”才报错；
// - 条件可能同时成立 = 两条边 Required 的并集里没有任何 Tag 命中 Blocked 的并集（按层级，方向是 Required 对 Blocked）；
// - 单条边 Required 与 Blocked 矛盾、停顿区间无效，都报错；
// - 蓄力配置下，多条无上限的松手分支可以并存，但必须共用同一个正数下限；
// - 从入口到不了的节点只给警告，图仍然合法，警告按节点数组顺序输出。

#if WITH_EDITOR
#include "Graph/CadenceArcGraph.h"
#include "Tests/CadenceArcTestSupport.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	static FCadenceArcTransition CondValidationEdge(
		const FGameplayTag& InputTag, const FGameplayTag& Target, const int32 Priority = 0,
		const FGameplayTagContainer& Required = {}, const FGameplayTagContainer& Blocked = {})
	{
		FCadenceArcTransition Edge;
		Edge.InputTag = InputTag;
		Edge.TargetActionTag = Target;
		Edge.Priority = Priority;
		Edge.RequiredContextTags = Required;
		Edge.BlockedContextTags = Blocked;
		return Edge;
	}

	static FCadenceArcTransition CondValidationPause(
		FCadenceArcTransition Edge, const double Min, const bool bHasMax = false, const double Max = 0.0)
	{
		Edge.bUsePauseRange = true;
		Edge.PauseRange.MinHeldDurationSeconds = Min;
		Edge.PauseRange.bHasMaxHeldDuration = bHasMax;
		Edge.PauseRange.MaxHeldDurationSecondsExclusive = Max;
		return Edge;
	}

	// Root 带给定的出边，Heavy01、Heavy02 是两个目标
	static TArray<FString> CondValidationErrors(const TArray<FCadenceArcTransition>& RootEdges,
	                                            TArray<FString>* OutWarnings = nullptr)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		AddNode(Graph, Action_Root).Transitions = RootEdges;
		AddNode(Graph, Action_Heavy01);
		AddNode(Graph, Action_Heavy02);
		TArray<FText> Errors;
		TArray<FText> Warnings;
		Graph->ValidateGraph(Errors, &Warnings);
		TArray<FString> Result;
		for (const FText& Error : Errors)
		{
			Result.Add(Error.ToString());
		}
		if (OutWarnings)
		{
			for (const FText& Warning : Warnings)
			{
				OutWarnings->Add(Warning.ToString());
			}
		}
		return Result;
	}

	static bool CondHasOverlapError(const TArray<FString>& Errors)
	{
		return Errors.ContainsByPredicate([](const FString& Error) { return Error.Contains(TEXT("overlap at equal priority")); });
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionValidationOverlapTest,
		"CadenceArc.Graph.Validation.ConditionOverlap",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionValidationOverlapTest::RunTest(const FString& Parameters)
	{
		const FGameplayTagContainer Forward(Context_Forward);
		const FGameplayTagContainer Air(Context_Air);
		const FGameplayTagContainer AirJump(Context_AirJump);
		struct FCase
		{
			const TCHAR* What;
			FCadenceArcTransition First;
			FCadenceArcTransition Second;
			bool bExpectError;
		};
		const FCase Cases[] = {
			{TEXT("Same priority without conditions"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01), CondValidationEdge(Input_Heavy, Action_Heavy02), true},
			{TEXT("Different priorities"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01, 1, Forward), CondValidationEdge(Input_Heavy, Action_Heavy02), false},
			{TEXT("Conditional and unconditional at the same priority can both hold"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01, 0, Forward), CondValidationEdge(Input_Heavy, Action_Heavy02), true},
			{TEXT("Required versus blocked Forward are mutually exclusive"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01, 0, Forward),
			 CondValidationEdge(Input_Heavy, Action_Heavy02, 0, {}, Forward), false},
			{TEXT("Requiring a child while another blocks its parent is exclusive"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01, 0, AirJump),
			 CondValidationEdge(Input_Heavy, Action_Heavy02, 0, {}, Air), false},
			{TEXT("Requiring a parent while another blocks a child can both hold"),
			 CondValidationEdge(Input_Heavy, Action_Heavy01, 0, Air),
			 CondValidationEdge(Input_Heavy, Action_Heavy02, 0, {}, AirJump), true},
			{TEXT("Complementary pause ranges are exclusive"),
			 CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy01), 0.0, true, 0.25),
			 CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy02), 0.25), false},
			{TEXT("Overlapping pause ranges can both hold"),
			 CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy01), 0.0, true, 0.3),
			 CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy02), 0.25), true},
			{TEXT("A pause edge overlaps an edge without a pause range"),
			 CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy01), 0.25),
			 CondValidationEdge(Input_Heavy, Action_Heavy02), true},
		};
		for (const FCase& Case : Cases)
		{
			const TArray<FString> Errors = CondValidationErrors({Case.First, Case.Second});
			TestEqual(*FString::Printf(TEXT("%s: overlap error"), Case.What), CondHasOverlapError(Errors), Case.bExpectError);
			if (!Case.bExpectError)
			{
				TestEqual(*FString::Printf(TEXT("%s: no errors at all"), Case.What), Errors.Num(), 0);
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionValidationEdgeTest,
		"CadenceArc.Graph.Validation.ConditionEdgeRules",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionValidationEdgeTest::RunTest(const FString& Parameters)
	{
		const auto HasError = [](const TArray<FString>& Errors, const TCHAR* Fragment)
		{
			return Errors.ContainsByPredicate([Fragment](const FString& Error) { return Error.Contains(Fragment); });
		};
		TestTrue(TEXT("Required and blocked on the same edge contradict"),
		         HasError(CondValidationErrors({
			                  CondValidationEdge(Input_Heavy, Action_Heavy01, 0, FGameplayTagContainer(Context_AirJump),
			                                     FGameplayTagContainer(Context_Air))
		                  }), TEXT("contradictory required and blocked context tags")));
		TestTrue(TEXT("Negative pause minimum is invalid"),
		         HasError(CondValidationErrors({CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy01), -0.1)}),
		                  TEXT("invalid pause range")));
		TestTrue(TEXT("Empty pause range is invalid"),
		         HasError(CondValidationErrors({
			                  CondValidationPause(CondValidationEdge(Input_Heavy, Action_Heavy01), 0.3, true, 0.3)
		                  }), TEXT("invalid pause range")));

		// 蓄力配置：两个无上限分支共用下限 0.5 合法；下限不同则拒绝
		const auto ChargeErrors = [](const double SecondMinimum)
		{
			UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
			Graph->EntryActionTag = Action_Root;
			FCadenceArcNode& Root = AddNode(Graph, Action_Root);
			for (const TPair<int32, double> Tier : {TPair<int32, double>(1, 0.5), TPair<int32, double>(0, SecondMinimum)})
			{
				FCadenceArcTransition Edge = CondValidationEdge(
					Input_Heavy, Tier.Key == 1 ? Action_Heavy01 : Action_Heavy02, Tier.Key,
					Tier.Key == 1 ? FGameplayTagContainer(Context_Forward) : FGameplayTagContainer());
				Edge.InputPhase = ECadenceArcInputPhase::Released;
				Edge.bUseDurationRange = true;
				Edge.DurationRange.MinHeldDurationSeconds = Tier.Value;
				Root.Transitions.Add(Edge);
			}
			FCadenceArcHoldChargeConfig& Config = Root.HoldChargeConfigs.AddDefaulted_GetRef();
			Config.InputTag = Input_Heavy;
			Config.ChargeStartSeconds = 0.2;
			Config.MaxChargedHoldSeconds = 0.5;
			AddNode(Graph, Action_Heavy01);
			AddNode(Graph, Action_Heavy02);
			TArray<FText> Errors;
			Graph->ValidateGraph(Errors);
			return Errors.Num();
		};
		TestEqual(TEXT("Conditional charged tiers sharing one minimum are valid"), ChargeErrors(0.5), 0);
		TestEqual(TEXT("Charged tiers with different minimums are rejected"), ChargeErrors(0.6), 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcConditionValidationReachabilityTest,
		"CadenceArc.Graph.Validation.Reachability",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcConditionValidationReachabilityTest::RunTest(const FString& Parameters)
	{
		// Root → Heavy01；Heavy02 和 Finisher01 到不了。Heavy02 → Finisher01 这条边不让 Finisher01 变得可达。
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(4);
		AddTransition(AddNode(Graph, Action_Root), Input_Heavy, Action_Heavy01);
		AddNode(Graph, Action_Heavy01);
		AddTransition(AddNode(Graph, Action_Heavy02), Input_Heavy, Action_Finisher01);
		AddNode(Graph, Action_Finisher01);
		TArray<FText> Errors;
		TArray<FText> Warnings;
		TestTrue(TEXT("Unreachable nodes keep the graph valid"), Graph->ValidateGraph(Errors, &Warnings));
		TestEqual(TEXT("No errors"), Errors.Num(), 0);
		if (TestEqual(TEXT("One warning per unreachable node"), Warnings.Num(), 2))
		{
			TestTrue(TEXT("Warnings follow the node order"),
			         Warnings[0].ToString().Contains(TEXT("Heavy02' at index 2"))
			         && Warnings[1].ToString().Contains(TEXT("Finisher01' at index 3")));
		}
		// 不传警告输出时不做可达性分析，也不影响结果
		TestTrue(TEXT("Warnings are optional"), Graph->ValidateGraph(Errors));
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
#endif // WITH_EDITOR
