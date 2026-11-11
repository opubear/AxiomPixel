# 世界意志、知识与玩家自主权

> 状态：离线授权观察—受限提案—真实动作—事实档案闭环已实现；网络模型、理解玩家语义及自主剧情未交付。核对日期：2026-11-10。

## 玩家体验目标

世界意志可以回应玩家请求，但只能依据被允许看到的世界提出受限建议。玩家应能区分“模型说了什么”“实际上做了什么”及“现在还有效吗”，并能选择阻止观察和撤销援助。

当前可玩的入口位于力场育圃开发面板：向家园设备请求临时推力，或启用保护选择自主维持。知识和权限已有领域能力，但没有完整的玩家知识图鉴、关系成长或剧情系统。

## 从观察到结果

| 环节 | 已实现的规则 | 详细合同 |
|---|---|---|
| 观察 | LLMView冻结同一个完整提交；只读取已驻留数据，不加载或激活世界 | [查询服务](LLMView.md)、[观测投影](ObservationProjection.md) |
| 授权 | 默认拒绝；区域、主题、库存和知识权限分别核验；撤权使旧查询/未交付结果失效 | [权限](Authority.md)、[设备库存观测](GameplayObservation.md) |
| 发现与历史 | ObserveForce记录真实加速度证据；重要动作保留有来源历史，知识不自动授予编辑或行动权限 | [知识与历史](KnowledgeAndHistory.md) |
| 提案 | 共用DecisionAPI只允许说明和一个不行动/格子目标/设备目标提案 | [模型接口](ModelServiceIntegration.md)、[PseudoLLM](LLMAndPseudoLLM.md) |
| 执行 | 宿主重新核验程序、设备版本、范围、权限与资源；模拟线程队列完成后才记Applied | [WorldWill](WorldWill.md)、[模拟线程](../../Docs/Architecture/SimulationThread.md) |
| 持续效果 | 最多4个干预槽，租约600个成功全局tick，后代共享归属/范围/额度 | [权限](Authority.md)、[持久援助](PersistentInterventions.md) |
| 玩家撤销 | 启用保护设备阻断对应区域观察/干预，撤销重叠已有援助；解除保护不复活旧实例 | [权限](Authority.md) |
| 追溯 | 玩家原文、模型说明、动作回执、事实范围和稳定历史证据分别存储 | [持久援助](PersistentInterventions.md) |

最多保留128条重要历史、128个玩家与位置组合的发现、16条有来源模型派生摘要，以及32条祈祷记录。容量淘汰后承认历史缺口，不补造事实。模型摘要是解释，不能成为世界状态的权威来源。

## 当前模型与交互

默认 `PseudoLLM` 是普通C++函数的随机组合：选择援助或等待，再从已获授权的观察中寻找合法目标。它不理解玩家祈祷语义，不需要网络、密钥或供应商配置。玩家文本最多1024字节、合法UTF-8；文本不扩大作用范围或权限。

组合根可注入其他 `llm::LLM`。`System/LLM/ModelService` 提供通用文本生命周期，`Gameplay/LLM/Services/ServiceLLM` 负责游戏观测编码和提案校验；目前没有真实网络提供者。测试专用FakeModel有工作线程、延迟、重复回包和取消控制，它不是默认游戏模型。

F2的育圃面板可以输入祈祷、请求和取消，并显示模型名称、解释、实际动作和当前援助状态。默认可能返回NoAction，允许玩家再次请求，没有自动重试。

