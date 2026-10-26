# 固定步协调与完整提交

M2.1 的实现入口为 `Gameplay/SimulationCoordinator.h`、`Gameplay/GameSession/GameSession.cpp`、`Gameplay/GameSession/SessionPublication.cpp` 和 `Gameplay/CommittedWorldView.*`。它接替 Environment 的累加器和 World 的跨领域回调链。World 继续仅依赖 Core/Performance；60 Hz、单步和会话提交策略属于 Pixel Gameplay；全局暂停状态由GameSession持有，引擎没有新增游戏调度规则。

## 全局暂停

GameSession持有唯一的`Paused`状态，普通新世界和R重置后默认运行；SimulationCoordinator只管理固定步时钟、单步请求及完整提交，不再保存独立暂停。`SetPaused`控制整个会话，F2顶部的`Pause game`提供入口。

暂停且没有显式单步请求时，Tick不推进流式服务、环境、角色、构式、设备或育圃，也不累计时间。输入丢弃暂停期间的跳跃；标准相机跟随停止，DebugCamera继续响应。世界修改、Apply和Clear返回`GamePaused`；恢复暂停、保存、重置及I/O重试属于宿主管理操作。`StepSimulation`仍表示一个包含全部领域的完整单步，暂停状态在单步后保留。

WorldWill暂停轮询和发布模型结果，并延后在途请求的截止时间；后台模型计算可结束，但不会在冻结期间变成世界动作或历史。取消只停止后台请求，领域终态在恢复后的安全边界发布。呈现继续读取已提交只读版本，存储服务可完成必要的驻留与文件工作，但不会推进设备和构式活动。

新世界元数据v6保存全局暂停；v2–v5中的暂停是已移除的环境开关，导入时转换为运行状态，保存余量和已提交时间保持原值。导入本身不执行欠步。

## 推进与等待

Session先在安全边界处理流式驻留，再由协调器累计最多0.25秒的待处理时间。每一步依次执行：Environment风预推进、原有同步材料提案/裁决/提交、风反馈、Actors角色一步、FormulaBridge碰撞与效果、设备和育圃逻辑。随后增加Tick和Revision、记录构式事实、结算权限/租约撤销与设备事实，最后封存并发布完整世界缓冲版本。独立World只提供`StepEnvironment(committedSeconds)`；Session随后调用`Actors::Step(terrain, input)`。纯世界测试与基准使用`UnitTest/Support/SimulatedWorld.h`的宿主夹具，角色组合使用`SimulatedActorWorld.h`。

只有完整成功的一步消费余量。单步请求消费已有余量中的一步；不足一步才补足，不清空剩余时间。不同帧时长分段仍使用同一固定步。Skip 沿引擎原帧协议不调用游戏推进。

材料 scratch/载荷分离或版本元数据预算不足时，Environment 保留本步已推进的风，协调器保留步首输入、时刻和全部余量。重试不再次推进风，不移动模拟/风域，也不接纳完成的地图加载；后台保存仍可完成并释放容量。等待期间世界动作、Apply/Clear 和会改变暂停状态的请求返回 WaitingForBudget。单步请求可用于显式重试；输入变化和清理只影响下一步，不能改写正在重试的步首输入。预算足够后仍按原顺序完成角色和构式，再一次性提交。

## 版本与回执

`GameAPI/CommitStamp.h` 提供 `{WorldId, WorldEpoch, Tick, Revision}`。初始完整状态 Revision=1，有限夹具 WorldId 为 `finite-world-<epoch>`；持久会话使用存档目录名。不同会话化身由宿主epoch区分，新建世界后Revision从1开始，恢复检查点时保留已保存的Tick/Revision。

成功物理步同时增加 Tick 和 Revision；帧间开采、放置、激活、重建风、应用程序、清理现存运行实例、改变全局暂停设置，以及接纳驻留/风域变化只增加 Revision。全局暂停时拒绝上述世界修改动作，宿主管理与显式单步除外。下一世界的生成配置、单步请求、存储重试和运行诊断属于服务控制，不代表当前世界的物理提交。查询、拒绝、重复请求和无变化的暂停设置不增加版本。

