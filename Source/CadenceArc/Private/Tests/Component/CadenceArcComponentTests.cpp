// UCadenceArcComponent 的确定性测试。组件不注册到 World：时间由 SetTimeSource 注入，逐帧推进直接调用 TickComponent。
// 输入路由部分迁移自 Sandbox 的 FCadenceArcHoldInputRouter 测试（CadenceArc.Sandbox.HoldRouter.*），覆盖范围保持不变。
//
// 被测契约：
// - 每个带时间的调用都先推进按住时间，并把到期释放产生的请求发出，再处理本次调用。
// - Press：Tracker 产生新按下才记录配对；PressOnly 提交输入，HoldRelease 申请按住资格。
//   申请被拒绝时仍保留按键配对，直到真实松开或取消；重复按下被忽略。
// - Release：结束配对；HoldRelease 只在解析器仍持有这个 Token 的资格时提交松手。
// - Cancel / CancelAll：取消资格并结束配对，不产生松手和请求。
// - 所有请求（直接提交、松手、自动释放、完成时消费缓冲）都从同一个出口发出。
// - 组件使用注入的时间来源；没有初始化解析器时所有调用都是安全的空操作。

#if WITH_DEV_AUTOMATION_TESTS

#include "Component/CadenceArcComponent.h"

#include "Tests/CadenceArcTestSupport.h"
#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Tests
{
	// 全部用二进制精确值，避免浮点相加落在阈值边界上：
	// 按下 1.0 -> 蓄力起点 1.25、蓄满 1.5；保持 0 时 1.5 自动释放，保持 1.0 时 2.5 自动释放。
	constexpr double ComponentChargeStartSeconds = 0.25;
	constexpr double ComponentChargeFullSeconds = 0.5;

	static FCadenceArcTransition MakeComponentEdge(
		const FGameplayTag& InputTag, const FGameplayTag& Target, const ECadenceArcInputPhase Phase,
		const double MinSeconds = 0.0, const bool bHasMax = false, const double MaxSeconds = 0.0)
	{
		FCadenceArcTransition Edge;
		Edge.InputTag = InputTag;
		Edge.TargetActionTag = Target;
		Edge.InputPhase = Phase;
		if (Phase == ECadenceArcInputPhase::Released)
		{
			Edge.bUseDurationRange = true;
			Edge.DurationRange.MinHeldDurationSeconds = MinSeconds;
			Edge.DurationRange.bHasMaxHeldDuration = bHasMax;
			Edge.DurationRange.MaxHeldDurationSecondsExclusive = MaxSeconds;
		}
		return Edge;
	}

	// Light 是 Pressed 分支（PressOnly），Heavy 是两档 Released 分支（HoldRelease）。
	// Root 上没有 Light 的 Released 边，用来制造"申请被拒绝"。
	static void AddComponentNode(
		UCadenceArcGraph* Graph, const FGameplayTag& ActionTag, const FGameplayTag& LightTarget,
		const FGameplayTag& TapTarget, const FGameplayTag& ChargedTarget, const double MaxChargedHoldSeconds)
	{
		FCadenceArcNode& Node = Graph->Nodes.AddDefaulted_GetRef();
		Node.ActionTag = ActionTag;
		Node.Transitions.Add(MakeComponentEdge(Input_Light, LightTarget, ECadenceArcInputPhase::Pressed));
		Node.Transitions.Add(MakeComponentEdge(Input_Heavy, TapTarget, ECadenceArcInputPhase::Released,
		                                       0.0, true, ComponentChargeFullSeconds));
		Node.Transitions.Add(MakeComponentEdge(Input_Heavy, ChargedTarget, ECadenceArcInputPhase::Released,
		                                       ComponentChargeFullSeconds));
		FCadenceArcHoldChargeConfig& Config = Node.HoldChargeConfigs.AddDefaulted_GetRef();
		Config.InputTag = Input_Heavy;
		Config.ChargeStartSeconds = ComponentChargeStartSeconds;
		Config.MaxChargedHoldSeconds = MaxChargedHoldSeconds;
	}

	// 对应关系：HeavyTap = Heavy01，HeavyCharged = Heavy02，FinisherTap = Finisher01，FinisherCharged = Finisher02
	static UCadenceArcGraph* MakeComponentGraph(const double MaxChargedHoldSeconds)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(8);
		AddComponentNode(Graph, Action_Root, Action_Light01, Action_Heavy01, Action_Heavy02, MaxChargedHoldSeconds);
		AddComponentNode(Graph, Action_Light01, Action_Light02, Action_Finisher01, Action_Finisher02,
		                 MaxChargedHoldSeconds);
		for (const FGameplayTag& Leaf : TArray<FGameplayTag>{
			     Action_Light02, Action_Heavy01, Action_Heavy02, Action_Finisher01, Action_Finisher02
		     })
		{
			Graph->Nodes.AddDefaulted_GetRef().ActionTag = Leaf;
		}
		return Graph;
	}

	// 模拟执行器：同步记录并接受请求，让解析器立刻进入 Executing。时间由 Now 注入。
	struct FComponentHost
	{
		UCadenceArcComponent* Component = nullptr;
		double Now = 0.0;
		TArray<FCadenceArcActionRequest> Started;
		TArray<ECadenceArcHoldStage> Stages;

		void Bind(UCadenceArcComponent* InComponent)
		{
			Component = InComponent;
			Component->SetTimeSource([this]() { return Now; });
			Component->OnActionRequestedNative.AddLambda([this](const FCadenceArcActionRequest& Request)
			{
				Started.Add(Request);
				Component->NotifyActionStarted(Request.RequestId);
			});
			Component->OnHoldStageChangedNative.AddLambda(
				[this](FGameplayTag, const FCadenceArcInputStageChange& Change) { Stages.Add(Change.ToStage); });
		}

		void Tick(const double Time)
		{
			Now = Time;
			Component->TickComponent(0.f, LEVELTICK_All, nullptr);
		}
	};

	static UCadenceArcComponent* MakeHostedComponent(
		FAutomationTestBase& Test, FComponentHost& Host, const double MaxChargedHoldSeconds = 1.0)
	{
		UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
		Host.Bind(Component);
		Test.TestEqual(TEXT("Component graph initializes"),
		               static_cast<int32>(Component->InitializeResolver(MakeComponentGraph(MaxChargedHoldSeconds))),
		               static_cast<int32>(ECadenceArcResolverInitResult::Success));
		return Component;
	}

	static bool ExpectStarted(
		FAutomationTestBase& Test, const TCHAR* What, const FComponentHost& Host, const TArray<FGameplayTag>& Expected)
	{
		if (!Test.TestEqual(*FString::Printf(TEXT("%s start count"), What), Host.Started.Num(), Expected.Num()))
		{
			for (const FCadenceArcActionRequest& Request : Host.Started)
			{
				Test.AddInfo(FString::Printf(TEXT("%s started %s"), What, *Request.TargetActionTag.ToString()));
			}
			return false;
		}
		bool bPassed = true;
		for (int32 Index = 0; Index < Expected.Num(); ++Index)
		{
			bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s start[%d] target"), What, Index),
			                          Host.Started[Index].TargetActionTag.ToString(), Expected[Index].ToString());
		}
		return bPassed;
	}

	static bool ExpectHold(FAutomationTestBase& Test, const TCHAR* What, const UCadenceArcComponent* Component,
	                       const bool bExpectHold)
	{
		const FString Label = FString::Printf(TEXT("%s resolver hold"), What);
		const bool bHasHold = Component->GetResolver()->GetInputHoldSnapshot().bHasHold;
		return bExpectHold ? Test.TestTrue(*Label, bHasHold) : Test.TestFalse(*Label, bHasHold);
	}

	// 资格已经不在时，松开不应再调用 ReleaseInputHold。调试历史只在编辑器构建中存在。
	static void ExpectNoResolverRelease(FAutomationTestBase& Test, const TCHAR* What, const UCadenceArcComponent* Component)
	{
#if WITH_EDITOR
		TArray<FCadenceArcDebugEvent> Events;
		Component->GetResolver()->GetDebugHistory().CopyEventsAfter(0, Events);
		const bool bReleased = Events.ContainsByPredicate([](const FCadenceArcDebugEvent& Event)
		{
			return Event.Operation == ECadenceArcDebugOperation::ReleaseHold;
		});
		Test.TestFalse(*FString::Printf(TEXT("%s: no ReleaseInputHold reaches the resolver"), What), bReleased);
#endif
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentPressOnlyTest,
		"CadenceArc.Component.Input.PressOnlySubmitsOnPress",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentPressOnlyTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);

		Host.Now = 1.0;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("PressOnly press"), Host, {Action_Light01});
		TestTrue(TEXT("PressOnly press is tracked until release"), Component->IsInputPressed(Input_Light));
		ExpectHold(*this, TEXT("PressOnly press"), Component, false);

		// 松开只结束配对，不会再次提交
		Host.Now = 1.1;
		Component->ReleaseInput(Input_Light, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("PressOnly release"), Host, {Action_Light01});
		TestFalse(TEXT("PressOnly release ends tracking"), Component->IsInputPressed(Input_Light));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentHoldReleaseTest,
		"CadenceArc.Component.Input.HoldReleaseResolvesOnRelease",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentHoldReleaseTest::RunTest(const FString& Parameters)
	{
		// 短按：按下只取得资格，松开时按时长选普通档
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			ExpectStarted(*this, TEXT("Hold press"), Host, {});
			ExpectHold(*this, TEXT("Hold press"), Component, true);

			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
			ExpectStarted(*this, TEXT("Tap release"), Host, {Action_Heavy01});
			ExpectHold(*this, TEXT("Tap release"), Component, false);
			TestFalse(TEXT("Tap release ends tracking"), Component->IsInputPressed(Input_Heavy));
		}

		// 长按：Tick 推进到蓄力阶段并发出阶段事件；按住时长由 Tracker 计算，不由宿主填写
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			Host.Tick(1.3);
			TestEqual(TEXT("Tick reaches Charging"),
			          static_cast<int32>(Component->GetResolver()->GetInputHoldSnapshot().Stage),
			          static_cast<int32>(ECadenceArcHoldStage::Charging));
			Host.Tick(1.6);
			TestTrue(TEXT("Stage events in order"), Host.Stages == TArray<ECadenceArcHoldStage>{
				         ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged
			         });
			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
			ExpectStarted(*this, TEXT("Charged release"), Host, {Action_Heavy02});
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentAdvanceFirstTest,
		"CadenceArc.Component.Input.AdvanceRunsBeforeInput",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentAdvanceFirstTest::RunTest(const FString& Parameters)
	{
		// 保持 0：1.5 应自动释放。中间没有 Tick，直接在 2.0 按下另一个键。
		// 组件必须先推进：到期的蓄力攻击先发出，新的按下才进入（此时窗口关闭，被忽略）。
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host, 0.0);
		Host.Now = 1.0;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		Host.Now = 2.0;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("Overdue release before new press"), Host, {Action_Heavy02});
		TestEqual(TEXT("Overdue release is executing"), Component->GetResolver()->GetCurrentActionTag().ToString(),
		          Action_Heavy02.GetTag().ToString());

		// 松开晚到：资格已经兑现过，不会再攻击一次，但配对要正常结束
		Host.Now = 2.1;
		Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("Late physical release"), Host, {Action_Heavy02});
		TestFalse(TEXT("Late physical release ends tracking"), Component->IsInputPressed(Input_Heavy));
		TestTrue(TEXT("Other key is still tracked"), Component->IsInputPressed(Input_Light));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentAutoReleaseTest,
		"CadenceArc.Component.Input.TickAutoReleaseOnce",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentAutoReleaseTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host, 0.0);
		Host.Now = 1.0;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		Host.Tick(1.4);
		ExpectStarted(*this, TEXT("Before the deadline"), Host, {});

		// Tick 推进到截止之后：自动释放一次，请求从统一出口发出
		Host.Tick(1.6);
		ExpectStarted(*this, TEXT("Tick auto release"), Host, {Action_Heavy02});

		// 重复推进和随后的松开都不会产生第二次攻击
		Host.Tick(1.7);
		Host.Now = 2.0;
		Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("After auto release"), Host, {Action_Heavy02});
		TestFalse(TEXT("Physical release still ends tracking"), Component->IsInputPressed(Input_Heavy));
		ExpectNoResolverRelease(*this, TEXT("After auto release"), Component);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentMultiTagTest,
		"CadenceArc.Component.Input.TagsRouteIndependently",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentMultiTagTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);

		// Heavy 仍处于 Holding（1.1 < 1.25），Light 的按下被接受并替换 Heavy 的资格
		Host.Now = 1.0;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		Host.Now = 1.1;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("Light replaces holding heavy"), Host, {Action_Light01});
		TestTrue(TEXT("Both keys are tracked"),
		         Component->IsInputPressed(Input_Heavy) && Component->IsInputPressed(Input_Light));

		// Heavy 松开使用 Heavy 自己的 Token：只结束 Heavy 的配对，被替换的资格不产生攻击
		Host.Now = 1.3;
		Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("Replaced heavy release"), Host, {Action_Light01});
		TestFalse(TEXT("Heavy release ends heavy tracking"), Component->IsInputPressed(Input_Heavy));
		TestTrue(TEXT("Heavy release leaves light tracked"), Component->IsInputPressed(Input_Light));

		Host.Now = 1.4;
		Component->ReleaseInput(Input_Light, FGameplayTagContainer());
		TestFalse(TEXT("Light release ends light tracking"), Component->IsInputPressed(Input_Light));
		ExpectStarted(*this, TEXT("Final"), Host, {Action_Light01});
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentRejectedBeginTest,
		"CadenceArc.Component.Input.RejectedBeginKeepsPhysicalPress",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentRejectedBeginTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);

		// Root 上 Light 没有 Released 边：申请被拒绝，但按键配对仍保留到真实松开
		Host.Now = 1.0;
		Component->PressInput(Input_Light, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		ExpectHold(*this, TEXT("Rejected begin"), Component, false);
		TestTrue(TEXT("Rejected begin keeps the physical press"), Component->IsInputPressed(Input_Light));
		Host.Now = 1.2;
		Component->ReleaseInput(Input_Light, FGameplayTagContainer());
		TestFalse(TEXT("Release after rejected begin ends tracking"), Component->IsInputPressed(Input_Light));
		ExpectStarted(*this, TEXT("Rejected begin"), Host, {});
		ExpectNoResolverRelease(*this, TEXT("Release after rejected begin"), Component);

		// 重复按下被忽略，不会替换已经授予的资格
		Host.Now = 2.0;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		const FCadenceArcHoldSnapshot First = Component->GetResolver()->GetInputHoldSnapshot();
		Host.Now = 2.1;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		const FCadenceArcHoldSnapshot Second = Component->GetResolver()->GetInputHoldSnapshot();
		TestTrue(TEXT("Repeated press keeps the token"), First.Token == Second.Token);
		TestEqual(TEXT("Repeated press keeps the press time"), Second.PressedTimestampSeconds, 2.0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentCancelTest,
		"CadenceArc.Component.Input.CancelNeverReleases",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentCancelTest::RunTest(const FString& Parameters)
	{
		// 单键取消：蓄力中取消，不合成松开；之后的松开和推进都不会攻击
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			Host.Tick(1.3);
			Component->CancelInput(Input_Heavy);
			ExpectHold(*this, TEXT("Cancel"), Component, false);
			TestFalse(TEXT("Cancel ends tracking"), Component->IsInputPressed(Input_Heavy));

			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
			Host.Tick(10.0);
			ExpectStarted(*this, TEXT("After cancel"), Host, {});

			// 取消之后同一个键可以重新按下
			Host.Now = 11.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			ExpectHold(*this, TEXT("Press after cancel"), Component, true);
		}

		// 全部取消：所有按键和资格都清掉，不产生松开；之后仍可正常使用
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			Host.Now = 1.05;
			Component->PressInput(Input_Light, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer()); // 申请被拒绝，仍跟踪
			TestTrue(TEXT("Both keys tracked before CancelAllInputs"),
			         Component->IsInputPressed(Input_Heavy) && Component->IsInputPressed(Input_Light));

			Component->CancelAllInputs();
			ExpectHold(*this, TEXT("CancelAllInputs"), Component, false);
			TestFalse(TEXT("CancelAllInputs clears heavy"), Component->IsInputPressed(Input_Heavy));
			TestFalse(TEXT("CancelAllInputs clears light"), Component->IsInputPressed(Input_Light));

			Host.Now = 1.2;
			Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
			Component->ReleaseInput(Input_Light, FGameplayTagContainer());
			Host.Tick(10.0);
			ExpectStarted(*this, TEXT("After CancelAllInputs"), Host, {});

			Host.Now = 11.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			ExpectHold(*this, TEXT("Press after CancelAllInputs"), Component, true);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentAcrossCompletionTest,
		"CadenceArc.Component.Execution.HoldSurvivesCompletion",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentAcrossCompletionTest::RunTest(const FString& Parameters)
	{
		// 完整时序：执行中开窗按下 -> 关窗 -> 完成（组件先推进再通知）-> 资格保留 -> 松开出长按攻击
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
		Host.Now = 0.4;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
		Host.Now = 0.5;
		Component->ReleaseInput(Input_Light, FGameplayTagContainer());
		if (!ExpectStarted(*this, TEXT("Light01 starts"), Host, {Action_Light01})) { return false; }
		const int64 RequestId = Host.Started[0].RequestId;

		Component->OpenBufferWindow(RequestId);
		Host.Now = 1.0;
		Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
		ExpectHold(*this, TEXT("Grant inside the window"), Component, true);
		Component->CloseBufferWindow(RequestId);

		Host.Now = 1.4;
		const FCadenceArcActionCompletionOutcome Completed = Component->NotifyActionCompleted(RequestId);
		TestEqual(TEXT("Completion handshake"), static_cast<int32>(Completed.GetHandshakeResult()),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));
		TestEqual(TEXT("Completion keeps the hold"), static_cast<int32>(Completed.GetBufferConsumptionReason()),
		          static_cast<int32>(ECadenceArcResolutionReason::WaitingForRelease));
		ExpectHold(*this, TEXT("After completion"), Component, true);

		Host.Now = 2.0;
		Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
		ExpectStarted(*this, TEXT("Release after completion"), Host, {Action_Light01, Action_Finisher02});
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentBufferedOutletTest,
		"CadenceArc.Component.Execution.CompletionRequestUsesSameOutlet",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentBufferedOutletTest::RunTest(const FString& Parameters)
	{
		// 执行中按下进缓冲；完成时消费缓冲产生的请求从同一个出口发出，时间来自注入的时间来源
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
		Host.Now = 1.0;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
		Host.Now = 1.1;
		Component->ReleaseInput(Input_Light, FGameplayTagContainer());
		if (!ExpectStarted(*this, TEXT("First press"), Host, {Action_Light01})) { return false; }
		const int64 First = Host.Started[0].RequestId;
		Component->OpenBufferWindow(First);
		Host.Now = 1.3;
		Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer(Context_Forward));
		ExpectStarted(*this, TEXT("Buffered press"), Host, {Action_Light01});

		Host.Now = 1.8;
		const FCadenceArcActionCompletionOutcome Completed = Component->NotifyActionCompleted(First);
		TestTrue(TEXT("Completion produces the next request"), Completed.HasNextActionRequest());
		ExpectStarted(*this, TEXT("Completion outlet"), Host, {Action_Light01, Action_Light02});
		TestEqual(TEXT("Next request is executing"), Component->GetResolver()->GetCurrentActionTag().ToString(),
		          Action_Light02.GetTag().ToString());
		TestEqual(TEXT("Component time comes from the injected source"), Component->GetTimeSeconds(), 1.8);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentUninitializedTest,
		"CadenceArc.Component.UninitializedIsNoOp",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentUninitializedTest::RunTest(const FString& Parameters)
	{
		// 没有初始化解析器：所有调用安全返回，不跟踪、不发出请求
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
			Host.Bind(Component);
			Host.Tick(1.0);
			Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			TestFalse(TEXT("Uninitialized component does not track"), Component->IsInputPressed(Input_Light));
			Component->ReleaseInput(Input_Light, FGameplayTagContainer());
			Component->CancelInput(Input_Heavy);
			Component->CancelAllInputs();
			TestEqual(TEXT("Completion without resolver"),
			          static_cast<int32>(Component->NotifyActionCompleted(1).GetHandshakeResult()),
			          static_cast<int32>(ECadenceArcHandshakeResult::NotInitialized));
			TestEqual(TEXT("Reset without resolver"), static_cast<int32>(Component->ResetCombo()),
			          static_cast<int32>(ECadenceArcResolverResetResult::NotInitialized));
			ExpectStarted(*this, TEXT("Uninitialized"), Host, {});
		}

		// 无效图：初始化失败，组件仍然是空操作
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
			Host.Bind(Component);
			TestNotEqual(TEXT("Empty graph is rejected"),
			             static_cast<int32>(Component->InitializeResolver(NewObject<UCadenceArcGraph>())),
			             static_cast<int32>(ECadenceArcResolverInitResult::Success));
			Host.Now = 1.0;
			Component->PressInput(Input_Light, ECadenceArcInputMode::PressOnly, FGameplayTagContainer());
			TestFalse(TEXT("Invalid graph does not track"), Component->IsInputPressed(Input_Light));
			ExpectStarted(*this, TEXT("Invalid graph"), Host, {});
		}

		// 重新初始化：清空旧的按键配对，旧按键的松开不产生请求
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy, ECadenceArcInputMode::HoldRelease, FGameplayTagContainer());
			Component->InitializeResolver(MakeComponentGraph(1.0));
			TestFalse(TEXT("Re-initialize clears tracking"), Component->IsInputPressed(Input_Heavy));
			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy, FGameplayTagContainer());
			ExpectStarted(*this, TEXT("Release after re-initialize"), Host, {});
		}
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
