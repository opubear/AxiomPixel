# 游戏动作与会话控制入口

M1.4 的实现入口是 `GameAPI/Actions.h`、`Gameplay/Actions/GameActions.cpp` 和 `GameSession`。公共动作值只依赖标准库，不依赖 World、Formula、ImGui 或 RHI。动作入口同步执行，回执携带`ActionReceipt.ResolvedAt`和因果事实范围；[固定步与提交](SimulationCommits.md)说明提交与呈现副本。M2已接入有界事实环及独立的[LLMView冻结查询](../../.awtcontext/GlobalDoc/LLMView.md)；M5的WorldWill在固定步外等待模型，再于安全边界同步提交，未引入通用动作队列。

## 调用与身份

宿主固定装配 Input、Debug、Driver 三个原生调用者，另为WorldWill分配受限调用身份。`CallerSession` 返回原生调用者当前的会话 token，`Submit(token, request)` 接收带 epoch、递增 sequence 和类型化 payload 的请求。Input可请求开采、放置、激活、设备操作、近距观测和重置；Debug/CPU Driver额外拥有风设置、程序应用和其他会话控制能力。`NativeCaller` 与 `Execute` 是可信本地装配的便利入口，不是外部协议字段。WorldWill只可提交干预动作，模型适配器不持有调用凭据，不能把自报来源转换为Debug身份。

`Execute` 为原生调用分配下一个序号，再进入同一个 Submit。每个调用者保存高水位及最近64条终态回执。拒绝也消耗合法的新序号；重复返回原回执，已淘汰返回 DuplicateExpired，永不重放。调用者和 epoch 在去重前核验。重置、加载与恢复更新 epoch 和全部调用者 token；进程内不同 GameSession 不复用这些身份。计数器耗尽致命失败，不回绕。此机制不跨进程持久化。

示例（可信 CPU driver）：

```cpp
const auto result = session.Execute(pixel::NativeCaller::Driver,
    pixel::api::MineRegion{{520, 512}, uint32_t(pixel::CellTypeId::Dirt)});
```

外部请求必须保留第一次的 token、epoch、sequence 和 payload 才能重试；修改参数使用新序号。`LastAction` 是 UI 读取最近原生回执的短期借用，下一次调用或世界替换后须重新获取。

## 领域动作

- MineRegion 开采一格，提供可选期望材料，返回实际前后材料及 changed/blocked/notReady 计数与 Collected 物品。缺失格先返回 NotReady，再比较材料前置条件；水和空气不可开采。M4.2 在同一提交段将产物收入玩家背包，InventoryFull 保留材料且不增加提交版本。
- PlaceMaterial 只支持 Dirt，检查空气、所有角色和设备占用，不覆盖水，不新增距离或模拟窗口限制。M4.2 消耗一份 soil；缺料返回 InsufficientItems，成功回执包含 Consumed。所有原生来源使用相同资源规则。库存合同与保存边界见[物品库存](../../.awtcontext/GlobalDoc/Items.md)。
- ActivateProgram 核验当前 ProgramRef、连续目标、会话阶段、发射路径和既有额度；返回本次激活身份和当前运行数量。Empty/延迟程序成功时可以没有球。拒绝发射不会改变上次成功瞄准方向。
- InstallIntervention、InterveneAtDevice 仅由宿主世界意志身份提交，核验当前程序、资源版本、区域控制及目标设备版本。成功干预占用4个并发槽之一，结束归还；模型不消耗玩家库存。动作序号在安全提交边界分配，异步调度与离线演示见 [WorldWill](../../.awtcontext/GlobalDoc/WorldWill.md)。
- SelectWindStrategy、SetWindBoundary 需要 Debug 能力。保留原有七策略及高成本后端自动暂停行为；没有通用属性写入接口。

坐标使用 int64 cell 和 [0,1) double offset，转换先保留整数格再处理局部小数，不经过绝对 double。边界拒绝非有限值、非法offset、不受支持的材料/枚举及超出当前 WorldPosition 能力的坐标；极端chunk预留给内部邻域和发射偏移。动作入口没有通用JSON写接口；外围模型协议只解析受限提案，由WorldWill构造获准动作。

