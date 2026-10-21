# DocIndex — AxiomPixel

| 文档名 | 路径 | 简介 | 状态 |
|--------|------|------|------|
| devmain 重构适配（2026-10-16） | `DevmainAdaptation-2026-10-16.md` | 新应用入口、服务所有权、输入注册、统一 overlay、配置与静态/共享构建；PixelMain rebase 和锁定版本 | 当前引擎接入契约 |
| Pixel模块化系统设计 | `workspace:Docs/Architecture/PixelModularDesign.docx` | 模块分层与依赖；旧World所有权、整图及交换细节由WorldRepresentation替代 | 当前实现设计 |
| 游戏性能插桩与帧率分析 | `workspace:Docs/PerformanceProfiling.md` | 可复用CPU计时、Performance面板/CSV、Debug与优化构建对照及主要热点 | 已实现并实测 |
| 世界方块更新性能调查 | `workspace:Docs/MaterialUpdatePerformance.md` | 旧移动链性能基线与方案来源；现行同步规则和收益见WorldRepresentation文档 | 调查完成；生产优化未实施 |
| 世界与力场内存统计 | `workspace:Docs/MemoryProfiling.md` | 帧末所有权计量、size/capacity、七风策略、窗口峰值与默认/暂停实测；排除RSS、临时分配及渲染资源 | 已实现并实测 |
| 风场可视化双模式 | `workspace:Docs/WindVisualizationSystemDesign.docx` | 纯风速GPU纹理、短轨迹/柔和折光、人物前合成、参数与验证 | 首版实现并独立审查通过 |
| 构式系统（Formula System） | `workspace:Docs/GameDesign/FormulaSystem.md` | 核心玩法、封闭组合、作用域、已实现的游戏基础版及后续边界 | 当前系统入口；基础版游戏已实现 |
| 构式语言架构（Formula DSL） | `context:GlobalDoc/FormulaLanguageArchitecture.md` | 独立语言四层、不可变访问、调度与作用域，以及游戏接入的职责边界 | 独立模块架构；游戏接入见基础版实现 |
| 构式语言原型使用说明 | `workspace:Pixel/Formula/README.md` | 独立DSL语法、CLI示例和构建测试入口；游戏通过Gameplay适配层接入 | 独立CLI与游戏适配并存 |
| 早期法术编程系统设计（历史提案） | `workspace:Docs/GameDesign/SpellProgrammingSystemDesign.docx` | 2026-10-12 原始设计；当前名称为构式，现行规则与实现边界见构式系统及语言架构 | 保留 v0.1 历史依据，不作为实现完成声明 |
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
| 初版自然世界素材候选 | `context:GlobalDoc/NaturalWorldAssetResearch-2026-10-20.md` | 免费试用的舒适自然横版像素素材、推荐组合、配套资源及替换边界 | |
| ArtRaw素材原件与来源 | `workspace:ArtRaw/README.md` | 19组下载与3组生成素材的入口、来源记录、运行时复制及完整性说明 | |
| 构式攻击特效素材与用途 | `workspace:ArtRaw/Effects/README.md` | 新增11组免费攻击特效、实际文件、构式阶段映射和图集适配边界 | |
| 素材来源价格与商用许可总索引 | `workspace:ArtRaw/RESOURCE_INDEX.md` | 19组下载与3组生成素材的逐包来源、价格、署名、商用条件及AI来源记录 | |
| 舒适山林四层视差背景试作 | `workspace:ArtRaw/Environment/NaturalParallax2K/README.md` | 四张2560x1440透明分层贴图、原件与生成提示词、视差预览及镜像重复用法 | |
| 游戏美术清单与资源缓存 | `workspace:Docs/ArtAssetIntegration.md` | Content复制件、自定义JSON、PNG共享缓存、GPU一次上传、视差及验证范围 | |
| 自然方块材质纹理试作 | `workspace:ArtRaw/Materials/NaturalBlocks/README.md` | 12项自然方块与矿石纹理、64和256像素版本、五种矿石辨识预览及铺贴用法 | |
| 分块程序化世界生成方案 | `context:GlobalDoc/WorldGenerationDesign.md` | 已评审设计基线；实现与当前接口、材质JSON和验证入口见WorldGenerationImplementation | |
| 火焰水体与水汽静态纹理 | `workspace:ArtRaw/Materials/Elements/README.md` | 六项64和256像素静态材质与透明效果、密度图、重复方向及已有动画入口 | |
| 世界生成与自然材质实现 | `context:GlobalDoc/WorldGenerationImplementation.md` | Natural/Flat策略选择、种子版本及旧档、有限水体、Content纹理JSON和验证入口 | |
| 构式基础版游戏接入设计与实施计划 | `context:GlobalDoc/FormulaGameplayV1Design.md` | 已评审实施基线与验收计划；当前快捷键调整及实现见基础版实现说明 | |
| 构式基础版游戏接入实现 | `context:GlobalDoc/FormulaGameplayV1Implementation.md` | 操作入口、F2/F4独立窗口、模块职责、固定步与v4兼容、额度及验证边界 | |
| Pixel 游戏设计总纲 | `workspace:Docs/GameDesign/GameDesignVision.md` | 核心愿景、隐性流程、构式作为世界运行原理的设想、家园与世界意志及设计取舍 | |
| 游戏设计与策划文档目录 | `workspace:Docs/GameDesign/README.md` | 游戏总纲、构式设计与参考资料的阅读入口，以及相关工程文档和维护约定 | |
| Gemini 游戏设计讨论原文 | `workspace:Docs/GameDesign/Pixel项目像素沙盒游戏设计探讨 - Google Gemini.pdf` | 流程与玩法参考：祈祷、自主成长、世界意志、环境改造与家园；术语和技术假设需辨别 | |
| 架构规划AWT原始副本 | `context:GlobalDoc/GameplayArchitecturePlan.md` | 复制前的保留副本；任务执行与后续维护以Docs/Architecture中的架构文档为准 | |
| Pixel面向游戏愿景的架构规划 | `workspace:Docs/Architecture/GameplayArchitecturePlan.md` | 执行前必读；独立构式语言、领域模块、LLMView、引擎职责和分阶段迁移方案 | |
| 首个贯穿场景与GameAPI契约 | `workspace:Docs/Architecture/FirstGameplaySliceContract.md` | M1.1执行规格：观测与开采/激活场景、身份单位、动作效果事实、提交查询及验收映射 | |

角色与双控制模式：见工程 `README.md` 的构建和运行章节，代码位于 `World/Character.*`、`Gameplay/GameSession.*`、`Input/GameInput.*`、`Camera/GameCamera.*` 和 `PixelRender/PixelWorldPresentation.*`；CPU碰撞与快照测试为 `PixelCharacterTests` 和 `PixelSessionTests`，完整键鼠行为由 `PixelInputTests` 验证，GPU模式/外观回归位于 `PixelRuntimeSmoke`。`Application/PixelGame.*` 只装配模块、编排生命周期并转发输入；`GameInput` 负责监听后的全部游戏输入处理和功能调用。
