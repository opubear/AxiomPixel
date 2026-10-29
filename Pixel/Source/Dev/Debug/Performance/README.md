# Pixel 性能统计

本目录保留 Pixel 的阶段/计数定义、世界与力场的内存分组及启动场景适配。采样、窗口统计、CSV、会话策略和可选 ImGui 表格已上移到 Axiom；通用合同见 [Axiom Diagnostics](../../../../../Axiom/Source/Core/Diagnostics/README.md)。`PixelPerformance` 链接无图形依赖的 `Axiom::Diagnostics`，`PixelGameProfiling` 只适配引擎会话与面板。Axiom 应用层唯一负责帧采样的开始和结束。

## 使用

Pixel 默认不启用 Performance。启动时加入 `--PerfTest`（也接受 `PerfTest`、`-PerfTest`）才会采集最近 240 个 Ready 帧。scope 构造时立即向独立 trace 写入事件名和开始时间，析构或显式结束时写入结束时间和持续时间。没有该参数时，性能环境变量被忽略，计时/内存查询/trace 写入均关闭，面板不能自行启用采样。

启用后，在 **Pixel Debug → Performance** 查看平均值、P95、独占耗时、模拟步数、上传量，以及世界和力场的内存；可以关闭采样、清空或导出 `Pixel/pixel-performance.csv` 及 `.summary.txt`。事件文件默认为 `Pixel/pixel-performance.trace.json`，每条开始/结束记录立即刷新为完整 JSON，正常退出完成收尾。F2 隐藏面板不关闭采样。面板展开时每 0.5 秒刷新统计，折叠时不计算汇总。所有帧内计时都是 CPU 时间，不是 GPU timestamp。

PixelApplication 在构造 Axiom 时登记游戏名 `Pixel`，通过 `GetGameName()` 读取。每次应用实例构造都会生成一个固定的运行 GUID，可通过 `GetRunGuid()` 读取。日志同时输出到控制台和 `Logs/Pixel_<YYYYMMDD_HHMMSS_毫秒>_<GUID>.log`（目录相对启动工作目录，时间为本地启动时间）；从 Pixel 目录启动时保存到 `Pixel/Logs/`，`GetLogFilePath()` 提供绝对路径。启动日志首条为 `event=run run_guid=... game_name="Pixel" log_file="..."`，trace 头含同一个 `run_guid` 和 `game_name`。这些元信息不随清空统计、暂停或重新配置采样而改变。没有 `PerfTest` 时仍写运行日志文件，但不会创建 trace。

日志每条消息另带 `[steady_ns=…]`。同一 `run_guid` 下，用该整数减去 trace 头的 `clock_origin_steady_ns`，得到消息在 trace 时间轴上的纳秒位置；负值表示 trace 开始之前。计算时保留整数精度，不以本地日历时间戳推算位置。标准 trace `ts` 单位是微秒，换算到纳秒需乘以 1000。

带 `--PerfTest` 启动后，用环境变量配置捕获：

```sh
# 从工程根运行；已有构建也可直接使用，无需再次配置。
cmake -S . -B build-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-runtime --target Pixel -j4
(cd Pixel && PIXEL_PROFILE_FRAMES=120 PIXEL_PROFILE_WARMUP=30 \
  PIXEL_PROFILE_CSV=/private/tmp/pixel-default.csv \
  ../build-runtime/bin/RelWithDebInfo/Pixel --PerfTest)
```

| 变量 | 默认 | 含义 |
|---|---|---|
| `PIXEL_PROFILE` | `1`（须有 PerfTest） | `0` 关闭计时、内存和 trace；禁用路径不读取时钟、不查询组件内存 |
| `PIXEL_PROFILE_FRAMES` | 未设置 | 设置为 1～10000 时，保存指定数量的完整帧后自动退出；未设置时为交互式循环缓冲 |
| `PIXEL_PROFILE_MEMORY_INTERVAL` | `1` | 每 N 个 Ready 帧查询一次内存，首帧查询；未采样帧内存列为空 |
| `PIXEL_PROFILE_WARMUP` | `30` | 自动捕获前丢弃 0～10000 帧；第一帧没有启动间隔，正式比较建议至少预热 1 帧 |
| `PIXEL_PROFILE_CSV` | `pixel-performance.csv` | 输出路径，父目录必须存在；汇总保存到同路径加 `.summary.txt`，已有同名文件会覆盖 |
| `PIXEL_PROFILE_TRACE` | CSV 路径扩展名替换为 `.trace.json` | 独立事件文件；不经过日志系统，不可与 CSV 或汇总路径相同 |
| `PIXEL_PROFILE_SCENARIO` | `default` | `default`、`no-wind-visual`、`paused`、`paused-no-wind-visual`、`field` |

