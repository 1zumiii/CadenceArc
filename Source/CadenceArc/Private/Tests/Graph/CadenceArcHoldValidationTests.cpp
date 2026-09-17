#include "Graph/CadenceArcGraph.h"
#include "Tests/CadenceArcTestSupport.h"

#include <limits>

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_DEV_AUTOMATION_TESTS

#include "UObject/CoreRedirects.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldNamingRedirectsTest,
	"CadenceArc.Graph.Hold.NamingRedirects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldNamingRedirectsTest::RunTest(const FString& Parameters)
{
	// Query redirects loaded by the plugin, rather than installing test-only redirects.
	const auto CheckRedirect = [this](ECoreRedirectFlags Type, const TCHAR* OldName, const TCHAR* NewName)
	{
		const FString OldPath = FString(TEXT("/Script/CadenceArc.")) + OldName;
		const FString NewPath = FString(TEXT("/Script/CadenceArc.")) + NewName;
		TestEqual(*OldPath, FCoreRedirects::GetRedirectedName(Type,
			FCoreRedirectObjectName(OldPath)).ToString(), FCoreRedirectObjectName(NewPath).ToString());
	};
	CheckRedirect(ECoreRedirectFlags::Type_Struct, TEXT("CadenceArcReleaseGestureConfig"), TEXT("CadenceArcHoldChargeConfig"));
	CheckRedirect(ECoreRedirectFlags::Type_Struct, TEXT("CadenceArcGestureSnapshot"), TEXT("CadenceArcHoldSnapshot"));
	CheckRedirect(ECoreRedirectFlags::Type_Struct, TEXT("CadenceArcGestureOutcome"), TEXT("CadenceArcHoldOutcome"));
	CheckRedirect(ECoreRedirectFlags::Type_Enum, TEXT("ECadenceArcGestureResult"), TEXT("ECadenceArcHoldResult"));
	CheckRedirect(ECoreRedirectFlags::Type_Enum, TEXT("ECadenceArcGestureStage"), TEXT("ECadenceArcHoldStage"));
	CheckRedirect(ECoreRedirectFlags::Type_Property, TEXT("CadenceArcNode.ReleaseGestureConfig"), TEXT("CadenceArcNode.HoldChargeConfigs"));
	CheckRedirect(ECoreRedirectFlags::Type_Property, TEXT("CadenceArcGestureSnapshot.bHasGesture"), TEXT("CadenceArcHoldSnapshot.bHasHold"));
	CheckRedirect(ECoreRedirectFlags::Type_Property, TEXT("CadenceArcHoldSnapshot.bHasGesture"), TEXT("CadenceArcHoldSnapshot.bHasHold"));

	TestEqual(TEXT("Old unqualified stage value resolves"), StaticEnum<ECadenceArcHoldStage>()->GetValueByNameString(TEXT("PendingTap")),
		static_cast<int64>(ECadenceArcHoldStage::Holding));
	TestEqual(TEXT("Old qualified stage value resolves"), StaticEnum<ECadenceArcHoldStage>()->GetValueByNameString(TEXT("ECadenceArcGestureStage::PendingTap")),
		static_cast<int64>(ECadenceArcHoldStage::Holding));
	TestEqual(TEXT("Unchanged stage value keeps its numeric value"), StaticEnum<ECadenceArcHoldStage>()->GetValueByNameString(TEXT("ECadenceArcGestureStage::Charging")),
		static_cast<int64>(ECadenceArcHoldStage::Charging));
	TestEqual(TEXT("Old None stage resolves"), StaticEnum<ECadenceArcHoldStage>()->GetValueByNameString(TEXT("ECadenceArcGestureStage::None")),
		static_cast<int64>(ECadenceArcHoldStage::None));
	TestEqual(TEXT("Old Charged stage resolves"), StaticEnum<ECadenceArcHoldStage>()->GetValueByNameString(TEXT("ECadenceArcGestureStage::Charged")),
		static_cast<int64>(ECadenceArcHoldStage::Charged));
	TestEqual(TEXT("Old input mode resolves"), StaticEnum<ECadenceArcInputMode>()->GetValueByNameString(TEXT("ECadenceArcInputMode::ReleaseGesture")),
		static_cast<int64>(ECadenceArcInputMode::HoldRelease));
	return !HasAnyErrors();
}

