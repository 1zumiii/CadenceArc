# 测试

[返回首页](../README.md)

测试使用 Unreal 自动化测试框架，在内存中构造图，无需预先准备项目图资产。时间边界通过显式传入的时间戳验证，无需等待真实时间经过或手动控制帧时间。

## 运行

关闭 Unreal Editor，然后在 CadenceArcSandbox 仓库根目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

脚本会冷编译编辑器目标，然后运行匹配 `CadenceArc` 筛选条件的测试，Unreal 输出使用英文。确认编译产物与当前源码一致时，可以添加 `-SkipBuild` 跳过构建。

## 运行时测试

在 `Source/CadenceArc/Private/Tests/` 下：

| 文件 | 覆盖 |
| --- | --- |
| `CadenceArcTestSupport.h/.cpp` | 共用的测试夹具、Tag、断言和时间工具 |
| `Resolver/CadenceArcResolverContractTests.cpp` | 公开结果、初始化、解析、分支、请求 ID |
| `Resolver/CadenceArcResolverBufferTests.cpp` | 缓冲窗口、替换和消费 |
| `Resolver/CadenceArcResolverLifecycleTests.cpp` | 生命周期回调、取消、打断、重置 |
| `Resolver/CadenceArcResolverTimeTests.cpp` | 时间戳和缓冲过期 |
| `Resolver/CadenceArcResolverHoldTests.cpp` | 按住资格、阶段、手动和自动松手、蓄力保护、小数阈值边界 |
| `Resolver/CadenceArcResolverConditionTests.cpp` | 转移条件：默认值等价、上下文并集和层级匹配、优先级、`ConditionNotMet`、运行时歧义、停顿区间、按住的上下文和停顿 |
| `Resolver/CadenceArcResolverDebugHistoryTests.cpp` | 调试历史的记录顺序、失败分类、沿用最近宿主时间的记录、环形缓冲、选边时的上下文和停顿（仅编辑器构建） |
| `Graph/CadenceArcGraphValidationTests.cpp` | 图的拓扑校验 |
| `Graph/CadenceArcHoldValidationTests.cpp` | 阶段、时长区间、蓄力配置、旧名重定向 |
| `Graph/CadenceArcConditionValidationTests.cpp` | 条件下的重叠规则、单边条件矛盾、无效停顿区间、可达性警告 |
| `Input/CadenceArcInputTrackerTests.cpp` | 按下和松开的配对、时长、Token |
| `Component/CadenceArcComponentTests.cpp` | CadenceArc 组件：按键配对、输入方式配置和无松手转移时的按下回退、上下文提供者、输入处理结果、按住结束通知、按住资格的提交和取消、逐帧推进与自动释放、统一请求出口、初始化前的持久上下文、注入的时间来源 |

## Enhanced Input 适配测试

在 `Source/CadenceArcEnhancedInput/Private/Tests/` 下：

| 文件 | 覆盖 |
| --- | --- |
| `CadenceArcInputBinderTests.cpp` | 触发事件到组件调用的映射、输入方式写入、无效和重复条目、重复绑定和解除绑定时取消按下中的输入、只取消已绑定的输入、Pawn Restart 后自动绑定、控制器变化时的取消 |

## GAS 执行器测试

在 `Source/CadenceArcGAS/Private/Tests/` 下。测试在独立的 Game World 中生成 Actor，挂上真实的 Ability System 组件并授予测试 Ability：

| 文件 | 覆盖 |
| --- | --- |
| `CadenceArcGASTestAbilities.h/.cpp` | 测试用的持续型、瞬发型、激活必然失败和重复资产 Tag 的 Ability |
| `CadenceArcAbilityExecutorTests.cpp` | Ability 激活和结束与握手回调的对应关系、取消即打断、激活过程中结束、激活失败和匹配异常时拒绝、缓冲消费后激活下一个 Ability（包括同一个 Ability）、窗口回调使用各自保存的请求编号，同一个蒙太奇资产的两次播放按实例 ID 区分 |

窗口测试直接调用执行器接口，没有实际播放蒙太奇，也未覆盖同一动画资产的窗口回调交叠。它验证的是请求编号的记录与使用，不能代替真实蒙太奇通知的集成验证。

## 编辑器模块测试

在 `Source/CadenceArcEditor/Private/Tests/` 下：

| 文件 | 覆盖 |
| --- | --- |
| `CadenceArcLayoutTestSupport.h/.cpp` | 布局测试共用的构图工具和几何断言 |
| `CadenceArcGraphLayoutTests.cpp` | 分层、回边、底部通道、重心排序、不可达节点、无效目标、确定性 |
| `CadenceArcLayoutGeometryTests.cpp` | 实际绘制路径不穿过无关节点、长边通道、引用标签 |
| `CadenceArcLayoutRoutingTests.cpp` | 直角折线的端点保持与竖线分隔、端口重排后的交叉数量 |
| `CadenceArcLayoutCompactChainTests.cpp` | 紧凑链的成组条件和模式切换 |
| `CadenceArcViewportMathTests.cpp` | 跟随缩放和滚动、视口外提示、命中测试 |
| `CadenceArcDebugViewTests.cpp` | 实时视图模型：当前节点、候选、预备边（含按条件选档位）、分支距离 |
| `CadenceArcDebugEventTextTests.cpp` | Arc History 显示的文字 |
| `CadenceArcConditionTextTests.cpp` | 端口行的条件标注、未满足条件的说明、条件类失败和选边上下文的文字 |
| `CadenceArcInputDisplayTests.cpp` | 左下角输入显示：最近输入、上下文、缓冲和忽略标记、按住、停顿、变暗 |

## 覆盖重点

测试覆盖正常流程、接口约定和异常处理，重点包括：

- 初始化和握手失败时状态不变；
- 过期和乱序的回调；
- Last Input Wins 替换；
- 精确的过期边界，零、负数、非有限值和倒退的时间；
- 确定的诊断输出，校验不修改资产。
