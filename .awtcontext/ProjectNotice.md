# AxiomPixel — 项目注意事项

- C++20、CMake 3.23+、Slang/Vulkan；CPU/mock 测试无需窗口或 GPU。
- 当前工具入口为 `../AIWorkTask/awt`，状态只通过 AWT CLI 更新。
- `../Axiom` 只读；本地 `Axiom/` 是来自远端 `PixelMain` 的 Git 子模块，准确版本见 `AxiomVersion.json`；当前已包含鼠标事件快照，Pixel 呈现变化回调已适配带事件参数的接口，AET 逻辑留在引擎。
- 两个 Git 仓库分别检查状态和差异；父仓库通过 `.gitmodules` 和 gitlink 跟踪 Axiom 的来源与提交，不直接跟踪其源码文件。
- World 不出现纹理索引或 RHI 类型；Gameplay 类型通过 PixelWorldPresentation 显式映射。
- Source 的当前归属为 Runtime/Gameplay、Simulation、Render、System 及 Dev/Debug；以工程 README 的模块表和依赖合同为准。输入处理归 System/IO/Input，PlayerInput 保留 Gameplay/Actors。System 通用服务不含游戏领域类型，IO/Input 单独适配 Gameplay；Gameplay 核心、Simulation 及其余 System 服务不反向依赖 Input，Gameplay 核心和 Input/Camera 不依赖渲染实现。
- 保留帧首发布、按区域版本复制、Skip 不推进游戏、Draw 不读取实时游戏状态的契约。
- 不修改第三方源码或锁定版本。准备依赖可初始化本地引擎的锁定子模块。
- 测试区分 CPU/mock、编译、GPU 与视觉；未执行项不能据历史记录宣称通过。
- 核心组合玩法统一为“构式（Formula）”，模块为 Pixel/Source/Runtime/System/Formula，示例扩展名为 .formula。当前默认采用 formula/2 函数组合，继续读取 process/1；现行语法与游戏合同见 [模块README](../Pixel/Source/Runtime/System/Formula/README.md) 和 [FormulaBridge](GlobalDoc/FormulaBridge.md)，旧cast语言与树形编辑器已删除。
- 构式设计定位为组织游戏API调用的逻辑层，全部世界作用限于游戏提供的能力。函数式组合、统一函数接口与封装、世界元信息metadata见[构式系统](../Docs/GameDesign/FormulaSystem.md)；已实现的最小语法、绑定、生命周期及兼容保存见[formula/2合同](../Docs/Architecture/FormulaComposition.md)；并行散射等扩展仍待设计。
