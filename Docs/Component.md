# CadenceArc 组件

[返回首页](../README.md)

`UCadenceArcComponent` 是 UE 项目接入 CadenceArc 的标准方式。组件持有一个解析器，并处理所有项目都需要以相同方式完成的工作；游戏只需配置一次输入方式和上下文来源，并实现执行器回调。本文介绍组件的职责、接入步骤和接口。

## 组件负责的工作

| 工作 | 组件的处理方式 |
| --- | --- |
| 时间戳 | 所有调用自动使用 World 游戏时间，开发者不需要传入时间 |
| 逐帧推进 | 在 Tick 中推进按住资格的时间，处理蓄力阶段和自动释放 |
| 按键配对 | 内部的 `FCadenceArcInputTracker` 配对按下和松开，生成 Token 和按住时长 |
| 输入方式 | 按 `InputModes` 配置决定按下立即提交，还是等待松开 |
| 事件上下文 | 按下和手动松开时，从上下文提供者采集一次 |
| 请求出口 | 直接提交、松手、自动释放和完成时消费缓冲产生的请求，统一从 `OnActionRequested` 发出 |
| 按住状态通知 | 蓄力阶段变化发出 `OnHoldStageChanged`，资格结束发出 `OnHoldEnded` |
| 调用顺序 | 每个带时间的调用都先推进按住时间，避免到期的自动释放导致 `InputTimeAdvanceRequired` |
| 清理 | `EndPlay` 时取消所有按键和按住资格，不产生松手 |

以下内容由游戏决定，组件不替开发者选择：

- 每个输入 Tag 的输入方式，配置一次即可；
- 事件上下文的计算规则，例如“前”以镜头还是锁定目标为参照；
- 持久上下文，例如空中、持剑姿态；
- 执行器的开始、拒绝、缓冲窗口、完成和打断回调。

## 接入步骤

1. 在角色上添加 `UCadenceArcComponent`，在 `Graph` 属性中指定动作图，在 `InputModes` 中为需要按住的输入 Tag 配置 `HoldRelease`。组件在 `BeginPlay` 时初始化解析器。
2. 让角色实现 `ICadenceArcInputContextProvider::CollectInputContext`，返回按键时的方向等事件上下文。不需要事件上下文时可以跳过这一步。
3. 在输入绑定中调用 `PressInput(Tag)`、`ReleaseInput(Tag)` 和 `CancelInput(Tag)`。
4. 执行器订阅 `OnActionRequested`。收到请求后，能执行时调用 `NotifyActionStarted`，否则调用 `NotifyActionRejected`。
5. 执行器在动作的对应时刻调用 `OpenBufferWindow`、`CloseBufferWindow` 和 `NotifyActionCompleted`；受击或取消时调用 `NotifyActionInterrupted` 或 `NotifyActionCancelled`。
6. 表现层按需订阅 `OnHoldStageChanged` 和 `OnHoldEnded`，播放和清理蓄力表现。

```cpp
// 输入绑定：输入方式来自 InputModes，事件上下文来自 CollectInputContext
CadenceArcComponent->PressInput(InputTag);
CadenceArcComponent->ReleaseInput(InputTag);

// 执行器：所有请求都从这里进入
CadenceArcComponent->OnActionRequested.AddDynamic(this, &UMyExecutor::HandleActionRequested);

void UMyExecutor::HandleActionRequested(const FCadenceArcActionRequest& Request)
{
    if (!CanPlay(Request.TargetActionTag))
    {
        CadenceArcComponent->NotifyActionRejected(Request.RequestId);
        return;
    }
    CadenceArcComponent->NotifyActionStarted(Request.RequestId);
    PlayAction(Request); // 动作结束时调用 NotifyActionCompleted(Request.RequestId)
}
```

