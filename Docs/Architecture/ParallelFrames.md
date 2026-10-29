# Pixel 隔帧并行接入与验证

引擎调度与线程合同见 [Axiom 并行帧](../../Axiom/Docs/ParallelFrames.md)。游戏第 N 帧在常驻 worker 执行，主线程绘制 N−1 的自持有数据；帧首、UI、输入、Present 和关闭仍在主线程同步边界。

2026-10-30起，Pixel在这个协议内增加[模拟worker](SimulationThread.md)：游戏侧角色呈现与世界模拟分别执行，世界命令等到完整模拟步后发布；帧末join仍保留。下方2026-10-29验证是原双线程基线。

## Pixel 数据交接

`PixelRender::FrameBegin` 在游戏工作派发前调用 publisher。`GamePresentation` 从已提交视图生成独立的 `WorldGridFrame`：地形、风、区域上传版本、相机、角色、设备、育圃和构式几何均复制到呈现端。`PublishedGameplay` 保存同一次提交的值；原生背包画布在该边界生成并写入 GPU upload buffer。`Draw` 不调用 publisher，不读取 Session 或活跃的游戏 read view。

`CommittedWorldView` 会被 `Session::PublishCommit` 更新，不能直接作为并行绘制的数据源。当前并行安全边界是转换后的 `WorldGridFrame`，不是 WorldBuffers 的共享所有权本身。帧末 join 后才允许重新打包、释放发布预算、重置世界或销毁应用。

`PixelGame::Tick` 消费已派发输入，将模拟任务交给独立worker，同时使用捕获的只读地形和角色状态更新玩家呈现及相机。DebugUI的查询和命令接纳、窗口尺寸采集、WorldWill服务和帧末内存采样均处于同步边界。完整提交、预算等待、暂停和固定步语义保留：一应用帧可以对应零个或多个模拟tick。

## 2026-10-29 验证

- Debug CPU 全量：`ctest --test-dir build-cpu --output-on-failure`，119/119 通过。
- `Pixel.ParallelFrame`：真实 Session 与 Camera 更新期间反复读取冻结的地形、风、上传版本、角色、相机、Gameplay 和背包画布；检查下一轮发布及世界重置。
- ThreadSanitizer：`Axiom.UnitTest.FramePipeline` 与 `Pixel.ParallelFrame`，2/2 通过，无竞态报告。
- 正式 Application 线程与关闭测试通过；Pixel、AET、两者生命周期及关闭 smoke 均编译成功。
- Apple M5 / MoltenVK 原生 `PixelRuntimeSmoke --parallel-only` 通过：24 帧逐像素 GPU 读回对照冻结快照，覆盖实际游戏推进、暂停／恢复、resize、世界重置和正常关闭。
- 现有完整 `PixelRuntimeSmoke` 通过，含两轮最小化／恢复、输入、DebugUI、原生背包、构式、设备与育圃；`PixelCloseSmoke` 的延期／失败保存拒绝、重试、活动程序恢复和最终释放通过。
- `AxiomLifecycleSmoke --skip-minimize` 两轮通过，`AxiomCloseRequestSmoke` 通过。AET 本轮没有重测最小化；共用引擎路径的最小化由 Pixel 完整回归覆盖。
- 共享库 CPU：`Axiom.UnitTest.FramePipeline`、`Trace`、`DiagnosticsLinkage`，3/3 通过。
- 正式 Pixel `--PerfTest`：2 帧预热加 12 帧记录，14/14 应用帧的 `engine.game_tick`（tid 2）与 `engine.render`（tid 1）时间区间重叠；trace 恰有 14 对应用帧标记，CSV 每行恰有一次引擎／游戏 Tick，应用自动关闭成功。这证明实际接线并行，不是帧率收益基准。

线程检测命令：

```sh
cmake -S . -B build-parallel-tsan -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-parallel-tsan --target AxiomFramePipelineTest PixelParallelFrameTests -j4
ctest --test-dir build-parallel-tsan -R '^(Axiom.UnitTest.FramePipeline|Pixel.ParallelFrame)$' --output-on-failure
(cd Pixel && ../build/tests/PixelRuntimeSmoke --parallel-only)
```

GPU 测试需要 macOS 原生窗口服务，沙箱内不能完成窗口初始化。Vulkan validation layer 在本机不可用；GPU 验证依靠现有协议检查与画面读回。本轮不宣称跨平台 GPU 或性能提升比例。

## 60 FPS 混合等待复测（2026-10-29）

