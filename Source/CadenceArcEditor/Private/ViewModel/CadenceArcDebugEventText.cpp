#include "CadenceArcDebugEventText.h"

#include "Graph/CadenceArcGraph.h"
#include "ViewModel/CadenceArcConditionText.h"

namespace
{
	namespace ConditionText = CadenceArc::Editor::ConditionText;

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

	// 记录里的输入还原成事件，用来和边做 Tag、阶段、按住时长的匹配
	FCadenceArcInputEvent RecordedInput(const FCadenceArcDebugEvent& Event)
	{
		FCadenceArcInputEvent Input;
		Input.InputTag = Event.InputTag;
		Input.InputPhase = Event.InputPhase;
		Input.TimestampSeconds = Event.TimestampSeconds;
		Input.HeldDurationSeconds = Event.bHasHeldDuration ? Event.HeldDurationSeconds : 0.0;
		return Input;
	}

	// 当时所在节点上，Tag、阶段和按住时长都对得上这次输入的边（条件和停顿之前的候选）。
	// 读的是当前资产：PIE 中途改了图时，说明可能和当时不完全一致。
	TArray<const FCadenceArcTransition*> CandidateEdges(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph)
	{
		TArray<const FCadenceArcTransition*> Candidates;
		const FCadenceArcNode* Node = Graph ? Graph->FindAction(Event.CommittedBefore) : nullptr;
		if (!Node)
		{
			return Candidates;
		}
		const FCadenceArcInputEvent Input = RecordedInput(Event);
		for (const FCadenceArcTransition& Edge : Node->Transitions)
		{
			if (Edge.Matches(Input))
			{
				Candidates.Add(&Edge);
			}
		}
		return Candidates;
	}

	// "Context: Forward, pause 0.42s" / "Context: none, no pause yet"
	FString ContextSummary(const FCadenceArcDebugEvent& Event)
	{
		return FString::Printf(TEXT("Context: %s, %s"), *ConditionText::FormatContext(Event.ContextTags),
		                       Event.bHasPauseDuration
			                       ? *FString::Printf(TEXT("pause %.2fs"), Event.PauseDurationSeconds)
			                       : TEXT("no pause yet"));
	}

	// 条件都不满足：逐条说明候选边差在哪
	FString DescribeConditionNotMet(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph)
	{
		TArray<FString> Lines;
		for (const FCadenceArcTransition* Edge : CandidateEdges(Event, Graph))
		{
			const TArray<FString> Unmet = ConditionText::UnmetConditions(
				*Edge, Event.ContextTags, Event.bHasPauseDuration, Event.PauseDurationSeconds);
			if (!Unmet.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("%s %s"), *ShortName(Edge->TargetActionTag), *FString::Join(Unmet, TEXT(", "))));
			}
		}
		return FString::Printf(TEXT("No edge for %s at %s fits%s. %s"), *InputName(Event), *ShortName(Event.CommittedBefore),
		                       Lines.IsEmpty() ? TEXT(" the conditions") : *(TEXT(": ") + FString::Join(Lines, TEXT("; "))),
		                       *ContextSummary(Event));
	}

	// 最高优先级打平：列出打平的边
	FString DescribeAmbiguous(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph)
	{
		TArray<const FCadenceArcTransition*> Satisfied;
		int32 TopPriority = TNumericLimits<int32>::Lowest();
		for (const FCadenceArcTransition* Edge : CandidateEdges(Event, Graph))
		{
			if (ConditionText::UnmetConditions(*Edge, Event.ContextTags, Event.bHasPauseDuration,
			                                   Event.PauseDurationSeconds).IsEmpty())
			{
				Satisfied.Add(Edge);
				TopPriority = FMath::Max(TopPriority, Edge->Priority);
			}
		}
		TArray<FString> Tied;
		for (const FCadenceArcTransition* Edge : Satisfied)
		{
			if (Edge->Priority == TopPriority)
			{
				Tied.Add(ShortName(Edge->TargetActionTag));
			}
		}
		if (Tied.Num() < 2)
		{
			return FString::Printf(TEXT("Several edges tie at the top priority for %s at %s. %s"), *InputName(Event),
			                       *ShortName(Event.CommittedBefore), *ContextSummary(Event));
		}
		return FString::Printf(TEXT("%s tie at priority %d for %s at %s; raise one priority or make their conditions exclusive. %s"),
		                       *FString::Join(Tied, TEXT(" and ")), TopPriority, *InputName(Event),
		                       *ShortName(Event.CommittedBefore), *ContextSummary(Event));
	}

	// 选边时的上下文，接在输入名后面："Heavy P [Forward, pause 0.42s]"。
	// 停顿只在选中的边看了停顿时显示（需要 Graph），否则每条记录都带一个停顿时长太吵。
	FString ResolutionNote(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph)
	{
		if (!Event.bHasResolutionContext)
		{
			return FString();
		}
		TArray<FString> Parts;
		if (!Event.ContextTags.IsEmpty())
		{
			Parts.Add(ConditionText::FormatContext(Event.ContextTags));
		}
		if (Event.bHasPauseDuration && Event.Category == ECadenceArcResolutionCategory::RequestProduced)
		{
			for (const FCadenceArcTransition* Edge : CandidateEdges(Event, Graph))
			{
				if (Edge->bUsePauseRange && Edge->TargetActionTag == Event.ProducedRequest.TargetActionTag)
				{
					Parts.Add(FString::Printf(TEXT("pause %.2fs"), Event.PauseDurationSeconds));
					break;
				}
			}
		}
		return Parts.IsEmpty() ? FString() : FString::Printf(TEXT(" [%s]"), *FString::Join(Parts, TEXT(", ")));
	}

	// 原因的白话说明，末尾附上枚举名方便程序对照
	FString DescribeReason(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph = nullptr)
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
		case ECadenceArcResolutionReason::ConditionNotMet: Text = DescribeConditionNotMet(Event, Graph); break;
		case ECadenceArcResolutionReason::AmbiguousTransition: Text = DescribeAmbiguous(Event, Graph); break;
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

