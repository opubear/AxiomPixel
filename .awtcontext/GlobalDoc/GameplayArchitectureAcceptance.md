# 游戏愿景模块化架构总验收

2026-10-24。基线为 `13cc7cdfdb506bc7194df134a12277ac800bd234`（已提交 M5/M6 阶段验收）。本次完成 AWT 总任务“按游戏愿景推进模块化架构建设”的整合验收：六个阶段、29个叶子任务均已完成，各子任务需求与验收已通过。本轮核对最终代码与合同并重新运行跨阶段验证，没有修改生产代码、测试或引擎。

验收范围是[架构规划](../../Docs/Architecture/GameplayArchitecturePlan.md)约定的观测、构式、设备家园和世界意志贯穿切片，不代表[游戏设计总纲](../../Docs/GameDesign/GameDesignVision.md)全部玩法已实现。原规划正文保留规划时基线，当前实现以本记录及专题合同为准。

## 六阶段结果

| 阶段 | 当前交付与整合关系 | 合同与主要验证 |
| --- | --- | --- |
| M1 应用边界与动作 | 组合根装配领域、输入和呈现；持久化实现、运行校验与编辑器分离；Input/Debug/Driver 共用有来源、权限与去重的类型化动作 | [GameActions](../../Docs/Architecture/GameActions.md)、Input、FormulaGameplay、Persistence、SessionStore |
| M2 时钟与观测 | 成功固定步才统一提交；预算等待不重复推进其他领域；关闭协商先保存；冻结 DTO、因果事实和独立 LLMView 具有容量、未知、权限、分页与租约语义 | [SimulationCommits](../../Docs/Architecture/SimulationCommits.md)、[ObservationProjection](ObservationProjection.md)、Session/Simulation、GameplayObservation、LLMView/LLMJson、关闭测试 |
| M3 独立构式与宿主 | process/1 可独立编译运行；有界步骤、等待、事件、状态及效果所有权由语言和宿主合同划分；玩家、天然来源、固定夹具共用桥接，后续接入真实设备和干预 | [FormulaBridge](FormulaBridge.md)、[状态与编辑](FormulaStateAndEditing.md)、独立 Formula 7项、Process 系列、Formula GPU/UI |
| M4 持久设备与家园 | Session 拥有角色、库存与设备；真实开采、建造、供煤和持续来源驱动育圃成长；区域冻结不补算，检查点一致保存运行状态和效果 | [M4验收](M4Acceptance.md)、Actors/Items/Devices/FieldSources/RegionActivity/Checkpoint/ForceGarden、育圃 GPU/UI |
| M5 世界意志与自主权 | 发现和历史保留来源；模型在固定步外产生提案；主线程按当前权限、设备、程序和资源裁决；祈祷、Ready、取消、保护撤销及干预 continuation 可追踪、可保存 | [M5验收](M5Acceptance.md)、Authority、WorldWill/Worker、InterventionCheckpoint、WorldWillCodec、ModelProtocol、PseudoLLM、援助 GPU/UI |
| M6 运行时逻辑实验 | 同场景比较原生函数组合和有限决策图；现有 DecisionAPI 提案与 Formula 持续执行足够，暂缓持久 BehaviorDSL | [M6验收](M6Acceptance.md)、[必要性实验](../../Docs/Architecture/BehaviorDecisionExperiment.md)、本轮全量 CPU 中的 BehaviorDecision 测试 |

贯穿结果是：观察天然晶体并应用构式，从真实矿脉获得资源，建造供煤设备使两个花床成长；离域或保存重建后按剩余活动时间接续。离线祈祷也可提出临时推力援助，Ready 本身不改变世界，执行后形成实际实例、场源与成长，保护或撤权同时清理干预及其后续作用。模型说明、实际动作、原始事实和派生知识分别归属。

## 最终依赖与提交边界

