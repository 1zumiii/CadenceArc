#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UCadenceArcGraph;


struct FCadenceArcLayoutNode
{
	int32 NodeIndex = INDEX_NONE; // 对应 Graph->Nodes 的数组索引
	FGameplayTag ActionTag;
	int32 Column = 0; // 从入口出发的 BFS 层数；不可达节点放在最后一列
	int32 Row = 0; // 同一列内的顺序
	bool bReachable = false;
};

struct FCadenceArcLayoutEdge
{
	int32 SourceNodeIndex = INDEX_NONE; // 源节点在 Graph->Nodes 中的索引
	int32 TransitionIndex = INDEX_NONE; // 源节点 Transitions 中的索引
	int32 TargetNodeIndex = INDEX_NONE; // 目标不存在时为 INDEX_NONE
	bool IsBrokenTarget() const { return TargetNodeIndex == INDEX_NONE; }
};

struct FCadenceArcGraphLayout
{
	TArray<FCadenceArcLayoutNode> Nodes; // 与 Graph->Nodes 一一对应、顺序相同
	TArray<FCadenceArcLayoutEdge> Edges; // 每条 Transition 一项，按节点顺序、Transition 顺序排列
	int32 NumColumns = 0;
	int32 MaxRows = 0;
};

FCadenceArcGraphLayout BuildGraphLayout(const UCadenceArcGraph& Graph);
