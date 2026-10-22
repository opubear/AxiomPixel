# 构式程序状态与编辑合同

2026-10-22，M3.4。独立语言仍只有标准库依赖，游戏接入见 [FormulaBridge](FormulaBridge.md)。本阶段完成程序与运行状态 DTO、fake host 挂起/恢复验证及游戏源码编辑入口。跨领域磁盘 checkpoint、游戏区域卸载恢复、配方库与设备实体存档属于 M4；现有 SessionStore 仍不保存程序、草稿或运行实例。

## 程序资源与版本

`ProgramArtifact::Export()` 返回 `ProgramRecord`：格式版本 1、完整 `ProgramIdentity` 和源码。身份包含资源名称、源码 SHA-256、语言语义版本与宿主合同版本。`ImportProgram` 先拒绝不支持的版本，再按编译器容量限制重新编译并核对内容身份。DTO 不保存 C++ 指针、编译器容器或原生字节码；未定义磁盘编码。

`Instance::Restore` 必须使用与快照完全相同的程序身份，不自动迁移、重新开始或改绑当前资源。编辑同一资源生成新的不可变产物，旧实例及挂起胶囊继续保留原源码与身份。游戏 `ProgramRef` 的 ResourceId=2 和递增会话 Version 是激活选择器；完整语言身份由产物及调试视图提供，两种版本不可互相替代。

运行状态的 `SchemaVersion=1` 同时固定当前 continuation 布局（指令编号、局部变量类型和挂起点）。以后即使不改语言语义，只要编译器改变该布局，也必须升级状态格式或提供显式迁移。当前不支持旧格式迁移。

## 安全挂起点与状态

`ExportState()` 仅允许 `Sleeping` / `Awaiting`，初始 Ready 和终止 Ended 调用属于宿主编程错误。宿主必须先提交整个 StepResult，再导出；运行器无法代替宿主判断世界是否已提交。快照不包含尚未提交的请求数组。

| 值 | 保存语义 |
| --- | --- |
| 实例 ID、PC、Phase | 从原 wait/await 后的 continuation 继续，不能重新执行 emit |
| Locals | 最多 16 个类型化值，只导出挂起点词法作用域内的活跃绑定；编译器提供类型与活跃槽信息 |
| ActiveSteps | 实例实际调用 Step 的活动次数，包含首次求值；不是世界时间差，也不是墙钟 |
| Remaining | 当前延迟/超时剩余的活动步数；恢复本身不递减 |
| LastBoundary | 原宿主边界坐标，恢复后的首次 Step 必须严格更大；不能把宿主时钟重置为零 |
| LastRequest、EventSequence | 请求与去重水位延续，恢复不复用已发出的序号 |
| Receipt/Impact、Seen 标志 | 保存早到事件与已消费状态，避免恢复后丢通知或把第二次回执当成第一次 |
| WakeSubscription、SubscriptionSequence | 当前等待及其 generation 保持，后续等待生成新的 token |
| Origin、LastOutcome | 保留命中改变后的精确格坐标/格内偏移及最近等待结果 |
| Limits | 保留片段执行预算；恢复宿主负责批准本次预算策略，语言校验其全局上限 |

导入检查版本、程序身份、挂起指令/等待种类、活跃槽/值类型、数值、请求/订阅及缓存事件关联；错误数据返回 Diagnostic。正常运行路径不做磁盘检查或重新编译，内部宿主协议错误继续直接抛出。DTO 可复制并不等于授权复制活实例；单实例身份、单一恢复所有权及宿主预算/区域/资源准入由宿主管理。

process/1 **没有随机操作，也没有隐式随机状态**。恢复不会重播 RNG 或重新取 seed。将来引入随机功能必须明确算法版本与完整状态，升级语言/状态合同；不能只保存 seed 后推断已消费次数。

## 宿主效果与世界事实

语言快照只保存语言状态；宿主同时保存对应的待交付通知、下一事件序号和归属效果状态。当前 fake host 的 `SuspendedProcess` 是这一组合的可执行示例。它只在已提交的 Advance 之后导出，将 interpreter 销毁并把载体/通知移出活动列表；Resume 重新导入程序、恢复实例，成功后消费胶囊。重复 Resume、导出未运行实例等属于宿主错误。能力拒绝保留胶囊，允许重新准入或取消。