- `PixelWorld` 只链接 `PixelPerformance` 与 `Axiom::Core`；不依赖角色、Formula、渲染或模型。角色与具体育圃规则位于 Pixel 的游戏层。
- 独立 `Pixel/Formula` 只依赖标准库及自身宿主合同。`FormulaBridge` 解释游戏查询和效果；`PixelGameplay` 不链接 `PixelFormulaEditing`。程序、实例、承载对象和领域效果分别拥有状态。
- `PixelObservationValues` 及其 Devices/Items 值依赖不接运行时；`PixelLLMView` 消费观测值并公开使用 nlohmann JSON 头文件，不链接引擎运行时；`PixelModelProtocol` 的 JSON 依赖为私有。`PixelWorldWill` 只链接 LLMView 与 LLM；模型拿到拥有值的输入，通过受限 DecisionAPI 提案，没有 Session/World 可变指针或调用者凭据。
- `PixelGame` 显式持有 Session、模型、WorldWill 与呈现组件。WorldWill 在模型和 Session 之前销毁。服务帧处理外部结果，模拟只在安全边界接受动作；等待真实 worker 时固定步仍可推进。
- 完整步顺序为 Environment → Actors → FormulaBridge → Devices/育圃 → Tick/Revision 提交 → 过程事实、权限/租约清理与设备事实 → 发布。预算等待保留最后完整观测与呈现，不发布半步状态。
- 检查点采集同一提交的世界、角色、库存、设备、程序、归属效果、知识、权限和祈祷，再由 SessionStore 写不可变世代并最后发布指针。恢复续接已执行状态，旧 Thinking/Ready 标为 Interrupted，不重放推理或动作。关闭协商负责保存，GameEnd 只关闭 WorldWill/模型与输入。

引擎继续提供通用 Core、平台、应用生命周期、RHI 与 Forward/AET。Pixel 专用渲染、shader、领域规则和测试留在父工程。本地 `Axiom/` 的 PixelMain HEAD、父仓库 gitlink 与 `AxiomVersion.json` 均为 `3676a9f4dfa7f64c056aeb5294b5c681c64411b0`，引擎工作区干净；相邻 `../Axiom` 未修改或构建。

## 本轮重新运行的验证

环境为 macOS/Darwin arm64、Apple clang 21.0.0（`/usr/bin/c++`），GPU 为 Apple M5 / MoltenVK。CPU 与 GPU 由不同独立验证 agent 执行；下表 CTest 时间来自其汇总，smoke 时间为进程墙钟时间。

| 验证 | 本轮结果 |
| --- | --- |
| 默认1024格、Debug、runtime OFF 全量构建与 CTest | 94/94，通过，48.35秒 |
| Formula 作为独立 CMake 工程，Debug 构建与 CTest | 7/7，通过，0.55秒 |
| 64格区块、Release 全量 CPU 构建与跨阶段专项 | 32/32，通过，3.31秒 |
| PixelObservationDemo | 运行成功，输出 JSON 可解析 |
| PixelWorldWillDemo | worker pending 时模拟到 tick 4；交付后 Applied，动作序号1、事实2条、水平加速度100、剩余干预槽3 |
| Pixel、嵌入 Axiom/AET 与全部 smoke 目标 | 构建成功；ApplicationClose CTest 1/1通过 |
| 完整 PixelRuntimeSmoke | 通过，130.54秒；地形/美术/风/输入、Debug、构式 GPU/编辑器、设备、育圃、祈祷/取消/保护、两轮 resize/minimize 生命周期 |
| PixelCloseSmoke | 通过，2.62秒；延期/失败保存拒绝关闭、可见提示、显式重试与不重复保存的 GameEnd |
| AxiomLifecycleSmoke --skip-minimize | 两轮通过，9.68秒 |
| AxiomCloseRequestSmoke | 通过，1.27秒 |
| 本地 Axiom 独立 AET 配置与构建 | 通过，构建164.23秒，RelWithDebInfo |

首次受限环境中的 PixelRuntimeSmoke 停在窗口初始化，并伴随 macOS hiservices 通知错误；终止该次自有进程后，经正常权限审批访问本机窗口/Vulkan 服务，重跑完整 smoke 通过。该次初始化停滞不计为通过。独立 AET 配置还遇到旧缓存中的失效 slangc 路径；仅清除 `AXIOM_SLANGC_EXECUTABLE` 缓存覆盖后重新选择仓库编译器，不改源码。

