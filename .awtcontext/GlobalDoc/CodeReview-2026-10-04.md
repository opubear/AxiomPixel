# 当前 Pixel 代码审查（2026-10-04）

审查确认 3 项 P2 缺陷，未在检查范围内发现 P0/P1。审查阶段的 CPU、内存检查和本机 GPU 回归通过，但没有覆盖下面三种触发条件。用户随后授权修复，F1–F3 已完成，当前行为及新增验证见文末“修复交付”；下文保留原审查证据。

## 审查基线与范围

- 主仓库为 `main`，HEAD `5eb2c9e`；Pixel、CMake、README 等业务文件尚未提交，因此按当前工作区全量审查，不把 Git diff 当作完整范围。
- 内嵌引擎为独立仓库 `Axiom/`，分支 `PixelMain`，HEAD `bfb26c97ff1cea01346e4c0442c93ee5b4c43fbe`，与 AxiomVersion.json 一致。其既有 AWT 改动保留。
- 逐文件检查全部 `Pixel/Source/`、三个 shader、六个测试源文件、两个 CMake 文件、Config/Content，以及项目约定和历史需求。
- 引擎审查集中于 Pixel 使用的调用链：启动/销毁、FrameClock、输入采集与派发、RenderPolicy、shader 构建与加载、Vulkan 图像/上传/descriptor/pipeline、帧提交及交换链重建。没有对独立引擎中不被 Pixel 使用的资产导入、3D World、数学和 ImGui 实现再做全量逐行审计；AET 做了构建和现有生命周期回归。
- 未审计第三方依赖内部实现，未写入相邻只读 `../Axiom` 或 AWT 工具仓库。未启用 Subagent/独立审查。
- AWT 初始检查无问题，审查任务上下文 `truncated=false`、`omittedSources=[]`。

## F1：点击派发时重读光标，导致编辑错误格子

**P2；真实 GLFW 窗口与生产输入链复现。** 位置：`Pixel/Source/PixelGame.cpp:65–69`，关联 `Axiom/Source/InputManager.cpp` 和 `Axiom/Source/Input/FrameInput.h`。

鼠标回调先把按下/释放事件排队；`MouseButton` 事件未保存点击坐标。游戏稍后在 `OnMouseButtonEvent` 中调用 `glfwGetCursorPos`，读到的是派发时的位置。只要同一批事件中点击之后还有移动，放置/删除就落在后来的光标位置。普通光标模式又会过滤 `MouseMove` 的游戏派发，因此目前不能依靠游戏收到的移动事件恢复按下位置。

复现使用默认视图和实际 1600×1200 framebuffer：移动到格子 `(512,508)`，通过 `PlatformInterface::OnButtonCallback` 排队左键按下/释放，再移动到 `(520,508)`，最后调用一次 `InputManager::Dispatch`。期间不推进 World，不经过渲染推断位置。

```text
cursor wanted=404,272 actual=404,272 focused=1 framebuffer=1600,1200
cursor wanted=468,272 actual=468,272 focused=1 framebuffer=1600,1200
queued_click_at_512_then_move_to_520: cell512=0 cell520=10
```

`0` 为空，`10` 为土块：应修改的格子仍空，后来的光标位置出现土块。现有 GPU smoke 在每次点击后立即 Dispatch，因此遗漏这一批处理情形。

**建议：** 在采集鼠标按下事件时保存位置，通过通用输入事件传给游戏；明确窗口/framebuffer 坐标转换的时点。增加“点击 A → 移动 B → 一次 Dispatch”以及同批多次点击的回归。

## F2：局部风脉冲会截断范围外的高风速

**P2；生产 WindField CPU 复现。** 位置：`Pixel/Source/World/WindField.cpp:86–91`，关联初始化 `:45–49`。

`Reset` 分别接受绝对值不超过 1000 的背景风和不超过 1000 的扰动，二者相加后可合法超过 1000。`AddImpulse` 遍历全场，不论空间权重和增量是否为零，都把两个速度分量 clamp 到 ±1000。于是调用局部脉冲会修改没有受到脉冲作用的位置，甚至零强度脉冲也会改变整个场。

复现：1024×424 默认分辨率/种子，`Ambient={1000,0}`、`GustSpeed=1000`、黏性和松弛为零。采样远处 `(512,225.25)`，调用 `AddImpulse(0,0,1,{0,0})` 后再次采样，未调用 Step：

```text
zero_impulse_far_sample before=1946.56,872.755 after=1000,872.755
```

该位置远在半径外，X 分量仍被截断约 946.56 格/秒。这违反“局部脉冲”的语义，会污染背景风和后续演化。当前默认低风速配置不会在初始化后立即触发这一例。

**建议：** 只更新真正受到脉冲作用的分量，并保证零增量不改变场；若需要全局速度上限，应在初始化/演化阶段明确定义并一致执行。增加大但合法风速下的零脉冲和范围外不变性测试。

## F3：交换链选择忽略可用的 RGBA sRGB，Pixel 因此拒绝启动

