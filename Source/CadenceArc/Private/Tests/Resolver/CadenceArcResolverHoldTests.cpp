#include "Graph/CadenceArcGraph.h"
#include "Input/CadenceArcInputTracker.h"
#include "Input/CadenceArcInputTrackingTypes.h"
#include "Resolver/CadenceArcResolver.h"
#include "Tests/CadenceArcTestSupport.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

namespace CadenceArc::Tests
{
	// 固定时间夹具，与 Phase 6 实施页里的例子一致：
	// 按下 1.0，T开始 = 0.2，T满 = 0.5，保持上限 = 2.0
	//   -> 蓄力起点 1.2、蓄满 1.5、自动截止 3.5
	//   -> 1.1 松手是普通攻击，1.5 松手是长按攻击
	namespace HoldFixture
	{
		constexpr double PressTime = 1.0;
		constexpr double ChargeStartSeconds = 0.2;
		constexpr double ChargeFullSeconds = 0.5;
		constexpr double MaxChargedHoldSeconds = 2.0;
		constexpr double ChargeFullTime = 1.5;   // PressTime + ChargeFullSeconds
		constexpr double AutoReleaseTime = 3.5;  // ChargeFullTime + MaxChargedHoldSeconds
		constexpr double TimeTolerance = 1.0e-9;
	}

	// 名字全部带 Hold 前缀：Unity Build 会把多个测试翻译单元拼进同一个文件，
	// 内部链接的同名 helper 会直接冲突。
	static FCadenceArcTransition MakeHoldEdge(
		const FGameplayTag& InputTag, const FGameplayTag& TargetActionTag,
		const double MinSeconds, const bool bHasMaxSeconds, const double MaxSeconds = 0.0)
	{
		FCadenceArcTransition Edge;
		Edge.InputTag = InputTag;
		Edge.TargetActionTag = TargetActionTag;
		Edge.InputPhase = ECadenceArcInputPhase::Released;
		Edge.bUseDurationRange = true;
		Edge.DurationRange.MinHeldDurationSeconds = MinSeconds;
		Edge.DurationRange.bHasMaxHeldDuration = bHasMaxSeconds;
		Edge.DurationRange.MaxHeldDurationSecondsExclusive = MaxSeconds;
		return Edge;
	}

	// 同一个 Tag 的两档松手分支：[0, T满) 普通攻击，[T满, 无上限) 长按攻击。
	static void AddHoldBranch(
		FCadenceArcNode& Node, const FGameplayTag& TapTarget, const FGameplayTag& ChargedTarget,
		const bool bWithChargeConfig, const double MaxChargedHoldSeconds)
	{
		Node.Transitions.Add(
			MakeHoldEdge(Input_Heavy, TapTarget, 0.0, true, HoldFixture::ChargeFullSeconds));
		Node.Transitions.Add(
			MakeHoldEdge(Input_Heavy, ChargedTarget, HoldFixture::ChargeFullSeconds, false));
		if (bWithChargeConfig)
		{
			FCadenceArcHoldChargeConfig& Config = Node.HoldChargeConfigs.AddDefaulted_GetRef();
			Config.InputTag = Input_Heavy;
			Config.ChargeStartSeconds = HoldFixture::ChargeStartSeconds;
			Config.MaxChargedHoldSeconds = MaxChargedHoldSeconds;
		}
	}

	// Root --Light(Pressed)--> Light01。两个节点都带同一套 Heavy 长按分支，
	// 这样才能验证资格跨 Completed 之后仍然按"授予时的节点"解析。
	static UCadenceArcGraph* MakeChargeGraph(
		const bool bWithChargeConfig = true,
		const double MaxChargedHoldSeconds = HoldFixture::MaxChargedHoldSeconds,
		const double MaxBufferedInputAgeSeconds = 0.0)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Action_Root;
		Graph->MaxBufferedInputAgeSeconds = MaxBufferedInputAgeSeconds;
		Graph->Nodes.Reserve(8); // 先占好容量，后面拿到的节点引用才不会因为扩容失效

		FCadenceArcNode& Root = AddNode(Graph, Action_Root);
		AddTransition(Root, Input_Light, Action_Light01);
		AddHoldBranch(Root, Action_Heavy01, Action_Heavy02, bWithChargeConfig, MaxChargedHoldSeconds);

		FCadenceArcNode& Light01 = AddNode(Graph, Action_Light01);
		AddTransition(Light01, Input_Light, Action_Light02);
		AddHoldBranch(Light01, Action_Finisher01, Action_Finisher02, bWithChargeConfig, MaxChargedHoldSeconds);

