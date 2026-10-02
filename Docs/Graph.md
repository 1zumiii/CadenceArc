# 动作图与校验

[返回首页](../README.zh-CN.md)

动作图是一个 `UDataAsset`，描述“在哪个动作、按了什么，接下来做什么”。本文列出图的字段和校验规则。

## 字段

| 类型 | 字段 |
| --- | --- |
| `UCadenceArcGraph` | `EntryActionTag`、`Nodes`、`MaxBufferedInputAgeSeconds` |
| `FCadenceArcNode` | `ActionTag`、`HoldChargeConfigs`、`Transitions` |
| `FCadenceArcTransition` | `InputTag`、`TargetActionTag`、`InputPhase`、`bUseDurationRange` 和 `DurationRange` |

入口节点可以用一个不可执行的根 Tag，只表示解析器的初始状态。`HoldChargeConfigs` 和 `DurationRange` 的含义见[按住输入](HoldInput.md)。

## 校验

编辑器资产校验（`IsDataValid`）和 `UCadenceArcResolver::Initialize` 共用 `UCadenceArcGraph::ValidateGraph`。它会报告：

- 空图，无效或重复的节点 Tag；
- 无效或缺失的入口节点；
- 无效的缓冲时长上限；
- 无效的转移 Tag，找不到的目标；
- 无效的阶段或区间，重叠的转移；
- 无效的蓄力配置。

校验从不修改资产，诊断按数组顺序输出。初始化先校验再替换状态，失败时返回 `InvalidGraph` 并保留原配置。

允许的写法：前向引用、自环、环、终止节点，以及不同节点复用同一个输入 Tag。

还没有实现的：可达性分析和边的优先级。图在初始化后可能被修改，所以解析时仍会检查当前节点和目标节点。
