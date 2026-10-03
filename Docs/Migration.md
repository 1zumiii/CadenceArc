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

## 转移条件与优先级（0.4.0）

已有的资产不用改：新字段的默认值（优先级 0、没有条件、不看停顿）下，解析结果和之前一样。需要注意的地方：

- `ECadenceArcResolutionReason` 追加了 `ConditionNotMet` 和 `AmbiguousTransition`，已有的值不变。对它做穷举 switch 的代码需要处理这两个成员。
- 图校验放宽了“重叠的转移”：以前同源、同 Tag、同阶段、时长区间重叠就报错，现在还要优先级相同、停顿区间重叠、条件可能同时满足，才报错。以前通不过校验的图，加上优先级或互斥条件后就能用。重叠报错的文字也变了，按原文匹配报错的测试需要更新。
- `UCadenceArcGraph::ValidateGraph` 多了一个可选参数 `TArray<FText>* OutWarnings`，用来接收“走不到的节点”这类警告。只传错误数组的旧调用不用改。

## 头文件调整（0.4.0 之后）

- `FCadenceArcInputEvent` 从 `Resolver/CadenceArcResolverTypes.h` 移到 `Input/CadenceArcInputTypes.h`。结构体名字和所在模块不变，蓝图和资产不受影响。`CadenceArcResolverTypes.h` 仍然包含它，原来的 include 照样能编译。
- `Input/CadenceArcInputTrackingTypes.h` 不再包含 `Resolver/CadenceArcResolverTypes.h`。只 include 了 Tracker 的头文件、却用到 `FCadenceArcSubmitOutcome` 这类解析器类型的代码，需要自己加上 `#include "Resolver/CadenceArcResolverTypes.h"`。
