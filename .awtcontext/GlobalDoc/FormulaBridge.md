# FormulaBridge 与三种程序承载者

2026-10-22，M3.3。现行游戏适配位于 `Pixel/Source/Gameplay/FormulaBridge/`；`FormulaBridge` 编排旧树形运行器和新的 `process 1` 语言实例，`ProcessHost` 解释新语言宿主请求，`FormulaWorldAdapter` 复用原有扫掠、开采和 Coarse 风脉冲规则。`PixelFormulaProcess` 仍只依赖标准库；World 无 Formula、RHI 或 UI 依赖，本轮未改引擎。

## 使用与范围

F4 的 Formula Editor 展开 **Process: three carriers**，点击 **Apply three-pulse process**。随后右键由玩家发射；**Start natural source / Start fixed fixture** 在玩家附近装配另外两种承载者。三者使用 `ProcessExample.h` 的同一个不可变产物：发球、等待命中、命中后挖掘、等待30活动步，重复三次。取消暂停或单步推进，Clear running 统一清理。两种固定承载者仅为 M3 CPU 夹具与调试样例，没有实体外观、放置占用、设备网络或真实物品库存。

旧树形编辑器的 Apply 切回旧语言。两种语言可以短期并存，已运行实例保留各自原程序及版本；Revert to applied 仍只恢复最近的旧树形描述，过程程序生效时工具栏明确说明。新过程的编辑/保存/恢复合同由 M3.4 处理，完整设备及持久化由 M4 处理。

`GameSession::ApplyProcess` 安装独立编译产物；已有 `ActivateProgram` 动作负责玩家激活、请求去重、发射起点和来源事实。`StartProcessFixture`、`CancelProcessCarrier`、`WakeProcessCarrier` 是仅供原生 Debug/Driver 装配使用的安全边界入口；玩家输入和外部语言不能提交自报的权限、主体或资源配置。预算等待期间以上修改均被拒绝。过程有自己的资源编号2，旧树编号1；会话版本递增，投影、弹体及事实不再把新过程标为旧树。完整源码身份仍由实例保留的 ProgramArtifact 管理，LLMView 不开放配方源码。

## 承载者和领域能力

`ProcessCarrier` 固定 Kind、Subject、原点、瞄准角、Scope、MaximumRadius、能力集合、片段预算与有限 Credits。三种 Kind 只影响来源标识，不分叉效果实现。Subject 仅在承载类型内区分来源；同一来源可发起多个实例，Cancel/Wake 按该来源统一作用。

Credits 是每次激活独有的演示额度：每个成功接受的 dig、wind 或 bolt 扣1，拒绝不扣。`inventory()` 返回本次片段开始时冻结的剩余额度，查询不读取片段内尚未提交的扣减。这不是 M4 的物品容器，也不承诺同主体的多个激活共用库存。UI 示例天然源6、固定夹具4、玩家64；原生装配可以显式提供不同额度。

同一阶段先完成全部语言求值和碰撞查询，再按实例激活次序与源码顺序提交效果。每个实例的库存值来自自己的固定视图。静态能力在 Instance::Start 核对，动态几何、资源、区域、驻留与风后端在提交时再次核对。

- 开采调用与原生开采相同的 `TryRemoveMaterial` 材料规则；radius=0选择所在格，其他半径按格中心圆域扫描。实际移除至少一格才 Applied；有部分结果时发布 Changed/Blocked/NotReady，全部无变化为 Blocked/NotReady。
- 风为世界坐标格/秒的增量，只适配启用且至多1024 cells的 Coarse 后端。零半径、低于解析度、零脉冲及其他后端返回 WindUnavailable，不自动改变策略。
- bolt 的语言 VelocityX 是承载者局部前向速度，游戏按激活时瞄准角旋转；不改变独立语言的坐标类型。半径0.35格，伤害仍为元数据，无重力/风漂移。速度绝对值至多600格/秒、寿命至多3600活动步、伤害至多1e6；挖掘/风半径至多16格且受承载者更小上限约束，风速模长至多600。
- 新请求的整个作用圆必须在承载者Scope和当前模拟域内；不扩张授权范围、不强制加载。飞行载体离开Scope/模拟域或遇缺失块返回 Cancelled，不能以越界命中恢复载荷。
- 程序语义/片段额度错误返回有源码位置的终止诊断；非法原生装配与内部阶段错误直接抛出。没有 catch-all 恢复半提交会话。

## 顺序、通知和生命周期

固定步仍为环境 → 玩家 → FormulaBridge → CommitStamp → 事实/投影。桥接先完成全部旧树扫掠、新过程片段求值与现有/新生球的扫掠，再提交旧树效果、新过程效果。因而任一版本的同批开采都不会改变另一版本已经确定的碰撞；跨版本效果争用时旧树批次先提交，过程批次后提交，这是短期并存的明确顺序。

新球出生边界不移动，出生重叠仍检查；已有球到期优先于被排除的最后一段运动。碰撞按 fraction、实例ID、request序号、格(y,x)排序，命中位置保留int64格坐标与格内double偏移。命中/到期在本边界提交，最早下一活动边界才交给实例；接连 `await result` 与 `await hit` 仍是两个片段，早到命中由语言运行时缓存。

桥接边界编号只用于通知因果，不等于游戏Tick。暂停激活仅执行新实例的初片段，不推进其他实例或载体；预算等待和Skip不调用Step。Wake只通知当前订阅，重复Wake合并，不唤醒未来订阅。仅最近request保留可观察回执，旧载体仍按正常寿命存在，直到实例结束。

stop/EOF先提交最后片段输出，再移除所属载体、通知及订阅；fault丢弃当前片段输出，之前已提交的世界事实保留。Clear、来源撤销和世界替换调用Cancel并清理所有归属。M3.3在承载者原点离开活动域或失去驻留时直接取消，**尚不支持挂起恢复或跨区域补算**；该策略为下一任务提供明确的迁移边界。

## 上限和观测

桥接合计最多16个运行根，每帧最多8次成功激活。每个过程每活动片最多256 work、8 query、16 outputs；全体过程每阶段最多4096 work和256 outputs。过程活球最多128；旧版继续预留至多256效果，合成发布/事实记录容量512足以覆盖两批。过程宿主每帧最多16次风后端调用，旧树自身的64次风预算独立保留；Clear/世界替换不退款同帧额度。

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