namespace CadenceArc::Tests
{
	static FCadenceArcTransition MakeTransition(
		const FGameplayTag& InputTag,
		const FGameplayTag& TargetActionTag,
		const ECadenceArcInputPhase InputPhase = ECadenceArcInputPhase::Released)
	{
		FCadenceArcTransition Transition;
		Transition.InputTag = InputTag;
		Transition.TargetActionTag = TargetActionTag;
		Transition.InputPhase = InputPhase;
		return Transition;
	}

	static FCadenceArcTransition MakeReleasedRange(
		const FGameplayTag& TargetActionTag,
		const double MinSeconds,
		const bool bHasMaxSeconds,
		const double MaxSeconds = 0.0)
	{
		FCadenceArcTransition Transition = MakeTransition(Input_Light, TargetActionTag);
		Transition.bUseDurationRange = true;
		Transition.DurationRange.MinHeldDurationSeconds = MinSeconds;
		Transition.DurationRange.bHasMaxHeldDuration = bHasMaxSeconds;
		Transition.DurationRange.MaxHeldDurationSecondsExclusive = MaxSeconds;
		return Transition;
	}

	static FString HoldSnapshot(const UCadenceArcGraph* Graph)
	{
		FString Snapshot;
		for (const FCadenceArcNode& Node : Graph->Nodes)
		{
			Snapshot += FString::Printf(TEXT("Node=%s;"), *Node.ActionTag.ToString());
			for (const FCadenceArcHoldChargeConfig& Config : Node.HoldChargeConfigs)
			{
				Snapshot += FString::Printf(TEXT("Config=%s,%.17g,%.17g;"), *Config.InputTag.ToString(),
					Config.ChargeStartSeconds, Config.MaxChargedHoldSeconds);
			}
			for (const FCadenceArcTransition& Transition : Node.Transitions)
			{
				Snapshot += FString::Printf(TEXT("Edge=%s,%s,%d,%d,%.17g,%d,%.17g;"),
					*Transition.InputTag.ToString(), *Transition.TargetActionTag.ToString(),
					static_cast<int32>(Transition.InputPhase), Transition.bUseDurationRange,
					Transition.DurationRange.MinHeldDurationSeconds, Transition.DurationRange.bHasMaxHeldDuration,
					Transition.DurationRange.MaxHeldDurationSecondsExclusive);
			}
		}
		return Snapshot;
	}

