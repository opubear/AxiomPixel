# 世界与力场内存统计（2026-10-15）

内存功能已接入现有 Performance 工具。打开 **Pixel Debug → Performance**，在内存表查看最近采样的 used、reserved 和保留窗口内的 reserved 峰值；开关、清空和导出与 CPU 计时共用。汇总报告另含均值，CSV 保存逐帧精确字节。

## 实测结果

macOS / Apple M5，RelWithDebInfo，1024×1024 世界，默认地形，当前默认 **Empty** 风策略、保留高度600（覆盖424行）。默认模拟预热5帧后采集30帧，启动即暂停对照预热5帧后采集20帧。两组各自采样窗口内的容量保持稳定。

| 组件 | 默认模拟 reserved / bytes | 默认模拟 / MiB | 启动即暂停 reserved / bytes |
|---|---:|---:|---:|
| 世界（排除力场） | 13,639,824 | 13.007950 | 12,583,056 |
| 基础力场 | 8,388,640 | 8.000031 | 8,388,640 |
| 风后端 | 24 | 0.000023 | 24 |
| 力场合计 | 8,388,664 | 8.000053 | 8,388,664 |
| 世界与力场总计 | 22,028,488 | 21.008003 | 20,971,720 |

默认模拟的世界 used 为13,639,808字节，比 reserved 少16字节；其余组件 used 与 reserved 相同。基础力场主要是每格8字节的 ForceVector 数组；世界格子每格12字节，共12 MiB。默认模拟相对启动即暂停多出的1,056,768字节为材料模拟保留的工作缓冲（Processed 1 MiB 和 MovementChain 8 KiB），并不代表内存泄漏。运行一段时间后再暂停通常仍会保留这些容量，不能与“启动即暂停”混为同一状态。

Empty 不创建数值风格子，但 WindSimulation 持有指针，具体空策略保存类型信息/区域，因此仍计24字节。切换其他策略时，统计由具体策略递归计算当前求解器和几何数组，不沿用固定估值；七策略均有CPU测试，本表没有宣称测量其他策略的默认大世界运行成本。

可追溯汇总为 [逐组件采样结果](Performance/2026-10-15-memory-summary.csv)。完整逐帧 CSV 和文本报告保留在本机忽略目录 `build-profile-results/memory-default.csv*`、`memory-paused.csv*`。复现方式见 [工具说明](../Pixel/Source/Dev/Debug/Performance/README.md)，默认采集使用 `PIXEL_PROFILE_FRAMES=30 PIXEL_PROFILE_WARMUP=5`；暂停对照另设 `PIXEL_PROFILE_SCENARIO=paused`，采集20帧。

## 封装与边界

`Performance/MemoryUsage.h` 只依赖标准库，以对象 sizeof、vector size/capacity 和明确的嵌套所有权计算存储。每个存储所有者暴露 `GetMemoryUsage()`；WindStrategy 的虚接口让新增策略必须提供实现。World 返回世界、基础力场、风后端三个互斥部分，派生总量来自同一帧，避免内联对象、发布快照或不同时间峰值的重复计数。

`Application/PixelGame::FrameEnd` 仅在采样启用时读取一次快照，交给 FrameProfiler 的有界历史。内存是瞬时量，同帧再次设置会替换先前值；未采样帧留空，不拉低均值。GameProfiler 负责面板、文件和刷新策略。查询只读取固定数量的容器元数据，没有逐格扫描、日志或内存分配。

该数值是组件在帧末仍拥有的CPU存储，不是进程RSS或瞬时分配峰值。排除分配器管理开销、临时局部容器、渲染侧发布快照、GPU和其他系统；尤其自适应策略在压力求解和拓扑重建时使用的临时工作区不在这里。窗口峰值随历史过期下降，清空会重建基线。统计定义、CSV字段和扩展示例见工具说明。

## 验证

- CPU完整CTest 15/15通过，含新增 `Pixel.Memory` 和扩展 `Pixel.Performance`。
- 两项内存/采样测试在 AddressSanitizer + UndefinedBehaviorSanitizer 下通过。
- 50个生产游戏样本逐行核对：对象/堆组成、used ≤ reserved、力场拆分加总、总量加总、报告最近值/均值/峰值与CSV一致。
- Debug 和 RelWithDebInfo 的 Pixel 构建成功；Apple M5 的 `PixelRuntimeSmoke` 通过，包含生产采样开关/内存表格及原有风效、F1、输入、重置、resize/minimize和生命周期断言。同步修正了旧测试在重置后仍通过粗网格诊断器读取风区的假设，改为通用区域接口并断言当前 Empty 默认策略。
- 本机未安装 Vulkan validation layer；GPU smoke 的像素/输入/生命周期断言通过，未运行该 layer 的验证。
