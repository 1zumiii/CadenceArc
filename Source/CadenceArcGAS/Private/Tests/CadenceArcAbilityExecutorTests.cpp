// UCadenceArcAbilityExecutorComponent 的测试。在独立的 Game World 中生成 Actor，挂上真实的 Ability System 组件、
// CadenceArc 组件和执行器，授予测试 Ability 后通过 PressInput 驱动。
//
// 被测契约：
// - 请求激活 AssetTags 含 TargetActionTag 的 Ability；激活成功 Started，激活失败 Rejected。
// - Ability 正常结束 Completed，被取消 Interrupted；在激活过程中结束时，先 Started 再报告结束。
// - 没有匹配或匹配多个 Ability 时拒绝请求。
// - 完成时消费缓冲产生的下一个请求，在结束回调中激活下一个 Ability，包括同一个 Ability。
// - 蒙太奇通知按动画记下请求编号，旧动作迟到的关闭不影响新动作的窗口。

#if WITH_DEV_AUTOMATION_TESTS

#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "CadenceArcAbilityExecutorComponent.h"
#include "CadenceArcGASTestAbilities.h"
#include "Component/CadenceArcComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"

namespace CadenceArc::Tests::GAS
{
	static void AddEdge(FCadenceArcNode& Node, const FGameplayTag& Input, const FGameplayTag& Target)
	{
		FCadenceArcTransition& Edge = Node.Transitions.AddDefaulted_GetRef();
		Edge.InputTag = Input;
		Edge.TargetActionTag = Target;
		Edge.InputPhase = ECadenceArcInputPhase::Pressed;
	}

