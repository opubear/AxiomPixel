# 字体、本地化与 Pixel 菜单联合审查（2026-11-11）

状态：本次变更的审查与验证证据，不能替代后续版本的验证。范围限于 AxiomPixel 及其 Axiom 子模块；相邻 Axiom 参考仓库未修改、构建或写入 Git/AWT。

## 范围与分工

- 独立审查者：`gpt-6-astra`，`ultra`，干净初始上下文；读取原始需求、工程约定、完整差异及上下游实现。修复后由同一审查者继续复核。
- 独立验证者：`gpt-6-luna`，`high`；执行构建、CTest 和真实窗口 smoke，不修改生产代码或测试预期。主 agent 核实发现、讨论行为合同并实施修复。
- Axiom 原始范围：`46ca20c09f6d6d09c872dc9974a720e3e850d34e..f96ed78090265172230972e2dcb75b72c9455d7a`，完整审查 `92a3f57` 字体渲染与 `f96ed78` 本地化两个提交；最终另复核 `b8b2403b76328b7c1542aa7ddfaf44b5bb355323` 验证脚本修复。
- Pixel 范围：相对 `3eda730db8210ab3a6070062e4b17aa285a61053` 的全部已暂存、未暂存和新增文件，包含七个新 Canvas、CanvasPainter、GameMenuUI、CanvasOverlay 文件，不只检查 Git 默认 diff 可见的文件。
- 引擎检查涵盖 FreeType/HarfBuzz 源码子模块和构建、字体加载、UTF-8、塑形与图集、TextRenderer 帧槽/上传/资源寿命、RHI/Vulkan 顶点布局与混合、shader、LocalizedString、语言表校验/回退/原子重载及静态共享导出。
- 游戏检查涵盖原生菜单与背包共用画布、输入捕获与事件快照、帧间线程边界、关闭与保存协商、测试、文档及引擎版本锁定。

## 发现、讨论与处理

### R1：P2，既有干净构建验证入口不完整

`Axiom/Scripts/CheckBuildTargets.sh` 维护部分测试目标清单，却执行全部 `Axiom.UnitTest.*`。在全新目录原样运行，配置与显式目标构建成功，CTest 只有 33/44 通过；10 项因缺少可执行文件 Not Run，Log 包装测试同样因缺少 `AxiomLogTest` 失败。旧构建目录中的遗留产物会掩盖问题。

缺少的目标涉及 FramePacer、FramePipeline、Guid、Trace、Command、LogFile、Log、Diagnostics、DiagnosticsAllocation、Platform 和 PlatformData。共享模式还需要条件创建的 DiagnosticsLinkage，因此仅补齐 11 个名字不足以解决清单漂移。

此问题早于本轮字体/本地化提交。主 agent 与 Astra 确认修复已触及的验证入口合理；sample 模式仍保留明确的运行时目标，避免额外构建无关第三方默认工具。新增由 UnitTest 自己维护的 `AxiomUnitTests` 聚合目标，收集本目录及 Platform 测试子目录的普通目标，包含公开头编译检查与共享条件目标。`EXCLUDE_FROM_ALL` 的 alignment 拒绝夹具仍由 CTest 单独触发。

修复后全新静态脚本 44/44、全新共享脚本 45/45、Pixel 中嵌入运行时配置的引擎测试 48/48 通过。构建合同见 [Axiom 构建指南](../../Axiom/.awtcontext/GlobalDoc/BuildAndValidation.md) 和 [UnitTest 入口](../../Axiom/UnitTest/README.md)。

### R2：P3，菜单测试未覆盖真实编辑控件捕获

原 `GameMenuInteraction(true)` 先关闭默认可见的 F2/F4 面板，无法证明正在编辑文本时 Escape 的行为。独立 ImGui 加生产 FrameInput 探针确认：第一次 Escape 取消编辑，松开后再按可打开菜单，之后可正常关闭；没有菜单卡死或输入恢复缺陷。

主 agent 与 Astra 对照既有输入合同及菜单需求后，保留 ImGui 编辑捕获的优先级。新增 `GameMenuEditingCapture`，通过真实 Formula Editor 的 `Process text` 控件建立编辑状态，明确断言 ActiveID、InputTextState 和 WantCaptureKeyboard，再通过生产 Platform/InputManager 发送按键；验证首次 Escape 只取消编辑、下一次打开菜单并隐藏开发面板、再次关闭后恢复面板。没有替换生产回调，也没有直接调用菜单切换来模拟成功。

完整 `PixelCloseSmoke` 与 `--menu-only` 均包含此用例并通过。输入优先级已写入 [关闭与菜单合同](../../Docs/Architecture/CloseRequests.md)、README 与呈现系统说明。此项修复增加验证与说明，未改变输入行为。

## 验证证据

