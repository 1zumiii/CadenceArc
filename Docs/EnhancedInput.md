# Enhanced Input 适配

[返回首页](../README.md)

`CadenceArcEnhancedInput` 是可选的适配模块，负责把 Enhanced Input 的 Input Action 绑定到 [CadenceArc 组件](Component.md)。使用 Enhanced Input 的项目只需配置 Input Action 与输入 Tag 的对应关系，不再手写按下、松开和取消的转发代码。核心模块 `CadenceArc` 不依赖这个模块，也不依赖 Enhanced Input。

## 组成

| 类型 | 作用 |
| --- | --- |
| `UCadenceArcInputActionSet` | 数据资产，列出每个 Input Action 对应的输入 Tag 和输入方式 |
| `FCadenceArcInputActionBinding` | 一条映射：`InputAction`、`InputTag`、`InputMode` |
| `UCadenceArcInputBinderComponent` | 把映射绑定到 `UEnhancedInputComponent`，并转发给 `UCadenceArcComponent` |

## 接入步骤

1. 在模块的 `Build.cs` 中添加依赖 `"CadenceArcEnhancedInput"`。只使用蓝图时跳过这一步。
2. 新建 `CadenceArcInputActionSet` 数据资产，为每个连招输入添加一条映射。项目已有输入配置资产时，可以让它继承 `UCadenceArcInputActionSet`。
3. 在角色上添加 `UCadenceArcInputBinderComponent`，并在 `ActionSet` 属性中指定上一步的资产。
4. 角色被玩家控制后，适配组件自动绑定，不需要编写代码。

自动绑定发生在 Pawn 的 Restart 之后。此时 Pawn 已经在 `PawnClientRestart` 中创建输入组件并调用了 `SetupPlayerInputComponent`。组件在 `BeginPlay` 时也会检查一次，以覆盖 `BeginPlay` 之前就已被控制的 Pawn。没有玩家控制的 Pawn（例如 AI）没有输入组件，不会绑定。

需要在代码中决定使用哪份映射时，可以关闭 `bAutoBind`，或在 `SetupPlayerInputComponent` 中手动绑定。手动绑定后，同一个输入组件上的自动绑定会跳过：

```cpp
void AMyCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    UEnhancedInputComponent* Input = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);
    CadenceArcInputBinder->BindInputActions(Input, MyActionSet); // 第二个参数替换 ActionSet
}
```

蓝图中的完整用法见[蓝图接入](Blueprint.md)。

Mapping Context 的添加和优先级仍由项目管理，适配模块不处理。

## 绑定规则

每条有效映射绑定三个触发事件：

| Enhanced Input 事件 | CadenceArc 组件调用 |
| --- | --- |
| `Started` | `PressInput(InputTag)` |
| `Completed` | `ReleaseInput(InputTag)` |
| `Canceled` | `CancelInput(InputTag)`，不当作松开，不产生松手请求 |

绑定时，映射中的输入方式写入 CadenceArc 组件，与调用 `SetInputMode` 相同。输入方式只需在 ActionSet 中维护，不必在组件的 `InputModes` 中重复配置。

缺少 Input Action 或输入 Tag 的条目会被跳过。同一个 Input Action 或同一个输入 Tag 出现多次时，只绑定第一条。两种情况都会输出警告，数据校验也会把它们报告为错误。`BindInputActions` 返回成功绑定的条目数。

目标组件默认是所属 Actor 上的 `UCadenceArcComponent`，可以用 `SetTargetComponent` 指定其他组件。

## 失去输入时的清理

以下情况收不到按住中按键的 `Completed`，适配组件会取消已绑定且仍处于按下状态的输入：

- 再次调用 `BindInputActions`，例如重新控制 Pawn 后绑定新的输入组件；
- 调用 `UnbindInputActions`，或组件 `EndPlay`；
- Pawn 的控制器发生变化。取消控制时，引擎先销毁 Pawn 的输入组件，再通知控制器变化。

打开菜单、切换输入模式等其他收不到松开的情况，由项目调用 `CancelBoundInputs`。这个函数只取消本组件绑定的输入，其他来源提交到同一个 CadenceArc 组件的输入不受影响。

视口失去焦点时，引擎是否清空按下的按键，取决于输入设置 `bShouldFlushPressedKeysOnViewportFocusLost` 和 PlayerController 的设置。按键被清空后，在下面推荐的触发器设置下，Enhanced Input 预计为按住中的键发出 `Completed`，即按松开处理。这一行为来自引擎源码，尚未在 PIE 中验证。项目希望失焦时取消输入，可以在失焦回调中调用 `CancelBoundInputs`。

## 触发器设置

CadenceArc 依靠 `Started` 和 `Completed` 判断物理按下和松开，并由此计算按住时长。Input Action 建议不加触发器，或只加 `Down` 触发器。其他触发器会改变这两个事件的含义：

- `Pressed` 在按下后的下一帧发出 `Completed`，按住的键会被立即当作松开；
- `Hold`、`Tap`、`Pulse` 等计时触发器在条件未满足时松开，会发出 `Canceled`，这次输入会被取消。

按住时长和蓄力档位应在动作图中配置，详见[按住输入](HoldInput.md)。绑定时，如果 Input Action 上有 `Down` 以外的触发器，适配组件会输出警告。Mapping Context 中为单个按键添加的触发器无法在绑定时检查，需要项目自行避免。

## 接口

| 接口 | 说明 |
| --- | --- |
| `ActionSet` | 要绑定的映射资产 |
| `bAutoBind` | 默认开启。所属 Pawn 每次 Restart 后自动绑定 |
| `BindInputActions(InputComponent = nullptr, ActionSet = nullptr)` | 解除上一次绑定后重新绑定，返回绑定的条目数。输入组件为空时使用所属 Actor 的输入组件 |
| `UnbindInputActions()` | 解除绑定，并取消仍处于按下状态的已绑定输入 |
| `CancelBoundInputs()` | 只取消输入，保留绑定 |
| `SetTargetComponent` / `GetTargetComponent` | 指定或查询转发目标 |
| `IsBound()` | 当前是否有绑定 |
