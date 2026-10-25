# AxiomPixel 协作入口

- 本仓库维护 Pixel 游戏；`Axiom/` 通过 Git 子模块引入，远端分支为 `PixelMain`，父仓库 gitlink 锁定提交并与 `AxiomVersion.json` 保持一致。
- 相邻 `../Axiom` 是只读参考，不在该目录进行修改、构建、Git 写操作或 AWT 写操作。
- 当前 AWT 工具位于 `../AIWorkTask/awt`，工程上下文为本仓库 `.awtcontext/`。
- 通过 `task list`、`context build` 和 `GlobalDoc/DocIndex.md` 进入任务；状态使用 AWT CLI 维护。
- 修改引擎前读取 `Axiom/AGENTS.md` 和引擎项目约定，在本地 `PixelMain` 修改，保留 AET；当前锁定提交已包含鼠标事件快照，Pixel 按带事件参数的接口接收呈现变化，当前验证范围见 README。
- World 不依赖渲染；Pixel 专用渲染、shader 和测试不得重新进入引擎。
- 游戏源码只归入 `Pixel/Source/Runtime/{Gameplay,Simulation,Render,System}` 和 `Pixel/Source/Dev/Debug`；完整模块映射和依赖合同见 README。
- Gameplay 主动查询/影响 Simulation；Simulation 不依赖 Gameplay/Render。System 的通用服务不包含游戏领域类型；`System/IO/Input` 是调用 Gameplay 的输入适配入口。LLMView、游戏提案和行为归 Gameplay，通用模型接口归 System。
- 输入处理归 `System/IO/Input`，`PlayerInput` 保留在 `Gameplay/Actors`；输入适配调用 Gameplay，Gameplay 核心、Simulation 和其余 System 服务不反向依赖 Input。
- Application 是装配 Input、Render 与 DebugUI 的组合根；Input通过接口表达显示意图，不依赖Render实现。Garden 与 Performance 保留显式开发接点，新增依赖需满足 `Pixel.ModuleBoundaries`。
- 构建、运行及验证命令见 `README.md`，历史依据和限制见迁移记录。
