# 按住输入

[返回首页](../README.md)

按住输入根据按键持续时间触发不同动作，例如轻按攻击和蓄力攻击。本文介绍图配置、宿主的逐帧处理流程和接入注意事项。

## 概念

一次 Hold 是从按下到松开的完整过程，由 `FCadenceArcInputToken` 标识。按下后立即松开也属于一次 Hold。

- `FCadenceArcInputTracker` 负责物理按下和松开的配对，并测量按住时长。
- 解析器负责管理按住资格：按下时申请，松开时根据输入和条件解析动作请求。
- `ECadenceArcInputMode::PressOnly` 在按下时提交；`HoldRelease` 在按下时申请资格，在手动或自动松手时结算。
- `ECadenceArcHoldStage` 有 `None`、`Holding`、`Charging` 和 `Charged`，由时间推导，不单独存储。

## 图配置

Released 转移可以启用 `DurationRange`，按左闭右开的区间 `[Min, Max)` 匹配按住时长，也可以不设置上限。同一输入可以有多个松手档位，区间可以相邻或存在间隔。区间重叠时，还需通过优先级、上下文条件和停顿区间的校验，详见[动作图与校验](Graph.md#校验)。

在 `FCadenceArcNode::HoldChargeConfigs` 中添加 `FCadenceArcHoldChargeConfig`，可以为某个输入 Tag 启用蓄力保护和自动释放。配置后，该 Tag 的所有 Released 边都必须启用按住时长区间，且至少有一条无上限区间。所有无上限分支共用一个正数下限，作为满蓄力阈值。

`ChargeStartSeconds` 指定开始蓄力的时长，必须大于等于 0，并小于满蓄力阈值。设为 0 时，授予资格便进入 `Charging`。`MaxChargedHoldSeconds` 指定满蓄力后还可以保持多久；为 0 时，达到满蓄力阈值便自动释放。未配置 `HoldChargeConfigs` 时，输入仍可按松手时长选择动作，但不启用蓄力保护和自动释放。

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
| `BeginInputHold` | `Ready`，或窗口打开的 `Executing`；通过输入、配置和现有资格的检查 | 保存资格，并复制该 Tag 的 Released 边、蓄力配置和缓冲时长上限。 |
| `AdvanceInputTime` | 已初始化，时间有限且非负；存在资格时，不早于该资格的最近观察时间 | 按顺序报告经过的阈值，到期时自动释放一次。没有待松手资格时接受调用，但不改变业务状态。 |
| `ReleaseInputHold` | Token 匹配，且事件是同一 Tag 的 `Released` 事件 | 结束资格。`Ready` 时立即解析，`Executing` 时存为缓冲。 |
| `CancelInputHold` | Token 与当前按住资格或其松手缓冲匹配 | 清空对应输入槽，不合成松手事件，也不撤回已产生的请求。 |

`BeginInputHold` 的结果也提供 `GetStageChanges()`。蓄力起点为 0 时，成功授予的结果带有一次 `Charging`，生效时间为按下时刻；后续 `AdvanceInputTime` 不重复报告。直接接解析器的宿主需要处理这份结果中的阶段变化。使用 CadenceArc 组件时，组件在 `PressInput` 内通过 `OnHoldStageChanged` 广播，无需等待下一帧。

使用 [CadenceArc 组件](Component.md) 时，下面的逐帧推进、Token 生成和松手提交都由组件完成，阶段变化通过 `OnHoldStageChanged` 发出。直接使用解析器时，宿主每帧**先推进时间**，处理完结果，再处理输入和生命周期回调：

```cpp
const FCadenceArcInputAdvanceOutcome Advance = Resolver->AdvanceInputTime(NowSeconds);
for (const FCadenceArcInputStageChange& Change : Advance.GetStageChanges())
{
    // 使用 Change.EffectiveTimestampSeconds 作为蓄力反馈的生效时间。
}
if (Advance.HasActionRequest())
{
    StartRequest(Advance.GetResolution().GetActionRequest());
}
```

## 规则

一次按下最多产生一个动作请求。自动释放结束资格后，再用同一 Token 调用 `ReleaseInputHold`，会返回 `NoMatchingHold`。

关闭缓冲窗口和正常完成动作都保留待松手资格，蓄力计时继续。存在待松手资格时，完成回调报告 `NoAction / WaitingForRelease`。

资格处于 `Charging` 或 `Charged` 时，新输入通过状态准入检查后，仍会因蓄力保护返回 `HoldProtected`。宿主需要通过闪避、格挡等操作打断蓄力时，应调用 `CancelInputHold`。没有蓄力配置的资格保持 `Holding`，可以由新接受的输入替换。

自动释放到期后，宿主应先调用 `AdvanceInputTime` 并处理结果。若仍有到期资格未处理，新输入在通过状态准入检查后会返回 `Rejected / InputTimeAdvanceRequired`；有效完成回调也会以握手结果 `InputTimeAdvanceRequired` 拒绝。这些拒绝不修改业务状态。

松手解析使用授予资格时保存的转移副本。按住期间修改资产上的转移条件、优先级或时长配置，不会改变这份副本。目标节点是否存在仍按当前图检查，持久上下文仍取实际解析时的值。

有效松手会结束按住资格，即使解析时没有匹配边、条件不满足或输入已经过期，也不会恢复该资格。

宿主或 Tracker 负责填写松手事件的按住时长。解析器会校验 `HeldDurationSeconds` 是否等于一次 `double` 运算得到的 `TimestampSeconds - PressedTimestampSeconds`，不一致时返回 `InvalidInputEvent`。

开始蓄力、满蓄力和自动释放的时间点，按持续时间首次达到对应阈值的可表示 `double` 时间计算。这保证了阶段判断与按住时长区间在 0.2、0.8 等小数阈值处一致。最终能否产生动作请求，还取决于上下文条件、停顿区间和优先级。

`BeginInputHold` 在授予资格前校验源节点，配置无效时返回 `InvalidGraphConfiguration`。

## 宿主接入注意事项

- 直接调用解析器时，`BeginInputHold` 要求实际选边源有该 Tag 的 `Released` 转移；选边源受超时重置和入口回退规则影响。仍找不到时返回 `NoMatchingTransition`。使用 CadenceArc 组件时，`HoldIfAvailable` 也考虑这些恢复规则，没有可用的 `Released` 转移时改为按下提交，详见[CadenceArc 组件](Component.md#输入方式)。
- 资格申请失败或资格已经结束时，再调用 `ReleaseInputHold` 会返回 `NoMatchingHold`，调试历史将其标记为失败。CadenceArc 组件先用 `GetInputHoldSnapshot()` 确认 Token 是否仍然匹配，再提交松手事件。
- 如果未收到物理松手，且没有触发自动释放或其他清理操作，资格会持续保留。宿主在按住期间失去控制时，例如取消控制 Pawn 或窗口失焦，应取消仍有效的资格，并清理输入追踪状态。使用 [Enhanced Input 适配](EnhancedInput.md#失去输入时的清理)时，重新绑定、解除绑定和控制器变化会自动取消按住中的输入。
- 使用 MSVC 的 `/fp:fast` 编译时，需要精确浮点舍入的代码应显式启用精确模式。例如，`CadenceArcHoldTiming.cpp` 使用 `#pragma float_control(precise, on)` 保护阈值计算。
