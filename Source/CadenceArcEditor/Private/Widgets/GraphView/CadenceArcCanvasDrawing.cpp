#include "CadenceArcCanvasDrawing.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "ViewModel/CadenceArcConditionText.h"

namespace CadenceArc::Editor::CanvasDrawing
{
	namespace
	{
		// 按累计路径长度截取，不直接按 Hermite 参数截取，视觉进度才会匀速沿曲线前进。
		TArray<FVector2f> TakePathPrefix(const TArray<FVector2f>& Path, const float Fraction)
		{
			if (Path.Num() < 2 || Fraction <= 0.f)
			{
				return {};
			}
			if (Fraction >= 1.f)
			{
				return Path;
			}
			float TotalLength = 0.f;
			for (int32 Index = 1; Index < Path.Num(); ++Index)
			{
				TotalLength += (Path[Index] - Path[Index - 1]).Size();
			}
			const float TargetLength = TotalLength * Fraction;
			float AccumulatedLength = 0.f;
			TArray<FVector2f> Prefix;
			Prefix.Reserve(Path.Num());
			Prefix.Add(Path[0]);
			for (int32 Index = 1; Index < Path.Num(); ++Index)
			{
				const float SegmentLength = (Path[Index] - Path[Index - 1]).Size();
				if (AccumulatedLength + SegmentLength >= TargetLength && SegmentLength > 0.f)
				{
					const float SegmentFraction = (TargetLength - AccumulatedLength) / SegmentLength;
					Prefix.Add(FMath::Lerp(Path[Index - 1], Path[Index], SegmentFraction));
					break;
				}
				Prefix.Add(Path[Index]);
				AccumulatedLength += SegmentLength;
			}
			return Prefix;
		}
	}

	FString ShortTagName(const FGameplayTag& Tag)
	{
		const FString Name = Tag.ToString();
		int32 Dot = INDEX_NONE;
		return Name.FindLastChar(TEXT('.'), Dot) ? Name.RightChop(Dot + 1) : Name;
	}

