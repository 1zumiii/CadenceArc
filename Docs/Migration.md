# 迁移说明

[返回首页](../README.md)

本文列出相对于早期开发版 API 的不兼容改动和迁移方法。

## 结果类型

- `SubmitInput` 返回 `FCadenceArcSubmitOutcome`，不再返回结果枚举加输出参数。把 `ECadenceArcInputResult` 的比较改成类别和原因的判断。
- 完成时的消费结果通过 `GetBufferConsumption()` 和 `GetBufferConsumptionReason()` 读取，读取前先确认握手成功。旧值的对应关系如下：

| 旧值 | 新值 |
| --- | --- |
| `Resolved` | `RequestProduced / None` |
| `InvalidTime` | `Rejected / InvalidCompletionTime` |
| `Expired` | `NoAction / Expired` |

- `Reset()` 返回 `ECadenceArcResolverResetResult`，不再返回 `bool`。
- 初始化结果 `Busy` 改名为 `UnexpectedState`。
- 结果类型的字段改为私有，请通过 getter 读取。
- 使用旧枚举输出的蓝图节点需要手动重新连线。重定向无法将枚举输出转换成结构体。

## Gesture 改名为 Hold

以下类型、字段和枚举成员已更名。其中，枚举成员的数值保持不变：

| 旧名 | 新名 |
| --- | --- |
| `ReleaseGestureConfig` | `HoldChargeConfigs` |
| `PendingTap` | `Holding` |
| `ReleaseGesture` | `HoldRelease` |
| 测试路径 `CadenceArc.Graph.Gesture.*` | `CadenceArc.Graph.Hold.*` |

`Config/DefaultCadenceArc.ini` 为已有资产提供核心重定向。加载和枚举查找已做过验证，但尚未逐一确认所有历史二进制资产在加载并重新保存后，是否完整保留原有数据。

## 新增的枚举成员

按住功能追加了成员，已有的值不变。对这两个枚举做穷举 switch 的代码需要处理新成员：

- `ECadenceArcResolutionReason`：`InvalidInputEvent`、`InputIdentityRequired`、`NoMatchingHold`、`HoldProtected`、`InputTimeAdvanceRequired`、`WaitingForRelease`、`InvalidGraphConfiguration`；
- `ECadenceArcHandshakeResult`：`InvalidCompletionTime`、`InputTimeAdvanceRequired`。

## 松手必须带 Token

`SubmitInput` 现在会以 `InputIdentityRequired` 拒绝 `Released` 事件。松手必须带着按下时的 Token，通过 `ReleaseInputHold` 提交。阶段或按住时长不一致的事件，以 `InvalidInputEvent` 拒绝。

## 转移条件与优先级（0.4.0）

已有合法资产无需修改。新增字段使用默认值（优先级 0、上下文条件为空、未启用停顿区间）时，解析行为不变。迁移时需要注意以下变化：

- `ECadenceArcResolutionReason` 追加了 `ConditionNotMet` 和 `AmbiguousTransition`，已有的值不变。对它做穷举 switch 的代码需要处理这两个成员。
- 图校验放宽了转移重叠规则。源节点、输入 Tag 和阶段相同的两条边，只有在时长区间重叠、优先级相同、停顿区间重叠且条件可能同时满足时，才报告重叠错误。调整优先级或配置互斥条件可以消除这类错误，图仍需通过其他校验。重叠错误的文字也已更新，按报错原文匹配的测试需要相应调整。
- `UCadenceArcGraph::ValidateGraph` 新增可选参数 `TArray<FText>* OutWarnings`，用于接收不可达节点等警告。只传错误数组的旧调用无需修改。

## 头文件调整（0.4.0 之后）

- `FCadenceArcInputEvent` 从 `Resolver/CadenceArcResolverTypes.h` 移到 `Input/CadenceArcInputTypes.h`。结构体名称和所在模块不变，蓝图和资产不受影响。`CadenceArcResolverTypes.h` 仍包含新的定义文件，因此原有包含方式仍然有效。
- `Input/CadenceArcInputTrackingTypes.h` 不再包含 `Resolver/CadenceArcResolverTypes.h`。如果代码仅包含 Tracker 头文件，却使用了 `FCadenceArcSubmitOutcome` 等解析器类型，需要显式添加 `#include "Resolver/CadenceArcResolverTypes.h"`。

## 标准接入组件（0.4.0 之后）

- 新增 `UCadenceArcComponent`，作为 UE 项目的标准接入方式。组件自动处理时间戳、逐帧推进、按键配对和请求出口，详见[CadenceArc 组件](Component.md)。直接调用 `UCadenceArcResolver` 的代码仍然有效，不需要修改。
- CadenceArcSandbox 中的 `FCadenceArcHoldInputRouter` 已删除，功能并入 `UCadenceArcComponent`。参照 Sandbox 复制过这个类的项目，可以改用组件的 `PressInput`、`ReleaseInput` 和 `CancelInput`。原有的 9 项 `CadenceArc.Sandbox.HoldRouter.*` 测试已迁移为插件中的 `CadenceArc.Component.*` 测试。
- Sandbox 的动作图从 `UCadenceArcDemoExecutorComponent::ComboGraph` 移到了角色上 CadenceArc 组件的 `Graph` 属性。
