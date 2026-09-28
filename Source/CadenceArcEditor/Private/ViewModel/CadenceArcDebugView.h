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
	int32 CandidateTargetNodeIndex = INDEX_NONE;
	int32 CandidateEdgeIndex = INDEX_NONE;
	bool bBufferWindowOpen = false;
	FGameplayTag BufferedInputTag;
	FCadenceArcHoldSnapshot HoldSnapshot = {};
};

// 每次都从默认值开始构建，不会残留上一帧的候选；只调用 Resolver 的 const 读取接口，不读图资产。
// 索引都指向 InLayout，调用方需保证 InLayout 由该 Resolver 当前的图构建。
FCadenceArcDebugView BuildDebugView(const UCadenceArcResolver& InResolver, const FCadenceArcGraphLayout& InLayout);
