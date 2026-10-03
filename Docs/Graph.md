# 动作图与校验

[返回首页](../README.zh-CN.md)

动作图是一个 `UDataAsset`，描述“在哪个动作、按了什么，接下来做什么”。本文列出图的字段、转移条件和校验规则。

## 字段

| 类型 | 字段 |
| --- | --- |
| `UCadenceArcGraph` | `EntryActionTag`、`Nodes`、`MaxBufferedInputAgeSeconds` |
| `FCadenceArcNode` | `ActionTag`、`HoldChargeConfigs`、`Transitions` |
| `FCadenceArcTransition` | `InputTag`、`TargetActionTag`、`InputPhase`、`bUseDurationRange` 和 `DurationRange`，以及条件字段 `Priority`、`RequiredContextTags`、`BlockedContextTags`、`bUsePauseRange` 和 `PauseRange` |

入口节点可以用一个不可执行的根 Tag，只表示解析器的初始状态。`HoldChargeConfigs` 和 `DurationRange` 的含义见[按住输入](HoldInput.md)。

## 转移条件与优先级

同一个节点上，同一个输入可以有多条边，按宿主提供的条件走不同的招。例如按住“前”再按 Heavy 出突进，空中按 Heavy 出下砸。条件字段都有默认值，不填时行为和没有条件的旧资产完全一样。

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| `RequiredContextTags` | 空 | 上下文必须包含其中全部 Tag（`HasAll`） |
| `BlockedContextTags` | 空 | 上下文不能包含其中任何一个 Tag（`HasAny`） |
| `bUsePauseRange`、`PauseRange` | 关 | 停顿时长必须落在区间里，左闭右开，可以没有上限 |
| `Priority` | 0 | 多条边都满足时，数值大的胜出 |

Tag 按层级匹配：上下文里有 `State.Air.Jump`，就满足 `Required = State.Air`，也会被 `Blocked = State.Air` 挡住。上下文的来源、停顿的算法和求值时机见[解析器](Resolver.md#转移条件)。

选边分三步：

1. 按 `InputTag`、`InputPhase` 和按住时长过滤；一条都没有时返回 `NoMatchingTransition`。
2. 按上下文和停顿过滤；全部不满足时返回 `ConditionNotMet`。
3. 取 `Priority` 最大的边。最大值有多条时返回 `Rejected / AmbiguousTransition`，状态不变，不按数组顺序猜。比最大值低的打平不影响结果。

一个例子：节点 `SkillA` 上有四条 Heavy 边。

| 边 | 条件 | 优先级 |
| --- | --- | --- |
| → 下砸 | `Required = State.Air` | 3 |
| → 突进 | `Required = Dir.Forward` | 2 |
| → 停顿重击 | 停顿 `[0.3, ∞)` | 1 |
| → 普通重击 | 无 | 0 |

空中按着前按 Heavy，下砸、突进和普通重击都满足，下砸优先级最高。在地面不按方向，上一招完成后 0.5 秒再按，停顿重击胜出；0.1 秒就按，只剩普通重击。

## 校验

编辑器资产校验（`IsDataValid`）和 `UCadenceArcResolver::Initialize` 共用 `UCadenceArcGraph::ValidateGraph`。它报告的错误：

- 空图，无效或重复的节点 Tag；
- 无效或缺失的入口节点；
- 无效的缓冲时长上限；
- 无效的转移 Tag，找不到的目标；
- 无效的阶段、按住时长区间或停顿区间；
- 同一条边的 `RequiredContextTags` 和 `BlockedContextTags` 冲突，这条边永远不可能满足；
- 重叠的转移；
- 无效的蓄力配置。

“重叠”指两条边一定会在运行时打平。同源、同 Tag、同阶段的两条边，下面四点同时成立才报错：

- 优先级相同；
- 按住时长区间重叠；
- 停顿区间重叠（没开停顿区间按 `[0, ∞)` 算）；
- 条件可能同时满足，即一条边要求的 Tag 没被另一条边禁止。

报错信息会提示两种改法：提高其中一条的优先级，或者让两条边的条件互斥。上面例子里，突进和普通重击如果都是优先级 0，按着前时一定打平，校验会报错；给普通重击加上 `Blocked = Dir.Forward` 也能解决。

另有一类警告：从入口出发永远走不到的节点。警告不阻止初始化，初始化时写进日志。

校验从不修改资产，诊断按数组顺序输出。初始化先校验再替换状态，失败时返回 `InvalidGraph` 并保留原配置。

允许的写法：前向引用、自环、环、终止节点，以及不同节点复用同一个输入 Tag。图在初始化后可能被修改，所以解析时仍会检查当前节点和目标节点；运行时出现的 `AmbiguousTransition` 通常就是这种情况。

## 带条件的蓄力档位

同一个键的多条无上限松手档位（例如 `[0.8, ∞)` 的普通蓄力和 `[0.8, ∞) + Dir.Forward` 的突进蓄力）必须用同一个下限。满蓄时间由这个下限决定，所以带条件的蓄力分支不能有各自不同的满蓄时间。
