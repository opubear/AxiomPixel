# Pixel 性能统计

`FrameProfiler` 是仅依赖 C++20 标准库的 CPU wall-clock 和组件内存采样器。`MemoryUsage.h` 定义独立的存储统计口径；`GameProfiler` 单独负责游戏开关、ImGui、预热、有限帧捕获和文件输出。世界和渲染只链接 `PixelPerformance`，不依赖 UI 或引擎中的诊断设施。

## 使用

正常运行 Pixel 会收集最近 240 个 Ready 帧。在 **Pixel Debug → Performance** 查看平均值、P95、独占耗时、模拟步数、上传量，以及世界和力场的内存；可以关闭采样、清空或导出 `Pixel/pixel-performance.csv` 及 `.summary.txt`。F2 隐藏面板不关闭采样。面板展开时每 0.5 秒刷新统计，折叠时不计算汇总。所有帧内计时都是 CPU 时间，不是 GPU timestamp。

自动采样通过环境变量配置，不改变引擎的命令行参数：

```sh
# 从工程根运行；已有构建也可直接使用，无需再次配置。
cmake -S . -B build-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-runtime --target Pixel -j4
(cd Pixel && PIXEL_PROFILE_FRAMES=120 PIXEL_PROFILE_WARMUP=30 \
  PIXEL_PROFILE_CSV=/private/tmp/pixel-default.csv \
  ../build-runtime/bin/RelWithDebInfo/Pixel)
```

| 变量 | 默认 | 含义 |
|---|---|---|
| `PIXEL_PROFILE` | `1` | `0` 关闭计时及内存采样；禁用路径不读取时钟、不查询组件内存 |
| `PIXEL_PROFILE_FRAMES` | 未设置 | 设置为 1～10000 时，保存指定数量的完整帧后自动退出；未设置时为交互式循环缓冲 |
| `PIXEL_PROFILE_WARMUP` | `30` | 自动捕获前丢弃 0～10000 帧；第一帧没有启动间隔，正式比较建议至少预热 1 帧 |
| `PIXEL_PROFILE_CSV` | `pixel-performance.csv` | 输出路径，父目录必须存在；汇总保存到同路径加 `.summary.txt`，已有同名文件会覆盖 |
| `PIXEL_PROFILE_SCENARIO` | `default` | `default`、`no-wind-visual`、`paused`、`paused-no-wind-visual`、`field` |

游戏现默认暂停，`default`、`no-wind-visual` 和 `field` 均保留默认暂停状态；测量运行中的模拟需在面板手动取消暂停。历史运行态数据不能与新的默认采样直接比较。

对照场景只设置启动时的模拟暂停、风效开关或 F1 视图，使用同一个生产游戏主循环。`no-wind-visual` 保留风物理，`paused` 保留快照/风效。捕获时不手动改配置或操作窗口；最小化、遮挡、其他程序负载均可能影响 wall time。自动模式禁用面板中的采样开关，正常环境设置仍可操作。CSV 每行一帧，保存每阶段的 inclusive/self ms、调用次数和原始计数；汇总保存构建类型与启动场景。

## 计时口径

- `frame`：相邻 FrameEnd 之间的间隔，等于 `frame.active + frame.gap`。平均 FPS 用 `1000 / 平均 frame ms`，不是逐帧 FPS 的算术平均。
- `frame.active`：游戏 FrameBegin（成功取得交换链图像之后）到 FrameEnd（Present/EndFrame 之后）。涵盖快照、UI、模拟、提交及 present 调用。
- `frame.gap`：上一个 FrameEnd 到本次 FrameBegin，包括引擎 60Hz 限速、事件处理、acquire/fence 等待及跳帧停顿，不能等同 GPU 时间。
- `frame.untracked`：active 减去最外层已测量区间的并集，包括未单独插桩的 input/UI buffer 更新/present 等。**不能把所有 inclusive 行相加**；`publish`、`game.tick`、`world.environment`、`render.cpu` 包含子阶段。
- `self`：扣除直接子 scope 的 inclusive 时间。`frame` 与 `active` 是派生总量，self 固定为零；gap 和 untracked 单独列出。所有普通 scope 的 self 加 untracked 等于 active。
- `*_commands`：录制命令的 CPU 时间；`render.cpu` 还包含上传写入和队列提交，**都不是 GPU 执行耗时**。没有加入 GPU 同步等会干扰流水线的测量操作。
- 阶段平均值和 P95 按“每个捕获帧累计时间”计算，未调用的帧计零；P95 是 nearest-rank。`ms_per_call` 用累计时间除以累计调用次数，可以区分材料单步成本和补算次数。上传计数是排入命令的字节量，不代表测得的总线带宽。
- `simulation_input_us` 是游戏收到并限制在 0.25s 内的步长（暂停时也记录输入）。引擎本身先限制到 0.05s；低 FPS 时游戏时间因此可能比真实时间慢，不能用此计数计算 wall FPS。

