#include "SCadenceArcChargeTimeline.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr float SidePadding = 8.f;
	constexpr float BarTop = 10.f;
	constexpr float BarHeight = 10.f;
	constexpr float LabelTop = 34.f;
	constexpr float LabelHeight = 17.f;

	float PositionForTime(const double Time, const double Pressed, const double Cutoff)
	{
		const double Duration = Cutoff - Pressed;
		if (Duration <= 0.0)
		{
			return 0.f;
		}
		return static_cast<float>(FMath::Clamp((Time - Pressed) / Duration, 0.0, 1.0));
	}
}

void SCadenceArcChargeTimeline::SetSnapshot(const FCadenceArcHoldSnapshot& InSnapshot)
{
	Snapshot = InSnapshot;
	Invalidate(EInvalidateWidgetReason::Paint);
}

FVector2D SCadenceArcChargeTimeline::ComputeDesiredSize(float) const
{
	return FVector2D(320.f, LabelTop + 5.f * LabelHeight);
}

int32 SCadenceArcChargeTimeline::OnPaint(
	const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	if (!Snapshot.bHasHold || !Snapshot.bHasChargeConfig)
	{
		return LayerId;
	}

	const float BarWidth = FMath::Max(0.f, static_cast<float>(AllottedGeometry.GetLocalSize().X) - 2.f * SidePadding);
	const double Pressed = Snapshot.PressedTimestampSeconds;
	const double ChargeStart = Pressed + Snapshot.ChargeStartSeconds;
	const double Full = Snapshot.ChargeFullTimestampSeconds;
	const double Cutoff = Snapshot.AutoReleaseTimestampSeconds;
	const float ObservedFraction = Cutoff <= Pressed
		? 1.f
		: PositionForTime(Snapshot.LastObservedTimestampSeconds, Pressed, Cutoff);
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);

	FSlateDrawElement::MakeBox(
		OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth, BarHeight),
			FSlateLayoutTransform(FVector2D(SidePadding, BarTop))),
		WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.20f, 0.20f, 0.24f));
	if (ObservedFraction > 0.f && BarWidth > 0.f)
	{
		FSlateDrawElement::MakeBox(
			OutDrawElements, LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth * ObservedFraction, BarHeight),
				FSlateLayoutTransform(FVector2D(SidePadding, BarTop))),
			WhiteBrush, ESlateDrawEffect::None, FLinearColor(0.25f, 0.65f, 0.35f));
	}

	const FPaintGeometry CanvasGeometry = AllottedGeometry.ToPaintGeometry();
	const double Times[4] = {Pressed, ChargeStart, Full, Cutoff};
	const TCHAR* Labels[4] = {TEXT("Press"), TEXT("Charge start"), TEXT("Full"), TEXT("Cutoff")};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float X = SidePadding + BarWidth * PositionForTime(Times[Index], Pressed, Cutoff);
		FSlateDrawElement::MakeLines(
			OutDrawElements, LayerId + 2, CanvasGeometry,
			TArray<FVector2D>{FVector2D(X, BarTop - 3.f), FVector2D(X, BarTop + BarHeight + 3.f)},
			ESlateDrawEffect::None, FLinearColor::White, true, 1.f);

		const FString Label = FString::Printf(TEXT("%s: %.3f s"), Labels[Index], Times[Index]);
		FSlateDrawElement::MakeText(
			OutDrawElements, LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth, LabelHeight),
				FSlateLayoutTransform(FVector2D(SidePadding, LabelTop + Index * LabelHeight))),
			Label, Font, ESlateDrawEffect::None, FLinearColor::White);
	}

	const FString ObservedLabel = FString::Printf(
		TEXT("Last observed: %.3f s"), Snapshot.LastObservedTimestampSeconds);
	FSlateDrawElement::MakeText(
		OutDrawElements, LayerId + 2,
		AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth, LabelHeight),
			FSlateLayoutTransform(FVector2D(SidePadding, LabelTop + 4.f * LabelHeight))),
		ObservedLabel, Font, ESlateDrawEffect::None, FLinearColor(0.65f, 0.9f, 0.65f));
	return LayerId + 2;
}
