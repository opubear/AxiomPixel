# Performance 上移 Axiom

2026-10-28 完成；引擎基线为 `b08f000d62467eb38b4379d7d6248e2edded371d`，最终引擎提交以根目录 `AxiomVersion.json` 和子模块 gitlink 为准。修改位于本仓库 `Axiom/` 的 `PixelMain`，相邻参考工程没有修改。

## 实现边界

- `Axiom::Diagnostics` 提供 C++20 采样核心、预注册指标、环形历史、inclusive/self、计数器、数值指标、内存记录、统计和 CSV。无需 Vulkan、ImGui 或游戏代码，关闭 runtime 仍可构建。
- `CaptureSession` 管理预热、定长捕获、开关、清空、内存采样间隔和报告。Axiom 统一管理帧开始/结束；等待或跳帧归入下一个 Ready 帧，并记录跳帧原因。
- 引擎阶段覆盖事件、限帧、帧获取、fence 等待、图像获取、游戏钩子、发布、UI、输入、渲染、提交和 Present。通用 `PerformancePanel` 位于 DebugUI 层。
- Pixel 保留阶段/计数器定义、世界内存提供者、场景和 `PIXEL_PROFILE_*` 配置；原有埋点调用继续通过薄适配层工作。AET 使用同一模块，增加 World tick、场景发布和实体/代理/网格指标。
- 动态库只通过 `AxiomDiagnostics` 中的一份线程局部采样路由连接。采样热路径预分配；汇总、导出和 UI 可分配。Pixel 的内存查询成本单独计时，可用 `PIXEL_PROFILE_MEMORY_INTERVAL` 降低查询频率。

## 数据口径与兼容性

报告标记 `schema_version=2`。原 Pixel 指标名保留，CSV 新增引擎列，消费者须按表头读取。`frame = frame.active + frame.gap`；active 从成功获取帧之后开始，包含游戏 FrameBegin 服务。gap 的 self 排除其中已计时的等待；所有 self 列（含 gap 和 untracked）相加等于 frame。父子 inclusive 不可直接相加。

内存与数值指标缺失时导出空值，并按实际样本数汇总。内存代表所有者报告的 CPU 常驻存储；GPU 执行时间、RSS、分配瞬时峰值和跨线程聚合不在本次范围内。具体 API 与采样边界见 [引擎说明](../Axiom/Source/Core/Diagnostics/README.md)，Pixel 使用方式见 [Performance 说明](../Pixel/Source/Dev/Debug/Performance/README.md)。

## 验证结果

本机 macOS / Apple M5 / MoltenVK：

| 检查 | 结果 |
| --- | --- |
| `cmake --build build-cpu -j4`；`ctest --test-dir build-cpu --output-on-failure -j4` | 103/103 通过，包含 Pixel 性能、内存和模块边界 |
| 独立 `Axiom/UnitTest` 动态库配置的 Diagnostics、DiagnosticsAllocation、DiagnosticsLinkage | 3/3 通过，无图形依赖 |
| 通用 Diagnostics 合同测试的 ASan + UBSan 构建 | 通过 |
| Debug 静态及 RelWithDebInfo 动态 runtime 构建 | Pixel 与 AET 均成功 |
| 动态 runtime 的 `Axiom.UnitTest.Linkage`、`Axiom.UnitTest.DiagnosticsLinkage` | 2/2 通过 |
| `(cd Pixel && ../build/tests/PixelRuntimeSmoke --debug-ui-only)` | 原生窗口通过；包含 Performance 开关、内存表格和各风策略 |
| `(cd Axiom && ../build/tests/AxiomLifecycleSmoke)` | 两轮生命周期通过，包括无 UI、已获取帧关闭、resize、最小化跳帧及恢复 |
| 静态 Pixel、动态 Pixel、动态 AET 自动捕获 | 各预热 3 帧、记录 12 帧，导出并正常退出 |

对三份实际 CSV 检查了列名唯一、帧数、frame/active/gap 与 self 对账、引擎及游戏埋点。Pixel 设置内存间隔为 3，两种链接方式均得到 4 个有效内存样本；其余帧的内存列为空。分配探针覆盖预分配后的 100 帧嵌套 scope、计数器、数值和内存写入，确认采样核心热路径没有堆分配。

复现短捕获示例（先完成相应 runtime 构建）：

```sh
(cd Pixel && PIXEL_PROFILE_FRAMES=12 PIXEL_PROFILE_WARMUP=3 PIXEL_PROFILE_MEMORY_INTERVAL=3 PIXEL_WORLD_SEED=42 PIXEL_PROFILE_CSV=/private/tmp/pixel-profile.csv ../build/bin/Debug/Pixel --PerfTest)
(cd Axiom && AXIOM_PROFILE_FRAMES=12 AXIOM_PROFILE_WARMUP=3 AXIOM_PROFILE_CSV=/private/tmp/aet-profile.csv ../build/bin/Debug/Axiom --PerfTest)
```

本轮本机证据保存在 `/private/tmp/axiom-diagnostics-*`，为临时产物。测试期间存在并行构建，捕获用于验证行为，不作为性能提升基准。Vulkan validation layer 不可用；未验证 Windows/Linux。本次变更没有增加 GPU 同步。

## 后续：PerfTest 与事件 trace

2026-10-28 后续改为显式 `PerfTest` 启动门控；上述复现命令已补参数，原迁移测试数字保留为历史证据。scope 现在支持 RAII/幂等提前结束，并另写 Chrome Trace JSON；CSV 仍保留原有统计。trace 纳秒字段、缓冲输出、预热和暂停语义见引擎说明。

本轮验证：CPU 全量 106/106；静态/动态诊断专项各 5/5；ASan+UBSan trace 合同通过；原生 Pixel Debug UI 验证无参数禁用提示与开启权限后的内存表格；共享模块在无参数且性能环境变量非法时仍能正常配置。实际带参数捕获中，静态 Pixel 导出 496 个事件、动态 AET 导出 266 个事件；均为 3 帧预热加 12 帧正式数据。验证了整数纳秒与标准 ts/dur 精确对应、CSV/self 对账和事件明细未进入日志；注册阶段的 trace 写入及逐帧刷新通过无堆分配探针。
