# 构式过程语言 v1 与有界运行时

> 2026-10-26 更新：默认程序与编辑器已支持 formula/2 函数组合和 World/Actor 绑定；process/1 仍保持原语义。新函数、事件作用域、状态格式3及宿主格式2兼容策略以[当前实现合同](../../Docs/Architecture/FormulaComposition.md)为准。下文保留过程执行核心和历史验证记录，不代表最新功能范围。

日期：2026-10-22。对应 M3.2「建立独立程序产物与有界执行运行时」。这是 [M3.1 语义对照](FormulaSemanticsExperiment.md) 选定的纯值与显式过程方案的独立实现；M3.3 已通过 [FormulaBridge](FormulaBridge.md) 接入游戏，旧 `cast` 编辑器短期并存。本文描述独立语言合同，游戏范围与额度以桥接文档为准。

## 产物、状态和模块

| 层 | 实际入口 | 职责 |
|---|---|---|
| 语言 | `Pixel/Source/Runtime/System/Formula/Source/Language/ProcessCompiler.cpp` | 文本词法、作用域、类型和能力检查，编译控制指令与有界表达式树 |
| 产物 | `Include/Formula/Process/Program.h`、`Source/Language/ProcessArtifact.cpp` | 私有 `shared_ptr<const ArtifactData>`、原始源码及源映射、程序身份 |
| 执行 | `Include/Formula/Process/Runtime.h`、`Source/Runtime/ProcessInstance.cpp` | 绑定产物的实例、局部值、PC、活动计时、等待状态、片段预算 |
| 宿主合同 | `Include/Formula/Process/HostContract.h` | 冻结查询、效果请求、回执、命中、唤醒、终止清理 |
| 工具 | `Tools/ProcessFakeHost.*`、`Tools/ProcessMain.cpp` | 有界 fake host 操作状态、显式注入事件、CLI 文件 I/O 与测试轨迹 |

`PixelFormulaProcess` 只依赖 C++20 标准库。`PixelFormulaFakeHost` 链接它，`PixelFormulaProcessDemo` 和 `PixelFormulaProcessTests` 使用 fake host；语言和运行时不依赖 Axiom、World、ImGui、旧 Formula 或文件系统。游戏专用效果不进入引擎。

`Compile(resource, source, availableCapabilities)` 返回不可变 `ProgramArtifact` 或带资源名、字节 offset/length、1-based 行列的 `Diagnostic`。身份由资源标识、源文本 SHA-256、语义版本 1、宿主合同版本 1 组成。空白/注释改变也形成新内容版本。哈希用于内容识别，不用于授权。产物保留源码，运行阶段只读编译好的指令/表达式，不解析、哈希或按资源名查找新版本。

`Instance::Start(artifact, id, grants, limits, origin)` 返回实例或能力拒绝。实例持有自己的产物引用；同名资源重新编译不会改变旧实例及其 continuation。`InstanceState` 只通过 const 视图检查。M3.4 已增加程序记录与运行状态 DTO、严格按原产物恢复、fake host 挂起及源码编辑入口，见[状态与编辑合同](FormulaStateAndEditing.md)；尚无磁盘编码或跨版本迁移。

## 文本与值

文件以 `process 1;` 开始。项目仅保留这个入口；旧cast语言及其运行时已删除，旧源码编译失败，不提供转换。

```text
process 1;
let carrier = damage_mul(damage_add(bolt(10, speed(60), ticks(20)), 5), 2);
emit carrier;
await result within ticks(3);
if outcome() == applied {
    await hit within ticks(30);
    if outcome() == hit {
        emit dig(cells(1));
    }
} else {
    stop;
}
```

语句包括 `let name = expr;`、`var name = expr;`、`name = expr;`、`if expr { ... } else { ... }`、`while expr { ... }`、`emit expr;`、`wait expr;`、`await result|hit|wake within expr;`、`stop;`。文件结束也是显式编译的终止指令，源位置为实际 EOF。