		AddNode(Graph, Action_Light02);
		AddNode(Graph, Action_Heavy01);
		AddNode(Graph, Action_Heavy02);
		AddNode(Graph, Action_Finisher01);
		AddNode(Graph, Action_Finisher02);
		return Graph;
	}

	static FCadenceArcInputToken MakeHoldToken(const int64 PressId = 1, const int32 SessionSeed = 1)
	{
		FCadenceArcInputToken Token;
		Token.SourceSession = FGuid(SessionSeed, 0x0A0B0C0D, 0x11223344, 0x55667788);
		Token.PressId = PressId;
		return Token;
	}

	static FCadenceArcInputEvent MakeHoldPress(const FGameplayTag& InputTag, const double TimestampSeconds)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = InputTag;
		Event.TimestampSeconds = TimestampSeconds;
		Event.InputPhase = ECadenceArcInputPhase::Pressed;
		return Event;
	}

	// HeldDuration 用与 Resolver 一致性检查相同的一次 double 运算得出。
	static FCadenceArcInputEvent MakeHoldRelease(
		const FGameplayTag& InputTag, const double PressedTimestampSeconds, const double ReleaseTimestampSeconds)
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = InputTag;
		Event.TimestampSeconds = ReleaseTimestampSeconds;
		Event.InputPhase = ECadenceArcInputPhase::Released;
		Event.HeldDurationSeconds = ReleaseTimestampSeconds - PressedTimestampSeconds;
		return Event;
	}

	static bool ExpectHoldOutcome(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcHoldOutcome& Actual,
		const ECadenceArcHoldResult ExpectedResult, const ECadenceArcResolutionReason ExpectedReason)
	{
		bool bPassed = Test.TestEqual(*FString::Printf(TEXT("%s result"), What),
		                              static_cast<int32>(Actual.GetResult()), static_cast<int32>(ExpectedResult));
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reason"), What),
		                          static_cast<int32>(Actual.GetReason()), static_cast<int32>(ExpectedReason));
		return bPassed;
	}

	static bool ExpectNoHold(FAutomationTestBase& Test, const TCHAR* What, const UCadenceArcResolver* Resolver)
	{
		const FCadenceArcHoldSnapshot Snapshot = Resolver->GetInputHoldSnapshot();
		bool bPassed = Test.TestFalse(*FString::Printf(TEXT("%s reports no hold"), What), Snapshot.bHasHold);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reports stage None"), What),
		                          static_cast<int32>(Snapshot.Stage), static_cast<int32>(ECadenceArcHoldStage::None));
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s clears charge flag"), What), Snapshot.bHasChargeConfig);
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s clears hold tag"), What), Snapshot.InputTag.IsValid());
		return bPassed;
	}

	static bool ExpectHoldStage(
		FAutomationTestBase& Test, const TCHAR* What, const UCadenceArcResolver* Resolver,
		const ECadenceArcHoldStage ExpectedStage, const double ExpectedLastObserved)
	{
		const FCadenceArcHoldSnapshot Snapshot = Resolver->GetInputHoldSnapshot();
		bool bPassed = Test.TestTrue(*FString::Printf(TEXT("%s still holds"), What), Snapshot.bHasHold);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s stage"), What),
		                          static_cast<int32>(Snapshot.Stage), static_cast<int32>(ExpectedStage));
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s last observed time"), What),
		                          Snapshot.LastObservedTimestampSeconds, ExpectedLastObserved,
		                          HoldFixture::TimeTolerance);
		return bPassed;
	}

	static bool ExpectStageChanges(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcInputAdvanceOutcome& Outcome,
		const TArray<ECadenceArcHoldStage>& ExpectedStages, const TArray<double>& ExpectedTimes)
	{
		const TArray<FCadenceArcInputStageChange>& Changes = Outcome.GetStageChanges();
		if (!Test.TestEqual(*FString::Printf(TEXT("%s stage change count"), What),
		                    Changes.Num(), ExpectedStages.Num()))
		{
			return false;
		}
		bool bPassed = true;
		for (int32 Index = 0; Index < Changes.Num(); ++Index)
		{
			bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s stage[%d]"), What, Index),
			                          static_cast<int32>(Changes[Index].ToStage),
			                          static_cast<int32>(ExpectedStages[Index]));
			bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s stage time[%d]"), What, Index),
			                          Changes[Index].EffectiveTimestampSeconds, ExpectedTimes[Index],
			                          HoldFixture::TimeTolerance);
		}
		return bPassed;
	}

	// 拒绝的推进不得带任何载荷，也不得改变资格。
	static bool ExpectAdvanceRejected(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcInputAdvanceOutcome& Outcome,
		const ECadenceArcResolutionReason ExpectedReason)
	{
		bool bPassed = Test.TestFalse(*FString::Printf(TEXT("%s is not accepted"), What), Outcome.IsAccepted());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reason"), What),
		                          static_cast<int32>(Outcome.GetReason()), static_cast<int32>(ExpectedReason));
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s has no release"), What), Outcome.HasRelease());
		bPassed &= Test.TestFalse(*FString::Printf(TEXT("%s has no request"), What), Outcome.HasActionRequest());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s has no stage change"), What),
		                          Outcome.GetStageChanges().Num(), 0);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s reports the source"), What),
		                          static_cast<int32>(Outcome.GetReleaseSource()),
		                          static_cast<int32>(ECadenceArcInputReleaseSource::None));
		return bPassed;
	}

	// 释放发生后的公共断言：资格一定终结，事件按生效时刻而不是调用时刻记录。
	static bool ExpectRelease(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcInputAdvanceOutcome& Outcome,
		const ECadenceArcInputReleaseSource ExpectedSource,
		const double ExpectedEventTime, const double ExpectedHeldDuration)
	{
		bool bPassed = Test.TestTrue(*FString::Printf(TEXT("%s is accepted"), What), Outcome.IsAccepted());
		bPassed &= Test.TestTrue(*FString::Printf(TEXT("%s reports a release"), What), Outcome.HasRelease());
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s release source"), What),
		                          static_cast<int32>(Outcome.GetReleaseSource()), static_cast<int32>(ExpectedSource));
		const FCadenceArcInputEvent Released = Outcome.GetReleasedInput();
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s release phase"), What),
		                          static_cast<int32>(Released.InputPhase),
		                          static_cast<int32>(ECadenceArcInputPhase::Released));
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s release timestamp"), What),
		                          Released.TimestampSeconds, ExpectedEventTime, HoldFixture::TimeTolerance);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s release duration"), What),
		                          Released.HeldDurationSeconds, ExpectedHeldDuration, HoldFixture::TimeTolerance);
		return bPassed;
	}

	static bool ExpectReleaseProducesTarget(
		FAutomationTestBase& Test, const TCHAR* What, const FCadenceArcInputAdvanceOutcome& Outcome,
		const UCadenceArcResolver* Resolver, const FGameplayTag& ExpectedSource, const FGameplayTag& ExpectedTarget)
	{
		bool bPassed = Test.TestTrue(*FString::Printf(TEXT("%s produces a request"), What),
		                             Outcome.HasActionRequest());
		const FCadenceArcActionRequest Request = Outcome.GetResolution().GetActionRequest();
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s request input"), What), Request.InputTag, Input_Heavy);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s request source"), What),
		                   Request.SourceActionTag, ExpectedSource);
		bPassed &= TestTag(Test, *FString::Printf(TEXT("%s request target"), What),
		                   Request.TargetActionTag, ExpectedTarget);
		bPassed &= TestState(Test, *FString::Printf(TEXT("%s enters AwaitingStart"), What),
		                     Resolver->GetState(), ECadenceArcResolverState::AwaitingStart);
		bPassed &= Test.TestEqual(*FString::Printf(TEXT("%s exposes the request"), What),
		                          Resolver->GetOutstandingRequest().RequestId, Request.RequestId);
		return bPassed;
	}

	// 在 Ready 状态直接申请一次 Heavy 按住资格。
	static bool GrantHoldInReady(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver, const FCadenceArcInputToken& Token,
		const double PressTimestampSeconds = HoldFixture::PressTime)
	{
		return ExpectHoldOutcome(Test, TEXT("Ready grant"),
		                         Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, PressTimestampSeconds)),
		                         ECadenceArcHoldResult::Granted, ECadenceArcResolutionReason::None);
	}

	// 走到 Light01 正在执行且窗口已开的状态，供"执行中申请资格"的用例复用。
	static bool EnterExecutingWithOpenWindow(
		FAutomationTestBase& Test, UCadenceArcResolver* Resolver, FCadenceArcActionRequest& OutRequest,
		const double SubmitTimestampSeconds = 0.4)
	{
		const FCadenceArcSubmitOutcome Outcome =
			Resolver->SubmitInput(MakeHoldPress(Input_Light, SubmitTimestampSeconds));
		OutRequest = Outcome.GetActionRequest();
		if (!TestSubmit(Test, TEXT("Executing setup resolves"), Outcome,
		                ECadenceArcResolutionCategory::RequestProduced, ECadenceArcResolutionReason::None))
		{
			return false;
		}
		if (!StartAndExpect(Test, Resolver, OutRequest, TEXT("Executing setup"))) { return false; }
		return TestHandshake(Test, TEXT("Executing setup opens window"),
		                     Resolver->OpenBufferWindow(OutRequest.RequestId),
		                     ECadenceArcHandshakeResult::Success);
	}

	static UCadenceArcResolver* MakeChargeResolver(
		FAutomationTestBase& Test, const bool bWithChargeConfig = true,
		const double MaxChargedHoldSeconds = HoldFixture::MaxChargedHoldSeconds,
		const double MaxBufferedInputAgeSeconds = 0.0)
	{
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		TestInit(Test, TEXT("Charge graph initializes"),
		         Resolver->Initialize(MakeChargeGraph(bWithChargeConfig, MaxChargedHoldSeconds,
		                                              MaxBufferedInputAgeSeconds)),
		         ECadenceArcResolverInitResult::Success);
		return Resolver;
	}

	// ---------------------------------------------------------------- P6-07

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldBeginEligibilityTest,
		"CadenceArc.Resolver.Hold.BeginEligibility",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldBeginEligibilityTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		{
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			ExpectHoldOutcome(*this, TEXT("Uninitialized begin"),
			                  Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, HoldFixture::PressTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NotInitialized);
			ExpectNoHold(*this, TEXT("Uninitialized begin"), Resolver);
		}

		// 身份与事件格式的拒绝都发生在碰任何状态之前
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			ExpectHoldOutcome(*this, TEXT("Invalid token"),
			                  Resolver->BeginInputHold(FCadenceArcInputToken{},
			                                           MakeHoldPress(Input_Heavy, HoldFixture::PressTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InvalidInputEvent);
			ExpectHoldOutcome(*this, TEXT("Released event as press"),
			                  Resolver->BeginInputHold(Token, MakeHoldRelease(Input_Heavy, 1.0, 1.2)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InvalidInputEvent);
			// 事件本身的原因顺序由 ValidateInputEvent 单点定义，与 SubmitInput 报同样的原因
			ExpectHoldOutcome(*this, TEXT("Invalid input tag"),
			                  Resolver->BeginInputHold(Token, MakeHoldPress(FGameplayTag::EmptyTag, 1.0)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InvalidInputTag);
			for (const double Invalid : InvalidTimes())
			{
				ExpectHoldOutcome(*this, *FString::Printf(TEXT("Invalid press time %g"), Invalid),
				                  Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, Invalid)),
				                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InvalidTimestamp);
			}
			ExpectNoHold(*this, TEXT("Rejected begin"), Resolver);
			TestState(*this, TEXT("Rejected begin keeps Ready"), Resolver->GetState(),
			          ECadenceArcResolverState::Ready);
		}

		// 当前节点上这个 Tag 一条 Released 边都没有：按多久都不会有匹配，当场拒绝
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			ExpectHoldOutcome(*this, TEXT("Tag without released edge"),
			                  Resolver->BeginInputHold(Token, MakeHoldPress(Input_Light, HoldFixture::PressTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingTransition);
			ExpectNoHold(*this, TEXT("Tag without released edge"), Resolver);
		}

		// Ready 可以申请，而且授予本身不产生请求、不改变状态
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestState(*this, TEXT("Grant keeps Ready"), Resolver->GetState(), ECadenceArcResolverState::Ready);
			TestEqual(TEXT("Grant allocates no request"), Resolver->GetOutstandingRequest().RequestId, int64{0});
			TestFalse(TEXT("Grant is not a buffered event"), Resolver->GetBufferedInputTag().IsValid());
			const FCadenceArcHoldSnapshot Snapshot = Resolver->GetInputHoldSnapshot();
			TestTrue(TEXT("Grant binds the token"), Snapshot.Token == Token);
			TestTag(*this, TEXT("Grant binds the committed node"), Snapshot.SourceActionTag, Action_Root);
		}

		// AwaitingStart 时当前节点马上就要变，资格无法绑定确定的源节点
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01, Request,
			                      TEXT("Awaiting setup"))) { return false; }
			ExpectHoldOutcome(*this, TEXT("Begin while awaiting start"),
			                  Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, HoldFixture::PressTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::RequestPending);
			ExpectNoHold(*this, TEXT("Begin while awaiting start"), Resolver);
		}

		// Executing 必须开窗；开窗后资格绑定的是已提交的 Light01，而不是候选目标
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!ResolveAndExpect(*this, Resolver, Input_Light, Action_Root, Action_Light01, Request,
			                      TEXT("Window setup"))) { return false; }
			if (!StartAndExpect(*this, Resolver, Request, TEXT("Window setup"))) { return false; }
			ExpectHoldOutcome(*this, TEXT("Begin with closed window"),
			                  Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, HoldFixture::PressTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::BufferWindowClosed);
			ExpectNoHold(*this, TEXT("Begin with closed window"), Resolver);

			TestHandshake(*this, TEXT("Window opens"), Resolver->OpenBufferWindow(Request.RequestId),
			              ECadenceArcHandshakeResult::Success);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestTag(*this, TEXT("Grant binds the executing node"),
			        Resolver->GetInputHoldSnapshot().SourceActionTag, Action_Light01);
			TestState(*this, TEXT("Grant keeps Executing"), Resolver->GetState(),
			          ECadenceArcResolverState::Executing);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldSnapshotTest,
		"CadenceArc.Resolver.Hold.SnapshotDerivesStages",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldSnapshotTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			ExpectNoHold(*this, TEXT("Fresh resolver"), Resolver);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }

			const FCadenceArcHoldSnapshot Snapshot = Resolver->GetInputHoldSnapshot();
			TestTrue(TEXT("Snapshot reports the hold"), Snapshot.bHasHold);
			TestTrue(TEXT("Snapshot reports the charge config"), Snapshot.bHasChargeConfig);
			TestTag(*this, TEXT("Snapshot input tag"), Snapshot.InputTag, Input_Heavy);
			TestTag(*this, TEXT("Snapshot source node"), Snapshot.SourceActionTag, Action_Root);
			TestEqual(TEXT("Snapshot pressed time"), Snapshot.PressedTimestampSeconds, HoldFixture::PressTime,
			          HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot charge start seconds"), Snapshot.ChargeStartSeconds,
			          HoldFixture::ChargeStartSeconds, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot derives the highest tier threshold"), Snapshot.ChargeFullSeconds,
			          HoldFixture::ChargeFullSeconds, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot max charged hold"), Snapshot.MaxChargedHoldSeconds,
			          HoldFixture::MaxChargedHoldSeconds, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot charge full timestamp"), Snapshot.ChargeFullTimestampSeconds,
			          HoldFixture::ChargeFullTime, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot auto release timestamp"), Snapshot.AutoReleaseTimestampSeconds,
			          HoldFixture::AutoReleaseTime, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot starts at the press time"), Snapshot.LastObservedTimestampSeconds,
			          HoldFixture::PressTime, HoldFixture::TimeTolerance);
			TestEqual(TEXT("Snapshot starts Holding"), static_cast<int32>(Snapshot.Stage),
			          static_cast<int32>(ECadenceArcHoldStage::Holding));

			// 阈值时刻以 Resolver 报告的为准：它是"按住时长刚好达到"的最早 double，不一定等于
			// Pressed + Seconds 的朴素求和（1.0 + 0.2 会向下舍入到时长还不足 0.2 的时刻）。
			// 蓄力起点从探针的阶段通知读取，蓄满取快照；阈值两侧都锁住，确认端点是"大于等于"。
			double ChargeStartTime = 0.0;
			{
				UCadenceArcResolver* Probe = MakeChargeResolver(*this);
				GrantHoldInReady(*this, Probe, Token);
				const FCadenceArcInputAdvanceOutcome Crossed = Probe->AdvanceInputTime(1.3);
				if (!TestEqual(TEXT("Probe reports the charge start crossing"), Crossed.GetStageChanges().Num(), 1))
				{
					return false;
				}
				ChargeStartTime = Crossed.GetStageChanges()[0].EffectiveTimestampSeconds;
			}
			TestTrue(TEXT("Charge start is reached in held duration"),
			         ChargeStartTime - Snapshot.PressedTimestampSeconds >= Snapshot.ChargeStartSeconds);
			TestTrue(TEXT("Charge full is reached in held duration"),
			         Snapshot.ChargeFullTimestampSeconds - Snapshot.PressedTimestampSeconds >= Snapshot.ChargeFullSeconds);

			const double BeforeChargeStart = std::nextafter(ChargeStartTime, 0.0);
			Resolver->AdvanceInputTime(BeforeChargeStart);
			ExpectHoldStage(*this, TEXT("Just before charge start"), Resolver, ECadenceArcHoldStage::Holding,
			                BeforeChargeStart);
			Resolver->AdvanceInputTime(ChargeStartTime);
			ExpectHoldStage(*this, TEXT("At charge start"), Resolver, ECadenceArcHoldStage::Charging,
			                ChargeStartTime);
			const double BeforeChargeFull = std::nextafter(Snapshot.ChargeFullTimestampSeconds, 0.0);
			Resolver->AdvanceInputTime(BeforeChargeFull);
			ExpectHoldStage(*this, TEXT("Just before charge full"), Resolver, ECadenceArcHoldStage::Charging,
			                BeforeChargeFull);
			Resolver->AdvanceInputTime(Snapshot.ChargeFullTimestampSeconds);
			ExpectHoldStage(*this, TEXT("At charge full"), Resolver, ECadenceArcHoldStage::Charged,
			                Snapshot.ChargeFullTimestampSeconds);
		}

		// 没有整体计时配置：有资格但恒为 Holding，计时字段全部置 0 且不参与判断
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this, false);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcHoldSnapshot Snapshot = Resolver->GetInputHoldSnapshot();
			TestTrue(TEXT("Config-less snapshot still reports a hold"), Snapshot.bHasHold);
			TestFalse(TEXT("Config-less snapshot has no charge config"), Snapshot.bHasChargeConfig);
			TestEqual(TEXT("Config-less snapshot stays Holding"), static_cast<int32>(Snapshot.Stage),
			          static_cast<int32>(ECadenceArcHoldStage::Holding));
			TestEqual(TEXT("Config-less charge start is zero"), Snapshot.ChargeStartSeconds, 0.0);
			TestEqual(TEXT("Config-less charge full is zero"), Snapshot.ChargeFullSeconds, 0.0);
			TestEqual(TEXT("Config-less max hold is zero"), Snapshot.MaxChargedHoldSeconds, 0.0);
			TestEqual(TEXT("Config-less charge full time is zero"), Snapshot.ChargeFullTimestampSeconds, 0.0);
			TestEqual(TEXT("Config-less auto release time is zero"), Snapshot.AutoReleaseTimestampSeconds, 0.0);

			// 按住很久也不会进入蓄力或自动释放
			const FCadenceArcInputAdvanceOutcome Outcome = Resolver->AdvanceInputTime(100.0);
			TestTrue(TEXT("Config-less advance is accepted"), Outcome.IsAccepted());
			TestFalse(TEXT("Config-less advance never releases"), Outcome.HasRelease());
			TestEqual(TEXT("Config-less advance has no stage change"), Outcome.GetStageChanges().Num(), 0);
			ExpectHoldStage(*this, TEXT("Config-less long hold"), Resolver, ECadenceArcHoldStage::Holding, 100.0);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldManualReleaseTest,
		"CadenceArc.Resolver.Hold.ManualReleaseMatchesDuration",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldManualReleaseTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		// 提前松手：普通攻击，没有跨过任何阈值
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1));
			ExpectRelease(*this, TEXT("Tap release"), Outcome, ECadenceArcInputReleaseSource::Manual, 1.1, 0.1);
			ExpectStageChanges(*this, TEXT("Tap release"), Outcome, {}, {});
			ExpectReleaseProducesTarget(*this, TEXT("Tap release"), Outcome, Resolver, Action_Root, Action_Heavy01);
			ExpectNoHold(*this, TEXT("Tap release"), Resolver);
		}

		// 蓄满后松手：长按攻击，补报这段时间跨过的两个阈值
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome = Resolver->ReleaseInputHold(
				Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, HoldFixture::ChargeFullTime));
			ExpectRelease(*this, TEXT("Charged release"), Outcome, ECadenceArcInputReleaseSource::Manual,
			              HoldFixture::ChargeFullTime, HoldFixture::ChargeFullSeconds);
			ExpectStageChanges(*this, TEXT("Charged release"), Outcome,
			                   {ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged},
			                   {
				                   HoldFixture::PressTime + HoldFixture::ChargeStartSeconds,
				                   HoldFixture::ChargeFullTime
			                   });
			ExpectReleaseProducesTarget(*this, TEXT("Charged release"), Outcome, Resolver, Action_Root,
			                            Action_Heavy02);
			ExpectNoHold(*this, TEXT("Charged release"), Resolver);
		}

		// 失败的释放不得改变资格，改正之后仍然能成功
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }

			ExpectAdvanceRejected(*this, TEXT("Mismatched token"),
			                      Resolver->ReleaseInputHold(MakeHoldToken(2),
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1)),
			                      ECadenceArcResolutionReason::NoMatchingHold);
			ExpectAdvanceRejected(*this, TEXT("Foreign session token"),
			                      Resolver->ReleaseInputHold(MakeHoldToken(1, 7),
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1)),
			                      ECadenceArcResolutionReason::NoMatchingHold);
			ExpectAdvanceRejected(*this, TEXT("Invalid token"),
			                      Resolver->ReleaseInputHold(FCadenceArcInputToken{},
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1)),
			                      ECadenceArcResolutionReason::NoMatchingHold);
			ExpectAdvanceRejected(*this, TEXT("Wrong tag"),
			                      Resolver->ReleaseInputHold(Token,
			                                                 MakeHoldRelease(Input_Light, HoldFixture::PressTime, 1.1)),
			                      ECadenceArcResolutionReason::InvalidInputEvent);
			ExpectAdvanceRejected(*this, TEXT("Pressed phase release"),
			                      Resolver->ReleaseInputHold(Token, MakeHoldPress(Input_Heavy, 1.1)),
			                      ECadenceArcResolutionReason::InvalidInputEvent);

			// duration 必须对应本次按下，不能由调用方随便填
			FCadenceArcInputEvent Inconsistent = MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1);
			Inconsistent.HeldDurationSeconds = HoldFixture::ChargeFullSeconds;
			ExpectAdvanceRejected(*this, TEXT("Inconsistent duration"),
			                      Resolver->ReleaseInputHold(Token, Inconsistent),
			                      ECadenceArcResolutionReason::InvalidInputEvent);

			// 时间倒退：旧时间戳不能绕过已经观察到的蓄力
			Resolver->AdvanceInputTime(1.3);
			ExpectAdvanceRejected(*this, TEXT("Backwards release"),
			                      Resolver->ReleaseInputHold(Token,
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.2)),
			                      ECadenceArcResolutionReason::InvalidTimestamp);
			for (const double Invalid : InvalidTimes())
			{
				FCadenceArcInputEvent BadTime = MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.4);
				BadTime.TimestampSeconds = Invalid;
				ExpectAdvanceRejected(*this, *FString::Printf(TEXT("Invalid release time %g"), Invalid),
				                      Resolver->ReleaseInputHold(Token, BadTime),
				                      ECadenceArcResolutionReason::InvalidInputEvent);
			}

			ExpectHoldStage(*this, TEXT("After failed releases"), Resolver, ECadenceArcHoldStage::Charging, 1.3);
			// 1.4 松手只按住了 0.4 秒：档位由最终时长决定，不因为已经观察到 Charging 就升级
			const FCadenceArcInputAdvanceOutcome Recovered = Resolver->ReleaseInputHold(
				Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.4));
			ExpectRelease(*this, TEXT("Recovered release"), Recovered, ECadenceArcInputReleaseSource::Manual,
			              1.4, 1.4 - HoldFixture::PressTime);
			ExpectReleaseProducesTarget(*this, TEXT("Recovered release"), Recovered, Resolver, Action_Root,
			                            Action_Heavy01);
		}

		// Tracker 产生的真实 Token 与事件可以直接喂给 Resolver
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcInputTracker Tracker;
			const FCadenceArcInputTrackingOutcome Pressed = Tracker.Press(Input_Heavy, HoldFixture::PressTime);
			TestEqual(TEXT("Tracker produces a press"), static_cast<int32>(Pressed.GetResult()),
			          static_cast<int32>(ECadenceArcInputTrackingResult::PressedProduced));
			if (!ExpectHoldOutcome(*this, TEXT("Tracker grant"),
			                       Resolver->BeginInputHold(Pressed.GetToken(), Pressed.GetInputEvent()),
			                       ECadenceArcHoldResult::Granted, ECadenceArcResolutionReason::None))
			{
				return false;
			}
			const FCadenceArcInputTrackingOutcome Released = Tracker.Release(Pressed.GetToken(), 1.1);
			TestEqual(TEXT("Tracker produces a release"), static_cast<int32>(Released.GetResult()),
			          static_cast<int32>(ECadenceArcInputTrackingResult::ReleasedProduced));
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->ReleaseInputHold(Released.GetToken(), Released.GetInputEvent());
			ExpectRelease(*this, TEXT("Tracker release"), Outcome, ECadenceArcInputReleaseSource::Manual, 1.1, 0.1);
			ExpectReleaseProducesTarget(*this, TEXT("Tracker release"), Outcome, Resolver, Action_Root,
			                            Action_Heavy01);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldReleaseDuringExecutionTest,
		"CadenceArc.Resolver.Hold.ReleaseDuringExecutionBuffers",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldReleaseDuringExecutionTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		// 关窗之后松手仍然存成缓冲事件：窗口只管"能不能取得资格"
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestHandshake(*this, TEXT("Window closes before release"),
			              Resolver->CloseBufferWindow(Request.RequestId), ECadenceArcHandshakeResult::Success);

			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1));
			ExpectRelease(*this, TEXT("Buffered release"), Outcome, ECadenceArcInputReleaseSource::Manual, 1.1, 0.1);
			TestSubmit(*this, TEXT("Buffered release resolution"), Outcome.GetResolution(),
			           ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
			TestFalse(TEXT("Buffered release produces no request yet"), Outcome.HasActionRequest());
			TestTag(*this, TEXT("Buffered release exposes its tag"), Resolver->GetBufferedInputTag(), Input_Heavy);
			ExpectNoHold(*this, TEXT("Buffered release"), Resolver);
			TestState(*this, TEXT("Buffered release keeps Executing"), Resolver->GetState(),
			          ECadenceArcResolverState::Executing);

			// 完成时间早于已观察到的松手时刻：写状态之前就拒绝，动作不结束
			const FCadenceArcActionCompletionOutcome Early =
				CompleteAt(*this, Resolver, Request.RequestId, 1.0);
			TestHandshake(*this, TEXT("Early completion is rejected"), Early.GetHandshakeResult(),
			              ECadenceArcHandshakeResult::InvalidCompletionTime);
			TestState(*this, TEXT("Rejected completion keeps Executing"), Resolver->GetState(),
			          ECadenceArcResolverState::Executing);
			TestTag(*this, TEXT("Rejected completion keeps the buffer"), Resolver->GetBufferedInputTag(),
			        Input_Heavy);
			TestEqual(TEXT("Rejected completion keeps the request"),
			          Resolver->GetOutstandingRequest().RequestId, Request.RequestId);

			// 用授予时冻结的边副本消费：0.1 秒落在 Light01 的普通攻击档
			const FCadenceArcActionCompletionOutcome Completed =
				CompleteAt(*this, Resolver, Request.RequestId, 1.2);
			TestHandshake(*this, TEXT("Hold buffer completes"), Completed.GetHandshakeResult(),
			              ECadenceArcHandshakeResult::Success);
			TestBufferConsumption(*this, TEXT("Hold buffer consumption"), Completed,
			                      ECadenceArcResolutionCategory::RequestProduced,
			                      ECadenceArcResolutionReason::None);
			TestTag(*this, TEXT("Hold buffer target"), Completed.GetNextActionRequest().TargetActionTag,
			        Action_Finisher01);
			TestTag(*this, TEXT("Hold buffer source"), Completed.GetNextActionRequest().SourceActionTag,
			        Action_Light01);
		}

		// 长按档同样按授予时的节点解析
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.8));
			ExpectRelease(*this, TEXT("Charged buffered release"), Outcome, ECadenceArcInputReleaseSource::Manual,
			              1.8, 1.8 - HoldFixture::PressTime);
			const FCadenceArcActionCompletionOutcome Completed =
				CompleteAt(*this, Resolver, Request.RequestId, 2.0);
			TestBufferConsumption(*this, TEXT("Charged buffer consumption"), Completed,
			                      ECadenceArcResolutionCategory::RequestProduced,
			                      ECadenceArcResolutionReason::None);
			TestTag(*this, TEXT("Charged buffer target"), Completed.GetNextActionRequest().TargetActionTag,
			        Action_Finisher02);
		}

		// 按住产生的缓冲事件按"松手时刻"计算年龄
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this, true, HoldFixture::MaxChargedHoldSeconds, 0.25);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1));
			const FCadenceArcActionCompletionOutcome Completed =
				CompleteAt(*this, Resolver, Request.RequestId, 1.5);
			TestHandshake(*this, TEXT("Expired hold buffer still completes"), Completed.GetHandshakeResult(),
			              ECadenceArcHandshakeResult::Success);
			TestBufferConsumption(*this, TEXT("Expired hold buffer"), Completed,
			                      ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired);
			TestState(*this, TEXT("Expired hold buffer returns Ready"), Resolver->GetState(),
			          ECadenceArcResolverState::Ready);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldProtectionTest,
		"CadenceArc.Resolver.Hold.ProtectionRejectsNewInput",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldProtectionTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		// Holding 阶段允许被真正接受的新输入替换
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Holding accepts a new input"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 1.1), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
			                                  ECadenceArcResolutionReason::None));
			ExpectNoHold(*this, TEXT("Accepted input replaces the hold"), Resolver);
			ExpectAdvanceRejected(*this, TEXT("Replaced hold"),
			                      Resolver->ReleaseInputHold(Token,
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.2)),
			                      ECadenceArcResolutionReason::NoMatchingHold);
		}

		// 解析失败的输入不算"被接受"，资格原样保留：Root 上 Heavy 只有 Released 边
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Unmatched input"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Heavy, 1.1), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::NoAction,
			                                  ECadenceArcResolutionReason::NoMatchingTransition));
			ExpectHoldStage(*this, TEXT("Unmatched input"), Resolver, ECadenceArcHoldStage::Holding,
			                HoldFixture::PressTime);
			// 资格未被破坏，随后的松手仍然能正常结算
			ExpectReleaseProducesTarget(
				*this, TEXT("Release after an unmatched input"),
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1)),
				Resolver, Action_Root, Action_Heavy01);
		}

		// Charging / Charged 受保护：图内其他攻击输入不能覆盖蓄力
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Resolver->AdvanceInputTime(1.3);
			const FResolverSnapshot Before(Resolver);
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Charging protects the hold"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 1.3), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                  ECadenceArcResolutionReason::HoldProtected));
			Before.ExpectUnchanged(*this, Resolver);
			ExpectHoldStage(*this, TEXT("Charging protection"), Resolver, ECadenceArcHoldStage::Charging, 1.3);

			// 另一个 Token 也不能抢走蓄力中的槽
			ExpectHoldOutcome(*this, TEXT("Begin during charging"),
			                  Resolver->BeginInputHold(MakeHoldToken(2), MakeHoldPress(Input_Heavy, 1.3)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::HoldProtected);
			TestTrue(TEXT("Protected hold keeps its token"), Resolver->GetInputHoldSnapshot().Token == Token);

			// 时间倒退在保护判断之前拦下
			TestTransition(*this, TEXT("Backwards submit"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 1.2), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                  ECadenceArcResolutionReason::InvalidTimestamp));
			ExpectHoldStage(*this, TEXT("Backwards submit"), Resolver, ECadenceArcHoldStage::Charging, 1.3);

			Resolver->AdvanceInputTime(HoldFixture::ChargeFullTime);
			TestTransition(*this, TEXT("Charged protects the hold"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, HoldFixture::ChargeFullTime), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                  ECadenceArcResolutionReason::HoldProtected));
			ExpectHoldStage(*this, TEXT("Charged protection"), Resolver, ECadenceArcHoldStage::Charged,
			                HoldFixture::ChargeFullTime);
		}

		// 漏掉 Advance：只有自动释放已经到期才要求先推进
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Before the deadline it is only protection"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 3.4), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                  ECadenceArcResolutionReason::HoldProtected));
			TestTransition(*this, TEXT("At the deadline a missed advance is reported"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, HoldFixture::AutoReleaseTime), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Rejected,
			                                  ECadenceArcResolutionReason::InputTimeAdvanceRequired));
			ExpectHoldOutcome(*this, TEXT("Begin with a missed advance"),
			                  Resolver->BeginInputHold(MakeHoldToken(2),
			                                           MakeHoldPress(Input_Heavy, HoldFixture::AutoReleaseTime)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InputTimeAdvanceRequired);
			// 拒绝不推进观察时间，也不产生任何请求
			ExpectHoldStage(*this, TEXT("Missed advance"), Resolver, ECadenceArcHoldStage::Holding,
			                HoldFixture::PressTime);
			TestEqual(TEXT("Missed advance allocates no request"),
			          Resolver->GetOutstandingRequest().RequestId, int64{0});
		}

		// 没有整体计时配置：不保护、不到期，新输入随时可以替换
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this, false);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Resolver->AdvanceInputTime(50.0);
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Config-less hold does not protect"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 50.0), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::RequestProduced,
			                                  ECadenceArcResolutionReason::None));
			ExpectNoHold(*this, TEXT("Config-less replacement"), Resolver);
		}
		return !HasAnyErrors();
	}

	// ---------------------------------------------------------------- P6-08

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldAdvanceTest,
		"CadenceArc.Resolver.Hold.AdvanceReportsStageCrossings",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldAdvanceTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();
		const double ChargeStartTime = HoldFixture::PressTime + HoldFixture::ChargeStartSeconds;

		{
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			ExpectAdvanceRejected(*this, TEXT("Uninitialized advance"), Resolver->AdvanceInputTime(1.0),
			                      ECadenceArcResolutionReason::NotInitialized);
		}

		// 没有资格时是接受的空操作：不创建输入，也不清普通缓存
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			const FCadenceArcInputAdvanceOutcome Idle = Resolver->AdvanceInputTime(1.0);
			TestTrue(TEXT("Idle advance is accepted"), Idle.IsAccepted());
			TestFalse(TEXT("Idle advance has no release"), Idle.HasRelease());
			TestEqual(TEXT("Idle advance has no stage change"), Idle.GetStageChanges().Num(), 0);
			TestFalse(TEXT("Idle advance carries no token"), Idle.GetToken().IsValid());

			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Plain buffered input"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 1.0), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Buffered,
			                                  ECadenceArcResolutionReason::None));
			TestTrue(TEXT("Advance keeps a plain buffer"), Resolver->AdvanceInputTime(2.0).IsAccepted());
			TestTag(*this, TEXT("Advance does not clear a plain buffer"), Resolver->GetBufferedInputTag(),
			        Input_Light);
		}

		// 逐帧推进：跨过阈值时各报告一次
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }

			const FCadenceArcInputAdvanceOutcome Early = Resolver->AdvanceInputTime(1.1);
			TestTrue(TEXT("Early advance is accepted"), Early.IsAccepted());
			TestTrue(TEXT("Early advance echoes the token"), Early.GetToken() == Token);
			ExpectStageChanges(*this, TEXT("Early advance"), Early, {}, {});
			ExpectHoldStage(*this, TEXT("Early advance"), Resolver, ECadenceArcHoldStage::Holding, 1.1);

			const FCadenceArcInputAdvanceOutcome Charging = Resolver->AdvanceInputTime(1.3);
			ExpectStageChanges(*this, TEXT("Charging advance"), Charging,
			                   {ECadenceArcHoldStage::Charging}, {ChargeStartTime});
			TestFalse(TEXT("Charging advance does not release"), Charging.HasRelease());

			const FCadenceArcInputAdvanceOutcome Charged = Resolver->AdvanceInputTime(1.6);
			ExpectStageChanges(*this, TEXT("Charged advance"), Charged,
			                   {ECadenceArcHoldStage::Charged}, {HoldFixture::ChargeFullTime});

			// 同一个阈值不会被报告两次
			ExpectStageChanges(*this, TEXT("Repeated advance"), Resolver->AdvanceInputTime(3.4), {}, {});
			ExpectHoldStage(*this, TEXT("Repeated advance"), Resolver, ECadenceArcHoldStage::Charged, 3.4);

			// 倒退与非法时间不推进、不终结资格
			ExpectAdvanceRejected(*this, TEXT("Backwards advance"), Resolver->AdvanceInputTime(1.6),
			                      ECadenceArcResolutionReason::InvalidTimestamp);
			for (const double Invalid : InvalidTimes())
			{
				ExpectAdvanceRejected(*this, *FString::Printf(TEXT("Invalid advance %g"), Invalid),
				                      Resolver->AdvanceInputTime(Invalid),
				                      ECadenceArcResolutionReason::InvalidTimestamp);
			}
			ExpectHoldStage(*this, TEXT("After rejected advances"), Resolver, ECadenceArcHoldStage::Charged, 3.4);
		}

		// 一次推进跨过两个阈值并触发自动释放，顺序固定
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome = Resolver->AdvanceInputTime(4.0);
			ExpectStageChanges(*this, TEXT("Single long advance"), Outcome,
			                   {ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged},
			                   {ChargeStartTime, HoldFixture::ChargeFullTime});
			ExpectRelease(*this, TEXT("Single long advance"), Outcome, ECadenceArcInputReleaseSource::HoldLimit,
			              HoldFixture::AutoReleaseTime, HoldFixture::AutoReleaseTime - HoldFixture::PressTime);
			ExpectReleaseProducesTarget(*this, TEXT("Single long advance"), Outcome, Resolver, Action_Root,
			                            Action_Heavy02);
			ExpectNoHold(*this, TEXT("Single long advance"), Resolver);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldAutoReleaseTest,
		"CadenceArc.Resolver.Hold.AutoReleaseOnce",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldAutoReleaseTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();
		const double ChargeStartTime = HoldFixture::PressTime + HoldFixture::ChargeStartSeconds;

		// 保持上限为 0：蓄满的同一时刻先报告 Charged，再自动释放
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this, true, 0.0);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->AdvanceInputTime(HoldFixture::ChargeFullTime);
			ExpectStageChanges(*this, TEXT("Zero hold limit"), Outcome,
			                   {ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged},
			                   {ChargeStartTime, HoldFixture::ChargeFullTime});
			ExpectRelease(*this, TEXT("Zero hold limit"), Outcome, ECadenceArcInputReleaseSource::HoldLimit,
			              HoldFixture::ChargeFullTime, HoldFixture::ChargeFullSeconds);
			ExpectReleaseProducesTarget(*this, TEXT("Zero hold limit"), Outcome, Resolver, Action_Root,
			                            Action_Heavy02);

			// 一次按下最多兑现一次：之后的物理松手和推进都不再产生候选
			ExpectAdvanceRejected(*this, TEXT("Release after auto release"),
			                      Resolver->ReleaseInputHold(Token,
			                                                 MakeHoldRelease(Input_Heavy, HoldFixture::PressTime,
			                                                                 1.6)),
			                      ECadenceArcResolutionReason::NoMatchingHold);
			const FCadenceArcInputAdvanceOutcome Later = Resolver->AdvanceInputTime(1.7);
			TestTrue(TEXT("Advance after auto release is accepted"), Later.IsAccepted());
			TestFalse(TEXT("Advance after auto release does not release again"), Later.HasRelease());
		}

		// 越过截止时刻才物理松手：按 HoldLimit 结算，事件时刻是截止时刻
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 4.0));
			ExpectRelease(*this, TEXT("Late manual release"), Outcome, ECadenceArcInputReleaseSource::HoldLimit,
			              HoldFixture::AutoReleaseTime, HoldFixture::AutoReleaseTime - HoldFixture::PressTime);
			ExpectStageChanges(*this, TEXT("Late manual release"), Outcome,
			                   {ECadenceArcHoldStage::Charging, ECadenceArcHoldStage::Charged},
			                   {ChargeStartTime, HoldFixture::ChargeFullTime});
			ExpectReleaseProducesTarget(*this, TEXT("Late manual release"), Outcome, Resolver, Action_Root,
			                            Action_Heavy02);
		}

		// 自动释放也要过年龄检查：4.0 才观察到 3.5 的释放，超过 0.25 的上限
		{
			UCadenceArcResolver* Resolver =
				MakeChargeResolver(*this, true, HoldFixture::MaxChargedHoldSeconds, 0.25);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome = Resolver->AdvanceInputTime(4.0);
			ExpectRelease(*this, TEXT("Expired auto release"), Outcome, ECadenceArcInputReleaseSource::HoldLimit,
			              HoldFixture::AutoReleaseTime, HoldFixture::AutoReleaseTime - HoldFixture::PressTime);
			TestSubmit(*this, TEXT("Expired auto release resolution"), Outcome.GetResolution(),
			           ECadenceArcResolutionCategory::NoAction, ECadenceArcResolutionReason::Expired);
			TestFalse(TEXT("Expired auto release produces no request"), Outcome.HasActionRequest());
			// 资格仍然终结，不会因为没产生请求而退回按住状态
			ExpectNoHold(*this, TEXT("Expired auto release"), Resolver);
			TestState(*this, TEXT("Expired auto release stays Ready"), Resolver->GetState(),
			          ECadenceArcResolverState::Ready);
			TestEqual(TEXT("Expired auto release allocates no request"),
			          Resolver->GetOutstandingRequest().RequestId, int64{0});
		}

		// 及时推进就不会过期：3.5 当帧观察到释放
		{
			UCadenceArcResolver* Resolver =
				MakeChargeResolver(*this, true, HoldFixture::MaxChargedHoldSeconds, 0.25);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Outcome =
				Resolver->AdvanceInputTime(HoldFixture::AutoReleaseTime);
			ExpectReleaseProducesTarget(*this, TEXT("On-time auto release"), Outcome, Resolver, Action_Root,
			                            Action_Heavy02);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldSurvivesCompletionTest,
		"CadenceArc.Resolver.Hold.SurvivesWindowCloseAndCompletion",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldSurvivesCompletionTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		// 窗口内按下 -> 关窗 -> 上一个动作正常 Completed -> 资格还在 -> 松手才产生候选
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestHandshake(*this, TEXT("Window closes at 1.3"), Resolver->CloseBufferWindow(Request.RequestId),
			              ECadenceArcHandshakeResult::Success);
			ExpectHoldStage(*this, TEXT("Window close"), Resolver, ECadenceArcHoldStage::Holding,
			                HoldFixture::PressTime);

			const FCadenceArcActionCompletionOutcome Completed =
				CompleteAt(*this, Resolver, Request.RequestId, 1.4);
			TestHandshake(*this, TEXT("Completion with a pending hold"), Completed.GetHandshakeResult(),
			              ECadenceArcHandshakeResult::Success);
			TestBufferConsumption(*this, TEXT("Completion with a pending hold"), Completed,
			                      ECadenceArcResolutionCategory::NoAction,
			                      ECadenceArcResolutionReason::WaitingForRelease);
			TestEmptyRequest(*this, Completed.GetNextActionRequest());
			TestState(*this, TEXT("Completion returns Ready"), Resolver->GetState(),
			          ECadenceArcResolverState::Ready);
			TestTag(*this, TEXT("Completion keeps the committed node"), Resolver->GetCurrentActionTag(),
			        Action_Light01);
			TestFalse(TEXT("Completion closes the window"), Resolver->IsBufferWindowOpen());
			TestEqual(TEXT("Completion clears the request"), Resolver->GetOutstandingRequest().RequestId,
			          int64{0});
			TestFalse(TEXT("A pending hold is not a buffered tag"), Resolver->GetBufferedInputTag().IsValid());
			// 蓄力继续计时，不会因为前一个动作结束而退化成普通攻击
			ExpectHoldStage(*this, TEXT("After completion"), Resolver, ECadenceArcHoldStage::Charging, 1.4);

			const FCadenceArcInputAdvanceOutcome Released =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.8));
			ExpectStageChanges(*this, TEXT("Release after completion"), Released,
			                   {ECadenceArcHoldStage::Charged}, {HoldFixture::ChargeFullTime});
			ExpectReleaseProducesTarget(*this, TEXT("Release after completion"), Released, Resolver,
			                            Action_Light01, Action_Finisher02);
		}

		// 完成时间早于已观察到的时刻：写状态之前拒绝
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Resolver->AdvanceInputTime(1.3);
			const FResolverSnapshot Before(Resolver);
			const FCadenceArcActionCompletionOutcome Early =
				CompleteAt(*this, Resolver, Request.RequestId, 1.2);
			TestHandshake(*this, TEXT("Backwards completion"), Early.GetHandshakeResult(),
			              ECadenceArcHandshakeResult::InvalidCompletionTime);
			Before.ExpectUnchanged(*this, Resolver);
			ExpectHoldStage(*this, TEXT("Backwards completion"), Resolver, ECadenceArcHoldStage::Charging, 1.3);
			for (const double Invalid : InvalidTimes())
			{
				TestHandshake(*this, *FString::Printf(TEXT("Invalid completion %g"), Invalid),
				              CompleteAt(*this, Resolver, Request.RequestId, Invalid).GetHandshakeResult(),
				              ECadenceArcHandshakeResult::InvalidCompletionTime);
			}
			Before.ExpectUnchanged(*this, Resolver);
		}

		// 自动释放已到期：Completed 无副作用返回 InputTimeAdvanceRequired
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }

			// 截止之前只是 Charged，不要求补 Advance
			TestHandshake(*this, TEXT("Before the deadline completion is fine"),
			              CompleteAt(*this, Resolver, Request.RequestId, 3.4).GetHandshakeResult(),
			              ECadenceArcHandshakeResult::Success);

			UCadenceArcResolver* Blocked = MakeChargeResolver(*this);
			FCadenceArcActionRequest BlockedRequest;
			if (!EnterExecutingWithOpenWindow(*this, Blocked, BlockedRequest)) { return false; }
			if (!GrantHoldInReady(*this, Blocked, Token)) { return false; }
			const FResolverSnapshot BlockedBefore(Blocked);
			TestHandshake(*this, TEXT("Missed advance blocks completion"),
			              CompleteAt(*this, Blocked, BlockedRequest.RequestId, HoldFixture::AutoReleaseTime)
			              .GetHandshakeResult(),
			              ECadenceArcHandshakeResult::InputTimeAdvanceRequired);
			BlockedBefore.ExpectUnchanged(*this, Blocked);
			ExpectHoldStage(*this, TEXT("Missed advance"), Blocked, ECadenceArcHoldStage::Holding,
			                HoldFixture::PressTime);

			// 先推进再重试：释放存成缓冲事件，Completed 正常消费
			const FCadenceArcInputAdvanceOutcome Advanced =
				Blocked->AdvanceInputTime(HoldFixture::AutoReleaseTime);
			ExpectRelease(*this, TEXT("Recovered advance"), Advanced, ECadenceArcInputReleaseSource::HoldLimit,
			              HoldFixture::AutoReleaseTime, HoldFixture::AutoReleaseTime - HoldFixture::PressTime);
			TestSubmit(*this, TEXT("Recovered advance buffers"), Advanced.GetResolution(),
			           ECadenceArcResolutionCategory::Buffered, ECadenceArcResolutionReason::None);
			const FCadenceArcActionCompletionOutcome Retried =
				CompleteAt(*this, Blocked, BlockedRequest.RequestId, 3.6);
			TestBufferConsumption(*this, TEXT("Retried completion"), Retried,
			                      ECadenceArcResolutionCategory::RequestProduced,
			                      ECadenceArcResolutionReason::None);
			TestTag(*this, TEXT("Retried completion target"), Retried.GetNextActionRequest().TargetActionTag,
			        Action_Finisher02);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldCancelTest,
		"CadenceArc.Resolver.Hold.CancelAndLifecycleCleanup",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldCancelTest::RunTest(const FString& Parameters)
	{
		const FCadenceArcInputToken Token = MakeHoldToken();

		// 不匹配的身份不能清掉别人的资格
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			ExpectHoldOutcome(*this, TEXT("Cancel without a hold"), Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			ExpectHoldOutcome(*this, TEXT("Cancel with a mismatched token"),
			                  Resolver->CancelInputHold(MakeHoldToken(2)),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
			ExpectHoldOutcome(*this, TEXT("Cancel with an invalid token"),
			                  Resolver->CancelInputHold(FCadenceArcInputToken{}),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
			ExpectHoldStage(*this, TEXT("Failed cancel"), Resolver, ECadenceArcHoldStage::Holding,
			                HoldFixture::PressTime);

			// 取消只清槽：不合成松手、不产生候选、不改变动作状态
			ExpectHoldOutcome(*this, TEXT("Cancel"), Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Cancelled, ECadenceArcResolutionReason::None);
			ExpectNoHold(*this, TEXT("Cancel"), Resolver);
			TestState(*this, TEXT("Cancel keeps Ready"), Resolver->GetState(), ECadenceArcResolverState::Ready);
			TestEqual(TEXT("Cancel allocates no request"), Resolver->GetOutstandingRequest().RequestId, int64{0});
			TestTag(*this, TEXT("Cancel keeps the committed node"), Resolver->GetCurrentActionTag(), Action_Root);
			ExpectHoldOutcome(*this, TEXT("Second cancel"), Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
		}

		// 取消同样覆盖"已经松手但还没消费"的缓冲事件
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1));
			TestTag(*this, TEXT("Released hold is buffered"), Resolver->GetBufferedInputTag(), Input_Heavy);
			ExpectHoldOutcome(*this, TEXT("Cancel a hold buffer"), Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Cancelled, ECadenceArcResolutionReason::None);
			TestFalse(TEXT("Cancelled hold buffer is gone"), Resolver->GetBufferedInputTag().IsValid());
			const FCadenceArcActionCompletionOutcome Completed =
				CompleteAt(*this, Resolver, Request.RequestId, 1.2);
			TestBufferConsumption(*this, TEXT("Completion after cancel"), Completed,
			                      ECadenceArcResolutionCategory::NoAction,
			                      ECadenceArcResolutionReason::NoBufferedInput);
		}

		// 普通 Pressed 缓存不带身份，取消调用不能顺手清掉它
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			FCadenceArcActionRequest Request;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
			FCadenceArcActionRequest Output;
			TestTransition(*this, TEXT("Plain buffer setup"),
			               SubmitForTest(Resolver, MakeHoldPress(Input_Light, 1.0), Output),
			               ExpectedResolution(ECadenceArcResolutionCategory::Buffered,
			                                  ECadenceArcResolutionReason::None));
			ExpectHoldOutcome(*this, TEXT("Cancel a plain buffer"), Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
			TestTag(*this, TEXT("Plain buffer survives cancel"), Resolver->GetBufferedInputTag(), Input_Light);
		}

		// 取消不撤销已经提交的候选动作
		{
			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			const FCadenceArcInputAdvanceOutcome Released =
				Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1));
			const FCadenceArcActionRequest Request = Released.GetResolution().GetActionRequest();
			ExpectHoldOutcome(*this, TEXT("Cancel after the candidate is committed"),
			                  Resolver->CancelInputHold(Token),
			                  ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::NoMatchingHold);
			TestEqual(TEXT("Cancel keeps the outstanding request"),
			          Resolver->GetOutstandingRequest().RequestId, Request.RequestId);
			TestHandshake(*this, TEXT("Committed candidate still starts"),
			              Resolver->NotifyActionStarted(Request.RequestId), ECadenceArcHandshakeResult::Success);
		}

		// 生命周期清理：打断、取消、Reset 与重新 Initialize 都会让旧资格失效
		{
			for (int32 Mode = 0; Mode < 2; ++Mode)
			{
				UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
				FCadenceArcActionRequest Request;
				if (!EnterExecutingWithOpenWindow(*this, Resolver, Request)) { return false; }
				if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
				TestHandshake(*this, TEXT("Lifecycle notification"),
				              Mode == 0
					              ? Resolver->NotifyActionInterrupted(Request.RequestId)
					              : Resolver->NotifyActionCancelled(Request.RequestId),
				              ECadenceArcHandshakeResult::Success);
				ExpectNoHold(*this, TEXT("Lifecycle notification"), Resolver);
				ExpectAdvanceRejected(*this, TEXT("Release after lifecycle cleanup"),
				                      Resolver->ReleaseInputHold(
					                      Token, MakeHoldRelease(Input_Heavy, HoldFixture::PressTime, 1.1)),
				                      ECadenceArcResolutionReason::NoMatchingHold);
			}

			UCadenceArcResolver* Resolver = MakeChargeResolver(*this);
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestEqual(TEXT("Reset succeeds"), static_cast<int32>(Resolver->Reset()),
			          static_cast<int32>(ECadenceArcResolverResetResult::Success));
			ExpectNoHold(*this, TEXT("Reset"), Resolver);

			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			TestInit(*this, TEXT("Re-initialize succeeds"), Resolver->Initialize(MakeChargeGraph()),
			         ECadenceArcResolverInitResult::Success);
			ExpectNoHold(*this, TEXT("Re-initialize"), Resolver);
		}
		return !HasAnyErrors();
	}
}