当前新建场景默认运行；`paused`、`paused-no-wind-visual` 主动暂停，其余场景保留运行态。场景标签只说明启动设置；采集中途手动修改状态会影响结果。旧默认暂停或旧世界实现的数据不能直接作为当前基线。

对照场景只设置启动时的模拟暂停、风效开关或 F1 视图，使用同一个生产游戏主循环。`no-wind-visual` 保留风物理，`paused` 保留快照/风效。捕获时不手动改配置或操作窗口；最小化、遮挡、其他程序负载均可能影响 wall time。自动模式禁用面板中的采样开关，正常环境设置仍可操作。CSV 每行一帧，保存每阶段的 inclusive/self ms、调用次数和原始计数；汇总保存构建类型与启动场景。

## 计时口径

- `frame`：相邻 FrameEnd 之间的间隔，等于 `frame.active + frame.gap`。平均 FPS 用 `1000 / 平均 frame ms`，不是逐帧 FPS 的算术平均。
- `frame.active`：成功取得交换链图像之后、调用游戏 FrameBegin 之前，到游戏 FrameEnd 与内存查询结束之后。涵盖快照、UI、模拟、提交及 present 调用。
- `frame.gap`：上一个采样结束到本次成功 acquire，包括引擎 60Hz 限速、事件处理、acquire/fence 等待及跳帧停顿，不能等同 GPU 时间。
- `frame.untracked`：active 减去最外层已测量区间的并集，包括未单独插桩的 input/UI buffer 更新/present 等。**不能把所有 inclusive 行相加**；`publish`、`game.tick`、`world.environment`、`render.cpu` 包含子阶段。
- `self`：扣除直接子 scope 的 inclusive 时间。`frame` 与 `active` 是派生总量，self 固定为零；gap 和 untracked 单独列出。新报告为 schema_version=2：gap 的 self 扣除已计时的帧前工作；所有 self（含 gap 与 untracked）之和等于 frame。帧前服务现在计入 engine.game_begin，而不再混入 gap。
- `*_commands`：录制命令的 CPU 时间；`render.cpu` 还包含上传写入和队列提交，**都不是 GPU 执行耗时**。没有加入 GPU 同步等会干扰流水线的测量操作。
- 阶段平均值和 P95 按“每个捕获帧累计时间”计算，未调用的帧计零；P95 是 nearest-rank。`ms_per_call` 用累计时间除以累计调用次数，可以区分材料单步成本和补算次数。上传计数是排入命令的字节量，不代表测得的总线带宽。
- `simulation_input_us` 是游戏收到并限制在 0.25s 内的步长（暂停且未请求单步时不记录）。引擎本身先限制到 0.05s；低 FPS 时游戏时间因此可能比真实时间慢，不能用此计数计算 wall FPS。

## 新增插桩

在 Pixel 的 `Stage` / `Counter` 与 `ProfileSchema()` 对应名称数组中添加稳定 ID，在语义完整的阶段使用 scope；不要对百万格循环中的每格读时钟。

```cpp
#include "Dev/Debug/Performance/FrameProfiler.h"

void HeavyOperation() {
    PIXEL_PROFILE_SCOPE(MaterialStep); // 实际新增阶段时使用自己的 Stage
    // 原有工作；正常返回或异常退出都自动结束 scope。
    pixel::performance::FrameProfiler::Count(
        pixel::performance::Counter::MaterialCells, numberOfCells);
}
```

已有 `PIXEL_PROFILE_SCOPE(StageName)` 自动记录阶段事件；提前结束使用 `PIXEL_PROFILE_BEGIN(timer, StageName)` / `PIXEL_PROFILE_END(timer)`。自定义名称无需登记阶段，可使用 `AXIOM_PERFORMANCE_SCOPE("事件名")`，或 `AXIOM_PERFORMANCE_BEGIN(timer, "事件名")` / `AXIOM_PERFORMANCE_END(timer)`。显式结束调用幂等 `End()`，离开作用域不会重复记录；不直接手动调用 C++ 析构函数。