	FLinearColor ColorForEdge(const int32 EdgeIndex)
	{
		const float Hue = FMath::Frac(EdgeIndex * 0.618034f);
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hue * 255.f), 170, 255);
	}

	FString FormatTransitionLabel(const FCadenceArcTransition& Transition)
	{
		const FString Input = ShortTagName(Transition.InputTag);
		const TCHAR* Phase = Transition.InputPhase == ECadenceArcInputPhase::Pressed ? TEXT("P") : TEXT("R");
		FString Label = FString::Printf(TEXT("%s %s"), *Input, Phase);
		if (Transition.InputPhase == ECadenceArcInputPhase::Released && Transition.bUseDurationRange)
		{
			const FCadenceArcHeldDurationRange& Range = Transition.DurationRange;
			// 上限不包含在区间内，所以右边是圆括号
			const FString Upper = Range.bHasMaxHeldDuration
				? FString::SanitizeFloat(Range.MaxHeldDurationSecondsExclusive)
				: FString(TEXT("∞"));
			Label += FString::Printf(TEXT(" [%s, %s)"), *FString::SanitizeFloat(Range.MinHeldDurationSeconds), *Upper);
		}
		// 转移条件和优先级（Phase 8）："Heavy P +Forward pause≥0.3s #2"
		const FString Conditions = CadenceArc::Editor::ConditionText::FormatConditions(Transition);
		if (!Conditions.IsEmpty())
		{
			Label += TEXT(" ") + Conditions;
		}
		return Label;
	}

	FVector2f ToFloatPoint(const FVector2D& Point)
	{
		return FVector2f(static_cast<float>(Point.X), static_cast<float>(Point.Y));
	}

	TArray<FVector2f> ToFloatPath(const TArray<FVector2D>& Path)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Path.Num());
		for (const FVector2D& Point : Path)
		{
			Points.Add(ToFloatPoint(Point));
		}
		return Points;
	}

	TArray<FVector2f> BoxOutline(const FBox2D& Box)
	{
		return {
			ToFloatPoint(Box.Min), ToFloatPoint(FVector2D(Box.Max.X, Box.Min.Y)),
			ToFloatPoint(Box.Max), ToFloatPoint(FVector2D(Box.Min.X, Box.Max.Y)), ToFloatPoint(Box.Min)
		};
	}

	TArray<FVector2f> RoundedOutline(const FBox2D& Box, const double Radius)
	{
		constexpr int32 Steps = 4;
		const FVector2D Centers[4] = {
			FVector2D(Box.Max.X - Radius, Box.Min.Y + Radius), FVector2D(Box.Max.X - Radius, Box.Max.Y - Radius),
			FVector2D(Box.Min.X + Radius, Box.Max.Y - Radius), FVector2D(Box.Min.X + Radius, Box.Min.Y + Radius)
		};
		TArray<FVector2f> Points;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double StartAngle = (Corner - 1) * UE_DOUBLE_HALF_PI; // 右上角从 -90° 开始，顺时针（屏幕坐标 y 向下）
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const double Angle = StartAngle + UE_DOUBLE_HALF_PI * Step / Steps;
				Points.Add(ToFloatPoint(Centers[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius));
			}
		}
		const FVector2f First = Points[0]; // 先复制：Add 可能扩容，直接传 Points[0] 的引用会指向已释放的内存
		Points.Add(First);
		return Points;
	}

	void DrawLabel(
		FSlateWindowElementList& OutDrawElements, const int32 Layer, const FGeometry& Geometry, const FVector2D& TopLeft,
		const FVector2D& Size, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, Layer,
		                            Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(TopLeft)),
		                            Text, Font, ESlateDrawEffect::None, Color);
	}

	FVector2D MeasureText(const FString& Text, const FSlateFontInfo& Font)
	{
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text, Font);
	}

	FString FitText(const FString& Text, const FSlateFontInfo& Font, const double MaxWidth)
	{
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		if (Measure->Measure(Text, Font).X <= MaxWidth)
		{
			return Text;
		}
		for (int32 Length = Text.Len() - 1; Length > 0; --Length)
		{
			const FString Candidate = Text.Left(Length) + TEXT("…");
			if (Measure->Measure(Candidate, Font).X <= MaxWidth)
			{
				return Candidate;
			}
		}
		return TEXT("…");
	}

	void DrawPreparatoryPath(
		FSlateWindowElementList& OutDrawElements, const int32 Layer, const FPaintGeometry& CanvasGeometry,
		TArray<FVector2f> Path, const float Progress, const bool bCurrentRelease,
		const FLinearColor& DashedColor, const FLinearColor& ProgressColor)
	{
		TArray<FVector2f> FilledPath = TakePathPrefix(Path, Progress);
		if (FilledPath.Num() >= 2)
		{
			// 已走过的长度先画成较宽的底色，再叠虚线，填满后仍能看出这是预备边。
			FSlateDrawElement::MakeLines(
				OutDrawElements, Layer, CanvasGeometry, MoveTemp(FilledPath),
				ESlateDrawEffect::None, ProgressColor, true, bCurrentRelease ? 5.f : 3.5f);
		}
		FSlateDrawElement::MakeDashedLines(
			OutDrawElements, Layer, CanvasGeometry, MoveTemp(Path),
			ESlateDrawEffect::None, DashedColor, bCurrentRelease ? 3.f : 2.f, 8.f);
	}

	void DrawArrowHead(
		FSlateWindowElementList& OutDrawElements, const int32 Layer, const FPaintGeometry& CanvasGeometry,
		const TArray<FVector2D>& Path, const FLinearColor& Color, const float Thickness)
	{
		if (Path.Num() < 2)
		{
			return;
		}
		const FVector2D Tip = Path.Last();
		const FVector2D Direction = (Tip - Path[Path.Num() - 2]).GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return;
		}
		const FVector2D Normal(-Direction.Y, Direction.X);
		constexpr double Length = 7.0;
		constexpr double HalfWidth = 4.0;
		FSlateDrawElement::MakeLines(
			OutDrawElements, Layer, CanvasGeometry,
			TArray<FVector2f>{
				ToFloatPoint(Tip - Direction * Length + Normal * HalfWidth), ToFloatPoint(Tip),
				ToFloatPoint(Tip - Direction * Length - Normal * HalfWidth)
			},
			ESlateDrawEffect::None, Color, true, Thickness);
	}

	void DrawReferenceIcon(
		FSlateWindowElementList& OutDrawElements, const int32 Layer, const FPaintGeometry& CanvasGeometry,
		const FVector2D& Center, const bool bJumpsBack, const FLinearColor& Color)
	{
		if (!bJumpsBack)
		{
			FSlateDrawElement::MakeLines(
				OutDrawElements, Layer, CanvasGeometry,
				TArray<FVector2f>{ToFloatPoint(Center + FVector2D(-5.0, 0.0)), ToFloatPoint(Center + FVector2D(4.0, 0.0))},
				ESlateDrawEffect::None, Color, true, 1.5f);
			FSlateDrawElement::MakeLines(
				OutDrawElements, Layer, CanvasGeometry,
				TArray<FVector2f>{
					ToFloatPoint(Center + FVector2D(0.0, -4.0)), ToFloatPoint(Center + FVector2D(4.5, 0.0)),
					ToFloatPoint(Center + FVector2D(0.0, 4.0))
				},
				ESlateDrawEffect::None, Color, true, 1.5f);
			return;
		}
		FSlateDrawElement::MakeLines(
			OutDrawElements, Layer, CanvasGeometry,
			TArray<FVector2f>{
				ToFloatPoint(Center + FVector2D(4.0, -4.5)), ToFloatPoint(Center + FVector2D(4.0, 1.0)),
				ToFloatPoint(Center + FVector2D(2.5, 2.5)), ToFloatPoint(Center + FVector2D(-4.5, 2.5))
			},
			ESlateDrawEffect::None, Color, true, 1.5f);
		FSlateDrawElement::MakeLines(
			OutDrawElements, Layer, CanvasGeometry,
			TArray<FVector2f>{
				ToFloatPoint(Center + FVector2D(-1.5, -0.5)), ToFloatPoint(Center + FVector2D(-4.5, 2.5)),
				ToFloatPoint(Center + FVector2D(-1.5, 5.5))
			},
			ESlateDrawEffect::None, Color, true, 1.5f);
	}
}
