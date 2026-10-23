# 设备放置、端口与最小工作状态（M4.3）

本轮实现一类风发射器，构成“开采物料 → 放置 → 供煤 → 启动 → Formula 风脉冲 → 停机/回收”的最小路径。设备属于 Pixel，既不编码进 CellType，也不进入 Axiom。当前设备记录仅在会话内存中存在；M4.4 接持续场源，M4.5 接区域暂停/恢复，M4.6 接跨领域保存，M4.7 接完整库存/设备 UI 与 LLMView 投影。

## 领域与身份

- `Devices` 是标准库与 Items 值类型上的有界集合，最多64台。每台有单调不复用的 ID、版本、玩家 ActorId 所有权、整数格坐标、启用状态、运行实例、脉冲次数和煤数量。
- `DeviceRef` 是 ID+版本；外部动作还受世界 epoch 与调用者会话约束。放置、补煤、启停和回收由 `GameSession` 在完整提交边界编排；原生 Input/Debug/Driver 均代表本会话玩家，payload不能指定所有者。
- `Items::Inventory` 持有燃料容器，设备仅保存容器身份与提交摘要。燃料槽容量64，仅接收煤。回收后销毁空容器，不累积废弃槽；世界替换统一清实例、设备与库存。
- 设备是2×2格非实体设施，不提供角色碰撞，也不改变材料模拟的运动规则。Construction 拒绝与地形、角色或其他设备重叠的放置，拒绝在已占用格放土。流动材料仍可能进入设备范围；下一成功步检测到非空气会取消工作实例并报告环境等待。

## 动作与原子性

`PlaceDevice` 花费4石头+2铜矿。先检查2×2驻留空气、角色/设备占用、设备/容器上限，再预留整批物料，建立燃料容器与设备，消费预留。缺料、占用、未驻留、无玩家权限和容量上限返回明确拒绝，不生成实体或扣料。预检后违反内部不变量直接失败，不用恢复分支隐藏程序错误。

`SupplyDevice` 向拥有的设备整批转移1–64煤，检查版本与容量。`SetDeviceEnabled` 只改变开关；工作只在成功固定步中推进。`RemoveDevice` 在检查背包可容纳建造成本及所有剩余煤后，取消设备承载者、转回燃料、退建造成本、销毁空容器及设备。背包不够时全部保留，不能只回收一部分。已消费的煤不退。

动作沿用最近64回执去重；同一请求重复不会重复扣料/回收，新的请求使用过期DeviceRef会返回PreconditionFailed。停机与移除按Device承载者种类+设备ID取消，不能误删相同数字ID的玩家/天然源/测试夹具。通用夹具的Start/Cancel/Wake端口不能冒充Device。

## 运行合同

每台设备安装独立不可变的 `device/wind-emitter/1` 程序（ProgramRef资源3、版本1）：

```text
process 1; emit wind(cells(16), speed(60), speed(0)); wait ticks(30);
```

输出圆心为设备格框中心，半径16格，向右速度增量60格/秒。一份煤支付一次脉冲及其30成功步等待周期；同一设备最多一个运行实例。只授予Wind能力、一次效果额度和16格半径限制。玩家Apply其他程序不会更换已安装设备程序。

需要启用Coarse风后端、网格不超过1024单元、设备范围为空气、输出圆心在风域内，完整输出圆在当前模拟窗口中。非零风分量必须覆盖至少一个粗网格MAC面样本；过粗网格或采样空隙返回OutputUnresolved，不消耗煤。覆盖查询与实际AddImpulse共用权重核；设备测试额外对照无设备会话检查实际风速差。正常状态包括Stopped、Running、NeedsFuel、WaitingForEnvironment、OutsideActiveArea、WaitingForCapacity、OutputUnresolved。启用后等待下一个成功步尝试启动；帧激活上限8、运行根上限16，过程风脉冲每帧上限16。被容量拒绝不消耗煤。暂停/预算等待不推进设备。

