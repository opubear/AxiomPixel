# 游戏性能插桩与低帧率分析（2026-10-14）

当前默认游戏在本机 Debug 构建中约 **4.5 FPS**，主要是 CPU 全世界材料模拟与风效快照的成本；同一代码的 RelWithDebInfo 构建约 **59.6 FPS**，重复采集为 **59.4 FPS**。优化构建已经接近引擎 60Hz 限速，但两项全图工作合计仍占约 **88%** 的帧时间，性能余量很小。此次实现测量设施，未改变模拟算法或默认风效，也未修改引擎源码与日常启动脚本。

## 测量条件与复现

- Apple M5 / MoltenVK，macOS 27.0.1；800×600 窗口、1600×1200 framebuffer。
- 生产 Pixel 主循环，默认地面相机、1024×1024 世界，524288 个初始泥土格，32×16 Coarse NS，风覆盖顶部424行。人物、UI与默认空气折光保持生产设置。
- Debug 编译参数 `-g`，没有 `-O`；RelWithDebInfo 为 `-O2 -g -DNDEBUG`。现有 `run.sh` / `build-and-run.sh` 默认指向 Debug。
- 每组独立启动，先预热30个 Ready 帧，再统计120帧；正式组串行运行，编译已结束，不操作窗口。Performance 分组折叠，避免展开表格的汇总/UI负载影响基线。
- 使用 `steady_clock` 的 CPU wall time；没有 GPU timestamp，也没有为计时增加 `WaitIdle`。`render.cpu` 包括上传写入、命令录制及提交；不能作为 GPU shader 执行时间。
- 主循环先将输入时间限制为50ms，Environment以1/60s固定步推进。这里的帧率来自真实时钟，未使用被限制后的游戏步长。

复现命令（工程根）：

```sh
cmake --build build --target Pixel -j4
cmake --build build-runtime --target Pixel -j4
mkdir -p build-profile-results
(cd Pixel && PIXEL_PROFILE_FRAMES=120 PIXEL_PROFILE_WARMUP=30 \
  PIXEL_PROFILE_SCENARIO=default \
  PIXEL_PROFILE_CSV="$PWD/../build-profile-results/debug-default.csv" \
  ../build/bin/Debug/Pixel)
# 将 executable 换成 ../build-runtime/bin/RelWithDebInfo/Pixel 即测优化版。
# 替换 SCENARIO 和输出文件可复现下表其他组。
```

首次配置和全部变量见 [采样模块说明](../Pixel/Source/Performance/README.md)。所有正式原始帧 CSV、日志和汇总保存在本机 `build-profile-results/`；便于跟随源码保留的证据为 [汇总 CSV](Performance/2026-10-14-summary.csv)、[Debug 默认阶段统计](Performance/debug-default.summary.txt)、[优化版默认阶段统计](Performance/release-default.summary.txt)、[优化版复测](Performance/release-default-repeat.summary.txt)、[F1 阶段统计](Performance/release-field.summary.txt)。初次编译期间的试运行已被正式重测覆盖，不用于结论。

## 对照结果

`active` 为成功取得交换链图像后到 present/EndFrame 后的区间；`gap` 为前一帧结束到本帧开始的间隔，包括限速、事件、acquire/fence等待及跳帧。`frame = active + gap`。

| 构建 / 场景 | 平均 ms | P95 ms | FPS | active ms | gap ms | 模拟步/帧 |
|---|---:|---:|---:|---:|---:|---:|
| Debug / 默认 | 224.59 | 226.41 | 4.5 | 224.29 | 0.30 | 3.00 |
| Debug / 关闭风效 | 167.06 | 169.62 | 6.0 | 166.80 | 0.26 | 3.00 |
| Debug / 暂停模拟 | 74.16 | 74.73 | 13.5 | 74.02 | 0.14 | 0.00 |
| Debug / 暂停且关闭风效 | 18.51 | 18.79 | 54.0 | 18.45 | 0.07 | 0.00 |
| RelWithDebInfo / 默认 | 16.79 | 17.04 | 59.6 | 16.56 | 0.23 | 1.01 |
| RelWithDebInfo / 默认复测 | 16.83 | 17.12 | 59.4 | 16.35 | 0.48 | 1.01 |
| RelWithDebInfo / 关闭风效 | 18.35 | 26.07 | 54.5 | 9.98 | 8.36 | 1.10 |
| RelWithDebInfo / 暂停模拟 | 18.34 | 18.61 | 54.5 | 9.77 | 8.57 | 0.00 |
| RelWithDebInfo / 暂停且关闭风效 | 18.54 | 19.76 | 53.9 | 3.84 | 14.70 | 0.00 |
| RelWithDebInfo / F1力场视图 | 24.33 | 28.70 | 41.1 | 24.24 | 0.09 | 1.46 |

关闭风效保留风模拟；暂停保留世界与风效快照。优化版的轻负载对照虽然 CPU 工作下降，却没有更高的显示 FPS：间隔中出现了8～15ms的 gap。引擎以16.67ms为周期限速，操作系统唤醒和交换链等待也在 gap 内，因此不能据54 FPS与60 FPS的表面差异认定关闭风效反而增加计算成本。当前未进一步分解 gap 内的各类等待。

## 热点及原因

### 1. 全图材料步进是 Debug 的最大成本

`Environment::StepForceField` 每步清空/重置 Processed 并扫描1048576格。静态地面也会沿移动链采样场、判断阻塞，没有静止区域或脏区跳过。

