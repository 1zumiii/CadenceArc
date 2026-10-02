# 按住输入

[返回首页](../README.zh-CN.md)

按住输入让同一个键按不同时长触发不同招式，例如轻点和蓄力攻击。本文说明图怎么配置、宿主每帧要做什么，以及几条容易踩的规则。

## 概念

一次 **Hold** 是一次从按下到松开的过程，用 `FCadenceArcInputToken` 标识。按下立刻松开也算一次 Hold。

- `FCadenceArcInputTracker` 负责物理按下和松开的配对，并测量按住时长。
- 解析器负责**按住资格**：按下时申请，松开时结算成动作请求。
- `ECadenceArcInputMode::PressOnly` 在按下时提交；`HoldRelease` 在按下时申请资格，在手动或自动松手时结算。
- `ECadenceArcHoldStage` 有 `None`、`Holding`、`Charging` 和 `Charged`，由时间推导，不单独存储。

## 图配置

转移可以设置 `InputPhase` 和左闭右开的 `DurationRange`（`[Min, Max)`，或没有上限）。同一源节点、同一 Tag、同一阶段的区间不能重叠，可以相邻或留空隙。这样一个键就能有多个松手档位。

`FCadenceArcNode::HoldChargeConfigs` 里可以放 `FCadenceArcHoldChargeConfig`，给某个输入 Tag 加上蓄力保护和自动松手时间。满蓄阈值取这个 Tag 唯一一条无上限 Released 区间的下限。

## 解析器 API

```cpp
FCadenceArcHoldOutcome         BeginInputHold(const FCadenceArcInputToken&, const FCadenceArcInputEvent& Press);
FCadenceArcInputAdvanceOutcome ReleaseInputHold(const FCadenceArcInputToken&, const FCadenceArcInputEvent& Release);
FCadenceArcInputAdvanceOutcome AdvanceInputTime(double NowSeconds);
FCadenceArcHoldOutcome         CancelInputHold(const FCadenceArcInputToken&);
FCadenceArcHoldSnapshot        GetInputHoldSnapshot() const;
```

| 调用 | 何时接受 | 效果 |
| --- | --- | --- |
| `BeginInputHold` | `Ready`，或窗口打开的 `Executing` | 存下一份资格，冻结这个 Tag 的 Released 边、蓄力配置和缓冲时长上限。 |
| `AdvanceInputTime` | 任何状态，时间有限且不递减 | 按顺序报告越过的阈值，到期时自动松手一次。没有资格时是空操作。 |
| `ReleaseInputHold` | Token 匹配，且事件是同一 Tag 的 `Released` 事件 | 结束资格。`Ready` 时立即解析，`Executing` 时存为缓冲。 |
| `CancelInputHold` | Token 匹配 | 清空缓冲格，不合成松手，不撤回已提交的请求。 |

宿主每帧**先推进时间**，处理完结果，再处理输入和生命周期回调：

```cpp
const FCadenceArcInputAdvanceOutcome Advance = Resolver->AdvanceInputTime(NowSeconds);
for (const FCadenceArcInputStageChange& Change : Advance.GetStageChanges())
{
    // 蓄力反馈按 Change.EffectiveTimestampSeconds 播放，而不是 NowSeconds。
}
if (Advance.HasActionRequest())
{
    StartRequest(Advance.GetResolution().GetActionRequest());
}
```

## 规则

**一次按下最多兑现一个动作。** 自动松手之后，物理松手返回 `NoMatchingHold`。

**蓄力不会因为前一个动作结束而降级。** 资格在窗口关闭和正常完成后仍然保留，完成时报告 `NoAction / WaitingForRelease`。

**蓄力中其他输入被挡住。** `Charging` 或 `Charged` 期间，图里的其他输入返回 `HoldProtected`。闪避、格挡这类打断应该直接调用 `CancelInputHold`，不要走图。没有蓄力配置的按住停在 `Holding`，可以被任何被接受的输入替换。

**自动松手到期后要先推进时间。** 这时 `SubmitInput`、`BeginInputHold` 返回 `Rejected / InputTimeAdvanceRequired`，`NotifyActionCompleted` 返回握手结果 `InputTimeAdvanceRequired`，都没有副作用。调用 `AdvanceInputTime` 处理结果后再重试。

**按住期间改资产不影响这次按下。** 松手匹配用的是资格里冻结的边副本。

**松手没匹配或已过期，资格也结束。** 不会退回待定状态。

**时长由解析器自己算。** 传给 `ReleaseInputHold` 的按住时长，必须等于一次 `double` 运算得到的 `TimestampSeconds - PressedTimestampSeconds`。

**阈值时刻精确对齐。** 开始蓄力、满蓄、自动松手的时刻，是按住时长第一次达到阈值的 `double` 时间。快照报告 `Charged` 时，此刻松手选中的档位一定一致，0.2、0.8 这样的小数阈值也一样。

**改坏的资产不会授予资格。** `BeginInputHold` 授予前会校验源节点，失败返回 `InvalidGraphConfiguration`。

## 宿主接入注意事项

- 把某个键设成 `HoldRelease` 后，当前节点必须有这个 Tag 的 `Released` 转移。只有 `Pressed` 边的节点会以 `NoMatchingTransition` 拒绝 `BeginInputHold`。
- 资格已经结束时（被拒、自动松手、被替换或取消），物理松手会返回 `NoMatchingHold`，调试历史里显示为失败。Sandbox 的输入路由先用 `GetInputHoldSnapshot()` 确认资格还是自己的 Token，再调用 `ReleaseInputHold`。
- 宿主一直没送来物理松手时，资格会一直待定。游戏在按住期间失去控制（取消控制 Pawn、窗口焦点吞掉松手）时，应该调用 `CancelInputHold` 并清空追踪器。这些情况下 Enhanced Input 发 `Completed` 还是 `Canceled`，还没有验证过。
- Unreal 用 MSVC 的 `/fp:fast` 编译。需要精确浮点舍入的代码要显式开启，例如 `CadenceArcHoldTiming.cpp` 里的 `#pragma float_control(precise, on)`。
