# Performance 上移 Axiom

2026-10-28 完成；引擎基线为 `b08f000d62467eb38b4379d7d6248e2edded371d`，最终引擎提交以根目录 `AxiomVersion.json` 和子模块 gitlink 为准。修改位于本仓库 `Axiom/` 的 `PixelMain`，相邻参考工程没有修改。

本文保留当时 schema 2 的串行采样验收。2026-10-29 的[游戏与渲染隔帧并行](Architecture/ParallelFrames.md)已升级为 schema 3 和两条 trace 时间线；跨线程 self 时间不能相加为 frame。

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

## 后续：构造与结束分别写入 trace

2026-10-29 按用户反馈，将完成时一次写入的 `X` 事件改为 `B`/`E` 配对：构造立即写入事件名和 `start_ns`，析构或幂等 `End()` 写入 `end_ns`、`duration_ns`，每条记录后刷新完整 JSON。取消帧、暂停和记录器关闭时按逆序结束受影响 scope 及其子 scope，并标记 `cancelled: true`。PerfTest 门控、独立输出和 CSV 统计接口保持不变。每条记录刷新增加文件 I/O 开销；开始记录的写入计入 scope 耗时。

本轮静态专项 5/5、动态专项 5/5（含跨共享库配对与无堆分配探针）、ASan+UBSan trace 测试通过。测试直接在计时实例存活时读取文件，验证开始标记已存在，并覆盖精确纳秒、重复结束、异常析构、混合嵌套取消及暂停续写。Pixel Debug 构建成功；原生运行 1 帧预热加 3 帧正式采集后正常退出，导出 146 对 B/E（32 个阶段）及 3 行 CSV。逐项检查了嵌套配对、时间顺序、`ts` 与整数纳秒精确对应、持续时间差值，以及事件明细未进入日志。临时产物为 `/private/tmp/axiom-trace-pairs-pixel.trace.json`、同前缀 CSV 和日志。

验证平台仍为 macOS/MoltenVK；本轮未重跑全量游戏测试，也不将此次捕获作为性能收益基准。

## 后续：每帧开始与结束标记

2026-10-29 增加 `frame.begin` / `frame.end` 线程级瞬时标记，复用采样器 BeginInterval/EndFrame 的时间戳，带连续 `frame_index` 与整数纳秒开始/结束/持续时间。帧编号跨预热、清空与暂停续写保持连续；跳过 acquire 不重复开始，取消、暂停或关闭时对未完成区间写取消结束标记。瞬时标记不占用 scope 嵌套栈，命名 scope 可跨帧。区间耗时与 CSV 相邻 Ready 帧结束的 cadence 分别保留各自口径。

静态及动态专项各 5/5、ASan+UBSan trace 测试通过，覆盖立即可读、精确纳秒、跨帧 scope、预热/清空/取消/暂停、共享库和无堆分配。Pixel Debug 编译及原生采集通过：1 帧预热加 3 帧正式采集导出 4 对帧标记（序号 1～4）和 3 行 CSV；验证了时间单调、标记配对、scope 嵌套、整数纳秒与标准 ts 对应、相邻结束时间差与 CSV frame_ms 精确对账，标记明细未进入日志。产物 `/private/tmp/axiom-frame-markers-pixel.trace.json` 及同前缀 CSV、日志；平台与验证范围同上。

## 后续：运行 GUID 关联日志与 trace

2026-10-29 增加 `Core/Guid.h` 的 UUID v4 生成函数。每个 Axiom 实例构造时生成一次不可变 `RunGuid`，通过 `GetRunGuid()` 读取；日志先写并刷新 `event=run run_guid=...`，该标识随实例传给 CaptureSession，并作为 trace JSON 首字段 `run_guid`。会话重新配置、清空和暂停续写不更换标识；直接使用独立会话/记录器且未传入标识时自动生成。日志头独立于 PerfTest，trace 仍遵循启动门控；CLI 帮助与参数错误仍在应用实例构造前退出。

