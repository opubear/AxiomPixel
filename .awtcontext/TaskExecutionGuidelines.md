# AxiomPixel — Task Execution Guidelines

1. 使用 `task list`、`feedback list --pending`、`context build` 进入任务并检查裁剪信息。
2. 阅读 `GlobalDoc/DocIndex.md`，按需追溯引擎历史，检查两个工作区状态。
3. 修改限定在本仓库和本地 Axiom/ 的 PixelMain 分支，相邻引擎及工具仓库只读。
4. CPU 改动运行 CMake/CTest；渲染和构建迁移追加 Pixel、AET 构建及可行的 GPU smoke。
5. 通过 AWT 记录需求、验收与实际证据，用 `task complete --confirm` 收尾并运行 `check`。