所有新世界动作都在 Ready 且没有预算延期步时执行；验证顺序为调用者/epoch、去重、payload合法性、可写阶段、能力、局部前置条件和实际领域操作。材料编辑由World执行，角色/设备占用与库存成本由Gameplay组合；玩家与构式开采共用Mining规则。构式效果经FormulaWorldAdapter接入领域，同批先碰撞查询再提交的顺序保持。

## 会话与界面

`Control` 提供暂停、单步、保存、重置、流式重试、恢复、下一世界生成器/seed、育圃场景及ClearRunning，返回 Applied/Saved/Replaced/Deferred/Failed/Rejected 和原因。Save/Reset在PendingStep期间返回Deferred，不写盘或补步；当前checkpoint/1保存完整多领域状态并兼容导入旧v2–v5档，见[保存边界](SessionSaveBoundary.md)。Control不属于离散动作去重协议，调用者显式重复控制会重新执行；世界意志身份不能调用此端口。

`ApplyProgram` 是依赖Formula的游戏适配入口，返回新ProgramRef或分析诊断，不依赖编辑草稿。Apply/Clear在预算等待期间拒绝，保留当前运行状态；Clear不触发后代，也不退还本帧已退休风额度。现存实例及延迟/命中后代保留原程序版本。

GameInput只负责事件筛选、按事件尺寸选格和请求转换，持续玩家意图仍按固定步采样，清理已施加输入经Session。Debug的Game actions折叠区可输入Cell X/Y后开采、放置或向格中心激活，显示Input和Debug的最近回执；风、保存、暂停、单步、生成设置和Formula工具栏均使用对应端口。相机、草稿、面板、外观继续由各自模块管理。

M1.5已移除Session与PixelGame的可变World访问器、Session的可变Runtime访问器及旧公开控制/激活方法。World的GetMutableMap仅供内部Environment使用；测试与离线基准的特殊夹具由UnitTest/Support显式构建，生产目标不包含这些访问实现。GamePresentation作为内部友元仅维护快照发布的驻留请求与预算生命周期；Input/Debug通过公开只读引用观察，通过动作/控制端口修改。

## 失败语义

FormulaBridge的Apply先分析外部程序，参数/结构/工作量不符合宿主规则时返回诊断，保留原版本。预算等待、激活/根/效果/风额度不足仍是可预期拒绝。分析后执行、碰撞解析、效果提交、快照生成中发生的内部异常直接传播；不再标记Failed、清掉该根后继续游戏。已经提交的早先效果不回滚，发生内部错误后宿主不能继续使用该会话。PendingStep期间非法调用FormulaBridge::Step、碰撞引用已退休弹体及身份耗尽使用发布构建仍生效的检查，不能依赖assert。

保存、恢复与后台块I/O只处理文件系统错误、流I/O错误和明确的WorldStorageError（损坏存档或加载必需存储不足）。磁盘数据的非法配置在读取/恢复边界转换为存储诊断。生成器不变量、分配失败和运行器错误不再被宽泛std::exception捕获转换成StorageFailure；Reset中的清理与恢复保护仍会重新抛出原故障。应用生命周期通过Session::BeginFrame重置本帧额度；PixelApplication在关闭请求阶段保存并裁决是否退出，GameEnd关闭WorldWill/模型并清理输入，不再次保存。

## 验证入口

`Pixel.InternalFailure.*`通过独立进程核验内部无效坐标、移后程序产物、非法PendingStep推进和损坏会话状态会传播至terminate；同时检查特定诊断与终止码，Debug/Release均执行。编译断言保证Session公开World/Runtime为const引用、WorldMap写入口非公开。

`Pixel.Actions`覆盖三入口开采结果、玩家/水放置阻挡、缺失格优先级、材料前置条件、重复和淘汰、调用者/epoch替换、权限、非法坐标、预算等待、暂停激活、单步及旧程序隔离。既有Input、Session、FormulaGameplay、FormulaRendering与存储回归继续验证完整行为。

`PixelRuntimeSmoke --debug-ui-only`使用真实生产按钮验证Debug开采和放置，并保留七风策略、int64边界、UI捕获和窗口重建；`--formula-only`验证真实Apply/Clear、版本隔离、输入捕获及GPU合成。执行结果以当前AWT任务日志为准，本文不替代测试报告。