- `let` 不可赋值，`var` 可修改但类型固定；块内声明不可向块外泄漏，不允许同名遮蔽。循环内声明每次执行重新赋值，不创建更多槽位。
- 标识符使用 ASCII 字母/下划线开头及字母/数字/下划线，区分大小写。支持 `//` 注释。保留字和内建函数名不可用作绑定。
- `bool`、有符号 64 位整数、有限 `double`、格距离、速度、活动 tick 时长、效果描述和 `Outcome` 是不同的类型。条件只接受 bool，不做真假数值转换。
- 十进制整数与带小数点的十进制数分别产生整数/number；不支持指数写法、字符串、导入或隐式 int/double 二元混算。负号是一元操作。`+`、`<` 要求同类数字；`==` 支持同类数字、bool、Outcome；`!` 只接受 bool。比较的优先级低于加法。
- `cells(number)` 产生非负格距离；`speed(number)` 是有符号格/秒；`ticks(integer)` 要求正活动 tick 数。构造器在动态求值时校验数值；编译时检查类型，负距离/零时长等返回运行诊断。整数溢出与非有限浮点结果终止当前片段。
- `dig(cells)`、`wind(cells, speedX, speedY)`、`bolt(damage, speedX, ticks)` 返回纯效果值，构造本身不执行世界写入。`damage_add(effect, number)`、`damage_mul(effect, number)`、`speed_mul(effect, number)` 返回修改后的新值，不改变原绑定；v1 仅支持 bolt 修改，错误效果种类在求值时终止。伤害必须非负；有符号速度可反向。
- `inventory()` 读取本片冻结的整数库存；`outcome()` 返回最近一次 await 的结果。两者都不修改宿主。初始 outcome 是内部 `None`，无对应源码字面量。
- 无用户函数、递归定义、外部调用或宿主对象句柄。用户递归被编译拒绝；解释表达式的 C++ 递归受编译深度约束。没有 JIT、通用插件注册表或隐含异步任务创建。

## 片段、时间与效果提交

`Step(HostView, events)` 从当前 PC 执行到 `wait`、`await`、`stop`、EOF 或运行额度/数值错误。每个等待都会结束当前片段，即使所等的回执已缓存，也在下一活动边界继续。`wait ticks(2)` 在第 0 次调用让出，在第 2 次后续活动调用恢复；`await ... within ticks(2)` 同样在两次后续活动调用后超时，截止边界已有合法事件时事件优先。

`HostView.At` 是严格递增的宿主提交边界编号，非经过时间差。未调用 Step 就不推进实例计时；不活跃区域恢复时跳过的宿主边界不会补跑等待。fake host 的 `Skip` 同时冻结载体寿命。当前宿主选择每次活动 Step 对应 1/60 秒游戏固定步，但库不调用墙钟。

每个宿主阶段先冻结查询、稳定顺序执行全部实例，之后才按实例顺序和源码顺序处理返回请求。`StepResult` 不是宿主成功回执；宿主仍负责资源、权限、几何上限与费用校验。拒绝通过带 RequestId 的事件回报，不能把某个实例刚写入的库存作为同阶段其他实例的查询。

位置保存 `int64 CellX/CellY` 和 `[0,1)` 的双精度格内偏移，不把远处整数格坐标转成 double。命中恢复时将该实例后续发射原点改为准确命中位置；伤害和速度修改规则不传入命中后的新效果表达式。当前只支持 X 方向 bolt 速度，真实方向、碰撞和世界适配属于游戏接入。

## 回执与订阅

每次 `emit` 产生 `(InstanceId, monotonically increasing sequence)`。v1 只允许等待**最近一次 emit**；再次 emit 永久替换可寻址 token，不提供 fan 多载体句柄表或并行等待。已应用的旧效果与载体仍归实例所有，直到各自到期或实例清理。

| 等待种类 | 可恢复结果 |
|---|---|
| `await result` | `applied`、`rejected`、`cancelled`、`timeout` |
| `await hit` | `hit`、`expired`、`rejected`、`cancelled`、`timeout` |
| `await wake` | `wake`、`rejected`、`cancelled`、`timeout` |

没有先前 request 的 result/hit 等待返回运行诊断。宿主可以因为资源不足、能力撤回等动态条件拒绝请求；Wake 订阅容量不足或失效也应返回 `rejected` / `cancelled`，不能只等待成功路径。所有 await 必须写有限的正超时，缺少事件不会永久卡住。超时本身不撤销已发出的效果，程序可选择停止或后续继续等待；实例终止才统一清理。

