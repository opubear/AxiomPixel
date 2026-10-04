# Pixel 从引擎迁移到独立工程

日期：2026-10-04。

> 后续复核：下文最初的构建/GPU 记录限定于 PixelMain。原始 devmain 不兼容；用户随后授权移植通用能力，本地 devmain 的 `c7fbb59` 已补齐接口，并重新通过独立引擎与外部 Pixel 的 CPU、构建和 GPU 验证。当前引擎引用见 AxiomVersion.json，过程见 [devmain 兼容性复核](DevmainCompatibilityReview.md)。

## 历史依据与范围

源自 Axiom `2DRender` 的 `5c8af21aafbc1eba249c0fc7679c3e06f6089cb8`。
相邻 `../Axiom` 工作区只读；本地 `Axiom/` 从原来的 `a7709c6` 切到相同的 2DRender 提交后，建立用户指定的 `PixelMain` 分支。
本轮没有合并相邻 devmain 工作区的未提交修复。
最初引擎适配保存在 PixelMain 本地提交 `2e03799faa09ae6eab9a4c8045472e60ef7f522f`；AxiomVersion.json 已随用户后续指示更新为移植后的 devmain。没有推送到原仓库或远端。

- `470afa8563ee9d0fe9157f2b7d89a215669aae43`：两遍 2D 格子渲染、通用 RHI 资源接口及纯色默认纹理。
- `5c8af21`：PixelGame、纯 gameplay World、Environment、帧首状态交换回填及显式表现映射。
- AWT 读取了「实现首版2D格子渲染策略」「实现PixelGame与GameplayWorld基础层」的上下文与历史；两次构建均 `truncated=false`、`omittedSources=[]`。
- 设计以 `PixelWorldDesign-2026-10-04.md` 为准；10-03 版直接存 TextureId 的方案已被后续历史替代。
- 原 AWT 设计、实现与审查记录保留在引擎 `2DRender` / `PixelMain` 分支历史的 `.awtcontext/GlobalDoc/` 中，是历史证据；不会随本次通用能力提交进入 devmain。其旧路径和历史测试结果不代表本轮实现位置或验证结论。

## 迁移映射

| 原引擎路径 | 当前路径 |
|---|---|
| `Source/GamePlay/Pixel2DSample/World*`、`Environment*` | `Pixel/Source/World/` |
| `Source/GamePlay/Pixel2DSample/PixelGame*`、`main.cpp` | `Pixel/Source/` |
| `Source/GamePlay/Pixel2DSample/PixelWorldPresentation*` | `Pixel/Source/PixelRender/` |
| `Source/Render/Renderer2D/Renderer2DPolicy*` | `Pixel/Source/PixelRender/PixelRender*`，类名同步改为 PixelRender |
| `Source/Render/Renderer2D/WorldGridFrame*` | `Pixel/Source/PixelRender/` |
| `Shader/Pixel2D*.slang` | `Pixel/Shader/` |
| `UnitTest/Tests/Pixel2DTests.cpp`、`PixelWorldTests.cpp`、`Pixel2DRuntimeSmoke.cpp` | `Pixel/Tests/` |

AET 的 `Source/GamePlay/EngineTest/`、共享组件、Forward 策略、通用 RHI 与 Vulkan 后端保留在 Axiom 原位置。
`Content/Engine/DefaultAssets/Textures/SolidColors/` 是引擎默认资源；Pixel 当前用代码生成素材，没有对这些 PNG 的依赖，因此保留在引擎。

## 构建与行为契约

- 父工程配置 Axiom 与 Pixel。引擎的源码、默认 shader、CPU 测试清单不再引用 Pixel；独立构建只编译自己的六个 shader。
- 父工程通过通用 `AXIOM_APPLICATION_SHADER_SOURCES` 传入游戏的三个绝对 shader 路径，沿用引擎的增量编译、反射、manifest 与加载机制。组合构建的 AxiomRuntime 因而使用九个 shader；Pixel 与 AET 可执行程序共享该 runtime。
- `PixelWorld` 只依赖 C++ 标准库。`PixelRender` 只使用 CPU RHI 契约并依赖 PixelWorld；真实应用由 PixelGame 链接 AxiomRuntime，CPU 测试提供 mock backend 和 shader lookup。
- 迁移保留 Ready 帧首的 A/B 交换与回填、Gameplay 到 WorldGridFrame 的显式映射；Draw 只用发布副本。Skip、resize、销毁契约不变。
- Pixel 拥有自己的 `.axiomroot`、Config 与 Content 根描述，运行工作目录是 Pixel。AET 的资源与配置仍在 Axiom。
- 引擎作为子工程时，AET 调试工作目录和 mesh fixture 路径改用引擎自己的 PROJECT_SOURCE_DIR。

