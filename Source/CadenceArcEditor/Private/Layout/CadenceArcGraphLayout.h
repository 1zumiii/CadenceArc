#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Graph/CadenceArcGraphTypes.h"

class UCadenceArcGraph;


struct FCadenceArcLayoutNode
{
	int32 NodeIndex = INDEX_NONE; // 对应 Graph->Nodes 的数组索引
	FGameplayTag ActionTag;
	int32 Column = 0; // 去掉回边后，从入口出发的最长路径长度；不可达节点放在最后一列
	int32 Row = 0; // 同一列内的顺序，按重心法减少交叉
	bool bReachable = false;
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
	bool IsBrokenTarget() const { return TargetNodeIndex == INDEX_NONE; }
};

struct FCadenceArcGraphLayout
{
	TArray<FCadenceArcLayoutNode> Nodes; // 与 Graph->Nodes 一一对应、顺序相同
	TArray<FCadenceArcLayoutEdge> Edges; // 每条 Transition 一项，按节点顺序、Transition 顺序排列
	int32 NumColumns = 0;
	int32 MaxRows = 0;
	int32 NumReturnLanes = 0;
};

FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph);
