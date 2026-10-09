# 解析器：握手、缓冲与时间

[返回首页](../README.md)

`UCadenceArcResolver` 根据输入生成动作请求，并跟踪请求的生命周期。动作由宿主执行。本文介绍宿主如何接入解析器。

## 两阶段握手

解析器找到转移后，执行器还需要检查动作所需的资源、状态和执行时机。因此，解析输入和提交目标节点分为两个阶段：

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

请求 ID 无效、不匹配或当前状态不允许的回调都会被拒绝，解析器状态不变。请求 ID 是单调递增的正数，`Reset` 和重新初始化都不会重置它，以免旧回调影响后续请求。

### 请求内容

`FCadenceArcActionRequest` 包含 `RequestId`、`InputTag`（选中转移的输入）、`SourceActionTag`（实际选边的源节点，通常是解析时所在的节点，发生连招恢复时为入口）和 `TargetActionTag`（候选动作）。

## 执行器接入

UE 项目通常通过 [CadenceArc 组件](Component.md) 接入，组件会自动传入时间、逐帧推进并统一请求出口。本节介绍直接调用解析器的方式，适用于测试、回放和自定义时间来源的场景。

直接调用解析器时，一个最小的执行器如下：

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
| `Buffered` | 输入已存入缓冲槽，`Reason` 为 `None`。 | 空 |
| `NoAction` | 本次未生成请求，例如已有待确认请求、缓冲窗口关闭或没有匹配的转移。具体原因由 `Reason` 提供。 | 空 |
| `Rejected` | 调用被拒绝，例如参数无效、资格受保护或图存在歧义。具体原因由 `Reason` 提供。 | 空 |

默认构造的结果不表示成功。调用 `HasActionRequest()` 或 `HasNextActionRequest()` 判断是否有请求，避免仅凭请求 ID 判断。`UCadenceArcBlueprintLibrary` 为这些 getter 提供了 `BlueprintPure` 节点。

## 输入缓冲

缓冲只有一个槽位，新接受的输入覆盖先前的输入（Last Input Wins）。普通按下输入的处理方式取决于状态；存在按住资格时，还需要通过蓄力保护等检查：

| 状态 | 缓冲窗口 | 结果 |
| --- | --- | --- |
| `Ready` | 无关 | 立即解析。 |
| `AwaitingStart` | 关闭 | `NoAction / RequestPending`，状态不变。 |
| `Executing` | 打开 | 存进缓冲，返回 `Buffered / None`。 |
| `Executing` | 关闭 | `NoAction / BufferWindowClosed`，已存的输入不变。 |

执行器用 `OpenBufferWindow(RequestId)` 和 `CloseBufferWindow(RequestId)` 控制窗口。两者都要求当前执行中的请求 ID，因此旧请求的动画通知不会影响当前窗口。关闭窗口后，已有输入仍保留在缓冲槽中。

动作完成时，`NotifyActionCompleted` 返回 `FCadenceArcActionCompletionOutcome`：

- `GetHandshakeResult()`：完成回调是否通过握手校验；
- `GetBufferConsumption()` / `GetBufferConsumptionReason()`：缓冲的解析结果，握手成功后才有意义；
- `GetNextActionRequest()`：`HasNextActionRequest()` 为真时的下一个请求。

缓冲消费产生新请求时，解析器进入 `AwaitingStart`，等待 `NotifyActionStarted` 提交目标节点。输入过期、没有匹配边或条件不满足时，解析器保持 `Ready`。

同一个输入槽也用于保存按住资格，见[按住输入](HoldInput.md)。

## 时间与过期

时间全部由调用方提供，单位是秒：

```cpp
FCadenceArcSubmitOutcome SubmitInput(const FCadenceArcInputEvent& InputEvent);
FCadenceArcActionCompletionOutcome NotifyActionCompleted(int64 RequestId, double CompletionTimestampSeconds);
```

输入和完成的时间戳必须使用同一个不递减的时间域。解析器不读取 `UWorld`、平台时间或帧计数。适配层负责选择并统一时间来源，例如随暂停和时间膨胀变化的 World 游戏时间。

