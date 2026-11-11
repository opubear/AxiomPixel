# 构式函数组合与世界绑定（formula/2）

初版：2026-10-26；现状核对：2026-11-10。这里描述已实现的构式合同；设计动机见[构式系统](../GameDesign/FormulaSystem.md)，验证记录见[独立验证](FormulaCompositionVerification.md)。函数与不可变描述位于 System/Formula，世界绑定和领域 API 位于 Gameplay/FormulaBridge。Simulation、Axiom 不依赖构式。本次没有需要放入引擎的通用能力变更。

## 使用

```text
formula 2;
fn impact_shot(velocity: speed, impact: action) -> action =
    on_hit(bolt(10, velocity, ticks(90)), impact);
fn burst(count: number, shot: action) -> action = repeat(count, ticks(30), shot);
let mining = dig(cells(2));
run burst(3, impact_shot(speed(60), mining));
```

所有函数使用 `name(arguments...)` 调用，参数按声明顺序传入。定义先于引用，词法作用域，参数不能覆盖已有名字；函数返回值必须与声明类型一致。`let` 绑定不可变值；将一个参数固定在包装函数中即表示部分参数绑定，不另设模块或半成品类型。函数可以调用之前定义的函数，也可以接收、返回 action 并继续组合。没有递归、赋值、原生对象或任意文件/网络能力。

每个文件只能有一个末尾 `run action;`。定义、调用描述构造函数、Compile 和 Apply 均不执行世界效果；激活实例才执行。编译时展开描述、生成有界指令，程序保存原始源码、SHA-256 和版本身份，运行时不解析函数或查找资源。共享的 ProgramArtifact 不可变，运行进展按实例分别持有。以同一输入将用户函数按定义展开，其请求顺序、活动边界、参数及空间上下文相同。

## 类型与函数

类型为 `number`、`cells`、`speed`、`acceleration`、`ticks`、`action`，不做隐式单位转换。数字支持小数及负号，不支持指数记法；字面量与计算结果绝对值至多 1e9。半径非负；ticks 是 1..1000000 的整数。数值和描述都按值绑定；执行时的世界观测由专门的动作组合函数请求。

| 签名 | 描述与上下文 |
|---|---|
| `cells(number) -> cells` | 半径/距离单位 |
| `speed(number) -> speed` | 格/秒 |
| `acceleration(number) -> acceleration` | 格/秒² |
| `ticks(number) -> ticks` | 本实例活动步，非墙钟 |
| `add(number, number) -> number`、`multiply(number, number) -> number` | 编译阶段的纯数值计算 |
| `dig(cells) -> action` | 在当前起点开采圆域；半径0选所在格，遵从材料和库存规则 |
| `wind(cells, speed, speed) -> action` | 向当前起点请求风脉冲；两分量按实例方向旋转，宿主仍要求可用 Coarse 风后端 |
| `bolt(number, speed, ticks) -> action` | 伤害、前向速度、寿命；等待一次命中或到期后结束该描述，游戏宿主对活怪物结算伤害 |
| `field(cells, acceleration, acceleration, ticks) -> action` | 安装有限寿命场源；方向同 wind，等待该寿命结束；场源仍由世界拥有实际模拟状态 |
| `sequence(action, action, ...) -> action` | 2..8项，左到右；前项完成后才执行后项 |
| `delay(ticks, action) -> action` | 先等待，再执行描述 |
| `repeat(number, ticks, action) -> action` | 1..64次；前次完成后等待间隔再执行下次，最后一次后无附加间隔 |
| `on_hit(action, action) -> action` | 第一项必须构造为 bolt（可以经用户函数返回）；只在该弹体命中时执行第二项一次 |
| `if_credits(number, action, action) -> action` | 读取当前片段冻结的实例 credits；达到非负整数阈值执行第二项，否则第三项 |
| `on_wake(ticks, action) -> action` | 有限等待当前订阅；收到 wake 后执行一次，超时或其他非 wake 结果跳过子描述 |
| `nothing() -> action` | 空描述，用于条件分支 |

