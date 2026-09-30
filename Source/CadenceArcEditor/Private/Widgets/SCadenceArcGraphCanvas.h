#pragma once

#include "CoreMinimal.h"
#include "Layout/CadenceArcGraphLayout.h"
#include "ViewModel/CadenceArcDebugView.h"
#include "Widgets/SLeafWidget.h"

class UCadenceArcGraph;

/**
 * 只读图画布：端口式节点，像蓝图节点的引脚一样。
 * 每个节点 = 标题行（节点 Tag）+ 每条出边一行（边的条件），连线从端口行右侧引出，
 * 接到目标节点标题行的左侧。要画的东西全部在 SetGraph 里算好，OnPaint 不读活资产。
 */
class CADENCEARCEDITOR_API SCadenceArcGraphCanvas : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcGraphCanvas)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
	}

	const FCadenceArcGraphLayout& GetLayout() const { return Layout; }
	// 节点在画布局部坐标中的矩形（已乘缩放）；索引无效时返回空，供面板把节点滚进视口
	TOptional<FBox2D> GetNodeBounds(int32 NodeIndex) const;
	// 节点在布局坐标中的矩形（不乘缩放），用于计算需要的缩放比例
	TOptional<FBox2D> GetNodeLayoutBounds(int32 NodeIndex) const;

	// 边的条件文字（例如 "Heavy R [0.6, ∞)"），供面板在去向提示里复用
	FString GetEdgeLabel(const int32 EdgeIndex) const
	{
		return EdgeLabels.IsValidIndex(EdgeIndex) ? EdgeLabels[EdgeIndex] : FString();
	}

	// 整张图按比例绘制，期望尺寸跟着缩放；布局坐标本身不变
	void SetZoom(float InZoom);
	float GetZoom() const { return Zoom; }
	void SetDebugView(const FCadenceArcDebugView& InDebugView);


	// 换图时重新布局并缓存标签；传 nullptr 表示清空
	void SetGraph(const UCadenceArcGraph* InGraph);

	virtual FVector2D ComputeDesiredSize(float) const override;

	virtual int32 OnPaint(
		const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	struct FCadenceArcGraphPalette
	{
		FLinearColor NormalNodeFill = FLinearColor(0.25f, 0.25f, 0.25f);
		FLinearColor CommittedNodeFill = FLinearColor(0.10f, 0.55f, 0.20f);
		FLinearColor CandidateOutline = FLinearColor(1.0f, 0.8f, 0.1f);
		FLinearColor NormalEdge = FLinearColor(0.55f, 0.55f, 0.55f);
		FLinearColor CandidateEdge = FLinearColor(1.0f, 0.8f, 0.1f);
		FLinearColor PreparatoryEdge = FLinearColor(1.0f, 0.42f, 0.06f);
		FLinearColor PreparatoryProgress = FLinearColor(1.0f, 0.62f, 0.12f);
	};

	// Slate 控件不参与 GC，引用 UObject 必须用弱引用
	TWeakObjectPtr<const UCadenceArcGraph> Graph;
	FCadenceArcGraphLayout Layout;
	FCadenceArcDebugView DebugView;

	// 布局和绘制共用同一份几何参数；节点位置、尺寸和连线路径都由布局给出，画布不再自己算。
	FCadenceArcLayoutParams Params;
	float Zoom = 1.f;

	// 以下都在 SetGraph 里一次算好，OnPaint 只读这些，不再按下标访问活资产：
	// 面板开着时资产被删改，也不会拿旧下标越界。
	TArray<FString> EdgeLabels; // 每条边的条件文字，与 Layout.Edges 同序
};
