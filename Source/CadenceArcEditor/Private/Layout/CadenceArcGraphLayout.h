#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"

class UCadenceArcGraph;

// 只改变编辑器几何，不改变图的动作、Transition 或运行状态。
enum class ECadenceArcLayoutMode : uint8
{
	Layered, // 原有分层布局：前向边严格向右推进。
	CompactChains // 将无分叉、无额外汇入的简单链纵向展开，整体仍按分层图排列。
};

// 前向边的画法。两种画法的端点、走线区域和合法性保证都相同，只是中间的形状不同。
enum class ECadenceArcEdgeStyle : uint8
{
	Curved, // 光滑曲线（保单调三次样条）
	Orthogonal // 只有水平和竖直线段的折线；竖线走在列间空隙里各自的轨道上，互不重叠
};

// 布局的全部几何参数。画布把同一份参数交给布局并用它绘制节点内部的行，节点尺寸只由布局计算。
struct FCadenceArcLayoutParams
{
	ECadenceArcLayoutMode Mode = ECadenceArcLayoutMode::Layered;
	ECadenceArcEdgeStyle EdgeStyle = ECadenceArcEdgeStyle::Curved;
	// 按每条边下一步去向的上下顺序重排节点里的端口行，减少出边之间的交叉。只改显示顺序（见 PortSlot），
	// 资产里的 Transition 顺序、边的索引都不变。
	bool bSortPorts = false;
	float TrackInset = 32.f; // 直角走线的竖线离列边的最小距离：让开自环、回边的竖线（最远 ReturnStub + 9）
	float TrackSpacing = 8.f; // 同一空隙里相邻两条竖线的最大间距，空隙放不下时按比例压缩
	// 引用标记：列号相差不小于它的边不画长线，改为源端口旁的一个小标记（写着目标名）加目标左侧的一小段接入线。
	// 列号和行序的含义不变，只是这些边不再占用中间列的通道或底部通道。0 表示关闭。
	int32 ReferenceMinSpan = 0;
	float ReferenceGap = 30.f; // 标记与端口的水平距离：让开自环（LoopMargin）和回边（ReturnStub + 最多 9 的错开）的竖线
	float ReferenceWidth = 80.f; // 标记宽度：Gap + Width 离下一列还剩 10，留给接入箭头
	float ReferenceHeight = 16.f; // 小于 PortHeight，相邻端口的标记之间还有 2 像素
	float ReferenceEntryStub = 16.f; // 目标一侧接入线的长度，短于 ReturnStub，不碰回边的竖线
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
	int32 Column = 0; // 分层模式为最长路径深度；紧凑模式为收缩简单链后的显示列，链内节点同列。
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
	int32 PortSlot = INDEX_NONE; // 端口在源节点里显示在第几行；不重排端口时等于 TransitionIndex
	int32 TargetNodeIndex = INDEX_NONE; // 目标不存在时为 INDEX_NONE
	FCadenceArcTransition Transition; // 构建时从 Transition 复制，下游匹配边时不必再按下标读活资产
	// 从入口深度优先遍历时指向当前递归路径上节点的边（含自环）。它闭合一个环，不参与列号计算。
	bool bIsBackEdge = false;
	// 非局部链内边、非自环且目标不在右列时，从底部通道绕回；链内转向不等于拓扑回边。
	int32 ReturnLane = INDEX_NONE;
	// 实际绘制的折线：第一个点是源节点的端口，最后一个点是目标标题行左侧的接入点（坏目标为短线末端）。
	// 引用边只有从端口到标记左边缘的一小段。
	TArray<FVector2D> Path;
	// 引用边（见 ReferenceMinSpan）：源一侧的标记矩形，和目标一侧的接入线（尾端 -> 接入点）。普通边两者都为空。
	bool bIsReference = false;
	FBox2D ReferenceBox = FBox2D(ForceInit);
	TArray<FVector2D> EntryStub;
	bool IsBrokenTarget() const { return TargetNodeIndex == INDEX_NONE; }
};

// 只用于布局与显示的分组；成员仍然存在于 Nodes 中，边仍是一条 Transition 对应一项。
struct FCadenceArcLayoutChain
{
	TArray<int32> NodeIndices; // 按链的执行顺序排列，不依赖资产中的节点数组顺序。
	FBox2D Bounds = FBox2D(ForceInit); // 包含成员、局部连线及分组留白。
};

struct FCadenceArcGraphLayout
{
	TArray<FCadenceArcLayoutNode> Nodes; // 与 Graph->Nodes 一一对应、顺序相同
	TArray<FCadenceArcLayoutEdge> Edges; // 每条 Transition 一项，按节点顺序、Transition 顺序排列
	TArray<FCadenceArcLayoutChain> FoldedChains; // 分层模式或没有合适的简单链时为空。
	int32 NumColumns = 0;
	int32 MaxRows = 0;
	int32 NumReturnLanes = 0;
	FVector2D Size = FVector2D::ZeroVector; // 画布需要的尺寸，含四周留白
};

// 读取图资产生成一份值快照；之后刷新运行状态和绘制都只用这份快照。同一输入每次结果逐点一致。
// 视口计算见 CadenceArcViewportMath.h，鼠标命中测试见 CadenceArcLayoutHitTest.h。
FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph, const FCadenceArcLayoutParams& Params = {});
