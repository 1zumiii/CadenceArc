#include "SCadenceArcInputStrip.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "CadenceArcCanvasDrawing.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr double StripPadding = 8.0;
	constexpr double StripKeyHeight = 22.0;
	constexpr double StripKeyLip = 3.0; // 键帽下沿的深色厚度，看起来像一颗按键
	constexpr double StripKeyTextInset = 9.0;
	constexpr double StripChipGap = 6.0;

	const FLinearColor StripBackground(0.035f, 0.035f, 0.045f, 0.88f);
	const FLinearColor StripKeyFill(0.21f, 0.21f, 0.25f);
	const FLinearColor StripKeyLipColor(0.09f, 0.09f, 0.11f);
	const FLinearColor StripKeyOutline(0.55f, 0.55f, 0.62f);
	const FLinearColor StripHeldOutline(1.0f, 0.62f, 0.12f); // 与画布上蓄力进度同色
	const FLinearColor StripBufferedOutline(0.40f, 0.70f, 1.0f);
	const FLinearColor StripTitleColor(0.55f, 0.55f, 0.60f);
	const FLinearColor StripStatusColor(0.80f, 0.80f, 0.84f);
	const FLinearColor StripDetailColor(0.68f, 0.68f, 0.72f);
	const FLinearColor StripContextColor(0.35f, 0.88f, 0.62f); // 事件上下文标签
	const FLinearColor StripContextFill(0.06f, 0.20f, 0.14f);
	constexpr double StripPillHeight = 18.0;

	FSlateFontInfo StripFont(const bool bBold, const float Size)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", static_cast<int32>(Size));
	}

	const TCHAR* StripStageName(const ECadenceArcHoldStage Stage)
	{
		switch (Stage)
		{
		case ECadenceArcHoldStage::Charging: return TEXT("charging");
		case ECadenceArcHoldStage::Charged: return TEXT("fully charged");
		default: return TEXT("holding");
		}
	}

	FLinearColor StripWithAlpha(FLinearColor Color, const float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}
}

void SCadenceArcInputStrip::SetDisplay(const FCadenceArcInputDisplay& InDisplay)
{
	Display = InDisplay;
	RebuildItems();
	Invalidate(EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Paint);
}

void SCadenceArcInputStrip::RebuildItems()
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	Texts.Reset();
	Keys.Reset();
	Dividers.Reset();
	ContentSize = FVector2D::ZeroVector;
	if (!Display.bValid)
	{
		return;
	}
	double Right = 0.0;
	const auto AddText = [this, &Right](const FVector2D& Position, const FString& Text, const bool bBold, const float Size,
	                                    const FLinearColor& Color)
	{
		Texts.Add({Position, Text, bBold, Size, Color});
		Right = FMath::Max(Right, Position.X + MeasureText(Text, StripFont(bBold, Size)).X);
	};
	const auto KeyWidth = [](const FString& Label)
	{
		return MeasureText(Label, StripFont(true, 9.f)).X + StripKeyTextInset * 2.0;
	};

	// 第一行：持久上下文和停顿
	double Y = StripPadding;
	AddText(FVector2D(StripPadding, Y), TEXT("INPUT"), true, 8.f, StripTitleColor);
	FString Status = FString::Printf(TEXT("Context: %s"), *Display.PersistentContext);
	if (!Display.PauseText.IsEmpty())
	{
		Status += TEXT("   ·   ") + Display.PauseText;
	}
	AddText(FVector2D(StripPadding + MeasureText(TEXT("INPUT"), StripFont(true, 8.f)).X + 10.0, Y), Status, false, 8.f,
	        StripStatusColor);
	Y += 18.0;

	// 按住中：一颗橙色描边的键帽和按住时长
	if (Display.bHolding)
	{
		const double Width = KeyWidth(Display.HeldInput);
		Keys.Add({FBox2D(FVector2D(StripPadding, Y), FVector2D(StripPadding + Width, Y + StripKeyHeight)), StripHeldOutline, 1.f});
		AddText(FVector2D(StripPadding + StripKeyTextInset, Y + 3.0), Display.HeldInput, true, 9.f, FLinearColor::White);
		AddText(FVector2D(StripPadding + Width + 8.0, Y + 4.0),
		        FString::Printf(TEXT("%s %.2fs"), StripStageName(Display.HeldStage), Display.HeldSeconds), false, 8.f,
		        StripHeldOutline);
		Y += StripKeyHeight + StripKeyLip + 6.0;
	}

	// 最近的输入：新的在左
	if (Display.Recent.IsEmpty())
	{
		AddText(FVector2D(StripPadding, Y), TEXT("no recent input"), false, 8.f, StripTitleColor);
		Y += 16.0;
	}
	else
	{
		double X = StripPadding;
		for (const FCadenceArcInputChip& Chip : Display.Recent)
		{
			const float Alpha = FCadenceArcInputDisplay::GetChipAlpha(Chip.AgeSeconds);
			const FString Label = Chip.Input + (Chip.bReleased ? TEXT(" ↑") : TEXT(""));
			TArray<FString> DetailParts;
			if (!Chip.Detail.IsEmpty())
			{
				DetailParts.Add(Chip.Detail);
			}
			if (Chip.bIgnored)
			{
				DetailParts.Add(TEXT("ignored"));
			}
			else if (Chip.bBuffered)
			{
				DetailParts.Add(TEXT("buffered"));
			}
			const FString Detail = FString::Join(DetailParts, TEXT(" · "));
			if (X > StripPadding)
			{
				// 每组输入之间一条竖分隔线，透明度跟右边较新的那组
				Dividers.Add({FBox2D(FVector2D(X, Y - 2.0), FVector2D(X, Y + StripKeyHeight + StripKeyLip + 14.0)),
				              StripTitleColor, Alpha});
				X += StripChipGap + 4.0;
			}
			const double ColumnStart = X;

			// 事件自带的上下文画在键帽前面："[Forward] + [Light]"
			for (const FString& Context : Chip.Context)
			{
				const double PillWidth = MeasureText(Context, StripFont(true, 8.f)).X + 12.0;
				const double PillTop = Y + (StripKeyHeight - StripPillHeight) * 0.5;
				Keys.Add({FBox2D(FVector2D(X, PillTop), FVector2D(X + PillWidth, PillTop + StripPillHeight)),
				          StripContextColor, Alpha, true});
				AddText(FVector2D(X + 6.0, PillTop + 2.0), Context, true, 8.f, StripWithAlpha(StripContextColor, Alpha));
				X += PillWidth + 3.0;
				AddText(FVector2D(X, Y + 3.0), TEXT("+"), true, 9.f, StripWithAlpha(StripDetailColor, Alpha));
				X += MeasureText(TEXT("+"), StripFont(true, 9.f)).X + 3.0;
			}

			const double Width = KeyWidth(Label);
			const FLinearColor Outline = Chip.bIgnored ? BrokenColor : Chip.bBuffered ? StripBufferedOutline : StripKeyOutline;
			Keys.Add({FBox2D(FVector2D(X, Y), FVector2D(X + Width, Y + StripKeyHeight)), Outline, Alpha});
			AddText(FVector2D(X + StripKeyTextInset, Y + 3.0), Label, true, 9.f, StripWithAlpha(FLinearColor::White, Alpha));
			double ColumnEnd = X + Width;
			if (!Detail.IsEmpty())
			{
				AddText(FVector2D(X, Y + StripKeyHeight + StripKeyLip + 2.0), Detail, false, 8.f,
				        StripWithAlpha(Chip.bIgnored ? BrokenColor : StripDetailColor, Alpha));
				ColumnEnd = FMath::Max(ColumnEnd, X + MeasureText(Detail, StripFont(false, 8.f)).X);
			}
			X = FMath::Max(ColumnEnd, ColumnStart) + StripChipGap + 4.0; // 分隔线落在两组正中
		}
		Right = FMath::Max(Right, X - StripChipGap - 4.0);
		Y += StripKeyHeight + StripKeyLip + 16.0;
	}
	ContentSize = FVector2D(Right + StripPadding, Y + StripPadding - 4.0);
}

