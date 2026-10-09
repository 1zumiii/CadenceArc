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
	// 图上的当前位置：Ready 使用有效源，待确认使用请求源，执行中使用已提交节点。
	int32 DisplayNodeIndex = INDEX_NONE;
	// 下一次解析的来源与最后提交的节点分开，查询不修改运行时状态。
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
	// 与 Layout.Nodes 对齐：从显示位置出发沿出边的最少步数；不可达为 INDEX_NONE。
	TArray<int32> NodeDistance;
};

// 每次都从默认值开始构建，不会残留上一帧的候选；只调用 Resolver 的 const 读取接口和 Layout 中的边副本。
// 索引都指向 InLayout，调用方需保证 InLayout 由该 Resolver 当前的图构建。
FCadenceArcDebugView BuildDebugView(const UCadenceArcResolver& InResolver, const FCadenceArcGraphLayout& InLayout);
