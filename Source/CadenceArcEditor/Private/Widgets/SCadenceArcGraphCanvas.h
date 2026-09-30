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

	// 平移和缩放要改滚动区，而滚动区在面板里：画布只识别手势，再交给面板处理
	void SetInteractionHandlers(
		TFunction<void(const FVector2D& ScreenDelta)> InOnPan,
		TFunction<void(float WheelDelta, const FVector2D& CanvasLocalPosition)> InOnZoom);

	// Arc History 里点中的记录对应的节点和边（索引指向当前布局）；空数组和 INDEX_NONE 表示没有
	void SetHistoryFocus(const TArray<int32>& InNodes, int32 InEdge);

	// 列号相差不小于 MinSpan 的边画成引用标记（0 关闭），立即重新布局。节点和边的索引不变，调试状态保留。
	void SetReferenceMinSpan(int32 MinSpan);
	int32 GetReferenceMinSpan() const { return Params.ReferenceMinSpan; }

	// 只改变几何布局；运行时状态、历史焦点和当前缩放不变。
	void SetLayoutMode(ECadenceArcLayoutMode InMode);
	ECadenceArcLayoutMode GetLayoutMode() const { return Params.Mode; }

	// 左键点引用标记跳到目标、点目标一侧的接入线跳回源节点；滚动区在面板里，画布只报告要看哪个节点
	void SetNavigateHandler(TFunction<void(int32 NodeIndex)> InOnNavigate) { OnNavigate = MoveTemp(InOnNavigate); }

	virtual FVector2D ComputeDesiredSize(float) const override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FCursorReply OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const override;

	virtual int32 OnPaint(
		const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	void RebuildGeometry(bool bResetHover = true);

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

	// 交互状态：只影响显示
	TFunction<void(const FVector2D&)> OnPan;
	TFunction<void(float, const FVector2D&)> OnZoom;
	TFunction<void(int32)> OnNavigate;
	bool bPanning = false;
	int32 HoveredNode = INDEX_NONE;
	int32 HoveredEdge = INDEX_NONE;
	FVector2D HoverPoint = FVector2D::ZeroVector; // 布局坐标
	TArray<int32> FocusNodes;
	int32 FocusEdge = INDEX_NONE;

	// 以下都在 SetGraph 里一次算好，OnPaint 只读这些，不再按下标访问活资产：
	// 面板开着时资产被删改，也不会拿旧下标越界。
	TArray<FString> EdgeLabels; // 每条边的条件文字，与 Layout.Edges 同序
};