基础函数和用户函数共享类型及调用规则；例如函数可以返回 `cells` 用作 dig 参数，也可以把 `on_hit(...)` 返回的 action 交给 repeat。非法类型、重复名字、未知调用和无效参数返回带原始行列的编译诊断。`on_hit` 的载体约束在描述构造阶段检查。游戏几何、权限、资源条件由运行宿主检查；描述合法不表示必然生效。

本版没有并行分支、散射、通用对象选择、独立子实例、对象旋转、运行中重绑定或随机数。这里的 action 是统一行为值，不是对过程脚本增加宏替换指令的公开接口。旧 process/1 的句柄式更新/停止场源仍可读取；formula/2 的场源采用有限寿命组合，暂不暴露句柄操作。

## 事件、时间、失败与作用域

所有实例先读取冻结状态并求值，碰撞也在同批世界修改前确定，再按激活次序和指令顺序提交。所有效果经 ProcessHost → FormulaWorldAdapter / 世界领域 API 提交。构式不能直接持有 World 或 Actor，也不能调用存档服务。

每个 dig/wind/field 请求后都等待下一活动边界的结果。成功继续，拒绝或结果超时停止当前根；已提交的世界事实不回滚。field 的第一个结果等待计入声明寿命，之后等待剩余 N-1 步；寿命1无需额外等待。bolt 等待其寿命加1步的命中/终态窗口；到期正常结束，拒绝、取消或超时停止根。回执到达之前不能开始依赖它的后续操作。相同边界的命中事件最早下一活动边界执行子描述；早到事件按 request ID 缓存且只消费一次。

`on_hit` 为载体保存外层空间上下文。命中后，子描述固定使用精确命中格坐标和格内偏移，跨 delay、repeat、保存、冻结仍保持该位置；子描述完成后恢复父上下文。根上下文重新解析当前绑定位置，嵌套命中则恢复上层固定命中点。方向继承实例显式角度，不使用碰撞法线。没有独立子实例：所有组合属于同一根，取消、停止、故障和正常完成清理该根所属的弹体、场源、订阅和通知。已提交开采和风脉冲保持。

`if_credits` 读取本片段冻结值；它不是背包查询。每个成功 dig/wind/bolt/field 请求扣1 credit，拒绝不扣；真实设备燃料及玩家库存仍由 Gameplay 单独处理。Wake 只针对当前订阅，不能预存来唤醒未来等待。

宿主回执保存边界、实例/request ID、来源 kind/subject、结果、拒绝原因、改变格数、程序内容身份、原始源码位置与实际请求起点。结果包括 Resources、Capability、Scope、Geometry、NotReady、Blocked、WindUnavailable、Capacity 等现有原因。诊断和回执各保留最近256条；源码位置指向实际基础作用的定义位置，复用的不同调用通过实例和 request 序号区分。旧存档没有的历史源码追踪字段保持为空，不伪造来源。

## 世界元信息与实例生命周期

`ProcessCarrier` 由可信 Gameplay 装配，不接受 DSL 或模型自报权限。构式体、元信息和执行进展分开：

| 部分 | 状态与权威来源 |
|---|---|
| 构式体 | ProgramArtifact：原始源码、精确身份、不可变指令 |
| 来源 | Kind/Subject：玩家、天然来源、固定夹具、设备或干预；与附着对象独立 |
| World 绑定 | Origin 是权威世界位置，AngleDegrees 是显式世界方向 |
| Actor 绑定 | AttachedActor 是本世界稳定 ActorId，LocalOffset 是相对 Body 起点的偏移；Origin 是解析结果缓存 |
| 授权与条件 | 固定 Scope、Grants、MaximumRadius、ActivityRadius、Credits、片段 Limits |
| 执行状态 | PC、局部计数器、剩余等待、事件水位、命中上下文栈；不保存在描述中 |

2026-11-10 怪物接入后，bolt 对活怪物结算 damage，木头人只触发命中事件，Player/Resident 不接受构式友伤。弹体出生重叠与高速扫掠采用相同结算规则，详见[怪物合同](../Monsters.md)。

