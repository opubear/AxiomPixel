# Pixel 适配重构后的 devmain（2026-10-16）

适配前 Pixel 基线为 `bcb4cd2`（默认暂停与空风场性能记录），引擎上游基线为 `devmain` 的 `0671a0eeca6d563f32bee91bc12aa31a515fefee`。后续按用户要求提交通用修复并切换到已 rebase 的 `PixelMain`，当前引擎为 `bf88a6ec235b6931fbdedb81db986dca74d5653a`，由 `AxiomVersion.json` 锁定。相邻 `../Axiom` 未修改或构建。

## 新引擎设计与游戏接入

| 责任 | devmain 契约 | Pixel 接入 |
|---|---|---|
| 进程入口 | 通用 `Source/main.cpp` 解析命令，帮助请求在创建应用前返回 | `EntryPoint.cpp` 实现 `CreateApplication()`，返回 `PixelApplication` |
| 应用所有权 | `Axiom` 拥有 Game、DebugUI 与 Engine；`CreateGame()` 为纯虚函数 | `PixelApplication::CreateGame()` 创建 PixelGame 并配置性能采集 |
| 引擎服务 | `GameEngine` 拥有平台、InputManager、资产、RHI、渲染器及可选场景 | 游戏在 `ConfigureEngine()` 注册输入并调用 `SetRender()` |
| 渲染策略 | 工厂接收 RHI 和可空场景，`Enable3DScene` 控制场景创建 | Pixel 保持无 3D 场景，消费独立 CPU 世界快照 |
| DebugUI | `AxiomSetup.EnableDebugUI` 控制 UI；从 `Axiom::GetDebugUI()` 借用 | Pixel 注册面板内容；PixelRender 在后处理后调用继承的 `RecordOverlay()` |
| 输入 | InputManager 向监听者广播，UI 先消费原始事件，再按捕获状态派发帧事件 | Pixel 显式注册帧回调，将 Clear/Key/MouseButton/MouseMove 转给 GameInput |
| 配置 | `GameConfig` 拥有 Root 和必需的 Window 配置 | `Pixel/Config/GameConfig.json` 指定 800×600、标题 Pixel |
| 链接 | Application → Runtime → VulkanBackend → ShaderInterface | PixelGame 链接 AxiomApplication；CPU 表现层链接 Axiom::Core；支持静态和共享库 |

`PixelApplication` 不导入 AET 样例。独立 Formula、World、风场和模拟算法未改动。旧模块设计文档继续描述游戏状态所有权；涉及应用入口、引擎/UI 所有权和链接目标时，以本文为准。

## 帧循环与关闭

就绪帧仍按以下顺序执行：事件采集 → acquire → PixelGame::FrameBegin（性能采样、视口）→ Engine::PrepareFrame（PixelRender 发布快照、UI 建帧）→ 输入派发 → 游戏 Tick → 渲染 → present → PixelGame::FrameEnd。

未就绪帧清理持续输入，跳过发布、模拟和绘制。Draw 仅消费已发布的快照，同帧后续游戏修改在下一就绪帧才可见。主流程与测试统一通过 Axiom 驱动这些钩子，不能只调用 Engine 的 acquire/Draw 而漏掉应用侧工作。

关闭顺序为断开输入/overlay → GameEnd → GPU idle → 释放 DebugUI/Game → 清理 Engine。渲染器仍可能持有游戏发布回调直到清理，但清理只销毁回调，绝不调用已释放的接收者。GPU smoke 覆盖多轮独立应用生命周期、最小化恢复和窗口 resize。

## 已提交的引擎修复与 PixelMain

引擎提交 `2dbfebc6a3f2a5e7c488905c5d8dfa2ed0a54b39` 包含两项通用修复：

1. 恢复事件时鼠标快照：`FrameInput::Event` 携带位置、window/framebuffer 尺寸，InputManager 在按钮到达时采集。`QueueMouseButton` 保留 devmain 原来的第四参数 `uiEventId`，在第五参数追加可选快照，既有调用方继续兼容。CPU 测试覆盖 UI 延迟消费、快照复制和捕获下的 release；Pixel GPU 测试覆盖同批多次点击后移动光标、HiDPI 及 resize 选格。
2. 为引擎 GameConfig 测试指定引擎源码工作目录。嵌入父工程后，默认 build/Axiom 目录向上找不到 `.axiomroot`，原测试无法运行；指定工作目录后，原配置校验测试通过。

引擎中没有 Pixel 类型、shader 或游戏测试。上述修复已提交到本地 devmain；刷新远端后，确认 origin/devmain 仍为 `0671a0e`，随后将 PixelMain rebase 到含此修复的 devmain 并切换实际工作副本。当前 PixelMain 比 origin/devmain 领先两个提交、落后零个，保留原分支的向量叉积、四元数归一化、交换链格式选择与回归测试。冲突处合并了鼠标快照测试，同时覆盖光标移动、DPI/尺寸变化、UI 延迟与捕获下的释放。

