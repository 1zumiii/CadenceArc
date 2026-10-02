# 测试

[返回首页](../README.zh-CN.md)

所有测试都是 Unreal 自动化测试，在内存里构造图，不依赖项目资产。时间边界用注入的时间戳验证，不靠 sleep 或手动控制帧时间。

## 运行

在 CadenceArcSandbox 的检出目录里：

```powershell
powershell -ExecutionPolicy Bypass -File .\Scripts\RunCadenceArcTests.ps1
```

脚本会冷编译编辑器，然后运行所有名字以 `CadenceArc` 开头的测试，Unreal 输出为英文。已经编译过可以加 `-SkipBuild`。

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
| `Resolver/CadenceArcResolverDebugHistoryTests.cpp` | 调试历史的记录顺序、失败分类、借用的时间、环形缓冲（仅编辑器构建） |
| `Graph/CadenceArcGraphValidationTests.cpp` | 图的拓扑校验 |
| `Graph/CadenceArcHoldValidationTests.cpp` | 阶段、时长区间、蓄力配置、旧名重定向 |
| `Input/CadenceArcInputTrackerTests.cpp` | 按下和松开的配对、时长、Token |

## 编辑器模块测试

在 `Source/CadenceArcEditor/Private/Tests/` 下：

| 文件 | 覆盖 |
| --- | --- |
| `CadenceArcLayoutTestSupport.h/.cpp` | 布局测试共用的构图工具和几何断言 |
| `CadenceArcGraphLayoutTests.cpp` | 分层、回边、底部通道、重心排序、不可达节点、坏目标、确定性 |
| `CadenceArcLayoutGeometryTests.cpp` | 实际绘制路径不穿过无关节点、长边通道、引用标签 |
| `CadenceArcLayoutRoutingTests.cpp` | 直角折线（端点不变、竖线不共用）、端口重排（交叉变少） |
| `CadenceArcLayoutCompactChainTests.cpp` | 紧凑链的成组条件和模式切换 |
| `CadenceArcViewportMathTests.cpp` | 跟随缩放和滚动、视口外提示、命中测试 |
| `CadenceArcDebugViewTests.cpp` | 实时视图模型：当前节点、候选、预备边、分支距离 |
| `CadenceArcDebugEventTextTests.cpp` | Arc History 显示的文字 |

## 覆盖重点

测试重点是约定，不只是正常流程：

- 初始化和握手失败时状态不变；
- 过期和乱序的回调；
- Last Input Wins 替换；
- 精确的过期边界，零、负数、非有限值和倒退的时间；
- 确定的诊断输出，校验不修改资产。
