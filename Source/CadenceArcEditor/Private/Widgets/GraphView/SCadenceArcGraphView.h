#pragma once

#include "CoreMinimal.h"
#include "Layout/CadenceArcGraphLayout.h"
#include "ViewModel/CadenceArcDebugView.h"
#include "Widgets/SCompoundWidget.h"

class SCadenceArcGraphCanvas;
class SCadenceArcInputStrip;
struct FCadenceArcInputDisplay;
class SCanvas;
class SScrollBox;
class UCadenceArcGraph;

/**
 * Arc Debugger 的图视口：可滚动的画布、视口外去向提示，以及浏览相关的行为——
 * 跟随当前节点（自动缩放和滚动）、右键 / 中键拖动平移、Ctrl + 滚轮缩放、
 * 点引用标签或提示跳转、Arc History 点中记录时高亮并滚到对应位置。
 * 只显示，不读 Resolver：图和调试状态都由面板传进来。
 */
class SCadenceArcGraphView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCadenceArcGraphView)
		{
		}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// 换图（同一张图每帧调用也可以：布局重建，悬停保留）；传 nullptr 表示清空
	void SetGraph(const UCadenceArcGraph* Graph);
	const FCadenceArcGraphLayout& GetLayout() const;

	// 显示一帧调试状态：更新画布，再按它跟随、同步历史焦点、更新视口外提示
	void ShowDebugView(const FCadenceArcDebugView& View);

	// 左下角的输入显示；bVisible 为 false 或没有有效数据时隐藏
	void ShowInputDisplay(const FCadenceArcInputDisplay& Display, bool bVisible);

	// 换一套布局选项；布局变了，下一帧按新位置重新定位当前节点
	void SetLayoutParams(const FCadenceArcLayoutParams& Params);
	const FCadenceArcLayoutParams& GetLayoutParams() const;

	// 跟随开关。手动平移或缩放会自动关掉；关掉时缩放回到 1 倍，方便手动阅读
	bool IsFollowing() const { return bFollowCommittedNode; }
	void SetFollowing(bool bFollow);
	// 下一帧无论当前节点变没变都重新定位一次（例如换了 Resolver 实例）
	void RequestFollow();

private:
	TSharedPtr<SCadenceArcGraphCanvas> Canvas;
	TSharedPtr<SCadenceArcInputStrip> InputStrip;
	TSharedPtr<SScrollBox> HorizontalScroll;
	TSharedPtr<SScrollBox> VerticalScroll;

	// 视口跟随：已提交节点变化时把它滚进视口，其余时间不动用户手动滚到的位置
	bool bFollowCommittedNode = true;
	int32 DisplayNodeIndex = INDEX_NONE;
	int32 LastFollowedNodeIndex = INDEX_NONE;
	int32 PendingFollowFrames = 0; // 换图后滚动区的内容尺寸下一帧才更新，跟随要连续做几帧

	// 视口外去向提示：当前节点的直接后继完全不在视口内时，在边缘放一个可点击的提示
	TSharedPtr<SCanvas> HintCanvas;
	FString HintSignature; // 上一次提示的内容，没变就不重建按钮

	uint64 AppliedFocusSequence = 0; // 已经滚到过的历史记录，同一条只滚一次

	void FollowCommittedNode();
	void UpdateOffscreenHints();
	void ApplyHistoryFocus();
	void ScrollNodeIntoView(int32 NodeIndex);

	// 手动浏览：画布把手势交给这里改滚动区和缩放
	void PanBy(const FVector2D& ScreenDelta);
	void ZoomAt(float WheelDelta, const FVector2D& CanvasLocal);
	FVector2D GetVisibleSize() const;
};