// 本轮审查补充：不用二进制精确的 0.5 门槛掩盖绝对时间与持续时间往返的边界。
namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldDecimalThresholdReviewTest,
		"CadenceArc.Resolver.Hold.Review.DecimalFullChargeBoundary",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldDecimalThresholdReviewTest::RunTest(const FString& Parameters)
	{
		// 四条真实调用路线：Ready 自动释放、迟到的物理松手、执行中自动释放后消费、
		// 有正保持时间时恰好在快照报告的蓄满时刻物理松手。
		for (int32 Route = 0; Route < 4; ++Route)
		{
			UCadenceArcGraph* Graph = MakeChargeGraph(true, Route == 3 ? 2.0 : 0.0);
			for (FCadenceArcNode& Node : Graph->Nodes)
			{
				for (FCadenceArcHoldChargeConfig& Config : Node.HoldChargeConfigs)
				{
					Config.ChargeStartSeconds = 0.1;
				}
				for (FCadenceArcTransition& Edge : Node.Transitions)
				{
					if (Edge.InputPhase != ECadenceArcInputPhase::Released) { continue; }
					if (Edge.DurationRange.bHasMaxHeldDuration)
					{
						Edge.DurationRange.MaxHeldDurationSecondsExclusive = 0.2;
					}
					else { Edge.DurationRange.MinHeldDurationSeconds = 0.2; }
				}
			}
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			TestInit(*this, TEXT("Decimal threshold graph initializes"), Resolver->Initialize(Graph),
				ECadenceArcResolverInitResult::Success);
			FCadenceArcActionRequest Executing;
			if (Route == 2 && !EnterExecutingWithOpenWindow(*this, Resolver, Executing)) { return false; }
			const FCadenceArcInputToken Token = MakeHoldToken();
			ExpectHoldOutcome(*this, TEXT("Decimal threshold grant"),
				Resolver->BeginInputHold(Token, MakeHoldPress(Input_Heavy, 10.0)),
				ECadenceArcHoldResult::Granted, ECadenceArcResolutionReason::None);
			const double FullTime = Resolver->GetInputHoldSnapshot().ChargeFullTimestampSeconds;
			FCadenceArcInputAdvanceOutcome Released;
			if (Route == 1 || Route == 3)
			{
				if (Route == 3)
				{
					Resolver->AdvanceInputTime(FullTime);
					TestEqual(TEXT("Snapshot is already Charged"),
						static_cast<int32>(Resolver->GetInputHoldSnapshot().Stage),
						static_cast<int32>(ECadenceArcHoldStage::Charged));
				}
				Released = Resolver->ReleaseInputHold(Token,
					MakeHoldRelease(Input_Heavy, 10.0, Route == 1 ? 10.5 : FullTime));
			}
			else { Released = Resolver->AdvanceInputTime(FullTime); }
			TestTrue(TEXT("Decimal threshold release is accepted"), Released.HasRelease());
			AddInfo(FString::Printf(TEXT("Route=%d FullTime=%.17g Duration=%.17g Threshold=%.17g"),
				Route, FullTime, Released.GetReleasedInput().HeldDurationSeconds, 0.2));
			const FCadenceArcActionRequest Request = Route == 2
				? Resolver->NotifyActionCompleted(Executing.RequestId, FullTime).GetNextActionRequest()
				: Released.GetResolution().GetActionRequest();
			TestTag(*this, *FString::Printf(TEXT("Route %d must choose the charged tier at full charge"), Route),
				Request.TargetActionTag, Route == 2 ? Action_Finisher02 : Action_Heavy02);
			ExpectNoHold(*this, TEXT("Decimal threshold release consumes qualification once"), Resolver);
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldInvalidReplacementReviewTest,
		"CadenceArc.Resolver.Hold.Review.InvalidGraphCannotReplaceGrant",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldInvalidReplacementReviewTest::RunTest(const FString& Parameters)
	{
		// Initialize 之后修改资产：新资格必须重新校验自己的配置，不能吃掉旧有效资格。
		for (int32 Mutation = 0; Mutation < 3; ++Mutation)
		{
			UCadenceArcGraph* Graph = MakeChargeGraph();
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			TestInit(*this, TEXT("Replacement graph initializes"), Resolver->Initialize(Graph),
				ECadenceArcResolverInitResult::Success);
			const FCadenceArcInputToken OriginalToken = MakeHoldToken();
			if (!GrantHoldInReady(*this, Resolver, OriginalToken)) { return false; }
			FCadenceArcNode& Root = Graph->Nodes[0];
			if (Mutation == 0)
			{
				FCadenceArcHoldChargeConfig Duplicate = Root.HoldChargeConfigs[0];
				Duplicate.MaxChargedHoldSeconds = 8.0;
				Root.HoldChargeConfigs.Add(Duplicate);
			}
			else if (Mutation == 1)
			{
				Root.Transitions.Add(MakeHoldEdge(Input_Heavy, Action_Heavy02, 0.0, true, 0.5));
			}
			else
			{
				Root.HoldChargeConfigs.Reset();
				Root.Transitions[1].DurationRange.MinHeldDurationSeconds = -1.0;
			}
			TArray<FText> Errors;
			TestFalse(TEXT("Edited graph is demonstrably invalid"), Graph->ValidateGraph(Errors));
			const FCadenceArcHoldOutcome Attempt = Resolver->BeginInputHold(MakeHoldToken(2),
				MakeHoldPress(Input_Heavy, 1.1));
			ExpectHoldOutcome(*this, *FString::Printf(TEXT("Invalid replacement %d"), Mutation), Attempt,
				ECadenceArcHoldResult::Rejected, ECadenceArcResolutionReason::InvalidGraphConfiguration);
			const FCadenceArcHoldSnapshot After = Resolver->GetInputHoldSnapshot();
			TestTrue(TEXT("Invalid replacement preserves original token"), After.Token == OriginalToken);
			TestEqual(TEXT("Invalid replacement preserves original press time"), After.PressedTimestampSeconds, 1.0);
			TestEqual(TEXT("Invalid replacement does not allocate a request"), Resolver->GetOutstandingRequest().RequestId, int64{0});
		}
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldFrozenGrantReviewTest,
		"CadenceArc.Resolver.Hold.Review.FrozenEdgesConfigAndAge",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldFrozenGrantReviewTest::RunTest(const FString& Parameters)
	{
		// 已授予的资格继续按旧边和旧年龄上限消费，未来新申请才读取改动后的资产。
		for (int32 Expired = 0; Expired < 2; ++Expired)
		{
			UCadenceArcGraph* Graph = MakeChargeGraph(true, 2.0, 0.25);
			UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
			TestInit(*this, TEXT("Frozen graph initializes"), Resolver->Initialize(Graph), ECadenceArcResolverInitResult::Success);
			FCadenceArcActionRequest Executing;
			if (!EnterExecutingWithOpenWindow(*this, Resolver, Executing)) { return false; }
			const FCadenceArcInputToken Token = MakeHoldToken();
			if (!GrantHoldInReady(*this, Resolver, Token)) { return false; }
			Graph->MaxBufferedInputAgeSeconds = Expired ? 0.0 : 0.01;
			FCadenceArcNode& Source = Graph->Nodes[1];
			Source.HoldChargeConfigs[0].MaxChargedHoldSeconds = 0.0;
			for (FCadenceArcTransition& Edge : Source.Transitions)
			{
				if (Edge.InputPhase == ECadenceArcInputPhase::Released) { Edge.TargetActionTag = Action_Light01; }
			}
			TestFalse(TEXT("Edited zero hold limit does not release old grant early"), Resolver->AdvanceInputTime(1.5).HasRelease());
			Resolver->ReleaseInputHold(Token, MakeHoldRelease(Input_Heavy, 1.0, 1.5));
			const FCadenceArcActionCompletionOutcome Completed = Resolver->NotifyActionCompleted(
				Executing.RequestId, Expired ? 2.0 : 1.75);
			TestHandshake(*this, TEXT("Frozen grant completion"), Completed.GetHandshakeResult(), ECadenceArcHandshakeResult::Success);
			TestBufferConsumption(*this, TEXT("Frozen age limit"), Completed,
				Expired ? ECadenceArcResolutionCategory::NoAction : ECadenceArcResolutionCategory::RequestProduced,
				Expired ? ECadenceArcResolutionReason::Expired : ECadenceArcResolutionReason::None);
			if (!Expired) { TestTag(*this, TEXT("Frozen target survives asset edit"), Completed.GetNextActionRequest().TargetActionTag, Action_Finisher02); }
		}
		return !HasAnyErrors();
	}
}

// 2026-09-24 满蓄力边界修复的回归：用十进制门槛扫大量按下时刻，锁定玩家可见的行为——
// 保持上限为 0 时，自动释放必须出长按档。修复前 T满 = 0.8 时约 46% 的按下时刻会错选普通档；
// 若 /fp:fast 把 HoldTiming 里的修正化简掉，这个测试同样会失败。
namespace CadenceArc::Tests
{
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcHoldDecimalThresholdSweepTest,
		"CadenceArc.Resolver.Hold.DecimalThresholdSweep",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcHoldDecimalThresholdSweepTest::RunTest(const FString& Parameters)
	{
		// 与 P6-09 示例资产相同的门槛：T开始 0.2、T满 0.8、保持 0
		UCadenceArcGraph* Graph = MakeChargeGraph(true, 0.0);
		for (FCadenceArcNode& Node : Graph->Nodes)
		{
			for (FCadenceArcHoldChargeConfig& Config : Node.HoldChargeConfigs)
			{
				Config.ChargeStartSeconds = 0.2;
			}
			for (FCadenceArcTransition& Edge : Node.Transitions)
			{
				if (Edge.InputPhase != ECadenceArcInputPhase::Released) { continue; }
				if (Edge.DurationRange.bHasMaxHeldDuration)
				{
					Edge.DurationRange.MaxHeldDurationSecondsExclusive = 0.8;
				}
				else
				{
					Edge.DurationRange.MinHeldDurationSeconds = 0.8;
				}
			}
		}
		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		if (!TestInit(*this, TEXT("Sweep graph initializes"), Resolver->Initialize(Graph),
		              ECadenceArcResolverInitResult::Success))
		{
			return false;
		}

		constexpr int32 Samples = 2000;
		int32 WrongTier = 0;
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			// 无理数步长的倍数对一小时取模：确定可复现，又能覆盖尾数满精度的 double
			const double Pressed = std::fmod(Index * 11.326237921249264, 3600.0);
			if (Resolver->BeginInputHold(MakeHoldToken(Index + 1), MakeHoldPress(Input_Heavy, Pressed)).GetResult()
				!= ECadenceArcHoldResult::Granted)
			{
				AddError(FString::Printf(TEXT("Grant failed at press %.17g"), Pressed));
				return false;
			}

			// 恰好在快照报告的截止时刻推进：端点"大于等于"，这一刻就应释放
			const double Deadline = Resolver->GetInputHoldSnapshot().AutoReleaseTimestampSeconds;
			const FCadenceArcInputAdvanceOutcome Released = Resolver->AdvanceInputTime(Deadline);
			const FCadenceArcActionRequest Request = Released.GetResolution().GetActionRequest();
			if (!Released.HasActionRequest() || Request.TargetActionTag != Action_Heavy02)
			{
				if (++WrongTier <= 5)
				{
					AddInfo(FString::Printf(TEXT("Wrong tier at press %.17g: released=%d duration=%.17g target=%s"),
					                        Pressed, Released.HasRelease(),
					                        Released.GetReleasedInput().HeldDurationSeconds,
					                        *Request.TargetActionTag.ToString()));
				}
			}
			// 拒绝候选请求，回到 Root 的 Ready，准备下一次按下
			if (Released.HasActionRequest())
			{
				Resolver->NotifyActionRejected(Request.RequestId);
			}
		}
		TestEqual(TEXT("Zero-hold auto release always picks the charged tier"), WrongTier, 0);
		TestEqual(TEXT("Sweep leaves the resolver Ready at the entry node"),
		          Resolver->GetCurrentActionTag().ToString(), Action_Root.GetTag().ToString());
		return !HasAnyErrors();
	}
}

#endif // WITH_DEV_AUTOMATION_TESTS
