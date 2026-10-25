# 构式语言最小语义选择与可执行对照

日期：2026-10-22。对应 M3.1「比较构式语言方案并确定最小语义」。本轮完成隔离实验及语言方向决定，**没有替换游戏中的 Formula、编辑器或存档格式**。M3.2 的生产产物/运行时、M3.3 的游戏 Bridge 和 M3.4 的保存合同分别继续实施。

## 决定及依据

下一阶段采用**不可变值表达式＋显式过程语句**：表达式构造或计算值，过程组织赋值、条件、循环、等待和宿主作用。保留现有构式可组合描述的价值，把随时间变化的控制状态放入实例。当前 `seq` 仍按原语义并发激活各描述；新过程的书写次序及等待另有明确语义，不能混用。

五场景中，一次挖掘和命中后续已由现有 DSL 完整表达；有限周期也直接可用。库存阈值与一般事件唤醒需要把规则移到 C++ 宿主，持续周期需要宿主重新激活或预先列出有限次重复。过程原型把这三种控制规则放回玩家可描述的程序，同时保留宿主对效果的裁决权。这是选取过程扩展的主要证据；微基准不足以证明它比现有语言全面更快。

弃选/暂缓：

| 方向 | 判断与理由 |
| --- | --- |
| 仅保留现有节点集合，宿主编写每种过程 | 五个场景可由 DSL＋C++ 完成，但库存规则、唤醒与持久控制状态分散在宿主，难以支持玩家修改同一套行为 |
| 不断加入“库存满停机”“周期晶体”等专用节点 | 暂不采用；本实验的赋值、条件、循环与等待已覆盖这些控制需求，专用节点容易隐藏查询及提交时机 |
| 显式过程＋纯值 | 采用为 M3.2 起点；同一实例拥有 PC、局部变量、剩余时间及事件过滤状态，行为边界可逐步观察 |
| 通用脚本虚拟机、协程栈/JIT、完整函数语言 | 本轮未比较其实现或速度；五场景没有证明需要这些能力。先不引入闭包、递归、动态类型或原生函数绑定 |

这不否定用原生 C++ 实现设备状态机；简单固定设备仍可原生执行。也不决定最终文本语法、可视编辑形状或二进制字节码。

## 可复现范围与源码

父仓库基线 `e3ef2a5bafd67053388aa0da3f5a732cf6d550a9`，进入任务时已有 M2.5 未提交成果。本轮仅增加 Formula 隔离实验、可选构建及本文/索引/说明。引擎 `PixelMain` 为 `3676a9f4dfa7f64c056aeb5294b5c681c64411b0`，与 AxiomVersion 一致，无引擎修改。

设计依据为已完整阅读的[架构方案](../../Docs/Architecture/GameplayArchitecturePlan.md)及[游戏总纲](../../Docs/GameDesign/GameDesignVision.md)，旧语言事实参照[独立语言架构](FormulaLanguageArchitecture.md)。实现入口：

| 层 | 本轮实现 | 边界 |
| --- | --- | --- |
| 对照 A | [现有 Parser/Simulator](../../Pixel/Source/Runtime/System/Formula/Source/Simulator.cpp)，解析真实 `.formula` 文本 | 未修改旧实现；库存与 Wake 的 C++ 补充逻辑明确计数 |
| 对照 B 前端 | [Process.h](../../Pixel/Source/Runtime/System/Formula/Experiments/Semantics/Process.h) 的类型化 AST 构造器 | C++ 嵌入式构造入口，可复用不可变表达式；没有新文本 parser，不能用它衡量玩家文本编辑体验 |
| B 编译及运行 | [Process.cpp](../../Pixel/Source/Runtime/System/Formula/Experiments/Semantics/Process.cpp) | AST 一次降为带来源的线性控制指令；纯表达式仍为树。不是承诺最终 bytecode/VM 格式 |
| 宿主及场景 | [Scenarios.cpp](../../Pixel/Source/Runtime/System/Formula/Experiments/Semantics/Scenarios.cpp) | 固定值快照、受控事件和效果接收；不依赖 Axiom、World、UI、网络或真实库存 |
| 驱动 | [Main.cpp](../../Pixel/Source/Runtime/System/Formula/Experiments/Semantics/Main.cpp) | 默认验证，`--benchmark` 输出 CSV；异常直接传播 |