环境或模拟窗口不适合时取消当前实例，保持开关与剩余煤；重新满足条件后需要新的煤启动新周期，不恢复原等待。当前不是M4.5的区域checkpoint。停机取消剩余等待，不撤销已提交的风扰动。`Clear running` 同时关闭设备，避免下一步自动重启。

## 事实与呈现

动作回执和领域事实包含拥有值的DeviceChanged（placed/removed/supplied/enabled/state_changed），包含DeviceRef、Owner、坐标、燃料、状态和实例身份。主动动作原因是RequestKey；成功步中的状态事实原因是DeviceCause。实际风脉冲仍记录WindPulseSubmitted，以资源3+实例身份关联设备，不伪称测得了风速变化。LLMView既有全域事实权限可读取这些事实；设备topic仍留M4.7。

`CommittedWorldView` 保存独立设备摘要，`DeviceFrame` 仅按已提交值生成呈现几何。颜色区分状态，右侧小矩形标出输出端口。远坐标先相对整数参考再转浮点。渲染复用现有合成目标，顺序为世界→风→设备→构式球→人物→后处理→UI；F1隐藏设备图形，数据仍保留。CPU快照纳入发布预留，几何内存单独统计；没有新增GPU目标。

B切换设备布置工具，左键放置；设备范围内右键启停、Shift+右键回收。F2 Devices可输入坐标放置、选择设备、补1煤、Enable/Disable、Recycle，显示建造成本、燃料、状态及操作拒绝。布置模式有常驻操作提示。测试夹具可直接给库存注入资源；正常游戏仍需开采。

默认风后端Empty且出生高度通常在风域外。验证工作时在F2选Coarse，并将Wind boundary Y调到设备中心下方（例如平地设备520,510，设600）；启动后取消暂停或单步。默认暂停只允许配置和建造，不产生脉冲。

## 验证入口

- `Pixel.Devices`：缺料/占用/未驻留/权限拒绝、占用不编码地形、过期引用、重复动作、整批燃料转移、背包满回收拒绝、完整回收/容器释放、工作周期、真实风速差/未覆盖采样拒绝、每帧风额度竞争、区域退出不重放、玩家程序隔离、设备归属与事实、停机/销毁/清空和世界替换。
- `Pixel.DeviceRendering`：远坐标精度、状态颜色、独立发布、移除与F1；`Pixel.Input`：B、非重复按键及设备鼠标路由。
- `PixelRuntimeSmoke`：设备body/port真实GPU颜色读回与状态变化、frame slots、F1和移除；F2真实点击放置/补煤/启停/回收，确认没有输入穿透。

2026-10-23，独立验证Subagent在最终代码执行 `cmake --build build-cpu -j6`、`ctest --test-dir build-cpu --output-on-failure -j4`，75/75通过（48.54秒）。设备测试的零时长单步、有限地图忽略模拟窗口两个夹具问题已修正，没有修改生产时钟或弱化预期。

Apple M5/macOS上 `cmake --build build --target Pixel PixelRuntimeSmoke -j6` 与完整 `PixelRuntimeSmoke` 通过，包括真实F2设备操作、body/port颜色读回及两轮resize/minimize。Review修复后重新构建，最终 `PixelRuntimeSmoke --formula-only` 退出0，重新覆盖设备、Formula GPU和Formula UI；F2设备点击代码与之前通过版本一致。Vulkan validation layer不可用，未执行Release/sanitizer；未改引擎或重跑AET。

独立review发现并关闭两项：P1正常过程风额度竞争被误判为内部错误，已在预留燃料前检查共享额度，回归验证不扣煤也不留下实例；P2两格脉冲常未覆盖任何MAC面样本，已采用16格半径并增加同核覆盖检查，独立对照观察到434176个格中心中的3392个风速变化。审查者复核当前全部变更、生命周期、呈现与事实，无未解决发现。`git diff --check` 与AWT `check` 通过。
