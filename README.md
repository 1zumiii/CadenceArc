# CadenceArc

简体中文 | [English](README.en.md)

CadenceArc 是一个面向 Unreal Engine 5 的分支连招框架。开发者在动作图中配置各动作接受的输入和后续动作。框架根据这些配置生成动作请求，交给项目的执行系统处理。

![PIE 连招中的 Arc Debugger：带条件的转移、输入显示和 Arc History](Docs/Images/arc-debugger-overview.png)

*PIE 中的运行时调试器：绿色节点是当前动作，高亮的分支是下一步可以到达的动作，右侧逐条列出解析器收到的调用。*

## 工作方式

```text
输入事件（InputTag + 时间戳）
  -> 解析器：立即解析，或在动作执行期间缓冲
  -> 动作请求（ActionRequest）
  -> 项目的执行器（GAS、蒙太奇、状态机等）
  -> 握手回调：开始、完成、拒绝、打断
```

CadenceArc 只决定下一个动作是什么。动作的播放、伤害结算和动画表现都由框架之外的系统负责。核心模块只依赖 Engine 和 Gameplay Tags，不依赖 GAS、动画系统或特定游戏。

## 功能

- **动作图**：以 `UDataAsset` 配置，用 Gameplay Tag 标识动作和输入，编辑器内自动校验。
- **两阶段握手**：执行器确认开始后，解析器才提交目标节点。执行器拒绝请求后，解析器清空候选请求，回到 `Ready`，保留原来的动作节点。
- **输入缓冲**：缓冲窗口由执行器开关。缓冲区只有一格，新输入覆盖旧输入，可以设置过期时间。
- **按住与蓄力**：同一按键可以根据按住时长触发不同动作，支持蓄力阶段、蓄力保护和自动释放。
- **转移条件**：同一输入可以根据上下文 Tag 和停顿时长转到不同动作。上下文可以随输入事件提交，也可以由宿主持续设置；停顿时长从上一个动作完成时开始计算。多条转移同时满足时，按优先级选择；最高优先级出现并列时返回歧义结果，不按配置顺序选取。
- **显式时间**：所有时间戳都由调用方传入，解析器不读取时钟。在相同的图配置和初始状态下，相同的输入、上下文、时间和生命周期调用产生相同的结果。
- **运行时调试器**：仅在编辑器中可用。实时显示动作图、解析器状态和收到的输入，并逐条记录调用结果和失败原因，包括未满足的条件。
- **自动化测试**：130 个 Unreal 自动化测试，覆盖时间边界、过期回调和失败时的状态保持。

## 快速开始

1. 将本仓库放到项目的 `Plugins/CadenceArc` 目录（可以作为 Git 子模块），并在编辑器中启用插件。
2. 在模块的 `Build.cs` 中添加依赖 `"CadenceArc"` 和 `"GameplayTags"`。
3. 新建 `CadenceArcGraph` 数据资产，配置入口节点、节点和转移。
4. 在执行器中创建解析器，接入输入和生命周期回调：

```cpp
Resolver = NewObject<UCadenceArcResolver>(this);
Resolver->Initialize(ComboGraph);

// 提交输入：可能立即产出请求
const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(InputEvent);
if (Submit.HasActionRequest())
{
    StartRequest(Submit.GetActionRequest()); // 能执行时调用 NotifyActionStarted，否则调用 NotifyActionRejected
}

// 动作完成：缓冲中的输入可能产出下一个请求
const FCadenceArcActionCompletionOutcome Completion = Resolver->NotifyActionCompleted(RequestId, NowSeconds);
if (Completion.HasNextActionRequest())
{
    StartRequest(Completion.GetNextActionRequest());
}
```

完整示例见 [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox)。它使用 Enhanced Input 和基于 Timer 的演示执行器驱动 CadenceArc，包含按住输入和转移条件的演示。

## 文档

| 文档 | 内容 |
| --- | --- |
| [解析器：握手、缓冲与时间](Docs/Resolver.md) | 状态与生命周期、执行器接入、结果类型、缓冲窗口、时间与过期、上下文与停顿 |
| [按住输入](Docs/HoldInput.md) | 松手档位、蓄力配置、逐帧推进、宿主接入注意事项 |
| [动作图与校验](Docs/Graph.md) | 图的字段、转移条件与优先级、校验规则 |
| [运行时调试器](Docs/Debugger.md) | Arc Debugger、条件标注、输入显示、Arc History、布局选项、Sandbox 调试场景 |
| [测试](Docs/Testing.md) | 运行方法和各测试文件的覆盖范围 |
| [迁移说明](Docs/Migration.md) | 各版本的不兼容改动和迁移方法 |

## 状态

当前版本为 `0.4.0-alpha`，仍处于实验阶段。稳定版发布前，API 和资产格式都可能调整。

- 已完成：核心解析与握手、输入缓冲与过期、按住与蓄力（Phase 6）、运行时调试器（Phase 7）、转移条件与优先级（Phase 8）。
- 按住相关的 API 尚未在已上线的游戏中使用，易用性可能继续调整。
- Sandbox 目前使用基于 Timer 的演示执行器，尚未在动画蒙太奇执行器上实测。

路线图：

1. 更多缓冲过期策略，以及可选的连招超时自动回到入口；
2. 可选的执行适配层，例如 GAS；
3. 输入录制与回放、联网和预测方面的研究。

## 环境要求

- Unreal Engine 5.7 及对应的 C++ 工具链
- Git LFS（用于管理 Unreal 二进制资产）
