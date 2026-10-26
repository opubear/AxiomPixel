# FormulaBridge 与三种程序承载者

> 2026-10-26 更新：默认程序与编辑器已支持 formula/2 函数组合和 World/Actor 绑定；process/1 仍保持原语义。新函数、事件作用域、状态格式3及宿主格式2兼容策略以[当前实现合同](../../Docs/Architecture/FormulaComposition.md)为准。下文保留过程执行核心和历史验证记录，不代表最新功能范围。

2026-10-25。游戏适配位于`Runtime/Gameplay/FormulaBridge`，仅编排process/1实例。`ProcessHost`处理承载者与效果，`FormulaWorldAdapter`负责扫掠、开采和Coarse风脉冲。System的`PixelFormulaProcess`只依赖标准库，World不依赖构式、RHI或UI。旧cast语言、Simulator、树形编辑器、FormulaValidation及实验代码已删除。

## 使用与范围

F4 的 Formula Editor 展开 **Process source**，点击 **Load three-pulse draft → Compile process → Apply process**。随后右键由玩家发射；展开 **Process: three carriers**，**Start natural source / Start fixed fixture** 在玩家附近装配另外两种承载者。三者使用同一个不可变产物：发球、等待命中、命中后挖掘、等待30活动步，重复三次。取消暂停或单步推进，Clear running 统一清理。两种固定承载者仅为 M3 CPU 夹具与调试样例，没有实体外观、放置占用、设备网络或真实物品库存。

默认玩家程序也使用process/1：emit黄色球后await hit，命中或到期后结束。F4仅保留源码Compile/Apply，Load applied process恢复当前源码；已有实例保留原程序版本。源码编辑与状态DTO见[合同](FormulaStateAndEditing.md)。真实Device与Intervention沿用同一ProcessHost。

`GameSession::ApplyProcess` 安装独立编译产物；已有 `ActivateProgram` 动作负责玩家激活、请求去重、发射起点和来源事实。`StartProcessFixture`、`CancelProcessCarrier`、`WakeProcessCarrier` 是仅供原生 Debug/Driver 装配使用的安全边界入口；玩家输入和外部语言不能提交自报的权限、主体或资源配置。全局暂停或预算等待期间以上修改均被拒绝。过程资源编号为2，会话版本递增，投影、弹体和事实使用同一身份。完整源码身份仍由实例保留的 ProgramArtifact 管理，LLMView 不开放配方源码。

## 承载者和领域能力

`ProcessCarrier`固定Kind、Subject、原点、瞄准角、Scope、MaximumRadius、能力集合、片段预算与有限Credits。当前包含Player、NaturalSource、FixedFixture、Device和Intervention；共用ProcessHost效果实现，资源供给和权限由各自宿主装配。Subject仅在承载类型内区分来源；同一来源可发起多个实例，Cancel/Wake按该来源统一作用。

Credits是每次激活独有的效果额度：成功的dig、wind、bolt、field或update_field扣1，拒绝和stop_field不扣。`inventory()`返回片段开始时冻结的剩余额度，不读取片段内尚未提交的扣减，也不是物品容器。UI示例天然源6、固定夹具4、玩家64；原生装配可显式提供不同额度。真实设备在启动周期前另由宿主支付Items燃料，玩家开采收益进入背包，两者不替代过程Credits。

同一阶段先完成全部语言求值和碰撞查询，再按实例激活次序与源码顺序提交效果。每个实例的库存值来自自己的固定视图。静态能力在 Instance::Start 核对，动态几何、资源、区域、驻留与风后端在提交时再次核对。

- 开采调用与原生开采相同的 `TryRemoveMaterial` 材料规则；radius=0选择所在格，其他半径按格中心圆域扫描。实际移除至少一格才 Applied；有部分结果时发布 Changed/Blocked/NotReady，全部无变化为 Blocked/NotReady。
- 风为世界坐标格/秒的增量，只适配启用且至多1024 cells的 Coarse 后端。零半径、低于解析度、零脉冲及其他后端返回 WindUnavailable，不自动改变策略。
- bolt 的语言 VelocityX 是承载者局部前向速度，游戏按激活时瞄准角旋转；不改变独立语言的坐标类型。半径0.35格，伤害仍为元数据，无重力/风漂移。速度绝对值至多600格/秒、寿命至多3600活动步、伤害至多1e6；挖掘/风半径至多16格且受承载者更小上限约束，风速模长至多600。
- 新请求的整个作用圆必须在承载者Scope和当前模拟域内；不扩张授权范围、不强制加载。飞行载体离开Scope/模拟域或遇缺失块返回 Cancelled，不能以越界命中恢复载荷。
- 程序语义/片段额度错误返回有源码位置的终止诊断；非法原生装配与内部阶段错误直接抛出。没有 catch-all 恢复半提交会话。

## 顺序、通知和生命周期

当前Session固定步为环境→Actors→FormulaBridge→设备/育圃→CommitStamp→事实及权限结算→投影，详见[固定步合同](../../Docs/Architecture/SimulationCommits.md)。桥接先完成全部过程片段求值与球的扫掠，再按稳定顺序提交效果，因此同批开采不会改变已经确定的碰撞。

新球出生边界不移动，出生重叠仍检查；已有球到期优先于被排除的最后一段运动。碰撞按 fraction、实例ID、request序号、格(y,x)排序，命中位置保留int64格坐标与格内double偏移。命中/到期在本边界提交，最早下一活动边界才交给实例；接连 `await result` 与 `await hit` 仍是两个片段，早到命中由语言运行时缓存。

