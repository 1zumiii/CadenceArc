# CadenceArc

简体中文 | [English](README.en.md)

CadenceArc 是一个面向 Unreal Engine 5 的数据驱动连招解析框架。连招规则以动作图的形式配置在 DataAsset 中，输入和动作均使用语义化的 GameplayTag 标识。宿主将输入事件和动作生命周期回调提交给解析器，解析器依据动作图生成动作请求，交由外部执行系统处理。核心模块不依赖特定的外部执行方式（如 GAS、蒙太奇等）。

![PIE 连招中的 Arc Debugger：带条件的转移、蓄力时间轴、输入显示和 Arc History](Docs/Images/arc-debugger-overview.png)

*PIE 中的运行时调试器：绿色节点是当前动作，高亮的分支是下一步可以到达的动作。中间显示最近一次按住的蓄力阶段，左下角显示最近的输入，右侧逐条列出解析器收到的调用。*

## 工作方式

```text
输入事件（InputTag + 时间戳）
  -> 解析器：立即解析，或在动作执行期间缓冲
  -> 动作请求（ActionRequest）
  -> 项目的执行器（GAS、蒙太奇、状态机等）
  -> 握手回调：开始、完成、拒绝、打断
```

CadenceArc 的核心只决定下一个动作是什么。动作执行和伤害结算由项目负责；可选模块提供 GAS 执行器和蓄力动画表现。核心模块只依赖 Engine 和 Gameplay Tags，不依赖 GAS 或特定游戏。

| 模块 | 职责 |
| --- | --- |
| `CadenceArc` | 动作图、解析器和标准接入组件 |
| `CadenceArcEnhancedInput` | Enhanced Input 绑定和输入清理 |
| `CadenceArcGAS` | GAS 执行器和缓冲窗口通知 |
| `CadenceArcAnimation` | 按图中的蓄力时间播放 Montage 起手与停留段，不依赖 GAS 或 Enhanced Input |
| `CadenceArcEditor` | 编辑器校验、Arc Debugger 和 Arc History |

## 功能

- **动作图**：以 `UDataAsset` 配置，用 Gameplay Tag 标识动作和输入，编辑器内自动校验。
- **标准接入组件**：`UCadenceArcComponent` 自动处理时间戳、逐帧推进、按键配对、输入方式和请求出口，并提供输入处理结果和按住结束通知。开发者只需配置一次输入方式和上下文来源，并实现执行器回调。
- **Enhanced Input 适配**：可选模块 `CadenceArcEnhancedInput`。在数据资产中配置 Input Action 对应的输入 Tag 和输入方式，角色被控制后自动绑定，并在失去控制时取消按住中的输入。
- **GAS 执行器**：可选模块 `CadenceArcGAS`。按动作 Tag 激活对应的 Ability，把 Ability 的激活、结束和取消转为握手回调，缓冲窗口由蒙太奇通知开关。
- **蓄力动画**：可选模块 `CadenceArcAnimation`。订阅蓄力阶段，按动作图中的时长计算 Montage 分段播放速率；松手后的出招仍由执行器负责。
- **蓝图支持**：组件、适配组件和上下文接口都可以在蓝图中使用，不写 C++ 也能完成接入。
- **两阶段握手**：执行器确认开始后，解析器才提交目标节点。执行器拒绝请求后，解析器清空候选请求，回到 `Ready`，保留原来的动作节点。
- **输入缓冲**：缓冲窗口由执行器开关。缓冲区只有一格，新输入覆盖旧输入，可以设置过期时间。
- **连招恢复**：新建图默认在停顿 1 秒后重新从入口开始，并在当前节点没有匹配转移时回退入口。两项都可关闭，校验会报告与停顿条件冲突的配置。
- **按住与蓄力**：同一按键可以根据按住时长触发不同动作，支持蓄力阶段、蓄力保护和自动释放。
- **转移条件**：同一输入可以根据上下文 Tag 和停顿时长转到不同动作。上下文可以随输入事件提交，也可以由宿主持续设置；停顿时长从上一个动作完成时开始计算。多条转移同时满足时，按优先级选择；最高优先级出现并列时返回歧义结果，不按配置顺序选取。
- **显式时间**：所有时间戳都由调用方传入，解析器不读取时钟。在相同的图配置和初始状态下，相同的输入、上下文、时间和生命周期调用产生相同的结果。
- **运行时调试器**：仅在编辑器中可用。实时显示动作图、解析器状态和收到的输入，并逐条记录调用结果和失败原因，包括未满足的条件。

## 快速开始

CadenceArc 支持 C++ 和蓝图两种接入方式，使用相同的资产和组件。执行器可以自行编写，也可以使用插件提供的 GAS 执行器。

共同的步骤：

1. 将本仓库放到项目的 `Plugins/CadenceArc` 目录（可以作为 Git 子模块），并在编辑器中启用插件。
2. 新建 `CadenceArcGraph` 数据资产，配置入口节点、节点和转移。
3. 在角色上添加 `UCadenceArcComponent`，在 `Graph` 属性中指定动作图。组件负责时间戳、逐帧推进、按键配对和请求出口。需要方向等事件上下文时，让角色实现 `ICadenceArcInputContextProvider`。
4. 使用 Enhanced Input 时，新建 `CadenceArcInputActionSet` 资产，为每个 Input Action 配置输入 Tag 和输入方式，再在角色上添加 `UCadenceArcInputBinderComponent` 并指定这个资产。角色被控制后自动绑定，详见[Enhanced Input 适配](Docs/EnhancedInput.md)。需要按住的输入配置 `HoldRelease`，只在部分动作中蓄力的输入配置 `HoldIfAvailable`。
5. 实现执行器：使用 GAS 时，在角色上添加 `UCadenceArcAbilityExecutorComponent`，让 Ability 的资产 Tag 与动作 Tag 一致，详见[GAS 执行器](Docs/GAS.md)；否则订阅 `OnActionRequested`，并回调动作的生命周期。

