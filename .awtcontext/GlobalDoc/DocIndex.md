# DocIndex — AxiomPixel

| 文档名 | 路径 | 简介 | 状态 |
|--------|------|------|------|

角色与双控制模式：见工程 `README.md` 的构建和运行章节，代码位于 `Gameplay/Actors/Actors.*`、`Gameplay/Actors/Body.*`、`Gameplay/Actors/PlayerInput.h`、`Gameplay/GameSession.*`、`System/IO/Input/GameInput.*`、`Gameplay/Camera/GameCamera.*` 和 `Render/PixelWorldPresentation.*`；CPU碰撞与快照测试为 `PixelBodyTests` 和 `PixelSessionTests`，完整键鼠行为由 `PixelInputTests` 验证，GPU模式/外观回归位于 `PixelRuntimeSmoke`。`Gameplay/Application/PixelGame.*` 只装配模块、编排生命周期并转发输入；`GameInput` 负责监听后的全部游戏输入处理和功能调用，PlayerInput保留Gameplay的运动意图契约。
| 总览｜游戏文档总入口 | `workspace:Docs/GameDesign/README.md` | 现行：总纲、六个游戏系统、昼夜季节与技术合同、产品状态及历史参考的导航 | |
| 总览｜Pixel 游戏设计总纲 | `workspace:Docs/GameDesign/GameDesignVision.md` | 设计方向：核心愿景、流程、构式调用逻辑、统一函数组合与世界绑定、家园及世界意志的设计取舍 | |
| 总览｜文档分类与维护规则 | `context:GlobalDoc/DocumentationGovernance.md` | 现行：文档分层、状态词、功能变更同步、历史清理和本轮整理记录 | |
| 总览｜产品现状与待办 | `context:GlobalDoc/ProductStatus.md` | 现行：玩家闭环、昼夜四季等近期功能、未交付能力、决策顺序与验收边界 | |
| 系统·世界｜世界、环境与地区生态 | `context:GlobalDoc/SystemsWorld.md` | 现行：三群落与材料风场、地区人口休眠、昼夜四季与晨昏预览、试玩和后续边界 | |
| 系统·世界｜世界生成与自然材质实现 | `context:GlobalDoc/WorldGenerationImplementation.md` | 兼容：Natural v1与旧生成实现；当前默认v2见BiomeWorldGeneration和WorldBiomes | |
| 系统·世界｜有归属的持续FieldSource | `context:GlobalDoc/FieldSources.md` | 现行：持续来源、区域冻结、驻留失效及M4.6归属持久化 | |
| 系统·世界｜水蒸汽火元胞运动模拟 | `context:GlobalDoc/CellularMaterials.md` | 现行：单格材料形态规则、调试笔刷、同步提交、存档与验证入口 | |
| 系统·世界｜分层群落世界生成v2 | `workspace:Docs/BiomeWorldGeneration.md` | 现行：保持v2气候地形植被复现，生态群落查询与地区内容的新入口及原生成验证 | |
| 系统·世界｜世界生态群落 | `context:GlobalDoc/WorldBiomes.md` | 现行：群落查询、地区生成与休眠、候选存档、背景装饰和验证边界 | |
| 系统·角色｜角色、怪物与战斗 | `context:GlobalDoc/SystemsActors.md` | 现行：玩家与Actor、八类怪物、命中死亡复活、地区休眠及成长缺口 | |
| 系统·角色｜Actors身份与地形边界 | `context:GlobalDoc/Actors.md` | 现行：Actor组件与生命周期；生产玩家构式归手持木棍，Gameplay v6保存 | |
| 系统·角色｜怪物内容与运行时 | `workspace:Docs/Monsters.md` | 现行：怪物资源配置、行为战斗、地区候选与休眠存档及验证 | |
| 系统·构式物品｜构式、资源与背包 | `context:GlobalDoc/SystemsFormulaItems.md` | 现行：木棍构式、F2掉落拾取、统一左键使用、移动后方块放置及快捷栏高亮 | |
| 系统·构式物品｜构式系统（Formula System） | `workspace:Docs/GameDesign/FormulaSystem.md` | 现行：函数组合与世界绑定设计；formula/2已实现范围和后续能力边界 | 当前设计入口；formula/2已实现，process/1兼容 |
| 系统·构式物品｜独立构式语言使用说明 | `workspace:Pixel/Source/Runtime/System/Formula/README.md` | 现行：System下独立DSL语法、CLI示例和构建测试入口；游戏通过Gameplay适配层接入 | 独立CLI与游戏适配并存 |
| 系统·构式物品｜构式过程语言v1与有界运行时 | `context:GlobalDoc/FormulaProcessRuntime.md` | 兼容：process/1编译与有界执行基础；当前formula/2差异见FormulaComposition | |
| 系统·构式物品｜FormulaBridge与程序承载者 | `context:GlobalDoc/FormulaBridge.md` | 现行：宿主基础与process/1兼容；formula/2绑定、伤害和保存见组合合同 | |
| 系统·构式物品｜构式程序状态与编辑合同 | `context:GlobalDoc/FormulaStateAndEditing.md` | 现行：木棍独立程序版本与F4编辑、工作台兼容、临时游戏与草稿保存边界 | |
| 系统·构式物品｜物品库存与开采资源合同 | `context:GlobalDoc/Items.md` | 现行：14类资源与独立木棍、4096总量、共享方块映射、预留转移和保存 | |
| 系统·构式物品｜构式函数组合与世界绑定实现 | `workspace:Docs/Architecture/FormulaComposition.md` | 现行：formula/2函数、Actor绑定、木棍承载、F4编辑与左键瞄准施放 | |
| 系统·构式物品｜背包装备与自动拾取系统设计 | `workspace:Docs/GameDesign/InventoryAndEquipmentDesign.md` | 设计提案：50格和通用装备；现有木棍掉落拾取、7＋35展示与统一使用 | |
| 系统·构式物品｜原生背包与快捷栏UI | `context:GlobalDoc/NativeInventoryUI.md` | 现行：7快捷格与35展示格、木棍手持高亮、同提交日历显示及画布缓存；无拖拽 | |
| 系统·家园｜设备、家园与会话进度 | `context:GlobalDoc/SystemsHome.md` | 现行：设备育圃与资源冻结、临时/持久会话、日历保存续接和产品边界 | |
| 系统·家园｜会话保存边界与旧档迁移 | `workspace:Docs/Architecture/SessionSaveBoundary.md` | 现行：临时与持久会话、完整提交保存、world.meta v7日历与Gameplay v6兼容 | |
| 系统·家园｜设备放置端口与最小工作状态 | `context:GlobalDoc/Devices.md` | 现行：推力设备、冻结和存档；M4.7同帧状态解释与授权观测 | |
| 系统·家园｜区域活动与设备程序冻结恢复 | `context:GlobalDoc/RegionActivity.md` | 现行：M4.5本地活动时间与身份续接；M4.6磁盘恢复另见Checkpoints | |
| 系统·家园｜游戏检查点与恢复 | `context:GlobalDoc/Checkpoints.md` | 现行：Gameplay v6物品与掉落、world.meta v7日历、跨域提交存档及恢复校验 | |
| 系统·家园｜设备库存与持续程序观测 | `context:GlobalDoc/GameplayObservation.md` | 现行：M4.7统一提交投影、库存授权与分页、设备状态与场源UI及验证 | |
| 系统·家园｜可保存的力场育圃贯穿场景 | `context:GlobalDoc/ForceGarden.md` | 现行：两花床与真实资源设备的开发场景；默认临时会话，成熟不产物 | |
| 系统·世界意志｜知识、世界意志与自主权 | `context:GlobalDoc/SystemsWorldWill.md` | 现行：授权观察、离线祈祷、排队执行、来源档案与保护撤销；网络未接入 | |
| 系统·世界意志｜有界领域观测与提交事实 | `context:GlobalDoc/ObservationProjection.md` | 现行：M2投影与事实；M4.7加入同提交设备库存程序和场源值 | |
| 系统·世界意志｜LLMView只读查询与离线贯穿样例 | `context:GlobalDoc/LLMView.md` | 现行：授权冻结分页与JSON；M4.7库存设备、M5.1知识历史和来源摘要 | |
| 系统·世界意志｜发现知识、重要历史与派生记忆 | `context:GlobalDoc/KnowledgeAndHistory.md` | 现行：有来源档案与授权查询；M5.4知识存档和祈祷激活证据 | |
| 系统·世界意志｜区域控制与干预撤销 | `context:GlobalDoc/Authority.md` | 现行：能力、保护与租约；M5.4同提交存档、重载核验和设备保护UI | |
| 系统·世界意志｜异步WorldWill与离线决策 | `context:GlobalDoc/WorldWill.md` | 现行：授权观察与受限提案提交、LLM抽象接线、测试worker及生命周期 | |
| 系统·世界意志｜持久援助、祈祷档案与自主权 | `context:GlobalDoc/PersistentInterventions.md` | 现行：M5.4离线育圃援助、保护撤销、同提交知识与权限存档、恢复和来源证据 | |
| 系统·世界意志｜模型接口与祈祷交互 | `context:GlobalDoc/ModelServiceIntegration.md` | 现行：模型注入、受限JSON、原文取消与Queued终态；Ready为兼容，未接网络 | |
| 系统·世界意志｜LLM抽象与随机PseudoLLM | `context:GlobalDoc/LLMAndPseudoLLM.md` | 现行：独立模型生命周期、共用决策API、随机函数组合兜底及验证边界 | |
| 系统·世界意志｜BehaviorDSL必要性与有限决策图实验 | `workspace:Docs/Architecture/BehaviorDecisionExperiment.md` | 现行：M6.1同场景表达、诊断和成本对照；暂缓持久DSL的依据与重启门槛 | |
| 系统·呈现｜画面、交互与内容资产 | `context:GlobalDoc/SystemsPresentation.md` | 现行：背景角色UI分层、昼夜与季节调色及时间显示、输入和资源分发边界 | |
| 系统·呈现｜游戏美术清单与资源缓存 | `workspace:Docs/ArtAssetIntegration.md` | 现行：Content美术清单、背景/地形/人物分层动画、共享缓存、GPU上传与验证 | |
| 工程｜分块、四叉树与流式世界 | `workspace:Docs/WorldRepresentation.md` | 现行：当前世界表示、材料调度、流式预算、文件与显示接口；空间场存储已统一 | |
| 工程｜统一游戏动作与会话控制入口 | `workspace:Docs/Architecture/GameActions.md` | 现行：统一手持使用与放置、排队终态、去重权限、暂停与持久会话边界 | |
| 工程｜固定步协调与完整提交 | `workspace:Docs/Architecture/SimulationCommits.md` | 现行：固定步与预算重试、WorldBuffers只读双缓冲、观测按需导出及GPU交接边界 | |
| 工程｜正常关闭请求与最终释放 | `workspace:Docs/Architecture/CloseRequests.md` | 现行：帧间关闭协商；默认临时会话直接关闭，持久会话先保存 | |
| 工程｜Core Allocator与容器后端的Pixel接入 | `context:GlobalDoc/CoreAllocatorAndContainers.md` | 现行：模板后端与实例资源选择、区块和scratch寿命、当前专项验证及限制 | |
| 工程｜工程入口与Runtime/Dev模块边界 | `workspace:README.md` | 现行：模块与输入分层、显示尺度、昼夜季节入口、构建运行和验证命令 | |
| 工程｜Pixel 性能采样模块说明 | `workspace:Pixel/Source/Dev/Debug/Performance/README.md` | 现行：PerfTest配置、CPU统计、三线程trace名称与帧标记、日志时钟和插桩入口 | |
| 工程｜Axiom通用CPU诊断 | `workspace:Axiom/Source/Core/Diagnostics/README.md` | 现行：PerfTest门控、RAII纳秒事件trace、聚合采样和静态动态链接合同 | |
| 工程｜Axiom统一日志 | `workspace:Axiom/Source/Log/README.md` | 现行：默认毫秒时间戳、日志级别和格式化接口、共享实例及静态动态链接验证 | |
| 工程｜Axiom跨平台系统信息查询 | `workspace:Axiom/Source/HAL/Platform/README.md` | 现行：CPU拓扑缓存指令集、GPU与内存、OS版本架构、未知语义及三平台构建验证入口 | |
| 工程｜世界空间场统一分块存储 | `workspace:Docs/SpatialFieldStorage.md` | 现行：页与元素对齐、tile和四叉树、多分辨率、GPU交换及已迁移字段清单 | |
| 工程｜游戏与渲染隔帧并行 | `workspace:Docs/Architecture/ParallelFrames.md` | 现行：引擎常驻游戏worker、上一帧呈现所有权、双线程诊断与CPU及GPU验证 | |
| 工程｜独立模拟线程与世界帧提交 | `workspace:Docs/Architecture/SimulationThread.md` | 现行：三线程所有权、角色预测、世界命令队列、完整提交与保存关闭和验证入口 | |
| 工程｜游戏float厘米制与网格单位边界 | `workspace:Docs/WorldUnits.md` | 现行：1格50厘米、float连续位置与人物运动、投影及旧构式/存档的显式换算 | |
| 资源｜ArtRaw素材原件与来源 | `workspace:ArtRaw/README.md` | 来源：19组下载与3组生成素材的入口、来源记录、运行时复制及完整性说明 | |
| 资源｜构式攻击特效素材与用途 | `workspace:ArtRaw/Effects/README.md` | 来源：新增11组免费攻击特效、实际文件、构式阶段映射和图集适配边界 | |
| 资源｜素材来源价格与商用许可总索引 | `workspace:ArtRaw/RESOURCE_INDEX.md` | 来源：19组下载与3组生成素材的逐包来源、价格、署名、商用条件及AI来源记录 | |
| 资源｜舒适山林四层视差背景试作 | `workspace:ArtRaw/Environment/NaturalParallax2K/README.md` | 来源：四张2560x1440透明分层贴图、原件与生成提示词、视差预览及镜像重复用法 | |
| 资源｜自然方块材质纹理试作 | `workspace:ArtRaw/Materials/NaturalBlocks/README.md` | 来源：12项自然方块与矿石纹理、64和256像素版本、五种矿石辨识预览及铺贴用法 | |
| 资源｜火焰水体与水汽静态纹理 | `workspace:ArtRaw/Materials/Elements/README.md` | 来源：六项64和256像素静态材质与透明效果、密度图、重复方向及已有动画入口 | |
| 资源｜人物动作与换装素材 | `workspace:ArtRaw/Characters/README.md` | 来源：五组免费像素人物、一个骨骼换装样例、预览、动作网格与来源许可 | |
| 资源｜人物装备换装与2D骨骼评估 | `workspace:ArtRaw/Characters/EquipmentAndSkeleton.md` | 来源：分层序列帧、武器挂点与骨骼的取舍；Spine样例许可及其他候选范围 | |
| 资源｜临时怪物素材与许可 | `workspace:RawData/Monsters/README.md` | 来源：八种横版像素怪物、免费原包、CC0许可、动作网格与本地预览 | |
| 资源｜群落地形原件与恢复 | `workspace:RawData/Materials/Biomes/README.md` | 来源：群落材质制品、生成记录、校验与Content恢复脚本 | |
| 资源｜沙漠雪山背景与恢复 | `workspace:RawData/Environment/Biomes/README.md` | 来源：两群落背景原件、提示词、预览、校验和运行时恢复 | |
| 证据｜世界改造验证与性能 | `workspace:Docs/WorldRepresentationValidation.md` | 原始证据：同步参考、三轮Release原始数据、CPU及GPU最终结果与独立审查结论 | |
| 证据｜领域观测Release基准原始数据 | `context:GlobalDoc/ObservationBenchmark-2026-10-22.csv` | 原始证据：M2.4三轮128帧静态与动态风有无投影的P50/P95、导出字节及容量 | |
| 证据｜LLMView Release基准原始数据 | `context:GlobalDoc/LLMViewBenchmark-2026-10-22.csv` | 原始证据：M2.5三轮128帧、三个观察者完整分页和JSON序列化耗时与保留字节 | |
| 证据｜构式语义对照Release原始数据 | `context:GlobalDoc/FormulaSemanticsBenchmark-2026-10-22.csv` | 原始证据：M3.1五场景三轮1000样本P50/P95、待办和工作计数；不同执行器范围见实验文档 | |
| 证据｜有限决策图Release成本原始数据 | `workspace:Docs/Architecture/BehaviorDecisionBenchmark-2026-10-24.csv` | 原始证据：三轮原生函数与决策图同输入求值P50/P95及保留字节；不含模型与模拟成本 | |
| 证据｜M6阶段决策实验Release复测原始数据 | `context:GlobalDoc/M6AcceptanceBenchmark-2026-10-24.csv` | 原始证据：阶段验收三轮原生与决策图同输入求值批均值P50/P95和保留字节 | |
| 证据｜构式升级验证 | `workspace:Docs/Architecture/FormulaCompositionVerification.md` | 原始证据：独立CPU与sanitizer复核，主agent GPU、关闭、Release成本及覆盖边界 | |
| 证据｜构式函数组合Release成本 | `workspace:Docs/Architecture/FormulaCompositionCost.txt` | 原始证据：三轮编译与包装/展开首片段成本；不含实例创建、世界提交和GPU | |
| 证据｜世界双缓冲验证与性能 | `workspace:Docs/WorldBuffersValidation.md` | 原始证据：只读版本、预算事务、独立审查、CPU/GPU验证及三轮Release取舍 | |
| 证据｜世界双缓冲Release原始数据 | `workspace:Docs/WorldBuffersBenchmark-2026-10-26.csv` | 原始证据：三轮同源基准的提交与F1打包耗时、World预留容量和校验值 | |
| 证据｜空间场存储验证与性能 | `workspace:Docs/SpatialFieldStorageValidation.md` | 原始证据：CPU、64格、ASan/UBSan、原生GPU及改造前后三轮Release结果与限制 | |
| 证据｜空间存储区域更新Release数据 | `workspace:Docs/SpatialStorage-regions-2026-10-29.csv` | 原始证据：三轮改造前后活动区域与全图参考的耗时、访问量、容量和材料哈希 | |
| 证据｜空间存储风策略Release数据 | `workspace:Docs/SpatialStorage-wind-2026-10-29.csv` | 原始证据：三轮六风策略空场墙体狭缝的耗时及速度散度质量诊断 | |
| 证据｜空间存储提交与打包Release数据 | `workspace:Docs/SpatialStorage-buffers-2026-10-29.csv` | 原始证据：三轮有无读者的动态风与运动材料提交、F1打包、容量及校验值 | |
| 历史｜devmain 重构适配（2026-10-16） | `context:GlobalDoc/DevmainAdaptation-2026-10-16.md` | 历史：新应用入口、服务所有权、输入注册、统一 overlay、配置与静态/共享构建；PixelMain rebase 和锁定版本 | 当前引擎接入契约 |
| 历史｜Pixel模块化系统设计 | `workspace:Docs/Architecture/PixelModularDesign.docx` | 历史：早期模块设计图；当前目录、依赖与世界所有权由README和WorldRepresentation替代 | 当前实现设计 |
| 历史｜游戏性能插桩与帧率分析 | `workspace:Docs/PerformanceProfiling.md` | 历史：2026-10采样与热点基线；现行工具与三线程trace见性能模块README | 已实现并实测 |
| 历史｜世界方块更新性能调查 | `workspace:Docs/MaterialUpdatePerformance.md` | 历史：旧移动链调查与成本基线；同步区域更新已由WorldRepresentation实现 | 调查完成；生产优化未实施 |
| 历史｜世界与力场内存统计 | `workspace:Docs/MemoryProfiling.md` | 历史：2026-10内存测量与口径；当前分块存储及诊断入口见空间场与性能模块 | 已实现并实测 |
| 历史｜风场可视化双模式 | `workspace:Docs/WindVisualizationSystemDesign.docx` | 历史：纯风速GPU纹理、短轨迹/柔和折光、人物前合成、参数与验证 | 首版实现并独立审查通过 |
| 历史｜构式语言架构（历史cast原型） | `context:GlobalDoc/FormulaLanguageArchitecture.md` | 历史：旧cast原型与编辑器已删除；现行formula/2见FormulaComposition | 独立模块架构；游戏接入见基础版实现 |
| 历史｜早期法术编程系统设计（历史提案） | `workspace:Docs/GameDesign/SpellProgrammingSystemDesign.docx` | 历史：2026-10-12 原始设计；当前名称为构式，现行规则与实现边界见构式系统及语言架构 | 保留 v0.1 历史依据，不作为实现完成声明 |
| 历史｜Astra 独立全代码审查（2026-10-05） | `context:GlobalDoc/CodeReview-Astra-2026-10-05.md` | 历史：干净上下文 xhigh 审查 Pixel 与本地 Axiom，P2 Cross、P3 Quaternion；覆盖清单与独立验证证据 | 两项已修复，原 Astra 复核通过 |
| 历史｜Pixel 代码审查（2026-10-04） | `context:GlobalDoc/CodeReview-2026-10-04.md` | 历史：全部 Pixel 代码与引擎集成审查，3 项 P2 缺陷、复现材料及修复验证 | F1–F3 已修复 |
| 历史｜devmain 兼容性复核（历史） | `context:GlobalDoc/DevmainCompatibilityReview.md` | 历史：旧 devmain 原始分支失败证据与通用能力移植后的重新验证 | 历史；当前接入见 2026-10-16 适配记录 |
| 历史｜Pixel 迁移记录 | `context:GlobalDoc/PixelMigration.md` | 历史：2DRender 历史依据、职责与路径映射、构建及验证证据 | 已迁移、验证 |
| 历史｜风场初版接口与模型 | `context:GlobalDoc/WindField.md` | 历史：简化NS求解、默认高度边界、泥土双向阻力反馈、F1调试视图、ImGui高度面板及近似限制 | 已实现 |
| 历史｜风向反馈诊断（2026-10-05） | `context:GlobalDoc/WindDirectionDiagnosis.md` | 历史：默认尺度诊断、伸入风区的窄土堆复现、方向性阻力与加权压力修复证据 | 已修复窄土堆反馈，仍为粗网格近似 |
| 历史｜风场策略首版 | `context:GlobalDoc/WindStrategiesV1.md` | 历史：六策略切换、四类候选首版、力场独立与数值/成本验证和独立审查 | 已实现，限制见正文 |
| 历史｜风场多尺度模拟方案评估 | `context:GlobalDoc/WindSimulationAlternatives.md` | 历史：四类候选方案、推荐混合结构、现有求解器成本基线与原型验证计划 | 四类 CPU 首版已实现，GPU 与后续目标未实现 |
| 历史｜逐像素力场与风场基准（2026-10-12） | `context:GlobalDoc/PixelWindBaseline.md` | 历史：连续基础力场、1:1 MAC硬边界基准、误差/成本实验、代码审查与使用边界 | 已实现，验证记录见正文 |
| 历史｜初版自然世界素材候选 | `context:GlobalDoc/NaturalWorldAssetResearch-2026-10-20.md` | 历史：免费试用的舒适自然横版像素素材、推荐组合、配套资源及替换边界 | |
| 历史｜分块程序化世界生成方案 | `context:GlobalDoc/WorldGenerationDesign.md` | 历史：已评审设计基线；实现与当前接口、材质JSON和验证入口见WorldGenerationImplementation | |
| 历史｜构式基础版游戏接入设计（历史） | `context:GlobalDoc/FormulaGameplayV1Design.md` | 历史：旧树构式接入设计与验收基线，仅供历史查阅 | |
| 历史｜构式基础版游戏接入实现（历史） | `context:GlobalDoc/FormulaGameplayV1Implementation.md` | 历史：旧树宿主、校验与编辑器的历史实现记录，已由process取代 | |
| 历史｜Gemini 游戏设计讨论原文 | `workspace:Docs/GameDesign/Pixel项目像素沙盒游戏设计探讨 - Google Gemini.pdf` | 历史：流程与玩法参考：祈祷、自主成长、世界意志、环境改造与家园；术语和技术假设需辨别 | |
| 历史｜架构规划旧地址 | `context:GlobalDoc/GameplayArchitecturePlan.md` | 合并跳转：重复架构正文已移除；原文与后续实施索引统一在Docs/Architecture | |
| 历史｜Pixel面向游戏愿景的架构规划 | `workspace:Docs/Architecture/GameplayArchitecturePlan.md` | 历史：2026-10架构规划及阶段实施索引；当前入口见系统说明与工程README | |
| 历史｜首个贯穿场景与GameAPI契约 | `workspace:Docs/Architecture/FirstGameplaySliceContract.md` | 历史：M1.1执行规格：观测与开采/激活场景、身份单位、动作效果事实、提交查询及验收映射 | |
| 历史｜构式语言语义选择（历史实验） | `context:GlobalDoc/FormulaSemanticsExperiment.md` | 历史：五场景对照与采用显式过程的依据；实验代码已移除 | |
| 历史｜M4持久设备与家园阶段验收 | `context:GlobalDoc/M4Acceptance.md` | 历史：八项子任务整合合同、当前CPU与GPU证据、独立审查、夹具修复及保存恢复边界 | |
| 历史｜M5可追溯世界意志阶段验收 | `context:GlobalDoc/M5Acceptance.md` | 历史：六项能力的异步、授权与持久化合同，CPU及GPU证据与独立审查 | |
| 历史｜M6运行时逻辑构建与DSL阶段验收 | `context:GlobalDoc/M6Acceptance.md` | 历史：暂缓持久DSL的场景依据、实验隔离、阶段复测与独立审查 | |
| 历史｜游戏愿景模块化架构总验收 | `context:GlobalDoc/GameplayArchitectureAcceptance.md` | 历史：六阶段29项任务的组合合同、CPU与GPU/AET验证、独立审查及范围限制 | |
| 历史｜Performance引擎迁移验收 | `workspace:Docs/DiagnosticsMigration.md` | 历史：通用诊断边界、PerfTest与纳秒trace、静态动态库及原生窗口验收结果 | |
| 历史｜Natural v1固定生成参数 | `workspace:Pixel/Source/Runtime/Simulation/World/Generation/Version1.md` | 兼容：保留旧生成版本复现；新游戏默认Natural v2 | |
| 系统·构式物品｜手持物品与木棍构式触发器 | `context:GlobalDoc/HandheldItems.md` | 现行：统一手持使用、身体外侧完整格放置、木棍构式与掉落拾取及v6存档 | |
| 系统·世界｜昼夜与季节实现合同 | `context:GlobalDoc/WorldCalendar.md` | 现行：20分钟昼夜、7天四季、完整提交、光照HUD、季末晨昏跳转及world.meta v7 | |