桥接边界编号只用于通知因果，不等于游戏Tick。GameSession全局暂停拒绝新激活并冻结程序，预算等待和Skip不调用Step；显式单步推进完整世界。Wake只通知当前订阅，重复Wake合并，不唤醒未来订阅。仅最近request保留可观察回执，旧载体仍按正常寿命存在，直到实例结束。

stop/EOF先提交最后片段输出，再移除所属载体、通知及订阅；fault丢弃当前片段输出，之前已提交的世界事实保留。Clear、来源撤销和世界替换调用Cancel并清理所有归属。M4.5已实现[区域冻结恢复](RegionActivity.md)：承载者原点、当前作用原点或归属效果离开活动域/失去必要驻留时冻结，返回后按剩余活动时间接续；不补算冻结期间的事件或效果。M4.6已将程序产物、实例、宿主事件和弹体接入[跨领域磁盘检查点](Checkpoints.md)，恢复不会重新激活；默认球和其他活跃过程均支持保存与正常关闭。桥接编码版本2仅保存process状态，不导入旧桥接格式；项目本地旧存档已按用户要求删除。

## 上限和观测

桥接合计最多16个运行根，每帧最多8次成功激活。每个过程每活动片最多256 work、8 query、16 outputs；全体过程每阶段最多4096 work和256 outputs。过程活球最多128，每阶段效果记录容量256。过程宿主每帧最多16次风后端调用；Clear/世界替换不退款同帧额度。

每实例最多3个待交付事件（最新请求的准入与载体终态、当前Wake），最近256条宿主回执和256条程序诊断；没有随总运行时长增长的生产Trace。ProcessStats报告最近活动边界的工作/查询/请求/拒绝及当前容量，RetainedBytes仅计桥接持有的对象和容器容量，排除语言私有执行存储、共享产物、deque块和分配器元数据，不等于进程RSS。归属来源与具体拒绝保留在ProcessReceipts。

## 验证入口

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu -R '^Pixel\.Formula' --output-on-failure
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --formula-only)
```

`Pixel.FormulaBridge` 覆盖三承载者开采/风与来源、fake host对照、同批跨版本扫掠、命中/出生重叠/到期、范围与额度拒绝、冻结查询、旧实例隔离、fault原子输出、Wake去重、128载体/256回执上限、±2^55坐标、缺失区域清理、玩家动作/暂停/单步/等待拒绝和投影版本。旧 `Pixel.FormulaGameplay` 保持原有全部行为断言；GPU formula smoke新增三承载者球的真实读回、单步、F1及Clear。

## 本轮验证与独立审查

2026-10-22，本机 Apple M5，未改引擎子模块或锁定版本。

> 以下为M3.3历史验证记录，包含当时的旧树编辑器；当前验证以本轮任务记录为准。

| 验证 | 实际结果 |
| --- | --- |
| 独立 Debug 全量构建与CPU CTest，`/private/tmp/pixel-bridge-verify` | 69/69通过；随后三项审查修复仅影响新host，最终重建并重跑Formula范围10/10 |
| 独立 Release，`/private/tmp/pixel-bridge-release` | 最终Formula范围10/10通过 |
| 独立 ASan+UBSan，`/private/tmp/pixel-bridge-sanitize` | 最终Bridge、Gameplay、Rendering、Process及Hit/Rejected CLI共6/6通过 |
| Pixel与PixelRuntimeSmoke | 最终Debug构建通过 |
| `PixelRuntimeSmoke --formula-only` | 最终Apple M5 GPU读回与ImGui旧编辑器专项均通过：旧fan、新过程三承载者、颜色、frame slot、单步、F1、resize、Clear及原编辑操作 |
| 独立审查者的定向UBSan probe | 非法坐标明确抛异常，无UBSan报告；大角度生成有限速度并正常推进 |
| 一致性 | `git diff --check`、AWT check通过；Axiom工作区干净 |

GPU首次沙箱运行未能连接macOS窗口服务，已终止本次测试进程并在允许窗口访问的环境重跑成功。Vulkan validation layer不可用，GPU通过不包含validation层检查。测试代理第一次sanitizer宽正则还匹配了未构建目标，出现Not Run；精确构建范围的最终6项全部通过，不将Not Run声称通过。未运行其他GPU全场景或新增设备持久化测试。

独立review发现三项P2并逐项闭环：

1. 终点越过Scope会覆盖更早的合法Solid接触。修复为计算圆形载体退出fraction并与地形首接触竞争。审查者原probe在scope宽64与16下均正确命中并开采x12墙；仓库新增回归。
2. 原生装配先Cell/Contains再验证坐标，inf原点和INT64_MIN范围可导致转换/差值UB。修复为入口规范原点与Scope端点的支持范围，再做几何查询。仓库覆盖inf、-inf、NaN、极端chunk及Scope；审查者UBSan复现现在明确抛异常。
3. 有限大角度先乘pi产生溢出，生成NaN速度。复用旧执行器的remainder360规则后再转换；1e308角度回归与独立probe均可成功激活并推进。

审查者复核全部修复后没有未决发现；其余检查覆盖事件早到与去重、到期、终态清理、跨版本批次冻结、程序身份和会话发布边界。独立复现位于临时目录，不作为稳定测试入口；可维护回归是仓库中的 `PixelFormulaBridgeTests.cpp`。
