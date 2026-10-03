# 解析器：握手、缓冲与时间

[返回首页](../README.zh-CN.md)

`UCadenceArcResolver` 把输入变成动作请求。它不执行动作，只跟踪请求的生命周期。本文说明宿主怎么和它配合。

## 两阶段握手

图里找到一条转移，不代表动作一定能开始。执行器可能因为资源、状态或时机拒绝请求。所以解析和提交是两步：

```text
解析输入 -> 产出 ActionRequest -> 执行器接受或拒绝 -> Started 提交目标节点 -> 终止回调结束请求
```

解析器从不假定发出的动作已经执行成功。

### 状态

| 状态 | 含义 |
| --- | --- |
| `Uninitialized` | 没有加载有效的图。 |
| `Ready` | 可以解析新输入。 |
| `AwaitingStart` | 有一个请求，等待执行器接受或拒绝。 |
| `Executing` | 执行器已确认动作开始执行。 |

同一时刻最多只有一个未完成的请求。

### 生命周期回调

| 事件 | 要求的状态 | 结果 |
| --- | --- | --- |
| `SubmitInput` 解析成功 | `Ready` | 创建请求，进入 `AwaitingStart`。当前动作不变。 |
| `NotifyActionStarted` | `AwaitingStart` | 提交目标动作，进入 `Executing`。 |
| `NotifyActionRejected` | `AwaitingStart` | 回到 `Ready`，保留源动作，清空请求。 |
| `NotifyActionCompleted` | `Executing` | 保留已提交的动作，消费缓冲。之后进入 `Ready`，或产出下一个请求并进入 `AwaitingStart`。 |
| `NotifyActionCancelled` | `Executing` | 回到 `Ready`，重置到入口动作，清空请求。 |
| `NotifyActionInterrupted` | `Executing` | 同上。 |

`Reset()` 只能在 `Ready` 调用，返回 `Success`、`NotInitialized` 或 `Busy`。

无效的请求 ID、过期回调和状态不对的回调都会被拒绝，解析器状态不变。请求 ID 是单调递增的正数，`Reset` 和重新初始化都不会重置它，所以过期回调不可能对上更新的请求。

### 请求内容

`FCadenceArcActionRequest` 包含 `RequestId`、`InputTag`（选中转移的输入）、`SourceActionTag`（解析时所在的节点）和 `TargetActionTag`（候选动作）。

## 执行器接入

一个最小的执行器：

```cpp
const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(InputEvent);
if (Submit.HasActionRequest())
{
    StartRequest(Submit.GetActionRequest());
}

const FCadenceArcActionCompletionOutcome Completion =
    Resolver->NotifyActionCompleted(CompletedRequestId, NowSeconds);
if (Completion.HasNextActionRequest())
{
    StartRequest(Completion.GetNextActionRequest());
}
```

`StartRequest` 是宿主自己的函数：动作不能执行就调用 `NotifyActionRejected`，否则调用 `NotifyActionStarted(RequestId)`。这个调用返回 `Success` 之后才开始执行动作。

## 结果类型

`FCadenceArcSubmitOutcome` 提供 `GetCategory()`、`GetReason()`、`GetActionRequest()` 和 `HasActionRequest()`。字段都是私有的，getter 返回副本。

| 类别 | 含义 | 请求 |
| --- | --- | --- |
| `RequestProduced` | 找到候选转移，`Reason` 为 `None`。 | 候选请求 |
| `Buffered` | 输入存进了缓冲格，`Reason` 为 `None`。 | 空 |
| `NoAction` | 现在没有可走的转移，例如 `RequestPending`、`BufferWindowClosed`、`NoMatchingTransition`。 | 空 |
| `Rejected` | 调用或图数据无效，原因看 `Reason`。 | 空 |

默认构造的结果永远不表示成功。判断有没有请求，用 `HasActionRequest()` / `HasNextActionRequest()`，不要看请求 ID。`UCadenceArcBlueprintLibrary` 把这些 getter 做成了 BlueprintPure 节点。

## 输入缓冲

缓冲只有一格，后来的有效输入覆盖先前的（Last Input Wins）。有效输入的处理方式取决于状态：

| 状态 | 缓冲窗口 | 结果 |
| --- | --- | --- |
| `Ready` | 无关 | 立即解析。 |
| `AwaitingStart` | 关闭 | `NoAction / RequestPending`，状态不变。 |
| `Executing` | 打开 | 存进缓冲，返回 `Buffered / None`。 |
| `Executing` | 关闭 | `NoAction / BufferWindowClosed`，已存的输入不变。 |

执行器用 `OpenBufferWindow(RequestId)` 和 `CloseBufferWindow(RequestId)` 控制窗口。两者都要求当前执行中的请求 ID，所以过期的动画通知没有影响。关闭窗口会冻结已存的输入，不会清空它。

动作完成时，`NotifyActionCompleted` 返回 `FCadenceArcActionCompletionOutcome`：

