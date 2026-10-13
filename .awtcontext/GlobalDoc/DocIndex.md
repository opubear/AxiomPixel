# DocIndex — AxiomPixel

| 文档名 | 路径 | 简介 | 状态 |
|--------|------|------|------|
| 风场可视化双模式 | `workspace:Docs/WindVisualizationSystemDesign.docx` | 纯风速GPU纹理、短轨迹/柔和折光、人物前合成、参数与验证 | 首版实现并独立审查通过 |
| Astra 独立全代码审查（2026-10-05） | `CodeReview-Astra-2026-10-05.md` | 干净上下文 xhigh 审查 Pixel 与本地 Axiom，P2 Cross、P3 Quaternion；覆盖清单与独立验证证据 | 两项已修复，原 Astra 复核通过 |
| 当前 Pixel 代码审查（2026-10-04） | `CodeReview-2026-10-04.md` | 全部 Pixel 代码与引擎集成审查，3 项 P2 缺陷、复现材料及修复验证 | F1–F3 已修复 |
| devmain 兼容性复核 | `DevmainCompatibilityReview.md` | 原始分支失败证据与通用能力移植后的重新验证 | 已修复并通过验证 |
| Pixel 迁移记录 | `PixelMigration.md` | 2DRender 历史依据、职责与路径映射、构建及验证证据 | 已迁移、验证 |
| 风场接口与模型 | `WindField.md` | 简化NS求解、默认高度边界、泥土双向阻力反馈、F1调试视图、ImGui高度面板及近似限制 | 已实现 |
| 风向反馈诊断（2026-10-05） | `WindDirectionDiagnosis.md` | 默认尺度诊断、伸入风区的窄土堆复现、方向性阻力与加权压力修复证据 | 已修复窄土堆反馈，仍为粗网格近似 |
| 风场策略首版 | `WindStrategiesV1.md` | 六策略切换、四类候选首版、力场独立与数值/成本验证和独立审查 | 已实现，限制见正文 |
| 风场多尺度模拟方案评估 | `WindSimulationAlternatives.md` | 四类候选方案、推荐混合结构、现有求解器成本基线与原型验证计划 | 四类 CPU 首版已实现，GPU 与后续目标未实现 |
| 逐像素力场与风场基准（2026-10-12） | `PixelWindBaseline.md` | 连续基础力场、1:1 MAC硬边界基准、误差/成本实验、代码审查与使用边界 | 已实现，验证记录见正文 |

角色与双控制模式：见工程 `README.md` 的构建和运行章节，代码位于 `World/Character.*`、`PixelGame.*` 和 `PixelWorldPresentation.*`；CPU碰撞与快照测试为 `PixelCharacterTests`，GPU模式/外观回归位于 `PixelRuntimeSmoke`。
