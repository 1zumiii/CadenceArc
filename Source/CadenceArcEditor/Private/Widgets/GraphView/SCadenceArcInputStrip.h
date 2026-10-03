#pragma once

#include "CoreMinimal.h"
#include "ViewModel/CadenceArcInputDisplay.h"
#include "Widgets/SLeafWidget.h"

/**
 * 画布左下角的输入显示：第一行是持久上下文和停顿，按住时多一行按住时长，
 * 最下面是最近几次输入，读作"[Forward] + [Light]"：事件自带的上下文是键帽前面的彩色标签，
 * 键帽下面写按住时长和 buffered / ignored，旧的输入变暗但不消失。
 * 只显示传进来的数据，不读 Resolver，也不接收鼠标。
 */
class SCadenceArcInputStrip : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcInputStrip)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
	}

	void SetDisplay(const FCadenceArcInputDisplay& InDisplay);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(
		const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	FCadenceArcInputDisplay Display;

	// 每个元素的位置在 SetDisplay 里算好，ComputeDesiredSize 和 OnPaint 共用
	struct FTextItem
	{
		FVector2D Position;
		FString Text;
		bool bBold = false;
		float Size = 8.f;
		FLinearColor Color;
	};

	struct FKeyItem
	{
		FBox2D Box;
		FLinearColor Outline;
		float Alpha = 1.f;
		bool bContext = false; // 上下文标签：扁平的彩色小框，不画键帽下沿
	};

	TArray<FTextItem> Texts;
	TArray<FKeyItem> Keys;
	TArray<FKeyItem> Dividers; // 组与组之间的竖线：Box 的 Min、Max 是线的两端
	FVector2D ContentSize = FVector2D::ZeroVector;

	void RebuildItems();
};