	static bool HasError(const TArray<FText>& Errors, const FString& Fragment)
	{
		return Errors.ContainsByPredicate([&Fragment](const FText& Error)
		{
			return Error.ToString().Contains(Fragment);
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldRangeBoundaryTest,
	"CadenceArc.Graph.Hold.DurationRangeBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldRangeBoundaryTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Infinity = std::numeric_limits<double>::infinity();

	FCadenceArcHeldDurationRange Bounded;
	Bounded.MinHeldDurationSeconds = 1.0;
	Bounded.bHasMaxHeldDuration = true;
	Bounded.MaxHeldDurationSecondsExclusive = 2.0;
	TestTrue(TEXT("Bounded range is valid"), Bounded.IsValid());
	TestTrue(TEXT("Lower boundary is included"), Bounded.Contains(1.0));
	TestTrue(TEXT("Interior duration is included"), Bounded.Contains(1.5));
	TestFalse(TEXT("Upper boundary is excluded"), Bounded.Contains(2.0));
	TestFalse(TEXT("Negative duration is rejected"), Bounded.Contains(-0.25));
	TestFalse(TEXT("NaN duration is rejected"), Bounded.Contains(NaN));
	TestFalse(TEXT("Infinite duration is rejected"), Bounded.Contains(Infinity));
	FCadenceArcHeldDurationRange InvalidUnbounded;
	InvalidUnbounded.MinHeldDurationSeconds = -1.0;
	TestFalse(TEXT("Invalid unbounded range cannot contain a duration"), InvalidUnbounded.Contains(0.0));
	FCadenceArcHeldDurationRange InvalidBounded = Bounded;
	InvalidBounded.MaxHeldDurationSecondsExclusive = InvalidBounded.MinHeldDurationSeconds;
	TestFalse(TEXT("Invalid bounded range cannot contain its boundary"), InvalidBounded.Contains(1.0));
	FCadenceArcHeldDurationRange UnboundedIgnoresStoredMaximum;
	UnboundedIgnoresStoredMaximum.MinHeldDurationSeconds = 1.0;
	UnboundedIgnoresStoredMaximum.MaxHeldDurationSecondsExclusive = NaN;
	TestTrue(TEXT("Unbounded range ignores stored maximum validity"), UnboundedIgnoresStoredMaximum.IsValid());
	TestTrue(TEXT("Unbounded range ignores stored maximum when containing duration"),
		UnboundedIgnoresStoredMaximum.Contains(1.0));

	const double InvalidMins[] = {-1.0, NaN, Infinity, -Infinity};
	for (const double Min : InvalidMins)
	{
		FCadenceArcHeldDurationRange Range;
		Range.MinHeldDurationSeconds = Min;
		TestFalse(*FString::Printf(TEXT("Unbounded minimum %g is invalid"), Min), Range.IsValid());
	}
	const double InvalidMaxes[] = {1.0, NaN, Infinity, -Infinity};
	for (const double Max : InvalidMaxes)
	{
		FCadenceArcHeldDurationRange Range = Bounded;
		Range.MaxHeldDurationSecondsExclusive = Max;
		TestFalse(*FString::Printf(TEXT("Bounded maximum %g is invalid"), Max), Range.IsValid());
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldTransitionPhaseTest,
	"CadenceArc.Graph.Hold.PhaseAndRangeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldTransitionPhaseTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	FCadenceArcNode Node;
	Node.ActionTag = Action_Root;
	Node.Transitions.Add(MakeTransition(Input_Light, Action_Light01, ECadenceArcInputPhase::Pressed));
	Node.Transitions.Add(MakeTransition(Input_Light, Action_Heavy01));
	TestTrue(TEXT("Pressed and released may share an input tag"), Node.IsValidTransition());

	// Pressed 边不能携带时长范围，Released 的禁用范围则始终表示无条件匹配。
	Node.Transitions[0].bUseDurationRange = true;
	Node.Transitions[0].DurationRange.MinHeldDurationSeconds = 1.0;
	TestFalse(TEXT("Pressed transition cannot enable a duration range"), Node.IsValidTransition());

	Node.Transitions[0].bUseDurationRange = false;
	Node.Transitions[1].DurationRange.MinHeldDurationSeconds = -100.0;
	Node.Transitions[1].DurationRange.MaxHeldDurationSecondsExclusive = std::numeric_limits<double>::quiet_NaN();
	Node.Transitions[1].DurationRange.bHasMaxHeldDuration = true;
	TestTrue(TEXT("Disabled released range is a catchall and ignores stored values"), Node.IsValidTransition());
	TestFalse(TEXT("Invalid stored range does not contain a duration"), Node.Transitions[1].DurationRange.Contains(0.0));

	Node.Transitions[1].bUseDurationRange = true;
	TestFalse(TEXT("Enabled released range must be locally valid"), Node.IsValidTransition());
	Node.Transitions[1].InputPhase = static_cast<ECadenceArcInputPhase>(255);
	TestFalse(TEXT("Unknown input phase is invalid"), Node.IsValidTransition());
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldTransitionPartitionTest,
	"CadenceArc.Graph.Hold.ReleasePartitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldTransitionPartitionTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	struct FCase
	{
		const TCHAR* Name;
		TArray<FCadenceArcTransition> Transitions;
		bool bValid;
	};
	const FCase Cases[] = {
		// 可以只有短按、只有长按、任意层数，且输入顺序不影响判定。
		{TEXT("Short only"), {MakeReleasedRange(Action_Light01, 0.0, true, 0.5)}, true},
		{TEXT("Long only"), {MakeReleasedRange(Action_Light01, 1.0, false)}, true},
		{TEXT("Unbounded catchall"), {MakeTransition(Input_Light, Action_Light01)}, true},
		{TEXT("Unsorted adjacent tiers"), {MakeReleasedRange(Action_Finisher01, 2.0, false),
			MakeReleasedRange(Action_Light01, 0.0, true, 1.0), MakeReleasedRange(Action_Heavy01, 1.0, true, 2.0)}, true},
		{TEXT("Gapped tiers"), {MakeReleasedRange(Action_Light01, 0.0, true, 1.0),
			MakeReleasedRange(Action_Heavy01, 2.0, false)}, true},
		{TEXT("Pressed duplicate"), {MakeTransition(Input_Light, Action_Light01, ECadenceArcInputPhase::Pressed),
			MakeTransition(Input_Light, Action_Heavy01, ECadenceArcInputPhase::Pressed)}, false},
		{TEXT("Overlapping different targets"), {MakeReleasedRange(Action_Light01, 0.0, true, 2.0),
			MakeReleasedRange(Action_Heavy01, 1.0, false)}, false},
		{TEXT("Overlapping same target"), {MakeReleasedRange(Action_Light01, 0.0, true, 2.0),
			MakeReleasedRange(Action_Light01, 1.0, false)}, false},
		{TEXT("Catchall conflicts with bounded tier"), {MakeTransition(Input_Light, Action_Light01),
			MakeReleasedRange(Action_Heavy01, 1.0, false)}, false},
		{TEXT("Two unbounded tiers conflict"), {MakeReleasedRange(Action_Light01, 1.0, false),
			MakeReleasedRange(Action_Heavy01, 2.0, false)}, false},
	};
	for (const FCase& Case : Cases)
	{
		FCadenceArcNode Node;
		Node.ActionTag = Action_Root;
		Node.Transitions = Case.Transitions;
		TestEqual(Case.Name, Node.IsValidTransition(), Case.bValid);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldConfigSourceScopeTest,
	"CadenceArc.Graph.Hold.ConfigSourceScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldConfigSourceScopeTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	FCadenceArcNode& Root = Graph->Nodes[0];
	Root.Transitions.Reset();
	Root.Transitions.Add(MakeReleasedRange(Action_Light01, 0.0, true, 1.0));
	Root.Transitions.Add(MakeReleasedRange(Action_Heavy01, 1.0, false));
	Root.HoldChargeConfigs.Add({Input_Light, 0.5, 3.0});

	// 配置只约束当前源节点；另一个节点可为同一输入标签使用独立的普通 Release 边。
	FCadenceArcNode& Light = Graph->Nodes[1];
	Light.Transitions.Add(MakeTransition(Input_Light, Action_Heavy01));
	TArray<FText> Errors;
	TestTrue(TEXT("Hold configuration is isolated to its source node"), Graph->ValidateGraph(Errors));
	TestEqual(TEXT("Valid source-scoped configuration has no errors"), Errors.Num(), 0);

	UCadenceArcGraph* LongOnlyGraph = MakeValidGraph();
	FCadenceArcNode& LongOnlyRoot = LongOnlyGraph->Nodes[0];
	LongOnlyRoot.Transitions.Reset();
	LongOnlyRoot.Transitions.Add(MakeReleasedRange(Action_Light01, 1.0, false));
	LongOnlyRoot.HoldChargeConfigs.Add({Input_Light, 0.5, 0.0});
	TestTrue(TEXT("Configured long-only hold needs no normal companion"), LongOnlyGraph->ValidateGraph(Errors));

	UCadenceArcGraph* MultiTierGraph = MakeValidGraph();
	FCadenceArcNode& MultiTierRoot = MultiTierGraph->Nodes[0];
	MultiTierRoot.Transitions.Reset();
	MultiTierRoot.Transitions.Add(MakeReleasedRange(Action_Light01, 0.0, true, 1.0));
	MultiTierRoot.Transitions.Add(MakeReleasedRange(Action_Heavy01, 1.0, true, 2.0));
	MultiTierRoot.Transitions.Add(MakeReleasedRange(Action_Finisher01, 2.0, false));
	MultiTierRoot.HoldChargeConfigs.Add({Input_Light, 1.5, 0.0});
	// 整体保护可在中间档之后开始；它只需要早于最高档的完整门槛。
	TestTrue(TEXT("Configured three-tier hold accepts intermediate charge start"), MultiTierGraph->ValidateGraph(Errors));

	UCadenceArcGraph* ShortOnlyGraph = MakeValidGraph();
	FCadenceArcNode& ShortOnlyRoot = ShortOnlyGraph->Nodes[0];
	ShortOnlyRoot.Transitions.Reset();
	ShortOnlyRoot.Transitions.Add(MakeReleasedRange(Action_Light01, 0.0, true, 1.0));
	ShortOnlyRoot.HoldChargeConfigs.Add({Input_Light, 0.0, 0.0});
	TestFalse(TEXT("Configured short-only hold has no full release tail"), ShortOnlyGraph->ValidateGraph(Errors));

	Root.HoldChargeConfigs.Add({Input_Light, 0.5, 3.0});
	TestFalse(TEXT("Only one configuration per source node and input is allowed"), Graph->ValidateGraph(Errors));
	TestTrue(TEXT("Duplicate configuration identifies input tag"), HasError(Errors, Input_Light.GetTag().ToString()));
	Root.HoldChargeConfigs.Pop();

	Root.Transitions[0].bUseDurationRange = false;
	TestFalse(TEXT("Configured hold requires ranges on every matching release edge"), Graph->ValidateGraph(Errors));
	Root.Transitions[0].bUseDurationRange = true;
	Root.Transitions[1].bUseDurationRange = true;
	Root.Transitions[1].DurationRange.MinHeldDurationSeconds = 0.0;
	TestFalse(TEXT("Configured hold needs a positive unbounded tail"), Graph->ValidateGraph(Errors));
	Root.Transitions[1].DurationRange.MinHeldDurationSeconds = 1.0;
	Root.HoldChargeConfigs[0].ChargeStartSeconds = 1.0;
	TestFalse(TEXT("Charge start must precede the full threshold"), Graph->ValidateGraph(Errors));
	Root.HoldChargeConfigs[0].ChargeStartSeconds = 0.5;
	Root.Transitions[1].DurationRange.MinHeldDurationSeconds = std::numeric_limits<double>::max();
	Root.HoldChargeConfigs[0].MaxChargedHoldSeconds = std::numeric_limits<double>::max();
	TestFalse(TEXT("Full threshold plus maximum hold must remain finite"), Graph->ValidateGraph(Errors));
	Root.Transitions[1].DurationRange.MinHeldDurationSeconds = 1.0;
	Root.HoldChargeConfigs[0].MaxChargedHoldSeconds = 3.0;

	const double InvalidValues[] = {-0.25, std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
	for (const double InvalidValue : InvalidValues)
	{
		Root.HoldChargeConfigs[0].ChargeStartSeconds = InvalidValue;
		TestFalse(*FString::Printf(TEXT("Invalid charge start %g is rejected"), InvalidValue),
			Graph->ValidateGraph(Errors));
		Root.HoldChargeConfigs[0].ChargeStartSeconds = 0.5;
		Root.HoldChargeConfigs[0].MaxChargedHoldSeconds = InvalidValue;
		TestFalse(*FString::Printf(TEXT("Invalid maximum hold %g is rejected"), InvalidValue),
			Graph->ValidateGraph(Errors));
		Root.HoldChargeConfigs[0].MaxChargedHoldSeconds = 3.0;
	}

	Root.Transitions[1].DurationRange.bHasMaxHeldDuration = true;
	Root.Transitions[1].DurationRange.MaxHeldDurationSecondsExclusive = 2.0;
	TestFalse(TEXT("Configured hold requires an unbounded release tail"), Graph->ValidateGraph(Errors));
	Root.Transitions[1].DurationRange.bHasMaxHeldDuration = false;
	Root.HoldChargeConfigs.Add({Input_Heavy, 0.0, 0.0});
	TestFalse(TEXT("Configuration without matching released edges is invalid"), Graph->ValidateGraph(Errors));
	return !HasAnyErrors();
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldEditorParityTest,
	"CadenceArc.Graph.Hold.EditorParity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldEditorParityTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	TArray<FText> RuntimeErrors;
	FDataValidationContext Context;
	TestTrue(TEXT("Runtime validator accepts valid graph"), Graph->ValidateGraph(RuntimeErrors));
	TestEqual(TEXT("Editor validator accepts valid graph"), static_cast<uint8>(Graph->IsDataValid(Context)),
		static_cast<uint8>(EDataValidationResult::Valid));

	Graph->Nodes[0].Transitions[0].TargetActionTag = Input_Light;
	RuntimeErrors.Reset();
	FDataValidationContext InvalidContext;
	TestFalse(TEXT("Runtime validator rejects missing target"), Graph->ValidateGraph(RuntimeErrors));
	TestEqual(TEXT("Editor validator rejects missing target"), static_cast<uint8>(Graph->IsDataValid(InvalidContext)),
		static_cast<uint8>(EDataValidationResult::Invalid));
	return !HasAnyErrors();
}
#endif // WITH_EDITOR

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCadenceArcHoldValidationDeterminismTest,
	"CadenceArc.Graph.Hold.DiagnosticsDeterminism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCadenceArcHoldValidationDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace CadenceArc::Tests;
	UCadenceArcGraph* Graph = MakeValidGraph();
	FCadenceArcNode& Root = Graph->Nodes[0];
	Root.Transitions.Reset();
	Root.Transitions.Add(MakeReleasedRange(Action_Light01, 0.0, true, 2.0));
	Root.Transitions.Add(MakeReleasedRange(Action_Heavy01, 1.0, false));
	Root.Transitions.Add(MakeTransition(Input_Heavy, Input_Light));
	Root.HoldChargeConfigs.Add({Input_Light, 1.5, -1.0});
	const FString Before = HoldSnapshot(Graph);

	TArray<FText> FirstErrors = {FText::FromString(TEXT("stale error"))};
	TArray<FText> SecondErrors;
	TestFalse(TEXT("First validation reports invalid graph"), Graph->ValidateGraph(FirstErrors));
	TestFalse(TEXT("Second validation reports invalid graph"), Graph->ValidateGraph(SecondErrors));
	TestFalse(TEXT("Validator clears supplied errors"), HasError(FirstErrors, TEXT("stale error")));
	TestTrue(TEXT("Independent errors are collected"), FirstErrors.Num() >= 3);
	TestTrue(TEXT("Graph-wide validation identifies a missing target"), HasError(FirstErrors, TEXT("does not exist")));
	TestEqual(TEXT("Diagnostic order is deterministic"), FString::JoinBy(FirstErrors, TEXT("\n"),
		[](const FText& Error) { return Error.ToString(); }), FString::JoinBy(SecondErrors, TEXT("\n"),
		[](const FText& Error) { return Error.ToString(); }));
	TestEqual(TEXT("Validation does not mutate graph configuration"), HoldSnapshot(Graph), Before);
	return !HasAnyErrors();
}

#endif // WITH_DEV_AUTOMATION_TESTS
