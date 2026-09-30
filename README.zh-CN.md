# CadenceArc

[English](README.md) | 简体中文

CadenceArc 是 Unreal Engine 5 的分支动作框架。它用 Gameplay Tag 驱动，不关心动作怎么执行。

![PIE 连招中的 Arc Debugger 和 Arc History](Docs/Images/arc-debugger-overview.png)

*PIE 连招中的[运行时调试器](#运行时调试器)（仅编辑器）。一次满蓄的 Heavy 松手进入了 `OpenerC`（绿色），它已经执行完，正在等下一个输入。它接下来能走的三招保持亮色，图的其余部分变暗。中间一栏是解析器状态、最近一次按住和蓄力时间轴，右侧的 Arc History 按时间列出解析器的每一次调用。*

CadenceArc 在可配置的动作图里查找语义输入 Tag，产出动作请求。动作具体怎么执行，它不知道，也不需要知道。

```text
InputEvent (InputTag + TimestampSeconds)
  -> SubmitInput
  -> 立即解析，或在动作执行期间缓冲
  -> ActionRequest
  -> 外部执行器
  -> 生命周期握手
  -> 动作完成时消费缓冲的输入
```

名字来自长期的设计目标：玩家的节奏（**cadence**）在一串分支动作里划出一条弧线（**arc**）。

## 状态

CadenceArc 当前版本是 `0.3.0-alpha`，还在实验阶段。第一个稳定版之前，运行时 API 和资产格式都可能变化。开发分支上的不兼容改动见[迁移说明](#迁移说明)。

已经可用的功能：

- 基于 `UDataAsset` 的可配置动作图；
- 确定性的 `InputTag -> ActionTag` 转移解析；
- 明确的解析器状态，以及“解析、提交”两阶段握手；
- 用请求 ID 对应异步的执行回调；
- 拒绝、完成、取消和打断的处理；
- 受 RequestId 保护的输入窗口，缓冲只有一格，后来的输入覆盖先前的（Last Input Wins）；
- 由调用方提供时间戳，缓冲输入可以设置过期时间；
- 结构化的只读结果类型，带蓝图访问节点；
- 编辑器资产校验和解析器初始化共用同一套图校验；
- 物理按下、松开的配对追踪器（`FCadenceArcInputTracker`）；
- 解析器侧的按住资格：按按住时长选择松手档位、由时间推导蓄力阶段、蓄力保护和自动松手；
- 仅编辑器的只读运行时调试器：实时图视图（**Arc Debugger**）和解释失败原因的调用历史（**Arc History**）；
- 纯内存的 Unreal 自动化测试。

Phase 6（按住输入）已经完成。解析器运行时有自动化测试覆盖，Sandbox 演示用真实的按下、松开、取消事件和逐帧游戏时间驱动它。按住相关的 API 还没有在上线的游戏里用过，易用性可能还会调整。

Phase 7（运行时调试器）已经完成，支持本地单进程 PIE。见[运行时调试器](#运行时调试器)。

## 为什么需要握手

图里找到一条转移，不代表外部动作一定能开始。Gameplay Ability、角色状态机或 AI 执行器都可能因为资源、状态或时机拒绝请求。

所以 CadenceArc 把“解析”和“提交”分开：

```text
解析输入
  -> 产出 ActionRequest
  -> 执行器接受或拒绝
  -> Started 回调提交目标节点
  -> 终止回调结束这个请求
```

解析器从不假定发出的动作已经执行成功。

## 输入缓冲

`SubmitInput` 接受输入之前，会校验输入 Tag 和时间戳。时间戳必须是有限的非负数，0 也有效。时间戳无效时返回 `InvalidTimestamp`，不会覆盖已有的有效缓冲。有效输入的处理方式取决于解析器状态：

| 解析器状态 | 缓冲窗口 | 结果 |
| --- | --- | --- |
| `Ready` | 无关 | 立即解析，可能产出一个 `ActionRequest`。 |
| `AwaitingStart` | 关闭 | 返回 `NoAction / RequestPending`，状态不变。 |
| `Executing` | 打开 | 存下这个输入，返回 `Buffered / None`。之后的有效输入会覆盖它。 |
| `Executing` | 关闭 | 返回 `NoAction / BufferWindowClosed`，已存的输入不变。 |

外部执行器用 `OpenBufferWindow(RequestId)` 和 `CloseBufferWindow(RequestId)` 控制窗口。两者都要求传入当前正在执行的请求 ID，所以过期的动画或状态机通知不会产生影响。关闭窗口会冻结已存的输入，不会清空它。

同一个缓冲格也用来存待定的按住资格。有按住资格时，`SubmitInput` 还会多两种拒绝：蓄力中返回 `HoldProtected`，自动松手已经到期返回 `InputTimeAdvanceRequired`。见[按住输入](#按住输入)。

当前动作完成时，`NotifyActionCompleted` 返回 `FCadenceArcActionCompletionOutcome`：

- `GetHandshakeResult()`：回调是否对上了当前请求；
- `GetBufferConsumption()` 和 `GetBufferConsumptionReason()`：缓冲的解析类别和原因，只在握手成功后有意义；
- `GetNextActionRequest()`：`HasNextActionRequest()` 为真时，返回下一个请求的副本。

缓冲的输入被消费后，解析器直接进入 `AwaitingStart`。执行器调用 `NotifyActionStarted` 之前，下一个目标动作仍然没有提交。

## 显式时间与过期

两个公开 API 的时间都由调用方提供：

```cpp
FCadenceArcSubmitOutcome SubmitInput(
    const FCadenceArcInputEvent& InputEvent);

FCadenceArcActionCompletionOutcome NotifyActionCompleted(
    int64 RequestId,
    double CompletionTimestampSeconds);
```

`FCadenceArcInputEvent` 包含 `InputTag` 和 `double TimestampSeconds`，以及 Phase 6 加入的 `InputPhase` 和 `HeldDurationSeconds`。输入和完成的时间戳必须在同一个不递减的时间域里，单位是秒。解析器从不读取 `UWorld`、平台时间或帧计数。不要按帧率或 delta time 缩放时间戳。选一个一致的时间域是适配层的事，例如 World 的游戏时间，它会随暂停停止，也跟随时间膨胀。

`UCadenceArcGraph::MaxBufferedInputAgeSeconds` 默认是 `0.0`，表示不限制缓冲的时长。上限为负数或非有限值时，初始化返回 `InvalidGraph`。Last Input Wins 会同时替换 Tag 和时间戳；关闭窗口时两者都保留。

完成握手成功之后：

| 条件 | 缓冲结果 | 之后的状态 |
| --- | --- | --- |
| 没有缓冲输入 | `NoAction / NoBufferedInput` | `Ready` |
| 完成时间非有限、为负，或早于缓冲输入的时间 | `Rejected / InvalidCompletionTime` | `Ready` |
| 开启了上限，且 `CompletionTimestampSeconds - TimestampSeconds > MaxBufferedInputAgeSeconds` | `NoAction / Expired` | `Ready` |
| 没超过上限，或没开启上限 | 解析缓冲的输入 | 成功时 `AwaitingStart`，否则 `Ready` |

时长正好等于上限时仍然有效。`InvalidCompletionTime` 和 `Expired` 都会结束旧动作，清空它的请求、窗口和缓冲，保留已提交的节点，不产出下一个请求。握手失败时解析器状态完全不变，这时消费字段的默认值（`Rejected / None`）表示没有尝试消费。

## 公开结果类型

`FCadenceArcSubmitOutcome` 提供 `GetCategory()`、`GetReason()`、`GetActionRequest()` 和 `HasActionRequest()`。字段都是私有的，请求的 getter 返回副本。

| 类别 | 含义 | 请求 |
| --- | --- | --- |
| `RequestProduced` | 找到候选转移，`Reason` 为 `None`。 | 候选请求，等待 Started |
| `Buffered` | 输入替换了唯一的缓冲格，`Reason` 为 `None`。 | 空 |
| `NoAction` | 现在没有可走的转移，例如 `RequestPending`、`BufferWindowClosed`、`NoMatchingTransition`。 | 空 |
| `Rejected` | 调用或图数据无效，原因看 `Reason`。 | 空 |

默认构造的结果永远不表示成功。空请求的 ID 为 0，Tag 为空。判断有没有请求，用 `HasActionRequest()` / `HasNextActionRequest()`，不要看请求 ID；`Reason` 用来诊断。

一个最小的外部执行器：

```cpp
const FCadenceArcSubmitOutcome Submit = Resolver->SubmitInput(InputEvent);
if (Submit.HasActionRequest())
{
    StartRequest(Submit.GetActionRequest());
}

const FCadenceArcActionCompletionOutcome Completion =
    Resolver->NotifyActionCompleted(CompletedRequestId, NowSeconds);
if (Completion.HasNextActionRequest())
{
    StartRequest(Completion.GetNextActionRequest());
}
```

`StartRequest` 是宿主自己的函数。它检查执行的前提条件：动作不能执行就调用 `NotifyActionRejected`，否则调用 `NotifyActionStarted(RequestId)`。只有这个调用返回 `ECadenceArcHandshakeResult::Success` 之后，才开始执行动作。

`UCadenceArcBlueprintLibrary` 把结果的 getter 和 `Has*` 检查做成了 BlueprintPure 节点。

## 按住输入

按住输入在 Phase 6 加入。一次 **Hold** 是一次从按下到松开的过程，用 `FCadenceArcInputToken` 标识。按下立刻松开也算一次 Hold，这个名字不代表达到了长按阈值。`FCadenceArcInputTracker` 负责物理按下和松开的配对，并测量按住时长；解析器负责按住资格，把这一对事件变成动作请求。

- `ECadenceArcInputMode::PressOnly` 在按下时提交。`HoldRelease` 在按下时申请按住资格，在手动或自动松手时结算。
- `ECadenceArcHoldStage` 有 `None`、`Holding`、`Charging` 和 `Charged` 四种。阶段由时间推导，不单独存储。`Holding` 表示已有资格但没在蓄力，没有蓄力配置的按住也属于这一种。
- 转移可以设置 `InputPhase` 和左闭右开的 `DurationRange`（`[Min, Max)`，也可以没有上限）。同一源节点、同一 Tag、同一阶段的区间不能重叠。区间可以相邻，也可以有空隙，所以能表达多个松手档位。
- `FCadenceArcNode::HoldChargeConfigs` 里可以放 `FCadenceArcHoldChargeConfig`，给某个输入 Tag 加上蓄力保护和自动松手时间。满蓄阈值取这个 Tag 唯一一条无上限 Released 区间的下限。

### 解析器 API

```cpp
FCadenceArcHoldOutcome         BeginInputHold(const FCadenceArcInputToken&, const FCadenceArcInputEvent& Press);
FCadenceArcInputAdvanceOutcome ReleaseInputHold(const FCadenceArcInputToken&, const FCadenceArcInputEvent& Release);
FCadenceArcInputAdvanceOutcome AdvanceInputTime(double NowSeconds);
FCadenceArcHoldOutcome         CancelInputHold(const FCadenceArcInputToken&);
FCadenceArcHoldSnapshot        GetInputHoldSnapshot() const;
```

宿主每帧先推进时间，处理完推进的结果，再处理输入和生命周期回调：

```cpp
const FCadenceArcInputAdvanceOutcome Advance = Resolver->AdvanceInputTime(NowSeconds);
for (const FCadenceArcInputStageChange& Change : Advance.GetStageChanges())
{
    // 蓄力反馈应该按 Change.EffectiveTimestampSeconds 播放，而不是 NowSeconds。
}
if (Advance.HasActionRequest())
{
    StartRequest(Advance.GetResolution().GetActionRequest());
}
```

| 调用 | 何时接受 | 效果 |
| --- | --- | --- |
| `BeginInputHold` | `Ready`，或窗口打开的 `Executing` | 存下一份按住资格，绑定已提交节点和上下文 ID，并复制这个 Tag 的 Released 边、蓄力配置和 `MaxBufferedInputAgeSeconds`。 |
| `AdvanceInputTime` | 任何状态，时间有限且不递减 | 按时间顺序报告越过的阈值，在 `Pressed + FullCharge + MaxChargedHold` 时自动松手一次。没有按住资格时，这是一次被接受的空操作，不影响普通缓冲。 |
| `ReleaseInputHold` | 存下的 Token 匹配，且事件是同一 Tag 的一致的 `Released` 事件 | 结束按住资格。`Ready` 时立即解析，`Executing` 时存为缓冲事件。 |
| `CancelInputHold` | 存下的 Token 匹配一份资格，或它尚未消费的松手 | 清空缓冲格。不会合成松手，也不会撤回已提交的请求。 |

由这个模型得出的规则：

- 一次按下最多兑现一个动作。自动松手之后，物理松手返回 `NoMatchingHold`。
- 按住资格在窗口关闭和正常的 `NotifyActionCompleted` 之后仍然保留，后者报告 `NoAction / WaitingForRelease`。蓄力继续计时，所以前一个动作结束不会把满蓄攻击降级成轻点。
- `Charging` 或 `Charged` 期间，图里的其他输入都返回 `HoldProtected`。闪避、格挡这类打断应该调用 `CancelInputHold`，不要走图。没有蓄力配置的输入停在 `Holding`，任何被接受的输入都可以替换它。
- 自动松手已经到期时，`SubmitInput` 和 `BeginInputHold` 返回 `Rejected / InputTimeAdvanceRequired`，`NotifyActionCompleted` 返回握手结果 `InputTimeAdvanceRequired`，都没有副作用。先调用 `AdvanceInputTime` 并处理结果，再重试。
- 松手匹配用的是资格里冻结的边副本，所以按住期间修改资产，不会改变这次按下的解释方式。缓冲输入的时长从松手时间戳算起；自动松手的事件时间戳记为截止时刻，时长则按实际观察到的时间算。
- 松手没匹配到任何边，或已经过期，也会结束按住资格，不会退回待定状态。
- 传给 `ReleaseInputHold` 的按住时长，必须等于用一次 `double` 运算算出的 `TimestampSeconds - PressedTimestampSeconds`。解析器不信任调用方给的时长。
- 阈值时刻（开始蓄力、满蓄、自动松手）是按住时长第一次达到阈值的那个 `double` 时间，不是直接相加的 `Pressed + Seconds`。所以快照报告 `Charged` 时，在这一刻松手选中的档位一定与之一致，0.2、0.8 这样的小数阈值也一样。
- `BeginInputHold` 授予资格前，会用共用的图校验器检查源节点。初始化之后资产被改坏时，它返回 `InvalidGraphConfiguration`，不会替换已有的有效资格。

### 宿主接入注意事项

- 宿主把某个键改成 `HoldRelease` 后，当前节点必须有这个 Tag 对应的 `Released` 转移。只有 `Pressed` 边的节点会以 `NoMatchingTransition` 拒绝 `BeginInputHold`；`HoldRelease` Tag 上的 `Pressed` 边永远不会被匹配。
- 按住资格结束后（按下时被拒绝、自动松手、被其他输入替换或被取消），物理松手没有害处，但会返回 `NoMatchingHold`，调试历史里会显示为一次失败的调用。Sandbox 的输入路由先用 `GetInputHoldSnapshot()` 确认资格还是自己的 Token，再调用 `ReleaseInputHold`。
- 如果宿主一直没有送来物理松手，追踪器里的配对和按住资格会一直待定。Sandbox 演示碰不到这种情况。但游戏如果在按住期间取消控制又重新控制同一个 Pawn，或者窗口焦点处理吞掉了松手，就应该在失去控制时调用 `CancelInputHold` 并清空追踪器。这些情况下 Enhanced Input 会发 `Completed` 还是 `Canceled`，还没有验证过。
- Unreal 用 MSVC 的 `/fp:fast` 编译。需要精确检查浮点舍入的代码，必须显式开启精确语义，例如 `CadenceArcHoldTiming.cpp` 里的 `#pragma float_control(precise, on)`。

## 编辑器图校验

编辑器资产校验（`IsDataValid`，在 `WITH_EDITOR` 下）和 `UCadenceArcResolver::Initialize` 共用 `UCadenceArcGraph::ValidateGraph`。它会报告以下问题：空图、无效或重复的节点 Tag、无效或缺失的入口节点、无效的时长上限、无效的转移 Tag、找不到的目标、无效的阶段或区间、重叠的转移，以及无效的蓄力配置。

校验从不修改资产，诊断按数组顺序输出，结果确定。初始化会先校验，再替换状态；失败时返回 `InvalidGraph`，保留原来的配置。前向引用、自环、环、终止节点，以及不同节点复用同一个输入 Tag，都是允许的。可达性分析和边的优先级还没有实现。图在初始化之后可能被修改，所以解析时仍然会检查当前节点和目标节点。

## 运行时调试器

运行时调试器在 Phase 7 加入。可选的 `CadenceArcEditor` 模块（`Type=Editor`）在 **Tools > Debug** 下加了两个 Nomad 标签页。两者都只读：从不调用会改变状态的解析器 API，从不写图资产，运行时也从不回调它们。

### Arc Debugger

选择一个 PIE 里的解析器（显示为 `Actor @ World`），就能看到它的图和实时状态，每帧刷新：

![Root 上按住 Heavy 时的 Arc Debugger](Docs/Images/arc-debugger-hold.png)

*在 Sandbox 的树形图里，于 `Root` 按住 Heavy。两条 `Released` 边都是预备边（虚线，随蓄力进度填充）。`OpenerC` 有虚线框，表示此刻松手会选中它。右侧的时间轴标出按下、开始蓄力、满蓄和自动松手的截止时刻。*

- 已提交节点（绿色标题栏）、等待 `Started` 的候选（黄色描边和黄色连线），以及状态、请求、窗口、缓冲输入和按住的详细信息。
- **分支聚焦**：已提交节点的出边保持亮色；从它出发还能走到的节点正常显示；当前路径（不重置、不打断）已经走不到的节点变暗。
- **预备边**：有按住资格时，源节点上这个 Tag 的 `Released` 边画成虚线，蓄力进度沿线填充；此刻松手会选中的档位加强显示。蓄力时间轴标出按下、开始蓄力、满蓄和自动松手的截止时刻。
- **Follow**：已提交节点变化时，视图在需要时缩小（最小 0.6 倍），并滚动到能看见这个节点和它的直接后继。仍然放不下的后继，会在视图边缘显示可点击的提示。
- **浏览**：右键或中键拖动平移，Ctrl + 滚轮以鼠标为中心缩放（0.3 倍～2 倍）。两种操作都会关掉 Follow，重新勾选即可恢复。悬停在节点上会高亮它的连线；悬停在连线上会高亮两端，并显示 `Source → Target · condition`。
- **Layout** 菜单里是下面几项显示选项。它们都按用户记住，只改变绘制，从不修改资产。
- **Right-angle edges**（默认打开）：前向边只用水平和竖直线段，拐角是小圆角。竖线走在列与列之间的空隙里，每条占一条轨道，不会有两条边共用一条竖线。端点和曲线画法完全相同；关闭后改用光滑曲线。
- **Reorder ports to reduce crossings**（默认打开）：每个节点按出边离开的方向排列端口行。前向边按到达位置从上到下排，然后是引用标签和坏目标，再是往回跳的边，最后是自环。只改变显示顺序，资产里的转移顺序不变，解析结果也不变。
- **References for long edges**（默认关闭，可设置最小跨列数 N）：源列和目标列相差不少于 N 的边不画长线。源节点旁出现一个写着目标名的小标签（`→ SkillF`；往回跳的边是 `↩ Root`），目标旁出现一小段接入线。点标签滚动到目标，点接入线滚回源节点。引用标签不改变列的分配，这些边只是不再占用通道和底部通道。Follow 只需要让标签可见，不必为了远处的目标缩小视图。
- **Compact chains**（默认关闭）：把简单链纵向叠在一个淡色分组框里，减少显示的列数，所有动作和转移仍然可见。例如，`Root → A → B → C → D` 加上 `Root → E → D`，只用三列，不用五列。只有至少两个连续、可达、非入口、恰好一进一出的节点才会成组；分叉、汇合的节点和碰到回边的节点留在组外。平行的转移和无效的目标也会阻止节点入链。没有合适的链时，布局保持不变。关闭选项即可回到分层布局；引用标签与它独立，用的是当前布局的显示列。
- 在 Arc History 里选中一行，会用紫色描出对应的节点和边，并滚动到视图里。

![OpenerA 到 ChainA3 之后的分支聚焦](Docs/Images/arc-debugger-branch-focus.png)

*`OpenerA → ChainA3` 之后（请求 #4，正在执行，缓冲窗口打开）。已提交节点是绿色，它的下一步保持亮色，当前路径已经走不到的分支变暗。产生这个请求的那次按住仍然显示为“Last observed”。*

默认布局是确定性的分层布局（Sugiyama 风格）。从入口深度优先遍历时找到的回边闭合了环，画成虚线，从图下方的通道绕回。列号取最长路径，所以前向边一定指向右侧。长边在经过的每一列都预留一条通道。前向边画成直角折线或保单调的曲线，都只走在节点之间的空闲区域里。紧凑链沿用同样的外层布局，把每条链当成一个不可拆开的格子。链内的边从成员之间预留的空隙里拐过去，仍然是普通的前向边，不是虚线回边。紧凑链用高度换宽度，不会把任意的分支区域重新分组，也不会把整张图折成多行。两种模式都保持节点和转移的索引、运行时高亮、历史焦点和图资产不变。布局每帧根据资产重新生成。

### Arc History

Arc History 跟随 Arc Debugger 里选中的解析器，按从新到旧列出它的调用。成功的调用只显示一行，例如 `Light P at Root → SkillA (request #5)` 或 `SkillA finished`。失败的调用标成红色，下面是一句白话原因，后面跟枚举名，例如 `No transition for Heavy P from SkillD (NoMatchingTransition)`。**Failures only** 只显示失败的调用；**Clear** 隐藏已有的行，不会停止记录。

![同一次 PIE 的 Arc History](Docs/Images/arc-history.png)

*同一次会话，从新到旧。带 `~` 前缀的时间借自解析器最近一次收到的宿主时间（见下文）。第 #15 行是在 `ChainC1` 被拒绝的一次按住，下面写着原因。*

记录规则：

- 记录只存在于 `WITH_EDITOR` 下。打包的游戏里既没有这些类型，也没有记录代码。没有录制开关。
- 每个公开入口先调用私有的 `*Impl`（业务逻辑原样放在这里），再往 256 条的环形缓冲（`GetDebugHistory()`）里追加一条固定大小的 `FCadenceArcDebugEvent`，满了就覆盖最旧的。查询从不记录。`AdvanceInputTime` 只在越过蓄力阶段、自动松手或被拒绝时记录，所以每帧的调用不会冲掉有用的记录。
- 一次调用没做成它要做的事，就算失败：输入既没产出请求也没进入缓冲、按住被拒绝、松手没选中任何招式、握手没返回 `Success`、重置或初始化失败，或者动作完成时缓冲的输入被丢弃。动作完成时没有缓冲输入，以及完成时还有按住在等松手，都是正常情况，不算失败。
- 每一行都有时间。自带时间戳的调用显示自己的时间。不带时间戳的调用（`Started`、窗口变化、握手）显示 `~` 加上解析器最近一次收到的宿主时间。宿主每帧都调用 `AdvanceInputTime`，所以这个时间最多差一帧。解析器仍然从不读取时钟。

Sandbox 的演示执行器提供 **Debug Scenarios**（`TimeScale`、`StartDelaySeconds`、`RejectEveryNthRequest`、`bSendStaleCallbacks`），用来在 PIE 里复现慢窗口、可见的候选、执行器拒绝和过期回调。`TimeScale` 只拉长执行器的计时，不影响图的 `MaxBufferedInputAgeSeconds`，所以它也会让缓冲的输入过期。要整体放慢来测试窗口，请改用控制台命令 `slomo`。

PIE 结束后，Arc History 保留已经读到的行（标题显示 `(ended)`）；选中另一个解析器时清空。

## 运行时模型

### 图数据

`FCadenceArcTransition`

- `InputTag`
- `TargetActionTag`
- `InputPhase`
- `bUseDurationRange` 和 `DurationRange`

`FCadenceArcNode`

- `ActionTag`
- `HoldChargeConfigs`
- `Transitions`

`UCadenceArcGraph`

- `EntryActionTag`
- `Nodes`
- `MaxBufferedInputAgeSeconds`

入口节点可以用一个不可执行的根 Tag，它只表示解析器的初始状态。

### 动作请求

`FCadenceArcActionRequest` 包含：

- `RequestId`：正数，单调递增；
- `InputTag`：选中这条转移的语义输入；
- `SourceActionTag`：解析时所在的节点；
- `TargetActionTag`：图选中的候选动作。

`Reset` 和重新初始化都不会重置请求 ID，所以过期的异步回调不可能对上更新的请求。

### 解析器状态

| 状态 | 含义 |
| --- | --- |
| `Uninitialized` | 没有加载有效的图。 |
| `Ready` | 可以解析新输入。 |
| `AwaitingStart` | 有一个请求，等待接受或拒绝。 |
| `Executing` | 外部执行器已确认请求的动作开始执行。 |

同一时刻最多只有一个未完成的请求。

### 生命周期约定

| 事件 | 要求的状态 | 结果 |
| --- | --- | --- |
| `SubmitInput` 解析成功 | `Ready` | 创建请求并进入 `AwaitingStart`，当前动作不变。 |
| `NotifyActionStarted` | `AwaitingStart` | 提交目标动作，进入 `Executing`。 |
| `NotifyActionRejected` | `AwaitingStart` | 回到 `Ready`，保留源动作，清空请求。 |
| `NotifyActionCompleted` | `Executing` | 保留已提交的动作，消费缓冲，然后进入 `Ready`，或产出下一个请求并进入 `AwaitingStart`。有待定的按住时，保留按住资格并报告 `NoAction / WaitingForRelease`。 |
| `NotifyActionCancelled` | `Executing` | 回到 `Ready`，重置到入口动作，清空请求。 |
| `NotifyActionInterrupted` | `Executing` | 回到 `Ready`，重置到入口动作，清空请求。 |

`Reset()` 只能在 `Ready` 状态调用，返回 `Success`、`NotInitialized` 或 `Busy`。无效的请求 ID、过期回调和状态不对的回调都会被拒绝，不改变解析器状态。

## 架构边界

核心运行时模块只依赖 Unreal Engine 基础模块和 Gameplay Tags。它不依赖：

- Gameplay Ability System；
- 动画蒙太奇；
- 特定的角色或武器类；
- 碰撞或伤害系统；
- 任何具体的游戏项目。

外部系统读取 `TargetActionTag` 并报告生命周期事件。GAS 只是一种可能的适配方式，不是必需的。

## 仓库结构

```text
CadenceArc/
|-- CadenceArc.uplugin
|-- Config/
|-- Content/
|-- Docs/Images/                 README 截图
|-- Resources/
`-- Source/
    |-- CadenceArc/              运行时模块
    |   |-- CadenceArc.Build.cs
    |   |-- Public/
    |   |   |-- Graph/
    |   |   |-- Input/
    |   |   `-- Resolver/
    |   `-- Private/
    |       |-- Graph/
    |       |-- Input/
    |       |-- Resolver/
    |       `-- Tests/
    `-- CadenceArcEditor/        仅编辑器的调试器（Arc Debugger、Arc History）
        |-- CadenceArcEditor.Build.cs
        |-- Public/
        `-- Private/
            |-- Layout/          纯函数的图布局（每个阶段一个文件）、视口计算、命中测试
            |-- ViewModel/       纯函数的调试视图、历史文字、共享的选中状态
            |-- Widgets/         Slate 界面：调试面板、图视口、画布（状态、绘制、绘制工具）、运行时信息、历史
            `-- Tests/
```

运行时模块从不依赖编辑器模块。非编辑器的 Game 目标不包含编辑器模块，也能正常构建。

CadenceArc 在独立的 [CadenceArcSandbox](https://github.com/1zumiii/CadenceArcSandbox) 项目里开发和验证。本仓库以 Git 子模块的形式挂在那里的 `Plugins/CadenceArc` 下。

## 测试

运行时测试在 `Source/CadenceArc/Private/Tests/` 下，在内存里构造图，不依赖项目资产：

- `CadenceArcTestSupport.h/.cpp`：共用的测试夹具、Tag、断言和时间工具；
- `Resolver/CadenceArcResolverContractTests.cpp`：公开结果、初始化、解析、分支和请求 ID；
- `Resolver/CadenceArcResolverBufferTests.cpp`：缓冲窗口、替换和消费；
- `Resolver/CadenceArcResolverLifecycleTests.cpp`：生命周期回调、取消、打断和重置；
- `Resolver/CadenceArcResolverTimeTests.cpp`：时间戳和缓冲过期；
- `Resolver/CadenceArcResolverHoldTests.cpp`：按住资格、快照、阶段跨越、手动和自动松手、蓄力保护、跨越动作完成、取消、生命周期清理、小数阈值边界，以及授予资格时冻结的配置；
- `Graph/CadenceArcGraphValidationTests.cpp`：图的拓扑校验；
- `Graph/CadenceArcHoldValidationTests.cpp`：阶段、时长区间、蓄力配置和旧名重定向；
- `Input/CadenceArcInputTrackerTests.cpp`：按下和松开的配对、时长、Token 和清理；
- `Resolver/CadenceArcResolverDebugHistoryTests.cpp`（编辑器构建）：记录的操作和顺序、失败分类、每帧推进的过滤、借用的时间和环形缓冲容量。

编辑器模块的测试在 `Source/CadenceArcEditor/Private/Tests/` 下：

- `CadenceArcGraphLayoutTests.cpp`：分层、回边、底部通道、重心排序、不可达节点、坏目标和结果确定性；
- `CadenceArcLayoutGeometryTests.cpp`：实际绘制路径的几何性质（路径不穿过无关节点、接入点落在目标标题栏、列之间不重叠）、长边通道、引用标签和接入线；
- `CadenceArcLayoutCompactChainTests.cpp`：紧凑链的成组条件、身份稳定和模式切换的确定性；
- `CadenceArcLayoutRoutingTests.cpp`：直角折线（端点不变、线段水平或竖直、竖线轨道不共用）和端口重排（行号是排列、分组顺序、交叉变少）；
- `CadenceArcViewportMathTests.cpp`：跟随缩放、跟随滚动、视口外提示和命中测试；
- `CadenceArcLayoutTestSupport.h/.cpp`：布局测试共用的构图工具和几何断言；
- `CadenceArcDebugViewTests.cpp`：实时视图模型，包括已提交节点和候选的对应、平行边、预备边和蓄力进度、分支距离，以及只读行为；
- `CadenceArcDebugEventTextTests.cpp`：Arc History 里成功和失败调用显示的文字。

测试重点是约定，不只是正常流程：初始化和握手失败时状态不变、过期和乱序的回调、Last Input Wins 替换、精确的过期边界、零、负数、非有限值和倒退的时间、确定的诊断输出，以及校验不修改资产。

在 CadenceArcSandbox 的检出目录里运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

这个脚本会冷编译编辑器，然后运行所有 `CadenceArc` 测试，Unreal 输出为英文。时间边界用注入的时间戳验证，不靠 sleep，也不靠手动控制帧时间。

## 迁移说明

相对于早期开发版 API 的变化：

- `SubmitInput` 返回 `FCadenceArcSubmitOutcome`，不再返回结果枚举加一个输出参数。把 `ECadenceArcInputResult` 的比较改成类别和原因的判断。
- 完成时的消费结果从 `GetBufferConsumption()` / `GetBufferConsumptionReason()` 读取，先看握手结果。旧的 `Resolved` 对应 `RequestProduced / None`，`InvalidTime` 对应 `Rejected / InvalidCompletionTime`，`Expired` 对应 `NoAction / Expired`。
- `Reset()` 返回 `ECadenceArcResolverResetResult`，不再返回 `bool`。初始化结果 `Busy` 改名为 `UnexpectedState`。
- 结果类型的字段是私有的，请用 getter。
- 用了旧枚举输出的蓝图节点需要手动重新连线。重定向没法把枚举输出转换成结果结构体。
- 原来的 Gesture 类型改名为 Hold，枚举值不变：`ReleaseGestureConfig` -> `HoldChargeConfigs`，`PendingTap` -> `Holding`，`ReleaseGesture` -> `HoldRelease`。`Config/DefaultCadenceArc.ini` 为已有资产提供核心重定向。测试路径 `CadenceArc.Graph.Gesture.*` 改为 `CadenceArc.Graph.Hold.*`。
- 按住功能给两个枚举追加了成员，已有的值不变。`ECadenceArcResolutionReason` 新增 `InvalidInputEvent`、`InputIdentityRequired`、`NoMatchingHold`、`HoldProtected`、`InputTimeAdvanceRequired`、`WaitingForRelease` 和 `InvalidGraphConfiguration`；`ECadenceArcHandshakeResult` 新增 `InvalidCompletionTime` 和 `InputTimeAdvanceRequired`。对这两个枚举做穷举 switch 的代码，需要处理新成员。
- `SubmitInput` 现在会以 `InputIdentityRequired` 拒绝 `Released` 事件：松手必须带着按下时的 Token，通过 `ReleaseInputHold` 提交。阶段或按住时长不一致的事件，会以 `InvalidInputEvent` 拒绝。

重定向在加载和枚举查找上验证过。不保证每一个历史二进制资产都能完整往返转换。

## 路线图

1. 暂停和方向输入条件、更多过期策略、优先级和可达性分析。
2. 可选的执行适配层，包括 GAS。
3. 输入录制、回放、联网和预测的研究。
4. 调试器的后续工作：对非常大的图，在简单链之外支持局部展开分支区域。

## 环境要求

- Unreal Engine 5.7
- Unreal Engine 支持的 C++ 工具链
- 用 Git LFS 管理 Unreal 二进制资产