- `GetHandshakeResult()`：回调是否对上了当前请求；
- `GetBufferConsumption()` / `GetBufferConsumptionReason()`：缓冲的解析结果，握手成功后才有意义；
- `GetNextActionRequest()`：`HasNextActionRequest()` 为真时的下一个请求。

缓冲被消费后，解析器直接进入 `AwaitingStart`。下一个动作仍要等 `NotifyActionStarted` 才提交。

同一个缓冲格也存按住资格，见[按住输入](HoldInput.md)。

## 时间与过期

时间全部由调用方提供，单位是秒：

```cpp
FCadenceArcSubmitOutcome SubmitInput(const FCadenceArcInputEvent& InputEvent);
FCadenceArcActionCompletionOutcome NotifyActionCompleted(int64 RequestId, double CompletionTimestampSeconds);
```

输入和完成的时间戳必须在同一个不递减的时间域里。解析器从不读取 `UWorld`、平台时间或帧计数。选一致的时间域是适配层的事，例如 World 的游戏时间，它会随暂停停止，也跟随时间膨胀。

时间戳必须是有限的非负数，0 也有效。无效时返回 `InvalidTimestamp`，不覆盖已有的缓冲。

`UCadenceArcGraph::MaxBufferedInputAgeSeconds` 默认为 `0.0`，表示不限制缓冲时长。完成握手成功后：

| 条件 | 缓冲结果 | 之后的状态 |
| --- | --- | --- |
| 没有缓冲输入 | `NoAction / NoBufferedInput` | `Ready` |
| 完成时间无效，或早于缓冲输入 | `Rejected / InvalidCompletionTime` | `Ready` |
| 缓冲时长超过上限 | `NoAction / Expired` | `Ready` |
| 没超过上限，或没开启上限 | 解析缓冲的输入 | 成功时 `AwaitingStart`，否则 `Ready` |

时长正好等于上限时仍然有效。`InvalidCompletionTime` 和 `Expired` 都会结束旧动作，清空请求、窗口和缓冲，保留已提交的节点。握手失败时状态完全不变。

## 转移条件

图里的边可以带上下文条件、停顿区间和优先级，规则见[动作图](Graph.md#转移条件与优先级)。本节说明这些条件的输入从哪来、什么时候求值。

### 上下文的两个来源

| 来源 | 写法 | 适合 |
| --- | --- | --- |
| 事件上下文 | `FCadenceArcInputEvent::ContextTags` | 按键那一刻的事实，例如按着“前” |
| 持久上下文 | `SetContextTags` / `GetContextTags`（Blueprint 可调） | 持续一段时间的状态，例如空中、持剑姿态 |

求值时用两者的并集。宿主在状态变化时调用 `SetContextTags` 整体替换持久上下文。它不改变请求、缓冲或连招状态。`Reset` 和重新初始化都不会清空它，这些状态属于游戏世界，不属于连招。

### 停顿

停顿时长 = 输入的时间戳 − 最近一次成功的 `NotifyActionCompleted` 的时间戳。

执行器把 `Completed` 放在哪里，停顿就从哪里算起：放在硬直开始，停顿从招式判定结束算；放在蒙太奇播完，就从动画结束算。不同动作游戏两种做法都有，所以框架不替你选，请在执行器里写清楚。

- 动作执行中按下、进了缓冲的输入，停顿按 0 算。下限大于 0 的停顿边不会被缓冲输入选中；`[0, 0.3)` 这类包含 0 的区间可以。
- 按住的停顿从按下时刻算，不包含蓄力时长。在上一招里按下、完成后才松手，按 0 算。
- 还没有起点时（刚初始化、`Reset`、取消或打断之后），带停顿区间的边一律不满足。执行器拒绝请求（`NotifyActionRejected`）不清除起点。

### 求值时机

| 情况 | 事件上下文 | 持久上下文 | 停顿 |
| --- | --- | --- | --- |
| `Ready` 时提交输入 | 这次输入的 | 当时的 | 按公式算 |
| 缓冲的输入，在动作完成时解析 | 按下时的，随缓冲保存 | 完成时的 | 0 |
| 手动松手 | 松手事件的 | 松手时的 | 从按下算 |
| 自动松手（蓄满到上限） | 按下时的 | 自动松手时的 | 从按下算 |

授予按住资格时不看条件，只看这个 Tag 在当前节点有没有 `Released` 边。否则按下时没按方向，整次蓄力就会被拒绝。

### 条件相关的原因

| 原因 | 类别 | 含义 |
| --- | --- | --- |
| `ConditionNotMet` | `NoAction` | 有 Tag、阶段和时长都对上的边，但上下文或停顿条件全部不满足 |
| `AmbiguousTransition` | `Rejected` | 满足条件的边里，最高优先级有多条 |

`ConditionNotMet` 和 `NoMatchingTransition` 分开，是为了区分“图里根本没这个输入”和“有这个输入，但条件不对”。`AmbiguousTransition` 正常情况下会被图校验提前拦住，只有初始化后资产被改坏时才会出现。两者都不改变解析器状态。
