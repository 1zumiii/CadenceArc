#include "CadenceArcDebugEventText.h"

namespace
{
	// CadenceArc.Test.Action.SkillA -> SkillA；空 Tag 显示为 "?"
	FString ShortName(const FGameplayTag& Tag)
	{
		if (!Tag.IsValid())
		{
			return TEXT("?");
		}
		FString Name = Tag.ToString();
		int32 Dot = INDEX_NONE;
		if (Name.FindLastChar(TEXT('.'), Dot))
		{
			Name.RightChopInline(Dot + 1);
		}
		return Name;
	}

	// "Light P" / "Heavy R"
	FString InputName(const FCadenceArcDebugEvent& Event)
	{
		return FString::Printf(TEXT("%s %s"), *ShortName(Event.InputTag),
		                       Event.InputPhase == ECadenceArcInputPhase::Released ? TEXT("R") : TEXT("P"));
	}

	FString RequestText(const FCadenceArcActionRequest& Request)
	{
		return FString::Printf(TEXT("%s (request #%lld)"), *ShortName(Request.TargetActionTag),
		                       static_cast<long long>(Request.RequestId));
	}

	template <typename TEnum>
	FString EnumName(const TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	// 原因的白话说明，末尾附上枚举名方便程序对照
	FString DescribeReason(const FCadenceArcDebugEvent& Event)
	{
		FString Text;
		switch (Event.Reason)
		{
		case ECadenceArcResolutionReason::NotInitialized: Text = TEXT("Resolver is not initialized"); break;
		case ECadenceArcResolutionReason::InvalidInputTag: Text = TEXT("Input tag is invalid"); break;
		case ECadenceArcResolutionReason::InvalidTimestamp: Text = TEXT("Time is invalid or went backwards"); break;
		case ECadenceArcResolutionReason::RequestPending: Text = TEXT("Previous action has not started yet"); break;
		case ECadenceArcResolutionReason::BufferWindowClosed: Text = TEXT("Buffer window is closed"); break;
		case ECadenceArcResolutionReason::CurrentNodeNotFound: Text = TEXT("Current node is missing from the graph"); break;
		case ECadenceArcResolutionReason::NoMatchingTransition:
			Text = FString::Printf(TEXT("No transition for %s from %s"), *InputName(Event),
			                       *ShortName(Event.CommittedBefore));
			break;
		case ECadenceArcResolutionReason::TargetNodeNotFound: Text = TEXT("Target node is missing from the graph"); break;
		case ECadenceArcResolutionReason::NoBufferedInput: Text = TEXT("No buffered input"); break;
		case ECadenceArcResolutionReason::InvalidCompletionTime: Text = TEXT("Completion time is invalid"); break;
		case ECadenceArcResolutionReason::Expired: Text = TEXT("Buffered input was too old when the action finished"); break;
		case ECadenceArcResolutionReason::InvalidInputEvent: Text = TEXT("Input event is malformed"); break;
		case ECadenceArcResolutionReason::InputIdentityRequired: Text = TEXT("A release must go through ReleaseInputHold"); break;
		case ECadenceArcResolutionReason::NoMatchingHold: Text = TEXT("No matching hold for this input"); break;
		case ECadenceArcResolutionReason::HoldProtected: Text = TEXT("A charge is in progress, other input is blocked"); break;
		case ECadenceArcResolutionReason::InputTimeAdvanceRequired: Text = TEXT("Host must advance input time first"); break;
		case ECadenceArcResolutionReason::WaitingForRelease: Text = TEXT("Waiting for the button to be released"); break;
		case ECadenceArcResolutionReason::InvalidGraphConfiguration: Text = TEXT("Graph configuration cannot explain this input"); break;
		default: Text = TEXT("Rejected"); break;
		}
		return FString::Printf(TEXT("%s (%s)"), *Text, *EnumName(Event.Reason));
	}

	FString DescribeHandshake(const FCadenceArcDebugEvent& Event)
	{
		FString Text;
		switch (Event.HandshakeResult)
		{
		case ECadenceArcHandshakeResult::NotInitialized: Text = TEXT("Resolver is not initialized"); break;
		case ECadenceArcHandshakeResult::InvalidRequestId: Text = TEXT("Request id is invalid"); break;
		case ECadenceArcHandshakeResult::UnexpectedState:
			Text = FString::Printf(TEXT("Not expected while %s"), *EnumName(Event.StateBefore));
			break;
		case ECadenceArcHandshakeResult::RequestIdMismatch:
			Text = FString::Printf(TEXT("Stale callback: current request is #%lld"),
			                       static_cast<long long>(Event.RequestBefore.RequestId));
			break;
		case ECadenceArcHandshakeResult::InvalidCompletionTime: Text = TEXT("Completion time is invalid"); break;
		case ECadenceArcHandshakeResult::InputTimeAdvanceRequired: Text = TEXT("Host must advance input time first"); break;
		default: Text = TEXT("Rejected"); break;
		}
		return FString::Printf(TEXT("%s (%s)"), *Text, *EnumName(Event.HandshakeResult));
	}

	// 解析得到了什么：候选、缓冲，或者什么都没有
	FString ResolutionTail(const FCadenceArcDebugEvent& Event)
	{
		if (Event.Category == ECadenceArcResolutionCategory::RequestProduced)
		{
			return FString::Printf(TEXT(" → %s"), *RequestText(Event.ProducedRequest));
		}
		if (Event.Category == ECadenceArcResolutionCategory::Buffered)
		{
			return TEXT(", buffered");
		}
		return TEXT(", no action");
	}

	FString HeldText(const FCadenceArcDebugEvent& Event)
	{
		return FString::Printf(TEXT("%.2fs"), Event.HeldDurationSeconds);
	}
}

FCadenceArcDebugEventText FormatDebugEvent(const FCadenceArcDebugEvent& Event)
{
	FCadenceArcDebugEventText Text;
	Text.bFailed = Event.bFailed;
	if (Event.bHasTimestamp)
	{
		// "~" 表示这个操作本身不带时间，取的是此前最近一次带时间的调用（误差不超过一帧）
		Text.Time = FString::Printf(TEXT("%s%.2fs"), Event.bTimeFromLastCall ? TEXT("~") : TEXT(""),
		                            Event.TimestampSeconds);
	}
	const FString Committed = ShortName(Event.CommittedBefore);
	const FString RequestId = FString::Printf(TEXT("#%lld"), static_cast<long long>(Event.CallerRequestId));

	switch (Event.Operation)
	{
	case ECadenceArcDebugOperation::Initialize:
		Text.Summary = Event.bFailed ? TEXT("Initialize failed")
		                             : FString::Printf(TEXT("Initialized at %s"), *ShortName(Event.CommittedAfter));
		if (Event.bFailed)
		{
			Text.FailureDetail = EnumName(Event.InitResult);
		}
		break;
	case ECadenceArcDebugOperation::Reset:
		Text.Summary = Event.bFailed ? TEXT("Reset refused")
		                             : FString::Printf(TEXT("Reset → %s"), *ShortName(Event.CommittedAfter));
		if (Event.bFailed)
		{
			Text.FailureDetail = FString::Printf(TEXT("Cannot reset while %s (%s)"),
			                                     *EnumName(Event.StateBefore), *EnumName(Event.ResetResult));
		}
		break;
	case ECadenceArcDebugOperation::SubmitInput:
		Text.Summary = Event.bFailed
			? FString::Printf(TEXT("%s ignored at %s"), *InputName(Event), *Committed)
			: FString::Printf(TEXT("%s at %s%s"), *InputName(Event), *Committed, *ResolutionTail(Event));
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event);
		}
		break;
	case ECadenceArcDebugOperation::BeginHold:
		Text.Summary = Event.bFailed
			? FString::Printf(TEXT("%s hold refused at %s"), *ShortName(Event.InputTag), *Committed)
			: FString::Printf(TEXT("%s held at %s"), *ShortName(Event.InputTag), *Committed);
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event);
		}
		break;
	case ECadenceArcDebugOperation::ReleaseHold:
	case ECadenceArcDebugOperation::AdvanceTime:
		{
			const bool bAuto = Event.ReleaseSource == ECadenceArcInputReleaseSource::HoldLimit;
			if (Event.ReleaseSource != ECadenceArcInputReleaseSource::None)
			{
				Text.Summary = FString::Printf(TEXT("%s %s after %s%s"), *ShortName(Event.InputTag),
				                               bAuto ? TEXT("auto-released") : TEXT("released"), *HeldText(Event),
				                               *ResolutionTail(Event));
			}
			else if (Event.StageReached == ECadenceArcHoldStage::Charged)
			{
				Text.Summary = FString::Printf(TEXT("%s fully charged"), *ShortName(Event.InputTag));
			}
			else if (Event.StageReached == ECadenceArcHoldStage::Charging)
			{
				Text.Summary = FString::Printf(TEXT("%s charging"), *ShortName(Event.InputTag));
			}
			else
			{
				Text.Summary = Event.Operation == ECadenceArcDebugOperation::ReleaseHold
					? FString::Printf(TEXT("%s release rejected"), *ShortName(Event.InputTag))
					: TEXT("Input time advance rejected");
			}
			if (Event.bFailed)
			{
				Text.FailureDetail = DescribeReason(Event);
			}
			break;
		}
	case ECadenceArcDebugOperation::CancelHold:
		Text.Summary = Event.bFailed ? TEXT("Hold cancel ignored")
		                             : FString::Printf(TEXT("%s hold cancelled"), *ShortName(Event.InputTag));
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event);
		}
		break;
	case ECadenceArcDebugOperation::ActionStarted:
		Text.Summary = Event.bFailed
			? FString::Printf(TEXT("Started %s ignored"), *RequestId)
			: FString::Printf(TEXT("Started %s (request %s)"), *ShortName(Event.CommittedAfter), *RequestId);
		break;
	case ECadenceArcDebugOperation::ActionRejected:
		Text.Summary = Event.bFailed
			? FString::Printf(TEXT("Rejected %s ignored"), *RequestId)
			: FString::Printf(TEXT("Executor rejected %s (request %s), stays at %s"),
			                  *ShortName(Event.RequestBefore.TargetActionTag), *RequestId,
			                  *ShortName(Event.CommittedAfter));
		break;
	case ECadenceArcDebugOperation::ActionCompleted:
		if (Event.HandshakeResult != ECadenceArcHandshakeResult::Success)
		{
			Text.Summary = FString::Printf(TEXT("Completed %s ignored"), *RequestId);
		}
		else if (Event.Category == ECadenceArcResolutionCategory::RequestProduced)
		{
			Text.Summary = FString::Printf(TEXT("%s finished, buffered input → %s"), *Committed,
			                               *RequestText(Event.ProducedRequest));
		}
		else
		{
			Text.Summary = Event.bFailed
				? FString::Printf(TEXT("%s finished, buffered input dropped"), *Committed)
				: FString::Printf(TEXT("%s finished"), *Committed);
		}
		if (Event.bFailed)
		{
			Text.FailureDetail = Event.HandshakeResult != ECadenceArcHandshakeResult::Success
				? DescribeHandshake(Event) : DescribeReason(Event);
		}
		break;
	case ECadenceArcDebugOperation::ActionCancelled:
	case ECadenceArcDebugOperation::ActionInterrupted:
		{
			const TCHAR* Verb = Event.Operation == ECadenceArcDebugOperation::ActionCancelled
				? TEXT("cancelled") : TEXT("interrupted");
			Text.Summary = Event.bFailed
				? FString::Printf(TEXT("%s %s ignored"), Verb, *RequestId)
				: FString::Printf(TEXT("%s %s → back to %s"), *Committed, Verb, *ShortName(Event.CommittedAfter));
			break;
		}
	case ECadenceArcDebugOperation::OpenWindow:
	case ECadenceArcDebugOperation::CloseWindow:
		{
			const bool bOpen = Event.Operation == ECadenceArcDebugOperation::OpenWindow;
			Text.Summary = FString::Printf(TEXT("Buffer window %s%s"), bOpen ? TEXT("opened") : TEXT("closed"),
			                               Event.bFailed ? TEXT(" ignored") : TEXT(""));
			break;
		}
	default:
		Text.Summary = TEXT("Unknown operation");
		break;
	}

	// 握手类操作的失败原因统一来自握手结果
	if (Event.bFailed && Text.FailureDetail.IsEmpty())
	{
		Text.FailureDetail = DescribeHandshake(Event);
	}
	if (!Event.bFailed)
	{
		Text.FailureDetail.Reset();
	}
	return Text;
}
