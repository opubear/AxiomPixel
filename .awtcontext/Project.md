# AxiomPixel

## 游戏设计总纲

> 在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。

Pixel 的高层设计以探索、构式创造、环境改造与家园生活为核心，轻量自动化支持长期积累，世界意志参与祈祷回应、环境干预及后期自主权的争取。后续玩法与系统设计参照[游戏设计总纲](../Docs/GameDesign/GameDesignVision.md)中的体验目标、隐性流程和取舍原则；具体实现状态见下文及各实现文档。

游戏设计与策划资料统一维护在 [Docs/GameDesign](../Docs/GameDesign/README.md)，AWT 索引通过 `workspace:` 地址引用正文。

总纲另包含构式作为世界底层运行原理之一的设计设想：在基础物理与角色活动等系统之上，优先用构式组织世界现象、设备行为和玩家能力。其覆盖范围及长期运行方式仍待验证，现有 Formula 语义与代码依赖保持原契约。

## 工程现状

AxiomPixel 是从 Axiom `2DRender` 分支拆出的早期像素游戏，源码位于 `Pixel/Source`。
`PixelWorld` 管理纯 CPU 世界，WorldMap 持有分块材料/基础力，Environment 负责合成受力与风模拟；`PixelGameplay` 的 GameSession 持有世界和默认场景，SimulationCoordinator管理固定步时钟、暂停/单步与提交版本，CommittedWorldView为呈现保留完整提交副本。
M2.4已在完整提交边界加入独立的 `ObservationProjector`：三槽有界区域缓存、拥有值的冻结材料/风/玩家/构式DTO、明确驻留覆盖，以及带因果来源和缺口报告的有限事实环。查询不加载世界、不借用渲染缓冲；外部LLMView的权限、租约和JSON查询留给M2.5。接口与容量/验证见 [有界领域观测与提交事实](GlobalDoc/ObservationProjection.md)。
`PixelInput` 的 GameInput 接收经过引擎UI/焦点过滤的键鼠事件，将玩法修改转换为游戏会话的类型化动作和控制请求，直接调用表现和相机功能，统一管理输入清理、模式切换、鼠标编辑与重置；平台退出、面板切换和framebuffer查询通过GameInputHost回调注入。
`PixelPresentation` 管理相机、外观与帧首快照转换；`PixelRender` 消费快照完成上传、世界与角色及风效合成、后处理。PixelInput链接CPU表现模块，不链接运行时或ImGui。
`Application/PixelGame` 直接实现 `IGameInterface`，只装配对象、编排生命周期并将输入接口转发给GameInput；`PixelRuntime` 通过 `RenderSetup` 创建策略，`PixelApplication` 创建游戏，应用所有的 DebugUI 通过引擎 overlay 回调叠加。`Debug/PixelDebugUI` 编辑各模块公开状态，模式切换统一进入GameInput；Performance 的采样核心和运行时策略分别构建。
目录、构建依赖和维护入口见工程 README 与 [模块化系统设计](../Docs/Architecture/PixelModularDesign.docx)。测试按领域归入 `UnitTest` 子目录，离线测量位于 `UnitTest/Benchmarks`，各模块拥有自己的 CMakeLists。

## 构式系统（Formula）

核心组合玩法正式命名为“构式（Formula）”，替代早期“法术”称呼（用户于 2026-10-13 确认）。构式是不可变、可嵌套的行为描述，修改器与装饰器的输出仍可作为下一节点输入；运行中弹体和效果指令属于独立执行状态。

