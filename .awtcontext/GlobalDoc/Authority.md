# 区域控制与干预撤销（M5.2）

Authority 是 Pixel 的玩法领域，拥有世界意志的观察/干预能力、设备保护模块和授权版本。Session 在安全提交边界使用它裁决动作、查询和持续实例；World 与独立 Formula 语言均不依赖 Authority。没有新增引擎能力或修改相邻 Axiom。

## 身份与能力

`GameSession::WorldWillSession()` 返回宿主装配凭据，和 Input、Debug、Driver 分开，世界替换后全部失效。请求仍只有 epoch、sequence、类型化 payload，没有 owner/admin/grants 字段。世界意志凭据仅可提交 `InstallIntervention`，不能调用普通玩家动作、程序编辑、调试设置或 SessionControl。原生可信代码仍负责注册观察者及向未来模型适配器绑定凭据。

`SetWorldWillCapabilities(Debug/Driver, {Observe, Intervene})` 修改当前能力，Input 无权调用。初始两项均开启；观察与执行独立。发现知识不授予这些能力，也不授予程序源码。外部协议不能自行选择 NativeCaller；原生 C++ 装配接口不是网络安全边界。

## 一个保护设施规则

已有推力设备可安装 `SetDeviceProtection{DeviceRef, Enabled}` 保护模块。动作核验当前设备 ID、Version 与玩家归属。保护矩形由设备位置决定：以 2×2 占地向四边各扩 16 格，得到 34×34 半开矩形；调用者不能自选矩形或归属。

模块安装且设备 `Enabled` 时，该矩形阻止世界意志的观察和干预；任一格重叠即拒绝整个请求。与煤驱动的推力功能分开，首版保护模块不耗煤，也不因设施离开活动岛而消失。关闭设备暂停保护，重新开启恢复；拆除设备删除模块，新设备即使放回同一坐标也不会继承。设备 ID 在同一世界内不重用，epoch 防止跨世界重用；旧 DeviceRef 修改返回 PreconditionFailed。

所有权限变化增加 Authority Version。原生玩家、自然晶体与玩家设备的作用不因这条针对世界意志的规则被取消。规则不模拟力场屏障，不逆转已完成的挖掘或已注入的风扰动。

## 持续干预

`InstallIntervention{ProgramRef, Origin, Scope}` 只能引用宿主已 ApplyProcess 的当前 process/1 程序。旧版本返回 StaleProgram，legacy tree 不支持。Scope 为 1–64 格边长的矩形，坐标范围 ±2^54 内，Origin 必须在其中且驻留于活动世界。宿主分配非重用承载 ID、最多64次付费输出额度、600个成功全局 tick 的租约；语言已有单步预算、最多16实例/128弹体/半径16限制继续生效。

安装先核验当前能力与区域，再启动并发布实际 ProgramActivated/效果事实。requestSequence 重复返回原回执，不再启动；重复成功回执表示原请求的结果，不承诺实例仍存在。暂停期间允许原生驱动的显式安装；异步自主调度策略留给 M5.3。

每次完整模拟步后及安全流式边界重新核验；保护/能力动作在同一提交内立即撤销不再合法的实例。撤销通过 FormulaBridge 的承载归属清理实例、弹体、队列事件和全部 owned FieldSource，包括冻结实例。续发、命中后续、field update 等继续共享原始 carrier 的范围、能力、额度和租约，不能扩大权限；已经撤销的实例不会在控制恢复后自动复活。

600 tick 租约按成功全局步计数，暂停和预算等待不耗时；冻结实例也会随其他区域的成功步到期。自然结束按原有 owner 清理；ClearRunning/显式原生取消可主动结束。控制撤销与租约到期写入 `InterventionStopped`，记录实例、主体和 control_revoked/lease_expired；`AuthorityChanged` 记录版本、保护设备/范围和当前能力。两类事实进入128条重要历史，并可由有全局历史权限的主体查看带来源 JSON。

## 动态查询

