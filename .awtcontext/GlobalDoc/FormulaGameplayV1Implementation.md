# 构式基础版游戏接入实现

日期：2026-10-20。依据：[已评审设计与实施计划](FormulaGameplayV1Design.md)。实现、验证及代码审查证据归属 AWT 任务“实现构式基础版游戏与编辑器”；设计阶段证据仍归属原设计任务。

## 使用方式

- 右键从人物前方朝鼠标激活已应用构式；Shift＋右键开采，左键放Dirt。普通模式和自由相机模式都沿用这一规则。
- 默认构式是不可变 Bolt 描述。先求值完整构式再产生运行实例：Empty不产生球，Delay在计划tick前没有占位球。默认球为不透明黄色，半径0.35格、速度60格/秒、寿命120tick；伤害为元数据。
- 默认暂停时，右键能生成当前tick的球；Environment取消暂停或单步后继续运动。F1只隐藏球表现，F2控制Pixel Debug，F4独立控制Formula Editor，两个窗口互不影响。
- 独立 `Formula Editor` 通过树选择节点，在参数栏修改数值、选择新节点类型，执行包裹、解包、增删、复制和重排。默认窗口适配800×600窗口，可关闭后从Pixel Debug重新打开。
- 预设只改草稿，Apply安装新的不可变版本；已发射的实例及其延迟/命中载荷保留旧版本。Revert恢复已应用结构；Clear running无触发清除所有实例、队列和诊断。
- 配方、草稿和运行实例不持久化。世界存档保存实际材料变化、风配置与时钟；不保存动态风扰动。

## 代码职责

| 模块/文件 | 实际责任 |
|---|---|
| `Pixel/Formula` | 不可变描述的Kind/Values/Child只读接口；Simulator保留原语义并新增DrainTrace、PendingCount、IsIdle和无触发DiscardProjectile |
| `FormulaEditing/FormulaDraft.*` | 稳定ID可变树、结构操作、参数/形状校验、构造不可变Formula及所有命中后代工作量分析；纯CPU域库 |
| `Gameplay/FormulaRuntime.*` | 每根独立Simulator、局部Anchor、Epoch/Cast/Projectile身份、旧版本保留、统一碰撞后效果提交、增量事件和跨帧额度 |
| `Gameplay/FormulaWorldAdapter.*` | 扫掠圆的格面/格角精确判定、未知域退场、Mineable圆形挖掘、受控Coarse风脉冲 |
| `Gameplay/GameSession.*` | 已应用构式访问、前方瞄准与堵枪口判断、成功固定步接线、世界替换清理和v4存档 |
| `World/Environment`、`World.cpp` | 保留原固定步累加器及预推进重试；成功步内更新角色并通知Session；等待时冻结模拟窗口和风域 |
| `Camera/GameCamera.*`、`Input/GameInput.cpp` | 事件尺寸下的连续世界瞄准、Shift分流和清空锁存输入 |
| `PixelRender/FormulaFrame.*`、`WorldGridFrame.*` | 帧首独立圆形数组，精确世界/相机差后转屏幕float；两个publisher不共享可变数据 |
| `PixelRender.cpp`、`Pixel2DFormula.Frag.slang` | world→wind→formula→character→post→UI；一个pass复制底图后小viewport/scissor绘圆；frame-slot私有目标和descriptor |
| `Debug/FormulaDebugUI.*` | 独立树形窗口、预设、Apply、结构参数错误和运行诊断；不直接改Simulator |

依赖为 `PixelGameplay → PixelWorld + PixelFormulaEditing → PixelFormula`。其中Editing包含纯CPU分析，不含UI；World不依赖Formula或渲染。Axiom子模块和AxiomVersion没有本轮修改，相邻 `../Axiom` 未写入或构建。

## 时钟、存档和边界

Session按每次成功世界固定步推进时间及构式，角色也在该步内更新。零步帧不丢跳跃，多步帧只首步消费跳跃；预算失败步不推进角色/构式，保留同一预推进风场重试。等待期间拒绝新激活、鼠标地形编辑及当前风配置重建；存储轮询、保存、单步、完整世界重置与纯草稿操作保留。

单步优先消费已有完整积压，只提交一步。v4元数据允许未预推进风的完整backlog，deferred仍只代表下一步风已推进但材料未提交；读入旧v2/v3时保留旧时钟约束，v3/v4均读取世界生成描述。成功1步、角色跨中心阈值、后一步预算失败时，publisher/UpdateStreaming也不能迁移风域；成功重试后恢复迁移。

