#pragma once

#include "CoreMinimal.h"
#include "Resolver/CadenceArcResolverTypes.h"


struct FCadenceArcGraphLayout;
class UCadenceArcResolver;


struct FCadenceArcDebugView
{
	ECadenceArcResolverState ResolverState = ECadenceArcResolverState::Uninitialized;
	FCadenceArcActionRequest OutstandingRequest;
	int32 CommittedNodeIndex = INDEX_NONE;
	// 下一次解析的来源与已提交节点分开；超时恢复不会移动已提交高亮。
	FGameplayTag EffectiveSourceActionTag;
	double ComboResetRemainingSeconds = -1.0;
	int32 CandidateTargetNodeIndex = INDEX_NONE;
	int32 CandidateEdgeIndex = INDEX_NONE;
	bool bBufferWindowOpen = false;
	FGameplayTag BufferedInputTag;
	FCadenceArcHoldSnapshot HoldSnapshot = {};
	// 与 Layout.Edges 对齐；-1 表示非预备边，其余值为这条边的蓄力填充比例。
	TArray<float> PreparatoryEdgeProgress;
	int32 CurrentReleaseEdgeIndex = INDEX_NONE;
	int32 CurrentReleaseTargetNodeIndex = INDEX_NONE;
	// 与 Layout.Nodes 对齐：从已提交节点出发沿出边的最少步数（已提交节点为 0，循环边照常计入）；
	// 不按 Reset、不被打断就再也走不到的节点为 INDEX_NONE。没有已提交节点时为空数组。
	TArray<int32> NodeDistance;
};

// 每次都从默认值开始构建，不会残留上一帧的候选；只调用 Resolver 的 const 读取接口和 Layout 中的边副本。
// 索引都指向 InLayout，调用方需保证 InLayout 由该 Resolver 当前的图构建。
FCadenceArcDebugView BuildDebugView(const UCadenceArcResolver& InResolver, const FCadenceArcGraphLayout& InLayout);
