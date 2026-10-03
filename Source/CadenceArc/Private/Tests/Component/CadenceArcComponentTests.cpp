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
			Component->OnHoldEndedNative.AddLambda(
				[this](const FGameplayTag InputTag, const ECadenceArcHoldEndReason Reason)
				{
					FString Name = InputTag.ToString();
					int32 Dot = INDEX_NONE;
					if (Name.FindLastChar(TEXT('.'), Dot))
					{
						Name.RightChopInline(Dot + 1);
					}
					Ended.Add(Name + TEXT(":") + StaticEnum<ECadenceArcHoldEndReason>()->GetNameStringByValue(
						static_cast<int64>(Reason)));
				});
		}

		// 每次资格结束记为 "Tag:原因"
		TArray<FString> Ended;

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
		Component->SetInputMode(Input_Heavy, ECadenceArcInputMode::HoldRelease); // Light 未配置，按 PressOnly 处理
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
		Component->PressInput(Input_Light);
		ExpectStarted(*this, TEXT("PressOnly press"), Host, {Action_Light01});
		TestTrue(TEXT("PressOnly press is tracked until release"), Component->IsInputPressed(Input_Light));
		ExpectHold(*this, TEXT("PressOnly press"), Component, false);

		// 松开只结束配对，不会再次提交
		Host.Now = 1.1;
		Component->ReleaseInput(Input_Light);
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
			Component->PressInput(Input_Heavy);
			ExpectStarted(*this, TEXT("Hold press"), Host, {});
			ExpectHold(*this, TEXT("Hold press"), Component, true);

			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy);
			ExpectStarted(*this, TEXT("Tap release"), Host, {Action_Heavy01});
			ExpectHold(*this, TEXT("Tap release"), Component, false);
			TestFalse(TEXT("Tap release ends tracking"), Component->IsInputPressed(Input_Heavy));
		}

		// 长按：Tick 推进到蓄力阶段并发出阶段事件；按住时长由 Tracker 计算，不由宿主填写
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Host.Tick(1.3);
			TestEqual(TEXT("Tick reaches Charging"),
			          static_cast<int32>(Component->GetResolver()->GetInputHoldSnapshot().Stage),
			          static_cast<int32>(ECadenceArcHoldStage::Charging));
			Host.Tick(1.6);
			TestTrue(TEXT("Stage events in order"), Host.Stages == TArray<ECadenceArcHoldStage>{
				         ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged
			         });
			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy);
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
		Component->PressInput(Input_Heavy);
		Host.Now = 2.0;
		Component->PressInput(Input_Light);
		ExpectStarted(*this, TEXT("Overdue release before new press"), Host, {Action_Heavy02});
		TestEqual(TEXT("Overdue release is executing"), Component->GetResolver()->GetCurrentActionTag().ToString(),
		          Action_Heavy02.GetTag().ToString());

		// 松开晚到：资格已经兑现过，不会再攻击一次，但配对要正常结束
		Host.Now = 2.1;
		Component->ReleaseInput(Input_Heavy);
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
		Component->PressInput(Input_Heavy);
		Host.Tick(1.4);
		ExpectStarted(*this, TEXT("Before the deadline"), Host, {});

		// Tick 推进到截止之后：自动释放一次，请求从统一出口发出
		Host.Tick(1.6);
		ExpectStarted(*this, TEXT("Tick auto release"), Host, {Action_Heavy02});

		// 重复推进和随后的松开都不会产生第二次攻击
		Host.Tick(1.7);
		Host.Now = 2.0;
		Component->ReleaseInput(Input_Heavy);
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
		Component->PressInput(Input_Heavy);
		Host.Now = 1.1;
		Component->PressInput(Input_Light);
		ExpectStarted(*this, TEXT("Light replaces holding heavy"), Host, {Action_Light01});
		TestTrue(TEXT("Both keys are tracked"),
		         Component->IsInputPressed(Input_Heavy) && Component->IsInputPressed(Input_Light));

		// Heavy 松开使用 Heavy 自己的 Token：只结束 Heavy 的配对，被替换的资格不产生攻击
		Host.Now = 1.3;
		Component->ReleaseInput(Input_Heavy);
		ExpectStarted(*this, TEXT("Replaced heavy release"), Host, {Action_Light01});
		TestFalse(TEXT("Heavy release ends heavy tracking"), Component->IsInputPressed(Input_Heavy));
		TestTrue(TEXT("Heavy release leaves light tracked"), Component->IsInputPressed(Input_Light));

		Host.Now = 1.4;
		Component->ReleaseInput(Input_Light);
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

		// Light01 执行中且窗口关闭：Heavy 申请按住资格被拒绝，但按键配对仍保留到真实松开
		Host.Now = 1.0;
		Component->PressInput(Input_Light);
		Component->ReleaseInput(Input_Light);
		if (!ExpectStarted(*this, TEXT("Light01 starts"), Host, {Action_Light01})) { return false; }
		Host.Now = 1.1;
		const FCadenceArcInputResult Rejected = Component->PressInput(Input_Heavy);
		TestEqual(TEXT("Closed window rejects the hold"), static_cast<int32>(Rejected.Reason),
		          static_cast<int32>(ECadenceArcResolutionReason::BufferWindowClosed));
		ExpectHold(*this, TEXT("Rejected begin"), Component, false);
		TestTrue(TEXT("Rejected begin keeps the physical press"), Component->IsInputPressed(Input_Heavy));
		Host.Now = 1.2;
		TestEqual(TEXT("Release after rejected begin only ends pairing"),
		          static_cast<int32>(Component->ReleaseInput(Input_Heavy).Status),
		          static_cast<int32>(ECadenceArcInputStatus::KeyReleased));
		TestFalse(TEXT("Release after rejected begin ends tracking"), Component->IsInputPressed(Input_Heavy));
		ExpectStarted(*this, TEXT("Rejected begin"), Host, {Action_Light01});
		ExpectNoResolverRelease(*this, TEXT("Release after rejected begin"), Component);

		// 重复按下被忽略，不会替换已经授予的资格
		Component->OpenBufferWindow(Host.Started[0].RequestId);
		Host.Now = 2.0;
		Component->PressInput(Input_Heavy);
		const FCadenceArcHoldSnapshot First = Component->GetResolver()->GetInputHoldSnapshot();
		Host.Now = 2.1;
		Component->PressInput(Input_Heavy);
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
			Component->PressInput(Input_Heavy);
			Host.Tick(1.3);
			Component->CancelInput(Input_Heavy);
			ExpectHold(*this, TEXT("Cancel"), Component, false);
			TestFalse(TEXT("Cancel ends tracking"), Component->IsInputPressed(Input_Heavy));

			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy);
			Host.Tick(10.0);
			ExpectStarted(*this, TEXT("After cancel"), Host, {});

			// 取消之后同一个键可以重新按下
			Host.Now = 11.0;
			Component->PressInput(Input_Heavy);
			ExpectHold(*this, TEXT("Press after cancel"), Component, true);
		}

		// 全部取消：所有按键和资格都清掉，不产生松开；之后仍可正常使用
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			TestTrue(TEXT("Heavy tracked before CancelAllInputs"), Component->IsInputPressed(Input_Heavy));

			Component->CancelAllInputs();
			ExpectHold(*this, TEXT("CancelAllInputs"), Component, false);
			TestFalse(TEXT("CancelAllInputs clears heavy"), Component->IsInputPressed(Input_Heavy));

			Host.Now = 1.2;
			TestEqual(TEXT("Release after CancelAllInputs is not paired"),
			          static_cast<int32>(Component->ReleaseInput(Input_Heavy).Status),
			          static_cast<int32>(ECadenceArcInputStatus::NotPressed));
			Host.Tick(10.0);
			ExpectStarted(*this, TEXT("After CancelAllInputs"), Host, {});

			Host.Now = 11.0;
			Component->PressInput(Input_Heavy);
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
		Component->PressInput(Input_Light);
		Host.Now = 0.5;
		Component->ReleaseInput(Input_Light);
		if (!ExpectStarted(*this, TEXT("Light01 starts"), Host, {Action_Light01})) { return false; }
		const int64 RequestId = Host.Started[0].RequestId;

		Component->OpenBufferWindow(RequestId);
		Host.Now = 1.0;
		Component->PressInput(Input_Heavy);
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
		Component->ReleaseInput(Input_Heavy);
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
		Component->PressInput(Input_Light);
		Host.Now = 1.1;
		Component->ReleaseInput(Input_Light);
		if (!ExpectStarted(*this, TEXT("First press"), Host, {Action_Light01})) { return false; }
		const int64 First = Host.Started[0].RequestId;
		Component->OpenBufferWindow(First);
		Host.Now = 1.3;
		Component->PressInputWithContext(Input_Light, FGameplayTagContainer(Context_Forward));
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
			Component->PressInput(Input_Light);
			Component->PressInput(Input_Heavy);
			TestFalse(TEXT("Uninitialized component does not track"), Component->IsInputPressed(Input_Light));
			Component->ReleaseInput(Input_Light);
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
			Component->PressInput(Input_Light);
			TestFalse(TEXT("Invalid graph does not track"), Component->IsInputPressed(Input_Light));
			ExpectStarted(*this, TEXT("Invalid graph"), Host, {});
		}

		// 重新初始化：清空旧的按键配对，旧按键的松开不产生请求
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Component->InitializeResolver(MakeComponentGraph(1.0));
			TestFalse(TEXT("Re-initialize clears tracking"), Component->IsInputPressed(Input_Heavy));
			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy);
			ExpectStarted(*this, TEXT("Release after re-initialize"), Host, {});
		}
		return !HasAnyErrors();
	}

	// ---- 输入方式配置、上下文提供者、输入结果和按住结束通知 ----

	static int32 StatusOf(const FCadenceArcInputResult& Result) { return static_cast<int32>(Result.Status); }
	static int32 StatusValue(const ECadenceArcInputStatus Status) { return static_cast<int32>(Status); }

	// 调试历史里最后一次提交（bRelease 为 false）或松手（true）记录的事件上下文。
	// 调试历史只在编辑器构建中存在，其他构建返回空值，调用方跳过检查。
	static TOptional<FGameplayTagContainer> LastInputContext(const UCadenceArcComponent* Component, const bool bRelease)
	{
		TOptional<FGameplayTagContainer> Found;
#if WITH_EDITOR
		const ECadenceArcDebugOperation Operation =
			bRelease ? ECadenceArcDebugOperation::ReleaseHold : ECadenceArcDebugOperation::SubmitInput;
		TArray<FCadenceArcDebugEvent> Events;
		Component->GetResolver()->GetDebugHistory().CopyEventsAfter(0, Events);
		for (const FCadenceArcDebugEvent& Event : Events)
		{
			if (Event.Operation == Operation)
			{
				Found = Event.InputContextTags;
			}
		}
#endif
		return Found;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentModeConfigTest,
		"CadenceArc.Component.Input.ModeConfiguredOnce",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentModeConfigTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
		TestEqual(TEXT("Unconfigured tag is PressOnly"), static_cast<int32>(Component->GetInputMode(Input_Light)),
		          static_cast<int32>(ECadenceArcInputMode::PressOnly));
		TestEqual(TEXT("Configured tag keeps its mode"), static_cast<int32>(Component->GetInputMode(Input_Heavy)),
		          static_cast<int32>(ECadenceArcInputMode::HoldRelease));

		// 不需要在每次按下时传入方式：Heavy 按配置申请按住资格，松开时解析
		Host.Now = 1.0;
		TestEqual(TEXT("Configured hold press"), StatusOf(Component->PressInput(Input_Heavy)),
		          StatusValue(ECadenceArcInputStatus::HoldGranted));
		Host.Now = 1.1;
		TestEqual(TEXT("Configured hold release"), StatusOf(Component->ReleaseInput(Input_Heavy)),
		          StatusValue(ECadenceArcInputStatus::RequestProduced));
		ExpectStarted(*this, TEXT("Configured hold"), Host, {Action_Heavy01});
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentHoldFallbackTest,
		"CadenceArc.Component.Input.HoldReleaseFallsBackToPress",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentHoldFallbackTest::RunTest(const FString& Parameters)
	{
		// Root 上 Heavy 只有 Pressed 转移：HoldRelease 的键不申请资格，按下立即提交
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		FCadenceArcNode& Root = Graph->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = Action_Root;
		Root.Transitions.Add(MakeComponentEdge(Input_Heavy, Action_Heavy01, ECadenceArcInputPhase::Pressed));
		Graph->Nodes.AddDefaulted_GetRef().ActionTag = Action_Heavy01;

		FComponentHost Host;
		UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
		Host.Bind(Component);
		Component->SetInputMode(Input_Heavy, ECadenceArcInputMode::HoldIfAvailable);
		if (!TestEqual(TEXT("Fallback graph initializes"), static_cast<int32>(Component->InitializeResolver(Graph)),
		               static_cast<int32>(ECadenceArcResolverInitResult::Success)))
		{
			return false;
		}

		// 同一张图上，严格的 HoldRelease 不回退：申请被拒绝，按键配对保留到松开
		{
			FComponentHost StrictHost;
			UCadenceArcComponent* Strict = NewObject<UCadenceArcComponent>();
			StrictHost.Bind(Strict);
			Strict->SetInputMode(Input_Heavy, ECadenceArcInputMode::HoldRelease);
			Strict->InitializeResolver(Graph);
			StrictHost.Now = 1.0;
			const FCadenceArcInputResult StrictPress = Strict->PressInput(Input_Heavy);
			TestEqual(TEXT("Strict hold has no action"), StatusOf(StrictPress), StatusValue(ECadenceArcInputStatus::NoAction));
			TestEqual(TEXT("Strict hold reason"), static_cast<int32>(StrictPress.Reason),
			          static_cast<int32>(ECadenceArcResolutionReason::NoMatchingTransition));
			TestTrue(TEXT("Strict hold keeps the pairing"), Strict->IsInputPressed(Input_Heavy));
			ExpectStarted(*this, TEXT("Strict hold"), StrictHost, {});
		}

		Host.Now = 1.0;
		TestEqual(TEXT("Press resolves immediately"), StatusOf(Component->PressInput(Input_Heavy)),
		          StatusValue(ECadenceArcInputStatus::RequestProduced));
		if (!ExpectStarted(*this, TEXT("Fallback press"), Host, {Action_Heavy01})) { return false; }
		ExpectHold(*this, TEXT("Fallback press"), Component, false);

		// 长按之后松开只结束配对，不会再提交一次
		Host.Tick(1.8);
		Host.Now = 2.0;
		TestEqual(TEXT("Release only ends pairing"), StatusOf(Component->ReleaseInput(Input_Heavy)),
		          StatusValue(ECadenceArcInputStatus::KeyReleased));
		ExpectStarted(*this, TEXT("Fallback release"), Host, {Action_Heavy01});
		ExpectNoResolverRelease(*this, TEXT("Fallback release"), Component);

		// Heavy01 是终止节点：完成后再按，没有可走的转移
		Component->NotifyActionCompleted(Host.Started[0].RequestId);
		Host.Now = 2.5;
		const FCadenceArcInputResult NoEdge = Component->PressInput(Input_Heavy);
		TestEqual(TEXT("Leaf press has no action"), StatusOf(NoEdge), StatusValue(ECadenceArcInputStatus::NoAction));
		TestEqual(TEXT("Leaf press reason"), static_cast<int32>(NoEdge.Reason),
		          static_cast<int32>(ECadenceArcResolutionReason::NoMatchingTransition));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentInputResultTest,
		"CadenceArc.Component.Input.Results",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentInputResultTest::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("Uninitialized"), StatusOf(NewObject<UCadenceArcComponent>()->PressInput(Input_Light)),
		          StatusValue(ECadenceArcInputStatus::NotInitialized));

		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			const FCadenceArcInputResult Produced = Component->PressInput(Input_Light);
			TestEqual(TEXT("Request produced"), StatusOf(Produced), StatusValue(ECadenceArcInputStatus::RequestProduced));
			TestTrue(TEXT("Produced is accepted"), Produced.IsAccepted());
			Host.Now = 1.05;
			TestEqual(TEXT("Repeated press"), StatusOf(Component->PressInput(Input_Light)),
			          StatusValue(ECadenceArcInputStatus::AlreadyPressed));
			TestEqual(TEXT("Release of an unpressed key"), StatusOf(Component->ReleaseInput(Input_Heavy)),
			          StatusValue(ECadenceArcInputStatus::NotPressed));
			TestEqual(TEXT("PressOnly release"), StatusOf(Component->ReleaseInput(Input_Light)),
			          StatusValue(ECadenceArcInputStatus::KeyReleased));

			if (!TestTrue(TEXT("A request was started"), Host.Started.Num() > 0)) { return false; }
			Component->OpenBufferWindow(Host.Started[0].RequestId);
			Host.Now = 1.2;
			const FCadenceArcInputResult Buffered = Component->PressInput(Input_Light);
			TestEqual(TEXT("Buffered"), StatusOf(Buffered), StatusValue(ECadenceArcInputStatus::Buffered));
			TestTrue(TEXT("Buffered is accepted"), Buffered.IsAccepted());
			ExpectStarted(*this, TEXT("Results"), Host, {Action_Light01});
		}

		// 蓄力中的保护：其他输入被拒绝，原因是 HoldProtected
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Host.Tick(1.3);
			const FCadenceArcInputResult Protected = Component->PressInput(Input_Light);
			TestEqual(TEXT("Charging protects the hold"), StatusOf(Protected), StatusValue(ECadenceArcInputStatus::Rejected));
			TestEqual(TEXT("Protection reason"), static_cast<int32>(Protected.Reason),
			          static_cast<int32>(ECadenceArcResolutionReason::HoldProtected));
			TestFalse(TEXT("Rejected is not accepted"), Protected.IsAccepted());
			ExpectHold(*this, TEXT("Protected hold"), Component, true);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentContextProviderTest,
		"CadenceArc.Component.Input.ContextProvider",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentContextProviderTest::RunTest(const FString& Parameters)
	{
		FComponentHost Host;
		UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
		int32 Calls = 0;
		Component->SetContextProviderFunction([&Calls](FGameplayTag, const ECadenceArcInputPhase Phase)
		{
			++Calls;
			return FGameplayTagContainer(Phase == ECadenceArcInputPhase::Pressed ? Context_Forward : Context_Air);
		});

		// 省略上下文：按下时调用一次提供者，结果写进输入事件
		Host.Now = 1.0;
		Component->PressInput(Input_Light);
		TestEqual(TEXT("Provider called once on press"), Calls, 1);
		if (const TOptional<FGameplayTagContainer> Submit = LastInputContext(Component, false))
		{
			TestTrue(TEXT("Press carries the provided context"), Submit->HasTagExact(Context_Forward));
		}
		Component->ReleaseInput(Input_Light);
		TestEqual(TEXT("PressOnly release does not collect"), Calls, 1);

		// 显式快照：不调用提供者，空容器表示没有上下文
		if (!TestTrue(TEXT("A request was started"), Host.Started.Num() > 0)) { return false; }
		Component->OpenBufferWindow(Host.Started[0].RequestId);
		Host.Now = 1.2;
		Component->PressInputWithContext(Input_Light, FGameplayTagContainer());
		TestEqual(TEXT("Explicit context skips the provider"), Calls, 1);
		if (const TOptional<FGameplayTagContainer> Submit = LastInputContext(Component, false))
		{
			TestTrue(TEXT("Explicit empty context stays empty"), Submit->IsEmpty());
		}

		// 按住：松开时再采集一次，使用松开阶段的上下文
		FComponentHost HoldHost;
		UCadenceArcComponent* HoldComponent = MakeHostedComponent(*this, HoldHost);
		int32 HoldCalls = 0;
		HoldComponent->SetContextProviderFunction([&HoldCalls](FGameplayTag, const ECadenceArcInputPhase Phase)
		{
			++HoldCalls;
			return FGameplayTagContainer(Phase == ECadenceArcInputPhase::Pressed ? Context_Forward : Context_Air);
		});
		HoldHost.Now = 1.0;
		HoldComponent->PressInput(Input_Heavy);
		HoldHost.Now = 1.1;
		HoldComponent->ReleaseInput(Input_Heavy);
		TestEqual(TEXT("Provider called on press and release"), HoldCalls, 2);
		if (const TOptional<FGameplayTagContainer> Release = LastInputContext(HoldComponent, true))
		{
			TestTrue(TEXT("Release carries the release context"), Release->HasTagExact(Context_Air));
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentPersistentContextTest,
		"CadenceArc.Component.PersistentContextBeforeInitialize",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentPersistentContextTest::RunTest(const FString& Parameters)
	{
		// 解析器创建之前设置的持久上下文不会丢失，初始化和重新初始化后都生效
		UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
		Component->SetContextTags(FGameplayTagContainer(Context_Air));
		TestTrue(TEXT("Context is readable before initialization"), Component->GetContextTags().HasTagExact(Context_Air));
		Component->InitializeResolver(MakeComponentGraph(1.0));
		TestTrue(TEXT("Resolver receives the early context"),
		         Component->GetResolver()->GetContextTags().HasTagExact(Context_Air));
		Component->InitializeResolver(MakeComponentGraph(1.0));
		TestTrue(TEXT("Re-initialization keeps the context"),
		         Component->GetResolver()->GetContextTags().HasTagExact(Context_Air));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentHoldEndedTest,
		"CadenceArc.Component.HoldEndedOncePerHold",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentHoldEndedTest::RunTest(const FString& Parameters)
	{
		const auto ExpectEnded = [this](const TCHAR* What, const FComponentHost& Host, const TArray<FString>& Expected)
		{
			TestEqual(*FString::Printf(TEXT("%s hold endings"), What), FString::Join(Host.Ended, TEXT(", ")),
			          FString::Join(Expected, TEXT(", ")));
		};

		// 手动松开
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			ExpectEnded(TEXT("Granted"), Host, {});
			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy);
			ExpectEnded(TEXT("Released"), Host, {TEXT("Heavy:Released")});
		}
		// 自动释放，之后的松开不再通知
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host, 0.0);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Host.Tick(1.6);
			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy);
			ExpectEnded(TEXT("AutoReleased"), Host, {TEXT("Heavy:AutoReleased")});
		}
		// 取消，之后的松开不再通知
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Component->CancelInput(Input_Heavy);
			Host.Now = 1.1;
			Component->ReleaseInput(Input_Heavy);
			ExpectEnded(TEXT("Cancelled"), Host, {TEXT("Heavy:Cancelled")});
		}
		// 被另一个输入替换
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Host.Now = 1.1;
			Component->PressInput(Input_Light);
			Host.Now = 1.2;
			Component->ReleaseInput(Input_Heavy);
			ExpectEnded(TEXT("Replaced"), Host, {TEXT("Heavy:Replaced")});
		}
		// 重置连招
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Component->ResetCombo();
			ExpectEnded(TEXT("Cleared"), Host, {TEXT("Heavy:Cleared")});
		}
		// 完成回调保留资格：直到松开才结束
		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			Host.Now = 0.4;
			Component->PressInput(Input_Light);
			Component->ReleaseInput(Input_Light);
			if (!TestTrue(TEXT("A request was started"), Host.Started.Num() > 0)) { return false; }
			const int64 RequestId = Host.Started[0].RequestId;
			Component->OpenBufferWindow(RequestId);
			Host.Now = 1.0;
			Component->PressInput(Input_Heavy);
			Host.Now = 1.4;
			Component->NotifyActionCompleted(RequestId);
			ExpectEnded(TEXT("Across completion"), Host, {});
			Host.Now = 2.0;
			Component->ReleaseInput(Input_Heavy);
			ExpectEnded(TEXT("Release after completion"), Host, {TEXT("Heavy:Released")});
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcComponentStatusTest,
		"CadenceArc.Component.StatusQueriesAndMissingExecutor",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcComponentStatusTest::RunTest(const FString& Parameters)
	{
		// 普通 UI 不需要访问解析器就能读到状态
		UCadenceArcComponent* Unready = NewObject<UCadenceArcComponent>();
		TestFalse(TEXT("Not initialized"), Unready->IsInitialized());
		TestEqual(TEXT("Uninitialized state"), static_cast<int32>(Unready->GetState()),
		          static_cast<int32>(ECadenceArcResolverState::Uninitialized));
		TestFalse(TEXT("No current action"), Unready->GetCurrentActionTag().IsValid());

		{
			FComponentHost Host;
			UCadenceArcComponent* Component = MakeHostedComponent(*this, Host);
			TestTrue(TEXT("Initialized"), Component->IsInitialized());
			TestEqual(TEXT("Ready at entry"), Component->GetCurrentActionTag().ToString(), Action_Root.GetTag().ToString());
			Host.Now = 1.0;
			Component->PressInput(Input_Light);
			TestEqual(TEXT("Executing after start"), static_cast<int32>(Component->GetState()),
			          static_cast<int32>(ECadenceArcResolverState::Executing));
			TestEqual(TEXT("Current action follows the start"), Component->GetCurrentActionTag().ToString(),
			          Action_Light01.GetTag().ToString());
		}

		// 没有执行器订阅请求：给出诊断，请求停在 AwaitingStart
		{
			UCadenceArcComponent* Component = NewObject<UCadenceArcComponent>();
			Component->SetTimeSource([]() { return 1.0; });
			Component->InitializeResolver(MakeComponentGraph(1.0));
			AddExpectedMessage(TEXT("has no handler"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			Component->PressInput(Input_Light);
			TestEqual(TEXT("Request waits without an executor"), static_cast<int32>(Component->GetState()),
			          static_cast<int32>(ECadenceArcResolverState::AwaitingStart));
		}
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
