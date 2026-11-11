# AxiomPixel

## 项目定位与阅读顺序

> 在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。

项目是以探索、构式创造、环境改造和家园生活为目标的像素沙盒原型。世界意志与轻量自动化服务于这些体验。设计愿景不等于功能交付；当前实现以当前工作区、系统合同和对应任务证据为准。

1. [游戏文档总入口](../Docs/GameDesign/README.md)：从总纲进入六个系统。
2. [产品现状与待办](GlobalDoc/ProductStatus.md)：当前试玩闭环、未交付能力与建议决策顺序。
3. [全量文档索引](GlobalDoc/DocIndex.md)：按总览、系统、工程、资源、证据、历史检索。
4. [工程README](../README.md)：源码归属、构建、操作与验证命令。
5. [文档维护规则](GlobalDoc/DocumentationGovernance.md)：功能变更如何同步文档，过时资料如何退出现行入口。

## 当前产品基线

核对日期：2026-11-10；包含本次开始前工作区中已有的 Actor、怪物与生态群落改动，不等同于最近一次 Git 提交。

- 默认 Natural v2 新世界并运行；普通游戏不保存进度，R 和重启创建新世界。显式持久会话保留检查点与旧档迁移。
- 地形、材料、风/力场与地区生态分开组织；探索生成当地生物，离区休眠，死亡后不补生。
- 玩家、居民、木头人、怪物沿用 Actor 身份与组件；居民已有简易几何呈现，暂无专用动画、AI、对话和生活行为。
- formula/2 是现行构式入口，process/1 是兼容层；手持木棍承载构式，左键统一使用物品。旧cast和树形编辑器已移除。
- 库存支持14类资源，4096总量容量；7快捷格与35展示格支持手持高亮；木棍掉落拾取已实现，通用格子堆叠与装备仍未交付。
- 推力设备与力场育圃构成最小家园实验；WorldWill 默认离线 PseudoLLM，真实网络模型未接入。
- 暂停冻结完整游戏逻辑并拒绝世界修改/新祈祷；自由相机、界面与显式单步仍可使用。

具体规则与限制只在相应系统文档维护，不在本入口重复复制长篇实现说明。

## 工程边界

本目录是 AWT Project 根，真实工程是父目录；工具入口为 `../AIWorkTask/awt`（相对真实工程）。本工程当前为 solo 项目，任务状态通过 CLI 维护。

游戏源码只放 `Pixel/Source/Runtime/{Gameplay,Simulation,Render,System}` 与 `Pixel/Source/Dev/Debug`。Gameplay主动查询/影响Simulation；Simulation不依赖Gameplay/Render。通用System服务不含游戏领域类型，`System/IO/Input`单独作为Gameplay输入适配；Application是组合根。

引擎位于本地 `Axiom/` 子模块、分支 `PixelMain`，由父仓库gitlink与AxiomVersion.json锁定。相邻 `../Axiom` 只读。引擎修改前读取其AGENTS；游戏专用渲染与测试留在Pixel。

运行时采用主线程呈现、游戏worker与独立模拟线程；世界修改通过队列在完整提交边界生效，Draw只消费自持有快照。合同见 [模拟线程](../Docs/Architecture/SimulationThread.md)、[游戏动作](../Docs/Architecture/GameActions.md) 和 README。

## 状态与证据

共享文档维护使用 `context project`、`doc add`、`doc index`；不为共享文档整理创建或重开已完成的功能任务。持续开发按 `task list`、`feedback list --pending`、`context build` 选择任务，完成时同步文档与真实验证证据。

M1–M6架构阶段已完成，但只覆盖当时定义的切片。历史通过记录不表示当前工作区自动通过，也不意味着远期生态、居民生活、装备或真实网络服务已经交付。
