# DocIndex — AxiomPixel

| 文档名 | 路径 | 简介 | 状态 |
|--------|------|------|------|
| devmain 重构适配（2026-10-16） | `DevmainAdaptation-2026-10-16.md` | 新应用入口、服务所有权、输入注册、统一 overlay、配置与静态/共享构建；PixelMain rebase 和锁定版本 | 当前引擎接入契约 |
| Pixel模块化系统设计 | `workspace:Docs/Architecture/PixelModularDesign.docx` | 模块分层与依赖；旧World所有权、整图及交换细节由WorldRepresentation替代 | 当前实现设计 |
| 游戏性能插桩与帧率分析 | `workspace:Docs/PerformanceProfiling.md` | 可复用CPU计时、Performance面板/CSV、Debug与优化构建对照及主要热点 | 已实现并实测 |
| 世界方块更新性能调查 | `workspace:Docs/MaterialUpdatePerformance.md` | 旧移动链性能基线与方案来源；现行同步规则和收益见WorldRepresentation文档 | 调查完成；生产优化未实施 |
| 世界与力场内存统计 | `workspace:Docs/MemoryProfiling.md` | 帧末所有权计量、size/capacity、七风策略、窗口峰值与默认/暂停实测；排除RSS、临时分配及渲染资源 | 已实现并实测 |
| 风场可视化双模式 | `workspace:Docs/WindVisualizationSystemDesign.docx` | 纯风速GPU纹理、短轨迹/柔和折光、人物前合成、参数与验证 | 首版实现并独立审查通过 |
| 构式系统（Formula System） | `workspace:Docs/GameDesign/FormulaSystem.md` | 函数组合与世界绑定设计；formula/2已实现范围和后续能力边界 | 当前设计入口；formula/2已实现，process/1兼容 |
| 构式语言架构（历史cast原型） | `context:GlobalDoc/FormulaLanguageArchitecture.md` | 旧cast原型历史架构；代码已删除，现行入口见FormulaComposition | 独立模块架构；游戏接入见基础版实现 |
| 构式语言原型使用说明 | `workspace:Pixel/Source/Runtime/System/Formula/README.md` | System下独立DSL语法、CLI示例和构建测试入口；游戏通过Gameplay适配层接入 | 独立CLI与游戏适配并存 |
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
| 分块、四叉树与流式世界 | `workspace:Docs/WorldRepresentation.md` | 当前世界表示、材料调度、流式预算、文件与显示接口；空间场存储已统一 | |
| 世界改造验证与性能 | `workspace:Docs/WorldRepresentationValidation.md` | 同步参考、三轮Release原始数据、CPU及GPU最终结果与独立审查结论 | |
| 初版自然世界素材候选 | `context:GlobalDoc/NaturalWorldAssetResearch-2026-10-20.md` | 免费试用的舒适自然横版像素素材、推荐组合、配套资源及替换边界 | |
| ArtRaw素材原件与来源 | `workspace:ArtRaw/README.md` | 19组下载与3组生成素材的入口、来源记录、运行时复制及完整性说明 | |
| 构式攻击特效素材与用途 | `workspace:ArtRaw/Effects/README.md` | 新增11组免费攻击特效、实际文件、构式阶段映射和图集适配边界 | |
| 素材来源价格与商用许可总索引 | `workspace:ArtRaw/RESOURCE_INDEX.md` | 19组下载与3组生成素材的逐包来源、价格、署名、商用条件及AI来源记录 | |
| 舒适山林四层视差背景试作 | `workspace:ArtRaw/Environment/NaturalParallax2K/README.md` | 四张2560x1440透明分层贴图、原件与生成提示词、视差预览及镜像重复用法 | |
| 游戏美术清单与资源缓存 | `workspace:Docs/ArtAssetIntegration.md` | Content美术清单、背景/地形/人物分层动画、共享缓存、GPU上传与验证 | |
| 自然方块材质纹理试作 | `workspace:ArtRaw/Materials/NaturalBlocks/README.md` | 12项自然方块与矿石纹理、64和256像素版本、五种矿石辨识预览及铺贴用法 | |
| 分块程序化世界生成方案 | `context:GlobalDoc/WorldGenerationDesign.md` | 已评审设计基线；实现与当前接口、材质JSON和验证入口见WorldGenerationImplementation | |
| 火焰水体与水汽静态纹理 | `workspace:ArtRaw/Materials/Elements/README.md` | 六项64和256像素静态材质与透明效果、密度图、重复方向及已有动画入口 | |
| 世界生成与自然材质实现 | `context:GlobalDoc/WorldGenerationImplementation.md` | Natural/Flat策略选择、种子版本及旧档、有限水体、Content纹理JSON和验证入口 | |
| 构式基础版游戏接入设计（历史） | `context:GlobalDoc/FormulaGameplayV1Design.md` | 旧树构式接入设计与验收基线，仅供历史查阅 | |
| 构式基础版游戏接入实现（历史） | `context:GlobalDoc/FormulaGameplayV1Implementation.md` | 旧树宿主、校验与编辑器的历史实现记录，已由process取代 | |
| Pixel 游戏设计总纲 | `workspace:Docs/GameDesign/GameDesignVision.md` | 核心愿景、流程、构式调用逻辑、统一函数组合与世界绑定、家园及世界意志的设计取舍 | |
| 游戏设计与策划文档目录 | `workspace:Docs/GameDesign/README.md` | 游戏总纲、formula/2现行合同、构式设计与旧cast历史资料入口 | |
| Gemini 游戏设计讨论原文 | `workspace:Docs/GameDesign/Pixel项目像素沙盒游戏设计探讨 - Google Gemini.pdf` | 流程与玩法参考：祈祷、自主成长、世界意志、环境改造与家园；术语和技术假设需辨别 | |
| 架构规划AWT原始副本 | `context:GlobalDoc/GameplayArchitecturePlan.md` | 复制前的保留副本；任务执行与后续维护以Docs/Architecture中的架构文档为准 | |
| Pixel面向游戏愿景的架构规划 | `workspace:Docs/Architecture/GameplayArchitecturePlan.md` | 执行前必读；独立构式语言、领域模块、LLMView、引擎职责和分阶段迁移方案 | |
| 首个贯穿场景与GameAPI契约 | `workspace:Docs/Architecture/FirstGameplaySliceContract.md` | M1.1执行规格：观测与开采/激活场景、身份单位、动作效果事实、提交查询及验收映射 | |
| 统一游戏动作与会话控制入口 | `workspace:Docs/Architecture/GameActions.md` | M1动作去重、只读公开访问、内部早失败、存储错误边界与M2待办 | |
| 固定步协调与完整提交 | `workspace:Docs/Architecture/SimulationCommits.md` | 固定步与预算重试、WorldBuffers只读双缓冲、观测按需导出及GPU交接边界 | |
| 正常关闭请求与最终释放 | `workspace:Docs/Architecture/CloseRequests.md` | M2.2 Application钩子、请求消费与拒绝后继续、AET默认行为及M2.3保存接入边界 | |
| 会话保存边界与旧档迁移 | `workspace:Docs/Architecture/SessionSaveBoundary.md` | Deferred和关闭协商；M4.6多域检查点、v2-v5导入与验证入口 | |
| 有界领域观测与提交事实 | `context:GlobalDoc/ObservationProjection.md` | M2投影与事实；M4.7加入同提交设备库存程序和场源值 | |
| 领域观测Release基准原始数据 | `context:GlobalDoc/ObservationBenchmark-2026-10-22.csv` | M2.4三轮128帧静态与动态风有无投影的P50/P95、导出字节及容量 | |
| LLMView只读查询与离线贯穿样例 | `context:GlobalDoc/LLMView.md` | 授权冻结分页与JSON；M4.7库存设备、M5.1知识历史和来源摘要 | |
| LLMView Release基准原始数据 | `context:GlobalDoc/LLMViewBenchmark-2026-10-22.csv` | M2.5三轮128帧、三个观察者完整分页和JSON序列化耗时与保留字节 | |
| 构式语言语义选择（历史实验） | `context:GlobalDoc/FormulaSemanticsExperiment.md` | 五场景对照与采用显式过程的依据；实验代码已移除 | |
| 构式语义对照Release原始数据 | `context:GlobalDoc/FormulaSemanticsBenchmark-2026-10-22.csv` | M3.1五场景三轮1000样本P50/P95、待办和工作计数；不同执行器范围见实验文档 | |
| 构式过程语言v1与有界运行时 | `context:GlobalDoc/FormulaProcessRuntime.md` | process/1兼容与执行核心历史合同；formula/2差异见FormulaComposition | |
| FormulaBridge与程序承载者 | `context:GlobalDoc/FormulaBridge.md` | 宿主基础合同；formula/2的Actor绑定、回执与检查点差异见FormulaComposition | |
| 构式程序状态与编辑合同 | `context:GlobalDoc/FormulaStateAndEditing.md` | 运行DTO与源码编辑基础合同；formula/2状态和保存演进见FormulaComposition | |
| Actors身份与地形边界 | `context:GlobalDoc/Actors.md` | 角色身份、运动、固定测试木头人及构式命中；提交呈现和检查点合同 | |
| 物品库存与开采资源合同 | `context:GlobalDoc/Items.md` | 物品、容器与资源操作；检查点保存及M4.7库存投影和UI | |
| 设备放置端口与最小工作状态 | `context:GlobalDoc/Devices.md` | 推力设备、冻结和存档；M4.7同帧状态解释与授权观测 | |
| 有归属的持续FieldSource | `context:GlobalDoc/FieldSources.md` | 持续来源、区域冻结、驻留失效及M4.6归属持久化 | |
| 区域活动与设备程序冻结恢复 | `context:GlobalDoc/RegionActivity.md` | M4.5本地活动时间与身份续接；M4.6磁盘恢复另见Checkpoints | |
| 多领域一致检查点与程序持久化 | `context:GlobalDoc/Checkpoints.md` | 不可变世代与程序恢复；M5.4扩展知识、权限和祈祷同提交保存 | |
| 设备库存与持续程序观测 | `context:GlobalDoc/GameplayObservation.md` | M4.7统一提交投影、库存授权与分页、设备状态与场源UI及验证 | |
| 可保存的力场育圃贯穿场景 | `context:GlobalDoc/ForceGarden.md` | M4.8天然晶体、真实资源、设备与植物条件响应、冻结存档、试玩和验证 | |
| 发现知识、重要历史与派生记忆 | `context:GlobalDoc/KnowledgeAndHistory.md` | 有来源档案与授权查询；M5.4知识存档和祈祷激活证据 | |
| 区域控制与干预撤销 | `context:GlobalDoc/Authority.md` | 能力、保护与租约；M5.4同提交存档、重载核验和设备保护UI | |
| 异步WorldWill与离线决策 | `context:GlobalDoc/WorldWill.md` | 授权观察与受限提案提交、LLM抽象接线、测试worker及生命周期 | |
| 持久援助、祈祷档案与自主权 | `context:GlobalDoc/PersistentInterventions.md` | M5.4离线育圃援助、保护撤销、同提交知识与权限存档、恢复和来源证据 | |
| 模型接口与祈祷交互 | `context:GlobalDoc/ModelServiceIntegration.md` | LLM注入、共用API提案协议、玩家原文、取消与Ready；真实服务未接入 | |
| LLM抽象与随机PseudoLLM | `context:GlobalDoc/LLMAndPseudoLLM.md` | 独立模型生命周期、共用决策API、随机函数组合兜底及验证边界 | |
| BehaviorDSL必要性与有限决策图实验 | `workspace:Docs/Architecture/BehaviorDecisionExperiment.md` | M6.1同场景表达、诊断和成本对照；暂缓持久DSL的依据与重启门槛 | |
| 有限决策图Release成本原始数据 | `workspace:Docs/Architecture/BehaviorDecisionBenchmark-2026-10-24.csv` | 三轮原生函数与决策图同输入求值P50/P95及保留字节；不含模型与模拟成本 | |
| M4持久设备与家园阶段验收 | `context:GlobalDoc/M4Acceptance.md` | 八项子任务整合合同、当前CPU与GPU证据、独立审查、夹具修复及保存恢复边界 | |
| M5可追溯世界意志阶段验收 | `context:GlobalDoc/M5Acceptance.md` | 六项能力的异步、授权与持久化合同，CPU及GPU证据与独立审查 | |
| M6阶段决策实验Release复测原始数据 | `context:GlobalDoc/M6AcceptanceBenchmark-2026-10-24.csv` | 阶段验收三轮原生与决策图同输入求值批均值P50/P95和保留字节 | |
| M6运行时逻辑构建与DSL阶段验收 | `context:GlobalDoc/M6Acceptance.md` | 暂缓持久DSL的场景依据、实验隔离、阶段复测与独立审查 | |
| 游戏愿景模块化架构总验收 | `context:GlobalDoc/GameplayArchitectureAcceptance.md` | 六阶段29项任务的组合合同、CPU与GPU/AET验证、独立审查及范围限制 | |
| Core Allocator与容器后端的Pixel接入 | `context:GlobalDoc/CoreAllocatorAndContainers.md` | 模板后端与实例资源选择、区块和scratch寿命、当前专项验证及限制 | |
| 工程入口与Runtime/Dev模块边界 | `workspace:README.md` | 模块归属、输入分层、逻辑与渲染像素比例、构建运行和验证命令 | |
| 性能采样模块 | `workspace:Pixel/Source/Dev/Debug/Performance/README.md` | Dev/Debug性能插桩、运行时采样开关、CSV和内存统计口径 | |
| 构式函数组合与世界绑定实现 | `workspace:Docs/Architecture/FormulaComposition.md` | formula/2函数、Actor绑定、时序、兼容保存及五套Debug函数预设 | |
| 构式升级验证 | `workspace:Docs/Architecture/FormulaCompositionVerification.md` | 独立CPU与sanitizer复核，主agent GPU、关闭、Release成本及覆盖边界 | |
| 构式函数组合Release成本 | `workspace:Docs/Architecture/FormulaCompositionCost.txt` | 三轮编译与包装/展开首片段成本；不含实例创建、世界提交和GPU | |
| 水蒸汽火元胞运动模拟 | `context:GlobalDoc/CellularMaterials.md` | 单格材料形态规则、调试笔刷、同步提交、存档与验证入口 | |
| 世界双缓冲验证与性能 | `workspace:Docs/WorldBuffersValidation.md` | 只读版本、预算事务、独立审查、CPU/GPU验证及三轮Release取舍 | |
| 世界双缓冲Release原始数据 | `workspace:Docs/WorldBuffersBenchmark-2026-10-26.csv` | 三轮同源基准的提交与F1打包耗时、World预留容量和校验值 | |
| 背包装备与自动拾取系统设计 | `workspace:Docs/GameDesign/InventoryAndEquipmentDesign.md` | 设计提案：格子背包、装备、掉落拾取、事务边界与现有合同迁移，尚未实现 | |
| 人物动作与换装素材 | `workspace:ArtRaw/Characters/README.md` | 五组免费像素人物、一个骨骼换装样例、预览、动作网格与来源许可 | |
| 人物装备换装与2D骨骼评估 | `workspace:ArtRaw/Characters/EquipmentAndSkeleton.md` | 分层序列帧、武器挂点与骨骼的取舍；Spine样例许可及其他候选范围 | |
| 原生背包与快捷栏UI | `context:GlobalDoc/NativeInventoryUI.md` | Gameplay/UI独立状态、七快捷格和五行七列库存、原生后处理叠加及验证 | |
| Axiom通用CPU诊断 | `workspace:Axiom/Source/Core/Diagnostics/README.md` | PerfTest门控、RAII纳秒事件trace、聚合采样和静态动态链接合同 | |
| Performance引擎迁移验收 | `workspace:Docs/DiagnosticsMigration.md` | 通用诊断边界、PerfTest与纳秒trace、静态动态库及原生窗口验收结果 | |
| Axiom统一日志 | `workspace:Axiom/Source/Log/README.md` | 默认毫秒时间戳、日志级别和格式化接口、共享实例及静态动态链接验证 | |
| 分层群落世界生成v2 | `workspace:Docs/BiomeWorldGeneration.md` | 温湿度抬升场、群落细节地层与树木、新材料RawData和Content、版本与验证 | |
| Axiom跨平台系统信息查询 | `workspace:Axiom/Source/HAL/Platform/README.md` | CPU拓扑缓存指令集、GPU与内存、OS版本架构、未知语义及三平台构建验证入口 | |
| 世界空间场统一分块存储 | `workspace:Docs/SpatialFieldStorage.md` | 页与元素对齐、tile和四叉树、多分辨率、GPU交换及已迁移字段清单 | |
| 空间场存储验证与性能 | `workspace:Docs/SpatialFieldStorageValidation.md` | CPU、64格、ASan/UBSan、原生GPU及改造前后三轮Release结果与限制 | |
| 空间存储区域更新Release数据 | `workspace:Docs/SpatialStorage-regions-2026-10-29.csv` | 三轮改造前后活动区域与全图参考的耗时、访问量、容量和材料哈希 | |
| 空间存储风策略Release数据 | `workspace:Docs/SpatialStorage-wind-2026-10-29.csv` | 三轮六风策略空场墙体狭缝的耗时及速度散度质量诊断 | |
| 空间存储提交与打包Release数据 | `workspace:Docs/SpatialStorage-buffers-2026-10-29.csv` | 三轮有无读者的动态风与运动材料提交、F1打包、容量及校验值 | |

角色与双控制模式：见工程 `README.md` 的构建和运行章节，代码位于 `Gameplay/Actors/Actors.*`、`Gameplay/Actors/Body.*`、`Gameplay/Actors/PlayerInput.h`、`Gameplay/GameSession.*`、`System/IO/Input/GameInput.*`、`Gameplay/Camera/GameCamera.*` 和 `Render/PixelWorldPresentation.*`；CPU碰撞与快照测试为 `PixelBodyTests` 和 `PixelSessionTests`，完整键鼠行为由 `PixelInputTests` 验证，GPU模式/外观回归位于 `PixelRuntimeSmoke`。`Gameplay/Application/PixelGame.*` 只装配模块、编排生命周期并转发输入；`GameInput` 负责监听后的全部游戏输入处理和功能调用，PlayerInput保留Gameplay的运动意图契约。