FCadenceArcDebugEventText FormatDebugEvent(const FCadenceArcDebugEvent& Event, const UCadenceArcGraph* Graph)
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
	const FString Note = ResolutionNote(Event, Graph); // 选边时的上下文，没有选过边时为空
	// 完成时消费缓冲：解析过的缓冲输入有名字；过期等没走到选边的仍叫 "input"
	const FString BufferedName = Event.InputTag.IsValid() ? InputName(Event) : FString(TEXT("input"));

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
			? FString::Printf(TEXT("%s%s ignored at %s"), *InputName(Event), *Note, *Committed)
			: FString::Printf(TEXT("%s%s at %s%s"), *InputName(Event), *Note, *Committed, *ResolutionTail(Event));
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event, Graph);
		}
		break;
	case ECadenceArcDebugOperation::BeginHold:
		Text.Summary = Event.bFailed
			? FString::Printf(TEXT("%s hold refused at %s"), *ShortName(Event.InputTag), *Committed)
			: FString::Printf(TEXT("%s held at %s"), *ShortName(Event.InputTag), *Committed);
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event, Graph);
		}
		break;
	case ECadenceArcDebugOperation::ReleaseHold:
	case ECadenceArcDebugOperation::AdvanceTime:
		{
			const bool bAuto = Event.ReleaseSource == ECadenceArcInputReleaseSource::HoldLimit;
			if (Event.ReleaseSource != ECadenceArcInputReleaseSource::None)
			{
				Text.Summary = FString::Printf(TEXT("%s %s after %s%s%s"), *ShortName(Event.InputTag),
				                               bAuto ? TEXT("auto-released") : TEXT("released"), *HeldText(Event),
				                               *Note, *ResolutionTail(Event));
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
				Text.FailureDetail = DescribeReason(Event, Graph);
			}
			break;
		}
	case ECadenceArcDebugOperation::CancelHold:
		Text.Summary = Event.bFailed ? TEXT("Hold cancel ignored")
		                             : FString::Printf(TEXT("%s hold cancelled"), *ShortName(Event.InputTag));
		if (Event.bFailed)
		{
			Text.FailureDetail = DescribeReason(Event, Graph);
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
			Text.Summary = FString::Printf(TEXT("%s finished, buffered %s%s → %s"), *Committed, *BufferedName, *Note,
			                               *RequestText(Event.ProducedRequest));
		}
		else
		{
			Text.Summary = Event.bFailed
				? FString::Printf(TEXT("%s finished, buffered %s%s dropped"), *Committed, *BufferedName, *Note)
				: FString::Printf(TEXT("%s finished"), *Committed);
		}
		if (Event.bFailed)
		{
			Text.FailureDetail = Event.HandshakeResult != ECadenceArcHandshakeResult::Success
				? DescribeHandshake(Event) : DescribeReason(Event, Graph);
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

FCadenceArcHistoryFocus MakeHistoryFocus(const FCadenceArcDebugEvent& Event)
{
	FCadenceArcHistoryFocus Focus;
	Focus.Sequence = Event.Sequence;
	const auto FocusRequest = [&Focus](const FCadenceArcActionRequest& Request)
	{
		Focus.SourceNode = Request.SourceActionTag;
		Focus.TargetNode = Request.TargetActionTag;
		Focus.InputTag = Request.InputTag;
	};
	if (Event.ProducedRequest.RequestId != 0)
	{
		FocusRequest(Event.ProducedRequest);
		return Focus;
	}
	const bool bRequestCallback = Event.Operation == ECadenceArcDebugOperation::ActionStarted
		|| Event.Operation == ECadenceArcDebugOperation::ActionRejected;
	if (bRequestCallback && !Event.bFailed && Event.RequestBefore.RequestId != 0)
	{
		FocusRequest(Event.RequestBefore);
		return Focus;
	}
	const bool bReturnsSomewhere = Event.Operation == ECadenceArcDebugOperation::Reset
		|| Event.Operation == ECadenceArcDebugOperation::Initialize
		|| Event.Operation == ECadenceArcDebugOperation::ActionCancelled
		|| Event.Operation == ECadenceArcDebugOperation::ActionInterrupted;
	Focus.SourceNode = bReturnsSomewhere && !Event.bFailed ? Event.CommittedAfter : Event.CommittedBefore;
	Focus.InputTag = Event.InputTag;
	return Focus;
}