FVector2D SCadenceArcInputStrip::ComputeDesiredSize(float) const
{
	return ContentSize;
}

int32 SCadenceArcInputStrip::OnPaint(
	const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace CadenceArc::Editor::CanvasDrawing;
	if (!Display.bValid)
	{
		return LayerId;
	}
	static const FSlateRoundedBoxBrush BackgroundBrush(FLinearColor::White, 6.f);
	static const FSlateRoundedBoxBrush KeyBrush(FLinearColor::White, 4.f);
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(ContentSize, FSlateLayoutTransform()),
	                           &BackgroundBrush, ESlateDrawEffect::None, StripBackground);

	const FPaintGeometry Canvas = AllottedGeometry.ToPaintGeometry();
	for (const FKeyItem& Key : Keys)
	{
		const FVector2D Size = Key.Box.GetSize();
		if (!Key.bContext)
		{
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
			                           AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Key.Box.Min + FVector2D(0.0, StripKeyLip))),
			                           &KeyBrush, ESlateDrawEffect::None, StripWithAlpha(StripKeyLipColor, Key.Alpha));
		}
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2,
		                           AllottedGeometry.ToPaintGeometry(Size, FSlateLayoutTransform(Key.Box.Min)),
		                           &KeyBrush, ESlateDrawEffect::None,
		                           StripWithAlpha(Key.bContext ? StripContextFill : StripKeyFill, Key.Alpha));
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 3, Canvas, RoundedOutline(Key.Box, 4.0),
		                             ESlateDrawEffect::None, StripWithAlpha(Key.Outline, Key.Alpha), true, 1.f);
	}
	for (const FKeyItem& Divider : Dividers)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Canvas,
		                             TArray<FVector2f>{ToFloatPoint(Divider.Box.Min), ToFloatPoint(Divider.Box.Max)},
		                             ESlateDrawEffect::None, StripWithAlpha(Divider.Outline, Divider.Alpha * 0.7f), true, 1.f);
	}
	for (const FTextItem& Item : Texts)
	{
		const FSlateFontInfo Font = StripFont(Item.bBold, Item.Size);
		DrawLabel(OutDrawElements, LayerId + 4, AllottedGeometry, Item.Position, MeasureText(Item.Text, Font), Item.Text,
		          Font, Item.Color);
	}
	return LayerId + 4;
}