环境：macOS、AppleClang 21、Debug、Apple M5、MoltenVK。命令从 AxiomPixel 根目录运行，特别注明者除外。

| 阶段 | 实际验证 | 结果 |
| --- | --- | --- |
| 审查基线 | `build-text-cpu` 完整构建及 CTest | 44/44 |
| 审查基线 | `build-text-shared` 完整构建及 Text/Localization CTest | 2/2 |
| 审查基线 | Pixel 及相关测试、Axiom 运行时与 smoke 目标构建；Input、InventoryUI、Actions、DeviceRendering、FormulaRendering、CharacterAnimation、Pixel.Cpu、ModuleBoundaries、ApplicationClose、Linkage | 10/10 |
| 审查基线 | `PixelRuntimeSmoke --inventory-ui-only`、`AxiomTextSmoke`、`AxiomCloseRequestSmoke`；菜单及完整保存关闭 smoke | 全部退出码 0 |
| 缺陷复现 | `Axiom/Scripts/CheckBuildTargets.sh cpu /private/tmp/axiom-review-fresh-cpu`，不手动补构建 | 退出码 8，33/44，10 Not Run、1 Failed |
| 修复验证 | `Axiom/Scripts/CheckBuildTargets.sh cpu /private/tmp/axiom-review-fixed-cpu` | 44/44，退出码 0 |
| 修复验证 | `Axiom/Scripts/CheckBuildTargets.sh cpu /private/tmp/axiom-review-fixed-shared -DAXIOM_BUILD_SHARED_LIBS=ON` | 45/45，退出码 0，含 DiagnosticsLinkage |
| 修复验证 | Pixel `build` 中构建 Axiom、Linkage、EngineBoundary、ApplicationClose、GameConfig 与 AxiomUnitTests；`ctest --test-dir build -C Debug -R '^Axiom\.UnitTest\.' --output-on-failure` | 48/48，退出码 0 |
| 修复验证 | 重编译 `PixelCloseSmoke`；以 `Pixel/` 为工作目录执行 `../build/tests/PixelCloseSmoke` 和 `../build/tests/PixelCloseSmoke --menu-only` | 均退出码 0；真实文本捕获、面板恢复、Deferred/I/O 失败拒绝、显式重试及保存恢复通过 |

原始日志位于本机 `/private/tmp/axiom-pixel-review-20261111/`；关键文件为 `axiom-script-fresh-cpu.log`、`fixed-cpu-script.log`、`fixed-shared-script.log`、`runtime-axiom-ctest.log`、`pixel-close-fixed-full.log`、`pixel-close-fixed-menu.log`。审查前工作树快照位于 `/private/tmp/axiom-pixel-review-baseline/`。临时路径记录本轮来源，不是仓库运行依赖。

既有 Pixel 构建树第一次同时请求五个运行时目标和新增 `AxiomUnitTests` 时，前五个构建成功，但 Make 报新增目标无规则并返回 2。随后单独构建聚合目标成功，再次执行原多目标命令也全部成功，CTest 为 48/48。日志分别为 `runtime-axiom-targets-build.log`、`runtime-axiom-unit-target-build.log`、`runtime-axiom-targets-build-rerun.log`。这可能与旧 Makefile 首次自动重新生成有关，未将推测当作已确认原因；显式配置后的两套全新 CPU 脚本均一次通过。

两仓 `git diff --check` 通过。引擎修复提交保留在本地 PixelMain，Pixel 的子模块 gitlink 与 `AxiomVersion.json` 同步到 `b8b2403`。Pixel 工作区其他修改保持未提交；本轮没有推送。

## 结论与限制

首轮未确认字体、本地化或 Pixel 菜单的 P0/P1/P2 生产缺陷；确认一项既有 P2 验证入口问题与一项 P3 测试/说明缺口，并完成上述修复和独立回归。同一 Astra 审查者读取最终代码和新日志后确认两项关闭，无实质未决意见。

- GPU 日志明确报告 `VK_LAYER_KHRONOS_validation is unavailable`，真实 GPU smoke 不代表 validation layer 诊断通过。
- 本轮未做 Windows/Linux 运行、多配置生成器、完整 Pixel 全量测试或所有 GPU 专项；sample 脚本的运行时目标在现有 Pixel 配置核验，未另建全新 runtime/sample 构建树。
- 旧 `CpuTests.cpp` 第 277/279/281/283 行的四条 nodiscard 警告来自 `99f3bee7`，不是本轮引入；没有为消除旧警告扩展修复范围。
- PixelCloseSmoke 链接器报告重复的 `libImGuid.a` 链接项被忽略，构建和运行均成功。
- 引擎字体和本地化的功能边界仍按各自合同：Pixel 现有菜单/背包文案尚未迁移，多语言段落双向算法、字体回退和自动换行不属于本次交付。有限审查与测试不能证明所有平台或任意输入均无缺陷。
