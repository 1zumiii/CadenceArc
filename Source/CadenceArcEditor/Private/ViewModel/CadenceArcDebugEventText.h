#pragma once

#include "CoreMinimal.h"
#include "Resolver/CadenceArcDebugHistory.h"

// 一条历史记录给人看的文字。成功只有一行摘要；失败才附带结果和原因（策划需要知道"为什么没反应"）。
struct FCadenceArcDebugEventText
{
	FString Time; // "12.35s"；取自之前调用的时间为 "~12.35s"；完全没有时间时为空
	FString Summary; // 一行摘要，例如 "Light P → SkillA (request #5)"
	FString FailureDetail; // 仅失败时非空，例如 "No transition for Light P from SkillD (NoMatchingTransition)"
	bool bFailed = false;
};

// 纯函数：只根据记录本身生成文字，不读 Resolver、不读资产。节点和输入只显示 Tag 的最后一段。
FCadenceArcDebugEventText FormatDebugEvent(const FCadenceArcDebugEvent& Event);