`ActionReceipt.ResolvedAt` 记录动作解决时的完整提交点。同步去重缓存保留原回执及其版本，即使世界后来继续运行，也不会用新 stamp 覆盖旧请求的结果。身份或版本耗尽、错误阶段提交仍直接抛出内部错误。

## 只发布已提交的数据

`WorldBuffers` 统一拥有材料/方向/基础力、风后端和场源。只有 World/Environment 的当前模拟槽能写。`PublishBuffers()` 在完整提交时将当前槽封存；下一次写入切换到另一个槽。渲染和观测通过 `shared_ptr<const WorldBuffer>` 共用最后完整版本，不再建立每个消费者自己的材料、Air、Field 全图/逐格快照。

静态块载荷共享；首次修改前分离。正常两槽循环会复用旧载荷，按64格脏区域同步材料、方向和基础力。替换/重载区块使用独立世代，避免相同版本号误判为同一份内容。风后端共享至首次写入后克隆，内部数值算法仍保持原有缓冲交换。慢消费者持有的槽不会被覆盖，必要时保留额外版本；释放后可回收。保留版本按实际共享载荷去重计量；材料/基础力和版本元数据纳入流式预算。数值风求解器沿用原有单独计量合同（含被读者保留的后端），不由世界流式预算封顶。材料批次和跨域库存动作都在副作用之前预留写入空间，失败保留原材料与库存。

`CreateReadView/RefreshReadView` 仍提供不超过2048×2048格的范围和角色/弹体/Gameplay元数据。它持有已提交世界的只读租约，场值在消费者实际需要时采样。`ReadState()` 是借用视图，有效至该呈现视图下次刷新；异步消费者应保留 `ReadBuffers()` 返回的租约。待提交步不会刷新读者；新消费者没有已完成视图时使用 Missing 区域。`GamePresentation` 仅在帧首转换所需材料/F1/风纹理，Draw 消费独立的渲染格式缓冲。

ObservationProjector 在提交时只替换只读世界租约和小型领域元数据；收到 Capture 时才按区域版本生成必要的 API DTO。空闲观察者不再逐帧扫描材料与风。LLMView 的冻结分页导出仍拥有序列化结果，以保持已交付查询的冻结与过期合同。可信调试的预算、I/O等实时诊断仍使用 World 服务接口。

当前双缓冲在主线程交接；不可把同一个可变 CommittedWorldView 跨线程共享。未来 GPU 后端应在命令提交后保留只读租约，直到 fence 完成再释放，并通过同一个发布边界交接完整版本。当前实现没有新增 GPU 求解或跨线程发布调度。

## 存储与验证边界

时钟由协调器导出/恢复，旧档成功tick从保存的已提交秒数转换；checkpoint/1恢复精确Tick/Revision并核对元数据时钟。M2.3将旧v2–v4 deferred转换为完整欠步；Save/Reset在Pending时返回Deferred，见[保存边界](SessionSaveBoundary.md)。正常退出通过[关闭请求](CloseRequests.md)协商。M4已持久化process/1运行实例及归属效果；旧树运行时已移除，默认程序和全部活动process实例支持保存；动态风扰动仍按配置重建，详见[检查点](../../.awtcontext/GlobalDoc/Checkpoints.md)。

正常保存可以等待并接纳已完成的加载，因此无论随后块写入成功还是存储失败，返回前都同步已接纳状态的服务 Revision。延期保存仍不接纳加载，不发布准备中的风。

`Pixel.Simulation` 验证提交版本、旧回执去重、帧间激活、全局冻结及完整构式提交、冻结输入、预算失败/恢复、呈现半步隔离和无样本回退；`Pixel.Session` 验证帧分段及单步余量，`Pixel.Persistence` 覆盖 v6、旧 v2–v5 导入及 v2–v4 欠步导入与 Deferred/流式回归，并覆盖成功与失败保存的服务版本同步。World、FormulaGameplay、Input 和呈现 CPU 回归继续执行。实际构建、CTest、GPU smoke 与独立审查结果记录于本任务 AWT 日志。

M2.4已在相同完整提交边界接入独立ObservationProjector与DomainFact环，见[观测实现](../../.awtcontext/GlobalDoc/ObservationProjection.md)。呈现视图仍可请求可视区域加载；观测直接读取共同的已提交世界版本，不触发加载。
