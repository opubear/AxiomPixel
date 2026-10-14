# AxiomPixel — 项目注意事项

- C++20、CMake 3.23+、Slang/Vulkan；CPU/mock 测试无需窗口或 GPU。
- 当前工具入口为 `../AIWorkTask/awt`，状态只通过 AWT CLI 更新。
- `../Axiom` 只读；按用户最新要求，本地 `Axiom/` 的通用能力移植和修改使用 PixelMain 分支，AET 逻辑留在原位置；旧 PixelMain 分支已由当前合并状态替换。
- 两个 Git 仓库分别检查状态和差异；父仓库不跟踪 Axiom/ 内容。
- World 不出现纹理索引或 RHI 类型；Gameplay 类型通过 PixelWorldPresentation 显式映射。
- 保留帧首发布、交换后回填、Skip 不更新、Draw 不读取实时游戏状态的契约。
- 不修改第三方源码或锁定版本。准备依赖可初始化本地引擎的锁定子模块。
- 测试区分 CPU/mock、编译、GPU 与视觉；未执行项不能据历史记录宣称通过。
- 核心组合玩法及代码统一为“构式（Formula）”，模块为 Pixel/Formula，类型/目标为 Formula/PixelFormula，示例扩展名为 .formula；cast 仍表示激活构式。设计与实现边界见 GlobalDoc/FormulaSystem.md 和 FormulaLanguageArchitecture.md。历史提案中的费用、预算与世界接入尚未实现。
