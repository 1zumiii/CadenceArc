# 动作图与校验

[返回首页](../README.md)

动作图是一个 `UDataAsset`，用于配置各动作接受的输入、转移条件和后续动作。本文介绍图的字段和校验规则。

## 字段

| 类型 | 字段 |
| --- | --- |
| `UCadenceArcGraph` | `EntryActionTag`、`Nodes`、`MaxBufferedInputAgeSeconds`，以及连招恢复字段 `ComboResetSeconds` 和 `bFallbackToEntryOnNoMatch` |
| `FCadenceArcNode` | `ActionTag`、`HoldChargeConfigs`、`Transitions` |
| `FCadenceArcTransition` | `InputTag`、`TargetActionTag`、`InputPhase`、`bUseDurationRange` 和 `DurationRange`，以及条件字段 `Priority`、`RequiredContextTags`、`BlockedContextTags`、`bUsePauseRange` 和 `PauseRange` |

入口节点可以用一个不可执行的根 Tag，只表示解析器的初始状态。`HoldChargeConfigs` 和 `DurationRange` 的含义见[按住输入](HoldInput.md)。

## 转移条件与优先级

同一个节点可以为同一输入配置多条转移，根据宿主提供的上下文选择后续动作。例如，按住“前”再按 Heavy 触发突进，在空中按 Heavy 触发下砸。新增字段使用默认值时，已有合法资产的解析行为不变。

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| `RequiredContextTags` | 空 | 上下文必须包含其中全部 Tag（`HasAll`） |
| `BlockedContextTags` | 空 | 上下文不能包含其中任何一个 Tag（`HasAny`） |
| `bUsePauseRange`、`PauseRange` | 关 | 停顿时长必须位于指定区间，左闭右开，可以不设上限 |
| `Priority` | 0 | 多条边满足条件时，选择数值最大的边 |

