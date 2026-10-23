# 持久援助、祈祷档案与自主权（M5.4）

力场育圃现在可以通过离线 WorldWill 请求一次实际援助，再用玩家设备的保护模块撤销它。知识、重要历史、控制权、干预 continuation 和有来源的祈祷结果写入同一检查点。实现仍属于 Pixel；World、独立 Formula 和 Axiom 没有增加游戏策略依赖，没有模型网络或供应商账号。

## 试玩

F2 展开 `Force garden`，保存并创建育圃，开采建造材料并建造 `Home emitter`。首轮试用可保持设备停用且不供煤，便于分辨援助与自建设备的效果。

1. 点击 `Pray for home aid`。原生试玩入口应用固定的 `home/world-will-aid/1` 程序，发起以设备为目标的授权观测和异步请求；该按钮会改变当前玩家所选 process，新实例使用新选择，旧实例保持原程序。M5.5接口工作增加玩家原文输入和取消，当前默认适配器标为PseudoLLM，会随机援助或不行动，进度见[模型交互](ModelServiceIntegration.md)。
2. 若结果是NoAction，可重新祈祷；若出现Ready，在5秒期限内恢复模拟。离线提案仍需通过设备版本、程序、资源槽和当前权限检查。成功后在设备中心安装半径16、向右3600格/秒²、600步寿命的场，家园花床按真实采样成长。没有扣煤、补物料或自动开放权限。
3. 面板分别展示 `Model explanation`、实际动作结果、实例、事实范围和稳定历史证据序号，以及当前活跃/冻结的干预和场源。Applied 表示当时创建成功，不表示该实例现在仍存在。没有在途自动祈祷或无限重试；默认5秒墙钟期限在暂停时也计算。
4. 安装 `Home protection` 后启用设备。保护只在设备启用时生效，不耗煤；同一提交撤销重叠援助和后续效果，场源消失、槽归还，既有花床成长不回退。天然晶体和玩家设备仍按原规则运行。移除保护不会复活旧援助。
5. Save world 后退出重启，已有援助按剩余寿命/程序执行位置续接；已撤销的效果保持停止，保护、非默认能力、发现和关系记忆也保留。

“关系”首版只记录真实接受援助次数与玩家是否选择安装保护。它没有好感值、自动惩罚、工厂规模规则，也不授予模型额外能力。两个值及最近32条祈祷都由 Session 的世界状态持有；WorldWill 控制器只拥有当前运行的观测、推理和提交调度。

## 请求与事实的分界

`WorldWillHost::BeginPrayer/ResolvePrayer` 是主线程宿主端口，后台模型只拿已存在的自有观测 DTO。请求得到接纳后建立稳定的 `(WorldId, PrayerId)`；一次终态只能解决一次，记录最多1024字节的模型说明。模型说明是非权威文本，不会写入 DomainFact，也不能改变权限。

Applied 结果引用真实 Action Request、FactRange、InstanceId 以及 `Knowledge` 中 `ProgramActivated` 的稳定 History.Sequence。重要档案仍保留最近128条，证据淘汰后承认缺口，不重新创造历史。普通拒绝不会伪造实例或事实。保护动作的回执事实范围包含实际 `AuthorityChanged` 和必要的 `InterventionStopped`；撤销理由与程序身份保留在重要历史。

请求及终态更新增加 CommitRevision。模型回包已到而模拟处于 PendingStep 时，控制器保留它，待完整边界再解决，不改准备中的领域状态。PendingStep 或未就绪期间达到墙钟期限仍立即取消 worker，终态档案等完整边界写入，晚到回复不能继续提交。正常暂停可以接纳祈祷，但不提交模型动作。成功提交到写入祈祷终态之间没有线程切换或外部保存回调。

帧首原生 `GameplayObservation` 包含上述有界值，育圃 UI 使用同一副本，Draw 不读取实时 World。LLMView 的区域投影不带这份全局祈祷/保护数据；现有授权的 Knowledge/History 查询维持原合同，没有悄悄扩大模型观察权限。

## 一致检查点与恢复