`Pixel/Formula` 提供可独立构建的 C++20 文本 DSL、不可变 `Formula` 描述、60 Hz CPU 模拟器、CLI 与行为测试；支持命名复用、管道修改、顺序组合、延迟、重复、散射与命中子构式。外层修改器只进入命中载体，不进入 payload。2026-10-20 已接入游戏：右键从人物前方朝鼠标激活；独立 Formula Editor 修改树形草稿并 Apply；Gameplay 适配扫掠碰撞、开采与受限 Coarse 风脉冲，渲染黄色球。Environment、角色与构式按成功固定步推进，预算重试冻结待提交风域；checkpoint/1使用v5世界元数据并支持导入旧v2–v5档。游戏层校验结构及后代工作量，非法玩家程序与额度不足返回诊断，内部运行错误直接传播；Session对外仅提供World/Runtime的只读引用。World 与引擎不依赖 Formula；尚无伤害结算；费用与process/1运行实例保存见后续M4记录。

系统设计入口为 [构式系统](../Docs/GameDesign/FormulaSystem.md)，语言契约见 [构式语言架构](GlobalDoc/FormulaLanguageArchitecture.md)，游戏接入、额度及验证见 [基础版实现](GlobalDoc/FormulaGameplayV1Implementation.md)，操作见 [工程 README](../README.md)，独立运行见 [模块 README](../Pixel/Formula/README.md)。2026-10-14 已完成代码命名迁移：类型 `Formula`、命名空间 `pixel::formula`、`.formula` 示例文件与 `PixelFormula` 构建目标；`cast` 语法及执行行为保留。早期 DOCX 与旧任务名是历史来源，不能据此认定生产方案已实现。

M3 已完成独立 process/1 语言、有界运行时和 `Gameplay/FormulaBridge/` 三承载者接线；固定批次查询/碰撞、下一边界回执和清理规则见 [FormulaBridge](GlobalDoc/FormulaBridge.md)。M3.4 新增程序/实例 DTO 与 fake host 挂起恢复、效果所有权验证；F4 可编辑过程源码、Compile/Apply 并查看实例版本与剩余等待，旧树编辑器短期并存。真实库存、设备实体与持续来源已由M4.2–M4.4接入，M4.5已实现游戏区域冻结恢复，M4.6已接跨领域磁盘存档，详见 [状态与编辑合同](GlobalDoc/FormulaStateAndEditing.md)。

M4.2 已加入 Session 所有的 `PixelItems`：七种稳定物品定义、数量容量、版本化转移及显式预留/释放/消费。玩家直接开采和构式开采收进同一背包，满包保留地形；放土消耗 soil。首个设备成本已用开采所得验证，放置由 M4.3 实现。M4.6已保存库存，R建新世界仍为空；库存观测留给M4.7，详见[物品库存合同](GlobalDoc/Items.md)。

## 世界与现有玩法

世界现支持策略化Natural与Superflat：游戏新建默认自然土岩、洞穴、五矿种、湖泊/地下积水和干燥草皮；调试Flat保留y<512空气、其他泥土。旧v2存档映射到Flat，当前checkpoint/1持久化多领域一致状态，内部v5元数据保存生成器/种子/版本与完整固定步余量（可导入v2–v5），不重涂已有块。WorldMap以1024²独立64 KiB对齐Axiom载荷存储材料/方向/基础力，稀疏四叉树细分到64²活动与版本区域；玩家模拟窗口默认1024²，远处冻结并卸载保存，默认世界CPU预算512 MiB（含索引、scratch、CPU快照及I/O余量，风求解器/GPU另计）。自然生成额外scratch单独预留。

F2的World generation选择下一世界生成器与seed，R保存旧世界后创建；应用也支持PIXEL_WORLD_GENERATOR/PIXEL_WORLD_SEED。固定地层可开采而不坍塌，旧Dirt保持动态；Water按四邻接有限水量与Dirt同步提交，角色可入水与上浮，风将水视为边界。ArtRaw所需材质原样复制到Content，由ArtAssets.json的Terrain配置路径、映射与表面条带。具体合同见GlobalDoc/WorldGenerationImplementation.md及源码Generation/Version1.md。

材料采用同步60 Hz：只申请步首空气，按源(y,x)裁决后批量提交，长链空位逐步传递；空间力不随材料移动。无风静止区域休眠，编辑、力变化、加载和移动唤醒邻域。未驻留区域阻挡角色/材料并显示占位，编辑返回NotReady。存档为Saves/<world-id>，R保存后创建新世界且保留旧文件。

