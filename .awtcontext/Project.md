# AxiomPixel

AxiomPixel 是从 Axiom `2DRender` 分支拆出的早期像素游戏，源码位于 `Pixel/Source`。
`PixelWorld` 管理纯 CPU 世界，Environment 统一持有基础力场与风模拟；`PixelGameplay` 的 GameSession 持有世界和默认场景、暂停及单步策略。
`PixelInput` 的 GameInput 接收经过引擎UI/焦点过滤的键鼠事件，直接调用游戏会话、表现和相机功能，统一管理输入清理、模式切换、鼠标编辑与重置；平台退出、面板切换和framebuffer查询通过GameInputHost回调注入。
`PixelPresentation` 管理相机、外观与帧首快照转换；`PixelRender` 消费快照完成上传、世界与角色及风效合成、后处理。PixelInput链接CPU表现模块，不链接运行时或ImGui。
`Application/PixelGame` 直接实现 `IGameInterface`，只装配对象、编排生命周期并将输入接口转发给GameInput；`PixelRuntime` 通过 `RenderSetup` 创建策略，`PixelApplication` 创建游戏，应用所有的 DebugUI 通过引擎 overlay 回调叠加。`Debug/PixelDebugUI` 编辑各模块公开状态，模式切换统一进入GameInput；Performance 的采样核心和运行时策略分别构建。
目录、构建依赖和维护入口见工程 README 与 [模块化系统设计](../Docs/Architecture/PixelModularDesign.docx)。测试按领域归入 `UnitTest` 子目录，离线测量位于 `UnitTest/Benchmarks`，各模块拥有自己的 CMakeLists。

## 构式系统（Formula）

核心组合玩法正式命名为“构式（Formula）”，替代早期“法术”称呼（用户于 2026-10-13 确认）。构式是不可变、可嵌套的行为描述，修改器与装饰器的输出仍可作为下一节点输入；运行中弹体和效果指令属于独立执行状态。

`Pixel/Formula` 已实现可独立构建的 C++20 文本 DSL、不可变 `Formula` 描述、60 Hz CPU 模拟器、CLI 与行为测试。架构为“文本解析 → 不可变描述 → 独立模拟执行 → 效果指令/事件日志”；支持命名复用、管道修改、顺序组合、延迟、重复、散射与命中子构式。外层修改器只进入命中载体，不进入 payload。当前命中由宿主显式注入，尚未接入实际 World、碰撞、伤害与渲染，也没有费用或执行预算。

系统设计入口为 [构式系统](GlobalDoc/FormulaSystem.md)，当前实现契约见 [构式语言架构](GlobalDoc/FormulaLanguageArchitecture.md)，运行方法见 [模块 README](../Pixel/Formula/README.md)。2026-10-14 已完成代码命名迁移：类型 `Formula`、命名空间 `pixel::formula`、`.formula` 示例文件与 `PixelFormula` 构建目标；`cast` 语法及执行行为保留。早期 DOCX 与旧任务名是历史来源，不能据此认定生产方案已实现。

## 世界与现有玩法

基础世界为 1024×1024 格，上 512 行蓝色空气、下 512 行棕褐色土块。
左键放置、右键移除单格；每格 Force 保存空间受力方向，Environment 以固定步长按方向驱动土块并处理堆叠和碰撞。
方向支持八邻域和静止。游戏高度自底向上：底部600格为向下重力场，600高度以上叠加简化二维NS风场，故512～600高度之间有88格稳定天空。
存储行号自顶向下，对应y<424有风、y>=424仅受基础方向场影响，土地为y>=512。Environment拥有32×16连续风速网格，以60Hz推进平流、扩散和压力投影，再与基础Force合成移动方向。完整移动批次后，风区中的泥土按覆盖面积和实际移动速度向空气反馈阻力，再作压力投影；静止泥土减速，移动泥土带风。启动及R重置恢复可复现的风；基础Force不随物体搬迁。风采用周期边界和粗网格双向阻力耦合，尚无逐像素无穿透边界或方块惯性。默认地面仍在风区之外，88格稳定天空保留。参数和使用接口见README的简化风场章节。玩家已实现独立CPU矩形碰撞体、重力和跳跃，NPC仍为空入口，尚未引入脏区或多线程。

F1切换力场调试视图：所有格子的连续合成场方向单位化后，映射为RGB=(0.5+0.5*dx,0.5+0.5*dy,0.5)，零向量灰色。World仅提供数值采样，表现层在帧首生成颜色快照，复用现有数据纹理；Draw不读取实时世界，普通材质视图行为保留。

`Axiom/` 通过 Git 子模块引入，当前跟踪远端 `PixelMain` 并由父仓库 gitlink 锁定到 `6baecde`，准确提交见 `AxiomVersion.json`。该提交已包含鼠标事件快照、嵌入测试路径及数学/交换链修复；Pixel 已适配带事件参数的呈现变化回调及按 const 引用传递的 RecordFrame 测试接口，回归覆盖基类分派后的渲染资源重建。当前验证范围与 Linux GPU smoke 运行方式见工程 README，历史接入设计见 `GlobalDoc/DevmainAdaptation-2026-10-16.md`。
通用 RHI、Forward 渲染和 AET 留在引擎；相邻 `../Axiom` 仅供只读历史参考。
本目录为 AWT Project 根，真实工程为其父目录。
构建入口是工程根 CMakeLists.txt，运行命令见 `../README.md`，迁移事实见 `GlobalDoc/PixelMigration.md`。

Pixel默认显示ImGui Pixel Debug面板，Environment分组运行时设置从底部计的风场起始高度（0全世界、1024关闭、默认600），F2显隐；修改高度保留物体及基础场并重建风速，R恢复默认。方向视图也可用复选框切换。UI独立于3D场景，后处理结束前在present pass中叠加；输入经ImGui捕获过滤。

玩家默认标准模式：A/D移动、Space跳跃、相机跟随；F3切换自由调试相机（WASD平移、PageDown切区域），切换清空输入。角色使用纯色2×4格头身矩形，以连续屏幕坐标独立快照在世界Pass之后合成，不写地图材料或地图数据纹理；F1仍是纯力场诊断。碰撞和限制见README的角色操作说明。

Camera2D以世界格为单位保存双精度中心；标准模式速度为目标距离乘跟随系数（默认8/s），指数积分平滑追近角色，F3返回不瞬移。视口由相机中心、缩放和framebuffer生成；尺寸变化保留世界中心。调试面板可调Camera follow rate。

角色合成使用未取整Camera中心与世界位置差，解析矩形的屏幕像素覆盖以支持亚像素移动；每个frame slot独立RGBA8合成目标，F1或无角色跳过该Pass，角色合成后再后处理和UI。

风场可视化在世界与角色之间增加独立Pass，默认空气折光，可切短轨迹或关闭。Pixel表现层发布纯风速R32UInt快照，不包含重力；GPU通过局部回溯流纹表达方向和相对强度。固体与场外屏蔽，人物后绘制。参数与试用入口见README“风场可视化”章节，设计文档见GlobalDoc/DocIndex。