Actor 当前只有平移，没有旋转姿态，因此局部轴与世界轴平行，偏移不自动旋转，AngleDegrees 仍显式指定。FormulaBridge 只读取 Actors，不修改其生命周期；GameSession 在装配时绑定同一 Actor 注册表。Actor 绑定仅允许 formula/2；process/1 继续保持原来的世界起点语义。

在安全边界解析 Actor 位置，后续请求从新位置产生。弹体发射后按其世界位置/速度运动；已安装场源停留在安装的世界位置，不随 Actor 移动。Actor 销毁立即取消根并清理归属效果，发出 ActorBindingLost 诊断；移动出授权 Scope 则取消并记录 ActorBindingScope，不扩大权限。世界载体仍执行原有请求范围校验。

位置仍在授权域内但离开模拟窗口、缺失必要驻留、当前固定命中点或归属效果不活动时，冻结整个根及其效果。返回后继续剩余时间，不补跑。冻结仍占容量。全局暂停、环境预算等待不调用活动 Step；全局暂停也拒绝新的激活与编辑。作用域取消和普通终止不退款已发生的游戏费用。

## 编辑、版本与存档

F4 的 Formula Editor 保留既有 `Process source`、`Process text`、`Compile process`、`Apply process` 控件名，接受新旧源码。加载三脉冲草稿会得到 formula/2 组合示例。Compile 不发布，Apply 只用于之后的激活，旧实例保留原内容身份。玩家默认弹体、真实推力设备、育圃晶体和援助程序均已采用 formula/2。

展开 `Process: three carriers`：Start natural source / Start fixed fixture 启动世界定位实例；Start actor attachment 以独立来源 FixedFixture/9001 附着玩家，偏移(3,1)，使用当前同一构式。实例列表显示绑定类型、Actor、来源、格坐标、方向及运行进展，回执区显示最近8次调用的位置与拒绝码。未应用草稿不保存。

| 数据 | 当前版本与兼容处理 |
|---|---|
| ProgramRecord | 格式1；process/1 身份=语义1/宿主2，formula/2 身份=语义2/宿主3；按原源码重新编译后核对完整身份 |
| InstanceSnapshot | process/1 仍导出格式2且保持原指令布局；formula/2 格式3额外保存 pinned 标志和完整命中作用域栈 |
| Pixel.ProcessHost | 写格式2，读取1/2；格式1承载者明确迁移为 World 定位，历史回执缺失的追踪字段为空 |
| Pixel.FormulaBridge | 外层格式仍为2，内部宿主有独立版本；程序、活动/冻结实例、事件和弹体一起恢复 |

以下保存能力用于显式 Persistent 会话；默认游戏为 Temporary 模式，退出后不保留进度。

保存恢复从原挂起点继续，不重新激活，不重发请求，不重新扣 credit/燃料。持续场状态与 Actor 状态由同一 Gameplay 检查点保存。Actor 引用按稳定 ID 解析，缺失引用明确拒绝恢复；未知版本、错误身份、异常上下文栈或非法额度同样拒绝，不清空存档。编辑后的产物不能恢复旧实例。编译器未来若改变 continuation 布局，必须升级状态/语义版本或提供明确迁移。

## Debug函数预设

2026-10-27新增五套内置示例，源码与装载入口集中于 `Pixel/Source/Dev/Debug/Formula/FormulaPresets.cpp`。它们使用现有formula/2函数，按需编译并共享不可变产物；没有引入新的语言特例或引擎能力。

F2点击 `Drop wooden stick`，玩家走近拾取后通过快捷格手持。F2的 `Formula presets` 或F4打开编辑器，`Equip preset`／`Apply process`只修改当前木棍，不创建运行实例；没有木棍时禁用。`Load preset source`仅替换草稿，编译不发射。

普通左键经UseHeldItem动作队列使用当前持有物。木棍施放起点在身体朝鼠标一侧的外缘，留出弹体半径与12.5 cm间隙，方向从真实起点指向鼠标；路径被地形挡住时拒绝。运行实例保留发射时的位置、方向和该木棍的程序版本。方块则在面前放置；Shift＋左键同样使用持有物，右键开采，UI、元素笔刷和设备布置优先处理输入。

