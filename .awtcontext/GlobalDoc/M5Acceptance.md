# M5 可追溯世界意志阶段验收

2026-10-24。基线 `74f78b0` 已提交 M4 验收修改。本次核对 M5.1–M5.6 在当前代码中的整合合同与运行证据，六项子任务均已完成，需求和各自验收已通过。本轮没有生产代码或测试改动。

真实模型服务继续按用户此前约定暂不接入。当前交付为独立 LLM 接口、随机 PseudoLLM 默认实现、受限模型协议和可控 FakeModel 测试适配器；阶段通过不代表网络服务、真实模型质量或断网行为经过实测。

## 六项能力与所有权

| 子任务 | 当前组合责任 | 主要验证入口 |
| --- | --- | --- |
| M5.1 发现知识与重要历史 | Session 从真实 DomainFact 写入 Knowledge；LLMView 分别授权知识与历史，派生摘要保留来源且不能回写事实 | Knowledge、KnowledgeView、KnowledgeCodec、LLMView、LLMJson |
| M5.2 区域控制与撤销 | Authority 拥有能力、保护和资源账本；GameAPI 裁决动作，FormulaBridge 清理受撤销实例及其后代 | Authority、AuthorityView |
| M5.3 异步决策 | WorldWill 取得自有授权观测，固定步之外轮询模型；主线程按当前条件提交提案 | WorldWill、WorldWillWorker、离线 Demo |
| M5.4 持久援助 | 同一检查点保存干预 continuation、租约、保护、知识和祈祷证据；育圃实际成长并可撤销援助 | InterventionCheckpoint、WorldWillCodec、garden-aid GPU/UI |
| M5.5 接口与祈祷交互 | 组合根注入模型；原文、说明和事实分别保存；Ready、取消和受限 JSON 协议 | ModelProtocol、WorldWill、InterventionCheckpoint、祈祷 UI |
| M5.6 LLM 与随机兜底 | PseudoLLM 随机选择普通函数组合，通过共用 DecisionAPI 构造提案；受控 worker 留在测试目标 | PseudoLLM、默认模型 UI 断言、显式 FakeModel 注入 |

`PixelGame` 持有 Session、LLM 和 WorldWill；WorldWill 先销毁，模型随后销毁，Session 最后销毁。服务在 FrameBegin 的 Session.BeginFrame 之后执行；Tick 只推进游戏。关闭协商被接受后取消决策并关闭模型，关闭接口要求停止工作并及时加入线程。

WorldWill 只链接 PixelLLMView 与 PixelLLM；PixelLLM 链接观测值库，外围 ModelProtocol 只链接 PixelLLM，JSON 为私有依赖。模型没有可变 Session/World 指针或调用者凭据。World、独立 Formula 和 Axiom 没有获得祈祷或模型策略依赖，游戏语义留在 Pixel。

## V1：异步、授权与因果

- **等待不阻塞固定步。** `DelayDoesNotStopSimulation` 检查模拟继续；`WorldWillWorker` 使用真实线程验证挂起、乱序完成、重复回复、不可用、容量与关闭唤醒。独立 Demo 在 worker 仍挂起时推进到 tick 4，然后释放请求并验证真实场源。本测试只证明当前实现和适配器合同，未来网络实现还必须验证自身取消与关闭行为。
- **过期动作不重放。** 每个决策具有进程内唯一 ID，完成后重复回复不产生新动作序号。世界替换、调用者或授权版本变化使旧提案失效；设备、程序和资源使用各自前置条件。单纯全局 revision 推进不使合法提案失效。
- **完整边界才发布。** 暂停时可将有效提案显示为 Ready，尚无实际动作；恢复后重新核验。`HoldUntilSafeBoundary` 与 `ExplicitCancellationWaitsForCommitAndRejectsLateReplies` 检查 PendingStep/未就绪时不改领域，期限或取消仍立即停止 worker，终态延后到安全边界，迟到回包不提交。这两个控制器用例使用转发 host 注入不可写状态；真实流式预算等待的权限拒绝由 Authority 测试覆盖。
- **保护与租约可抗衡。** 已启用设备的保护区拒绝重叠观察与干预，撤销已存在的世界意志实例、弹体、事件和场源并归还槽；不取消天然晶体或玩家设备。租约按成功全局 tick 到期，冻结不续租；观察和干预能力分开，发现知识不授予权限。
- **文本不冒充事实。** 模型只能提出 None、AtCell 或 AtDevice；本地程序、资源版本、身份和动作序号由宿主装配。`MalformedAndUnavailableResults` 验证非法/不可用/无动作结果不增加事实水位。祈祷 Applied 必须引用实际动作回执、实例和 ProgramActivated 历史证据；模型说明另存。授权历史的派生摘要也不能写入 Journal 或权限。
- **保存继续结果，不继续推理。** `AidSurvivesRestartAndProtection` 使用真实磁盘重建会话，比较成长、干预资源槽、来源身份/寿命与程序位置，核验旧调用凭据失效，再启用保护撤销已恢复的援助。Thinking/Ready 加载成 Interrupted，原文仍保留；取消后和旧世界的引用不能取消新请求，已撤销干预不复活。冻结租约测试用显式时钟夹具跳到最后一 tick，再经生产步路径验证到期，不是运行全部 600 次环境求解。

