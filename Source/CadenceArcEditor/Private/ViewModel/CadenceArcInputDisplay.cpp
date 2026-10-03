#include "CadenceArcInputDisplay.h"

#include "Resolver/CadenceArcResolver.h"
#include "ViewModel/CadenceArcConditionText.h"

namespace
{
	namespace ConditionText = CadenceArc::Editor::ConditionText;

	// 只看历史末尾这么多条：输入显示最多几个，不必复制整个环形缓冲
	constexpr uint64 InputDisplayScanDepth = 64;

	// 哪些记录算"一次输入"。自动松手记在 AdvanceTime 上；其余推进、握手、窗口开关都不是输入
	bool IsInputRecord(const FCadenceArcDebugEvent& Event)
	{
		switch (Event.Operation)
		{
		case ECadenceArcDebugOperation::SubmitInput:
		case ECadenceArcDebugOperation::BeginHold:
		case ECadenceArcDebugOperation::ReleaseHold:
			return true;
		case ECadenceArcDebugOperation::AdvanceTime:
			return Event.ReleaseSource == ECadenceArcInputReleaseSource::HoldLimit;
		default:
			return false;
		}
	}

	FCadenceArcInputChip MakeChip(const FCadenceArcDebugEvent& Event, const double AgeSeconds)
	{
		FCadenceArcInputChip Chip;
		Chip.Input = ConditionText::ShortTag(Event.InputTag);
		Chip.bReleased = Event.InputPhase == ECadenceArcInputPhase::Released
			|| Event.ReleaseSource != ECadenceArcInputReleaseSource::None;
		Chip.AgeSeconds = AgeSeconds;
		Chip.bIgnored = Event.bFailed;
		Chip.bBuffered = Event.Category == ECadenceArcResolutionCategory::Buffered;

		for (const FGameplayTag& Tag : Event.InputContextTags)
		{
			Chip.Context.Add(ConditionText::ShortTag(Tag));
		}
		if (Event.bHasHeldDuration)
		{
			const bool bAuto = Event.ReleaseSource == ECadenceArcInputReleaseSource::HoldLimit;
			Chip.Detail = FString::Printf(TEXT("%s%.2fs"), bAuto ? TEXT("auto ") : TEXT(""), Event.HeldDurationSeconds);
		}
		return Chip;
	}

	FString MakePauseText(const UCadenceArcResolver& Resolver, const double HostTime)
	{
		switch (Resolver.GetState())
		{
		case ECadenceArcResolverState::AwaitingStart:
			return TEXT("waiting for start");
		case ECadenceArcResolverState::Executing:
			// 动作执行中按下的输入进缓冲，完成时按停顿 0 解析
			return TEXT("in action, buffered input counts as pause 0");
		case ECadenceArcResolverState::Ready:
			break;
		default:
			return FString();
		}
		const double Anchor = Resolver.GetDebugPauseAnchorSeconds();
		if (Anchor < 0.0)
		{
			return TEXT("no pause yet");
		}
		return HostTime >= Anchor ? FString::Printf(TEXT("pause %.2fs"), HostTime - Anchor) : FString();
	}
}

float FCadenceArcInputDisplay::GetChipAlpha(const double AgeSeconds)
{
	const double Progress = FMath::Clamp((AgeSeconds - DimAfterSeconds) / DimSeconds, 0.0, 1.0);
	return static_cast<float>(FMath::Lerp(1.0, static_cast<double>(DimAlpha), Progress));
}

FCadenceArcInputDisplay BuildInputDisplay(const UCadenceArcResolver& Resolver)
{
	FCadenceArcInputDisplay Display;
	if (!Resolver.IsInitialized())
	{
		return Display;
	}
	Display.bValid = true;
	Display.PersistentContext = ConditionText::FormatContext(Resolver.GetContextTags());

	const double HostTime = Resolver.GetDebugLastHostTime();
	Display.PauseText = HostTime >= 0.0 ? MakePauseText(Resolver, HostTime) : FString();

	const FCadenceArcHoldSnapshot Hold = Resolver.GetInputHoldSnapshot();
	if (Hold.bHasHold)
	{
		Display.bHolding = true;
		Display.HeldInput = ConditionText::ShortTag(Hold.InputTag);
		Display.HeldSeconds = FMath::Max(0.0, Hold.LastObservedTimestampSeconds - Hold.PressedTimestampSeconds);
		Display.HeldStage = Hold.Stage;
	}

	if (HostTime < 0.0)
	{
		return Display;
	}
	const FCadenceArcDebugHistory& History = Resolver.GetDebugHistory();
	const uint64 Newest = History.GetNewestSequence();
	TArray<FCadenceArcDebugEvent> Events;
	History.CopyEventsAfter(Newest > InputDisplayScanDepth ? Newest - InputDisplayScanDepth : 0, Events);
	for (int32 Index = Events.Num() - 1; Index >= 0 && Display.Recent.Num() < FCadenceArcInputDisplay::MaxRecentChips;
	     --Index)
	{
		const FCadenceArcDebugEvent& Event = Events[Index];
		if (!IsInputRecord(Event) || !Event.bHasTimestamp)
		{
			continue;
		}
		Display.Recent.Add(MakeChip(Event, FMath::Max(0.0, HostTime - Event.TimestampSeconds)));
	}
	return Display;
}
