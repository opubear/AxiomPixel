# AxiomPixel 协作入口

- 本仓库维护 Pixel 游戏；`Axiom/` 通过 Git 子模块引入，远端分支为 `PixelMain`，父仓库 gitlink 锁定提交并与 `AxiomVersion.json` 保持一致。
- 相邻 `../Axiom` 是只读参考，不在该目录进行修改、构建、Git 写操作或 AWT 写操作。
- 当前 AWT 工具位于 `../AIWorkTask/awt`，工程上下文为本仓库 `.awtcontext/`。
- 通过 `task list`、`context build` 和 `GlobalDoc/DocIndex.md` 进入任务；状态使用 AWT CLI 维护。
- 修改引擎前读取 `Axiom/AGENTS.md` 和引擎项目约定，在本地 `PixelMain` 修改，保留 AET；当前锁定提交已包含鼠标事件快照，Pixel 按带事件参数的接口接收呈现变化，当前验证范围见 README。
- World 不依赖渲染；Pixel 专用渲染、shader 和测试不得重新进入引擎。
- 构建、运行及验证命令见 `README.md`，历史依据和限制见迁移记录。
