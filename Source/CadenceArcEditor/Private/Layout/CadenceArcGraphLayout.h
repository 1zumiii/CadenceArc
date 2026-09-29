#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"

class UCadenceArcGraph;

// 布局的全部几何参数。画布把同一份参数交给布局并用它绘制节点内部的行，节点尺寸只由布局计算。
struct FCadenceArcLayoutParams
{
	float NodeWidth = 180.f;
	float HeaderHeight = 24.f; // 节点标题行
	float PortHeight = 18.f; // 每条出边一行
	float NodeBottomPad = 4.f; // 有端口的节点底部留白
	float ColumnGap = 120.f; // 相邻两列之间的空隙，列间 S 曲线和回边竖线都走在这里
	float NodeGap = 44.f; // 同一列两个真实节点之间的最小空隙
	float ChannelHeight = 10.f; // 长边穿过中间列时预留的通道高度（线宽、箭头和安全余量）
	float ChannelGap = 10.f; // 通道与相邻通道或节点之间的最小空隙
	float Padding = 36.f; // 至少容纳指向第 0 列的回边在左侧空隙里的竖线
	float LoopMargin = 14.f; // 自环绕出节点的距离
	float BrokenStubLength = 24.f;
	float ReturnLaneTopGap = 20.f; // 节点区域（含自环）与第一条底部通道的距离
	float ReturnLaneSpacing = 12.f; // 相邻底部通道的距离
	float ReturnStub = 20.f; // 回边离开端口、接近目标时在列间空隙里的水平距离
	float EntrySpread = 5.f; // 同一目标的多条入边在标题行左侧错开的间距
	float CornerRadius = 10.f; // 回边、自环拐角的圆角半径（受相邻线段长度限制）
	int32 CurveSamples = 16; // 每段列间 S 曲线的采样段数
};

struct FCadenceArcLayoutNode
{
	int32 NodeIndex = INDEX_NONE; // 对应 Graph->Nodes 的数组索引
	FGameplayTag ActionTag;
	int32 Column = 0; // 去掉回边后，从入口出发的最长路径长度；不可达节点放在最后一列
	int32 Row = 0; // 同一列真实节点中的顺序（不计长边通道），按重心法减少交叉
	bool bReachable = false;
	int32 NumPorts = 0; // 出边数，决定节点高度
	FVector2D Position = FVector2D::ZeroVector; // 画布局部坐标中的左上角
	FVector2D Size = FVector2D::ZeroVector;
};

struct FCadenceArcLayoutEdge
{
	int32 SourceNodeIndex = INDEX_NONE; // 源节点在 Graph->Nodes 中的索引
	int32 TransitionIndex = INDEX_NONE; // 源节点 Transitions 中的索引
	int32 TargetNodeIndex = INDEX_NONE; // 目标不存在时为 INDEX_NONE
	FCadenceArcTransition Transition; // 构建时从 Transition 复制，下游匹配边时不必再按下标读活资产
	// 从入口深度优先遍历时指向当前递归路径上节点的边（含自环）。它闭合一个环，不参与列号计算。
	bool bIsBackEdge = false;
	// 目标列不在源列右侧的非自环边，从所有节点下方的通道绕回；值为通道序号，从上往下数。
	int32 ReturnLane = INDEX_NONE;
	// 实际绘制的折线：第一个点是源节点的端口，最后一个点是目标标题行左侧的接入点（坏目标为短线末端）。
	TArray<FVector2D> Path;
	bool IsBrokenTarget() const { return TargetNodeIndex == INDEX_NONE; }
};

struct FCadenceArcGraphLayout
{
	TArray<FCadenceArcLayoutNode> Nodes; // 与 Graph->Nodes 一一对应、顺序相同
	TArray<FCadenceArcLayoutEdge> Edges; // 每条 Transition 一项，按节点顺序、Transition 顺序排列
	int32 NumColumns = 0;
	int32 MaxRows = 0;
	int32 NumReturnLanes = 0;
	FVector2D Size = FVector2D::ZeroVector; // 画布需要的尺寸，含四周留白
};

// 读取图资产生成一份值快照；之后刷新运行状态和绘制都只用这份快照。同一输入每次结果逐点一致。
FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph, const FCadenceArcLayoutParams& Params = {});

// 视口跟随的滚动计算（单个轴）：已提交节点为 [PrimaryMin, PrimaryMax]，它和直接后继的外包范围为
// [GroupMin, GroupMax]。整组放得下就以最小移动让整组可见；放不下则保证已提交节点完整可见，
// 并在这个前提下尽量朝整组的中心靠。已提交节点本身比视口还大时对齐它的起点。结果限制在 [0, MaxOffset]。
double ComputeFollowOffset(
	double CurrentOffset, double Visible, double PrimaryMin, double PrimaryMax,
	double GroupMin, double GroupMax, double Margin, double MaxOffset);
