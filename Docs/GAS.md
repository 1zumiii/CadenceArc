# GAS 执行器

[返回首页](../README.md)

`CadenceArcGAS` 是可选的执行器模块，用 Gameplay Ability System 执行 CadenceArc 的动作请求。它通过动作 Tag 查找 Ability，自动报告动作的开始、结束和取消。项目仍需初始化 Ability System 组件、实现并授予 Ability，以及设置缓冲窗口；无需再编写这些生命周期回调的转发代码。核心模块 `CadenceArc` 不依赖这个模块，也不依赖 GAS。

## 组成

| 类型 | 作用 |
| --- | --- |
| `UCadenceArcAbilityExecutorComponent` | 执行器组件：收到请求后激活对应的 Ability，并把激活和结束报告给 CadenceArc 组件 |
| `UAnimNotifyState_CadenceArcBufferWindow` | 蒙太奇中的通知状态，显示名为 CadenceArc Buffer Window，开始时打开缓冲窗口，结束时关闭 |

## 接入步骤

1. 在模块的 `Build.cs` 中添加依赖 `"CadenceArcGAS"`。只使用蓝图时跳过这一步。
2. 为每个动作创建一个 Ability，在 `AssetTags` 中加入动作图里对应的动作 Tag，例如 `Action.Light1`。
3. 按项目原有的方式初始化 Ability System 组件（ASC），并授予这些 Ability，例如调用 `InitAbilityActorInfo` 和 `GiveAbility`。执行器不负责初始化或授予。动作结束时，Ability 必须调用 `EndAbility`，执行器才能报告完成。
4. 在角色上添加 `UCadenceArcAbilityExecutorComponent`。它在 `BeginPlay` 时绑定同一个 Actor 上的 CadenceArc 组件。
5. 在 Ability 播放的蒙太奇中添加 CadenceArc Buffer Window 通知状态，覆盖允许输入下一招的区间。

执行器每次收到请求时都会重新获取 ASC。它优先使用 `SetAbilitySystemComponent` 显式指定的组件，否则通过所属 Actor 的 `IAbilitySystemInterface` 或组件查找获取。

ASC 放在 PlayerState 上时，需要让角色实现该接口并返回 PlayerState 上的 ASC，或显式指定。执行器不会自动查找 PlayerState。ASC 可以在角色被控制后再初始化，但必须在处理动作请求前准备好。

## 请求与 Ability 的对应关系

| CadenceArc | GAS |
| --- | --- |
| 收到动作请求 | 在已授予的 Ability 中查找 `AssetTags` 精确包含 `TargetActionTag` 的一个，调用 `TryActivateAbility` |
| 激活成功 | `NotifyActionStarted` |
| 激活失败，例如冷却、消耗不足或被 Tag 阻挡 | `NotifyActionRejected`，保留原来的动作节点 |
| 没有匹配的 Ability，或匹配到多个 | `NotifyActionRejected`，并输出警告 |
| Ability 正常结束 | `NotifyActionCompleted`，同时开始计算停顿时长，并消费缓冲 |
| Ability 被取消 | `NotifyActionInterrupted`，回到入口动作 |

是否允许激活由 GAS 决定。例如，动作请求对应的 Ability 因冷却而无法激活时，执行器会拒绝请求，连招保留在原来的节点。Arc History 会记录请求被拒绝，但不会记录冷却、消耗不足等具体的 GAS 失败原因。

Ability 在激活过程中就结束时（例如瞬发技能），执行器先报告开始，再报告结束，保证握手顺序正确。

完成回调消费缓冲并产生下一个请求时，执行器会在当前 Ability 的结束回调中同步尝试激活下一个 Ability。下一招也可以使用同一个 Ability，但仍须满足 GAS 的激活条件。

## 缓冲窗口

蒙太奇中的 CadenceArc Buffer Window 开始时，执行器以“动画资产 + 蒙太奇播放实例 ID”为键保存当前请求编号，再打开窗口；结束时取出保存的编号关闭窗口。播放实例 ID 来自通知事件引用中的 `FAnimNotifyMontageInstanceContext`。旧通知迟到的关闭回调携带旧编号，解析器会拒绝它，不会关闭新动作的窗口。前后两个动作共用同一个蒙太奇资产（例如同一招循环，或共用一个多 Section 的蒙太奇）时，两次播放的实例 ID 不同，也能各自关闭自己的窗口。

同一次播放中如果放了两个互相重叠的 Buffer Window，它们的键相同，后开始的窗口会覆盖先开始的记录。一个蒙太奇中的窗口不应重叠。这一机制已有自动化测试覆盖，尚未在真实的蒙太奇播放中验证。

不使用蒙太奇的 Ability，可以在动作开始握手成功后调用执行器的 `OpenBufferWindow` 和 `CloseBufferWindow`。这两个无参接口使用调用时的当前请求编号。如果延迟回调可能在下一招开始后才到达，应预先保存本次请求编号，并调用 CadenceArc 组件上带编号的接口，避免影响新动作。

在动画编辑器中预览蒙太奇时，预览角色上没有执行器，这个通知不做任何事。

## 注意事项

- **结束时机就是完成时机。** `NotifyActionCompleted` 在 Ability 结束时发出，这也是停顿时长的起点。希望在蒙太奇淡出时就允许衔接下一招，应让 Ability 在淡出时结束，而不是等蒙太奇完全播放完。
- **取消即打断。** Ability 被取消时，执行器一律报告 `NotifyActionInterrupted`，连招回到入口动作。GAS 的结束回调只提供 `bWasCancelled`，无法区分“玩家主动取消”（例如闪避）和“被外力打断”（例如受击）。两者在解析器中的行为相同，区别只在 Arc History 的记录名称。
- **动作 Tag 也参与 GAS 的 Tag 规则。** 动作 Tag 位于 Ability 的 `AssetTags` 中，因此也会被其他 Ability 的 Cancel Abilities With Tag 和 Block Abilities With Tag 匹配。例如，Ability 配置了取消 `Action` 下的所有 Tag 时，激活它会取消带有这些 Tag 的其他 Ability。连招衔接时上一招已经结束，不受影响；但如果另有系统在动作执行中激活这类 Ability，被取消的动作会报告为打断。
- **攻击输入只走 CadenceArc。** 交给 CadenceArc 的输入，不要再让 Ability System 组件按输入 Tag 或 Input ID 直接激活 Ability（例如 Lyra 的 `AbilityInputTagPressed`），否则同一次按键会激活两次。
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