**P2；生产格式选择函数的合成能力输入复现，未在对应硬件上运行。** 位置：`Axiom/Source/HAL/Vulkan/VulkanSwapChain.cpp:128–135`；拒绝路径在 `Pixel/Source/PixelRender/PixelRender.cpp:44–45`。

格式选择只优先查找 `B8G8R8A8Srgb + SrgbNonlinear`，找不到就取列表首项。Pixel 接受 BGRA 和 RGBA 两种 sRGB 格式，但不接受 UNORM。因此当列表首项是 RGBA UNORM、后面提供 RGBA sRGB、没有首选 BGRA sRGB 时，仍选中 UNORM，随后 `PixelRender::Initialize` 抛出 `2D rendering requires an sRGB presentation target`。设备已有可用格式，应用仍启动失败；重建时的相同情况也会触发拒绝。

直接调用实际 `ChooseSwapSurfaceFormat`，输入按顺序为 `R8G8B8A8Unorm`、`R8G8B8A8Srgb`，两者颜色空间均为 `SrgbNonlinear`：

```text
surface_format_selected=R8G8B8A8Unorm srgb_available=R8G8B8A8Srgb
```

**建议：** 选择过程考虑应用支持的格式集合，至少在取任意首项前尝试 RGBA sRGB。测试不同列表顺序、仅 RGBA sRGB 可用和确无兼容格式三种情况。这是通用引擎的格式选择问题，修复时无需将 Pixel 类型引入引擎。

## 设计与性能评估

现有分层值得保留：World 只依赖标准库，空间 Force 与物体 Type 分离，Environment 持有连续风速，表现映射集中于 PixelWorldPresentation；帧首发布和交换后回填与引擎顺序一致。移动链使用迭代遍历，避免长链递归；按帧槽配置 GPU 可写资源，销毁时先等待 GPU，再按所有权释放。没有证据支持为风格做大范围重写。

一项已测得的改进机会是默认 Debug 配置的模拟成本。对默认 1024×1024 地形和风场，先预热 10 步、再运行 60 步，纯 World 的本机计时为：

| 编译方式 | 单步平均时间 |
|---|---:|
| `-O2` | 约 8.47 ms |
| `-O0`（默认 Debug 的优化级别） | 约 29.39 ms |

这里只测模拟，不是完整游戏 FPS，也不代表所有设备。Debug 单步已超过 60 Hz 的 16.67 ms 预算，落后时还会执行多个补算步；现有运行时 GPU 验收使用的是 RelWithDebInfo。建议把交互体验验证配置写清楚，并为默认场景建立帧时间基准。若性能仍不足，优先测量稳定土地的全量链遍历与风场迭代成本，再考虑局部优化；全量纹理上传和快照发布是当前明确契约，本轮不将其直接判为缺陷。

测试体系已有较好的语义断言，但三项发现显示还应覆盖：同批输入的事件时序、合法极值参数的局部不变性、设备能力列表的选择结果。周期风、不处理地形绕流、方块没有惯性、有限次压力迭代等均已明确为模型限制，本轮不将它们列作缺陷。

## 本轮实际验证

环境：macOS arm64、AppleClang、本机 Apple M5/MoltenVK。没有把历史验收结果算作本轮执行。

| 检查 | 本轮结果 |
|---|---|
| `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`、构建和 CTest | 5/5 通过，约 2.07 秒 |
| 全部 Pixel CPU/mock 测试及 World/PixelRender 源码，`-O1 -fsanitize=address,undefined` | 通过，未报告 sanitizer 错误 |
| 既有 build-runtime 增量构建 Pixel、Axiom、PixelRuntimeSmoke、AxiomLifecycleSmoke | 全部通过 |
| PixelRuntimeSmoke | 退出 0；默认玩法图像读回、输入/下落/风场/重置及两轮渲染生命周期通过；两轮均观察到最小化并验证恢复 |
| AxiomLifecycleSmoke | 退出 0；两轮通过，各 completed_frames=240，包含既有失败清理及生命周期检查 |
| F1 输入探针 | 实际排队/派发复现错误格子，见上方输出 |
| F2 风场探针 | CPU 复现范围外速度变化 |
| F3 格式探针 | 合成能力列表复现错误选择 |

运行时构建沿用 RelWithDebInfo 和既有外部 slangc：`/tmp/axiom-s23-sample/cmake/SlangCompiler/slang/RelWithDebInfo/bin/slangc`。没有从零构建锁定第三方依赖或内置 Slang。

GPU 首次沙箱内运行无法访问窗口服务并挂起，已停止该进程；随后在获准访问本机图形服务的环境补跑成功。初版临时输入探针未等待窗口聚焦，未能编辑任何格子；增加窗口就绪检查并验证实际光标坐标后才得到 F1 的有效证据。两者属于验证准备问题，不列为源码缺陷。

本机缺少 `VK_LAYER_KHRONOS_validation`，因此 GPU 成功不能证明通过 Vulkan 验证层或同步验证。未覆盖 Windows/Linux、多配置构建、不同 surface 格式硬件、真实 device/surface lost、人工视觉验收及最终 swapchain 编码像素读回。ASan/UBSan 针对 CPU/mock，不覆盖 GPU 驱动。

