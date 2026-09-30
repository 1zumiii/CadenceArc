#pragma once

// 调试器视口的纯计算：跟随当前节点时的滚动和缩放、视口外的去向提示。不依赖 Slate，便于单独测试。

#include "CoreMinimal.h"

// 视口跟随的滚动计算（单个轴）：已提交节点为 [PrimaryMin, PrimaryMax]，它和直接后继的外包范围为
// [GroupMin, GroupMax]。整组放得下就以最小移动让整组可见；放不下则保证已提交节点完整可见，
// 并在这个前提下尽量朝整组的中心靠。已提交节点本身比视口还大时对齐它的起点。结果限制在 [0, MaxOffset]。
double ComputeFollowOffset(
	double CurrentOffset, double Visible, double PrimaryMin, double PrimaryMax,
	double GroupMin, double GroupMax, double Margin, double MaxOffset);

// 视口跟随的缩放：当前节点和直接后继组成的整组（布局坐标下的宽高）在 1 倍下放得下时为 1；
// 放不下时缩到刚好放下（四周留 Margin），但不低于 MinZoom，保证文字仍可读。视口尚未排布时返回 CurrentZoom。
double ComputeFollowZoom(
	double CurrentZoom, double VisibleWidth, double VisibleHeight,
	double GroupWidth, double GroupHeight, double Margin, double MinZoom);

// 视口外的去向提示：Target 与 Viewport 完全不相交时，给出提示在视口内的锚点（目标中心夹进视口、
// 各边留 Inset）和指向目标的单位方向；目标有任何部分可见时不提示。两个矩形使用同一坐标系。
struct FCadenceArcOffscreenHint
{
	FVector2D Anchor = FVector2D::ZeroVector;
	FVector2D Direction = FVector2D::ZeroVector;
};

TOptional<FCadenceArcOffscreenHint> ComputeOffscreenHint(const FBox2D& Viewport, const FBox2D& Target, double Inset);