代码受 `PIXEL_FORMULA_BUILD_SEMANTICS_EXPERIMENT=ON` 控制，默认关闭；`PixelFormulaProcessExperiment` 只依赖 C++20 标准库，比较驱动额外链接旧 `PixelFormula`。无游戏目标链接实验。

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B /private/tmp/pixel-formula-m31-release \
  -DCMAKE_BUILD_TYPE=Release -DPIXEL_FORMULA_BUILD_SEMANTICS_EXPERIMENT=ON
cmake --build /private/tmp/pixel-formula-m31-release -j4
ctest --test-dir /private/tmp/pixel-formula-m31-release --output-on-failure
/private/tmp/pixel-formula-m31-release/Experiments/Semantics/PixelFormulaSemantics --benchmark
```

使用同构造器实际编译执行的过程示例，`Inventory` 是查询表达式，`Dig`/`Wind` 是值：

```cpp
Program once({Emit(Dig)});
Program impact({Emit(Bolt), Await(EventKind::Hit), Emit(Dig)});
Program crystal({While(Constant(1), {Emit(Wind), Wait(2)})});
Program stock({While(Less(Inventory(), Constant(3)), {Emit(Dig), Wait(1)})});
Program external({Await(EventKind::Wake), Emit(Wind)});
```

这些是可运行的嵌入式源码，不是新 `.formula` 文本语法。纯值原型只实现有符号 64 位整数、局部读取、加法和小于比较；条件采用 0/非 0，尚无独立布尔静态类型。`Effect` 是带明确参数含义的值，当前仅 Dig/Wind/固定 Bolt。旧语言的修改器、散射和所有嵌套作用域继续由旧回归验证，未声称在新过程原型中完整迁移。M3.2 应先把条件/计时/效果值的类型与单位校验明确化，再决定文本表面语法。

## 五场景及观察到的结果

比较窗口为活动 tick 0–11。fake host 规定每次接受挖掘增加 1 件库存；这是实验规则，**不代表当前游戏已经实现挖掘掉落和库存**。半径单位为格，Wind 参数为速度增量格/秒；Bolt 固定 damage=10、speed=60、lifetime=20。只比较有序效果的 tick、种类、参数与位置，命中/到期日志不冒充效果。

| 场景 | A：现有 DSL / 宿主补充 | B：过程原型 | 明确期望及实际核对 |
| --- | --- | --- | --- |
| 一次性挖掘 | `cast dig(1);` | Emit 后结束 | t=0，Dig(1)，x=0 |
| 命中后续 | `on_hit(bolt(10,60,20),dig(1))` | Emit Bolt，Await Hit，Emit Dig | t=0 发球；宿主 t=3 在 x=7 注入命中；t=4 在 x=7 挖掘 |
| 周期晶体 | `repeat(6,2,wind(2,4))` | While 真，Emit Wind，Wait(2) | t=0/2/4/6/8/10 六次脉冲；B 在窗口结束后仍等待下一次 |
| 库存到阈值停机 | 宿主查询库存 `<3` 才重新 Cast Dig | While Inventory<3，Emit Dig，Wait(1) | t=0/1/2 各挖掘；t=3 看到库存3后结束；各做4次查询 |
| 外部事件唤醒 | C++ 宿主在 t=4 激活 Wind | Await Wake，Emit Wind | t=3 提交事件，t=4 发一次脉冲 |

每个变体分别核对手写期望，不能仅因两者输出相等就判定正确。旧 parser 对 `inventory()`、`await()` 和 `while(...)` 明确拒绝，验证缺失能力不是文案猜测。这里的 C++ Wake 适配器使用轮询，记录5次判断；宿主也可用一次回调完成，这个计数不是旧方案不可消除的成本。

额外长周期用例在1000个活动 tick 内核对500次脉冲：旧 `repeat(500,2,...)` 首次执行后保留499个未来工作，候选只保留一个休眠续点。旧宿主同样可自行维护一个计时器再激活；差别在于持续规则与待保存状态由语言实例还是宿主拥有。

## 选定的最小语义

### 求值、查询与顺序

- 程序发布后只读，实例保存可变状态。表达式从左至右求值；加法溢出是程序陷阱，不能回绕。相同表达式可被多个语句复用；读取局部变量反映当前语句之前的赋值。
- `Assign`、条件及循环按源顺序执行；If/While 降为分支和回跳。纯表达式没有宿主写入能力。当前 `Inventory()` 读取本片固定 `View`，同片发出 Dig 后仍读到旧库存。
- 宿主在运行一批实例前冻结查询截面，按稳定实例顺序收集每个片段的有序意图，全部求值后再提交。程序不能通过重新查询读到自己或另一个实例尚未提交的效果。下一活动步重新获取视图。
- `seq(a,b)` 的旧同刻展开代数不用于新过程语句；`Emit(a); Wait(2); Emit(b)` 明确相隔两个活动步。实验没有据此推导含变量或查询的重排/结合律优化。

### 等待、循环及事件

- `Wait(n)` 要求 n>0；本次到达等待点提交本片，之后第 n 次活动 Step 才继续。调用 Step 表示一次活动机会，宿主每实例每活动步仅调用一次，跳过调用即冻结；不会依全局 tick 补发历史工作。
- While 可持续运行，但每个片段都有工作额度，循环条件与表达式节点也计费；没有 Wait 的无限循环在本片陷阱并停止。首版不加入递归。
- Await 开始订阅**冻结视图事件高水位之后**的匹配事件，之前的事件不重放。事件须在更早 tick 已提交，且匹配种类与请求 token；Hit 恢复时把后续效果原点设为命中位置，Wake 使用 token=0。同一事件不会满足后面的第二次 Await。
- Applied/Rejected 同样能作为类型化宿主事件接收。当前 Await 是单一事件种类过滤；等待结果应直接跟随 Emit，若先跨步再 Await，之前已交付结果会被高水位排除。生产语义还需在 M3.2 定义“等待任一终态回执”及结果分支，避免等待单一结果造成永久悬挂；本原型未实现收件箱历史检索、超时或多路等待。
- 每实例至多一个等待续点。宿主向它传有序且不超过16条的事件批；超过上限或不符合冻结高水位是宿主契约错误，直接抛异常。实例匹配后不修改共享事件列表。实际订阅/队列保留、容量拒绝、承载者路由与撤销仍由 M3.2/M3.3 建立。

### 效果与失败

- `Emit` 只生成有序意图，Wait/Await/Stop 是合法片段边界；越过边界后由宿主裁决并提交。Emit 不等于实际成功，Applied/Rejected 区分宿主结果。实验没有多域事务、真实权限和库存消费。
- 默认每片64工作单位（指令及表达式节点）、8次查询、16个效果；超额清空本片输出并把实例置为 Trapped。之前已提交的事实不撤回，已陷阱实例不重启。终止状态中残余 locals/PC 没有可恢复语义。
- 只捕获本模块显式的工作/查询/效果/整数/序号等程序陷阱。内部无效 PC、宿主批次违约以及分配失败传播；没有 catch-all 返回空效果。
- 构建期限定4个局部槽、256条控制指令、块/表达式深度16；这是受控实验参数。前端由可信 C++ 构造，非法构造抛异常；它不构成可接收任意外部 AST 的生产编译器/沙箱。
- 运行诊断绑定当前指令的 C++ 作者文件和行号；隐式结束指令映射最后一条作者语句，空程序映射构造位置，测试覆盖恰好耗尽预算后的终止陷阱。新文本语言需要自己的 source span，不能沿用这些 C++ 行号给玩家显示。

### 挂起、版本和可调试性

实例值状态包括 PC、4个 locals、已消费活动步数、剩余延迟、下一输出序号、等待种类/token、事件高水位、运行阶段、当前效果原点。本机 `sizeof(State)=96`；无原生协程栈、指针或程序副本。运行片段还会分配意图 vector，程序含表达式节点和源位置字符串，所以96字节不是整个运行器或峰值内存。

测试在休眠、循环局部变量已修改及等待事件时复制状态，使用同一个 Program 续跑；跨1000个全局 tick 的冻结不会补时，也不重放之前的发射/挖掘。独立新 Program 不影响仍使用旧 Program 的实例。**这只是内存中的状态充分性实验**，没有序列化格式、语义版本号、内容身份验证和旧档兼容。M3.2 建立 ProgramArtifact/InstanceState 绑定，M3.4 再定义持久 DTO 与恢复核验；不得把裸内存复制当作磁盘存档格式。

调试器可以在每个挂起点展示 PC 对应来源、locals、剩余时间与等待过滤器，陷阱也能定位；本轮仅测试这些值，没有新增 UI。持续 FieldSource/子实例/订阅的归属与终止清理依旧由后续任务实现，本实验没有此类资源，不能声称已验证持续效果撤销。

## 成本实测

环境：macOS 27.0.1、arm64、AppleClang 21.0.0.21000334，独立 Release（CMake 默认 Release 优化）。每场景/变体预编译一次，每轮先100次预热，再1000次单独计时，保存3轮。计时包含新建模拟器/状态、12步 fake host 驱动、效果日志和分配，排除 Parse/Lower、结果期望检查与 CSV I/O。完整数据见[原始CSV](FormulaSemanticsBenchmark-2026-10-22.csv)。

| 场景 | A P95 μs（三轮范围） | B P95 μs（三轮范围） | A/B 峰值待办数 | B 工作单位 |
| --- | ---: | ---: | ---: | ---: |
| Dig | 0.167–0.167 | 0.125–0.125 | 0 / 0 | 2 |
| Hit | 0.417–0.459 | 0.208–0.209 | 2 / 1 | 4 |
| Periodic | 0.791–0.833 | 0.500–0.500 | 5 / 1 | 29 |
| InventoryStop | 0.292–0.333 | 0.292–0.334 | 0 / 1 | 26 |
| Wake | 0.167–0.209 | 0.167–0.167 | 0 / 1 | 3 |

A 的待办包括到期/激活队列，B 是休眠/事件续点；表示不同，不能当作等大小对象。旧执行器没有公开节点工作计数和堆内存统计，CSV 明记 unavailable，不能把0或对象指针大小充当其内存。没有测量全分配次数、峰值 RSS、前端成本和游戏预算；新 B 的 Bolt 也没有旧 A 的活动弹体表/解析运动。因此不推导“新 VM 加速几倍”，更不据此引入字节码优化。

可支持的结论是：五个小场景中候选的完整驱动处于亚微秒级，库存循环和旧宿主方案接近；持续循环可以维持一个续点而无需预展开所有未来动作。语言归属与状态可解释性支持当前方向，真实设备数量/持续作用/游戏宿主的性能另在后续阶段测量。

## 验证与后续交接

已运行的独立 Debug、Release 以及 ASan/UBSan 构建各注册3项 CTest（Semantics、旧 Formula、旧 Demo）；过程测试核对五场景和长周期，以及源顺序、固定查询、循环局部、无等待循环、输出/查询/整数上限、同 tick 事件隔离、token、重复事件、拒绝结果、冻结恢复、独立程序、零 Wait 和内部错误传播。独立验证 subagent 执行 Debug 与 sanitizer；最终审查和最终重测记录见本任务日志。

独立 review 发现1项P2：源码恰好消费64个工作单位时，隐式Stop的预算陷阱指向编译器实现位置。已修复来源映射，新增精确文件/行号及空程序零预算测试；原审查者复核关闭，无未决意见。修复后独立Debug、sanitizer与review构建各3/3通过，主agent执行最终Release验证。

根 CPU 相关回归：`cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`，构建 PixelFormulaTests/PixelFormulaValidationTests/PixelFormulaEditingTests/PixelFormulaGameplayTests；`ctest --test-dir build-cpu -R '^Pixel\.Formula(Validation|Editing|Gameplay)?$' --output-on-failure`，4/4通过。没有全量 CPU、GPU、AET 或真实编辑器交互的新验证，因为本轮没有修改这些运行路径。

M3.2 接续时：

1. 采用此过程/快照/缓冲提交方向，明确条件及效果的类型，形成 ProgramArtifact 身份、语义版本、实例与产物绑定和源位置诊断；仍可选择树解释或简单控制计划，不要求字节码。
2. 把回执等待升级为可判别的完整终态结果；规定宿主拒绝、订阅容量/结束、命中未发生/载体到期的后续行为，不能让等待只处理成功。
3. 按旧修改器/时序/命中作用域与实验合同迁移代表样例；任何语义变化明确标版本，允许一次破坏式迁移，不永久包装旧 AST。
4. 游戏作用、碰撞、稳定实例排序、权限/资源与取消在 Bridge 中接入；持续作用和保存继续按各自叶子任务验收。

本轮完成后按用户要求暂停，不启动 M3.2。
