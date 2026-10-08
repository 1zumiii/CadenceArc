// UCadenceArcInputBinderComponent 的测试。组件不注册到 World，直接执行 UEnhancedInputComponent 上的绑定来模拟触发事件。
//
// 被测契约：
// - Started 调用 PressInput，Completed 调用 ReleaseInput，Canceled 调用 CancelInput（不产生松手）。
// - 绑定时把 ActionSet 中的输入方式写入目标组件；无效和重复的条目被跳过。
// - 重复绑定先解除上一次绑定，并取消上一次绑定中仍处于按下状态的输入。
// - CancelBoundInputs 只取消本组件绑定的输入。
// - 放在 Pawn 上时，Pawn Restart 后自动绑定到它的输入组件，不重复绑定；控制器变化时取消按住中的输入。

#if WITH_DEV_AUTOMATION_TESTS

#include "CadenceArcInputActionSet.h"
#include "CadenceArcInputBinderComponent.h"
#include "Component/CadenceArcComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Graph/CadenceArcGraph.h"
#include "InputAction.h"
#include "Misc/AutomationTest.h"
#include "Tests/CadenceArcAutomationTags.h"
#include "Resolver/CadenceArcResolver.h"

namespace CadenceArc::Tests::EnhancedInput
{
	static const FAutomationTag Action_Root{TEXT("CadenceArc.Automation.EnhancedInput.Action.Root")};
	static const FAutomationTag Action_Light{TEXT("CadenceArc.Automation.EnhancedInput.Action.Light")};
	static const FAutomationTag Action_Heavy{TEXT("CadenceArc.Automation.EnhancedInput.Action.Heavy")};
	static const FAutomationTag Input_Light{TEXT("CadenceArc.Automation.EnhancedInput.Input.Light")};
	static const FAutomationTag Input_Heavy{TEXT("CadenceArc.Automation.EnhancedInput.Input.Heavy")};
	static const FAutomationTag Input_Other{TEXT("CadenceArc.Automation.EnhancedInput.Input.Other")};

