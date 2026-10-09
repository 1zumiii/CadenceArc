# 蓄力 Montage 表现

## 职责与接入

`CadenceArcAnimation` 提供 `UCadenceArcChargeMontageComponent`，负责按住期间的起手和停留动画。组件订阅 `OnHoldStageChanged` 和 `OnHoldEnded`，读取动作图中的蓄力时长并计算播放速率。组件不确认动作请求，不修改解析器状态，也不依赖 GAS 或 Enhanced Input。

在角色上添加 `CadenceArc Charge Montage` 组件，再配置 `Entries`。蓝图项目无需编写代码；C++ 项目使用这个组件时，在自己的 `Build.cs` 中添加 `CadenceArcAnimation` 依赖。

| 属性 | 配置方式 |
| --- | --- |
| `CadenceArcComponent` | 可选。留空时使用所属 Actor 上的 CadenceArc 组件 |
| `SkeletalMesh` | 可选。留空时优先使用 Character 的 Mesh，否则查找所属 Actor 上的 SkeletalMeshComponent |
| `Entries` | 为需要表现的输入与来源动作配置 Montage |

每个条目的字段如下：

| 字段 | 含义 |
| --- | --- |
| `InputTag` | 对应的输入 Tag，必须有效 |
| `SourceActionTag` | 可选。指定后只在这个来源节点上生效；留空表示任意来源 |
| `Montage` | 包含起手与停留分段的动画 |
| `WindupSection` | 起手分段，默认 `Windup` |
| `HoldSection` | 停留分段，默认 `Hold` |
| `StopBlendOutSeconds` | 结束自身播放时的淡出时间，默认 0.2 秒 |

输入 Tag 和来源动作 Tag 都按精确匹配处理。指定来源的匹配条目优先于通用条目。同样具体的多个条目同时匹配时，组件输出警告并使用第一个。

## 分段与时间

在 Montage 中设置 `Windup`、`Hold` 和项目自己的出招分段。`Windup` 与 `Hold` 必须各自有正的长度。组件只在本次播放实例上连接 `Windup → Hold → Hold`，不会修改资产中的分段连接。

进入 `Charging` 时，组件播放起手段。起手的目标时长为 `ChargeFullSeconds - ChargeStartSeconds`，播放速率由分段长度除以这个时长得出。Montage 自身的 `RateScale` 也参与换算。修改图中的蓄力时长后，无需再手动同步动画速率。

`ChargeStartSeconds = 0` 时，授予按住资格就会报告一次 `Charging`。`BeginInputHold` 的结果通过 `GetStageChanges()` 提供这次切换，CadenceArc 组件在按下调用内广播；后续推进时间不会重复广播。因此起手动画可以在按下时立即播放。

阶段事件可能晚于门槛时刻到达。组件按事件的 `EffectiveTimestampSeconds` 补偿起始位置，避免把整段起手从当前帧重新播放。若同一帧已经触发自动释放、按住资格也已结束，组件跳过这次蓄力表现，交由执行器处理动作请求。

进入 `Charged` 后，`MaxChargedHoldSeconds` 大于 0 时切到停留段。停留速率按 `HoldLength / MaxChargedHoldSeconds × 0.97` 计算，并换算 Montage 的 `RateScale`。0.97 留出少量余量，停留段同时循环，防止时间偏差使动画提前进入后续分段。保持上限为 0 时，解析器在蓄满后自动释放，无需播放停留表现。

## 松手与执行器交接

`OnHoldEnded` 到达后，组件延迟到后续帧清理自身播放。清理时检查 Montage 实例 ID，只停止自己创建的实例。

执行器可以在松手后重新播放同一个 Montage，并从出招分段开始。UE 5.7 会创建新的播放实例，因此蓄力组件不会误停这次出招。不同按住之间的延迟清理同样按实例区分。组件结束运行时也只清理自己拥有的实例。

执行器仍负责动作开始、缓冲窗口和动作结束回调。以 GAS 为例，由 Ability 使用 `PlayMontageAndWait` 播放出招段；蓄力组件不激活 Ability，也不接管这套回调。

Montage 的 Slot、Group 和角色的 AnimGraph 仍由项目配置。同组 Montage 的播放可能中断前一个 Montage，起手段也可能与尚未结束的上一招冲突。应结合项目的缓冲窗口和动画分层检查这一点。组件只同步蓄力计时，不提供联网复制或预测。

## 无效配置

资产校验会报告无效输入 Tag、空 Montage 和不存在的分段。同一输入与来源组合重复时报告警告。

运行时遇到缺少 Mesh 或 AnimInstance、无效分段、非正分段长度或无效蓄力时长时，组件输出警告并跳过表现。解析器的输入处理和动作请求继续按原有规则运行。

如果项目在 Montage 开始播放的同步回调中再次播放同一资产，可能同时出现多个新实例。组件无法确认归属时会输出警告，放弃修改这些实例，避免误停执行器的播放。

排查没有起手动画的问题时，在控制台执行 `Log LogCadenceArcChargeMontage Verbose`。日志会记录收到的阶段和生效时刻，以及资格已结束、没有匹配条目或播放实例已失效等跳过原因。收到 `Charged` 却没有对应播放实例时，也会提示检查前面的 `Charging` 和跳过记录。排查结束后可用 `Log LogCadenceArcChargeMontage Log` 恢复默认日志级别。