球按闭集扫掠与固体接触，切线和静止重叠计入；候选格保守覆盖精确判定的epsilon。四向格面与模拟域边界、圆角非接触均有回归。命中使用球心接触位置，载荷下一成功tick开始；到期优先，寿命末tick不扫最后一段。新生成球本tick只做重叠检查，不提前移动。

同tick所有球先看同一份已提交材料，再按稳定顺序提交效果。Dig按格中心圆距离，radius=0只处理中心格，开采Mineable而不删除水；缺失块和模拟窗口外不写入。未知域无触发退场，不伪造Hit。

所有运行位置保存在固定WorldPosition Anchor相对坐标中；风域转换先做整数原点差，再转float，真正域内点夹到`std::nextafter(float(extent), 0.0f)`。半径0或低于1/256格只给诊断，不取消同根其他效果。

## 基础版限制与统计

草稿结构操作在分配前限制64节点、16层；拒绝增长保持原树、选择和ID序列，仍可删除/解包恢复。Compile继续检查直接构造的非法草稿；导入描述在递归复制前迭代检查形状。

每根最多64个基本效果、4096次求值访问、3600tick结束上界；参数count≤16、delay/interval≤600、寿命1～600、半径≤16、实际速度及风强度≤600。会话最多16根，预留效果总量≤256，一帧接受≤8次激活。全部后代都计入分析，包含多个OnHit及嵌套payload；原型CLI不采用这些游戏额度。

实际风效果只适配启用的Coarse，网格≤1024。其余后端/域外有明确跳过诊断，其他效果继续。所有根最坏风效果预留和本帧退休预留合计≤64，退休、Clear、重置均不能在同帧退额度；下一FrameBegin退还。每帧新增脉冲扫描≤65536网格迭代，不承诺完整帧必达60FPS。

窗口统计根/球/队列/预留和最近256条事件。CPU内存显示快照容量、诊断条目及字符串估计；不包含Simulator内部队列、共享描述和分配器开销，不是完整运行时heap或RSS。表现层另提供圆数组容量及每frame-slot RGBA8目标字节数，不纳入世界CPU512MiB预算。

## 验证与审查

最终修订后独立CPU验证：根CTest53/53、独立Formula2/2、ChunkEdge64的FormulaGameplay/GenerationSession/WindDomains/Chunks/Persistence共5/5通过。Pixel、AET和RuntimeSmoke构建及新shader编译成功。真实Apple M5构式专项与完整GPU/UI smoke通过，含两轮resize/minimize生命周期；桌面运行需要正常窗口服务，沙箱内初次窗口初始化未完成后已终止，正式结果来自桌面服务运行。

独立Astra（干净上下文，xhigh）代码审查确认两项P2：Sweep/Dig候选边界漏格；预算等待期间窗口迁移改动预推进风域。两项已修订并补回归，主方自查的草稿无界增长也已限制。独立Astra最终复核关闭全部意见，没有未决发现；修订后完整CPU、独立Formula、ChunkEdge64专项及完整GPU/UI smoke再次全部通过。原始及最终审查、CPU报告、运行时记录分别保存于该AWT任务Doc。

快捷键后续调整（同日用户请求）：F2仅控制Pixel Debug，F4独立控制Formula Editor。独立代理复查无发现，重新构建PixelInputTests且Pixel.Input 1/1通过；Pixel/PixelRuntimeSmoke构建和真实`--formula-only`退出0，覆盖两个窗口显隐的四种组合，以及普通面板隐藏时的真实编辑、Apply与输入捕获。本次小改动没有重跑全部CPU/完整smoke；上面的全量结果归属基础版交付。

常用验证入口：

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

cmake -S Pixel/Formula -B build-formula -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula -j4
ctest --test-dir build-formula --output-on-failure

cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --formula-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
```

尚无伤害结算、费用、装备槽、追踪/反弹/穿透、配方存档或其他风求解器的构式脉冲适配。A1～A20设计矩阵由新增FormulaEditing/FormulaGameplay/FormulaRendering、既有Input/Session/Persistence/GenerationSession及实际Runtime/UI测试共同覆盖；具体覆盖和执行范围以AWT验证记录为准，不将静态计划视为测试通过。