	// Root 通向每种 Ability；A 可以接 B 或再接 A，B 可以接 A
	static UCadenceArcGraph* MakeGraph()
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->Nodes.Reserve(6);
		FCadenceArcNode& Root = Graph->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = Action_Root;
		AddEdge(Root, Input_A, Action_A);
		AddEdge(Root, Input_Instant, Action_Instant);
		AddEdge(Root, Input_Blocked, Action_Blocked);
		AddEdge(Root, Input_Missing, Action_Missing);
		FCadenceArcNode& A = Graph->Nodes.AddDefaulted_GetRef();
		A.ActionTag = Action_A;
		AddEdge(A, Input_B, Action_B);
		AddEdge(A, Input_A, Action_A);
		FCadenceArcNode& B = Graph->Nodes.AddDefaulted_GetRef();
		B.ActionTag = Action_B;
		AddEdge(B, Input_A, Action_A);
		for (const FGameplayTag& Leaf : TArray<FGameplayTag>{Action_Instant, Action_Blocked, Action_Missing})
		{
			Graph->Nodes.AddDefaulted_GetRef().ActionTag = Leaf;
		}
		return Graph;
	}

	struct FGASFixture
	{
		UWorld* World = nullptr;
		AActor* Actor = nullptr;
		UAbilitySystemComponent* AbilitySystem = nullptr;
		UCadenceArcComponent* Arc = nullptr;
		UCadenceArcAbilityExecutorComponent* Executor = nullptr;
		double Now = 1.0;

		explicit FGASFixture(const TArray<TSubclassOf<UGameplayAbility>>& Abilities)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("CadenceArcGASTestWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Actor = World->SpawnActor<AActor>();

			AbilitySystem = NewObject<UAbilitySystemComponent>(Actor);
			AbilitySystem->RegisterComponent();
			AbilitySystem->InitAbilityActorInfo(Actor, Actor);
			for (const TSubclassOf<UGameplayAbility>& Ability : Abilities)
			{
				AbilitySystem->GiveAbility(FGameplayAbilitySpec(Ability));
			}

			Arc = NewObject<UCadenceArcComponent>(Actor);
			Arc->Graph = MakeGraph();
			Arc->SetTimeSource([this]() { return Now; });
			Arc->RegisterComponent();

			Executor = NewObject<UCadenceArcAbilityExecutorComponent>(Actor);
			Executor->RegisterComponent();
			Actor->DispatchBeginPlay(); // CadenceArc 组件初始化解析器，执行器绑定请求出口
		}

		~FGASFixture()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		FGameplayAbilitySpec* FindSpec(const TSubclassOf<UGameplayAbility>& Class) const
		{
			return AbilitySystem->FindAbilitySpecFromClass(Class);
		}

		bool IsActive(const TSubclassOf<UGameplayAbility>& Class) const
		{
			const FGameplayAbilitySpec* Spec = FindSpec(Class);
			return Spec && Spec->IsActive();
		}

		void Finish(const TSubclassOf<UGameplayAbility>& Class) const
		{
			const FGameplayAbilitySpec* Spec = FindSpec(Class);
			if (UCadenceArcTestLatentAbility* Instance = Spec ? Cast<UCadenceArcTestLatentAbility>(Spec->GetPrimaryInstance()) : nullptr)
			{
				Instance->FinishForTest();
			}
		}

		void Cancel(const TSubclassOf<UGameplayAbility>& Class) const
		{
			if (const FGameplayAbilitySpec* Spec = FindSpec(Class))
			{
				AbilitySystem->CancelAbilityHandle(Spec->Handle);
			}
		}

		FString State() const { return StaticEnum<ECadenceArcResolverState>()->GetNameStringByValue(static_cast<int64>(Arc->GetState())); }
		FString Current() const { return Arc->GetCurrentActionTag().ToString(); }
	};

	static FString StateName(const ECadenceArcResolverState State)
	{
		return StaticEnum<ECadenceArcResolverState>()->GetNameStringByValue(static_cast<int64>(State));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASLifecycleTest,
		"CadenceArc.GAS.Executor.AbilityLifecycleDrivesHandshake",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASLifecycleTest::RunTest(const FString& Parameters)
	{
		// 正常结束：Started 后 Completed，提交的动作保留
		{
			FGASFixture F({UCadenceArcTestAbilityA::StaticClass()});
			TestTrue(TEXT("Resolver initialized at BeginPlay"), F.Arc->IsInitialized());
			F.Arc->PressInput(Input_A);
			TestTrue(TEXT("Ability A activated"), F.IsActive(UCadenceArcTestAbilityA::StaticClass()));
			TestEqual(TEXT("Started on activation"), F.State(), StateName(ECadenceArcResolverState::Executing));
			TestEqual(TEXT("A committed"), F.Current(), Action_A.GetTag().ToString());
			TestTrue(TEXT("Executor tracks the request"), F.Executor->GetActiveRequestId() != 0);

			F.Finish(UCadenceArcTestAbilityA::StaticClass());
			TestEqual(TEXT("Ability end completes the action"), F.State(), StateName(ECadenceArcResolverState::Ready));
			TestEqual(TEXT("Completed keeps A"), F.Current(), Action_A.GetTag().ToString());
			TestEqual(TEXT("Executor cleared"), F.Executor->GetActiveRequestId(), static_cast<int64>(0));
		}

		// 取消：Interrupted 回到入口
		{
			FGASFixture F({UCadenceArcTestAbilityA::StaticClass()});
			F.Arc->PressInput(Input_A);
			F.Cancel(UCadenceArcTestAbilityA::StaticClass());
			TestEqual(TEXT("Cancelled ability interrupts"), F.State(), StateName(ECadenceArcResolverState::Ready));
			TestEqual(TEXT("Interrupt returns to entry"), F.Current(), Action_Root.GetTag().ToString());
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASInstantTest,
		"CadenceArc.GAS.Executor.AbilityEndingDuringActivation",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASInstantTest::RunTest(const FString& Parameters)
	{
		// 瞬发技能在 TryActivateAbility 内部结束：必须先 Started 再 Completed，否则完成回调会被拒绝，状态停在 Executing
		FGASFixture F({UCadenceArcTestInstantAbility::StaticClass()});
		F.Arc->PressInput(Input_Instant);
		TestEqual(TEXT("Instant ability leaves the resolver Ready"), F.State(), StateName(ECadenceArcResolverState::Ready));
		TestEqual(TEXT("Instant action committed"), F.Current(), Action_Instant.GetTag().ToString());
		TestEqual(TEXT("Executor cleared"), F.Executor->GetActiveRequestId(), static_cast<int64>(0));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASRejectTest,
		"CadenceArc.GAS.Executor.RejectsWhenAbilityCannotRun",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASRejectTest::RunTest(const FString& Parameters)
	{
		FGASFixture F({UCadenceArcTestAbilityA::StaticClass(), UCadenceArcTestAbilityDuplicateA::StaticClass(),
		               UCadenceArcTestBlockedAbility::StaticClass()});

		// GAS 拒绝激活（冷却、消耗、Tag 阻挡）
		F.Arc->PressInput(Input_Blocked);
		TestEqual(TEXT("Activation failure rejects"), F.State(), StateName(ECadenceArcResolverState::Ready));
		TestEqual(TEXT("Rejected request keeps the source node"), F.Current(), Action_Root.GetTag().ToString());

		// 没有匹配的 Ability
		AddExpectedMessage(TEXT("0 granted abilities have asset tag"), EAutomationExpectedMessageFlags::Contains, 1);
		F.Arc->ReleaseInput(Input_Blocked);
		F.Arc->PressInput(Input_Missing);
		TestEqual(TEXT("Missing ability rejects"), F.State(), StateName(ECadenceArcResolverState::Ready));

		// 匹配到多个 Ability
		AddExpectedMessage(TEXT("2 granted abilities have asset tag"), EAutomationExpectedMessageFlags::Contains, 1);
		F.Arc->PressInput(Input_A);
		TestEqual(TEXT("Ambiguous abilities reject"), F.State(), StateName(ECadenceArcResolverState::Ready));
		TestFalse(TEXT("Neither A ability activated"), F.IsActive(UCadenceArcTestAbilityA::StaticClass())
		          || F.IsActive(UCadenceArcTestAbilityDuplicateA::StaticClass()));
		TestEqual(TEXT("Still at entry"), F.Current(), Action_Root.GetTag().ToString());
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASBufferChainTest,
		"CadenceArc.GAS.Executor.BufferedInputActivatesNextAbility",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASBufferChainTest::RunTest(const FString& Parameters)
	{
		FGASFixture F({UCadenceArcTestAbilityA::StaticClass(), UCadenceArcTestAbilityB::StaticClass()});
		F.Arc->PressInput(Input_A);
		F.Arc->ReleaseInput(Input_A);

		// A 执行中打开窗口并缓冲 B；A 结束时消费缓冲，在结束回调里激活 B
		TestEqual(TEXT("Manual window opens"), static_cast<int32>(F.Executor->OpenBufferWindow()),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));
		TestEqual(TEXT("B buffered"), static_cast<int32>(F.Arc->PressInput(Input_B).Status),
		          static_cast<int32>(ECadenceArcInputStatus::Buffered));
		F.Arc->ReleaseInput(Input_B);
		F.Finish(UCadenceArcTestAbilityA::StaticClass());
		TestTrue(TEXT("B activated from the buffer"), F.IsActive(UCadenceArcTestAbilityB::StaticClass()));
		TestFalse(TEXT("A ended"), F.IsActive(UCadenceArcTestAbilityA::StaticClass()));
		TestEqual(TEXT("B committed"), F.Current(), Action_B.GetTag().ToString());
		TestEqual(TEXT("B executing"), F.State(), StateName(ECadenceArcResolverState::Executing));

		// B 执行中缓冲 A；B 结束后再进入 A，然后 A 中缓冲 A：在 A 自己的结束回调里重新激活同一个 Ability
		F.Executor->OpenBufferWindow();
		F.Arc->PressInput(Input_A);
		F.Arc->ReleaseInput(Input_A);
		F.Finish(UCadenceArcTestAbilityB::StaticClass());
		TestTrue(TEXT("A activated after B"), F.IsActive(UCadenceArcTestAbilityA::StaticClass()));
		F.Executor->OpenBufferWindow();
		F.Arc->PressInput(Input_A);
		F.Arc->ReleaseInput(Input_A);
		const int64 FirstA = F.Executor->GetActiveRequestId();
		F.Finish(UCadenceArcTestAbilityA::StaticClass());
		TestTrue(TEXT("Same ability reactivated inside its end callback"), F.IsActive(UCadenceArcTestAbilityA::StaticClass()));
		TestTrue(TEXT("Reactivation is a new request"), F.Executor->GetActiveRequestId() > FirstA);
		TestEqual(TEXT("Reactivated A executing"), F.State(), StateName(ECadenceArcResolverState::Executing));

		F.Finish(UCadenceArcTestAbilityA::StaticClass());
		TestEqual(TEXT("Chain ends Ready"), F.State(), StateName(ECadenceArcResolverState::Ready));
		TestFalse(TEXT("No ability left active"), F.IsActive(UCadenceArcTestAbilityA::StaticClass()));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASAnimationWindowTest,
		"CadenceArc.GAS.Executor.AnimationWindowUsesCapturedRequest",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASAnimationWindowTest::RunTest(const FString& Parameters)
	{
		FGASFixture F({UCadenceArcTestAbilityA::StaticClass(), UCadenceArcTestAbilityB::StaticClass()});
		UAnimSequence* MontageA = NewObject<UAnimSequence>();
		UAnimSequence* MontageB = NewObject<UAnimSequence>();

		F.Arc->PressInput(Input_A);
		F.Arc->ReleaseInput(Input_A);
		TestEqual(TEXT("Notify opens A's window"), static_cast<int32>(F.Executor->OpenBufferWindowForAnimation(MontageA)),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));
		F.Arc->PressInput(Input_B);
		F.Arc->ReleaseInput(Input_B);
		F.Finish(UCadenceArcTestAbilityA::StaticClass()); // B 开始执行，A 的窗口通知还没结束
		TestEqual(TEXT("Notify opens B's window"), static_cast<int32>(F.Executor->OpenBufferWindowForAnimation(MontageB)),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));

		// A 的蒙太奇迟到的 NotifyEnd 使用 A 的请求编号，被拒绝，B 的窗口保持打开
		TestNotEqual(TEXT("Late close from A is rejected"), static_cast<int32>(F.Executor->CloseBufferWindowForAnimation(MontageA)),
		             static_cast<int32>(ECadenceArcHandshakeResult::Success));
		TestEqual(TEXT("B's window still open"), static_cast<int32>(F.Arc->PressInput(Input_A).Status),
		          static_cast<int32>(ECadenceArcInputStatus::Buffered));
		TestEqual(TEXT("B's own notify closes it"), static_cast<int32>(F.Executor->CloseBufferWindowForAnimation(MontageB)),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));

		// 没有执行中的动作时（例如动作之外播放的蒙太奇），通知不打开任何窗口
		F.Finish(UCadenceArcTestAbilityB::StaticClass());
		F.Finish(UCadenceArcTestAbilityA::StaticClass());
		TestNotEqual(TEXT("Notify outside an action does nothing"),
		             static_cast<int32>(F.Executor->OpenBufferWindowForAnimation(MontageA)),
		             static_cast<int32>(ECadenceArcHandshakeResult::Success));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcGASSharedMontageWindowTest,
		"CadenceArc.GAS.Executor.SharedMontageWindowKeepsInstancesApart",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcGASSharedMontageWindowTest::RunTest(const FString& Parameters)
	{
		// A 和 B 播放同一个蒙太奇资产。B 的实例先进入窗口，A 正在淡出的实例后结束窗口
		FGASFixture F({UCadenceArcTestAbilityA::StaticClass(), UCadenceArcTestAbilityB::StaticClass()});
		UAnimSequence* SharedMontage = NewObject<UAnimSequence>();

		F.Arc->PressInput(Input_A);
		F.Arc->ReleaseInput(Input_A);
		F.Executor->OpenBufferWindowForAnimation(SharedMontage, 1);
		F.Arc->PressInput(Input_B);
		F.Arc->ReleaseInput(Input_B);
		F.Finish(UCadenceArcTestAbilityA::StaticClass());
		F.Executor->OpenBufferWindowForAnimation(SharedMontage, 2); // B 的播放实例
		TestNotEqual(TEXT("A's late close is rejected"),
		             static_cast<int32>(F.Executor->CloseBufferWindowForAnimation(SharedMontage, 1)),
		             static_cast<int32>(ECadenceArcHandshakeResult::Success)); // A 的实例迟到的 NotifyEnd
		TestEqual(TEXT("B's window survives A's late close"), static_cast<int32>(F.Arc->PressInput(Input_A).Status),
		          static_cast<int32>(ECadenceArcInputStatus::Buffered));
		TestEqual(TEXT("B's own instance closes its window"),
		          static_cast<int32>(F.Executor->CloseBufferWindowForAnimation(SharedMontage, 2)),
		          static_cast<int32>(ECadenceArcHandshakeResult::Success));
		return !HasAnyErrors();
	}
}

#endif
