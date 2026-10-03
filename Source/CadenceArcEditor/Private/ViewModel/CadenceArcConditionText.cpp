#include "CadenceArcConditionText.h"

namespace CadenceArc::Editor::ConditionText
{
	FString ShortTag(const FGameplayTag& Tag)
	{
		FString Name = Tag.ToString();
		int32 Dot = INDEX_NONE;
		if (Name.FindLastChar(TEXT('.'), Dot))
		{
			Name.RightChopInline(Dot + 1);
		}
		return Name;
	}

	FString FormatContext(const FGameplayTagContainer& Context)
	{
		if (Context.IsEmpty())
		{
			return TEXT("none");
		}
		TArray<FString> Names;
		for (const FGameplayTag& Tag : Context)
		{
			Names.Add(ShortTag(Tag));
		}
		return FString::Join(Names, TEXT(", "));
	}

	namespace
	{
		// 区间本身："≥0.25s" / "<0.25s" / "0.1–0.3s"
		FString FormatPauseBounds(const FCadenceArcHeldDurationRange& Range)
		{
			const FString Min = FString::SanitizeFloat(Range.MinHeldDurationSeconds);
			if (!Range.bHasMaxHeldDuration)
			{
				return FString::Printf(TEXT("≥%ss"), *Min);
			}
			const FString Max = FString::SanitizeFloat(Range.MaxHeldDurationSecondsExclusive);
			return Range.MinHeldDurationSeconds <= 0.0
				? FString::Printf(TEXT("<%ss"), *Max)
				: FString::Printf(TEXT("%s–%ss"), *Min, *Max);
		}
	}

	FString FormatPauseRange(const FCadenceArcHeldDurationRange& Range)
	{
		const FString Bounds = FormatPauseBounds(Range);
		// 纯数字区间和前缀之间留空格，符号开头的直接接上
		return FChar::IsDigit(Bounds[0]) ? TEXT("pause ") + Bounds : TEXT("pause") + Bounds;
	}

	FString FormatConditions(const FCadenceArcTransition& Transition)
	{
		TArray<FString> Parts;
		for (const FGameplayTag& Tag : Transition.RequiredContextTags)
		{
			Parts.Add(TEXT("+") + ShortTag(Tag));
		}
		for (const FGameplayTag& Tag : Transition.BlockedContextTags)
		{
			Parts.Add(TEXT("-") + ShortTag(Tag));
		}
		if (Transition.bUsePauseRange)
		{
			Parts.Add(FormatPauseRange(Transition.PauseRange));
		}
		if (Transition.Priority != 0)
		{
			Parts.Add(FString::Printf(TEXT("#%d"), Transition.Priority));
		}
		return FString::Join(Parts, TEXT(" "));
	}

	TArray<FString> UnmetConditions(
		const FCadenceArcTransition& Transition, const FGameplayTagContainer& Context,
		const bool bHasPause, const double PauseSeconds)
	{
		// 与 GraphQuery::FindUniqueTransition 的判断一致：Required 用 HasAll，Blocked 用 HasAny，都按层级
		TArray<FString> Unmet;
		for (const FGameplayTag& Tag : Transition.RequiredContextTags)
		{
			if (!Context.HasTag(Tag))
			{
				Unmet.Add(FString::Printf(TEXT("needs %s"), *ShortTag(Tag)));
			}
		}
		for (const FGameplayTag& Tag : Transition.BlockedContextTags)
		{
			if (Context.HasTag(Tag))
			{
				Unmet.Add(FString::Printf(TEXT("blocked by %s"), *ShortTag(Tag)));
			}
		}
		if (Transition.bUsePauseRange)
		{
			if (!bHasPause)
			{
				Unmet.Add(TEXT("no pause yet (nothing has finished)"));
			}
			else if (!Transition.PauseRange.Contains(PauseSeconds))
			{
				Unmet.Add(FString::Printf(TEXT("pause %.2fs, needs %s"), PauseSeconds,
				                          *FormatPauseBounds(Transition.PauseRange)));
			}
		}
		return Unmet;
	}
}