Tag 按层级匹配：上下文包含 `State.Air.Jump` 时，满足 `Required = State.Air`，也会触发 `Blocked = State.Air` 的排除规则。上下文的来源、停顿时长的计算方式和求值时机见[解析器](Resolver.md#转移条件)。

选边分三步：

1. 按 `InputTag`、`InputPhase` 和按住时长过滤；没有匹配边时返回 `NoMatchingTransition`。
2. 按上下文和停顿过滤；全部不满足时返回 `ConditionNotMet`。
3. 选择 `Priority` 最大的边。最高优先级存在多条候选边时，返回 `Rejected / AmbiguousTransition`，不生成动作请求。低于最高优先级的候选边即使优先级相同，也不影响结果。

选边查询本身不修改状态。松手和完成回调仍会按各自的规则结束按住资格或消费缓冲，详见[解析器](Resolver.md#条件相关的原因)。

一个例子：节点 `SkillA` 上有四条 Heavy 边。

| 边 | 条件 | 优先级 |
| --- | --- | --- |
| → 下砸 | `Required = State.Air` | 3 |
| → 突进 | `Required = Dir.Forward` | 2 |
| → 停顿重击 | 停顿 `[0.3, ∞)` | 1 |
| → 普通重击 | 无 | 0 |

在空中按住“前”并按 Heavy 时，下砸的优先级最高。在地面未按方向键时，若在上一个动作完成后 0.5 秒按 Heavy，会选择停顿重击；若只间隔 0.1 秒，则选择普通重击。

## 连招恢复

两个图级字段控制连招什么时候回到入口，默认都关闭，已有资产的行为不变。

| 字段 | 默认 | 含义 |
| --- | --- | --- |
| `ComboResetSeconds` | 0（关闭） | 上一个动作完成后，停顿达到这个秒数时，下一次输入从入口选边 |
| `bFallbackToEntryOnNoMatch` | 关 | 当前节点没有匹配这个输入的转移时，改从入口选边 |

例如，`ComboResetSeconds = 1.0` 时，Light01 完成后 1 秒内按 Light 接 Light02，超过 1 秒再按 Light 则重新从 Light01 开始。开启回退后，打到没有后续的终结技时，再按 Light 也会重新出 Light01，而不是没有反应。

两者都只改变“从哪个节点选边”，不会提前改变已提交的节点。运行时规则见[解析器](Resolver.md#连招恢复)。

## 校验

编辑器资产校验（`IsDataValid`）和 `UCadenceArcResolver::Initialize` 共用 `UCadenceArcGraph::ValidateGraph`。校验包含以下错误：

- 空图，无效或重复的节点 Tag；
- 无效或缺失的入口节点；
- 无效的缓冲时长上限或连招重置时间（负数或非有限值）；
- 无效的转移 Tag，不存在的目标节点；
- 无效的阶段、按住时长区间或停顿区间；
- 同一条边的 `RequiredContextTags` 和 `BlockedContextTags` 冲突，这条边永远不可能满足；
- 启用 `ComboResetSeconds` 时，非入口节点上某条边的停顿区间下限不小于重置时间。停顿到达下限之前连招已经重置，这条边永远不可能满足；
- 重叠的转移；
- 无效的蓄力配置。

校验会拒绝可能同时满足条件的同优先级转移。对于源节点、输入 Tag 和输入阶段相同的两条边，以下四项同时成立时报告重叠错误：

- 优先级相同；
- 按住时长区间重叠；
- 停顿区间重叠（未启用停顿区间时按 `[0, ∞)` 处理）；
- 条件可能同时满足，即两条边要求的 Tag 与禁止的 Tag 合并后不存在冲突。冲突判断遵循 Tag 的层级匹配规则。

校验逐对检查转移，即使还有更高优先级的转移，也会报告这两条边的重叠错误。报错信息会提示提高其中一条边的优先级，或让两条边的条件互斥。例如，突进和普通重击如果都设为优先级 0，校验会报告重叠；提高突进的优先级，或为普通重击设置 `Blocked = Dir.Forward`，都可以消除这项冲突。

校验还会报告从入口不可达的节点。这项检查只分析图的连接关系，不推断上下文条件是否会在游戏中出现。不可达节点以警告形式写入初始化日志，不阻止初始化。开启 `bFallbackToEntryOnNoMatch` 不会让断开的节点变得可达；没有出边的终止节点本来就不报警告。

启用 `ComboResetSeconds` 后，非入口节点上的停顿区间上限超过重置时间或不设上限时，校验报告警告，并在提示中写明实际生效的区间 `[下限, 重置时间)`。例如，重置时间为 1.0 时，区间 `[0.5, 1.2)` 中的 `[1.0, 1.2)` 永远走不到。校验不修改资产，也不在运行时截断区间；资产中填写的区间保持原样。入口节点上的边不做这项检查，因为在入口重置到入口不会改变任何东西。

校验不修改资产，诊断按数组顺序输出。初始化检查通过后才替换状态，失败时保留原配置。返回原因包括 `InvalidGraph`、`InvalidEntryActionTag`、`EntryNodeNotFound` 和 `UnexpectedState`。

图允许前向引用、自环、环、终止节点，以及不同节点复用同一个输入 Tag。图在初始化后仍可能被修改，因此解析时会再次检查当前节点、候选边和目标节点，必要时返回 `AmbiguousTransition` 等错误。

## 带条件的蓄力档位

为某个输入配置 `HoldChargeConfigs` 后，这个输入的所有 Released 边都必须启用按住时长区间。至少要有一条无上限区间，且所有无上限区间必须共用一个正数下限，作为满蓄力阈值。

例如，普通蓄力和前向突进蓄力都可以使用 `[0.8, ∞)`，再通过不同的优先级或互斥条件区分。未配置 `HoldChargeConfigs` 时，不要求所有无上限区间共用下限，但仍须通过转移重叠校验。