当前模型输入为有界材料和设备观测、目标、玩家原文与宿主报价，尚未把知识历史或长期关系自动接入决策输入。“关系”首版仅记录接受援助次数与安装保护的自主选择；不包含好感系统或自主循环决策。

## 本次构建与运行

独立 CPU 与 GPU agent 执行，未修改代码或 AWT。CPU 使用 CMake 缓存指定的 `/usr/bin/c++`，AppleClang 21.0.0.21000334；默认 Debug、ChunkEdge=1024，另一配置为 Release、ChunkEdge=64，均 `AXIOM_BUILD_RUNTIME=OFF`。GPU 实际使用 macOS/Apple M5/MoltenVK。

| 验证 | 本轮结果 |
| --- | --- |
| 默认 CPU 构建 14 项测试及 PixelWorldWillDemo | 成功，命令耗时 3.463 秒 |
| 默认 CPU 的 M5 专项集合 | 14/14，CTest 汇总 2.97 秒 |
| chunk64 Release 构建同一 14 项 | 成功，命令耗时 12.409 秒 |
| chunk64 Release 的同一专项集合 | 14/14，CTest 汇总 1.07 秒 |
| `./build-cpu/bin/Debug/PixelWorldWillDemo` | 退出 0，命令耗时 0.966 秒；先 `model pending=1; simulation tick=4`，随后 Applied、action sequence=1、facts=2、实际场 acceleration x=100、free influence slots=3 |
| `cmake --build build --target Pixel PixelRuntimeSmoke -j4` | 退出 0 |
| 在 Pixel 目录运行 `../build/tests/PixelRuntimeSmoke --garden-aid-only` | 退出 0；无设备燃料的援助成长、三种对象像素读回、多个帧槽与 F1 隔离；实际平台输入/ImGui 的玩家原文、请求、取消、Ready 无动作、真实实例/场/成长、保护撤销与冻结发布 |
| 在 Pixel 目录运行 `../build/tests/PixelRuntimeSmoke --debug-ui-only` | 退出 0；默认 PseudoLLM 身份及既有调试面板、设备和窗口回归 |

CPU 专项使用以下集合，两个配置均先构建对应 `Pixel*Tests` 目标后执行；默认配置另构建 Demo：

```sh
cmake --build build-cpu --target PixelKnowledgeTests PixelKnowledgeViewTests PixelKnowledgeCodecTests PixelAuthorityTests PixelAuthorityViewTests PixelWorldWillTests PixelWorldWillWorkerTests PixelInterventionCheckpointTests PixelWorldWillCodecTests PixelModelProtocolTests PixelPseudoLLMTests PixelLLMViewTests PixelLLMJsonTests PixelGameplayObservationTests PixelWorldWillDemo -j4
ctest --test-dir build-cpu -R '^Pixel\.(Knowledge|KnowledgeView|KnowledgeCodec|Authority|AuthorityView|WorldWill|WorldWillWorker|InterventionCheckpoint|WorldWillCodec|ModelProtocol|PseudoLLM|LLMView|LLMJson|GameplayObservation)$' --output-on-failure -j4
```

chunk64 使用相同命令，将目录替换为 `build-chunk64`，构建时不含 Demo 目标。固定援助成功的 GPU 夹具显式注入 FakeModel；默认 PseudoLLM 本来就可能选择 NoAction，未把随机输出强行解释为总能援助。

日志前缀 `/private/tmp/pixel-m5-acceptance-`：`config.log`、`cpu-build.log`、`cpu-ctest.log`、`cpu-demo.log`、`chunk64-build.log`、`chunk64-ctest.log`、`runtime-build.log`、`garden-aid.log`、`debug-ui.log`。这些是本机临时日志，本记录保存可重跑命令与结论。

上一任务的默认全量 94/94、Actors 修正后复验及关闭保存测试见[M4验收](M4Acceptance.md)。本轮没有生产或测试改动，因此未重复全量、garden-only 或 CloseSmoke；旧记录不计为本轮新运行。本轮也未运行完整长 GPU smoke、AET、sanitizer、性能基准或网络服务。Vulkan validation layer 不可用。引擎未改动。

## V2：独立 review

独立 review agent 审查当前整合实现，包括 WorldWill 请求/取消/期限/Ready/提交、Session 的祈祷证据与恢复、Authority 槽和撤销、ProcessHost 后代归属与清理、LLMView 撤权与派生记忆、模型协议、UI 发布及 Application 生命周期；未发现有证据的生产缺陷。审查同时核对专项测试、CPU 和 GPU 日志；没有把代码阅读当成新的运行结果。

本轮文档闭环：补充实际使用的 CTest `-j4`，将 CTest 汇总时间与外层命令耗时明确区分；Authority 文档中“验证后登记”的旧表述改为本阶段入口。生产与测试均无修改，无未决代码问题。阶段 V1/V2 的最终判断由 AWT CLI 记录。

当前接口、历史实现与明确限制继续由[知识历史](KnowledgeAndHistory.md)、[区域控制](Authority.md)、[WorldWill](WorldWill.md)、[持久援助](PersistentInterventions.md)、[模型接口](ModelServiceIntegration.md)及[LLM/PseudoLLM](LLMAndPseudoLLM.md)维护。M6 实验与阶段收尾仍是独立任务；本次完成 M5 后按用户要求暂停。
