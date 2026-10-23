# 区域活动与冻结恢复（M4.5）

2026-10-23。设备、process/1、旧树构式及其归属效果随玩家的单个活动窗口冻结和恢复。本页描述会话内状态；后续M4.6已接[磁盘检查点与process/1跨进程恢复](Checkpoints.md)。World 不依赖 Formula、Devices 或渲染；引擎没有改动。

## 时间与活动范围

Session 在接纳流式加载、更新活动窗口后，通过 `SynchronizeReadViews` 先核对设备承载关系，再调用 `FormulaBridge::SynchronizeActivity`，最后发布一个完整提交。这个服务边界也在暂停时执行，且位于环境固定步之前。预算等待期间不改变活动状态或场源，不接纳会改变半步输入的加载；重试仍只完成原来的一步。

- 设备运行所需的完整16格圆盘和2×2设备范围必须在活动窗口内，设备格须为驻留空气。
- 普通过程要求原始激活锚点、当前 continuation 的作用原点在活动窗口且驻留。所有归属场源的完整圆盘，以及所有归属弹体当前圆盘，也必须在活动窗口内；它们的中心须驻留。任一条件不满足，整个过程及其归属效果一同冻结。
- 旧树构式同样按激活锚点和现有弹体冻结，保留 Simulator 的本地时间和排队任务。
- 活动弹体自主飞出允许范围仍沿用终态取消回执；窗口移动使现有弹体离域，则随所属实例冻结。两者分别发生在运动扫描和区域同步边界。
- 视觉加载只改变驻留与显示需求，LLM/原生观测只读已有投影，两者都不扩大模拟窗口。玩家返回并加载必要区域后才恢复。

不根据全局 Tick 差计算欠步。冻结中不调用语言 Step、不扫描弹体、不递减场源寿命、不启动设备新周期；恢复本身也不推进一次。材料沿用 World 原有的活动窗口规则，动态风继续丢弃域外扰动，不承诺精确物理重放。

## 状态与身份

会话内保留原有有界对象，使用显式 `Suspended` 状态区分活动与冻结。无需重新编译、重启解释器或把已经完整存在的语言状态来回复制成存档 DTO。原程序产物、实例 ID、PC、locals、剩余等待、已消费事件水位、待交付事件、请求序号和 credits 全部保留。M3.4 的导出/恢复 DTO 继续为后续磁盘检查点提供合同，本轮不修改语言格式。

Environment 的 `FieldSourceSnapshot` 保留 Handle、Owner、描述、Remaining 和 Suspended。宿主通过 `SuspendFieldSources(owner, ...)` 将整组来源排除出合成、区域唤醒和寿命推进；恢复只改变活动标志，不安装新来源。World 内原生来源离开完整活动域或中心失去驻留也保留身份和剩余寿命；采样、区域唤醒和寿命推进共用活动资格。来源顺序不变化，浮点累加次序不受冻结/恢复次数影响。

冻结仍占用原有容量：最多16个运行根、128个过程弹体、256个场源及64台设备；没有额外无界胶囊集合。`FieldSources::Size()` 包含冻结来源，`HasCapacity()` 不把冻结当作空槽。重复同步为无操作，不增加来源版本；真正切换会增加版本并唤醒相关材料区域，让固定视窗/F1缓存失效。WorldMap另维护块驻留版本，FieldSources按它和来源版本缓存中心驻留；中心资格变化同样增加来源版本，确保邻块投影刷新。无关块的加载不刷新来源版本，逐格采样不重复查询中心驻留。

设备恢复前核对非复用 Device ID、原运行实例 ID、启用状态及所属 Actor ID 的存在性。补煤等操作造成的 DeviceRef 版本变化不替换设备身份。丢失或换成其他身份的承载者取消旧实例与全部来源；天然源/固定夹具尚无独立实体集合，其生命周期由显式 CancelCarrier 端口管理。捕获的程序能力与作用范围不会因恢复而扩大。

## 事件、资源与终止