CPU 构建出现已有的重复链接 `libPixelPresentation.a` 警告；本机缺少 `VK_LAYER_KHRONOS_validation`，GPU 结果不包含 Vulkan validation layer 检查。AET 生命周期使用 `--skip-minimize`，其最小化恢复未覆盖；Pixel 的两轮最小化恢复已执行。

复跑 CPU 与独立语言：

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4
cmake --build build-cpu --target PixelObservationDemo PixelWorldWillDemo -j4
./build-cpu/bin/Debug/PixelObservationDemo
./build-cpu/bin/Debug/PixelWorldWillDemo

cmake -S Pixel/Formula -B build-formula -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula -j4
ctest --test-dir build-formula --output-on-failure

cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-chunk64 -j4
ctest --test-dir build-chunk64 -R '^Pixel\.(Generation|Liquids|GenerationSession|TerrainAssets|Cpu|Chunks|Persistence|WindDomains|Actors|Items|InventoryGameplay|Devices|FieldSources|RegionActivity|Checkpoint|ProcessPersistence|GameplayObservation|ForceGarden|DeviceRendering|Knowledge|KnowledgeView|KnowledgeCodec|Authority|AuthorityView|WorldWill|WorldWillWorker|InterventionCheckpoint|WorldWillCodec|ModelProtocol|PseudoLLM|LLMView|LLMJson)$' --output-on-failure -j4
```

完整窗口、关闭和 AET 命令见 [README 验证入口](../../README.md)。本轮独立 AET 配置在该命令基础上加 `-DAXIOM_SLANGC_EXECUTABLE=` 清空失效缓存。原始日志在本机 `/private/tmp/pixel-architecture-acceptance-cpu-cpu/`、`/private/tmp/pixel-architecture-acceptance-cpu-formula/`、`/private/tmp/pixel-architecture-acceptance-cpu-chunk64/` 与 `/private/tmp/pixel-architecture-acceptance-gpu-*`；子任务审计输出为 `/private/tmp/pixel-architecture-task-audit.json`。这些是本次本机诊断材料，不是仓库长期依赖。

## 文档同步与独立审查

本次修正 GameActions、SimulationCommits、CloseRequests、SessionSaveBoundary、FormulaBridge/状态编辑和 ObservationProjection 合同中的早期阶段描述：事实与冻结查询、真实设备与干预承载者、process/知识持久化、关闭模型及恢复语义均已落地；`inventory()` 仍是过程 Credits 查询，实际物品容器和燃料由游戏宿主拥有。未为文档验收引入新抽象、异常兜底或无需求的测试。

独立 review 已核对设计正文、六阶段合同、CMake、组合根、动作/发布/存储、FormulaBridge/ProcessHost、设备/育圃、观测/LLMView 和 WorldWill/Authority，未发现需修复的代码问题。最终文档与CPU/GPU/AET日志、子任务审计及链接已复核；审查指出的LLMView公开JSON头文件依赖和CTest名称表述已修正，并明确三个原生槽加一个临时外部捕获槽。无未决审查意见。

Reviewer 未另行构建运行，不将只读检查作为额外测试次数；审查覆盖上述组合边界及关键路径，没有逐行全面审查全部求解器、语言内部或引擎源码。V1/V2的当前证据支持完成架构总任务。

## 保留的限制

检查点不提供 fsync/断电耐久承诺，当前同步采集可能复制全部区块；活动旧树实例不能保存，process/1 按当前状态契约保存。离域冻结不补算，育圃只是两个固定花床，开花没有额外物品产出。居民完整行为、生产网络和完整生态仍属后续玩法。

没有接入真实模型网络或凭据，也未验证真实模型质量；默认输入尚未自动加入知识历史或植物主题。持久 BehaviorDSL 未交付；M6 的三轮 Release 基准和 BUILD_TESTING=OFF 构建隔离沿用该阶段明确记录的证据，本轮没有重跑基准或生产关闭测试目标配置。本轮未做 sanitizer、全平台 GPU 或新性能基准。

本总任务完成后按用户要求暂停，历史 Core Allocator 任务不属于本轮。