运行时为最近 request 保留一个 admission 回执和一个 carrier 终态缓存；普通 wait 期间的早到 Applied/Hit 不丢失，先 await result 再 await hit 可以分别消费。每类终态只消费一次，新序号重复回报同类终态也不会重复触发。订阅使用独立递增 generation，旧 wake 不会唤醒新 await。Wake 订阅从 await 语句提交后生效，不接收过去未订阅的 wake。

宿主事件序号按目标实例严格单调，每个批次按序交付；已消费的序号可重放但被忽略。其他实例、旧 request、旧订阅的通知不影响当前 continuation。每个事件的 `CommittedAt` 必须早于当前 `HostView.At`，不允许同片递归消费刚提交的事实。宿主可过滤永久不可寻址 token 的通知，但不能丢弃当前 token 在普通等待期间的早回执。

## 额度、终止与错误边界

编译上限：源码 16384 字节、资源名 256 字节、标识符 64 字节、全程序 16 个 local 槽、512 条指令（包含隐式 EOF）、1024 个表达式节点、表达式/块深度 24。超出即源码诊断，不展开无限循环。控制流循环反复执行有限的编译指令。

每片默认 256 work、8 query、16 outputs；可配置上限分别为 4096、32、64，work 必须正数，query/output 可为 0。每条执行指令及每次表达式节点求值各计 1 work，循环回跳和终止也计费；每次 inventory 调用再计 1 query。成功结果使用固定 64 项输出数组。每次事件批次最多 32 项，超过作为运行额度错误终止；实例只保留固定 16 个值和最多三个事件槽，不保存动态未来任务队列。

额度、数值错误、查询不可用或非法动态参数使当前片段 fault：全部未提交请求丢弃，实例终止并输出源码诊断。前一片段已经应用的挖掘/风等事实不回滚。本片此前已修改的 local 不再使用，因为实例不可恢复执行。

宿主收到一次性 `Termination` 后按以下顺序执行：

1. 对正常 stop/EOF，先处理同结果中的最后一片请求；fault/cancel 没有新请求。
2. 移除该实例拥有的载体、持续效果、订阅和待办工作；已提交的一次性世界事实保留。
3. 释放实例；后续 Step/Cancel 不重复输出清理通知。外部移除运行中的实例必须显式调用 `Cancel` 并消费其结果，C++ 析构不能代替宿主清理。

因此 `emit bolt(...);` 立即 EOF 会提交后立刻清理该 bolt。要保持载体存活，程序必须继续 wait/await；v1 无脱离实例的持久化所有权。当前语言只实现 dig/wind 一次性事实和 bolt 载体，持续场效果尚未实现；未来效果也必须遵守此清理合同。

编译与运行领域错误用具体结果返回；不捕获内部 `std::exception` 来假装继续运行。非法宿主 ID、限制、非规范位置、倒退边界或同边界事件等编程错误直接抛异常。内存分配失败也直接传播。额度不构成对进程总内存或多实例调度的完整安全沙箱，实例数量、全局公平调度和宿主操作代价仍由宿主控制。

