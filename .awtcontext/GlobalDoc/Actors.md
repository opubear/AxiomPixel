# Actors 身份、生命周期与地形边界

M4.1 将玩家和第二类最小角色 Resident 放在 Session 拥有的 `Actors` 中。`World` 只拥有材料、环境与流式存储，不再知道角色类型或玩家输入。此模块先解决角色状态归属和引用生命周期，没有建立通用 ECS。

## 状态与身份

`ActorRecord` 保存 `ActorId`、`ActorKind` 和 `Body` 运动状态。`Body` 负责位置、速度、碰撞、移动和水体响应，`BodyState` 是可捕获与恢复的身体状态值；身份与生命周期由 `Actors` 管理。Player 接收宿主输入；Resident 使用同样的重力、地形碰撞和水体响应，不接收玩家输入。Resident 是用于后续居民/对象发展的最小物理对象，目前没有 AI、生成玩法或单独的渲染入口。

`TrainingDummy` 是固定的构式测试目标。新世界初始化在玩家出生点右侧12格生成一个，包含Natural、Flat和育圃；位置位于生成器的干燥平坦出生区域。它跳过Body推进，不受重力、水体、输入或固体解嵌影响，也不执行攻击。当前没有生命值和伤害结算，弹体命中只产生构式Hit事件，不删除或改写目标。持久会话沿用Actors检查点保存类型、身份和位置；载入旧档不补生，重置新世界才重新创建。

`ActorId` 是世界内的值身份，从 1 单调分配。删除、复用 slot 或清空当前集合不会倒退 ID 分配器；0 表示无效。跨域引用使用 ID，并携带所属世界/会话上下文。`ActorHandle` 是本进程的 `{Slot, Generation, Owner}`：删除提升代次，其他集合/重建集合的 Owner 不同，旧句柄不会命中新对象。ID 与 slot 没有数值对应关系。

`Spawn` 创建新身份；`Remove` 删除对象；`Resolve(id)` 重新获得当前内存句柄。`Find` 对失效引用返回空；`Require` 用于内部保证有效的句柄，失效直接抛出逻辑错误。`Restore(record)` 恢复同一身份及完整运动状态，分配新的内存句柄，并提升后续新 ID 的起点。恢复重复的活 ID、重复玩家、未知类型或非法位置属于程序错误。容量固定最多 256 个对象，容量不足返回空且不修改集合。

`Find` / `Require` / `Player` 返回的引用只用于当前同步调用，不可跨集合变更保存；需要留存的调用方复制 `ActorRecord` 或 `ActorSnapshot`。`ActorSnapshot` 只有身份、类型、分段位置和着地状态，不暴露 slot、代次或运动内部状态。

## 推进与占用

`SimulationCoordinator` 是唯一输入与时钟所有者，仍按 Environment → Actors → FormulaBridge → commit 推进。`Actors::Step` 接收只读 `WorldMapView` 和本次成功固定步的输入，内部按 1/60 秒推进；Player/Resident 都不能写材料格。角色不相互碰撞；建造占用检查独立于角色物理。

环境等待预算时，Session 不调用 Actors；重试使用步首冻结输入。清理或更新输入仅影响下一步。角色身体未完整处于当前模拟窗口内，或身体覆盖格尚未驻留时不推进，避免把窗口/缺块边界当实体重叠而向上解嵌；推进中的缺失目标地形仍视为障碍。此处没有实现对象卸载或离线补算，区域存储/冻结恢复属于 M4.5。

Session 以玩家位置调用 `World::SetSimulationFocus`，World 只按明确的活动中心管理流式窗口。成功角色步后更新 focus；更新位置本身不会移动模拟域。安全边界的 `UpdateStreaming` / 可见区请求执行原有居中策略，pending 环境步仍禁止域重映射或接纳加载。

`GameSession` 的 PlaceMaterial 先检查所有 Actors 的矩形占用，再写材料。World 的材料写入口只判断材料/驻留约束；它不会反向依赖 Actors。程序化材料变化进入角色身体时，Body 的向上解嵌行为保持。

## 观测、呈现与保存边界

Session 的 `GetPlayer` 是同步只读借用；渲染使用 `CommittedWorldView` 中复制的 `ActorSnapshot`。Actors 修订与材料/风修订一起参与离散提交刷新，pending 期间只保留最后完整快照。相机接收明确的 bounds 和角色读引用，不再从 World 查找玩家；呈现只消费角色 DTO。

`Actors::Snapshot`复制有界角色集合；`CommittedWorldView::GetActors`在同一提交中发布它并计入内存预算。Render的`ActorFrame`将木头人转换成五个木色矩形，复用场景物件Pass，在弹体与玩家之前绘制，F1不显示。玩家保留原CharacterPass，Resident暂不绘制。

