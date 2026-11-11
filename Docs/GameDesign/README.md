# Pixel 游戏文档总入口

> 在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。

本页从游戏策划与项目管理视角组织文档。先读总纲理解体验目标，再按系统查当前规则，最后进入实现与历史依据。核对日期：2026-11-10；当前工作区包含尚未提交的近期功能。

## 第一层：游戏目标与产品状态

| 入口 | 回答的问题 |
| --- | --- |
| [游戏设计总纲](GameDesignVision.md) | 这是一款什么游戏，玩家为何探索、创造和建设，哪些方向不采用 |
| [产品现状与待办](../../.awtcontext/GlobalDoc/ProductStatus.md) | 哪些体验已经可运行，哪些只是设计，下一步先决定什么 |
| [文档维护规则](../../.awtcontext/GlobalDoc/DocumentationGovernance.md) | 功能变化如何进入文档，如何处理重复、过时和历史资料 |

当前是开发原型。普通游戏默认运行且不保存进度；设备、育圃、构式和祈祷仍主要通过开发界面操作。底层持久化存在不等于默认存档开放，阶段任务完成也不等于完整游戏交付。

## 第二层：按游戏系统阅读

| 系统 | 玩家关注点 | 系统入口与下钻 |
| --- | --- | --- |
| 世界、环境与生态 | 探索什么，昼夜四季如何流转，材料如何变化，地区有什么生物 | [系统说明](../../.awtcontext/GlobalDoc/SystemsWorld.md) → 群落、生成、昼夜季节、元胞材料、风与力场 |
| 角色与战斗 | 我如何行动、施放、遭遇与战斗 | [系统说明](../../.awtcontext/GlobalDoc/SystemsActors.md) → Actor、木头人、怪物、地区休眠 |
| 构式、物品与背包 | 如何组合世界能力，怎样获得和管理资源 | [系统说明](../../.awtcontext/GlobalDoc/SystemsFormulaItems.md) → formula/2、库存、快捷栏、装备提案 |
| 设备、家园与会话 | 如何建设、消耗燃料、让环境支持生活 | [系统说明](../../.awtcontext/GlobalDoc/SystemsHome.md) → 推力设备、育圃、冻结、检查点 |
| 知识、世界意志与自主权 | 世界如何被理解，祈祷如何回应，如何保有控制权 | [系统说明](../../.awtcontext/GlobalDoc/SystemsWorldWill.md) → 观测、知识历史、模型提案、权限与撤销 |
| 画面、交互与内容资源 | 玩家看到什么，如何操作，素材从哪里来 | [系统说明](../../.awtcontext/GlobalDoc/SystemsPresentation.md) → 背景/动画/UI、显示尺度、资源与来源 |

每个系统说明区分体验目标、当前规则、可用入口、未交付项、依赖和验收切面；具体接口只在链接的专题正文维护。

## 第三层：设计与技术合同

| 类别 | 现行入口 |
| --- | --- |
| 构式设计与实现 | [构式系统](FormulaSystem.md)、[formula/2合同](../Architecture/FormulaComposition.md)、[独立语言README](../../Pixel/Source/Runtime/System/Formula/README.md) |
| 资源管理设计 | [背包、装备与自动拾取提案](InventoryAndEquipmentDesign.md)：50格与通用装备/资源掉落仍为提案，木棍拾取已实现；现状见系统页 |
| 工程模块与构建 | [工程README](../../README.md)：Gameplay/Simulation/Render/System与Dev边界、操作、构建验证命令 |
| 动作与运行时 | [游戏动作](../Architecture/GameActions.md)、[独立模拟线程](../Architecture/SimulationThread.md)、[完整提交](../Architecture/SimulationCommits.md) |
| 世界数据与单位 | [世界表示](../WorldRepresentation.md)、[空间场存储](../SpatialFieldStorage.md)、[厘米与格单位](../WorldUnits.md) |
| 昼夜与季节 | [日历与呈现合同](../../.awtcontext/GlobalDoc/WorldCalendar.md)：默认节奏、季节日长/调色、暂停提交和v7保存 |
| 会话与恢复 | [保存边界](../Architecture/SessionSaveBoundary.md)、[检查点](../../.awtcontext/GlobalDoc/Checkpoints.md) |
| 全量分类检索 | [AWT文档索引](../../.awtcontext/GlobalDoc/DocIndex.md)：包含系统、工程、资源、证据与历史 |

## 第四层：历史与原始参考

历史资料解释当时的选择，不覆盖现行规则。旧路径与测试数字只对原时点有效。

- [2026-10架构规划](../Architecture/GameplayArchitecturePlan.md) 与 [阶段总验收](../../.awtcontext/GlobalDoc/GameplayArchitectureAcceptance.md)：已实施的阶段规划及当时证据。
- [旧cast语言](../../.awtcontext/GlobalDoc/FormulaLanguageArchitecture.md)、[旧基础版设计](../../.awtcontext/GlobalDoc/FormulaGameplayV1Design.md)、[旧实现](../../.awtcontext/GlobalDoc/FormulaGameplayV1Implementation.md)：相关树形原型和编辑器已移除。
- [早期法术编程设计](SpellProgrammingSystemDesign.docx)：历史提案，现行术语为“构式”。
- [Gemini讨论原文](<Pixel项目像素沙盒游戏设计探讨 - Google Gemini.pdf>)：外部讨论资料，不直接成为需求或已实现规则。

新增或修改功能时，按 [维护规则](../../.awtcontext/GlobalDoc/DocumentationGovernance.md) 同步对应系统与唯一正文，再通过AWT登记索引；任务进度和验收不在本目录复制维护。
