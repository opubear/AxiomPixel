# DocIndex — AxiomPixel

| 文档名 | 路径 | 简介 | 状态 |
|--------|------|------|------|
| devmain 重构适配（2026-10-16） | `DevmainAdaptation-2026-10-16.md` | 新应用入口、服务所有权、输入注册、统一 overlay、配置与静态/共享构建；PixelMain rebase 和锁定版本 | 当前引擎接入契约 |
| Pixel模块化系统设计 | `workspace:Docs/Architecture/PixelModularDesign.docx` | 模块分层与依赖；旧World所有权、整图及交换细节由WorldRepresentation替代 | 当前实现设计 |
| 游戏性能插桩与帧率分析 | `workspace:Docs/PerformanceProfiling.md` | 可复用CPU计时、Performance面板/CSV、Debug与优化构建对照及主要热点 | 已实现并实测 |
| 世界方块更新性能调查 | `workspace:Docs/MaterialUpdatePerformance.md` | 旧移动链性能基线与方案来源；现行同步规则和收益见WorldRepresentation文档 | 调查完成；生产优化未实施 |
| 世界与力场内存统计 | `workspace:Docs/MemoryProfiling.md` | 帧末所有权计量、size/capacity、七风策略、窗口峰值与默认/暂停实测；排除RSS、临时分配及渲染资源 | 已实现并实测 |
| 风场可视化双模式 | `workspace:Docs/WindVisualizationSystemDesign.docx` | 纯风速GPU纹理、短轨迹/柔和折光、人物前合成、参数与验证 | 首版实现并独立审查通过 |
| 构式系统（Formula System） | `FormulaSystem.md` | 正式命名、核心玩法、封闭组合、载体与子构式、世界接入方案、实现状态及后续路线 | 当前系统设计入口；区分已实现与提案 |
| 构式语言架构（Formula DSL） | `FormulaLanguageArchitecture.md` | 语言/描述/执行/效果四层、不可变结构、调度、作用域、历史提案差异及验证依据 | 当前原型架构；未接游戏世界 |
| 构式语言原型使用说明 | `workspace:Pixel/Formula/README.md` | 语法、参数、CLI、示例和构建测试入口；代码名已统一为 Formula | 原型已实现，未接游戏世界 |
| 早期法术编程系统设计（历史提案） | `workspace:Docs/SpellProgrammingSystemDesign.docx` | 2026-10-12 原始完整设计；当前名称为构式，现行状态与规则见上方两份工程知识 | 保留 v0.1 历史依据，不作为实现完成声明 |
| Astra 独立全代码审查（2026-10-05） | `CodeReview-Astra-2026-10-05.md` | 干净上下文 xhigh 审查 Pixel 与本地 Axiom，P2 Cross、P3 Quaternion；覆盖清单与独立验证证据 | 两项已修复，原 Astra 复核通过 |
| 当前 Pixel 代码审查（2026-10-04） | `CodeReview-2026-10-04.md` | 全部 Pixel 代码与引擎集成审查，3 项 P2 缺陷、复现材料及修复验证 | F1–F3 已修复 |
| devmain 兼容性复核（历史） | `DevmainCompatibilityReview.md` | 旧 devmain 原始分支失败证据与通用能力移植后的重新验证 | 历史；当前接入见 2026-10-16 适配记录 |
| Pixel 迁移记录 | `PixelMigration.md` | 2DRender 历史依据、职责与路径映射、构建及验证证据 | 已迁移、验证 |
| 风场接口与模型 | `WindField.md` | 简化NS求解、默认高度边界、泥土双向阻力反馈、F1调试视图、ImGui高度面板及近似限制 | 已实现 |
| 风向反馈诊断（2026-10-05） | `WindDirectionDiagnosis.md` | 默认尺度诊断、伸入风区的窄土堆复现、方向性阻力与加权压力修复证据 | 已修复窄土堆反馈，仍为粗网格近似 |
| 风场策略首版 | `WindStrategiesV1.md` | 六策略切换、四类候选首版、力场独立与数值/成本验证和独立审查 | 已实现，限制见正文 |
| 风场多尺度模拟方案评估 | `WindSimulationAlternatives.md` | 四类候选方案、推荐混合结构、现有求解器成本基线与原型验证计划 | 四类 CPU 首版已实现，GPU 与后续目标未实现 |
| 逐像素力场与风场基准（2026-10-12） | `PixelWindBaseline.md` | 连续基础力场、1:1 MAC硬边界基准、误差/成本实验、代码审查与使用边界 | 已实现，验证记录见正文 |
| 世界分块与流式接口 | `workspace:Docs/WorldRepresentation.md` | 现行分块、四叉树、同步规则、预算、存档、移动风域及区域发布契约 | |
| 世界改造验证与性能 | `workspace:Docs/WorldRepresentationValidation.md` | 同步参考、三轮Release原始数据、CPU及GPU最终结果与独立审查结论 | |

角色与双控制模式：见工程 `README.md` 的构建和运行章节，代码位于 `World/Character.*`、`Gameplay/GameSession.*`、`Input/GameInput.*`、`Camera/GameCamera.*` 和 `PixelRender/PixelWorldPresentation.*`；CPU碰撞与快照测试为 `PixelCharacterTests` 和 `PixelSessionTests`，完整键鼠行为由 `PixelInputTests` 验证，GPU模式/外观回归位于 `PixelRuntimeSmoke`。`Application/PixelGame.*` 只装配模块、编排生命周期并转发输入；`GameInput` 负责监听后的全部游戏输入处理和功能调用。