## 新增插桩

在 `Stage` / `Counter` 与对应名称数组中添加稳定 ID，在语义完整的阶段使用 scope；不要对百万格循环中的每格读时钟。

```cpp
#include "Performance/FrameProfiler.h"

void HeavyOperation() {
    PIXEL_PROFILE_SCOPE(MaterialStep); // 实际新增阶段时使用自己的 Stage
    // 原有工作；正常返回或异常退出都自动结束 scope。
    pixel::performance::FrameProfiler::Count(
        pixel::performance::Counter::MaterialCells, numberOfCells);
}
```

独立 CPU benchmark 可自行创建 `FrameProfiler` 并在每次完整迭代外调用 `BeginFrame()/EndFrame()`，无须初始化 `GameProfiler` 或 GPU。测量与存储分离；只有汇总/导出复制有界历史，热路径没有字符串查找、锁、日志或分配。当前仅支持同线程严格嵌套 scope，一个线程同一时间只有一个 capture；将来引入 worker 时，应为每线程独立采样和显式聚合，不跨线程共享对象。

`CancelFrame()` 丢弃未完成帧并断开活跃 scope；析构自动取消。开关在下一帧生效，禁用时清除上次时刻，重新启用不会把禁用间隔计成卡顿。清空统计在帧边界执行，保留正常 cadence。计时器用 `steady_clock`，测试注入时钟验证精确数值而非 sleep。

## 内存口径与扩展

每个被采样帧结束时，通过 `World::GetMemoryUsage()` 读取组件仍持有的 CPU 存储。查询只读取对象大小和容器元数据，不扫描世界格子、不分配临时缓冲。面板单位为 MiB（1,048,576 字节），CSV 保留精确字节。

| 行 | 计入范围 |
|---|---|
| World (excluding fields) | World 内联状态、世界格子数组、材料模拟的 Processed / MovementChain / DirtMovements 工作缓冲；排除以下两项 |
| Base forces | 基础力场对象及逐格 ForceVector 数组 |
| Wind backend | WindSimulation、当前策略的具体对象，以及它持有的求解器、几何和反馈数组；覆盖 Empty、Coarse、Reference、Procedural、Hybrid、Adaptive、LBM |
| Force fields | 同一帧的 Base forces + Wind backend |
| Total (world + fields) | 同一帧的 World + Force fields，以上三种所有者只计一次 |

- `Last used = sizeof(对象) + Σ vector.size() × sizeof(元素)`，包含内联成员和填充字节。
- `Last reserved = sizeof(对象) + Σ vector.capacity() × sizeof(元素)`，包含尚未使用的预留容量；`clear()` 不一定降低这个数值。
- `Window peak` 是保留历史内的 reserved 最大帧末样本。历史过期或清空后会下降；总量峰值来自同一帧的合计，不累加各组件不同时刻的峰值。
- 这是按所有权核算的存储量，不是进程 RSS。未计入分配器元数据/对齐开销、帧内已释放的临时数组和容器、渲染发布快照、GPU 资源、其他系统和 profiler 自身。尤其 Adaptive 的临时压力求解/重建工作区不在帧末样本中，不能把窗口峰值当作瞬时分配峰值。
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
