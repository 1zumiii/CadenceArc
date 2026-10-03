#pragma once

// 画布左下角的输入显示：解析器实际收到的语义输入，而不是物理按键。
// 类似格斗游戏训练模式的输入记录，只是显示的是 Tag 和上下文，方便看出"按了但没带方向"这类问题。

#include "CoreMinimal.h"
#include "Input/CadenceArcInputTypes.h"

class UCadenceArcResolver;

// 一次输入（按下、松手或自动松手）
struct FCadenceArcInputChip
{
	FString Input; // 输入 Tag 的最后一段："Light"
	bool bReleased = false; // 松手（含自动松手）
	TArray<FString> Context; // 事件自带的上下文，每个 Tag 的最后一段："Forward"；画在键帽前面
	FString Detail; // 松手时的按住时长："1.20s"、"auto 1.80s"；按下时为空
	double AgeSeconds = 0.0; // 距离"现在"（最近一次调用方传入的时间）多久
	bool bIgnored = false; // 解析器没接受：被拒绝、没有匹配的边、条件不满足等
	bool bBuffered = false; // 动作执行中按下，存进了缓冲
};

struct FCadenceArcInputDisplay
{
	// 像格斗游戏训练模式的输入记录一样一直保留最近几次，由新输入挤出去；旧的只变暗，方便回看和截图
	static constexpr int32 MaxRecentChips = 6;
	static constexpr double DimAfterSeconds = 1.5; // 这之后开始变暗
	static constexpr double DimSeconds = 0.5; // 用这么久降到 DimAlpha
	static constexpr float DimAlpha = 0.4f;

	bool bValid = false; // 有已初始化的 Resolver
	TArray<FCadenceArcInputChip> Recent; // 新的在前，不按年龄移除

	bool bHolding = false; // 有按住资格
	FString HeldInput;
	double HeldSeconds = 0.0;
	ECadenceArcHoldStage HeldStage = ECadenceArcHoldStage::None;

	FString PersistentContext; // "Air" / "none"
	// 停顿状态："pause 0.42s"（Ready 且有起点）、"no pause yet"、"in action, buffered input counts as pause 0"、
	// "waiting for start"
	FString PauseText;

	// 按年龄变暗：DimAfterSeconds 之前为 1，之后 DimSeconds 内线性降到 DimAlpha 并保持
	static float GetChipAlpha(double AgeSeconds);
};

// 只调用 Resolver 的 const 读取接口和调试历史，不改变任何状态
FCadenceArcInputDisplay BuildInputDisplay(const UCadenceArcResolver& Resolver);