| 显示 / 时序 | 当前含义 |
|---|---|
| Thinking | 请求被接纳、尚无最终结果；不代表世界已改变 |
| 内部提案就绪 | 已收到模型回复，仍需核验和执行；Ready枚举为旧协议/存档保留，当前控制器不在暂停时发布Ready |
| Queued动作回执 | 命令已入模拟队列，尚未提交；WorldWill继续等待 |
| Applied | 当时确实提交动作，须另查当前实例/场源判断效果是否仍存在 |
| NoAction / Rejected | 没有产生援助，不能显示为已完成帮助 |
| 全局暂停 | 拒绝新请求；暂停轮询/发布并顺延已有期限，恢复后继续 |
| 预算等待 | 仍消耗默认5秒期限，到期取消模型，终态等完整提交边界发布 |
| 取消 / 撤权 | 可撤回尚未执行的排队动作；已进入模拟事务不可撤回，最终结果以实际提交为准 |
| 世界替换 | 旧epoch/调用者和迟到回复失效，不跨世界执行 |

## 自主权与保存边界

保护模块依附玩家设备：2×2占地向四周各扩16格，形成34×34范围。模块安装且设备启用时生效，不耗煤；观察与干预是独立能力。保护撤销实际场源及后续效果，但不会撤回已发生的材料运动和花床成长。

当前“关系”只记录接受援助次数和是否选择安装保护，没有好感值、敌意、惩罚或工厂规模规则。干预的600 tick租约是成功全局时间，冻结区域仍会到期；程序/场源自身的活动时间则冻结，二者不能混淆。

显式 Persistent 会话能保存知识、权限、活跃/冻结干预和祈祷证据，恢复不续租、不重放动作。Thinking及兼容Ready记录恢复为Interrupted，不恢复模型线程或请求。默认 Temporary 游戏不保存进度，退出即结束该世界的档案；详见[检查点](Checkpoints.md)。

## 未交付与后续设计

| 议题 | 需补齐的内容 |
|---|---|
| 真实模型服务 | 选择提供者、配置/凭据管理、联网取消/超时/失败与实际连通验收 |
| 语义理解 | 当前随机兜底不根据原文推理；需定义模型目标与允许行动，不能只替换UI文案 |
| 玩家知识体验 | 当前有数据与授权查询，无独立知识UI、解锁/学习进阶或完整配方发现流程 |
| 世界人格与关系 | 需定义长期反馈、玩家可理解的原因与自主权；当前数据不足以声称已实现人格成长 |
| 持续自主行动 | 当前显式请求一次，不自动祈祷、不后台无限规划；多动作事务与持久BehaviorDSL尚未交付 |
| 更广观察主题 | 群落生成已经存在，LLMView温度/生态主题仍Unsupported；不要把生成参数当成模型可见模拟值 |

上述为策划/项目决策入口，未自动转为承诺或排期。有限决策图实验及暂缓持久DSL的理由见[行为实验](../../Docs/Architecture/BehaviorDecisionExperiment.md)。

## 验收与实现定位

验收首先验证“说明不等于动作”：非法/过期/撤权提案无效果，合法动作有真实回执与来源；延迟、乱序、重复、取消和跨世界回复不会重复执行。另核对区域隐私、全局暂停、队列取消、保护撤销、持久恢复与历史缺口。

现有入口包括 `Pixel.WorldWill`、`Pixel.WorldWillWorker`、`Pixel.PseudoLLM`、`Pixel.ModelService`、`Pixel.Authority`、`Pixel.AuthorityView`、`Pixel.Knowledge`、`Pixel.KnowledgeView`、`Pixel.InterventionCheckpoint` 及 GPU `--garden-aid-only`。本次整理未重跑这些测试，历史结果按其原文日期读取。

- [WorldWill请求与时序](../../Pixel/Source/Runtime/Gameplay/WorldWill/WorldWill.cpp)
- [权限及干预安装](../../Pixel/Source/Runtime/Gameplay/GameSession/SessionAuthority.cpp)
- [祈祷档案](../../Pixel/Source/Runtime/Gameplay/GameSession/SessionWorldWill.cpp)
- [模型边界](../../Pixel/Source/Runtime/Gameplay/LLM/LLM.h)
- [当前应用帧装配](../../Pixel/Source/Runtime/Gameplay/Application/PixelGame.cpp)