## 独立 CLI 和验证

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B build-formula -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula -j4
ctest --test-dir build-formula --output-on-failure
./build-formula/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Process/hit.formula --steps 12 --hit-at 3
./build-formula/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Process/receipts.formula --reject
./build-formula/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Process/wake.formula --wake-at 3
```

CLI 默认执行 12 个活动边界；`--steps` 最多 100000，`--hit-at`/`--wake-at` 为提交事件的边界，最早下一边界可见。命中注入当时全部载体到 `(7,0)`，没有真实轨迹/碰撞。fake host 每次接受 dig 增加一单位库存，接受 wind 记录事实，bolt 最多 32 个，满额返回 rejected；按活动步到期产生 expired。它只排入可观察 token 的通知，同订阅的重复唤醒合并；操作队列硬上限 128，正常回执/终态不随输出历史增长。完整 Trace 为测试观察记录，按运行长度增长，不作为运行时待办或生产日志设计。

`UnitTest/Formula/ProcessTests.cpp` 覆盖五场景黄金轨迹、1000步/500次脉冲、每片64输出连续100片、类型与语法拒绝、递归拒绝、编译容量、工作/查询/输出/事件预算、精确额度边界、旧实例隔离、修改器顺序与载荷隔离、源码定位、早回执、所有等待终态、超时优先级、重复及旧通知、活动时间冻结、正常/fault/cancel清理、固定快照和4000项确定性非法文本语料。两个额外 CLI CTest 检查实际命中和拒绝路径。

## 与旧 DSL 的兼容范围

> 下表为历史设计对照，旧实现及其构建目标已删除。

| 主题 | 旧 `PixelFormula` / `cast` | 新 `PixelFormulaProcess` / `process 1` |
|---|---|---|
| 游戏使用 | 当前游戏和编辑器继续使用 | M3.3经FormulaBridge接入，三承载者示例 |
| 值组合 | `seq/fan/repeat/delay/on_hit` 描述树与管道 | 显式 emit、变量、条件、循环、wait/await，无自动转换 |
| 修改器 | 参数先于效果；不适用保持效果并诊断；负伤害钳到0 | 效果先于数值；不适用或负伤害终止片段；不钳制 |
| 时间 | 可零延迟/零重复间隔，预展开计划任务 | 正时长等待，有限 continuation，每片额度 |
| 命中 | carrier 自动注册 payload，载荷独立排队 | 等待最后request的终态，命中位置进入后续过程，失败分支显式 |
| 所有权 | 已排队子构式可独立继续 | 所有载体/待办从属于实例，结束统一清理 |
| 错误 | 原型异常可能已有部分效果 | 新运行时片段输出原子丢弃，宿主已经完成的事实保留 |

语言本体暂不迁移 fan、混合描述闭包、任意方向、编辑器节点和存档；M3.3游戏适配将前向速度按承载者朝向旋转，并接入领域效果。新文本错误位置来自新文本，不复用 M3.1 实验的 C++ source_location。M3.1 实验保留作历史证据，可选构建；新测试为独立行为合同，不把旧实验通过当作当前实现验证。

## 本轮验证与审查记录

2026-10-22，最终源码在独立构建中通过以下检查：

| 配置 | 命令/目录 | 结果 |
|---|---|---|
| Debug | `cmake -S Pixel/Source/Runtime/System/Formula -B /private/tmp/pixel-process-verify -DCMAKE_BUILD_TYPE=Debug`，build 与 CTest | 5/5 |
| Release（独立验证 agent） | `/private/tmp/pixel-process-release`，build 与 CTest | 5/5 |
| ASan/UBSan（独立验证 agent） | `/private/tmp/pixel-process-asan-ubsan`，`-Wall -Wextra -Wpedantic`，build 与 CTest | 5/5，无编译警告/检测报告 |
| 根项目 Debug | `cmake --build build-cpu --target PixelFormulaProcessTests PixelFormulaProcessDemo -j4`；`ctest --test-dir build-cpu -R '^Pixel\.Formula($|\.|Validation$|Editing$|Gameplay$)' --output-on-failure` | 8/8 |
| 内容哈希 | CLI 输出对照 Python hashlib，10、65、16384 字节实际源文件 | 全部一致 |
| 独立审查 | `/private/tmp/pixel-process-review` 构建与 CTest、额外容量/早回执/所有权复现 | 5/5，修复后无未决发现 |

审查发现一项 P2：合法每片64次输出使 fake host 旧回执积压，第四片出现部分宿主提交后容量异常。修复为只保留当前可寻址 request/订阅通知，全部事实和旧载体保持原有生命周期，并合并同订阅重复通知。提交了64输出×100片及256次重复wake回归；审查者用原复现重新链接验证10片640事实、pending恒为1，并额外验证早Applied/Hit、旧载体保留、32载体容量拒绝与Cancel清理，确认关闭。

审查者还对三个有效程序做60000次插入/删除/替换变异，成功编译者执行至多10活动步，ASan/UBSan无报告。该额外临时harness不作为仓库测试入口；可维护回归是当前 `ProcessTests.cpp`。本轮未运行窗口/GPU验证，未修改引擎或游戏运行逻辑；验证范围止于独立新语言及现有CPU适配回归。
