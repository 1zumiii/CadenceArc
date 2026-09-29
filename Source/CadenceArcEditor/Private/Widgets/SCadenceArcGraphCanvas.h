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

	FVector2D GetNodeTopLeft(const FCadenceArcLayoutNode& Node) const;
	float GetNodeHeight(int32 NodeIndex) const;
	// 出边在源节点右侧的引出点：第 TransitionIndex 个端口行的垂直中点
	FVector2D GetPortAnchor(const FCadenceArcLayoutEdge& Edge) const;
	// 入边的接入点：目标节点标题行左侧中点
	FVector2D GetInputAnchor(int32 NodeIndex) const;
	// 第 Lane 条底部通道的纵坐标：在所有节点和自环下方，从上往下排
	double GetReturnLaneY(int32 Lane) const;
	// 边在画布上的完整路径（折线点）：普通边为采样后的 S 曲线，自环、回边和坏目标为折线
	TArray<FVector2f> BuildEdgePath(const FCadenceArcLayoutEdge& Edge) const;

	// Slate 控件不参与 GC，引用 UObject 必须用弱引用
	TWeakObjectPtr<const UCadenceArcGraph> Graph;
	FCadenceArcGraphLayout Layout;
	FCadenceArcDebugView DebugView;

	// 以下都在 SetGraph 里一次算好，OnPaint 只读这些，不再按下标访问活资产：
	// 面板开着时资产被删改，也不会拿旧下标越界。
	TArray<int32> PortCounts; // 每个节点的出边数，与 Layout.Nodes 同序
	TArray<FString> EdgeLabels; // 每条边的条件文字，与 Layout.Edges 同序
	float MaxNodeHeight = 0.f;
	float RowSpacing = 0.f; // 取全图最高节点，保证同一列上下不重叠
};