## 可复现材料

探针源码和脚本保存在 `../完整审查当前Pixel代码Task/Doc/`：CpuProbe.cpp、InputProbe.cpp、SurfaceProbe.cpp、reproduce.py。它们仅为审查证据，不加入产品或默认测试目标；探针输出展示当前行为，退出 0 不代表缺陷对应的行为正确。

在工程根执行（脚本沿用本次 CMake Makefiles 构建目录及其 compile_commands/link.txt）：

```bash
python3 .awtcontext/完整审查当前Pixel代码Task/Doc/reproduce.py
# 需要本机窗口服务，额外运行输入探针：
python3 .awtcontext/完整审查当前Pixel代码Task/Doc/reproduce.py --window
```

本机日志：`/tmp/axiompixel-review-build.log`、`/tmp/axiompixel-review-sanitizer.log`、`/tmp/axiompixel-review-gpu-host.log`、`/tmp/axiompixel-review-aet-host.log`、`/tmp/axiompixel-review-input-probe.log`、`/tmp/axiompixel-review-saved-probes.log`；CTest 日志位于 `build-cpu/Testing/Temporary/LastTest.log`。

三项缺陷在审查交付时均未修复。审查任务完成只表示证据和报告完成，不代表产品没有剩余问题或已验收修复。

## 修复交付（2026-10-04）

AWT 任务：`修复Pixel审查三项缺陷`。用户授权后在当前 Pixel 工作区及内嵌引擎 `PixelMain` 修复，未修改相邻参考仓库、第三方依赖或 shader。

| 缺陷 | 修复后的行为 | 新增回归 |
|---|---|---|
| F1 | InputManager 在鼠标回调时同时保存位置及窗口/framebuffer 尺寸，FrameInput 按值保存并传给游戏。Pixel 直接使用该快照拾取，不再查询最新光标。 | CPU 验证不同位置/DPI/尺寸的事件跨 UI 延迟仍各自保留原值，释放事件在鼠标捕获下仍派发；实际 GPU 验证同批两次放置/删除，随后移动光标再统一派发，且图像读回符合预期。 |
| F2 | 仅对收到非零增量的交错网格分量执行限幅；零脉冲、范围外或另一轴未受力的分量保持原值。 | 正/负大背景风加扰动，零脉冲全场不变；单轴局部脉冲的范围外和另一分量不变；受影响分量仍满足 ±1000 上限；包含周期接缝。 |
| F3 | 通用格式选择优先 BGRA sRGB，其次 RGBA sRGB，两者均要求 SrgbNonlinear；无优选格式保留原有首项后备，空列表明确报错。生产交换链使用同一选择函数。 | 覆盖 RGBA sRGB 位于 UNORM 前/后、仅 RGBA sRGB、BGRA 优先级、颜色空间不同、仅 UNORM 及空列表。Pixel 仍拒绝真正不兼容的格式。 |

新增风场回归在修复前失败于 `zero impulse changed a high-speed field`；新增 GPU 点击回归在修复前失败于 `batched clicks used the later cursor position`。修复后同一组回归通过。F3 原探针已在审查中证明旧选择结果，正式回归直接测试生产共用函数。

本轮实际验证：

- Debug CPU/mock CTest 5/5 通过，包含新输入/格式/风场测试。
- 新建 `build-review/cpu-sanitized`，RelWithDebInfo + ASan/UBSan，完整 CPU/mock CTest 5/5 通过。
- Pixel、Axiom、PixelRuntimeSmoke、AxiomLifecycleSmoke 构建通过；沿用既有外部 slangc 和 RelWithDebInfo。既有重复 ImGui 静态库链接警告仍存在。
- 修复后的 Pixel GPU smoke 退出 0，玩法及两轮生命周期通过，两轮均含最小化恢复。
- AET GPU smoke 退出 0，两轮生命周期通过，各 240 帧。
- 按变更范围格式化，两个仓库 `git diff --check` 通过；引擎旧文件的未修改换行/格式保留。

接口说明：`IGameInterface::OnMouseButtonEvent` 增加第四个参数 `const MousePointerState&`。仓库内 Pixel/AET 均已适配并重新构建；外部游戏实现或直接调用者也需适配。快照类型只包含标准库数据，不引入 GLFW、Vulkan 或 Pixel 依赖。引擎更改仍为本地未提交工作区内容；AxiomVersion.json 的提交号表示原基线，单独检出该提交不包含本次修复，交付时应一并保存内嵌引擎改动。

本机仍没有 Vulkan validation layer；没有跨平台、对应 RGBA-only surface 硬件或人工视觉验收。本次未调整模拟算法或优化级别，审查中的 Debug 性能改进项仍单独保留。历史探针继续作为旧缺陷证据；当前验收使用正式测试。

本机日志：`/tmp/axiompixel-fixes-red-gpu.log`、`/tmp/axiompixel-fixes-runtime-build.log`、`/tmp/axiompixel-fixes-sanitizer.log`、`/tmp/axiompixel-fixes-gpu.log`、`/tmp/axiompixel-fixes-aet.log`。