- Debug：**46.66ms/步 × 3步/帧 = 139.99ms/帧**，占整个帧约62.3%。完整 Environment（含风推进和反馈）为148.04ms/帧。
- 优化版：**7.16ms/步**，平均1.008步/帧，累计7.22ms/帧。
- 低帧率触发补算，放大每帧工作；50ms输入上限将实际游戏限制在每帧约3步。Debug每帧真实过去224.59ms却只推进50ms，表现还包含模拟相对于真实时间变慢，不能将帧率乘数直接当作游戏模拟速度。

### 2. 风效快照不区分可见范围，每帧生成整张百万格纹理

`BuildWindVisualFrame` 遍历整个世界，对空气格采样风、量化两个分量，写出4MiB风纹理快照。默认相机朝向无风的地面附近，但世界顶部有风，仍会生成全图数据并执行风效 Pass。

- Debug：**57.71ms/帧**；关闭风效后总帧时间减少约57.53ms，直接支持这一归因。
- 优化版：**7.63ms/帧**；仅这一阶段消耗60Hz预算的约46%。默认复测7.65ms，结果接近。
- 本次计数确认每个启用帧扫描1048576格，世界与风各排入4MiB上传，共8MiB。实际 CPU 上传写入只有约0.145ms，主要观测成本来自生成数据，不能把8MiB直接解释为上传已经成为瓶颈。

### 3. Debug 放大了材质映射与全图验证成本

`BuildWorldGridFrame` 每帧遍历格子，并对非空气格进行材质表查询；`ValidateWorldGridFrame` 再检查整张图。

| 阶段 | Debug ms/帧 | RelWithDebInfo ms/帧 |
|---|---:|---:|
| 世界状态复制 | 0.221 | 0.247 |
| 材质映射 | 13.209 | 0.525 |
| 整帧数据验证 | 4.668 | 0.244 |
| 风模拟推进 | 6.643（3次） | 0.280（约1次） |
| 风材料反馈 | 1.401（3次） | 0.117（约1次） |
| 渲染 CPU 总成本 | 0.358 | 0.264 |
| 角色更新 | 0.005 | <0.001 |

默认粗网格 NS 求解、角色和渲染命令提交不是本次主要的已测 CPU 热点。已测 CPU 时间足以解释默认 Debug 的低帧率，但这不证明任意场景都没有 GPU 瓶颈。

### 4. F1 本身是一个显著的诊断成本

优化版 `BuildForceFieldFrame` 对百万格进行合成场采样、方向正规化和RGB量化，平均 **12.86ms/帧**。加上约1.46个材料步，F1平均为24.33ms/帧（41.1 FPS）。因此性能基线需要明确材质视图或F1视图，不能混用。

## 后续改进顺序

1. 日常性能观察使用 RelWithDebInfo，保留调试符号；当前 Debug 脚本保持原样，由项目决定是否调整默认构建模式。
2. 优先减少风效快照工作：风区不与可见区域相交时跳过视觉采样/风 Pass；再评估可见范围、较低分辨率或直接发布粗风网格供GPU插值。保持纯空气风速、固体标记、帧槽隔离及现有视觉语义。
3. 为静止材料引入活动区域/脏区或安全的休眠机制，减少全世界重复模拟。唤醒条件必须包含周围材料改变、基础力变化、动态风和移动链依赖；不能只根据“上一帧没移动”跳过。
4. 评估世界/材质/风快照的独立版本与增量发布；验证扫描可以缓存未变化版本的结果，仍保留非法数据检查契约。先以当前插桩对比收益，不直接删除检查。
5. 如后续出现 gap 或 GPU 相关疑点，再在引擎通用层扩展 fence/acquire/pacing 和异步 GPU timestamp。此次未修改引擎或引入 Pixel 专用 GPU 诊断依赖。

这些是根据成本提出的后续方向，尚未实现或宣称获得相应收益。

## 封装与验证

核心采样器为 `Pixel/Source/Performance/FrameProfiler.*`，集成层为 `GameProfiler.*`。固定枚举和有界缓冲避免热路径字符串、分配、日志与锁；RAII支持嵌套inclusive/self，按帧统计mean/P95/max，按调用数统计单次成本。关闭时只做空指针检查，不调用时钟。新增重逻辑通常只需添加ID并放置一个scope，计数在循环外汇总，不逐格插桩。

本机空scope探针（10万帧，每帧1父scope和20子scope）约0.68微秒/帧，关闭时21个scope及计数调用约0.058微秒；只代表采样器自身，不包括展开面板的汇总绘制与文件导出。探针与结果在 `build-profile-results/overhead-probe.cpp`、`overhead.txt`。

实际验证：

- Debug CPU/mock完整CTest **12/12**通过，含新 `Pixel.Performance`。
- 新采样器ASan/UBSan测试通过；确定性时钟检查嵌套独占、重复调用、未调用帧计零、P95、环形缓冲、CSV、异常展开、取消及关闭路径。
- Debug与RelWithDebInfo的Pixel构建成功，AET目标构建成功。
- Apple M5真实GPU `PixelRuntimeSmoke`通过：风效、F1、游戏行为、现有ImGui输入、resize/minimize及生命周期。当前系统无Vulkan validation layer；运行的是真实图像/输入断言，不替代validation。
- 10组生产游戏自动采样各120帧、预热30帧后正常退出；逐行核对 `frame=active+gap`、self/untracked加总、材料格数与步数、世界/风上传字节数。

统计不涵盖GPU逐Pass执行时间；没有测量高分辨率reference、自适应NS或LBM复杂场景。这些策略已通过统一的wind.advance/wind.feedback范围接入，后续可在交互面板中选择并继续测量。
