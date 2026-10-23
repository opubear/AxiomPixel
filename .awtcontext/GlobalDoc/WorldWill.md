# 异步 WorldWill 与离线模型（M5.3）

WorldWill 通过 LLMView 取得授权观测副本，在固定步之外等待模型，再由主线程把提案转换成 GameAPI 动作。当前默认使用随机PseudoLLM，受控FakeModel仅用于测试，没有网络、供应商 SDK 或自动祈祷；M5.4增加[育圃祈祷UI与持久结果](PersistentInterventions.md)。已有过程语言负责实际持续效果。本模块属于 Pixel，未修改引擎。

## 所有权和运行时接线

`PixelWorldWill` 链接LLMView和独立的PixelLLM，不链接Gameplay、World或Formula；线程测试适配器位于PixelFakeModel目标。`GameAPI::WorldWillHost` 提供当前提交、宿主分配的调用者、下一个动作序号、已批准程序、干预资源和可写阶段。`GameSession` 实现这个主线程端口；后台线程从不持有它。

`PixelGame` 持有 `GameSession → unique_ptr<LLM> → WorldWill`，按相反顺序销毁。模型所有权、随机兜底与共享API见[LLM与PseudoLLM](LLMAndPseudoLLM.md)。在 `FrameBegin` 的 `Session.BeginFrame()` 后推进 WorldWill 服务，固定步 `Tick` 不推理、不等待模型。`GameEnd` 在既有关闭协商接受后取消请求并加入工作线程；保存被拒绝时继续原会话。请求由宿主显式调用 `GetWorldWill().Request(goal)` 发起，当前不会自动循环请求；M5.4 的育圃面板提供离线祈祷入口。

请求建立绑定当前世界意志身份、区域和 Materials/Devices 主题的 LLMView，完整读取同一冻结视图后立即关闭。`ModelInput` 自有提交 stamp、意图、程序引用、资源报价、材料格和设备观测；没有 span、string_view、可变领域引用、源码或授权凭据。设备 Fuel 仍受库存授权约束，本观察者没有该权限。未驻留格的 MaterialId 缺失，不能当空气。

## 有界决策与关闭

每个控制器最多 2 个在途请求，每次观测至多 64×64 材料格、64 设备、512 KiB 自有输入。提案说明至多 1024 字节；保留最近 32 条终态记录，每次服务最多取 8 条回复。私有 LLMView 沿用自身快照、分页和交付限制。

状态为 Thinking、Ready 以及 Applied、Rejected、NoAction、TimedOut、StaleEpoch、ViewRevoked、ModelUnavailable、InvalidProposal、Closed；持久化恢复另有 Interrupted，M5.5接口工作增加显式Cancelled。Ready在安全暂停边界发布有效但未执行的提案，恢复后仍须重新核验游戏条件，见[模型交互](ModelServiceIntegration.md)。默认期限为单调墙钟 5 秒，构造可选 1ms–60s；暂停和预算等待不延长期限。Service 必须使用单调时间；测试可注入该时间。暂停、PendingStep 或主机未就绪时保留已返回提案，等安全边界再提交。PendingStep/未就绪期间到期仍及时取消 worker，终态档案等完整提交边界再写入；晚到回复不能恢复已到期的动作。

世界替换或调用者改变后，旧请求在下一次服务时终止并取消后台工作；权限版本变化也会终止，即使能力先撤销后恢复。每个决策 ID 在进程内唯一，退休 ID 和重复回复不能产生新动作。Close 取消全部在途请求、释放观察者并关闭模型端口，幂等且不可重新开启；重新创建控制器使用新 ID。

`LLM/Testing/FakeModel` 有真实工作线程，支持受控延迟、手动释放、重复回复和 unavailable。最多 2 份待执行输入、4 条完成回复、8 个测试计划；完成回复也参与新请求的准入。等待用条件变量，挂起第一条不阻止第二条先完成，关闭会唤醒等待。决定仅扫描已交付观测，找不到目标返回 NoAction。没有自动重试或吞掉内部异常的逻辑。

## 提案与动作裁决

模型只能返回 None、AtCell 或 AtDevice，带目标格或设备 ID；不能选择调用者、epoch、序号、程序源码、控制权、额度或删除前置条件。主线程把冻结的 ProgramRef、DeviceRef 和资源版本重新装配到动作中，并把目标限制在已观测范围内。

