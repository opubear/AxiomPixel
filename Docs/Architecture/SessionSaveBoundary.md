# 会话保存、退出与旧档导入

M2.3 将会话元数据写入限制在完整提交边界；M4.6已接入[多领域一致检查点](../../.awtcontext/GlobalDoc/Checkpoints.md)。游戏定义保存策略；Axiom 只提供通用关闭请求钩子，本轮无需修改引擎。

## 请求结果

`GameSession::Control` 的 SaveWorld/ResetWorld 在协调器持有 PendingStep 时返回 `ControlStatus::Deferred`、`WaitingForBudget` 和可读提示。Deferred 不属于 Succeeded，不调用 World.Save、不写 world.meta/current.txt、不创建替代世界，也不补步或回滚。当前会话、身份、已提交数据和全部时间余量继续保留。正常模拟可以按原规则重试待完成步；保存请求本身不会排队。容量恢复并完成该步后，用户必须再次保存、退出或按 R。

正常 Save 返回 Saved；存储失败返回 Failed 并保留内存数据。正常 Reset 先保存原世界，再创建并首存新世界，成功后返回 Replaced；原有失败恢复合同保持。R 遇到 Deferred/Failed 保留相机和呈现设置。非存储的内部不变量错误继续抛出。

`LastPersistenceResult` 保留最近一次保存/重置/恢复的结果；失败或 Deferred 提示独立于 F2 调试面板显示，新的成功请求清除提示。它是服务诊断，不是领域提交，不增加 Tick/Revision。

## 关闭阶段

窗口关闭按钮、Escape 和性能捕获结束都进入 `PixelApplication::OnCloseRequested`。应用同步发起 SaveWorld，成功才允许退出；Deferred、Failed 或不可用会话拒绝当前请求。关闭请求由引擎消费一次，不会因后续容量或 I/O 恢复而自动退出。拒绝期间游戏、窗口、输入回调和调试 UI 继续工作。

`PixelGame::GameEnd`关闭WorldWill及模型工作并清理输入；直接Shutdown/析构不再保存。正常保存捕获的Thinking/Ready祈祷在下次加载时变为Interrupted，不恢复请求线程、超时或待执行提案。嵌入宿主要保存退出，必须先在帧间处理关闭请求。单独丢弃未保存状态退出不由本任务增加UI入口。

## v5 和兼容读取

v5 继续保留块格式、世界生成描述、角色分块坐标、风配置和暂停设置。时钟行改为 `CommittedTime TimeRemainder WindBoundary WindEnabled Paused`，不再保存重复的模拟时间和 DeferredStep。完整余量可以大于一个固定步，单步只消费其中一步。

v2–v4 读取旧时钟行时，以旧 PhysicsTime 为已提交时间，保留全部 TimeRemainder，校验旧 deferred 标记但不把它恢复为 PendingStep。旧 SimulationTime 不作为已提交时间。v2 保持默认平坦生成描述；v3/v4 保留已存生成身份。

导入后按保存配置在已提交时刻重建风、发布完整初始视图。旧半步欠账仍在余量中，协调器从风预推进开始执行材料、角色和构式，再提交一次。暂停导入不自行消费余量。动态风状态、构式实例和旧输入未被旧格式保存，无法恢复；checkpoint/1保存新会话的process/1运行状态、库存和设备，旧输入仍不保存。再次保存发布完整新代，内部环境配置文件沿用v5。

块在流式运行时只写独立工作目录；正常保存先等待块写入，再捕获各域并发布完整不可变世代。写失败保留上代和内存，未完成候选不被加载。有活跃旧树实例时保存明确失败；可完成或Clear running后重试。新格式与限制见检查点文档。

## 验证入口

- `Pixel.SessionStore`：独立 v5 格式样本、v2–v4 转换、损坏文件及原子单文件提交边界。
- `Pixel.Persistence`：真实容量不足时反复 Save/Reset、元数据/目录不变、显式重试、暂停和完整余量、旧 deferred 导入后材料/角色/风推进、保存失败和流式驻留版本同步。
- `Pixel.Input`：延期 R 保留相机和呈现。
- `PixelCloseSmoke`：生产关闭钩子，程序/原生关闭拒绝、独立提示、I/O 失败后 Escape 显式重试、GameEnd 不重复保存。与 PixelRuntimeSmoke 一样需真实 GLFW/Vulkan 窗口，运行目录为 Pixel。
