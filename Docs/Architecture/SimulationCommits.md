# 固定步协调与完整提交

M2.1 的实现入口为 `Gameplay/SimulationCoordinator.h`、`Gameplay/GameSession/GameSession.cpp`、`Gameplay/GameSession/SessionPublication.cpp` 和 `Gameplay/CommittedWorldView.*`。它接替 Environment 的累加器和 World 的跨领域回调链。World 继续仅依赖 Core/Performance；60 Hz、暂停、单步和会话提交策略属于 Pixel Gameplay，引擎没有新增游戏调度规则。

## 推进与等待

Session先在安全边界处理流式驻留，再由协调器累计最多0.25秒的待处理时间。每一步依次执行：Environment风预推进、原有同步材料提案/裁决/提交、风反馈、Actors角色一步、FormulaBridge碰撞与效果、设备和育圃逻辑。随后增加Tick和Revision、记录构式事实、结算权限/租约撤销与设备事实，最后发布完整读副本。独立World只提供`StepEnvironment(committedSeconds)`；Session随后调用`Actors::Step(terrain, input)`。纯世界测试与基准使用`UnitTest/Support/SimulatedWorld.h`的宿主夹具，角色组合使用`SimulatedActorWorld.h`。

只有完整成功的一步消费余量。单步请求消费已有余量中的一步；不足一步才补足，不清空剩余时间。不同帧时长分段仍使用同一固定步。Skip 沿引擎原帧协议不调用游戏推进。

材料 scratch 预算不足时，Environment 保留本步已推进的风，协调器保留步首输入、时刻和全部余量。重试不再次推进风，不移动模拟/风域，也不接纳完成的地图加载；后台保存仍可完成并释放容量。等待期间世界动作、Apply/Clear 和会改变暂停状态的请求返回 WaitingForBudget。单步请求可用于显式重试；输入变化和清理只影响下一步，不能改写正在重试的步首输入。预算足够后仍按原顺序完成角色和构式，再一次性提交。

## 版本与回执

`GameAPI/CommitStamp.h` 提供 `{WorldId, WorldEpoch, Tick, Revision}`。初始完整状态 Revision=1，有限夹具 WorldId 为 `finite-world-<epoch>`；持久会话使用存档目录名。不同会话化身由宿主epoch区分，新建世界后Revision从1开始，恢复检查点时保留已保存的Tick/Revision。

成功物理步同时增加 Tick 和 Revision；暂停开采、放置、激活、重建风、应用程序、清理现存运行实例、改变暂停设置，以及接纳驻留/风域变化只增加 Revision。下一世界的生成配置、单步请求、存储重试和运行诊断属于服务控制，不代表当前世界的物理提交。查询、拒绝、重复请求和无变化的暂停设置不增加版本。

`ActionReceipt.ResolvedAt` 记录动作解决时的完整提交点。同步去重缓存保留原回执及其版本，即使世界后来继续运行，也不会用新 stamp 覆盖旧请求的结果。身份或版本耗尽、错误阶段提交仍直接抛出内部错误。

## 只发布已提交的数据

`CreateReadView/RefreshReadView` 是主线程内部消费者使用的有界实时读投影。每个消费者明确请求不超过2048×2048格的范围，副本拥有材料、格中心空气速度、合成场速度、角色、弹体和提交版本；不会持有求解器或可变地图的借用。稳定区域使用材料/风版本跳过重新采样。读副本及扩容峰值计入 World publication 预算，容量不足保留已有完整范围。

所有已注册副本在成功步或离散提交后刷新。等待期间继续读取最后完整副本；新消费者如果没有副本，返回无可用样本，不能现场采样准备中的风。呈现回退使用相机附近的 Missing 区域并更新上传版本，支持远坐标与帧复用。相机移动可以继续显示旧范围，范围外显示缺失；恢复后再接纳新范围。`GamePresentation` 仅从此副本转换材料、F1、风效、角色和弹体，仍在帧首发布，Draw 仍只消费独立 frame 数据。

这个实时投影在下一次提交时更新，引用仅供主线程使用。M2.4/M2.5已另行实现[领域投影](../../.awtcontext/GlobalDoc/ObservationProjection.md)与[LLMView](../../.awtcontext/GlobalDoc/LLMView.md)的冻结查询租约、分页、事实流、观察权限和过期策略，二者不共用呈现缓冲。可信调试面板的求解器迭代数、预算与I/O状态仍属于实时服务诊断，不能当作冻结领域查询。

## 存储与验证边界

时钟由协调器导出/恢复，旧档成功tick从保存的已提交秒数转换；checkpoint/1恢复精确Tick/Revision并核对元数据时钟。M2.3将旧v2–v4 deferred转换为完整欠步；Save/Reset在Pending时返回Deferred，见[保存边界](SessionSaveBoundary.md)。正常退出通过[关闭请求](CloseRequests.md)协商。M4已持久化process/1运行实例及归属效果；活跃旧树实例仍拒绝保存，动态风扰动仍按配置重建，详见[检查点](../../.awtcontext/GlobalDoc/Checkpoints.md)。

正常保存可以等待并接纳已完成的加载，因此无论随后块写入成功还是存储失败，返回前都同步已接纳状态的服务 Revision。延期保存仍不接纳加载，不发布准备中的风。

`Pixel.Simulation` 验证提交版本、旧回执去重、暂停激活、完整构式提交、冻结输入、预算失败/恢复、呈现半步隔离和无样本回退；`Pixel.Session` 验证帧分段及单步余量，`Pixel.Persistence` 覆盖 v5、旧 v2–v4 欠步导入与 Deferred/流式回归，并覆盖成功与失败保存的服务版本同步。World、FormulaGameplay、Input 和呈现 CPU 回归继续执行。实际构建、CTest、GPU smoke 与独立审查结果记录于本任务 AWT 日志。

M2.4已在相同完整提交边界接入独立ObservationProjector与DomainFact环，见[观测实现](../../.awtcontext/GlobalDoc/ObservationProjection.md)。呈现视图仍可请求可视区域加载；观测不会借用其缓冲或加载行为。
