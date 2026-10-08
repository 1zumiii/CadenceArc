// 画布左下角的输入显示（BuildInputDisplay）。
//
// 被测契约：
// - 显示的是解析器收到的语义输入：Tag 最后一段、事件自带的上下文、按住时长；新的在前，最多 6 个，由新输入挤出去，旧的只变暗不消失。
// - 被拒绝或没起作用的输入标为 ignored，动作执行中按下的标为 buffered。
// - 第一行的持久上下文来自 GetContextTags；停顿只在 Ready 时按"最近一次调用方传入的时间 − 最近一次成功完成"计算。
// - 只读：构建前后 Resolver 不变（只调用 const 接口）。

#if WITH_DEV_AUTOMATION_TESTS

#include "Graph/CadenceArcGraph.h"
#include "Misc/AutomationTest.h"
#include "Tests/CadenceArcAutomationTags.h"
#include "Resolver/CadenceArcResolver.h"
#include "ViewModel/CadenceArcInputDisplay.h"

namespace CadenceArc::Editor::Tests
{
	static FGameplayTag InputDisplayTag(const TCHAR* Name)
	{
		return CadenceArc::Tests::AutomationTag(Name); // 第一次请求时由 Runtime 模块注册
	}

	static FGameplayTag Display_Root() { return InputDisplayTag(TEXT("CadenceArc.Automation.Action.Root")); }
	static FGameplayTag Display_Light01() { return InputDisplayTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	static FGameplayTag Display_Heavy01() { return InputDisplayTag(TEXT("CadenceArc.Automation.Action.Heavy01")); }
	static FGameplayTag Display_Finisher01() { return InputDisplayTag(TEXT("CadenceArc.Automation.Action.Finisher01")); }
	static FGameplayTag Display_Light() { return InputDisplayTag(TEXT("CadenceArc.Automation.Input.Light")); }
	static FGameplayTag Display_Heavy() { return InputDisplayTag(TEXT("CadenceArc.Automation.Input.Heavy")); }
	static FGameplayTag Display_Forward() { return InputDisplayTag(TEXT("CadenceArc.Automation.Context.Forward")); }
	static FGameplayTag Display_Air() { return InputDisplayTag(TEXT("CadenceArc.Automation.Context.Air")); }

	static FCadenceArcTransition& AddDisplayEdge(
		FCadenceArcNode& Node, const FGameplayTag& Input, const FGameplayTag& Target, const ECadenceArcInputPhase Phase)
	{
		FCadenceArcTransition& Edge = Node.Transitions.AddDefaulted_GetRef();
		Edge.InputTag = Input;
		Edge.TargetActionTag = Target;
		Edge.InputPhase = Phase;
		return Edge;
	}

	// Root：Light P → Light01，Heavy R → Heavy01；Light01：Heavy P 需要 Forward → Finisher01
	static UCadenceArcResolver* MakeDisplayResolver(FAutomationTestBase& Test)
	{
		UCadenceArcGraph* Graph = NewObject<UCadenceArcGraph>();
		Graph->EntryActionTag = Display_Root();
		Graph->Nodes.Reserve(4);
		FCadenceArcNode& Root = Graph->Nodes.AddDefaulted_GetRef();
		Root.ActionTag = Display_Root();
		AddDisplayEdge(Root, Display_Light(), Display_Light01(), ECadenceArcInputPhase::Pressed);
		AddDisplayEdge(Root, Display_Heavy(), Display_Heavy01(), ECadenceArcInputPhase::Released);
		FCadenceArcNode& Light01 = Graph->Nodes.AddDefaulted_GetRef();
		Light01.ActionTag = Display_Light01();
		AddDisplayEdge(Light01, Display_Heavy(), Display_Finisher01(), ECadenceArcInputPhase::Pressed)
			.RequiredContextTags.AddTag(Display_Forward());
		Graph->Nodes.AddDefaulted_GetRef().ActionTag = Display_Heavy01();
		Graph->Nodes.AddDefaulted_GetRef().ActionTag = Display_Finisher01();

		UCadenceArcResolver* Resolver = NewObject<UCadenceArcResolver>();
		Test.TestTrue(TEXT("Display fixture initializes"),
		              Resolver->Initialize(Graph) == ECadenceArcResolverInitResult::Success);
		return Resolver;
	}

	static FCadenceArcInputEvent MakeDisplayInput(
		const FGameplayTag& Input, const double Timestamp, const FGameplayTag& Context = FGameplayTag())
	{
		FCadenceArcInputEvent Event;
		Event.InputTag = Input;
		Event.TimestampSeconds = Timestamp;
		if (Context.IsValid())
		{
			Event.ContextTags.AddTag(Context);
		}
		return Event;
	}

	// "Forward+Light@0.20"：上下文和输入按显示顺序用 + 连起来，括号里是按住时长
	static FString DescribeChips(const FCadenceArcInputDisplay& Display)
	{
		TArray<FString> Parts;
		for (const FCadenceArcInputChip& Chip : Display.Recent)
		{
			TArray<FString> Keys = Chip.Context;
			Keys.Add(Chip.Input + (Chip.bReleased ? TEXT("↑") : TEXT("")));
			Parts.Add(FString::Printf(TEXT("%s%s%s%s@%.2f"), *FString::Join(Keys, TEXT("+")),
			                          Chip.Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("(%s)"), *Chip.Detail),
			                          Chip.bIgnored ? TEXT(" ignored") : TEXT(""),
			                          Chip.bBuffered ? TEXT(" buffered") : TEXT(""), Chip.AgeSeconds));
		}
		return FString::Join(Parts, TEXT(" | "));
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputDisplayRecentTest,
		"CadenceArc.Editor.InputDisplay.RecentInputsAndPause",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputDisplayRecentTest::RunTest(const FString& Parameters)
	{
		TestFalse(TEXT("Uninitialized resolver shows nothing"),
		          BuildInputDisplay(*NewObject<UCadenceArcResolver>()).bValid);

		UCadenceArcResolver* Resolver = MakeDisplayResolver(*this);
		FCadenceArcInputDisplay Display = BuildInputDisplay(*Resolver);
		TestTrue(TEXT("Initialized resolver is shown"), Display.bValid);
		TestEqual(TEXT("No persistent context"), Display.PersistentContext, FString(TEXT("none")));
		TestTrue(TEXT("No host time yet: no pause, no inputs"), Display.PauseText.IsEmpty() && Display.Recent.IsEmpty());

		// 按下：带事件上下文；持久上下文单独显示在第一行
		Resolver->SetContextTags(FGameplayTagContainer(Display_Air()));
		const FCadenceArcActionRequest First =
			Resolver->SubmitInput(MakeDisplayInput(Display_Light(), 1.0, Display_Forward())).GetActionRequest();
		Display = BuildInputDisplay(*Resolver);
		TestEqual(TEXT("Persistent context"), Display.PersistentContext, FString(TEXT("Air")));
		TestEqual(TEXT("Awaiting start"), Display.PauseText, FString(TEXT("waiting for start")));
		TestEqual(TEXT("Press shows its own context"), DescribeChips(Display), FString(TEXT("Forward+Light@0.00")));

		// 执行中按下：进缓冲
		Resolver->NotifyActionStarted(First.RequestId);
		Resolver->OpenBufferWindow(First.RequestId);
		Resolver->SubmitInput(MakeDisplayInput(Display_Heavy(), 1.2));
		Display = BuildInputDisplay(*Resolver);
		TestEqual(TEXT("In action"), Display.PauseText, FString(TEXT("in action, buffered input counts as pause 0")));
		TestEqual(TEXT("Newest first, buffered marked"), DescribeChips(Display),
		          FString(TEXT("Heavy buffered@0.00 | Forward+Light@0.20")));

		// 完成：缓冲的 Heavy 没带 Forward，没出招；回到 Ready，停顿从完成时刻算
		Resolver->NotifyActionCompleted(First.RequestId, 1.5);
		TestEqual(TEXT("Pause starts at completion"), BuildInputDisplay(*Resolver).PauseText, FString(TEXT("pause 0.00s")));
		Resolver->AdvanceInputTime(1.9);
		Display = BuildInputDisplay(*Resolver);
		TestEqual(TEXT("Pause follows host time"), Display.PauseText, FString(TEXT("pause 0.40s")));
		TestEqual(TEXT("Ages follow host time"), DescribeChips(Display),
		          FString(TEXT("Heavy buffered@0.70 | Forward+Light@0.90")));

		// Light01 上没有 Light 的边：输入没起作用
		Resolver->SubmitInput(MakeDisplayInput(Display_Light(), 1.95));
		TestEqual(TEXT("Ignored input marked"), DescribeChips(BuildInputDisplay(*Resolver)),
		          FString(TEXT("Light ignored@0.00 | Heavy buffered@0.75 | Forward+Light@0.95")));

		// 旧的输入不按时间移除，只变暗（GetChipAlpha）；由新输入挤出去
		Resolver->AdvanceInputTime(3.3);
		TestEqual(TEXT("Old inputs stay"), DescribeChips(BuildInputDisplay(*Resolver)),
		          FString(TEXT("Light ignored@1.35 | Heavy buffered@2.10 | Forward+Light@2.30")));
		for (int32 Press = 1; Press <= FCadenceArcInputDisplay::MaxRecentChips; ++Press)
		{
			Resolver->SubmitInput(MakeDisplayInput(Display_Light(), 3.3 + Press * 0.1));
		}
		Display = BuildInputDisplay(*Resolver);
		TestEqual(TEXT("At most six inputs"), Display.Recent.Num(), FCadenceArcInputDisplay::MaxRecentChips);
		TestTrue(TEXT("Older inputs pushed out"), !DescribeChips(Display).Contains(TEXT("Heavy")));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputDisplayHoldTest,
		"CadenceArc.Editor.InputDisplay.HoldAndRelease",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputDisplayHoldTest::RunTest(const FString& Parameters)
	{
		UCadenceArcResolver* Resolver = MakeDisplayResolver(*this);
		FCadenceArcInputToken Token;
		Token.SourceSession = FGuid(9, 0x0A0B0C0D, 0x11223344, 0x55667788);
		Token.PressId = 1;
		if (!TestTrue(TEXT("Hold granted"), Resolver->BeginInputHold(
			              Token, MakeDisplayInput(Display_Heavy(), 1.0, Display_Forward())).GetResult()
		              == ECadenceArcHoldResult::Granted))
		{
			return false;
		}
		Resolver->AdvanceInputTime(1.3);
		FCadenceArcInputDisplay Display = BuildInputDisplay(*Resolver);
		TestTrue(TEXT("Holding"), Display.bHolding && Display.HeldInput == TEXT("Heavy")
		         && Display.HeldStage == ECadenceArcHoldStage::Holding);
		TestEqual(TEXT("Held time"), Display.HeldSeconds, 0.3, 1.e-9);
		TestEqual(TEXT("Press chip"), DescribeChips(Display), FString(TEXT("Forward+Heavy@0.30")));

		FCadenceArcInputEvent Release = MakeDisplayInput(Display_Heavy(), 1.5);
		Release.InputPhase = ECadenceArcInputPhase::Released;
		Release.HeldDurationSeconds = 0.5;
		Resolver->ReleaseInputHold(Token, Release);
		Display = BuildInputDisplay(*Resolver);
		TestFalse(TEXT("Hold ended"), Display.bHolding);
		TestEqual(TEXT("Release chip shows held time"), DescribeChips(Display),
		          FString(TEXT("Heavy↑(0.50s)@0.00 | Forward+Heavy@0.50")));
		return !HasAnyErrors();
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcInputDisplayFadeTest,
		"CadenceArc.Editor.InputDisplay.Fade",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcInputDisplayFadeTest::RunTest(const FString& Parameters)
	{
		TestEqual(TEXT("New input is opaque"), FCadenceArcInputDisplay::GetChipAlpha(0.0), 1.f);
		TestEqual(TEXT("Opaque until the fade starts"), FCadenceArcInputDisplay::GetChipAlpha(1.5), 1.f);
		TestEqual(TEXT("Half way through the fade"), FCadenceArcInputDisplay::GetChipAlpha(1.75), 0.7f, 1.e-5f);
		TestEqual(TEXT("Dimmed after the fade"), FCadenceArcInputDisplay::GetChipAlpha(2.0), 0.4f, 1.e-5f);
		TestEqual(TEXT("Old inputs stay dimmed, not hidden"), FCadenceArcInputDisplay::GetChipAlpha(60.0), 0.4f, 1.e-5f);
		return !HasAnyErrors();
	}
}

#endif