`NotifyActionCompleted` 的调用时刻也是停顿时长的起点，请在执行器中明确这一约定，详见[解析器](Resolver.md#停顿)。完成时消费缓冲产生的下一个请求同样从 `OnActionRequested` 发出，执行器不需要单独处理完成回调的返回值。

## 输入方式

`InputModes` 为每个输入 Tag 指定输入方式，未配置的 Tag 按 `PressOnly` 处理。也可以在运行时调用 `SetInputMode` 写入配置，例如从项目自己的输入配置资产中读取。

| 输入方式 | 按下时 | 松开时 |
| --- | --- | --- |
| `PressOnly` | 立即提交输入 | 只结束按键配对 |
| `HoldRelease`，当前节点有该 Tag 的 `Released` 转移 | 申请按住资格 | 按按住时长解析 |
| `HoldRelease`，当前节点没有该 Tag 的 `Released` 转移 | 与 `PressOnly` 相同，立即提交 | 只结束按键配对 |

第三行保证同一个按键在没有蓄力分支的动作中仍然按下即响应。例如 Heavy 配置为 `HoldRelease` 时，在有蓄力档位的节点上等待松开，在只有普通转移的节点上按下立即出招。

同一节点上，同一个 `HoldRelease` 输入同时配置了 `Pressed` 和 `Released` 转移时，按住优先，`Pressed` 转移不会被选中。

## 事件上下文

事件上下文描述输入那一刻的事实，例如按键时是否按住“前”。组件按以下顺序确定事件上下文：

1. 调用 `PressInputWithContext` 或 `ReleaseInputWithContext` 时，使用调用方提供的完整快照，不调用提供者。传入空容器表示没有上下文。
2. 调用 `PressInput` 或 `ReleaseInput` 时，从上下文提供者采集。C++ 中通过 `SetContextProviderFunction` 设置的函数优先；否则使用 `SetContextProvider` 指定的对象；都没有设置时，使用所属 Actor（如果它实现了 `ICadenceArcInputContextProvider`）。

采集只发生在按下和手动松开时。缓冲消费不会重新采集；自动松手沿用按下时采集的上下文；持久上下文仍在实际解析时读取，规则见[解析器](Resolver.md#求值时机)。

持久上下文通过 `SetContextTags` 设置，可以在解析器初始化之前调用，组件会保存并在初始化时生效。

## 输入处理结果

`PressInput` 和 `ReleaseInput` 返回 `FCadenceArcInputResult`，调用方可以忽略。它适合用于播放拒绝提示、设置输入 UI 和排查“按了没有反应”的问题。动作请求不在返回值中，只通过 `OnActionRequested` 发出，避免同一个请求被执行两次。

| `Status` | 含义 |
| --- | --- |
| `RequestProduced` | 产生了动作请求，已通过 `OnActionRequested` 发出 |
| `Buffered` | 动作执行中，输入已存入缓冲 |
| `HoldGranted` | 按住资格已授予，松开或自动释放时解析 |
| `KeyReleased` | 按键配对已结束，没有需要提交的松手 |
| `NoAction` | 输入已处理，但没有产生动作，例如没有匹配的转移、条件不满足、缓冲窗口关闭 |
| `Rejected` | 输入被拒绝，例如蓄力保护、时间无效 |
| `AlreadyPressed` / `NotPressed` | 重复按下，或松开了没有记录按下的键 |
| `NotInitialized` | 解析器尚未初始化 |

`NoAction` 和 `Rejected` 时，`Reason` 给出解析器的具体原因。`IsAccepted()` 在 `RequestProduced`、`Buffered` 和 `HoldGranted` 时为真。

## 按住结束通知

`OnHoldEnded(InputTag, Reason)` 在按住资格结束时发出，每份资格只发一次，用于清理蓄力特效、音效和 UI。

| `Reason` | 触发情况 |
| --- | --- |
| `Released` | 手动松开 |
| `AutoReleased` | 达到保持上限后自动释放 |
| `Cancelled` | `CancelInput`、`CancelAllInputs` 或 `EndPlay` |
| `Replaced` | 被另一个被接受的输入替换 |
| `Cleared` | 连招重置、重新初始化，或动作开始、取消、打断时清空 |

动作正常完成不会结束待松手的资格，蓄力会继续计时。组件通过对比调用前后的按住快照检测资格结束；绕过组件直接调用解析器时，组件不会发出这个通知。

## 执行器约定

- **一个组件只能有一个执行器。** 执行器订阅 `OnActionRequested`，负责确认并执行请求。UI、音效等系统不应订阅这个事件来执行动作；多个订阅者都执行时，同一个请求会播放两次，解析器会以握手结果拒绝第二次确认。
- **取消输入不等于打断动作。** `CancelInput` 和 `CancelAllInputs` 只清理按键配对和按住资格，不会停止正在执行的动作。失去控制或窗口失焦时是否停止动作，由游戏决定；需要停止时，由执行器调用 `NotifyActionInterrupted`。

## 接口

| 分类 | 接口 | 说明 |
| --- | --- | --- |
| 配置 | `Graph`、`InputModes` | 动作图和输入方式 |
| 配置 | `InitializeResolver(Graph)` | 运行中更换动作图；成功后清空按键配对，持久上下文保留 |
| 配置 | `SetInputMode(Tag, Mode)` / `GetInputMode(Tag)` | 运行时读写输入方式 |
| 输入 | `PressInput(Tag)` / `ReleaseInput(Tag)` | 按下和松开，事件上下文从提供者采集 |
| 输入 | `PressInputWithContext` / `ReleaseInputWithContext` | 使用调用方提供的完整上下文快照 |
| 输入 | `CancelInput(Tag)` / `CancelAllInputs()` | 取消按住资格和按键配对，不产生松手 |
| 输入 | `SetContextTags(Tags)` | 替换持久上下文，初始化前调用也有效 |
| 输入 | `SetContextProvider(Object)` / `SetContextProviderFunction` | 指定事件上下文的提供者 |
| 执行器 | `NotifyActionStarted` / `NotifyActionRejected` | 确认或拒绝请求 |
| 执行器 | `OpenBufferWindow` / `CloseBufferWindow` | 开关缓冲窗口 |
| 执行器 | `NotifyActionCompleted` | 动作完成；下一个请求从 `OnActionRequested` 发出 |
| 执行器 | `NotifyActionCancelled` / `NotifyActionInterrupted` | 结束动作并回到入口 |
| 执行器 | `ResetCombo()` | 在 `Ready` 状态下回到入口动作 |
| 输出 | `OnActionRequested` | 所有动作请求的统一出口 |
| 输出 | `OnHoldStageChanged` | 按住资格进入蓄力或蓄满，携带输入 Tag 和阈值时间 |
| 输出 | `OnHoldEnded` | 按住资格结束，携带输入 Tag 和结束原因 |
| 查询 | `GetResolver()` | 底层解析器，用于查询状态或调试 |

三个输出事件都有对应的 C++ 多播委托（`OnActionRequestedNative`、`OnHoldStageChangedNative` 和 `OnHoldEndedNative`），与蓝图委托同时发出，便于 C++ 代码绑定 lambda。

## 时间来源

组件默认使用 `GetWorld()->GetTimeSeconds()`。这个时间随游戏暂停而停止，并跟随全局时间膨胀。以下情况需要替换时间来源：

- 角色设置了 `CustomTimeDilation`，需要按角色自身的时间流速计算；
- 联网游戏需要使用同步后的服务器时间；
- 回放系统需要使用录制的时间。

C++ 中调用 `SetTimeSource` 传入新的时间函数，传入空函数恢复默认。新的时间来源必须与已提交的时间戳处于同一时间域，并且不递减。

## 直接使用解析器

自动化测试、回放和完全自定义时间的场景，可以不使用组件，直接调用 `UCadenceArcResolver`。这时宿主需要自行传入时间戳、逐帧调用 `AdvanceInputTime`、生成按住 Token，并处理三个请求来源，详见[解析器](Resolver.md)和[按住输入](HoldInput.md)。