引擎限帧现采用提前休眠唤醒加尾部自旋；提前量自适应最近的休眠超时，范围 0.5–4 ms。截止时间已过时立即继续，不追赶补帧，原有模拟步长上限和跳帧重置不变。算法和 CPU 代价见 [引擎节拍合同](../../Axiom/Docs/ParallelFrames.md#帧率上限与节拍)。最大帧率现由 `Config/GameConfig.json` 的 `MaxFPS` 设置（缺省60），`--max-fps` 优先覆盖，0关闭主动限帧；以下数据为60 FPS配置的历史对照。

Apple M5 / macOS 27.0.1 / MoltenVK，Release、1600×1200 framebuffer、默认运行场景及 UI、角色静止、Natural v2 固定种子 `20261029`。按旧版/新版交替运行三轮，每轮预热 180 帧、统计 600 帧；两组各 1800 帧，均开启 `--PerfTest` 逐事件 trace 和每帧内存采样。

| 指标 | 纯休眠 | 混合等待 |
|---|---:|---:|
| 平均 FPS（总帧数/总帧间隔） | 55.358 | 60.000 |
| 平均完整帧间隔 | 18.064 ms | 16.667 ms |
| 完整帧间隔 P95 | 19.833 ms | 17.789 ms |
| 主循环起点间隔 P95 | 18.695 ms | 16.668 ms |
| 进程 CPU / wall（单核100%） | 30.13% | 31.79% |

新版三轮分别 60.0065、59.9938、60.0001 FPS。主循环起点由相邻 `engine.pacing` 结束时间近似，完整帧间隔另受快照/绘制等工作量波动影响；尾部计时和统计区间边缘可使有限样本的 FPS 略高于 60。CPU 为 `/usr/bin/time -p` 的整个进程 user+sys/wall，包含启动、预热、游戏和采样开销，不是隔离的自旋成本。独立等待探针预热 60 帧、统计 180 帧，混合等待的平均尾部自旋为 0.839 ms；该值不能直接当作正式游戏的自旋时长。

确定性 `FramePacer`、现有 `Cpu` / `FramePipeline` 和 `ApplicationClose` 四项通过；Pixel Debug/Release 与 AET Release 构建通过。正式 Pixel 六轮均运行 780 帧并正常退出，没有最小化/重建/延迟跳帧；AET Release 和 Pixel Debug 各 60 帧预热加 120 帧采样后正常退出。本轮未重测跨平台、resize/最小化或 GPU 全量 smoke；不保证系统重载或长帧下始终 60 FPS。

本机原始 CSV、trace、进程计时、分析脚本和复测命令保存在 `build-profile-results/frame-pacing-20261029/`（构建产物，不进入版本控制）。

## 启动最大FPS配置验证（2026-10-29）

在同一 Apple M5/MoltenVK 环境，以正式 Pixel Release 主循环验证配置与命令行优先级，每例预热60帧、采样180帧：

| 配置 MaxFPS | 命令行 | 生效上限 | 实测平均FPS | engine.pacing 平均 |
|---|---|---:|---:|---:|
| 30 | 无 | 30 | 30.006 | 29.646 ms |
| 30 | `--max-fps 120` | 120 | 60.000 | 0.017 ms |
| 30 | `--max-fps=0` | 0 | 59.986 | 0.015 ms |
| 0 | 无 | 0 | 59.985 | 0.017 ms |
| 0 | `--max-fps 60` | 60 | 59.987 | 13.391 ms |

120和不限帧在本机均约60FPS；此时主动限帧已近乎消失，不限帧的 `engine.submit` 平均约14.232 ms，而限60时约0.288 ms，耗时主要位于图形队列提交。此任务不修改呈现同步策略，也未测GPU时间戳，不能把0不限帧解释为关闭显示同步或保证达到更高FPS。

AET Release 的 `--max-fps 30` 实测30.004 FPS；Pixel Debug 的 `--max-fps 0` 正常运行。七例均运行240帧后正常退出，测试后恢复配置为60。`FramePacer`、`Command`、`Cpu`、`GameConfig`、`ApplicationClose` 五项通过；缺参、负数、小数和溢出的正式可执行入口均以64退出且未初始化引擎，`--help` 无需加载配置即可成功。Debug/Release构建通过；构建中曾遇到Slang生成头被截断，重生成相应构建缓存后恢复，未修改第三方源码。原始CSV、trace、日志和复测脚本位于 `build-profile-results/max-fps-20261029/`。本轮未验证跨平台或resize/最小化专项。