`llm::Policy::AuthoritySubject` 由宿主绑定上述主体凭据，并必须指定 Region。源接口 `ObservationSource::AuthorizeObservation(subject, region)` 默认拒绝，GameSession 使用同一个 Authority 规则返回版本与许可。

OpenView 在捕获前核验整个 Policy.Region；任一保护区相交则拒绝，即使请求的较小 Scope 没相交。这是首版保守规则，宿主可重建更小的授权区。Query、ReadFacts、ReadHistory、Deliver 和服务推进重新核验；版本变化或撤权使旧 view、游标与 pending 整体失效并释放数据，返回 ViewRevoked。权限恢复后必须重新打开。授权主体不能通过 SetPolicy 解除或改绑，宿主需销毁观察者后重新注册。

动态观察者只持有已授权区域/主题的副本。Programs、Runtime、Facts、History 等全局信息不允许绑定区域策略；私有库存与知识仍需独立 owner/all grant，局部设备未授权 Fuel 隐去；隐藏的全局对象不会进入保留内存统计。既有原生静态 Policy 是可信宿主权限，行为保持；已交付文本不能收回，未来动作仍核验当前授权。

## PendingStep 与存档边界

真实预算等待期间，安装干预、切换保护及修改能力均返回 WaitingForBudget，不能修改准备中的物理步、来源或授权版本。释放预算后仅完成原来的一个成功步，随后可正常撤销。

本项不实现 M5.4 的持久干预检查点。持久 Session 存在保护模块、活跃干预或非默认能力时，Save/Reset/CreateForceGarden 返回 Unsupported 并保留当前会话，不写出不完整权限存档。停止干预、移除模块并恢复默认能力后可正常保存；历史过程回执可以保存，但不会复活已终止实例。知识/重要历史仍沿 M5.1 的 DTO-only 边界，尚未接磁盘。

当前提供 C++ 动作及 CPU 贯穿验证，没有新的 UI、模型网络或自主决策循环。后续 M5.3 接异步 fake model，M5.4 再接持久化与可试玩的自主权场景。

## 验证

测试入口为 `Pixel.Authority` 和 `Pixel.AuthorityView`。前者使用真实 Session、设备、FieldSource、LLMView 和流式预算；后者使用默认拒绝/动态版本的 fake source，验证所有查询、交付、策略和数据过滤边界。构建与实际结果在本任务最终验证后登记。

2026-10-23，macOS，本轮最终代码验证：

- 独立验证agent执行 `cmake --build build-cpu -j4`，全目标重链接成功；随后 `ctest --test-dir build-cpu --output-on-failure -j4` **86/86通过**，46.94秒。
- `build-chunk64` 刷新为 Release、`AXIOM_BUILD_RUNTIME=OFF`、`PIXEL_CHUNK_EDGE=64` 后构建 Authority、AuthorityView、KnowledgeView、LLMView 测试，**4/4通过**。
- `cmake --build build --target Pixel -j4` 成功。第三方配置警告未影响构建。
- `git diff --check`、AWT check及更新文档的本地链接检查通过。
- 独立干净上下文review及最终增量复核完成，无未决重要项；review只检查代码，构建测试由独立验证agent执行。

实际覆盖：正负/远坐标和半开边界、宿主身份与独立能力、旧设备版本/删除同址重建、重复与过期请求、保护开关阻断、真实场源撤销、天然来源所有权隔离、后续弹体/事件范围继承、冻结撤销、600 tick租约、旧查询/游标/pending释放、隐藏对象与保留内存统计、真实预算等待拒绝与单步恢复、暂态状态保存拒绝、清理后checkpoint不复活旧干预。预算夹具排空并移除可逐出的预取块，再发布基线后耗尽预算，明确断言实际进入PendingStep。

未执行新GPU smoke、AET、sanitizer或性能基准；本项没有渲染、输入UI或引擎变更。该记录不代表模型网络、祈祷、持久权限和可试玩自主权场景已实现。
