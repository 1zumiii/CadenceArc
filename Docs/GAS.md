# GAS 执行器

[返回首页](../README.md)

`CadenceArcGAS` 是可选的执行器模块，用 Gameplay Ability System 执行 CadenceArc 的动作请求。动作图中的动作 Tag 直接作为 Ability 的资产 Tag，开发者只需授予 Ability，并在蒙太奇中标出缓冲窗口，不需要再写执行器代码。核心模块 `CadenceArc` 不依赖这个模块，也不依赖 GAS。

## 组成

| 类型 | 作用 |
| --- | --- |
| `UCadenceArcAbilityExecutorComponent` | 执行器组件：收到请求后激活对应的 Ability，并把激活和结束报告给 CadenceArc 组件 |
| `UAnimNotifyState_CadenceArcBufferWindow` | 蒙太奇中的通知状态，显示名为 CadenceArc Buffer Window，开始时打开缓冲窗口，结束时关闭 |

## 接入步骤

1. 在模块的 `Build.cs` 中添加依赖 `"CadenceArcGAS"`。只使用蓝图时跳过这一步。
2. 为每个动作创建一个 Ability，在 `AssetTags` 中加入动作图里对应的动作 Tag，例如 `Action.Light1`。
3. 按项目原有的方式授予这些 Ability，例如在初始化 Ability System 组件时调用 `GiveAbility`。执行器不负责授予。
4. 在角色上添加 `UCadenceArcAbilityExecutorComponent`。它在 `BeginPlay` 时绑定同一个 Actor 上的 CadenceArc 组件。
5. 在 Ability 播放的蒙太奇中添加 CadenceArc Buffer Window 通知状态，覆盖允许输入下一招的区间。

执行器每次收到请求时，都通过 `IAbilitySystemInterface` 或组件查找重新获取 Ability System 组件。因此，Ability System 组件放在 PlayerState 上、被控制后才初始化的项目也可以直接使用。需要指定其他组件时，调用 `SetAbilitySystemComponent`。

## 请求与 Ability 的对应关系

| CadenceArc | GAS |
| --- | --- |
| 收到动作请求 | 在已授予的 Ability 中查找 `AssetTags` 精确包含 `TargetActionTag` 的一个，调用 `TryActivateAbility` |
| 激活成功 | `NotifyActionStarted` |
| 激活失败，例如冷却、消耗不足或被 Tag 阻挡 | `NotifyActionRejected`，保留原来的动作节点 |
| 没有匹配的 Ability，或匹配到多个 | `NotifyActionRejected`，并输出警告 |
| Ability 正常结束 | `NotifyActionCompleted`，同时开始计算停顿时长，并消费缓冲 |
| Ability 被取消 | `NotifyActionInterrupted`，回到入口动作 |

GAS 的激活条件因此直接成为执行器的拒绝理由。冷却中按下攻击键，请求会被拒绝，连招停在原来的节点，Arc History 中会记录这次拒绝。

Ability 在激活过程中就结束时（例如瞬发技能），执行器先报告开始，再报告结束，保证握手顺序正确。

完成回调消费缓冲产生的下一个请求，会在上一个 Ability 的结束回调中同步激活下一个 Ability。这时上一个 Ability 的任务、计时器和激活 Tag 都已经清理完毕；下一招是同一个 Ability 时，也能在结束回调中重新激活。

## 缓冲窗口

蒙太奇中的 CadenceArc Buffer Window 开始时，执行器记下当前请求的编号，再打开窗口；结束时用同一个编号关闭窗口。上一个动作的蒙太奇如果在下一个动作开始后才结束这个通知，它使用的是旧编号，解析器按过期回调拒绝，不会关闭新动作的窗口。

不使用蒙太奇的 Ability，可以在合适的时机调用执行器的 `OpenBufferWindow` 和 `CloseBufferWindow`。

在动画编辑器中预览蒙太奇时，预览角色上没有执行器，这个通知不做任何事。

## 注意事项

- **结束时机就是完成时机。** `NotifyActionCompleted` 在 Ability 结束时发出，这也是停顿时长的起点。希望在蒙太奇淡出时就允许衔接下一招，应让 Ability 在淡出时结束，而不是等蒙太奇完全播放完。
- **取消即打断。** 受击时取消正在执行的 Ability，连招会回到入口动作。
- **一个执行器。** 使用本组件时，不要再让其他系统处理 `OnActionRequested`，否则同一个请求会被执行两次。
- **联网未验证。** 目前只在单机环境中测试。在客户端上，`TryActivateAbility` 对需要服务器激活的 Ability 的返回值，与实际激活结果可能不一致，联网项目需要自行验证。
- **插件依赖。** 本插件在描述文件中声明了对 Gameplay Abilities 插件的依赖，启用 CadenceArc 时会一并启用它。

## 接口

| 接口 | 说明 |
| --- | --- |
| `OpenBufferWindow()` / `CloseBufferWindow()` | 打开或关闭当前动作的缓冲窗口，供不使用蒙太奇的 Ability 调用 |
| `GetActiveRequestId()` | 正在执行的请求编号，没有时为 0 |
| `SetAbilitySystemComponent` / `GetAbilitySystemComponent` | 指定或查询使用的 Ability System 组件 |
| `SetCadenceArcComponent` | 指定要执行的 CadenceArc 组件，默认使用所属 Actor 上的组件 |
