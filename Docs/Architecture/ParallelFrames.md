# Pixel 隔帧并行接入与验证

引擎调度与线程合同见 [Axiom 并行帧](../../Axiom/Docs/ParallelFrames.md)。游戏第 N 帧在常驻 worker 执行，主线程绘制 N−1 的自持有数据；帧首、UI、输入、Present 和关闭仍在主线程同步边界。

## Pixel 数据交接

`PixelRender::FrameBegin` 在游戏工作派发前调用 publisher。`GamePresentation` 从已提交视图生成独立的 `WorldGridFrame`：地形、风、区域上传版本、相机、角色、设备、育圃和构式几何均复制到呈现端。`PublishedGameplay` 保存同一次提交的值；原生背包画布在该边界生成并写入 GPU upload buffer。`Draw` 不调用 publisher，不读取 Session 或活跃的游戏 read view。

`CommittedWorldView` 会被 `Session::PublishCommit` 更新，不能直接作为并行绘制的数据源。当前并行安全边界是转换后的 `WorldGridFrame`，不是 WorldBuffers 的共享所有权本身。帧末 join 后才允许重新打包、释放发布预算、重置世界或销毁应用。

`PixelGame::Tick` 仅消费已派发的游戏输入、推进 Session 并更新游戏相机。DebugUI 的状态查询和动作执行在 worker 启动前完成；窗口尺寸采集、WorldWill 服务和帧末内存采样也处于同步边界。完整提交、预算等待、暂停和固定步语义保持原样：一应用帧可以对应零个或多个模拟 tick。

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