输入时间戳必须是有限的非负数，0 也有效。输入时间戳无效时返回 `InvalidTimestamp`，不覆盖已有缓冲。完成时间的错误处理见下表。

`UCadenceArcGraph::MaxBufferedInputAgeSeconds` 默认为 `0.0`，表示不限制缓冲时长。普通按下输入的缓冲在完成握手成功后按下表处理：

| 条件 | 缓冲结果 | 之后的状态 |
| --- | --- | --- |
| 没有缓冲输入 | `NoAction / NoBufferedInput` | `Ready` |
| 完成时间无效，或早于缓冲输入 | `Rejected / InvalidCompletionTime` | `Ready` |
| 缓冲时长超过上限 | `NoAction / Expired` | `Ready` |
| 未超过上限，或未设置上限 | 解析缓冲输入 | 产生请求时 `AwaitingStart`，否则 `Ready` |

缓冲时长等于上限时仍然有效。对于普通按下输入的缓冲，`InvalidCompletionTime` 和 `Expired` 都属于握手成功后的消费结果：旧动作结束，请求、窗口和缓冲被清空，已提交的节点保留。

存在待松手资格或松手缓冲时，无效完成时间会直接拒绝握手，返回 `ECadenceArcHandshakeResult::InvalidCompletionTime`，状态保持不变。待松手资格在正常完成后仍然保留，消费结果为 `NoAction / WaitingForRelease`。

## 连招恢复