静态与动态专项各 7/7，通过 GUID 格式/版本/variant、512 个并发生成样本的独立性、trace 头部及会话标识保留、日志回归、帧采样、共享库和无堆分配验证。运行时静态 Linkage 1/1、动态 Linkage/Help 2/2 通过，包含实例/会话 GUID 一致、不同实例标识不同及 Configure/Shutdown 后保持不变。静态 Pixel 与动态 AET 原生运行各采集 1 帧预热加 3 帧正式数据，验证日志首行与 trace 首字段 GUID 完全相同，两个独立进程 GUID 不同，帧标记及 CSV 数量正常。产物 `/private/tmp/axiom-run-guid-pixel.*`、`/private/tmp/axiom-run-guid-aet.*`；仅 macOS/MoltenVK 验证，未重跑全量游戏测试。

## 后续：游戏元信息与按运行命名的日志文件

2026-10-29 增加 Axiom 不可变 `GameName` 和 `GetGameName()`，Pixel/AET 应用分别显式登记 `Pixel`、`AET`。日志新增文件输出，默认 `Logs/<游戏名>_<YYYYMMDD_HHMMSS_毫秒>_<GUID>.log`，使用本地启动时间，`GetLogFilePath()` 返回固定绝对路径。构造时创建并写入运行头，析构在关闭服务后刷新并分离文件；控制台仍保留，日志级别不变。文件名转换不合法路径字符，原游戏名保留在元信息中；日志头与 trace 的 `game_name`、`run_guid` 一致。日志文件不依赖 PerfTest，生成目录已加入 Git 忽略。

静态与动态专项各 8/8，包括新增 LogFile 的命名、中文与路径字符处理、运行头刷新、并发完整行、跨 DLL 路由、过滤、关闭与新文件隔离、失败目录检查；原 Log/Guid/Trace/Diagnostics 测试通过。运行时静态 Linkage 1/1、动态 Linkage/Help 2/2 通过。静态 Pixel 和动态 AET 的原生 1+3 帧采集均正常退出，确认实际文件名、游戏名、GUID、日志头、启动至最终成功退出消息，以及 trace 元信息相符。Pixel 文件含 12 行、AET 含 13 行 Axiom 日志，分别位于 `Pixel/Logs/` 和 `Axiom/Logs/`；验证产物索引 `/private/tmp/axiom-game-log-verified-paths.json`，控制台与 trace 位于 `/private/tmp/axiom-game-log-{pixel,aet}.*`。仅 macOS/MoltenVK 验证，本轮未重跑全量游戏测试。

## 提交前联合审查：日志与 trace 时间关联

2026-10-29 审查父仓库和 Axiom 子模块的全部本地修改，包括其他任务加入的时钟关联和逻辑/渲染像素分离。日志的 `steady_ns` 减去 trace 的 `clock_origin_steady_ns`，得到同一运行中相对 trace 原点的纳秒位置；日志保留原有本地时间戳。审查发现时钟原点通过流式整数输出会受全局 locale 的千位分组影响，导致 JSON 无法解析；已通过回归测试复现，并改用与事件时间相同的 `to_chars` 序列化，覆盖负值及超过 double 精确整数范围的原点。

联合验证：CPU 全量 114/114；共享库专项 8/8；静态运行时 Linkage 1/1、共享 Linkage/Help 2/2；Pixel/AET/PixelRuntimeSmoke/PixelCloseSmoke 构建通过。原生静态 Pixel 和共享 AET 分别生成 300、144 条事件，均有 4 对帧标记和 3 行正式 CSV。脚本验证游戏名/GUID/文件名、B/E 配对、`ts` 与纳秒字段、CSV 帧间隔，并确认运行头位于 trace 开始前、首帧完成日志位于前后帧标记之间、退出日志位于 trace 结束后。GPU 五种倍率地形和关闭保存协商回归通过。临时证据位于 `/private/tmp/axiom-review-*`。本轮仅验证 macOS/MoltenVK，未重新执行完整 GPU 场景集或 Windows/Linux。