FormulaBridge在每个准备边界捕获角色值快照，弹体出生与移动均比较圆形扫掠对地形和木头人身体的最早接触；支持高速穿越、圆角和起点重叠，地形或未就绪边界在同一时刻优先。只对TrainingDummy启用角色命中，Player/Resident不引入友伤；World不依赖Actors。`on_hit`使用弹体的实际接触位置，后续开采等动作仍通过原有游戏能力执行。

PlayerObservation 新增世界内 `ActorId`，LLMView JSON 的 `actorId` 使用十进制字符串，避免超过 2^53 时丢精度。观测仍按玩家矩形是否与 scope 相交决定返回；当前没有将整个 Actors 集合暴露为新查询 topic。

M4.6通过 `ActorsState` 保存所有角色的持久ID、next-ID、分段位置、垂直速度和着地状态。恢复重建Owner与slot，不恢复内存句柄；会话epoch和调用者身份更新。磁盘损坏在载入边界报告，具体见[一致检查点](Checkpoints.md)。

## 验证入口

`Pixel.TrainingDummy`覆盖默认生成与重置、Natural/Flat/育圃出生、提交快照隔离和外观、输入/水/火/固体下静止、扫掠/圆角/遮挡，以及连续百万伤害弹体和出生重叠触发真实`on_hit`开采。`Pixel.Checkpoint`覆盖包含木头人的完整磁盘保存恢复；`PixelRuntimeSmoke --dummy-only`读回木头人五个部件并验证帧切换与F1显隐。

2026-10-27木头人接入验证：默认CPU全量99/99、64格Release专项（TrainingDummy、GenerationSession、Actors、Checkpoint）4/4通过；Pixel、AET和PixelRuntimeSmoke编译成功。Apple M5真实窗口木头人专项和完整PixelRuntimeSmoke均通过，包含构式/预设/育圃UI及两轮resize/minimize生命周期。已检查导出画面；Vulkan validation layer不可用，本轮未运行AET窗口测试。引擎无改动。

新增 `Pixel.Actors` 覆盖删除复用、跨集合句柄、Clear、运行态恢复/运动延续、大 ID 和容量拒绝；同时验证 Resident 落地/水体/输入隔离、窗口外/部分身体缺失冻结与恢复、材料不被角色改写、±2^55 格以上的亚格运动/占用/相机/呈现，以及 Session 占用与观测身份。

`Pixel.Body`、`Pixel.Liquids`、`Pixel.Input`、`Pixel.Session`、`Pixel.Persistence`、`Pixel.Camera`、`Pixel.Simulation`、`Pixel.Observation`、`Pixel.LLMView` 与呈现回归覆盖行为迁移。纯 World 测试使用无角色的 `SimulatedWorld`；需要角色组合的独立测试使用 `SimulatedActorWorld`。测试夹具不进入游戏公开 API。

2026-10-25，身体类型及文件由 `Character` 改为 `Body`，`CharacterState` 改为 `BodyState`；测试目标由 `PixelCharacterTests` / `Pixel.Character` 改为 `PixelBodyTests` / `Pixel.Body`。运动规则与存档格式保持不变，渲染层 `CharacterDraw` 继续表示角色外观。下列历史验证记录保留当时的名称。

2026-10-22，本机 Apple M5 / macOS，独立验证 Subagent 完成最终 Debug 全量构建及 CTest **71/71**（78.64 秒）；Release 最终相关 CTest **12/12**（12.44 秒），ASan/UBSan 最终相关 CTest **11/11**（29.22 秒），无诊断。Sanitizer 的 `Pixel.Cpu` 在本轮稍早通过（195.35 秒），最后的边缘冻结/身份测试改动未涉及该目标。运行时 `cmake --build build --target Pixel PixelRuntimeSmoke -j 6` 成功；`(cd Pixel && ../build/tests/PixelRuntimeSmoke)` 完整运行退出 0，包含角色/材质 GPU 读回、点击/移动、HiDPI 与模式切换、F1、构式及 ImGui、两轮 resize/minimize 生命周期。Vulkan validation layer 在本机不可用；没有把该运行称为 validation-layer 检查。没有引擎代码变动，本任务未重跑 AET。

独立 Review 发现并修复一项 P2：身体跨活动窗口底边时，被 Character 的固体解嵌向上推移。现在先检查完整身体的视图覆盖与地形驻留。审查者以独立临时程序验证底边冻结、缺块补齐恢复和冻结前后速度连续性，并复核无未决意见；对应窗口/缺块回归已加入 `Pixel.Actors`。


独立验证使用三个 CPU 构建目录：`/private/tmp/pixel-bridge-verify`（Debug）、`/private/tmp/pixel-bridge-release`（Release）、`/private/tmp/pixel-bridge-sanitize`（`-fsanitize=address,undefined -fno-omit-frame-pointer`）。Debug 完整构建后执行全量 CTest；Release 目标和 CTest 选择 Actors、Character、Liquids、Input、Session、Persistence、Camera、Simulation、Observation、LLMView、LLMJson、Cpu；最终 Sanitizer 选择相同集合但不重复未受最后修改影响的 Cpu。`git diff --check` 和 AWT `check` 均通过。
