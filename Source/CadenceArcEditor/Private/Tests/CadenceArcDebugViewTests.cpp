// BuildDebugView 的确定性测试。用真实 Resolver 走握手流程，布局由同一张图构建，不依赖 PIE 或 Slate。
//
// 被测契约（Phase 7 / 7A 第 7 步）：
// - 未初始化时返回默认视图：所有索引为 INDEX_NONE，不读布局。
// - CommittedNodeIndex 是 CurrentActionTag 在 Layout 中的索引；只有 Started 成功才移动它。
// - 候选目标与候选边只在 AwaitingStart 时出现；Started／Rejected／Completed／Reset 之后都清空。
// - 候选边按 源→目标＋InputTag 匹配，恰好一条才给出，多条时不猜；平行边靠 InputTag 区分。
// - Tag 不在 Layout 中时得到 INDEX_NONE，而不是越界或伪造的索引。
// - 窗口、缓冲 Tag、按住快照原样复制。
// - BuildDebugView 只读：前后 Resolver 可观察状态不变，重复调用结果一致。

#if WITH_DEV_AUTOMATION_TESTS

#include "ViewModel/CadenceArcDebugView.h"

#include "Graph/CadenceArcGraph.h"
#include "Layout/CadenceArcGraphLayout.h"
#include "Misc/AutomationTest.h"
#include "Tests/CadenceArcAutomationTags.h"
#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Editor::Tests
{
	// 与布局测试一样：Editor 模块不能定义原生 Tag，测试 Tag 由 Runtime 模块在第一次请求时注册。
	static FGameplayTag DebugViewTag(const TCHAR* Name)
	{
		return CadenceArc::Tests::AutomationTag(Name); // 第一次请求时由 Runtime 模块注册
	}

	static FGameplayTag View_Root() { return DebugViewTag(TEXT("CadenceArc.Automation.Action.Root")); }
	static FGameplayTag View_Light01() { return DebugViewTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	static FGameplayTag View_Light02() { return DebugViewTag(TEXT("CadenceArc.Automation.Action.Light02")); }
	static FGameplayTag View_InputLight() { return DebugViewTag(TEXT("CadenceArc.Automation.Input.Light")); }
	static FGameplayTag View_InputHeavy() { return DebugViewTag(TEXT("CadenceArc.Automation.Input.Heavy")); }

	// 节点数组故意不以入口开头，确保断言的是 Layout 索引而不是"入口 = 0"的巧合。
	// 布局索引：Light01=0、Light02=1、Root=2。
	// 边索引：0 = Light01 -Light(Pressed)-> Light02
	//         1 = Root -Light(Pressed)-> Light01
	//         2 = Root -Heavy(Released)-> Light01   与边 1 是平行边，只能靠 InputTag 区分
	constexpr int32 ViewNode_Light01 = 0;
	constexpr int32 ViewNode_Light02 = 1;
	constexpr int32 ViewNode_Root = 2;
	constexpr int32 ViewEdge_RootLight = 1;
	constexpr int32 ViewEdge_RootHeavy = 2;

	static void AddViewTransition(
		FCadenceArcNode& Node, const FGameplayTag& Input, const FGameplayTag& Target,
		const ECadenceArcInputPhase Phase = ECadenceArcInputPhase::Pressed)
	{
		FCadenceArcTransition& Transition = Node.Transitions.AddDefaulted_GetRef();
		Transition.InputTag = Input;
		Transition.TargetActionTag = Target;
		Transition.InputPhase = Phase;
	}

	static UCadenceArcGraph* MakeViewGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = View_Root();
		Graph->Nodes.SetNum(3);
		Graph->Nodes[ViewNode_Light01].ActionTag = View_Light01();
		AddViewTransition(Graph->Nodes[ViewNode_Light01], View_InputLight(), View_Light02());
		Graph->Nodes[ViewNode_Light02].ActionTag = View_Light02();
		Graph->Nodes[ViewNode_Root].ActionTag = View_Root();
		AddViewTransition(Graph->Nodes[ViewNode_Root], View_InputLight(), View_Light01());
		AddViewTransition(Graph->Nodes[ViewNode_Root], View_InputHeavy(), View_Light01(),
		                  ECadenceArcInputPhase::Released);
		return Graph;
	}

	struct FViewFixture
	{
		UCadenceArcGraph* Graph = nullptr;
		UCadenceArcResolver* Resolver = nullptr;
		FCadenceArcGraphLayout Layout;
	};

	static bool MakeViewFixture(FAutomationTestBase& Test, FViewFixture& Out)
	{
		Out.Graph = MakeViewGraph();
		Out.Resolver = NewObject<UCadenceArcResolver>();
		Out.Layout = BuildGraphLayout(*Out.Graph);
		const bool bInitialized = Out.Resolver->Initialize(Out.Graph) == ECadenceArcResolverInitResult::Success;
		Test.TestTrue(TEXT("Fixture graph initializes"), bInitialized);
		// 下面的索引常量依赖这个形状，形状变了先在这里报错，而不是在后面报一串看不懂的索引差异
		const bool bShape = Test.TestEqual(TEXT("Fixture node count"), Out.Layout.Nodes.Num(), 3)
			& Test.TestEqual(TEXT("Fixture edge count"), Out.Layout.Edges.Num(), 3);
		return bInitialized && bShape;
	}

	static FCadenceArcInputEvent MakeViewPress(const FGameplayTag& Input, const double Timestamp)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = Input;
		Event.TimestampSeconds = Timestamp;
		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		return Event;
	}

	static FCadenceArcInputEvent MakeViewRelease(const FGameplayTag& Input, const double Timestamp, const double Held)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = Input;
		Event.TimestampSeconds = Timestamp;
		Event.InputPhase = ECadenceArcInputPhase::Released;
		Event.HeldDurationSeconds = Held;
		return Event;
	}

	static FCadenceArcInputToken MakeViewToken()
	{
		FCadenceArcInputToken Token;
		Token.SourceSession = FGuid(7, 0x0A0B0C0D, 0x11223344, 0x55667788);
		Token.PressId = 1;
		return Token;
	}

	// 提交一次 Pressed 输入并要求产生请求，返回该请求
	static bool RequestPress(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver, const FGameplayTag& Input,
		FCadenceArcActionRequest& OutRequest)
	{
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(MakeViewPress(Input, 1.0));
		OutRequest = Outcome.GetActionRequest();
		return Test.TestTrue(TEXT("Press produces a request"),
		                     Outcome.GetCategory() == ECadenceArcResolutionCategory::RequestProduced);
	}

	static bool ExpectStateIs(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcDebugView& View,
		const ECadenceArcResolverState Expected)
	{
		return Test.TestTrue(*FString::Printf(TEXT("%s: state is %s (was %s)"), What,
		                                      *UEnum::GetValueAsString(Expected),
		                                      *UEnum::GetValueAsString(View.ResolverState)),
		                     View.ResolverState == Expected);
	}

	static bool ExpectIndices(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcDebugView& View,
		const int32 ExpectedCommitted, const int32 ExpectedCandidateTarget, const int32 ExpectedCandidateEdge)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s: committed node"), What),
		                              View.CommittedNodeIndex, ExpectedCommitted);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s: candidate target"), What),
		                          View.CandidateTargetNodeIndex, ExpectedCandidateTarget);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s: candidate edge"), What),
		                          View.CandidateEdgeIndex, ExpectedCandidateEdge);
		return bPassed;
	}

	// Resolver 的全部公开可观察状态，用来证明 BuildDebugView 不改动它
	struct FObservedResolver
	{
		ECadenceArcResolverState State;
		FGameplayTag Current;
		FCadenceArcActionRequest Request;
		bool bWindowOpen;
		FGameplayTag Buffered;
		FCadenceArcHoldSnapshot Hold;

		explicit FObservedResolver(const UCadenceArcResolver& Resolver)
			: State(Resolver.GetState())
			, Current(Resolver.GetCurrentActionTag())
			, Request(Resolver.GetOutstandingRequest())
			, bWindowOpen(Resolver.IsBufferWindowOpen())
			, Buffered(Resolver.GetBufferedInputTag())
			, Hold(Resolver.GetInputHoldSnapshot())
		{
		}

		bool operator==(const FObservedResolver& Other) const
		{
			return State == Other.State && Current == Other.Current
				&& Request.RequestId == Other.Request.RequestId && Request.InputTag == Other.Request.InputTag
				&& Request.SourceActionTag == Other.Request.SourceActionTag
				&& Request.TargetActionTag == Other.Request.TargetActionTag
				&& bWindowOpen == Other.bWindowOpen && Buffered == Other.Buffered
				&& Hold.bHasHold == Other.Hold.bHasHold && Hold.Token == Other.Hold.Token
				&& Hold.Stage == Other.Hold.Stage
				&& Hold.LastObservedTimestampSeconds == Other.Hold.LastObservedTimestampSeconds;
		}
	};

	// 构建两次：Resolver 前后不变，两次结果一致
	static FCadenceArcDebugView BuildReadOnly(
		FAutomationTestBase& Test, const TCHAR* What, const UCadenceArcResolver& Resolver,
		const FCadenceArcGraphLayout& Layout)
	{
		const FObservedResolver Before(Resolver);
		const FCadenceArcDebugView First = BuildDebugView(Resolver, Layout);
		const FCadenceArcDebugView Second = BuildDebugView(Resolver, Layout);
		Test.TestTrue(*FString::Printf(TEXT("%s: resolver unchanged by BuildDebugView"), What),
		              Before == FObservedResolver(Resolver));
		Test.TestTrue(*FString::Printf(TEXT("%s: repeated build is identical"), What),
		              First.ResolverState == Second.ResolverState
		              && First.CommittedNodeIndex == Second.CommittedNodeIndex
		              && First.EffectiveSourceActionTag == Second.EffectiveSourceActionTag
		              && First.ComboResetRemainingSeconds == Second.ComboResetRemainingSeconds
		              && First.CandidateTargetNodeIndex == Second.CandidateTargetNodeIndex
		              && First.CandidateEdgeIndex == Second.CandidateEdgeIndex
		              && First.OutstandingRequest.RequestId == Second.OutstandingRequest.RequestId
		              && First.bBufferWindowOpen == Second.bBufferWindowOpen
		              && First.BufferedInputTag == Second.BufferedInputTag
		              && First.HoldSnapshot.bHasHold == Second.HoldSnapshot.bHasHold);
		return First;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewRecoveryTest,
		"CadenceArc.Editor.DebugView.RecoverySourceAndCountdown",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewRecoveryTest::RunTest(const FString& Parameters)
	{
		UCadenceArcGraph* Graph = MakeViewGraph();
		Graph->ComboResetSeconds = 1.0;
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		if (!TestTrue(TEXT("Recovery graph initializes"), Resolver->Initialize(Graph) == ECadenceArcResolverInitResult::Success))
		{
			return false;
		}
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Graph);
		FCadenceArcActionRequest Request;
		if (!RequestPress(*this, Resolver, View_InputLight(), Request))
		{
			return false;
		}
		Resolver->NotifyActionStarted(Request.RequestId);
		Resolver->NotifyActionCompleted(Request.RequestId, 2.0);
		Resolver->AdvanceInputTime(2.25);
		FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Before recovery"), *Resolver, Layout);
		TestEqual(TEXT("Countdown uses last host time"), View.ComboResetRemainingSeconds, 0.75);
		TestTrue(TEXT("Source remains committed before deadline"), View.EffectiveSourceActionTag == View_Light01());
		Resolver->AdvanceInputTime(3.0);
		View = BuildReadOnly(*this, TEXT("At recovery"), *Resolver, Layout);
		TestEqual(TEXT("Deadline has expired"), View.ComboResetRemainingSeconds, 0.0);
		TestTrue(TEXT("Next resolution starts at entry"), View.EffectiveSourceActionTag == View_Root());
		ExpectIndices(*this, TEXT("Recovery preserves committed highlight"), View, ViewNode_Light01, INDEX_NONE, INDEX_NONE);
		const FCadenceArcSubmitOutcome Outcome = Resolver->SubmitInput(MakeViewPress(View_InputLight(), 3.0));
		TestTrue(TEXT("Entry request produced"), Outcome.GetCategory() == ECadenceArcResolutionCategory::RequestProduced);
		View = BuildReadOnly(*this, TEXT("Recovery awaiting start"), *Resolver, Layout);
		ExpectIndices(*this, TEXT("Candidate uses entry edge"), View, ViewNode_Light01, ViewNode_Light01, ViewEdge_RootLight);

		// 另一实例在到期之前授予 Hold：跨过期限后仍显示冻结来源，不能显示入口倒计时。
		UCadenceArcGraph* HoldGraph = MakeViewGraph();
		HoldGraph->ComboResetSeconds = 1.0;
		AddViewTransition(HoldGraph->Nodes[ViewNode_Light01], View_InputHeavy(), View_Light02(),
			ECadenceArcInputPhase::Released);
		UCadenceArcResolver* HoldResolver = NewObject<UCadenceArcResolver>();
		if (!TestTrue(TEXT("Hold recovery graph initializes"), HoldResolver->Initialize(HoldGraph) == ECadenceArcResolverInitResult::Success)
			|| !RequestPress(*this, HoldResolver, View_InputLight(), Request))
		{
			return false;
		}
		HoldResolver->NotifyActionStarted(Request.RequestId);
		HoldResolver->NotifyActionCompleted(Request.RequestId, 2.0);
		TestTrue(TEXT("Hold granted before deadline"),
			HoldResolver->BeginInputHold(MakeViewToken(), MakeViewPress(View_InputHeavy(), 2.25)).GetResult()
			== ECadenceArcHoldResult::Granted);
		HoldResolver->AdvanceInputTime(3.0);
		View = BuildReadOnly(*this, TEXT("Hold freezes recovery source"), *HoldResolver, BuildGraphLayout(*HoldGraph));
		TestTrue(TEXT("Hold still shows granted source"), View.EffectiveSourceActionTag == View_Light01());
		TestEqual(TEXT("Hold suppresses countdown"), View.ComboResetRemainingSeconds, -1.0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewUninitializedTest,
		"CadenceArc.Editor.DebugView.UninitializedIsDefault",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewUninitializedTest::RunTest(const FString& Parameters)
	{
		// 布局非空也不能从中读出任何高亮
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*MakeViewGraph());
		const UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Uninitialized"), *Resolver, Layout);

		ExpectStateIs(*this, TEXT("Uninitialized"), View, ECadenceArcResolverState::Uninitialized);
		ExpectIndices(*this, TEXT("Uninitialized"), View, INDEX_NONE, INDEX_NONE, INDEX_NONE);
		TestEqual(TEXT("Uninitialized: no request"), View.OutstandingRequest.RequestId, int64{0});
		TestFalse(TEXT("Uninitialized: window closed"), View.bBufferWindowOpen);
		TestFalse(TEXT("Uninitialized: no buffered tag"), View.BufferedInputTag.IsValid());
		TestFalse(TEXT("Uninitialized: no hold"), View.HoldSnapshot.bHasHold);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewReadyTest,
		"CadenceArc.Editor.DebugView.ReadyShowsCommittedEntry",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewReadyTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		if (!MakeViewFixture(*this, Fixture))
		{
			return false;
		}
		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Ready"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("Ready"), View, ECadenceArcResolverState::Ready);
		ExpectIndices(*this, TEXT("Ready"), View, ViewNode_Root, INDEX_NONE, INDEX_NONE);
		TestEqual(TEXT("Ready: no request"), View.OutstandingRequest.RequestId, int64{0});
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewAwaitingStartTest,
		"CadenceArc.Editor.DebugView.AwaitingStartShowsCandidate",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewAwaitingStartTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}

		const FCadenceArcDebugView View =
			BuildReadOnly(*this, TEXT("AwaitingStart"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("AwaitingStart"), View, ECadenceArcResolverState::AwaitingStart);
		// 候选还没被 Started 接受：已提交节点仍是源节点
		ExpectIndices(*this, TEXT("AwaitingStart"), View, ViewNode_Root, ViewNode_Light01, ViewEdge_RootLight);
		TestEqual(TEXT("AwaitingStart: request id copied"), View.OutstandingRequest.RequestId, Request.RequestId);
		TestTrue(TEXT("AwaitingStart: request tags copied"),
		         View.OutstandingRequest.InputTag == View_InputLight()
		         && View.OutstandingRequest.SourceActionTag == View_Root()
		         && View.OutstandingRequest.TargetActionTag == View_Light01());
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewParallelEdgeTest,
		"CadenceArc.Editor.DebugView.ParallelEdgeChosenByInputTag",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewParallelEdgeTest::RunTest(const FString& Parameters)
	{
		// 按住 Heavy 再松手，走 Root -Heavy(Released)-> Light01。
		// 它与 Root -Light-> Light01 源和目标都相同，只有 InputTag 能区分。
		FViewFixture Fixture;
		if (!MakeViewFixture(*this, Fixture))
		{
			return false;
		}
		const FCadenceArcInputToken Token = MakeViewToken();
		const FCadenceArcHoldOutcome Begin =
			Fixture.Resolver->BeginInputHold(Token, MakeViewPress(View_InputHeavy(), 1.0));
		if (!TestTrue(TEXT("Hold begins"), Begin.GetResult() == ECadenceArcHoldResult::Granted))
		{
			return false;
		}
		Fixture.Resolver->ReleaseInputHold(Token, MakeViewRelease(View_InputHeavy(), 1.5, 0.5));
		if (!TestTrue(TEXT("Release produces a request"),
		              Fixture.Resolver->GetState() == ECadenceArcResolverState::AwaitingStart))
		{
			return false;
		}

		const FCadenceArcDebugView View =
			BuildReadOnly(*this, TEXT("Heavy release"), *Fixture.Resolver, Fixture.Layout);
		ExpectIndices(*this, TEXT("Heavy release"), View, ViewNode_Root, ViewNode_Light01, ViewEdge_RootHeavy);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewAmbiguousEdgeTest,
		"CadenceArc.Editor.DebugView.AmbiguousEdgeNotGuessed",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewAmbiguousEdgeTest::RunTest(const FString& Parameters)
	{
		// 布局里有两条 源→目标＋InputTag 都相同的边（例如同 Tag 的不同松手时长档位）。
		// 视图不知道请求走的是哪一条：目标照常高亮，边不高亮。
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		FCadenceArcGraphLayout Ambiguous = Fixture.Layout;
		const FCadenceArcLayoutEdge Duplicate = Ambiguous.Edges[ViewEdge_RootLight]; // 先复制：Add 自身元素会触发别名断言
		Ambiguous.Edges.Add(Duplicate);

		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Ambiguous"), *Fixture.Resolver, Ambiguous);
		ExpectIndices(*this, TEXT("Ambiguous"), View, ViewNode_Root, ViewNode_Light01, INDEX_NONE);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewStartedTest,
		"CadenceArc.Editor.DebugView.StartedMovesCommitAndClearsCandidate",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewStartedTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		// 先在 AwaitingStart 构建一次，证明下一次构建不会残留这次的候选
		BuildDebugView(*Fixture.Resolver, Fixture.Layout);
		if (!TestTrue(TEXT("Started accepted"),
		              Fixture.Resolver->NotifyActionStarted(Request.RequestId) == ECadenceArcHandshakeResult::Success))
		{
			return false;
		}

		const FCadenceArcDebugView Executing =
			BuildReadOnly(*this, TEXT("Executing"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("Executing"), Executing, ECadenceArcResolverState::Executing);
		ExpectIndices(*this, TEXT("Executing"), Executing, ViewNode_Light01, INDEX_NONE, INDEX_NONE);

		// 正常完成：回到 Ready，已提交节点留在 Light01（连招继续），没有候选
		const FCadenceArcActionCompletionOutcome Completed =
			Fixture.Resolver->NotifyActionCompleted(Request.RequestId, 2.0);
		TestTrue(TEXT("Completed accepted"), Completed.GetHandshakeResult() == ECadenceArcHandshakeResult::Success);
		const FCadenceArcDebugView Ready = BuildReadOnly(*this, TEXT("Completed"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("Completed"), Ready, ECadenceArcResolverState::Ready);
		ExpectIndices(*this, TEXT("Completed"), Ready, ViewNode_Light01, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewRejectedTest,
		"CadenceArc.Editor.DebugView.RejectedKeepsSourceCommitted",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewRejectedTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		if (!TestTrue(TEXT("Rejected accepted"),
		              Fixture.Resolver->NotifyActionRejected(Request.RequestId) == ECadenceArcHandshakeResult::Success))
		{
			return false;
		}
		// 被拒绝的候选不能被画成走过的边：已提交节点仍是 Root，候选清空
		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Rejected"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("Rejected"), View, ECadenceArcResolverState::Ready);
		ExpectIndices(*this, TEXT("Rejected"), View, ViewNode_Root, INDEX_NONE, INDEX_NONE);
		TestEqual(TEXT("Rejected: request cleared"), View.OutstandingRequest.RequestId, int64{0});
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewResetTest,
		"CadenceArc.Editor.DebugView.ResetReturnsToEntry",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewResetTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		Fixture.Resolver->NotifyActionStarted(Request.RequestId);
		Fixture.Resolver->NotifyActionCompleted(Request.RequestId, 2.0);
		if (!TestTrue(TEXT("Reset accepted"),
		              Fixture.Resolver->Reset() == ECadenceArcResolverResetResult::Success))
		{
			return false;
		}
		// Reset 直接回到入口，不经过任何边，所以不能出现候选
		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Reset"), *Fixture.Resolver, Fixture.Layout);
		ExpectStateIs(*this, TEXT("Reset"), View, ECadenceArcResolverState::Ready);
		ExpectIndices(*this, TEXT("Reset"), View, ViewNode_Root, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewTagOutsideLayoutTest,
		"CadenceArc.Editor.DebugView.TagOutsideLayoutIsNone",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewTagOutsideLayoutTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}

		// 空布局：所有查找都失败，得到 INDEX_NONE，不越界
		const FCadenceArcDebugView Empty =
			BuildReadOnly(*this, TEXT("Empty layout"), *Fixture.Resolver, FCadenceArcGraphLayout{});
		ExpectStateIs(*this, TEXT("Empty layout"), Empty, ECadenceArcResolverState::AwaitingStart);
		ExpectIndices(*this, TEXT("Empty layout"), Empty, INDEX_NONE, INDEX_NONE, INDEX_NONE);

		// 布局里只缺目标节点：已提交的源节点照常找到，候选目标和边都给不出
		UCadenceArcGraph* Partial = NewObject<UCadenceArcGraph>();
		Partial->EntryActionTag = View_Root();
		Partial->Nodes.AddDefaulted_GetRef().ActionTag = View_Light02();
		FCadenceArcNode& Root = Partial->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = View_Root();
		AddViewTransition(Root, View_InputLight(), View_Light01()); // 坏目标
		const FCadenceArcDebugView Missing =
			BuildReadOnly(*this, TEXT("Missing target"), *Fixture.Resolver, BuildGraphLayout(*Partial));
		ExpectIndices(*this, TEXT("Missing target"), Missing, 1, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewBufferTest,
		"CadenceArc.Editor.DebugView.BufferWindowAndInputCopied",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewBufferTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		Fixture.Resolver->NotifyActionStarted(Request.RequestId);
		if (!TestTrue(TEXT("Window opens"),
		              Fixture.Resolver->OpenBufferWindow(Request.RequestId) == ECadenceArcHandshakeResult::Success))
		{
			return false;
		}

		const FCadenceArcDebugView Open = BuildReadOnly(*this, TEXT("Window open"), *Fixture.Resolver, Fixture.Layout);
		TestTrue(TEXT("Window open: flag copied"), Open.bBufferWindowOpen);
		TestFalse(TEXT("Window open: nothing buffered yet"), Open.BufferedInputTag.IsValid());

		const FCadenceArcSubmitOutcome Buffered = Fixture.Resolver->SubmitInput(MakeViewPress(View_InputLight(), 1.5));
		if (!TestTrue(TEXT("Input buffered"), Buffered.GetCategory() == ECadenceArcResolutionCategory::Buffered))
		{
			return false;
		}
		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Buffered"), *Fixture.Resolver, Fixture.Layout);
		TestTrue(TEXT("Buffered: tag copied"), View.BufferedInputTag == View_InputLight());
		// 缓冲的输入还没有被解析成请求：不能提前画成候选
		ExpectIndices(*this, TEXT("Buffered"), View, ViewNode_Light01, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewHoldSnapshotTest,
		"CadenceArc.Editor.DebugView.HoldSnapshotCopied",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewHoldSnapshotTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		if (!MakeViewFixture(*this, Fixture))
		{
			return false;
		}
		const FCadenceArcInputToken Token = MakeViewToken();
		const FCadenceArcHoldOutcome Begin =
			Fixture.Resolver->BeginInputHold(Token, MakeViewPress(View_InputHeavy(), 1.0));
		if (!TestTrue(TEXT("Hold begins"), Begin.GetResult() == ECadenceArcHoldResult::Granted))
		{
			return false;
		}

		const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("Holding"), *Fixture.Resolver, Fixture.Layout);
		const FCadenceArcHoldSnapshot Expected = Fixture.Resolver->GetInputHoldSnapshot();
		TestTrue(TEXT("Holding: snapshot has hold"), View.HoldSnapshot.bHasHold);
		TestTrue(TEXT("Holding: token copied"), View.HoldSnapshot.Token == Token);
		TestTrue(TEXT("Holding: input tag copied"), View.HoldSnapshot.InputTag == View_InputHeavy());
		TestTrue(TEXT("Holding: stage copied"), View.HoldSnapshot.Stage == Expected.Stage);
		TestEqual(TEXT("Holding: press time copied"), View.HoldSnapshot.PressedTimestampSeconds, 1.0);
		TestFalse(TEXT("Holding: no charge config"), View.HoldSnapshot.bHasChargeConfig);
		// 按住期间没有请求：只显示已提交节点
		ExpectIndices(*this, TEXT("Holding"), View, ViewNode_Root, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	// ---- 预备边：按住期间 Released 边的蓄力进度与"此刻松手会走的档位" ----

	static FGameplayTag View_Heavy01() { return DebugViewTag(TEXT("CadenceArc.Automation.Action.Heavy01")); }

	// Root 的边索引：e0 Heavy R [0, 0.8) -> Light01；e1 Heavy R [0.8, 无上限) -> Light02；
	//               e2 Heavy P -> Heavy01（同 Tag 的 Pressed 边，不是预备边）；e3 Light P -> Light01（不同 Tag）
	// bWithCharge 为真时 Root 带 Heavy 蓄力配置：0.2s 开始蓄力，蓄满（0.8s）后最多再按 1.0s，即按下后 1.8s 自动释放。
	static bool MakePreparatoryFixture(FAutomationTestBase& Test, const bool bWithCharge, FViewFixture& Out)
	{
		Out.Graph = NewObject<UCadenceArcGraph>();
		Out.Graph->EntryActionTag = View_Root();
		FCadenceArcNode& Root = Out.Graph->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = View_Root();
		const double Tier = bWithCharge ? 0.8 : 0.5;
		const auto AddReleased = [&Root](const FGameplayTag& Target, const double Min, const TOptional<double> Max)
		{
			FCadenceArcTransition& Transition = Root.Transitions.AddDefaulted_GetRef();
			Transition.InputTag = View_InputHeavy();
			Transition.TargetActionTag = Target;
			Transition.InputPhase = ECadenceArcInputPhase::Released;
			Transition.bUseDurationRange = true;
			Transition.DurationRange.MinHeldDurationSeconds = Min;
			Transition.DurationRange.bHasMaxHeldDuration = Max.IsSet();
			Transition.DurationRange.MaxHeldDurationSecondsExclusive = Max.Get(0.0);
		};
		AddReleased(View_Light01(), 0.0, Tier);
		AddReleased(View_Light02(), Tier, {});
		AddViewTransition(Root, View_InputHeavy(), View_Heavy01());
		AddViewTransition(Root, View_InputLight(), View_Light01());
		if (bWithCharge)
		{
			FCadenceArcHoldChargeConfig& Charge = Root.HoldChargeConfigs.AddDefaulted_GetRef();
			Charge.InputTag = View_InputHeavy();
			Charge.ChargeStartSeconds = 0.2;
			Charge.MaxChargedHoldSeconds = 1.0;
		}
		for (const FGameplayTag& Tag : {View_Light01(), View_Light02(), View_Heavy01()})
		{
			Out.Graph->Nodes.AddDefaulted_GetRef().ActionTag = Tag;
		}

		Out.Resolver = NewObject<UCadenceArcResolver>();
		Out.Layout = BuildGraphLayout(*Out.Graph);
		const bool bInitialized = Out.Resolver->Initialize(Out.Graph) == ECadenceArcResolverInitResult::Success;
		Test.TestTrue(TEXT("Preparatory fixture initializes"), bInitialized);
		return bInitialized && Test.TestEqual(TEXT("Preparatory fixture edge count"), Out.Layout.Edges.Num(), 4);
	}

	static bool BeginViewHold(FAutomationTestBase& Test, UCadenceArcResolver* Resolver, const double PressTime)
	{
		return Test.TestTrue(TEXT("Hold begins"),
		                     Resolver->BeginInputHold(MakeViewToken(), MakeViewPress(View_InputHeavy(), PressTime))
		                     .GetResult() == ECadenceArcHoldResult::Granted);
	}

	static bool AdvanceViewTime(FAutomationTestBase& Test, UCadenceArcResolver* Resolver, const double Now)
	{
		return Test.TestTrue(*FString::Printf(TEXT("Advance to %.2f accepted"), Now),
		                     Resolver->AdvanceInputTime(Now).IsAccepted());
	}

	// Progress 期望值：-1 表示"不是预备边"
	static void ExpectPreparatory(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcDebugView& View,
		const TArray<float>& ExpectedProgress, const int32 ExpectedReleaseEdge, const int32 ExpectedReleaseTarget)
	{
		if (!Test.TestEqual(*FString::Printf(TEXT("%s: progress aligned with edges"), What),
		                    View.PreparatoryEdgeProgress.Num(), ExpectedProgress.Num()))
		{
			return;
		}
		for (int32 EdgeIndex = 0; EdgeIndex < ExpectedProgress.Num(); ++EdgeIndex)
		{
			Test.TestEqual(*FString::Printf(TEXT("%s: edge %d progress"), What, EdgeIndex),
			               View.PreparatoryEdgeProgress[EdgeIndex], ExpectedProgress[EdgeIndex], 1.e-4f);
		}
		Test.TestEqual(*FString::Printf(TEXT("%s: release edge"), What),
		               View.CurrentReleaseEdgeIndex, ExpectedReleaseEdge);
		Test.TestEqual(*FString::Printf(TEXT("%s: release target"), What),
		               View.CurrentReleaseTargetNodeIndex, ExpectedReleaseTarget);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewPreparatoryChargeTest,
		"CadenceArc.Editor.DebugView.PreparatoryEdgesFollowCharge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewPreparatoryChargeTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		if (!MakePreparatoryFixture(*this, true, Fixture))
		{
			return false;
		}
		UCadenceArcResolver* Resolver = Fixture.Resolver;
		ExpectPreparatory(*this, TEXT("No hold"), BuildReadOnly(*this, TEXT("No hold"), *Resolver, Fixture.Layout),
		                  {-1.f, -1.f, -1.f, -1.f}, INDEX_NONE, INDEX_NONE);

		// 只有同 Tag 的 Released 边是预备边；刚按下落在第一档
		if (!BeginViewHold(*this, Resolver, 1.0))
		{
			return false;
		}
		ExpectPreparatory(*this, TEXT("Pressed"), BuildReadOnly(*this, TEXT("Pressed"), *Resolver, Fixture.Layout),
		                  {0.f, 0.f, -1.f, -1.f}, 0, 1);

		// 有上限的档位：0.5 / 0.8；无上限档位还没到下限
		AdvanceViewTime(*this, Resolver, 1.5);
		ExpectPreparatory(*this, TEXT("Held 0.5"), BuildReadOnly(*this, TEXT("Held 0.5"), *Resolver, Fixture.Layout),
		                  {0.625f, 0.f, -1.f, -1.f}, 0, 1);

		// 正好 0.8：左闭右开，第一档填满但不再匹配，换到第二档
		AdvanceViewTime(*this, Resolver, 1.8);
		ExpectPreparatory(*this, TEXT("Held 0.8"), BuildReadOnly(*this, TEXT("Held 0.8"), *Resolver, Fixture.Layout),
		                  {1.f, 0.f, -1.f, -1.f}, 1, 2);

		// 无上限档位有蓄力配置：从 0.8 填到自动释放的 1.8
		AdvanceViewTime(*this, Resolver, 2.3);
		ExpectPreparatory(*this, TEXT("Held 1.3"), BuildReadOnly(*this, TEXT("Held 1.3"), *Resolver, Fixture.Layout),
		                  {1.f, 0.5f, -1.f, -1.f}, 1, 2);

		// 松手产生请求，资格结束：预备边全部消失，只剩候选
		Resolver->ReleaseInputHold(MakeViewToken(), MakeViewRelease(View_InputHeavy(), 2.4, 1.4));
		const FCadenceArcDebugView Released = BuildReadOnly(*this, TEXT("Released"), *Resolver, Fixture.Layout);
		ExpectPreparatory(*this, TEXT("Released"), Released, {-1.f, -1.f, -1.f, -1.f}, INDEX_NONE, INDEX_NONE);
		ExpectIndices(*this, TEXT("Released"), Released, 0, 2, 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewPreparatoryNoChargeTest,
		"CadenceArc.Editor.DebugView.PreparatoryUnboundedWithoutCharge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewPreparatoryNoChargeTest::RunTest(const FString& Parameters)
	{
		// 没有蓄力配置就没有自动释放时刻：无上限档位在达到下限的瞬间填满
		FViewFixture Fixture;
		if (!MakePreparatoryFixture(*this, false, Fixture) || !BeginViewHold(*this, Fixture.Resolver, 1.0))
		{
			return false;
		}
		AdvanceViewTime(*this, Fixture.Resolver, 1.25);
		ExpectPreparatory(*this, TEXT("Held 0.25"),
		                  BuildReadOnly(*this, TEXT("Held 0.25"), *Fixture.Resolver, Fixture.Layout),
		                  {0.5f, 0.f, -1.f, -1.f}, 0, 1);
		AdvanceViewTime(*this, Fixture.Resolver, 1.5);
		ExpectPreparatory(*this, TEXT("Held 0.5"),
		                  BuildReadOnly(*this, TEXT("Held 0.5"), *Fixture.Resolver, Fixture.Layout),
		                  {1.f, 1.f, -1.f, -1.f}, 1, 2);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewPreparatoryNoRangeTest,
		"CadenceArc.Editor.DebugView.PreparatoryEdgeWithoutRange",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewPreparatoryNoRangeTest::RunTest(const FString& Parameters)
	{
		// 基础夹具里 Root -Heavy(Released, 不分档)-> Light01 是边 2：接受任意时长，一按下就满，也就是松手会走的边
		FViewFixture Fixture;
		if (!MakeViewFixture(*this, Fixture) || !BeginViewHold(*this, Fixture.Resolver, 1.0))
		{
			return false;
		}
		ExpectPreparatory(*this, TEXT("No range"),
		                  BuildReadOnly(*this, TEXT("No range"), *Fixture.Resolver, Fixture.Layout),
		                  {-1.f, -1.f, 1.f}, ViewEdge_RootHeavy, ViewNode_Light01);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewPreparatorySourceMissingTest,
		"CadenceArc.Editor.DebugView.PreparatorySourceOutsideLayout",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewPreparatorySourceMissingTest::RunTest(const FString& Parameters)
	{
		FViewFixture Fixture;
		if (!MakePreparatoryFixture(*this, true, Fixture) || !BeginViewHold(*this, Fixture.Resolver, 1.0))
		{
			return false;
		}
		// 空布局：进度数组跟着布局为空，不越界
		ExpectPreparatory(*this, TEXT("Empty layout"),
		                  BuildReadOnly(*this, TEXT("Empty layout"), *Fixture.Resolver, FCadenceArcGraphLayout{}),
		                  {}, INDEX_NONE, INDEX_NONE);

		// 布局里没有资格的源节点 Root：即使有同 Tag 的 Released 边也不认作预备边
		UCadenceArcGraph* Other = NewObject<UCadenceArcGraph>();
		Other->EntryActionTag = View_Light01();
		FCadenceArcNode& Light01 = Other->Nodes.AddDefaulted_GetRef();
		Light01.ActionTag = View_Light01();
		AddViewTransition(Light01, View_InputHeavy(), View_Light02(), ECadenceArcInputPhase::Released);
		Other->Nodes.AddDefaulted_GetRef().ActionTag = View_Light02();
		ExpectPreparatory(*this, TEXT("Source missing"),
		                  BuildReadOnly(*this, TEXT("Source missing"), *Fixture.Resolver, BuildGraphLayout(*Other)),
		                  {-1.f}, INDEX_NONE, INDEX_NONE);
		return !HasAnyErrors();
	}

	// ---- 分支聚焦：从已提交节点出发还要几步 ----

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewBranchDistanceTest,
		"CadenceArc.Editor.DebugView.BranchDistanceFromCommitted",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewBranchDistanceTest::RunTest(const FString& Parameters)
	{
		// 未初始化没有已提交节点：不做分支聚焦
		{
			const UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			const FCadenceArcDebugView View =
				BuildReadOnly(*this, TEXT("Uninitialized"), *Resolver, BuildGraphLayout(*MakeViewGraph()));
			TestEqual(TEXT("Uninitialized: no distances"), View.NodeDistance.Num(), 0);
		}

		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture))
		{
			return false;
		}
		// 在 Root：Root 0，下一步 Light01 为 1（两条平行边只算一次），再下一步 Light02 为 2
		{
			const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("At Root"), *Fixture.Resolver, Fixture.Layout);
			if (TestEqual(TEXT("At Root: one distance per node"), View.NodeDistance.Num(), 3))
			{
				TestEqual(TEXT("At Root: Root"), View.NodeDistance[ViewNode_Root], 0);
				TestEqual(TEXT("At Root: Light01"), View.NodeDistance[ViewNode_Light01], 1);
				TestEqual(TEXT("At Root: Light02"), View.NodeDistance[ViewNode_Light02], 2);
			}
		}
		// 提交到 Light01 后，Root 在这条路上已经走不到（没有边回去；Reset 不算）
		if (!RequestPress(*this, Fixture.Resolver, View_InputLight(), Request)
			|| !TestTrue(TEXT("Started accepted"),
			             Fixture.Resolver->NotifyActionStarted(Request.RequestId) == ECadenceArcHandshakeResult::Success))
		{
			return false;
		}
		{
			const FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("At Light01"), *Fixture.Resolver, Fixture.Layout);
			if (TestEqual(TEXT("At Light01: one distance per node"), View.NodeDistance.Num(), 3))
			{
				TestEqual(TEXT("At Light01: Light01"), View.NodeDistance[ViewNode_Light01], 0);
				TestEqual(TEXT("At Light01: Light02"), View.NodeDistance[ViewNode_Light02], 1);
				TestEqual(TEXT("At Light01: Root is off the path"), View.NodeDistance[ViewNode_Root], INDEX_NONE);
			}
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewBranchLoopTest,
		"CadenceArc.Editor.DebugView.BranchDistanceFollowsLoops",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewBranchLoopTest::RunTest(const FString& Parameters)
	{
		// 布局里 Light02 有一条回到 Root 的循环边：停在 Light01 时，沿循环还能回到 Root，所以 Root 不变暗
		FViewFixture Fixture;
		FCadenceArcActionRequest Request;
		if (!MakeViewFixture(*this, Fixture) || !RequestPress(*this, Fixture.Resolver, View_InputLight(), Request))
		{
			return false;
		}
		Fixture.Resolver->NotifyActionStarted(Request.RequestId);
		UCadenceArcGraph* Looped = MakeViewGraph();
		AddViewTransition(Looped->Nodes[ViewNode_Light02], View_InputLight(), View_Root());
		const FCadenceArcDebugView View =
			BuildReadOnly(*this, TEXT("Loop"), *Fixture.Resolver, BuildGraphLayout(*Looped));
		if (TestEqual(TEXT("Loop: one distance per node"), View.NodeDistance.Num(), 3))
		{
			TestEqual(TEXT("Loop: Light02"), View.NodeDistance[ViewNode_Light02], 1);
			TestEqual(TEXT("Loop: Root reachable through the loop"), View.NodeDistance[ViewNode_Root], 2);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugViewPreparatoryConditionTest,
		"CadenceArc.Editor.DebugView.PreparatoryFollowsConditions",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugViewPreparatoryConditionTest::RunTest(const FString& Parameters)
	{
		// 在无蓄力夹具的 Root 上再加两条 Heavy R [0.5, 无上限)：需要 Air、优先级 1 → Heavy01；
		// 带停顿区间、优先级 2 → Light01。停顿边要等松手事件才知道，预览一律跳过它。
		FViewFixture Fixture;
		if (!MakePreparatoryFixture(*this, false, Fixture))
		{
			return false;
		}
		FCadenceArcNode& Root = Fixture.Graph->Nodes[0];
		FCadenceArcTransition AirTier = Root.Transitions[1];
		AirTier.TargetActionTag = View_Heavy01();
		AirTier.Priority = 1;
		AirTier.RequiredContextTags.AddTag(DebugViewTag(TEXT("CadenceArc.Automation.Context.Air")));
		FCadenceArcTransition PauseTier = Root.Transitions[1];
		PauseTier.TargetActionTag = View_Light01();
		PauseTier.Priority = 2;
		PauseTier.bUsePauseRange = true;
		Root.Transitions.Add(AirTier);
		Root.Transitions.Add(PauseTier);

		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		const FCadenceArcGraphLayout Layout = BuildGraphLayout(*Fixture.Graph);
		if (!TestTrue(TEXT("Conditional fixture initializes"),
		              Resolver->Initialize(Fixture.Graph) == ECadenceArcResolverInitResult::Success)
			|| !BeginViewHold(*this, Resolver, 1.0) || !AdvanceViewTime(*this, Resolver, 1.6))
		{
			return false;
		}
		const auto FindEdge = [&Layout](const FGameplayTag& Target, const int32 Priority)
		{
			return Layout.Edges.IndexOfByPredicate([&](const FCadenceArcLayoutEdge& Edge)
			{
				return Edge.Transition.InputPhase == ECadenceArcInputPhase::Released
					&& Edge.Transition.TargetActionTag == Target && Edge.Transition.Priority == Priority;
			});
		};
		const auto FindNode = [&Layout](const FGameplayTag& Tag)
		{
			return Layout.Nodes.IndexOfByPredicate([&](const FCadenceArcLayoutNode& Node) { return Node.ActionTag == Tag; });
		};
		const int32 PlainTier = FindEdge(View_Light02(), 0);
		const int32 AirEdge = FindEdge(View_Heavy01(), 1);
		const int32 PauseEdge = FindEdge(View_Light01(), 2);
		if (!TestTrue(TEXT("Conditional edges are laid out"),
		              PlainTier != INDEX_NONE && AirEdge != INDEX_NONE && PauseEdge != INDEX_NONE))
		{
			return false;
		}

		// 没有 Air：Air 档位不满足，停顿档位不参与，落在普通档位
		FCadenceArcDebugView View = BuildReadOnly(*this, TEXT("No context"), *Resolver, Layout);
		TestEqual(TEXT("No context picks the plain tier"), View.CurrentReleaseEdgeIndex, PlainTier);
		TestEqual(TEXT("No context target"), View.CurrentReleaseTargetNodeIndex, FindNode(View_Light02()));
		TestEqual(TEXT("Conditional tiers still show progress"), View.PreparatoryEdgeProgress[AirEdge], 1.f);

		// 持久上下文里有 Air.Jump（子 Tag）：优先级更高的 Air 档位胜出，停顿档位仍被跳过
		Resolver->SetContextTags(FGameplayTagContainer(DebugViewTag(TEXT("CadenceArc.Automation.Context.Air.Jump"))));
		View = BuildReadOnly(*this, TEXT("Air"), *Resolver, Layout);
		TestEqual(TEXT("Air context picks the Air tier"), View.CurrentReleaseEdgeIndex, AirEdge);
		TestEqual(TEXT("Air context target"), View.CurrentReleaseTargetNodeIndex, FindNode(View_Heavy01()));
		return !HasAnyErrors();
	}
}

#endif