| 预设 | 展示内容 | 左键施放后的观察 |
|---|---|---|
| Mining volley | 命中开采函数与三次重复 | 速度40格/秒，命中开采半径2，射击完成后间隔20步 |
| Delayed excavation | 命中后的延时与顺序组合 | 等30步开采半径1，再等45步扩至半径4，命中点不随玩家移动 |
| Alternating force | 两轮交替力场 | 在发射点沿瞄准方向施力90步，再反向90步，两轮间隔30步；F1观察 |
| Beacon pulses | 六次静止信标 | 发射点固定，每个存活45步，结束后隔15步再发；Actor附着仍可在three carriers手动示例验证 |
| Wake launcher | `on_wake`包裹命中开采 | 左键先启动等待，600活动步内按`Wake player formulas`发射，超时不发射 |

`Stop player formulas`停止以玩家为来源的运行实例及归属效果，`Clear running`清理所有承载者；两者均保留木棍上的程序。场景初始化／重置不再给人物装载构式；人物继续提供身体和施法来源。木棍拥有独立ProgramArtifact／版本，PixelGameplay 6保存每根木棍，v1–v5的当前人物程序迁移到木棍。方块适配、物品身份、事务和保存兼容的唯一合同见[手持物品](../../.awtcontext/GlobalDoc/HandheldItems.md)。显式Actor组件测试仍支持可选FormulaComponent，生产玩家不装配它；独立语言和Simulation不依赖物品接口。

专项测试覆盖装载零副作用、人物归属、旧实例版本保留、唤醒/停止、重置/读档、左右上下与近距离瞄准、跨块远坐标、左键按下/释放、暂停以及独立模拟线程提交。2026-11-09验证：15项不同CPU专项全部通过（Checkpoint新增断言后复测通过），Pixel和PixelRuntimeSmoke编译成功；原生`--formula-only`通过GPU读回与真实ImGui装载/左键施放/停止/唤醒。沙箱窗口服务不可用，原生复测使用允许访问窗口服务的执行环境；本机无Vulkan validation layer，本轮未跑全量CPU/GPU或AET。

## 边界与成本

原始源码≤16KiB，标识符≤64字节，函数和 let 合计≤64项，每函数/调用≤8参数，表达式≤1024节点/24层。描述构造≤4096次求值、调用深度≤48，展开动作≤1024次访问/20层，生成文本≤64KiB。最终仍受512指令、1024过程表达式、16个计数器局部槽及原有块深度限制约束。超限在编译时拒绝。

运行宿主合计≤16根、128弹体；每活动片≤256 work、8 query、16 outputs，全部根≤4096 work/256 outputs。函数展开只发生在编译阶段；命中上下文存储在启动时预留，单次 Step 不展开定义。内部宿主调用顺序和非法可信装配仍按程序错误抛出，不增加 catch-all 恢复；用户源码、领域拒绝和存档格式边界保留诊断。

在本机 Apple M5 的 Release 构建，固定包装示例三轮500次编译约10.8—13.7微秒/次；包装和展开两种源码各2000次首片段测量，P50为0.208—0.250微秒，P95为0.209—0.250微秒，均17 work/22指令。每轮两种源码的首片段分位数相同。原始输出两行的 compile_us 共用包装示例测量，未比较两种形式的编译时间。运行测量仅计首片段 Step，不含实例创建、游戏世界提交、GPU、并发负载或整场景吞吐；原始输出见 [成本记录](FormulaCompositionCost.txt)。

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B build-formula-system -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula-system -j4
ctest --test-dir build-formula-system --output-on-failure
./build-formula-system/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Composition/mining.formula --steps 12 --hit-at 3

cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

cmake -S Pixel/Source/Runtime/System/Formula -B build-formula-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-formula-release -j4
./build-formula-release/UnitTest/Formula/PixelFormulaCompositionTests --benchmark
```

完整实际结果及限制以验证报告为准；旧文档中的历史测试数量不作为本次完成证据。