	// Root：Light 按下 -> Light，Heavy 松开 -> Heavy。两个目标都是叶子节点。
	static UCadenceArcGraph* MakeGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		FCadenceArcNode& Root = Graph->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = Action_Root;
		FCadenceArcTransition& LightEdge = Root.Transitions.AddDefaulted_GetRef();
		LightEdge.InputTag = Input_Light;
		LightEdge.TargetActionTag = Action_Light;
		LightEdge.InputPhase = ECadenceArcInputPhase::Pressed;
		FCadenceArcTransition& HeavyEdge = Root.Transitions.AddDefaulted_GetRef();
		HeavyEdge.InputTag = Input_Heavy;
		HeavyEdge.TargetActionTag = Action_Heavy;
		HeavyEdge.InputPhase = ECadenceArcInputPhase::Released;
		Graph->Nodes.AddDefaulted_GetRef().ActionTag = Action_Light;
		Graph->Nodes.AddDefaulted_GetRef().ActionTag = Action_Heavy;
		return Graph;
	}

	struct FBinderFixture
	{
		UCadenceArcComponent* Target = nullptr;
		UCadenceArcInputBinderComponent* Binder = nullptr;
		UEnhancedInputComponent* InputComponent = nullptr;
		UCadenceArcInputActionSet* ActionSet = nullptr;
		UInputAction* LightAction = nullptr;
		UInputAction* HeavyAction = nullptr;
		double Now = 1.0;
		int64 LastRequestId = 0;
		TArray<FGameplayTag> Requested;

		explicit FBinderFixture(FAutomationTestBase& Test)
		{
			Target = NewObject<UCadenceArcComponent>();
			Target->SetTimeSource([this]() { return Now; });
			Target->OnActionRequestedNative.AddLambda([this](const FCadenceArcActionRequest& Request)
			{
				Requested.Add(Request.TargetActionTag);
				LastRequestId = Request.RequestId;
				Target->NotifyActionStarted(Request.RequestId);
			});
			Test.TestEqual(TEXT("Graph initializes"), static_cast<int32>(Target->InitializeResolver(MakeGraph())),
			               static_cast<int32>(ECadenceArcResolverInitResult::Success));

			LightAction = NewObject<UInputAction>();
			HeavyAction = NewObject<UInputAction>();
			ActionSet = NewObject<UCadenceArcInputActionSet>();
			AddMapping(LightAction, Input_Light, ECadenceArcInputMode::PressOnly);
			AddMapping(HeavyAction, Input_Heavy, ECadenceArcInputMode::HoldRelease);

			Binder = NewObject<UCadenceArcInputBinderComponent>();
			Binder->SetTargetComponent(Target);
			InputComponent = NewObject<UEnhancedInputComponent>();
		}

		void AddMapping(UInputAction* Action, const FGameplayTag& Tag, const ECadenceArcInputMode Mode)
		{
			FCadenceArcInputActionBinding& Binding = ActionSet->InputActions.AddDefaulted_GetRef();
			Binding.InputAction = Action;
			Binding.InputTag = Tag;
			Binding.InputMode = Mode;
		}

		// 执行输入组件上所有匹配的绑定，返回执行的数量
		static int32 Fire(const UEnhancedInputComponent* Component, const UInputAction* Action, const ETriggerEvent Event)
		{
			int32 Count = 0;
			const FInputActionInstance Instance(Action);
			for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Component->GetActionEventBindings())
			{
				if (Binding->GetAction() == Action && Binding->GetTriggerEvent() == Event)
				{
					Binding->Execute(Instance);
					++Count;
				}
			}
			return Count;
		}

		int32 Fire(const UInputAction* Action, const ETriggerEvent Event) const
		{
			return Fire(InputComponent, Action, Event);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputBinderEventsTest,
		"CadenceArc.EnhancedInput.Binder.TriggerEventsMapToComponent",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputBinderEventsTest::RunTest(const FString& Parameters)
	{
		FBinderFixture Fixture(*this);
		TestEqual(TEXT("Both mappings bind"), Fixture.Binder->BindInputActions(Fixture.InputComponent, Fixture.ActionSet),
		          2);
		TestTrue(TEXT("Binder reports bound"), Fixture.Binder->IsBound());
		TestEqual(TEXT("Three trigger events per mapping"), Fixture.InputComponent->GetActionEventBindings().Num(), 6);
		TestEqual(TEXT("Mode written from action set"),
		          static_cast<int32>(Fixture.Target->GetInputMode(Input_Heavy)),
		          static_cast<int32>(ECadenceArcInputMode::HoldRelease));

		// Heavy：Started 取得按住资格，Canceled 取消资格，不产生松手请求
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Started);
		TestTrue(TEXT("Started presses Heavy"), Fixture.Target->IsInputPressed(Input_Heavy));
		TestTrue(TEXT("Heavy holds"), Fixture.Target->GetResolver()->GetInputHoldSnapshot().bHasHold);
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Canceled);
		TestFalse(TEXT("Canceled ends the Heavy press"), Fixture.Target->IsInputPressed(Input_Heavy));
		TestFalse(TEXT("Canceled drops the hold"), Fixture.Target->GetResolver()->GetInputHoldSnapshot().bHasHold);
		TestEqual(TEXT("Canceled produces no request"), Fixture.Requested.Num(), 0);

		// Heavy：Started 后 Completed 在松开时提交
		Fixture.Now = 2.0;
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Started);
		Fixture.Now = 2.25;
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Completed);
		TestFalse(TEXT("Completed ends the Heavy press"), Fixture.Target->IsInputPressed(Input_Heavy));
		if (TestEqual(TEXT("Completed releases Heavy"), Fixture.Requested.Num(), 1))
		{
			TestEqual(TEXT("Release target"), Fixture.Requested[0].ToString(), Action_Heavy.GetTag().ToString());
		}

		// Light：PressOnly 在 Started 时提交
		Fixture.Target->NotifyActionCompleted(Fixture.LastRequestId);
		Fixture.Target->ResetCombo();
		Fixture.Now = 3.0;
		Fixture.Fire(Fixture.LightAction, ETriggerEvent::Started);
		if (TestEqual(TEXT("Started presses Light"), Fixture.Requested.Num(), 2))
		{
			TestEqual(TEXT("Press target"), Fixture.Requested[1].ToString(), Action_Light.GetTag().ToString());
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputBinderSkipsInvalidTest,
		"CadenceArc.EnhancedInput.Binder.SkipsInvalidAndDuplicateEntries",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputBinderSkipsInvalidTest::RunTest(const FString& Parameters)
	{
		FBinderFixture Fixture(*this);
		UInputAction* OtherAction = NewObject<UInputAction>();
		Fixture.AddMapping(nullptr, Input_Other, ECadenceArcInputMode::PressOnly); // 没有 Input Action
		Fixture.AddMapping(OtherAction, FGameplayTag(), ECadenceArcInputMode::PressOnly); // 没有 Tag
		Fixture.AddMapping(OtherAction, Input_Light, ECadenceArcInputMode::HoldRelease); // Tag 重复
		Fixture.AddMapping(Fixture.LightAction, Input_Other, ECadenceArcInputMode::PressOnly); // Action 重复

		AddExpectedMessage(TEXT("skipped an entry without Input Action or input tag"), EAutomationExpectedMessageFlags::Contains, 2);
		AddExpectedMessage(TEXT("skipped duplicate mapping"), EAutomationExpectedMessageFlags::Contains, 2);
		TestEqual(TEXT("Only the two valid mappings bind"),
		          Fixture.Binder->BindInputActions(Fixture.InputComponent, Fixture.ActionSet), 2);
		TestEqual(TEXT("Skipped entries add no bindings"), Fixture.InputComponent->GetActionEventBindings().Num(), 6);
		TestEqual(TEXT("Duplicate tag does not override the first mode"),
		          static_cast<int32>(Fixture.Target->GetInputMode(Input_Light)),
		          static_cast<int32>(ECadenceArcInputMode::PressOnly));
		TestEqual(TEXT("Skipped action has no bindings"), FBinderFixture::Fire(Fixture.InputComponent, OtherAction,
		          ETriggerEvent::Started), 0);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputBinderRebindTest,
		"CadenceArc.EnhancedInput.Binder.RebindReplacesPreviousBinding",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputBinderRebindTest::RunTest(const FString& Parameters)
	{
		FBinderFixture Fixture(*this);
		Fixture.Binder->BindInputActions(Fixture.InputComponent, Fixture.ActionSet);

		// 同一个输入组件重复绑定：旧绑定被移除，一次 Started 只按下一次
		Fixture.Binder->BindInputActions(Fixture.InputComponent);
		TestEqual(TEXT("Rebinding the same component keeps one set of bindings"),
		          Fixture.InputComponent->GetActionEventBindings().Num(), 6);
		TestEqual(TEXT("One Started reaches the component once"), Fixture.Fire(Fixture.LightAction, ETriggerEvent::Started), 1);
		TestEqual(TEXT("One request"), Fixture.Requested.Num(), 1);
		Fixture.Fire(Fixture.LightAction, ETriggerEvent::Completed);

		// 按住 Heavy 时换到新的输入组件（重新控制 Pawn）：旧组件不会再发 Completed，按下被取消
		Fixture.Target->NotifyActionCompleted(Fixture.LastRequestId);
		Fixture.Target->ResetCombo();
		Fixture.Now = 2.0;
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Started);
		TestTrue(TEXT("Heavy pressed before rebinding"), Fixture.Target->IsInputPressed(Input_Heavy));
		UEnhancedInputComponent* NewInputComponent = NewObject<UEnhancedInputComponent>();
		Fixture.Binder->BindInputActions(NewInputComponent);
		TestFalse(TEXT("Rebinding cancels the held Heavy"), Fixture.Target->IsInputPressed(Input_Heavy));
		TestFalse(TEXT("Rebinding drops the hold"), Fixture.Target->GetResolver()->GetInputHoldSnapshot().bHasHold);
		TestEqual(TEXT("Old component bindings removed"), Fixture.InputComponent->GetActionEventBindings().Num(), 0);
		TestEqual(TEXT("New component bound"), NewInputComponent->GetActionEventBindings().Num(), 6);

		// 解除绑定后不再响应
		Fixture.Binder->UnbindInputActions();
		TestFalse(TEXT("Unbound"), Fixture.Binder->IsBound());
		TestEqual(TEXT("Unbind removes bindings"), NewInputComponent->GetActionEventBindings().Num(), 0);
		TestEqual(TEXT("No request from the cancelled hold"), Fixture.Requested.Num(), 1);
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputBinderCancelBoundTest,
		"CadenceArc.EnhancedInput.Binder.CancelBoundInputsOnlyTouchesBoundTags",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputBinderCancelBoundTest::RunTest(const FString& Parameters)
	{
		FBinderFixture Fixture(*this);
		Fixture.Binder->BindInputActions(Fixture.InputComponent, Fixture.ActionSet);

		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Started);
		Fixture.Target->PressInput(Input_Other); // 其他来源的输入，没有对应的转移，只记录配对
		TestTrue(TEXT("Other input pressed"), Fixture.Target->IsInputPressed(Input_Other));

		Fixture.Binder->CancelBoundInputs();
		TestFalse(TEXT("Bound Heavy cancelled"), Fixture.Target->IsInputPressed(Input_Heavy));
		TestTrue(TEXT("Other source untouched"), Fixture.Target->IsInputPressed(Input_Other));
		TestTrue(TEXT("Bindings kept"), Fixture.Binder->IsBound());

		// 取消后真实的 Completed 到达：组件把它当作没有按下的松开，不产生请求
		Fixture.Fire(Fixture.HeavyAction, ETriggerEvent::Completed);
		TestEqual(TEXT("Late Completed produces no request"), Fixture.Requested.Num(), 0);
		return !HasAnyErrors();
	}
	// 只用于这个测试的 Game World。没有 BeginPlay 和 Tick，Pawn 的 Restart 和控制器变化由测试直接触发。
	struct FScopedBinderWorld
	{
		UWorld* World = nullptr;

		FScopedBinderWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CadenceArcBinderTestWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		}

		~FScopedBinderWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputBinderAutoBindTest,
		"CadenceArc.EnhancedInput.Binder.AutoBindsOnPawnRestart",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputBinderAutoBindTest::RunTest(const FString& Parameters)
	{
		FBinderFixture Fixture(*this); // 只借用其中的动作图、Input Action 和 ActionSet
		FScopedBinderWorld TestWorld;
		APawn* Pawn = TestWorld.World->SpawnActor<APawn>();
		if (!TestNotNull(TEXT("Pawn spawned"), Pawn))
		{
			return false;
		}

		UCadenceArcComponent* Target = NewObject<UCadenceArcComponent>(Pawn);
		Target->RegisterComponent();
		double Now = 1.0;
		Target->SetTimeSource([&Now]() { return Now; });
		Target->InitializeResolver(MakeGraph());

		UCadenceArcInputBinderComponent* Binder = NewObject<UCadenceArcInputBinderComponent>(Pawn);
		Binder->ActionSet = Fixture.ActionSet;
		Binder->RegisterComponent();
		TestFalse(TEXT("No input component yet, nothing bound"), Binder->IsBound());
		TestTrue(TEXT("Target found on the owner"), Binder->GetTargetComponent() == Target);

		// 模拟玩家控制：PawnClientRestart 创建输入组件，然后广播 Restarted
		UEnhancedInputComponent* InputComponent = NewObject<UEnhancedInputComponent>(Pawn);
		Pawn->InputComponent = InputComponent;
		Pawn->DispatchRestart(false);
		TestTrue(TEXT("Restart binds automatically"), Binder->IsBound());
		TestEqual(TEXT("Bound to the pawn input component"), InputComponent->GetActionEventBindings().Num(), 6);
		Pawn->DispatchRestart(false);
		TestEqual(TEXT("Second restart does not bind twice"), InputComponent->GetActionEventBindings().Num(), 6);

		// 同一个输入组件上的绑定可能在 Pawn 初始化期间被清空；旧 handle 不能当作仍然绑定。
		InputComponent->ClearActionEventBindings();
		TestFalse(TEXT("Cleared bindings invalidate IsBound even when the input component is unchanged"), Binder->IsBound());
		Pawn->DispatchRestart(false);
		TestTrue(TEXT("Restart restores bindings removed by initialization"), Binder->IsBound());
		TestEqual(TEXT("Restored exactly one set of bindings"), InputComponent->GetActionEventBindings().Num(), 6);

		// 按住时控制器变化：输入组件不会再发 Completed，按下被取消
		FBinderFixture::Fire(InputComponent, Fixture.HeavyAction, ETriggerEvent::Started);
		TestTrue(TEXT("Heavy pressed through auto binding"), Target->IsInputPressed(Input_Heavy));
		Pawn->NotifyControllerChanged();
		TestFalse(TEXT("Controller change cancels the held Heavy"), Target->IsInputPressed(Input_Heavy));

		// 关闭自动绑定后，新的 Restart 不再绑定
		Binder->UnbindInputActions();
		Binder->bAutoBind = false;
		Pawn->DispatchRestart(false);
		TestFalse(TEXT("bAutoBind false skips restart binding"), Binder->IsBound());

		// 手动绑定时省略输入组件，使用所属 Actor 的输入组件
		TestEqual(TEXT("Manual bind defaults to the owner input component"), Binder->BindInputActions(), 2);
		TestEqual(TEXT("Owner input component bound"), InputComponent->GetActionEventBindings().Num(), 6);
		return !HasAnyErrors();
	}
}

#endif
