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
#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Editor::Tests
{
	// 与布局测试一样：Editor 模块不能定义原生 Tag，按名字复用 Runtime 注册的测试 Tag，且在运行时再取。
	static FGameplayTag DebugViewTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
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
		              && First.CandidateTargetNodeIndex == Second.CandidateTargetNodeIndex
		              && First.CandidateEdgeIndex == Second.CandidateEdgeIndex
		              && First.OutstandingRequest.RequestId == Second.OutstandingRequest.RequestId
		              && First.bBufferWindowOpen == Second.bBufferWindowOpen
		              && First.BufferedInputTag == Second.BufferedInputTag
		              && First.HoldSnapshot.bHasHold == Second.HoldSnapshot.bHasHold);
		return First;
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
}

#endif
