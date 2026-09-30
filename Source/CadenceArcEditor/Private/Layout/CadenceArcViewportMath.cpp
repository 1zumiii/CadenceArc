#include "CadenceArcViewportMath.h"

double ComputeFollowOffset(
	const double CurrentOffset, const double Visible, const double PrimaryMin, const double PrimaryMax,
	const double GroupMin, const double GroupMax, const double Margin, const double MaxOffset)
{
	if (Visible <= 0.0)
	{
		return CurrentOffset; // 还没完成第一次排布
	}
	double Result;
	if (PrimaryMax - PrimaryMin + 2.0 * Margin > Visible)
	{
		Result = PrimaryMin - Margin; // 节点本身比视口大：至少让它的开头可读
	}
	else if (GroupMax - GroupMin + 2.0 * Margin <= Visible)
	{
		// 整组放得下：已经完整可见就不动，否则移动最少的距离
		Result = FMath::Clamp(CurrentOffset, GroupMax + Margin - Visible, GroupMin - Margin);
	}
	else
	{
		// 放不下：保证已提交节点可见，其余自由度用来尽量朝整组中心靠
		Result = FMath::Clamp((GroupMin + GroupMax - Visible) * 0.5, PrimaryMax + Margin - Visible,
		                      PrimaryMin - Margin);
	}
	return FMath::Clamp(Result, 0.0, FMath::Max(MaxOffset, 0.0));
}

double ComputeFollowZoom(
	const double CurrentZoom, const double VisibleWidth, const double VisibleHeight,
	const double GroupWidth, const double GroupHeight, const double Margin, const double MinZoom)
{
	if (VisibleWidth <= 0.0 || VisibleHeight <= 0.0)
	{
		return CurrentZoom; // 还没完成第一次排布
	}
	double Zoom = 1.0;
	if (GroupWidth > 0.0)
	{
		Zoom = FMath::Min(Zoom, (VisibleWidth - 2.0 * Margin) / GroupWidth);
	}
	if (GroupHeight > 0.0)
	{
		Zoom = FMath::Min(Zoom, (VisibleHeight - 2.0 * Margin) / GroupHeight);
	}
	return FMath::Clamp(Zoom, MinZoom, 1.0);
}

TOptional<FCadenceArcOffscreenHint> ComputeOffscreenHint(const FBox2D& Viewport, const FBox2D& Target, const double Inset)
{
	if (Viewport.Intersect(Target))
	{
		return {};
	}
	const FVector2D Center = Target.GetCenter();
	FCadenceArcOffscreenHint Hint;
	Hint.Anchor = FVector2D(
		FMath::Clamp(Center.X, Viewport.Min.X + Inset, FMath::Max(Viewport.Min.X + Inset, Viewport.Max.X - Inset)),
		FMath::Clamp(Center.Y, Viewport.Min.Y + Inset, FMath::Max(Viewport.Min.Y + Inset, Viewport.Max.Y - Inset)));
	Hint.Direction = (Center - Hint.Anchor).GetSafeNormal();
	return Hint;
}
