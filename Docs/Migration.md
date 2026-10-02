# 迁移说明

[返回首页](../README.zh-CN.md)

相对于早期开发版 API 的不兼容改动。

## 结果类型

- `SubmitInput` 返回 `FCadenceArcSubmitOutcome`，不再返回结果枚举加输出参数。把 `ECadenceArcInputResult` 的比较改成类别和原因的判断。
- 完成时的消费结果从 `GetBufferConsumption()` / `GetBufferConsumptionReason()` 读取，先看握手结果。旧值的对应关系：

| 旧值 | 新值 |
| --- | --- |
| `Resolved` | `RequestProduced / None` |
| `InvalidTime` | `Rejected / InvalidCompletionTime` |
| `Expired` | `NoAction / Expired` |

- `Reset()` 返回 `ECadenceArcResolverResetResult`，不再返回 `bool`。
- 初始化结果 `Busy` 改名为 `UnexpectedState`。
- 结果类型的字段改为私有，请用 getter。
- 用了旧枚举输出的蓝图节点需要手动重新连线，重定向没法把枚举输出转换成结构体。

## Gesture 改名为 Hold

枚举值不变，只改名字：

| 旧名 | 新名 |
| --- | --- |
| `ReleaseGestureConfig` | `HoldChargeConfigs` |
| `PendingTap` | `Holding` |
| `ReleaseGesture` | `HoldRelease` |
| 测试路径 `CadenceArc.Graph.Gesture.*` | `CadenceArc.Graph.Hold.*` |

`Config/DefaultCadenceArc.ini` 为已有资产提供核心重定向。重定向在加载和枚举查找上验证过，不保证每个历史二进制资产都能完整往返转换。

## 新增的枚举成员

按住功能追加了成员，已有的值不变。对这两个枚举做穷举 switch 的代码需要处理新成员：

- `ECadenceArcResolutionReason`：`InvalidInputEvent`、`InputIdentityRequired`、`NoMatchingHold`、`HoldProtected`、`InputTimeAdvanceRequired`、`WaitingForRelease`、`InvalidGraphConfiguration`；
- `ECadenceArcHandshakeResult`：`InvalidCompletionTime`、`InputTimeAdvanceRequired`。

## 松手必须带 Token

`SubmitInput` 现在会以 `InputIdentityRequired` 拒绝 `Released` 事件。松手必须带着按下时的 Token，通过 `ReleaseInputHold` 提交。阶段或按住时长不一致的事件，以 `InvalidInputEvent` 拒绝。