## 本轮验证

本机环境为 macOS 27.0.1、Apple M5、AppleClang 21、CMake 4.4.0、MoltenVK 1.4.3。

| 检查 | 本轮结果 |
|---|---|
| 组合工程 CPU/mock | `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`、构建及 CTest 5/5 通过；格式整理后再次通过 |
| 纯 CPU 边界 | 编译命令确认 PixelWorld 只有自身 include；PixelHeaderCompileCheck 不包含 Vulkan/GLFW include |
| 独立引擎 CPU/mock | `cmake -S Axiom -B build-engine-cpu -DAXIOM_BUILD_RUNTIME=OFF -DAXIOM_BUILD_FEATURE_SHOW=OFF -DAXIOM_BUILD_UNIT_TESTS=ON`、构建及 CTest 4/4 通过 |
| 组合运行时 | RelWithDebInfo 构建 Pixel、Axiom、PixelRuntimeSmoke、AxiomLifecycleSmoke 通过；九个 shader 从源编译 |
| 独立引擎运行时 | `cmake -S Axiom -B build-engine` 配置和 Axiom 构建通过；独立 manifest 仅有六个引擎 shader，无 Pixel 源码或 shader 依赖 |
| shader 增量 | 再构建 AxiomShaders 无 shader 重编译 |
| Pixel GPU smoke | 两轮通过：发布状态/类型映射、四页图像读回、alpha、缩放/裁剪、后处理颜色、resize、恢复、无 UI 输入和重复销毁 |
| AET GPU smoke | 两轮通过，各 240 帧，包含初始化失败、场景/材质/对象操作、重载、resize、最小化恢复与关闭 |
| 迁移完整性 | 21 个源码、shader 和测试文件与 5c8af21 做路径/类名替换及 clang-format 后逐一相同；AET/共享组件/第三方源码与基线无差异 |
| 格式 | Pixel C++ 文件 clang-format 检查通过；两个仓库 git diff --check 通过 |
| 命令入口 | Pixel --help 成功；GPU smoke 从 Pixel/ 加载独立 Config，AET smoke 从 Axiom/ 加载原资源 |

运行时配置显式使用之前从引擎 Slang 源码构建的
`/tmp/axiom-s23-sample/cmake/SlangCompiler/slang/RelWithDebInfo/bin/slangc`。
本轮没有重建内置 Slang 编译器。引擎锁定子模块从相邻仓库只读克隆到本地；MoltenVK 依赖缓存复制到本项目构建目录后使用，没有以原仓库作为可写缓存。

沙箱内两个 GPU 尝试分别卡在窗口初始化，且报告 Metal 不可用；已停止相应进程。
上述成功 GPU 结果来自获准访问窗口/Metal 后的实际运行。
Pixel 第一轮未观察到窗口最小化，跳过该断言；第二轮观察到最小化并通过 Skip/恢复检查。
本机没有 VK_LAYER_KHRONOS_validation，不声称通过 validation 层同步/寿命诊断。
没有新增人物/NPC 或环境模拟，也未做跨平台、性能基准、人工视觉或 swapchain 最终编码像素验收。

本机日志：`/tmp/axiompixel-final-cpu.log`、`/tmp/axiompixel-engine-cpu.log`、
`/tmp/axiompixel-runtime-build.log`、`/tmp/axiompixel-final-runtime.log`、
`/tmp/axiompixel-engine-runtime.log`、`/tmp/axiompixel-gpu-unrestricted.log`、
`/tmp/axiompixel-aet-gpu-unrestricted.log`。这些临时日志不是构建依赖。
