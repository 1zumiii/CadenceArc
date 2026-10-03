#pragma once

// 画布绘制用的配色和小工具：文字、箭头、虚线进度、圆角轮廓、引用标签图标等。
// 只依赖 Slate 的绘制接口，不认识布局和调试状态；SCadenceArcGraphCanvas 的绘制代码组合它们。

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"

class FSlateWindowElementList;
struct FGeometry;
struct FPaintGeometry;
struct FSlateFontInfo;

namespace CadenceArc::Editor::CanvasDrawing
{
	// 分支聚焦的三档透明度：下一步（及候选、预备边）不透明；之后还能走到的一档；这条路上已经走不到的最淡
	inline constexpr float OnPathEdgeAlpha = 0.55f;
	inline constexpr float OffPathEdgeAlpha = 0.12f;
	inline constexpr float OnPathLabelAlpha = 0.8f;
	inline constexpr float OffPathLabelAlpha = 0.3f;
	inline constexpr float OffPathNodeAlpha = 0.35f;
	inline const FLinearColor NextStepHeaderColor(0.36f, 0.38f, 0.50f); // 下一步节点的标题行稍亮
	inline const FLinearColor HistoryFocusColor(0.72f, 0.45f, 1.0f); // Arc History 点中的记录对应的边和节点

	inline const FLinearColor BodyColor(0.16f, 0.16f, 0.20f);
	inline const FLinearColor HeaderColor(0.26f, 0.26f, 0.32f);
	inline const FLinearColor UnreachableBodyColor(0.07f, 0.07f, 0.07f);
	inline const FLinearColor UnreachableHeaderColor(0.11f, 0.11f, 0.11f);
	inline const FLinearColor BrokenColor(1.f, 0.25f, 0.25f);
	inline const FLinearColor ChainGroupColor(0.38f, 0.38f, 0.42f, 0.45f);
	inline constexpr float ReferenceTagRadius = 4.f; // 引用标签的圆角

	// CadenceArc.Test.Action.Light01 -> Light01
	FString ShortTagName(const FGameplayTag& Tag);

	// 边的颜色只由它在整张图的边数组里的序号决定：同一张图每帧颜色都一样。
	// 按黄金分割比例在色相环上取点，相邻序号的颜色差得最开。
	FLinearColor ColorForEdge(int32 EdgeIndex);

	// "Heavy R [0, 0.8)" / "Heavy R [0.8, ∞)" / "Light P"
	FString FormatTransitionLabel(const FCadenceArcTransition& Transition);

	FVector2f ToFloatPoint(const FVector2D& Point);
	TArray<FVector2f> ToFloatPath(const TArray<FVector2D>& Path);
	TArray<FVector2f> BoxOutline(const FBox2D& Box);
	// 圆角矩形的闭合折线。圆角画刷自带的描边颜色不受 MakeBox 的 Tint 影响，
	// 没法按边的颜色和透明度着色，所以描边用线画。
	TArray<FVector2f> RoundedOutline(const FBox2D& Box, double Radius);

	// 文字需要一个非零的绘制区域，否则会被 Slate 剔除
	void DrawLabel(
		FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const FVector2D& TopLeft,
		const FVector2D& Size, const FString& Text, const FSlateFontInfo& Font, const FLinearColor& Color);

	// 按字体实际宽度截断，放不下时末尾加省略号
	FString FitText(const FString& Text, const FSlateFontInfo& Font, double MaxWidth);
	FVector2D MeasureText(const FString& Text, const FSlateFontInfo& Font);

	// 预备边：已走过的长度画成较宽的底色，再叠虚线；进度按路径长度截取，沿曲线匀速前进
	void DrawPreparatoryPath(
		FSlateWindowElementList& OutDrawElements, int32 Layer, const FPaintGeometry& CanvasGeometry,
		TArray<FVector2f> Path, float Progress, bool bCurrentRelease,
		const FLinearColor& DashedColor, const FLinearColor& ProgressColor);

	// 终点箭头：沿路径最后一段的方向，落在目标左侧的空隙里
	void DrawArrowHead(
		FSlateWindowElementList& OutDrawElements, int32 Layer, const FPaintGeometry& CanvasGeometry,
		const TArray<FVector2D>& Path, const FLinearColor& Color, float Thickness);

	// 引用标签右端的方向图标，用线画，不依赖字体里有没有箭头字形：
	// 往前（目标在右侧的列）是右箭头；往回跳是一个"掉头"箭头：先向下再向左，箭头朝左。
	void DrawReferenceIcon(
		FSlateWindowElementList& OutDrawElements, int32 Layer, const FPaintGeometry& CanvasGeometry,
		const FVector2D& Center, bool bJumpsBack, const FLinearColor& Color);
}
