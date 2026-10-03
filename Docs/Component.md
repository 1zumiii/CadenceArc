# CadenceArc 组件

[返回首页](../README.md)

`UCadenceArcComponent` 是 UE 项目接入 CadenceArc 的标准方式。组件持有一个解析器，并处理所有项目都需要以相同方式完成的工作，开发者只需提供输入、上下文和执行器回调。本文介绍组件的职责、接入步骤和接口。

## 组件负责的工作

| 工作 | 组件的处理方式 |
| --- | --- |
| 时间戳 | 所有调用自动使用 World 游戏时间，开发者不需要传入时间 |
| 逐帧推进 | 在 Tick 中推进按住资格的时间，处理蓄力阶段和自动释放 |
| 按键配对 | 内部的 `FCadenceArcInputTracker` 配对按下和松开，生成 Token 和按住时长 |
| 请求出口 | 直接提交、松手、自动释放和完成时消费缓冲产生的请求，统一从 `OnActionRequested` 发出 |
| 调用顺序 | 每个带时间的调用都先推进按住时间，避免到期的自动释放导致 `InputTimeAdvanceRequired` |
| 清理 | `EndPlay` 时取消所有按键和按住资格，不产生松手 |

以下内容仍由开发者提供，因为它们取决于具体游戏：

- 按键的输入方式（`PressOnly` 或 `HoldRelease`）；
- 事件上下文，例如按键时的方向；
- 持久上下文，例如空中、持剑姿态；
- 执行器的开始、拒绝、缓冲窗口、完成和打断回调。

## 接入步骤

1. 在角色或控制器上添加 `UCadenceArcComponent`，并在 `Graph` 属性中指定动作图。组件在 `BeginPlay` 时初始化解析器。
2. 在输入绑定中调用 `PressInput`、`ReleaseInput` 和 `CancelInput`。
3. 执行器订阅 `OnActionRequested`。收到请求后，能执行时调用 `NotifyActionStarted`，否则调用 `NotifyActionRejected`。
4. 执行器在动作的对应时刻调用 `OpenBufferWindow`、`CloseBufferWindow` 和 `NotifyActionCompleted`；受击或取消时调用 `NotifyActionInterrupted` 或 `NotifyActionCancelled`。

```cpp
// 输入绑定
CadenceArcComponent->PressInput(InputTag, ECadenceArcInputMode::HoldRelease, MakeInputContextTags());
CadenceArcComponent->ReleaseInput(InputTag, MakeInputContextTags());

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

## 接口

| 分类 | 接口 | 说明 |
| --- | --- | --- |
| 配置 | `Graph` | 动作图，`BeginPlay` 时用于初始化 |
| 配置 | `InitializeResolver(Graph)` | 运行中更换动作图；成功后清空当前的按键配对 |
| 输入 | `PressInput(Tag, Mode, ContextTags)` | 按键按下 |
| 输入 | `ReleaseInput(Tag, ContextTags)` | 按键松开；只在按住资格仍有效时提交松手 |
| 输入 | `CancelInput(Tag)` / `CancelAllInputs()` | 取消按住资格和按键配对，不产生松手 |
| 输入 | `SetContextTags(Tags)` | 替换持久上下文 |
| 执行器 | `NotifyActionStarted` / `NotifyActionRejected` | 确认或拒绝请求 |
| 执行器 | `OpenBufferWindow` / `CloseBufferWindow` | 开关缓冲窗口 |
| 执行器 | `NotifyActionCompleted` | 动作完成；返回完成结果，下一个请求从 `OnActionRequested` 发出 |
| 执行器 | `NotifyActionCancelled` / `NotifyActionInterrupted` | 结束动作并回到入口 |
| 执行器 | `ResetCombo()` | 在 `Ready` 状态下回到入口动作 |
| 输出 | `OnActionRequested` | 所有动作请求的统一出口 |
| 输出 | `OnHoldStageChanged` | 按住资格进入蓄力或蓄满，携带输入 Tag 和阈值时间 |
| 查询 | `GetResolver()` | 底层解析器，用于查询状态或调试 |

`OnActionRequestedNative` 和 `OnHoldStageChangedNative` 是对应的 C++ 多播委托，与蓝图委托同时发出，便于 C++ 执行器绑定 lambda。

## 时间来源

组件默认使用 `GetWorld()->GetTimeSeconds()`。这个时间随游戏暂停而停止，并跟随全局时间膨胀。以下情况需要替换时间来源：

- 角色设置了 `CustomTimeDilation`，需要按角色自身的时间流速计算；
- 联网游戏需要使用同步后的服务器时间；
- 回放系统需要使用录制的时间。

C++ 中调用 `SetTimeSource` 传入新的时间函数，传入空函数恢复默认。新的时间来源必须与已提交的时间戳处于同一时间域，并且不递减。

## 直接使用解析器

自动化测试、回放和完全自定义时间的场景，可以不使用组件，直接调用 `UCadenceArcResolver`。这时宿主需要自行传入时间戳、逐帧调用 `AdvanceInputTime`、生成按住 Token，并处理三个请求来源，详见[解析器](Resolver.md)和[按住输入](HoldInput.md)。