图上的 `ComboResetSeconds` 和 `bFallbackToEntryOnNoMatch` 决定连招何时回到入口，字段说明见[动作图](Graph.md#连招恢复)。两者都只改变一次输入的选边源，不提前修改已提交的节点。

### 超时重置

`Ready` 状态下收到新输入时，如果距上次成功完成回调的停顿达到 `ComboResetSeconds`，这次输入从入口选边。停顿正好等于重置时间也算到期。以下情况不会重置：

- `ComboResetSeconds` 为 0；
- 还没有停顿起点，例如刚初始化、`Reset`、取消或打断之后；
- 动作执行中（`AwaitingStart` 或 `Executing`），不计时；
- 缓冲的输入。它在完成回调中消费，停顿为 0。

重置是惰性的：只在下一次输入时判断，到期本身不改变任何状态。重置产生的请求，`SourceActionTag` 为入口，执行器确认开始后才提交目标节点。在那之前，`GetCurrentActionTag` 仍返回旧节点；执行器拒绝请求时，已提交节点也保持不变。

按住输入在 `BeginInputHold` 时判断是否重置，并把判断结果和转移副本一起冻结在资格中。按下时未到期、松手时才超过重置时间的按住，仍从按下时的节点解析。

### 回退入口

开启 `bFallbackToEntryOnNoMatch` 后，非入口节点上的选边结果为 `NoMatchingTransition` 时，同一次输入改从入口再选一次边。回退只发生一次，只针对 `NoMatchingTransition`：`ConditionNotMet`、`AmbiguousTransition` 和图配置错误都不回退，避免借入口的分支绕过条件。

回退适用于三处：`Ready` 时直接提交的输入、完成回调中消费的缓冲，以及按住申请。按住申请时，如果当前节点没有这个输入的 `Released` 转移，就使用入口节点的 `Released` 转移。CadenceArc 组件的 `HoldIfAvailable` 按同一规则判断是否申请按住。

### 查询

以下查询只读取状态，不推进时间，也不提交节点：

| 接口 | 返回 |
| --- | --- |
| `GetEffectiveActionTag(Now)` | 下一次新输入实际的选边源。到期后为入口；存在待松手资格时，为资格冻结的源节点 |
| `GetComboResetRemainingSeconds(Now)` | 距重置的剩余秒数；0 表示已到期；-1 表示当前不计时（未启用、没有停顿起点、动作执行中或正在按住） |

CadenceArc 组件提供同名的无参版本，使用组件的时间来源。

### 与停顿条件的关系

停顿条件和超时重置使用同一个停顿起点。重置时间以图上的全局值为准，运行时不会根据停顿区间推迟重置，也不会截断区间；冲突由校验报告，规则见[动作图](Graph.md#校验)。

## 转移条件

转移可以配置上下文条件、停顿区间和优先级，规则见[动作图](Graph.md#转移条件与优先级)。本节介绍条件数据的来源和求值时机。

### 上下文的两个来源

| 来源 | 写法 | 适合 |
| --- | --- | --- |
| 事件上下文 | `FCadenceArcInputEvent::ContextTags` | 按键那一刻的事实，例如按着“前” |
| 持久上下文 | `SetContextTags` / `GetContextTags`（Blueprint 可调） | 持续一段时间的状态，例如空中、持剑姿态 |

求值时用两者的并集。宿主在状态变化时调用 `SetContextTags` 整体替换持久上下文。它不改变请求、缓冲或连招状态。`Reset` 和重新初始化都不会清空它，这些状态属于游戏世界，不属于连招。

### 停顿

`Ready` 状态下，普通按下输入的停顿时长等于输入时间戳减去最近一次成功完成回调记录的有效时间戳。缓冲和按住输入采用下面的规则。

停顿起点由执行器发送 `NotifyActionCompleted` 的时机决定。例如，执行器可以在动作判定结束或蒙太奇播放结束时发送完成回调。请在执行器中明确这一约定。

- 缓冲输入在动作完成时解析，停顿时长为 0。下限大于 0 的停顿区间不会匹配；`[0, 0.3)` 这类包含 0 的区间可以匹配。
- 按住输入在 `Ready` 下解析时，停顿时长为 `Max(0, 按下时间 − 最近完成时间)`，不包含蓄力时长。在前一个动作执行期间按下、完成后松手的输入，停顿时长为 0。
- 没有有效起点时（刚初始化、`Reset`、取消或打断之后），带停顿区间的边不满足条件。执行器拒绝请求（`NotifyActionRejected`）不清除起点。

### 求值时机

| 情况 | 事件上下文 | 持久上下文 | 停顿 |
| --- | --- | --- | --- |
| `Ready` 时提交普通按下输入 | 本次输入事件的上下文 | 解析时的值 | 输入时间减去最近完成时间 |
| 普通按下输入的缓冲 | 按下事件的上下文，随缓冲保存 | 完成回调消费缓冲时的值 | 0 |
| 手动松手，在 `Ready` 下立即解析 | 松手事件的上下文 | 解析时的值 | 按下时间减去最近完成时间，最小为 0 |
| 自动松手，在 `Ready` 下立即解析 | 按下事件的上下文 | 实际处理自动松手时的值 | 按下时间减去最近完成时间，最小为 0 |
| 手动或自动松手产生的缓冲 | 手动松手保留松手上下文；自动松手保留按下上下文 | 完成回调消费缓冲时的值 | 0 |

申请按住资格时，解析器检查源节点配置，以及当前 Tag 是否有 `Released` 边，但不求值上下文和停顿条件。因此，玩家可以先按住攻击键，再在松手前改变方向。

### 条件相关的原因

| 原因 | 类别 | 含义 |
| --- | --- | --- |
| `ConditionNotMet` | `NoAction` | 有输入 Tag、阶段和时长均匹配的边，但上下文或停顿条件全部不满足 |
| `AmbiguousTransition` | `Rejected` | 满足条件的候选边中，有多条边具有最高优先级 |

`NoMatchingTransition` 表示没有边匹配输入 Tag、阶段和按住时长；`ConditionNotMet` 表示通过这些检查的边均未满足上下文或停顿条件。图校验会提前拒绝同优先级的重叠转移，运行时的 `AmbiguousTransition` 检查用于防护初始化后的配置修改等情况。

这些选边失败都不会生成请求或提交目标节点。状态变化取决于调用入口：`Ready` 下直接提交普通输入时保留已有状态；有效松手仍会结束按住资格；完成回调仍会结束旧动作并消费缓冲。