| 对象 | 挂起 / 终止行为 |
| --- | --- |
| 已提交的一次性 Dig、Wind 脉冲 | 世界事实留在世界域，不从运行状态重放，不因 Cancel 回滚 |
| 程序拥有的 Bolt / 未来持续作用句柄 | 和实例共同冻结，保存当前状态及剩余寿命；正常终止/fault/cancel 清理 |
| 独立世界实体 | 世界域拥有其身份与寿命，程序结束不删除；不能同时登记为程序归属效果 |
| 待交付事件 | 已产生但未送达的事件由宿主胶囊保存，已经送达的缓存由语言快照保存；去重序号不重置 |
| 唤醒订阅 | 挂起保留 token；超时、取消和终止撤销；无活动 Step 不扣超时 |

fake host 的 `ReleaseCarrier` 显式把测试载体转交世界列表，之后不再向程序发送 hit/expiry；准入回执仍有效。此测试操作验证所有权，未给游戏 DSL 增加实体生成或转移语法。独立实体按世界的 Advance 推进，程序挂起/取消不影响它；Skip 表示整个参考世界不推进。

参考宿主冻结期间不接纳 Hit/Wake；真实游戏未来若允许外部事件唤醒非活动区，应在宿主层排队，并定义区域加载、订阅有效性和交付边界。当前游戏 ProcessHost 在承载者离开活动域或丢失驻留时仍取消；本阶段不把 fake host 胶囊冒充跨区域或跨进程存档。风脉冲只保证不重复注入，不保证求解器动态风场精确恢复。

## 编辑与调试入口

F4 的 Formula Editor 展开 **Process source**：载入三脉冲草稿或当前过程源码，编辑 **Process text**，点击 **Compile process**，再点击 **Apply process**。编译不发布；编辑文本会撤销旧编译结果，编译错误显示资源、行列和说明，Apply 在无有效产物时禁用。此最小编辑器固定使用 `editor.process` 资源，载入其他程序相当于把源码复制到此草稿。

Apply 经 GameSession 安全边界发布新版本，只影响后续玩家/承载者激活；原实例不重启、不补跑、不改绑。旧树编辑器的 Apply 切回旧语言。**Process: three carriers** 继续提供天然来源与固定夹具启动按钮，并按实例显示版本、内容 hash、PC、等待阶段、ActiveSteps、Remaining 和剩余 credits。只读 InspectProcesses 返回值副本，不泄漏执行器可变句柄，也不导出完整存档。

## 验证入口

- `Pixel.Formula.Process.State`：程序记录往返、所有类型局部值、非法/跨版本拒绝、挂起后重建解释器、冻结计时、载体剩余寿命、早回执/命中缓存、重复回执、Wake generation/超时、取消清理、独立实体寿命、循环多次往返、草稿失效和新旧产物隔离。
- `Pixel.FormulaBridge`：新增编辑产物发布、只读状态检查与新旧实例效果隔离。
- `PixelRuntimeSmoke --formula-only`：真实键盘输入源码、Compile/Apply、错误草稿、旧实例隔离以及原 GPU / 树编辑器回归。

最终验证基线为 `f15702a` 加本任务变更，运行环境为 macOS / Apple M5：

| 验证 | 结果 |
| --- | --- |
| 独立验证 agent：`cmake -S Pixel/Formula -B /private/tmp/pixel-state-standalone -DCMAKE_BUILD_TYPE=Debug`、build、CTest | 6/6 |
| 独立验证 agent：`cmake --build /private/tmp/pixel-bridge-verify -j4`；`ctest --test-dir /private/tmp/pixel-bridge-verify --output-on-failure` | Debug 完整 70/70 |
| 独立验证 agent：Release Formula 相关目标 build；`ctest --test-dir /private/tmp/pixel-bridge-release -R Formula --output-on-failure` | 11/11 |
| 独立验证 agent：ASan+UBSan Formula 相关目标 build；`ctest --test-dir /private/tmp/pixel-bridge-sanitize -R Formula --output-on-failure` | 11/11，无 sanitizer 报告 |
| `cmake --build build --target Pixel PixelRuntimeSmoke -j4` | 游戏与 GPU/UI 测试均构建通过 |
| 在 `Pixel/` 运行 `../build/tests/PixelRuntimeSmoke --formula-only` | GPU 黄色球读回、三承载者、旧树编辑器、新过程源码编译/Apply、无效草稿、新旧实例隔离全部通过 |
| 独立 review | 源码、上下游和本合同无发现；独立重建与 CTest 6/6、Bridge 1/1 |

GPU 首次测试在编辑器展开时误把右键投到其覆盖的坐标，输入被 UI 正常捕获；测试改为真实 F4 收起/重开面板后通过，没有更改输入过滤。当前环境未安装 Vulkan validation layer，因此 GPU 成功结果不包含该层检查。未改引擎，未运行 AET；游戏跨领域磁盘保存不在本轮验收范围。
