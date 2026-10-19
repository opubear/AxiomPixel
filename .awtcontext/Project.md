# AxiomPixel

AxiomPixel 是从 Axiom `2DRender` 分支拆出的早期像素游戏，源码位于 `Pixel/Source`。
`PixelWorld` 管理纯 CPU 世界，WorldMap 持有分块材料/基础力，Environment 负责合成受力与风模拟；`PixelGameplay` 的 GameSession 持有世界和默认场景、暂停及单步策略。
`PixelInput` 的 GameInput 接收经过引擎UI/焦点过滤的键鼠事件，直接调用游戏会话、表现和相机功能，统一管理输入清理、模式切换、鼠标编辑与重置；平台退出、面板切换和framebuffer查询通过GameInputHost回调注入。
`PixelPresentation` 管理相机、外观与帧首快照转换；`PixelRender` 消费快照完成上传、世界与角色及风效合成、后处理。PixelInput链接CPU表现模块，不链接运行时或ImGui。
`Application/PixelGame` 直接实现 `IGameInterface`，只装配对象、编排生命周期并将输入接口转发给GameInput；`PixelRuntime` 通过 `RenderSetup` 创建策略，`PixelApplication` 创建游戏，应用所有的 DebugUI 通过引擎 overlay 回调叠加。`Debug/PixelDebugUI` 编辑各模块公开状态，模式切换统一进入GameInput；Performance 的采样核心和运行时策略分别构建。
目录、构建依赖和维护入口见工程 README 与 [模块化系统设计](../Docs/Architecture/PixelModularDesign.docx)。测试按领域归入 `UnitTest` 子目录，离线测量位于 `UnitTest/Benchmarks`，各模块拥有自己的 CMakeLists。

## 构式系统（Formula）

核心组合玩法正式命名为“构式（Formula）”，替代早期“法术”称呼（用户于 2026-10-13 确认）。构式是不可变、可嵌套的行为描述，修改器与装饰器的输出仍可作为下一节点输入；运行中弹体和效果指令属于独立执行状态。

`Pixel/Formula` 已实现可独立构建的 C++20 文本 DSL、不可变 `Formula` 描述、60 Hz CPU 模拟器、CLI 与行为测试。架构为“文本解析 → 不可变描述 → 独立模拟执行 → 效果指令/事件日志”；支持命名复用、管道修改、顺序组合、延迟、重复、散射与命中子构式。外层修改器只进入命中载体，不进入 payload。当前命中由宿主显式注入，尚未接入实际 World、碰撞、伤害与渲染，也没有费用或执行预算。

系统设计入口为 [构式系统](GlobalDoc/FormulaSystem.md)，当前实现契约见 [构式语言架构](GlobalDoc/FormulaLanguageArchitecture.md)，运行方法见 [模块 README](../Pixel/Formula/README.md)。2026-10-14 已完成代码命名迁移：类型 `Formula`、命名空间 `pixel::formula`、`.formula` 示例文件与 `PixelFormula` 构建目标；`cast` 语法及执行行为保留。早期 DOCX 与旧任务名是历史来源，不能据此认定生产方案已实现。

## 世界与现有玩法

世界现为可用正负坐标的流式平坦地形，y<512空气、其他泥土。WorldMap以1024²独立64 KiB对齐Axiom载荷存储材料/方向/基础力，稀疏四叉树细分到64²活动与版本区域；玩家模拟窗口默认1024²，远处冻结并卸载保存，默认世界CPU预算512 MiB（含索引、scratch、CPU快照及I/O余量，风求解器/GPU另计）。

材料采用同步60 Hz：只申请步首空气，按源(y,x)裁决后批量提交，长链空位逐步传递；空间力不随材料移动。无风静止区域休眠，编辑、力变化、加载和移动唤醒邻域。未驻留区域阻挡角色/材料并显示占位，编辑返回NotReady。存档为Saves/<world-id>，R保存后创建新世界且保留旧文件。

风域跟随玩家并截取世界y<424，保留地表上方88格无风带。七后端迁移重叠状态，游戏使用环境流入/流出开放边界；周期模式仅显式数值测试使用。默认Empty且暂停，可单步/切换策略。材料、F1和风效使用可见区域快照，frame slot独立缓存并只上传变化区域，Draw不读取实时World。

现行接口、算法、预算与失败处理见 [世界表示约定](../Docs/WorldRepresentation.md)，实测、验证和审查证据见 [验证报告](../Docs/WorldRepresentationValidation.md)。早期世界/风场文档中整图数组、移动链、四张纹理与周期游戏边界已被替代。

`Axiom/` 通过 Git 子模块引入，当前跟踪远端 `PixelMain` 并由父仓库 gitlink 锁定到 `150ea98`，准确提交见 `AxiomVersion.json`。该提交包含通用纹理区域上传、鼠标事件快照、嵌入测试路径及数学/交换链修复；Pixel 已适配带事件参数的呈现变化回调及按 const 引用传递的 RecordFrame 测试接口，回归覆盖基类分派后的渲染资源重建。当前验证范围与 Linux GPU smoke 运行方式见工程 README，历史接入设计见 `GlobalDoc/DevmainAdaptation-2026-10-16.md`。
通用 RHI、Forward 渲染和 AET 留在引擎；相邻 `../Axiom` 仅供只读历史参考。
本目录为 AWT Project 根，真实工程为其父目录。
构建入口是工程根 CMakeLists.txt，运行命令见 `../README.md`，迁移事实见 `GlobalDoc/PixelMigration.md`。

Pixel默认显示ImGui Pixel Debug面板，Environment使用世界坐标Wind boundary Y（默认424），F2显隐；可查看驻留/排队、预算、I/O重试与手动保存。F1切换合成方向视图，暂停冻结模拟但允许加载/相机移动；UI输入经ImGui捕获过滤。

玩家默认标准模式：A/D移动、Space跳跃、相机跟随；F3切换自由调试相机（WASD平移、PageDown切区域），切换清空输入。角色使用纯色2×4格头身矩形，以连续屏幕坐标独立快照在世界Pass之后合成，不写地图材料或地图数据纹理；F1仍是纯力场诊断。碰撞和限制见README的角色操作说明。

Camera2D以块坐标和块内双精度保存连续中心；标准模式速度为目标距离乘跟随系数（默认8/s），指数积分平滑追近角色，F3返回不瞬移。视口由相机中心、缩放和framebuffer生成；尺寸变化保留世界中心。调试面板可调Camera follow rate。

角色合成使用未取整Camera中心与世界位置差，解析矩形的屏幕像素覆盖以支持亚像素移动；每个frame slot独立RGBA8合成目标，F1或无角色跳过该Pass，角色合成后再后处理和UI。

风场可视化在世界与角色之间增加独立Pass，默认空气折光，可切短轨迹或关闭。Pixel表现层发布纯风速R32UInt快照，不包含重力；GPU通过局部回溯流纹表达方向和相对强度。固体与场外屏蔽，人物后绘制。参数与试用入口见README“风场可视化”章节，设计文档见GlobalDoc/DocIndex。