- `InstallIntervention{Program, Origin, Scope, ExpectedResourceVersion}`：使用宿主已批准的当前 process/1、当前区域权限、驻留/活动范围和明确资源版本。
- `InterveneAtDevice{Program, DeviceRef, ExpectedResourceVersion}`：首先验证当前设备仍是原 ID/Version，再派生设备中心 `(x+1,y+1)` 和 34×34 范围，复用上述安装路径。模型不能用旧坐标替代设备身份；完整作用范围须在观测区域内。

动作序号在实际提交时从主机取得，按提交顺序递增；不把模型请求编号当动作序号。回包乱序、其他可信调用及重复投递都不会重新使用旧序号。GameAPI 继续使用每调用者高水位及有限回执缓存去重。

全局 revision 的普通推进不会使提案自动失效。设备、程序、资源和授权按各自合同裁决；设备删除/版本变化或资源报价改变返回 PreconditionFailed，程序变化返回 StaleProgram，权限不符拒绝。已撤销的观测直接产生 ViewRevoked，不提交动作。成功产生真实 ProgramActivated/效果事实，DecisionResult 保留 ActionReceipt 及其事实范围。模型说明只是一段派生文本，不写成领域事实。

## 干预资源

Authority 维护独立于解释器 16 实例技术上限的 4 个并发干预槽。每次成功安装占 1 槽，自然结束、取消、ClearRunning、撤权或租约到期归还；每次占用/归还都增加资源版本。资源变化不增加观察授权版本，不会把资源冲突伪装成 ViewRevoked。缺少槽返回 LimitExceeded；请求必须携带非零的预期资源版本，旧报价拒绝。安装立即结束的程序在同一安全边界回收，不预付无限持续影响，也不使用玩家库存。

M5.2 的能力、作用范围、64 次付费输出、600 个成功全局 tick 租约及全部后代归属规则继续有效。重置生成新 epoch/caller 并恢复 4 槽。M5.4将资源账本、干预租约、保护/能力及有来源的祈祷终态接入同提交存档；加载不重放未完成推理，详细合同见[持久援助](PersistentInterventions.md)。

## 离线演示和验证入口

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu --target PixelWorldWillDemo PixelWorldWillTests PixelWorldWillWorkerTests PixelAuthorityTests -j4
./build-cpu/bin/Debug/PixelWorldWillDemo
ctest --test-dir build-cpu -R '^Pixel\.(WorldWill|WorldWillWorker|Authority|AuthorityView)$' --output-on-failure
```

Demo 先挂起真实 worker 请求，推进四个模拟步，再释放提案；输出模拟 tick、动作序号、真实事实数量、实际加速度和剩余槽数。无需窗口、网络或模型密钥。

`Pixel.WorldWill` 使用可控模型端口及真实 GameSession 检查延迟、顺序、重复、超时、旧 epoch、对象/资源/程序变化、授权撤销、非法回复和关闭；成功路径验证回执与领域事实的因果链。Pending/NotReady 由转发主机注入，真实流式 PendingStep 仍由 `Pixel.Authority` 覆盖。`Pixel.WorldWillWorker` 使用真实线程检验模拟推进、逆序释放、重复回复、不可用、队列容量及关闭唤醒；`Pixel.Authority` 追加槽生命周期及设备锚点合同。

2026-10-23，macOS，独立验证 agent 执行：

- `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF` 与完整 `cmake --build build-cpu -j4` 成功；`ctest --test-dir build-cpu --output-on-failure` **88/88 通过，89.61 秒**。
- 离线 Demo exit 0：`model pending=1; simulation tick=4`，随后 `Applied; action sequence=1; facts=2; field acceleration x=100; free influence slots=3`。
- `build-chunk64` 显式配置 Release、`AXIOM_BUILD_RUNTIME=OFF`、`PIXEL_CHUNK_EDGE=64`，构建四项 Authority/AuthorityView/WorldWill/WorldWillWorker，**4/4 通过，1.36 秒**。
- `cmake --build build --target Pixel -j4` 成功。已有第三方配置和重复库警告未阻断构建。
- 独立干净上下文强模型 review 检查新模块及动作、资源、事实、Application 生命周期的上下游，无未决重要问题；该 review 未运行构建，实际测试由另一验证 agent 完成。

未运行 GPU smoke、AET、sanitizer 或性能基准，没有网络服务验收。本项未修改嵌入引擎源码或相邻参考 Axiom。