锁定版本无需额外补丁，原临时补丁导出已移除。引擎提交尚未推送；旧分支保留在 `codex/backup-pixelmain-before-devmain-20261016` 和 `codex/backup-pixelmain-before-input-commit-20261016`。引擎工作区仅保留原有未跟踪的空 AWT 任务文档索引，未将过程文件混入代码提交。

## 验证

- 全新 CPU Debug：`cmake -S . -B build-devmain-cpu -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug`，构建及 CTest **41/41** 通过，包含 Pixel、Formula、引擎 CPU/mock 与 Core。
- 静态 Debug：Pixel、PixelRuntimeSmoke、Axiom 及 CTest 对应目标构建成功；`ctest --test-dir build --output-on-failure` **46/46** 通过。
- 静态 GPU：在 Pixel 工作目录运行 `../build/tests/PixelRuntimeSmoke`，完整通过风效/材质/角色像素读回、F1 力场、点击和相机、UI 捕获/风策略/内存面板、resize/minimize 与两轮生命周期。旧测试中需要模拟运动的场景显式恢复模拟，并验证启动与 R 重置默认暂停。
- `Pixel --help` 从 `/private/tmp` 且 `PIXEL_PROFILE_FRAMES=invalid` 时仍退出 0，证明帮助不会加载游戏配置、创建游戏或窗口。

- 共享库 RelWithDebInfo：全新 `build-devmain-shared` 配置启用 `AXIOM_BUILD_SHARED_LIBS=ON`，复用已构建的 vendored slangc。Pixel、PixelRuntimeSmoke、Axiom、AxiomEngineBoundaryTest、AxiomLinkageTest 构建成功；边界/链接/两个帮助测试 **4/4** 通过；完整 PixelRuntimeSmoke 也通过相同六组 GPU 验证。
- 真实入口：`run.sh` 启动静态 Debug，直接启动共享库 RelWithDebInfo；两者均使用 `PIXEL_PROFILE_FRAMES=8 PIXEL_PROFILE_WARMUP=2`，输出 8 行采样、完成 10 帧并正常退出 0。采样中材料模拟步数为零，保持默认暂停；此短采样仅验证入口和帧钩子，不用作性能比较。

共享库复现示例（`/absolute/path/to/slangc` 替换为实际兼容编译器，省略该选项则构建内置编译器）：

```bash
cmake -S . -B build-devmain-shared -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DAXIOM_BUILD_SHARED_LIBS=ON -DPIXEL_BUILD_SMOKE=ON \
  -DAXIOM_SLANGC_EXECUTABLE=/absolute/path/to/slangc
cmake --build build-devmain-shared --target Pixel PixelRuntimeSmoke Axiom \
  AxiomEngineBoundaryTest AxiomLinkageTest -j4
ctest --test-dir build-devmain-shared \
  -R 'Axiom.UnitTest.(EngineBoundary|Linkage)|RuntimeHelp' --output-on-failure
(cd Pixel && ../build-devmain-shared/tests/PixelRuntimeSmoke)
```

本机为 Apple M5 / MoltenVK；`VK_LAYER_KHRONOS_validation` 不可用，GPU 像素与生命周期验证不等同于 validation layer 检查。尚未在 Windows/Linux 或多配置生成器实测。

## 切换到 PixelMain 后的复验

以下结果来自实际 `Axiom/` 工作副本的 `bf88a6e`，不是之前临时 rebase 工作树的测试：

- 重新配置并构建静态 Debug 的 Pixel、Axiom、PixelRuntimeSmoke、AxiomLifecycleSmoke 及全部 CTest 对应目标；CTest **46/46** 通过，包含此次合并的鼠标快照、数学和交换链 mock 回归。
- 重新配置并构建共享库 RelWithDebInfo 的 Pixel、Axiom、PixelRuntimeSmoke、引擎边界与链接测试；相关 CTest **4/4** 通过。
- 静态与共享库各运行完整 PixelRuntimeSmoke，六组 GPU 验证全部通过，均正常退出 0。
- 从引擎目录运行 `../build/tests/AxiomLifecycleSmoke`，覆盖 acquired-frame 关闭、无 DebugUI 应用、两轮 AET 生命周期、场景/资源测试、resize/minimize/restore，输出 `lifecycle smoke passed` 并退出 0。
- 真实 `run.sh` 入口以 2 帧预热和 8 帧采样运行，完成 10 帧、导出 8 行 CSV、保持默认暂停并退出 0。采样仅确认启动和帧钩子，不作为性能基准。
- Git 核对：当前分支为 PixelMain，HEAD 与版本文件一致，origin/devmain 是 HEAD 的祖先；已跟踪引擎文件无未提交修改。父游戏仓库的适配改动仍未提交。

本轮日志位于 `/tmp/pixel-pixelmain-*-20261016.log`，真实入口采样为 `/private/tmp/pixel-pixelmain-entry-20261016.csv`。平台与 validation layer 限制同上。
