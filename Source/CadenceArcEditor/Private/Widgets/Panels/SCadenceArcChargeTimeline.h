#pragma once

#include "CoreMinimal.h"
#include "Resolver/CadenceArcResolverTypes.h"
#include "Widgets/SLeafWidget.h"

/** 只绘制 Resolver 已观察到的按住时间，不自行推进时间。 */
class SCadenceArcChargeTimeline : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcChargeTimeline) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs) {}
	void SetSnapshot(const FCadenceArcHoldSnapshot& InSnapshot);

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual int32 OnPaint(
		const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	FCadenceArcHoldSnapshot Snapshot;
};