### C++

在模块的 `Build.cs` 中添加依赖 `"CadenceArc"` 和 `"GameplayTags"`。使用可选组件时，再添加对应模块：输入适配用 `"CadenceArcEnhancedInput"`，GAS 执行器用 `"CadenceArcGAS"`，蓄力动画用 `"CadenceArcAnimation"`。使用其他输入系统时，在组件的 `InputModes` 中配置输入方式，并在输入绑定中调用 `PressInput` 和 `ReleaseInput`。

```cpp
// 不使用适配组件时的输入绑定：输入方式来自 InputModes，事件上下文来自角色的 CollectInputContext
CadenceArcComponent->PressInput(InputTag);
CadenceArcComponent->ReleaseInput(InputTag);

// 执行器：所有动作请求都从这里进入，包括完成时消费缓冲产生的请求
void UMyExecutor::HandleActionRequested(const FCadenceArcActionRequest& Request)
{
    if (!CanPlay(Request.TargetActionTag))
    {
        CadenceArcComponent->NotifyActionRejected(Request.RequestId);
        return;
    }
    CadenceArcComponent->NotifyActionStarted(Request.RequestId);
    PlayAction(Request); // 动作结束时调用 CadenceArcComponent->NotifyActionCompleted(Request.RequestId)
}
```

组件的完整接口见[CadenceArc 组件](Docs/Component.md)。测试、回放等需要直接控制时间的场景，可以绕过组件直接使用 `UCadenceArcResolver`。

### 蓝图

只使用蓝图时不需要编写 C++，也不需要修改 `Build.cs`。如果自行实现执行器，在 `CadenceArc` 组件的 Events 中添加 `On Action Requested`，在事件图中确认请求、打开和关闭缓冲窗口并报告完成。使用 GAS 执行器组件时无需再实现这套事件处理。

![蓝图示例的执行器](Docs/Images/blueprint-sample-executor.png)

*Sandbox 中的蓝图示例：上一行确认请求并发送 GAS 事件，下一行依次打开缓冲窗口、关闭缓冲窗口并完成动作。*

完整步骤见[蓝图接入](Docs/Blueprint.md)。

完整示例见 [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox)：`L_CadenceArcDemo` 使用 C++ 和基于 Timer 的演示执行器；`L_CadenceArcBlueprintDemo` 复用 C++ 演示角色，在蓝图中实现执行器。两个示例都包含按住输入和转移条件。

## 文档

| 文档 | 内容 |
| --- | --- |
| [CadenceArc 组件](Docs/Component.md) | 标准接入方式：组件的职责、接入步骤、接口和时间来源 |
| [Enhanced Input 适配](Docs/EnhancedInput.md) | 可选模块：用数据资产把 Input Action 绑定到组件，处理失去输入时的清理和触发器设置 |
| [蓝图接入](Docs/Blueprint.md) | 只用蓝图接入的最小用法：资产、组件、执行器和可选功能 |
| [GAS 执行器](Docs/GAS.md) | 可选模块：用 Ability 执行动作请求，请求与 Ability 的对应关系、缓冲窗口和注意事项 |
| [蓄力 Montage 表现](Docs/Animation.md) | 可选模块：起手与停留分段、图驱动的速率和播放实例隔离 |
| [解析器：握手、缓冲与时间](Docs/Resolver.md) | 状态与生命周期、执行器接入、结果类型、缓冲窗口、时间与过期、上下文与停顿 |
| [按住输入](Docs/HoldInput.md) | 松手档位、蓄力配置、逐帧推进、宿主接入注意事项 |
| [动作图与校验](Docs/Graph.md) | 图的字段、转移条件与优先级、校验规则 |
| [运行时调试器](Docs/Debugger.md) | Arc Debugger、条件标注、输入显示、Arc History、布局选项、Sandbox 调试场景 |
| [测试](Docs/Testing.md) | 运行方法和各测试文件的覆盖范围 |
| [迁移说明](Docs/Migration.md) | 各版本的不兼容改动和迁移方法 |

## 状态

当前版本为 `0.6.0-alpha`，仍处于实验阶段。稳定版发布前，API 和资产格式都可能调整。

- 已完成：核心解析与握手、输入缓冲与过期、按住与蓄力（Phase 6）、运行时调试器（Phase 7）、转移条件与优先级（Phase 8）。
- 已完成标准接入组件、Enhanced Input 适配和蓝图接口。Sandbox 中的蓝图示例已在 PIE 中验证。
- GAS 执行器已有自动化测试，覆盖真实的 Ability System 组件；尚未在 Sandbox 中用真实的 Ability 和蒙太奇实测，联网环境也未验证。
- 按住相关的 API 尚未在已上线的游戏中使用，易用性可能继续调整。
- Sandbox 目前使用基于 Timer 的演示执行器，尚未在动画蒙太奇执行器上实测。

路线图：

1. 更多缓冲过期策略；
2. 在 Sandbox 中用真实的 Ability 和蒙太奇验证 GAS 执行器；
3. 输入录制与回放、联网和预测方面的研究。

## 环境要求

- Unreal Engine 5.7 及对应的 C++ 工具链
- Enhanced Input 插件（引擎默认启用，本插件已声明依赖）
- Gameplay Abilities 插件（本插件已声明依赖，启用 CadenceArc 时会一并启用）
- Git LFS（用于管理 Unreal 二进制资产）
