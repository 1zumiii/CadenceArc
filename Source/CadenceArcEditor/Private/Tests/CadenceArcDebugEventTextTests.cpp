// FormatDebugEvent 的测试：历史 Tab 上给策划看的文字。
//
// 被测契约（Phase 7B / P7-09）：
// - 成功只有一行摘要，FailureDetail 为空；失败才附带结果和原因，原因末尾带枚举名方便程序对照。
// - 节点和输入只显示 Tag 的最后一段；有时间才显示时间。
// - "动作完成时没有缓冲输入"是正常情况，不带失败说明。

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ViewModel/CadenceArcDebugEventText.h"

namespace CadenceArc::Editor::Tests
{
	static FGameplayTag TextTag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name));
	}

	static FGameplayTag Text_Root() { return TextTag(TEXT("CadenceArc.Automation.Action.Root")); }
	static FGameplayTag Text_Light01() { return TextTag(TEXT("CadenceArc.Automation.Action.Light01")); }
	static FGameplayTag Text_Light() { return TextTag(TEXT("CadenceArc.Automation.Input.Light")); }
	static FGameplayTag Text_Heavy() { return TextTag(TEXT("CadenceArc.Automation.Input.Heavy")); }

	static FCadenceArcDebugEvent MakeTextEvent(const ECadenceArcDebugOperation Operation)
	{
		FCadenceArcDebugEvent Event;
		Event.Operation = Operation;
		Event.CommittedBefore = Text_Root();
		Event.CommittedAfter = Text_Root();
		return Event;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FCadenceArcDebugEventTextTest,
		"CadenceArc.Editor.History.EventText",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FCadenceArcDebugEventTextTest::RunTest(const FString& Parameters)
	{
		// 输入产生候选：一行摘要，没有失败说明
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::SubmitInput);
			Event.InputTag = Text_Light();
			Event.bHasTimestamp = true;
			Event.TimestampSeconds = 1.234;
			Event.Category = ECadenceArcResolutionCategory::RequestProduced;
			Event.ProducedRequest.RequestId = 5;
			Event.ProducedRequest.TargetActionTag = Text_Light01();
			const FCadenceArcDebugEventText Text = FormatDebugEvent(Event);
			TestEqual(TEXT("Produced summary"), Text.Summary, FString(TEXT("Light P at Root → Light01 (request #5)")));
			TestEqual(TEXT("Produced time"), Text.Time, FString(TEXT("1.23s")));
			TestFalse(TEXT("Produced is not a failure"), Text.bFailed);
			TestTrue(TEXT("Produced has no failure detail"), Text.FailureDetail.IsEmpty());
		}
		// 输入没有匹配的边：失败，说明里讲清楚在哪个节点、哪个输入，并带枚举名
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::SubmitInput);
			Event.InputTag = Text_Heavy();
			Event.bFailed = true;
			Event.Category = ECadenceArcResolutionCategory::NoAction;
			Event.Reason = ECadenceArcResolutionReason::NoMatchingTransition;
			const FCadenceArcDebugEventText Text = FormatDebugEvent(Event);
			TestEqual(TEXT("Ignored summary"), Text.Summary, FString(TEXT("Heavy P ignored at Root")));
			TestEqual(TEXT("Ignored detail"), Text.FailureDetail,
			          FString(TEXT("No transition for Heavy P from Root (NoMatchingTransition)")));
			TestTrue(TEXT("Ignored is a failure"), Text.bFailed);
			TestTrue(TEXT("No time when none was given"), Text.Time.IsEmpty());
		}
		// 过期的 Started：失败，说明当前请求是哪一个
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::ActionStarted);
			Event.bFailed = true;
			Event.CallerRequestId = 3;
			Event.HandshakeResult = ECadenceArcHandshakeResult::RequestIdMismatch;
			Event.RequestBefore.RequestId = 7;
			const FCadenceArcDebugEventText Text = FormatDebugEvent(Event);
			TestEqual(TEXT("Stale summary"), Text.Summary, FString(TEXT("Started #3 ignored")));
			TestTrue(TEXT("No time recorded shows no time"), Text.Time.IsEmpty());
			TestEqual(TEXT("Stale detail"), Text.FailureDetail,
			          FString(TEXT("Stale callback: current request is #7 (RequestIdMismatch)")));
		}
		// 借用之前调用的时间：前面加 "~"
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::OpenWindow);
			Event.bHasTimestamp = true;
			Event.bTimeFromLastCall = true;
			Event.TimestampSeconds = 53.8;
			TestEqual(TEXT("Borrowed time"), FormatDebugEvent(Event).Time, FString(TEXT("~53.80s")));
		}
		// 正常完成、没有缓冲：不是失败，不带说明
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::ActionCompleted);
			Event.CommittedBefore = Text_Light01();
			Event.CommittedAfter = Text_Light01();
			Event.Category = ECadenceArcResolutionCategory::NoAction;
			Event.Reason = ECadenceArcResolutionReason::NoBufferedInput;
			const FCadenceArcDebugEventText Text = FormatDebugEvent(Event);
			TestEqual(TEXT("Finished summary"), Text.Summary, FString(TEXT("Light01 finished")));
			TestTrue(TEXT("Finished has no failure detail"), Text.FailureDetail.IsEmpty());
		}
		// 缓冲的输入过期被丢弃：失败
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::ActionCompleted);
			Event.CommittedBefore = Text_Light01();
			Event.bFailed = true;
			Event.Category = ECadenceArcResolutionCategory::NoAction;
			Event.Reason = ECadenceArcResolutionReason::Expired;
			const FCadenceArcDebugEventText Text = FormatDebugEvent(Event);
			TestEqual(TEXT("Dropped summary"), Text.Summary, FString(TEXT("Light01 finished, buffered input dropped")));
			TestEqual(TEXT("Dropped detail"), Text.FailureDetail,
			          FString(TEXT("Buffered input was too old when the action finished (Expired)")));
		}
		// 蓄力阶段与自动释放
		{
			FCadenceArcDebugEvent Event = MakeTextEvent(ECadenceArcDebugOperation::AdvanceTime);
			Event.InputTag = Text_Heavy();
			Event.StageReached = ECadenceArcHoldStage::Charged;
			TestEqual(TEXT("Charged summary"), FormatDebugEvent(Event).Summary, FString(TEXT("Heavy fully charged")));

			Event.StageReached = ECadenceArcHoldStage::None;
			Event.ReleaseSource = ECadenceArcInputReleaseSource::HoldLimit;
			Event.bHasHeldDuration = true;
			Event.HeldDurationSeconds = 1.8;
			Event.Category = ECadenceArcResolutionCategory::RequestProduced;
			Event.ProducedRequest.RequestId = 9;
			Event.ProducedRequest.TargetActionTag = Text_Light01();
			TestEqual(TEXT("Auto release summary"), FormatDebugEvent(Event).Summary,
			          FString(TEXT("Heavy auto-released after 1.80s → Light01 (request #9)")));
		}
		return !HasAnyErrors();
	}
}

#endif