风域跟随玩家并截取世界水平Y边界（Flat默认424，Natural默认360），最高可能地表上方保留88格无风带，不贴合山坡。七后端迁移重叠状态，游戏使用环境流入/流出开放边界；周期模式仅显式数值测试使用。默认Empty且暂停，可单步/切换策略。材料、F1和风效使用可见区域快照，frame slot独立缓存并只上传变化区域，Draw不读取实时World。

现行接口、算法、预算与失败处理见 [世界表示约定](../Docs/WorldRepresentation.md)，实测、验证和审查证据见 [验证报告](../Docs/WorldRepresentationValidation.md)。早期世界/风场文档中整图数组、移动链、四张纹理与周期游戏边界已被替代。

`Axiom/` 通过 Git 子模块引入，当前跟踪远端 `PixelMain` 并由父仓库 gitlink 锁定，准确提交见 `AxiomVersion.json`。锁定版本包含通用纹理区域上传、鼠标事件快照、可接受/拒绝的应用关闭请求、嵌入测试路径及数学/交换链修复；Pixel 已适配带事件参数的呈现变化回调及按 const 引用传递的 RecordFrame 测试接口，回归覆盖基类分派后的渲染资源重建。当前验证范围与 Linux GPU smoke 运行方式见工程 README，历史接入设计见 `GlobalDoc/DevmainAdaptation-2026-10-16.md`。
通用 RHI、Forward 渲染和 AET 留在引擎；相邻 `../Axiom` 仅供只读历史参考。
本目录为 AWT Project 根，真实工程为其父目录。
构建入口是工程根 CMakeLists.txt，运行命令见 `../README.md`，迁移事实见 `GlobalDoc/PixelMigration.md`。

Pixel默认显示ImGui Pixel Debug面板，Environment使用世界坐标Wind boundary Y（默认424），F2显隐；Formula Editor由F4独立显隐；可查看驻留/排队、预算、I/O重试与手动保存。F1切换合成方向视图，暂停冻结模拟但允许加载/相机移动；UI输入经ImGui捕获过滤。

玩家默认标准模式：A/D移动、Space跳跃、相机跟随；F3切换自由调试相机（WASD平移、PageDown切区域），切换清空输入。角色使用纯色2×4格头身矩形，以连续屏幕坐标独立快照在世界Pass之后合成，不写地图材料或地图数据纹理；F1仍是纯力场诊断。碰撞和限制见README的角色操作说明。

Camera2D以块坐标和块内双精度保存连续中心；标准模式速度为目标距离乘跟随系数（默认8/s），指数积分平滑追近角色，F3返回不瞬移。视口由相机中心、缩放和framebuffer生成；尺寸变化保留世界中心。调试面板可调Camera follow rate。

角色合成使用未取整Camera中心与世界位置差，解析矩形的屏幕像素覆盖以支持亚像素移动；每个frame slot独立RGBA8合成目标，F1或无角色跳过该Pass，角色合成后再后处理和UI。

风场可视化在世界与角色之间增加独立Pass，默认空气折光，可切短轨迹或关闭。Pixel表现层发布纯风速R32UInt快照，不包含重力；GPU通过局部回溯流纹表达方向和相对强度。固体与场外屏蔽，人物后绘制。参数与试用入口见README“风场可视化”章节，设计文档见GlobalDoc/DocIndex。

M4.5已实现[区域活动与冻结恢复](GlobalDoc/RegionActivity.md)：设备、两类程序及归属弹体/场源保留同一身份和剩余活动时间，离域不补算、不重扣煤，返回前重新核验设备承载者。视觉加载和LLM查询不激活模拟，冻结仍占用有界容量；M4.6已接[不可变检查点](GlobalDoc/Checkpoints.md)，保存库存、设备、角色动力、process/1状态及归属效果。
