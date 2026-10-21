# Pixel 游戏设计与策划文档

本目录集中维护游戏总纲、玩法设计和策划参考资料。后续设计从总纲中的体验目标与取舍原则出发，再结合具体系统的现行契约确定方案。

> 在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。

## 阅读入口

| 文档 | 用途与状态 |
| --- | --- |
| [游戏设计总纲](GameDesignVision.md) | 当前高层设计指导，包含游戏流程、家园与自动化、世界意志，以及构式作为世界运行原理之一的设想 |
| [构式系统](FormulaSystem.md) | 构式的概念、核心玩法、组合与作用域规则，以及已实现范围和后续方向 |
| [早期法术编程系统设计](SpellProgrammingSystemDesign.docx) | 历史提案；其中“法术”对应当前“构式”，具体规则和实现状态以现行文档为准 |
| [Gemini 游戏设计讨论](<Pixel项目像素沙盒游戏设计探讨 - Google Gemini.pdf>) | 外部讨论原始资料，供流程与玩法设计参考；术语和技术假设需结合当前项目辨别 |

## 相关工程文档

- [面向游戏愿景的架构规划](../Architecture/GameplayArchitecturePlan.md)：独立构式语言、领域模块、LLMView 与未来逻辑 DSL 的接入边界及迁移阶段；属于规划提案。
- [构式语言架构](../../.awtcontext/GlobalDoc/FormulaLanguageArchitecture.md)
- [构式基础版游戏接入设计与实施计划](../../.awtcontext/GlobalDoc/FormulaGameplayV1Design.md)
- [构式基础版游戏接入实现](../../.awtcontext/GlobalDoc/FormulaGameplayV1Implementation.md)
- [分块程序化世界生成方案](../../.awtcontext/GlobalDoc/WorldGenerationDesign.md)
- [世界生成与自然材质实现](../../.awtcontext/GlobalDoc/WorldGenerationImplementation.md)
- [世界表示与流式约定](../WorldRepresentation.md)
- [项目文档索引](../../.awtcontext/GlobalDoc/DocIndex.md)

## 维护约定

新增游戏总纲与玩法策划文档放在本目录，并通过 AWT 的 `workspace:Docs/GameDesign/文件名` 地址登记到项目文档索引。新增或移动文档时同步维护本阅读入口与引用，保持同一正文只有一个维护位置。

设计意图、待验证设想和已实现规则在各自文档中注明。原始讨论与历史提案保留其来源语境；当前设计选择在总纲或相应专题设计中维护。
