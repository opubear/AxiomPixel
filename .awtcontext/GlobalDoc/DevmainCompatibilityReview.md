# 切换 devmain 的兼容性复核

> 最新结果（2026-10-04）：用户已选择将通用能力移入本地 devmain。代码提交 `c7fbb59`，验收记录提交 `6d3afd3`；外部 CPU 5/5、独立引擎 CPU 4/4，Pixel/AET 构建及各两轮 GPU 生命周期通过。Pixel 游戏源码没有修改，当前停留在干净的 devmain，未 push。具体范围与限制见 [引擎移植记录](../../Axiom/.awtcontext/GlobalDoc/DevmainRenderBackport-2026-10-04.md)。下文保留原始 devmain `de013e1` 的失败证据，不再代表当前版本。

日期：2026-10-04。复核标准：本地 `Axiom/` 切到未修改的 `devmain` 后，Pixel 仍能构建并保留现有运行行为。

**结论：当前迁移没有满足这一标准。** 上轮验证针对 PixelMain，不能据此推出对 devmain 兼容。
已实际将本地引擎从 PixelMain `2e03799` 切到 devmain `de013e13078e3b243a99d13f391ece383fd8b0a2`；
配置与 PixelRender 公开头编译均失败。检查后恢复了干净的 PixelMain，没有修改 devmain 或相邻 `../Axiom`。

## 已确认的阻断点

| 范围 | Pixel 仍依赖的能力 | devmain 的状态与影响 |
|---|---|---|
| 构建入口 | 根 CMake 要求 `Render/RenderSetup.h` 存在 | devmain 没有此文件，配置直接失败；连 CPU 测试入口都无法通过 |
| 启动与生命周期 | `RenderSetup`、三参数 `Axiom::Run`、可注入 RenderPolicy、Initialize/FrameBegin/重建/Clear 回调 | devmain 的 Render 直接持有 ForwardRenderPolicy，接口只有 Draw；无法装配 PixelRender，也不会在 Ready 帧首发布 Pixel 快照 |
| 可选 UI/场景与输入 | Engine 可关闭 3D Scene/ImGui；InputManager 在无 ImGui 时安全派发输入 | devmain 无条件建立这些组件，并直接解引用 ImGui；不能保持当前无 UI Pixel 运行路径 |
| 光栅 RHI 与 Vulkan | RHITexture、UploadBuffer、Sampler、图像 descriptor、光栅 pipeline、离屏 Pass、整帧 RecordFrame、状态转换及帧槽查询 | devmain 没有 RHIRasterResources 与 VulkanRasterResources，也缺少对应虚接口和实现；四张纹理上传与两遍绘制无法使用原路径 |
| shader | `AXIOM_APPLICATION_SHADER_SOURCES`、GetShaderInfo、PushConstantBytes 反射 | devmain 只登记引擎的六个 shader，不处理外部 shader 列表，也无新增 CPU shader 查询接口和 push constant 大小字段；迁出的三个 Pixel shader 不会自动参与当前加载流程 |

具体使用点：

- `Pixel/Source/main.cpp:13` 使用 RenderSetup，`:19` 调用带 setup 的 Run。
- `Pixel/Source/PixelRender/PixelRender.h:18` 开始覆写 devmain 不存在的生命周期方法；`:25` 起使用缺失的 RHI 资源类型。
- `Pixel/Source/PixelRender/PixelRender.cpp:42` 起调用扩展 RHI；`:92` 使用 GetShaderInfo；RecordWorld/RecordPost/Draw 依赖显式 Pass 与 RecordFrame。
- `Pixel/Source/PixelGame.cpp` 的发布回调需要 Engine 在 Ready 帧首执行 Render.FrameBegin；复制类定义不能补上这个调度点。
- 本地 PixelMain 的 Engine.cpp、InputManager.cpp、Render/*、HAL/RenderHardwareInterface/*、HAL/Vulkan/VulkanRHI.*、VulkanRasterResources.*、HAL/Shader/* 和 cmake/AxiomShaders.cmake 中仍保留相关新增能力。

## 已分离部分与责任判断

PixelGame、World、WorldState、Environment、WorldGridFrame、表现映射、两遍渲染策略、三个 shader 及 Pixel 测试的源码已迁出。
纯 World 的三个 cpp 只加 `Pixel/Source` include 即可独立通过 C++20 语法编译，不依赖 devmain 或 PixelMain。

剩余依赖并不全是游戏逻辑漏搬：通用纹理资源、Vulkan 同步、shader 反射和渲染策略生命周期属于引擎基础能力，
它们最初随 2DRender 开发加入，但尚未进入当前 devmain。
因此“游戏文件已分离”和“能运行在未扩展的 devmain”是两个不同的完成条件；此前仅证明了前者及 PixelMain 上的运行。

## 实测证据

1. `git -C Axiom switch devmain` 成功，HEAD 为 de013e1。
2. `cmake -S . -B build-devmain-probe -DAXIOM_BUILD_RUNTIME=OFF` 返回 1：`Axiom/ must contain the PixelMain engine checkout`。
3. 用 devmain 的 Source include 直接编译 Pixel/Tests/HeaderCompileCheck.cpp，返回 1：生命周期 override 无对应虚方法，RHITexture/RHIUploadBuffer 类型不存在。
4. 同样直接检查 PixelRender.cpp 返回 1；不是仅删除根 CMake 的保护判断即可解决。
5. World.cpp、WorldState.cpp、Environment.cpp 的独立 `clang++ -std=c++20 -fsyntax-only -I Pixel/Source` 返回 0。

未在 devmain 上执行 GPU 测试，因为配置与编译已阻断；不把此前 PixelMain 的 GPU 通过结果用于本标准。
日志位于 `/tmp/pixel-devmain-configure.log`、`/tmp/pixel-devmain-pixel-header.log`、`/tmp/pixel-devmain-pixel-render.log`、`/tmp/pixel-devmain-world.log`。

## 修复边界待选

- 如果 devmain 必须原样保留，需要在 Pixel 工程提供启动、帧调度、输入、shader 与渲染后端的适配；无法只靠继续移动 PixelGame/World 达成。
- 如果允许演进本地 devmain，应把通用引擎能力单独合入主线，Pixel 专属策略和 World 继续留在游戏项目。这避免在游戏中复制一份引擎后端。

两种方案影响引擎与游戏的长期职责分工，本轮先记录已验证事实，等待用户选择后实施；未对生产代码作兼容性修补。