`PixelGameplay 3` 继续嵌在原有不可变 `checkpoint/1` 世代中，并读取版本1/2（缺失的新领域使用初始状态）。M5.4增加的独立值格式是 `PixelKnowledge 1` 和 `PixelWorldWill 1`；M5.5接口工作将后者扩展至版本2并继续读取版本1，保存原文、Ready与Cancelled。原有地图/程序格式、原子指针发布、旧v2–v5导入和写失败保留上一代规则不变。

- Authority 保存能力、授权版本、保护的设备引用/矩形/启用状态、4槽资源账本、干预身份高水位及绝对成功tick到期点；不保存可继续使用的运行调用者凭据。
- 活跃与冻结干预继续通过 FormulaBridge 保存原执行位置、局部变量、剩余等待、已消耗credits、事件/弹体和归属场。恢复不会重新Cast，不重扣、不重发，也不把租约重新设为600步。
- 租约按成功全局tick计数，冻结区域也会到期；场源与程序的活动时间保持冻结。加载后先重建身份和控制，再核验跨域引用，之后才同步活动状态和发布。
- 保护必须对应仍存在的设备和精确派生矩形；每个干预必须恰好对应一个实例，scope、credits、能力和场源边界一致，槽数也必须匹配。无租约的干预、错位保护、过期租约、将来历史或错误祈祷证据明确作为坏存档拒绝。
- Knowledge 保留稳定WorldId、历史高水位、所有来源及发现；恢复验证世界、时间和拥有者，再建立新运行的来源消费边界。历史来源的旧epoch是证据，不是新调用凭据。
- 已完成祈祷保持原记录；保存时仍在Thinking/Ready的请求加载成Interrupted，并标明新的恢复截面。worker、未交付回包、墙钟期限、观察租约和动作回执缓存不恢复；新请求使用下一个持久祈祷ID。旧运行回包没有新动作权限。

仍拒绝 PendingStep 保存和活跃旧树实例保存。此次没有引入跨进程网络 exactly-once、全量事件溯源、动态风精确存档、独立模型摘要存档或后台远处模拟。

## 验证入口

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4
ctest --test-dir build-cpu -R '^Pixel\.(InterventionCheckpoint|KnowledgeCodec|WorldWillCodec)$' --output-on-failure
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --garden-aid-only)
```

`Pixel.InterventionCheckpoint` 使用真实临时目录与fake worker，覆盖开采建造、发现、异步/重复结果、援助成长、重新构造Session、继续剩余等待/寿命、保护撤销、非默认能力、Thinking转Interrupted、稳定来源及跨领域坏档拒绝。冻结租约用显式时钟夹具跳到最后一tick，再经生产步路径验证到期，不冒充跑完全部600次环境求解。

`--garden-aid-only` 复用世界GPU读回，检查援助驱动幼苗成熟及多个帧槽/F1，再通过真实ImGui按钮和平台输入检验请求、暂停、保护拒绝/撤销和同帧发布。

2026-10-24，macOS，独立验证 agent 完成默认 CPU 全量 **91/91**（45.91秒）、chunk64 Release 回归 **18/18**（3.12秒），并成功构建 `Pixel` 和 `PixelRuntimeSmoke`。之后补充的冻结租约测试单独通过。

收尾修正让 PendingStep/未就绪期间到期及时取消 worker。增量构建后，默认 CPU 与 chunk64 的 `WorldWill`、`InterventionCheckpoint`、`WorldWillCodec` 各 **3/3** 通过，运行程序及 smoke 重新构建成功；独立增量 review 通过。新增期限用例直接验证控制器终态延后和迟到回包不提交；持久档案写入边界另经代码审查确认。

独立 UI agent 完成真实 MoltenVK/Apple M5 上的 `--garden-aid-only`、`--garden-only`、`--debug-ui-only`，均 exit 0；覆盖新援助场景、原育圃路径及调试输入/设备/窗口回归。本机没有 `VK_LAYER_KHRONOS_validation`，此次未启用该层；没有运行 AET、sanitizer 或网络模型验收。

独立 review agent 审查权限、去重、租约/冻结、恢复、撤销、知识证据和发布边界，未发现阻塞问题；review 未代替实际测试。实现不修改嵌入引擎或相邻参考 Axiom。任务完成后按用户要求暂停，未启动 M5.5。
