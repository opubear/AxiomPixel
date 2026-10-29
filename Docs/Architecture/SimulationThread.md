# 独立模拟线程与世界帧提交

运行时由 `PixelGame::Initialize` 启用 `GameSession::EnableSimulationThread`。引擎的游戏 worker、Pixel 的常驻模拟 worker 和主线程上的渲染各自工作。`SimulationWorker` 复用引擎的有界 `FramePipeline`；每次最多一个模拟任务，游戏和模拟都完成后才离开应用帧。同步单元测试仍可直接使用未启用 worker 的 `GameSession`。

## 帧与所有权

1. 帧首和输入/UI 边界没有在途模拟。发布器将上一份完整世界版本及游戏侧角色呈现、相机转换成渲染自有的 `WorldGridFrame`。Draw 只读取这份冻结数据。
2. 游戏线程消费输入，复制角色运动状态、固定步时钟和世界只读租约，然后派发模拟任务。模拟 worker 独占可变 World、权威 Actors、FormulaBridge、设备和领域提交。
3. 游戏线程使用独立的 `PlayerPresentation`，针对上一份已提交地形计算玩家显示位置和相机；它不写权威角色、不移动世界、不产生动作回执或存档。渲染的角色层可直接采用这份游戏侧结果。每个游戏帧重新从权威状态开始；换世界、暂停或角色身份变化时回退到完整提交中的角色。
4. 模拟 worker 执行固定步、跨领域逻辑和完整发布。双方结束后释放游戏侧地形租约；下一次渲染发布才接纳新的世界结果。异常也先 join，再传回调用方，销毁时先停止 worker。

角色呈现是预测，碰撞和存档仍以模拟线程的权威结果为准。改变地形可能让预测与该步的权威结果短暂不同，下一游戏帧重新校正。当前保留应用帧末同步，因此慢模拟仍可能限制应用帧率；没有建立可以无限积压的独立模拟时钟或无等待渲染循环。

`GameSession` 的公开借用引用、`CommittedWorldView`、观察服务和调试查询仍要求处于 join 后的边界。它们不是任意线程可用的服务。并发游戏工作只能访问派发前捕获的值及 `WorldBuffers::ReadLease`；共享所有权不能替代帧协议。Simulation 继续不依赖 Gameplay 或 Render。

## 世界命令与回执

开启 worker 后，`Execute/Submit` 只接纳命令，不准备世界写槽、不立即修改地形。`ActionStatus::Queued` 没有 `ResolvedAt` 和结果，不表示动作成功。队列总计最多64条；重复序号返回同一请求状态，拒绝和容量失败也消费序号，终态缓存仍为每调用者64条。`FindAction(RequestKey)` 查询完成结果；`LastAction` 保持最新请求的顺序，不会被较早请求的延迟完成覆盖。

不足一个固定步时命令继续排队。下一步在环境推进之前按提交顺序执行命令，并重新检查权限、程序/设备版本、资源和容量。期间禁止发布，随后依次推进环境、角色、构式、设备、育圃和权限协调。完整一步成功后才交付终态回执和新的只读版本。回执的 `ResolvedAt` 是完成该步的提交点；动作事实仍保留步内操作顺序和相应的领域 Revision，未完成事务的事实不向外暴露。

预算不足时保留已执行命令、步首输入和准备中的环境，不再次开采、扣库存或推进风。呈现和观察继续保留旧提交，回执保持 Queued。知识快照与观察范围在完整提交时一起更新；延期发布时返回缓存中的知识，不能把实时 Journal 附到旧世界版本。准备前可取消排队命令；已经进入模拟事务后不能撤回局部效果。世界重置清除旧队列并换 epoch。排队期间保存和暂停请求返回 Deferred；完成模拟后可以重试。原有预算 Pending、全局暂停、显式单步与存储错误语义保留。

宿主控制（保存、重置、暂停、程序编辑、场景装配和 WorldWill 记录）在同步边界调用 worker。程序编辑属于元数据提交；清理/启动/撤销运行实例等会改变世界的宿主操作延后世界发布，直到下一完整模拟步。渲染预算登记和只读视图的生命周期处理也只允许在同步边界。

WorldWill 对 Queued 保持等待状态，查询终态后才记录 Applied/Rejected；不会重复发起同一提案。帧末join后结清已完成动作和待发布的取消结果，不轮询模型或接受新提案，使紧接着的关闭保存包含祈祷终态。取消、超时和权限撤销可以移除尚未执行的命令；若宿主仍在延期发布，则保留取消原因和活动记录，等完整提交后一起写入持久祈祷及控制器结果。恢复权限或晚到回复不能复活已撤回的提案，完成的动作不会被误记成取消。DebugUI 和 JSON 回执明确显示 Queued。

## 诊断与验证入口