| 情形 | 行为 |
| --- | --- |
| 设备离域 | 状态为 OutsideActiveArea，保留 Instance、Cycles、剩余周期及库存 |
| 返回且仍合法 | 恢复同一个实例、同一个来源句柄，接续已经支付的煤周期 |
| 已产生的回执/命中/唤醒 | 冻结时保留，返回后的下一次语言边界交付，不重发原请求 |
| 冻结期间新外部 Wake | 忽略，不隐式加载、唤醒活动岛或扣超时 |
| 完整圆盘跨活动边界的即时效果 | process/1 返回 Scope 拒绝；旧树记录 OutOfSimulation，不提交部分作用 |
| 停机、回收、取消、Clear running、世界替换 | 清理活动及冻结的实例、事件、弹体和场源；重复清理无副作用 |
| 恢复时设备被地形占用 | 取消旧周期并显示 WaitingForEnvironment，已消耗煤不退；不重放旧作用 |
| 所属 Actor 消失 | 取消旧周期并停机，保留未用燃料 |

来源仅从下一成功环境步施加作用。原先一煤支付30步的设备程序和停止顺序不变；冻结后只需要完成剩余的活动步，到下一周期才再消耗一煤。已完成的开采、材料移动、资源扣除仍是世界事实，不回滚或重播。暂停中的显式停机、回收、补煤等操作保持原授权规则，并不是后台生产。

F4 的过程实例显示 `frozen` 状态。呈现中隐藏冻结的程序弹体；设备仍有独立已提交状态，显示加载不会让其启动。设备状态变化在安全边界记录 DeviceChanged 事实。

## 验证入口

`Pixel.RegionActivity` 覆盖：完整场盘跨边界、频繁往返和重复同步、保留源码版本/locals句柄、等待安装回执和命中事件、冻结Wake、一次性开采不重播、寿命剩一格步时冻结、跨域效果拒绝、冻结根计入容量、旧树延迟、真实WorldStreamer离开/返回、视觉与观测不激活、设备燃料周期，以及冻结中的停机/删除/承载身份变化/地形阻塞。

`Pixel.Devices`、`Pixel.FieldSources`、`Pixel.FormulaBridge`、`Pixel.FormulaGameplay` 更新旧“离域取消”的预期并验证续接。FieldSources 的预算测试同时检查挂起操作在 pending 环境步中早失败。`Pixel.DeviceRendering` 验证中心块卸载时，相邻固定视窗及直接/已提交 F1 投影立即撤掉贡献，重载后恢复；无关块加载不刷新来源版本。

## 本轮验证与独立审查

2026-10-23，最终代码的实际结果：

- Debug CPU：`cmake --build build-cpu -j4`，`ctest --test-dir build-cpu --output-on-failure -j4`，78/78通过。
- Release、ChunkEdge=64：构建 `PixelRegionActivityTests`、`PixelFieldSourceTests`、`PixelDeviceRenderingTests`，对应CTest 3/3通过。
- Runtime：`cmake --build build --target Pixel PixelRuntimeSmoke -j4` 成功；从 `Pixel/` 运行 `../build/tests/PixelRuntimeSmoke --formula-only` 与 `--debug-ui-only` 均退出0，包含Formula GPU读回、Formula ImGui及设备Debug UI回归。沙箱内首次启动受macOS窗口服务限制未完成；上述结果来自获准的沙箱外运行。
- Release `PixelFieldSourceBenchmark`：0/1/8/32/64/256来源全部完成，[原始CSV](../../Docs/Validation/RegionActivity/field-sources.csv)。256来源的step中位数605.625µs、P95 921.375µs，单点sample中位数0.490601µs；来源容器固定容量22832 bytes。该结果是本机单轮观测，不作为跨版本性能收益结论。
- 独立review发现原生场源中心卸载后仍向邻块贡献，以及相邻已提交视窗/F1缓存未失效。统一活动资格并增加驻留版本缓存后，两份独立复现均确认卸载后受力为0，最新六项CPU专项6/6通过，审查无未决发现。验证agent独立完成上述全量CPU、构建、基准和ChunkEdge=64专项；主agent执行GPU smoke。

未修改Axiom子模块，未执行AET或sanitizer。本轮不覆盖磁盘检查点、跨进程恢复、多活动岛和远处后台生产；这些不是会话内冻结恢复的已实现能力。