trace 采用 Chrome Trace Event JSON 的 `B`/`E` 配对事件：`B` 含整数纳秒 `args.start_ns`；`E` 含 `args.start_ns`、`args.end_ns`、`args.duration_ns`。两条记录的标准 `ts` 分别表示开始/结束时间，按格式要求用微秒并保留三位小数，均相对本次 trace 的单调时钟起点。计时实例尚未结束时，文件中也能读到开始事件。逐事件刷新会增加采集开销，开始记录的写入也计入该 scope 的耗时。

trace 保留开始/结束事件流，包括初始化、预热和跳帧，并不受 240 帧窗口或 `Clear samples` 影响；自定义名称仅进入 trace，不新增 CSV 列。所有 scope（包括命名宏和提前结束）须在所属线程严格嵌套。取消帧、暂停或关闭记录器时，受影响的未结束 scope 及其子 scope 按逆序写入带 `args.cancelled: true` 的结束事件，耗时截止取消时刻，后续析构不重复写入。详细格式和文件刷新语义见 [Axiom Diagnostics](../../../../../Axiom/Source/Core/Diagnostics/README.md#nanosecond-event-trace)。

每帧另有 `frame.begin` / `frame.end` 标记（`cat: "frame"`、线程级瞬时事件），两者共享 `args.frame_index`。开始标记带 `start_ns`，结束标记另带 `end_ns`、`duration_ns`，均立即刷新。起点位于事件处理、限帧等待和 acquire 之前，终点位于 Present 与帧末内存采样之后。跳过 acquire 的尝试沿用同一帧区间；取消时写入 `cancelled: true` 的结束标记。帧序号从 1 开始，在预热、清空统计和暂停续写期间不重置，不能直接当作会重置的 CSV 行序号。标记不占用 B/E 嵌套栈，因此命名 scope 可以跨帧。其持续时间仅覆盖当前采样区间，CSV 的 `frame_ms` 还包含上一帧结束到本次区间开始之间的时间。

独立 CPU benchmark 可自行创建 `FrameProfiler` 并在每次完整迭代外调用 `BeginFrame()/EndFrame()`，无须初始化 `GameProfiler` 或 GPU。测量与存储分离；计时核心仅在构造时分配有界历史；汇总/导出可分配。业务内存提供者的去重分配单独计时。当前仅支持同线程严格嵌套 scope，一个线程同一时间只有一个 capture；将来引入 worker 时，应为每线程独立采样和显式聚合，不跨线程共享对象。

`CancelFrame()` 丢弃未完成帧并断开活跃 scope；析构自动取消。开关在下一帧生效，禁用时清除上次时刻，重新启用不会把禁用间隔计成卡顿。清空统计在帧边界执行，保留正常 cadence。计时器用 `steady_clock`，测试注入时钟验证精确数值而非 sleep。

## 内存口径与扩展

每个被采样帧结束时，通过 `World::GetMemoryUsage()` 读取组件仍持有的 CPU 存储。查询遍历块与保留缓冲，并为共享存储去重创建临时集合；不会逐格扫描。查询成本单独计入 diagnostics.memory_sample，可通过 PIXEL_PROFILE_MEMORY_INTERVAL 降低频率。面板单位为 MiB（1,048,576 字节），CSV 保留精确字节。

| 行 | 计入范围 |
|---|---|
| World (excluding fields) | World/WorldMap内联状态、块内材料/方向、四叉树、流式工作余量、Proposals/Winners/Epochs/DirtMovements及存活CPU发布缓冲；排除以下两项 |
| Base forces | 块内持久ForceVector列与基础力场借用视图 |
| Wind backend | WindSimulation、当前策略的具体对象，以及它持有的求解器、几何和反馈数组；覆盖 Empty、Coarse、Reference、Procedural、Hybrid、Adaptive、LBM |
| Force fields | 同一帧的 Base forces + Wind backend |
| Total (world + fields) | 同一帧的 World + Force fields，以上三种所有者只计一次 |

- `Last used = sizeof(对象) + Σ vector.size() × sizeof(元素)`，包含内联成员和填充字节。
- `Last reserved = sizeof(对象) + Σ vector.capacity() × sizeof(元素)`，包含尚未使用的预留容量；`clear()` 不一定降低这个数值。
- `Window peak` 是保留历史内的 reserved 最大帧末样本。历史过期或清空后会下降；总量峰值来自同一帧的合计，不累加各组件不同时刻的峰值。
- 这是按所有权核算的存储量，不是进程RSS。地图计入块载荷对齐容量、节点估算、流式工作余量、CPU发布快照及材料scratch；材料扩容/发布峰值另由预算预留保障。GPU、其他系统、profiler自身和风求解器帧内已释放的临时数组不计，尤其Adaptive临时压力/重建工作区不在帧末样本中。世界预算排除风后端，不能把采样窗口峰值当作进程瞬时分配峰值。
- 切换风策略、风区大小、重置或关闭风后，下一帧查询当前所有者；旧值仍参与历史窗口峰值，清空可开始新的对照。

CSV 在原计时列之后追加 `memory_sampled`，并为 `memory.world`、`memory.fields`、`memory.base_forces`、`memory.wind`、`memory.total` 各追加 `_object_bytes`、`_used_heap_bytes`、`_reserved_heap_bytes`、`_used_bytes`、`_reserved_bytes`。其中 heap 字段已递归计入持有的后端对象。独立计时 benchmark 没有调用内存接口时，`memory_sampled=0` 且内存列留空，不记成零占用。汇总报告记录有效样本数、最近值、均值和窗口峰值。

组件自行说明所有权，采样器不依赖 World 类型。为新组件增加以下接口即可复用计量工具：

```cpp
performance::MemoryUsage Component::GetMemoryUsage() const {
    auto usage = performance::MeasureObject(*this, Cells, Scratch);
    usage.AddEmbedded(Child.GetMemoryUsage()); // Child 的 sizeof 已含在父对象中
    if (Backend)
        usage.AddOwned(Backend->GetMemoryUsage()); // 另行分配的具体后端对象
    return usage;
}
```

`MeasureObject` 的数组参数仅用于扁平、可平凡复制的元素；嵌套持有的资源必须显式递归统计，非拥有指针不重复计数。改变组件存储布局时同步维护 getter 和载荷测试。新增顶层指标时扩展 `MemoryComponent`、名称/界面行及 `MemorySnapshot::Get()`；汇总和 CSV 按枚举统一遍历。独立采样使用 `BeginFrame(); SetMemoryUsage(snapshot); EndFrame();`，同帧重复设置是替换而非累加。

测试入口：`ctest --test-dir build-cpu -R 'Pixel\.(Performance|Memory)' --output-on-failure`。除原计时测试外，覆盖容器 size/capacity、所有权分区、七策略、维度变化、重置/关闭、缺失样本、同帧替换、窗口峰值过期及 CSV；`PixelRuntimeSmoke` 检查生产 ImGui 开关和内存表格。

世界分块版本新增 `active_regions` 计数；材料访问格数、活动区域、区域上传字节与驻留预算的约定见 [世界表示](../../../Docs/WorldRepresentation.md)。

## 引擎阶段与兼容性

CSV 保留 Pixel 原有指标名称和内存列，新增 engine.* 阶段及 skipped_* 计数，列位置不再固定，消费者应按表头读取。frame 仍为相邻 Ready 帧结束间隔；第一帧包含首个采样区间内的等待，正式比较保留预热。active 现在包含 ObservationViews、Session.BeginFrame 和 WorldWill.Service，gap 不再承担这些服务耗时。旧报告的 self 合计公式需按上文升级。

Axiom 的 events/pacing/acquire/fence_wait/image_acquire/input/submit/present 分开报告；Pixel 的 publish/game.tick/render.cpu 仍保留，并嵌在引擎对应阶段内。勿将两个层级的 inclusive 相加。场景物体、构式和设备等没有独立 Pixel scope 的部分仍包含在父阶段中。本次没有引入 GPU timestamp 或后台线程聚合。

生产入口使用 PIXEL_PROFILE_*；独立 AET 使用 AXIOM_PROFILE_*，两者均须 PerfTest 启动参数。直接构造 PixelGame 的测试夹具默认关闭采样，须显式提供启动权限后才可使用 Performance 开关。内存提供者和 Pixel 世界测试仍留在父仓库，通用计时/会话测试位于 Axiom/UnitTest/Tests/DiagnosticsTests.cpp。
