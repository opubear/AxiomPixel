# AxiomPixel 协作入口

- 本仓库维护 Pixel 游戏；`Axiom/` 是独立 Git 仓库，当前使用已移植通用渲染能力并合并远端修复的 `PixelMain` 分支。
- 相邻 `../Axiom` 是只读参考，不在该目录进行修改、构建、Git 写操作或 AWT 写操作。
- 当前 AWT 工具位于 `../AIWorkTask/awt`，工程上下文为本仓库 `.awtcontext/`。
- 通过 `task list`、`context build` 和 `GlobalDoc/DocIndex.md` 进入任务；状态使用 AWT CLI 维护。
- 修改引擎前读取 `Axiom/AGENTS.md` 和引擎项目约定，按用户最新要求在本地 `PixelMain` 修改，保留 AET；旧 `PixelMain` 已由合并后的引擎版本替换。
- World 不依赖渲染；Pixel 专用渲染、shader 和测试不得重新进入引擎。
- 构建、运行及验证命令见 `README.md`，历史依据和限制见迁移记录。