PerfTest 的 `simulation.tick` 及世界/权威角色指标在独立 collector 中采集，join 后合并到同一应用帧。Trace 借用引擎的通用 sink：渲染 lane 1、游戏和玩家呈现 lane 2、模拟 lane 3。只有主线程输出应用帧标记。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
cmake -S . -B build-parallel-tsan -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_CXX_FLAGS=-fsanitize=thread -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-parallel-tsan --target PixelSimulationThreadTests PixelWorldWillTests PixelParallelFrameTests AxiomFramePipelineTest -j4
ctest --test-dir build-parallel-tsan -R '^(Axiom.UnitTest.FramePipeline|Pixel.SimulationThread|Pixel.ParallelFrame|Pixel.WorldWill)$' --output-on-failure
(cd Pixel && ../build/tests/PixelRuntimeSmoke --parallel-only)
(cd Pixel && PIXEL_WORLD_GENERATOR=flat PIXEL_WORLD_SEED=1 ../build/tests/PixelCloseSmoke)
```

`Pixel.SimulationThread` 覆盖常驻 worker、并发交接、双侧异常 join、排队与去重、队列上限、取消/重置、保存/暂停、固定步、预算重试和游戏侧角色呈现。`Pixel.WorldWill` 增加真实队列的完成、取消，以及延期发布期间撤权/超时后的持久结算；`Pixel.Observation` 检查连续延期发布时知识与世界快照同版本。GPU 并行专项检查实际命令的延迟生效和冻结画面。

## 2026-10-30 验证记录

本机 Apple M5 / macOS，Debug 构建。引擎通用 trace sink 支持提交为本地 `PixelMain` 的 `dd1b3423afa14eba4f68b48acb5b555e9d4b4147`，父仓库子模块引用与 `AxiomVersion.json` 同步；未推送远端。Pixel 专用实现与测试均留在父仓库。

| 范围 | 实际结果 |
| --- | --- |
| CPU 全量 | 首轮 121 项中 120 项通过；新增预算压力夹具未正确耗尽预算，修正后失败项复测通过。后续角色地形检查复用及动作修改由 7 项专项复验通过，最后 WorldWill/Worker/InterventionCheckpoint 3 项通过。 |
| ThreadSanitizer | FramePipeline、ParallelFrame、SimulationThread、WorldWill 四项通过；后续 Body 与 WorldWill 修改分别复测对应专项通过，没有报告数据竞争。 |
| 动态库 | 共享库配置下 FramePipeline、Trace、DiagnosticsLinkage、SimulationThread 四项通过。 |
| 运行时构建 | Pixel、AET（`Axiom` target）、PixelRuntimeSmoke、PixelCloseSmoke 编译通过。 |
| 原生 GPU | 并行冻结帧、命令延迟生效、背包、角色动画/运动、地形、元胞材料、背景、风、木头人、世界输入、构式/设备、育圃及离线援助画面读回通过；两轮窗口缩放/最小化恢复通过。最终并行专项再次通过。各专项分批执行，不声明完整 smoke 一次运行通过。 |
| 保存关闭 | 固定平坦生成器与 seed 下通过排队动作延期关闭、预算与 I/O 失败保留会话、显式重试、活动构式恢复及析构不重复保存。随机自然地形下固定发射目标的启动断言曾失败，固定场景复测通过；没有把随机场景的该次结果记为通过。 |
| 原生 UI 限制 | 环境/设备/内存面板与构式编辑器曾分别通过。后续复测中 GLFW 原生光标保持 `(54,532)`，未到达请求位置；夹具已增加焦点请求和坐标核验，仍无法完成定位。因此最终四项 UI 复测未通过，育圃/援助 UI 不声明通过。其领域与 GPU 画面链路已单独验证。 |

PerfTest 实际采集了 2 个预热帧及 12 个记录帧：14 次 `engine.game_tick` 和 `game.player_presentation` 位于 lane 2，14 次 `simulation.tick` 位于 lane 3，14 次 `engine.render` 位于 lane 1。全部 scope 开闭配对，应用帧标记仅 14 对，CSV 有 12 行。该结果验证三线程接线和诊断合并，没有做可归因的性能提升基准。

本轮原始日志位于 `/private/tmp/pixel-simulation-*`，其中 CPU 全量为 `pixel-simulation-cpu-tests.log`，最终 TSan 四项为 `pixel-simulation-tsan-final-tests.log`，后续结清测试为 `pixel-simulation-settle-{tests,tsan-tests}.log`，最终并行与关闭为 `pixel-simulation-gpu-last-parallel.log` / `pixel-simulation-last-close.log`，采样为 `pixel-simulation-runtime.csv` / `.trace.json`。

### 独立审查后的两项修复

Astra / ultra 独立审查确认两项 P2：延期发布时撤销排队提案会提前删除活动记录，导致持久祈祷永久停留 Thinking；原生观测在返回旧提交后重新捕获实时知识，会触发提交版本异常。修复分别保留待结算的取消原因，并将知识纳入观察缓存的完整提交。新增回归在修复前均失败，分别报告提前退役祈祷和 `knowledge snapshot does not belong to this commit`。

修复后由独立验证代理重新构建并运行：

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4
cmake --build build-parallel-tsan --target PixelObservationTests PixelGameplayObservationTests \
  PixelKnowledgeTests PixelKnowledgeViewTests PixelWorldWillTests PixelWorldWillWorkerTests \
  PixelInterventionCheckpointTests PixelSimulationThreadTests -j4
ctest --test-dir build-parallel-tsan \
  -R 'Pixel\.(Observation|GameplayObservation|Knowledge|KnowledgeView|WorldWill|WorldWillWorker|InterventionCheckpoint|SimulationThread)$' \
  --output-on-failure -j1
```

CPU 全量 **121/121** 通过，ThreadSanitizer 相关专项 **8/8** 通过且未报告数据竞争；主代理运行 `cmake --build build --target Pixel -j2`，运行时重新编译通过。此轮未重新运行原生 GPU/UI smoke，也未执行 TSan 全套。日志位于 `/private/tmp/pixel-review-fixes-{build-cpu,ctest-cpu,build-tsan,ctest-tsan,runtime-build}.log`。

Astra / ultra 随后复核上述修复及验证证据，确认两项 P2 均已闭合，未发现新增可操作缺陷；复核包含暂停、超时、撤权与恢复权限、帧末保存、关闭、epoch 生命周期、知识容量及模块依赖。
