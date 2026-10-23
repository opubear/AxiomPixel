# 设备放置、端口与最小工作状态（M4.5更新）

设备属于 Pixel，不编码进 CellType，也不进入 Axiom。M4.3 建立建造和供煤路径，M4.4 将唯一设备更新为局部推力发生器（类型3001），以持续FieldSource验证程序承载。当前设备与来源仅在会话内存中存在；M4.5已接[区域冻结恢复](RegionActivity.md)，M4.6接跨领域保存，M4.7接完整库存/设备 UI 与 LLMView投影。

## 领域与身份

- `Devices` 是标准库与 Items 值类型上的有界集合，最多64台。每台有单调不复用的 ID、版本、玩家 ActorId 所有权、整数格坐标、启用状态、运行实例、工作周期数（Cycles）和煤数量。
- `DeviceRef` 是 ID+版本；外部动作还受世界 epoch 与调用者会话约束。放置、补煤、启停和回收由 `GameSession` 在完整提交边界编排；原生 Input/Debug/Driver 均代表本会话玩家，payload不能指定所有者。
- `Items::Inventory` 持有燃料容器，设备仅保存容器身份与提交摘要。燃料槽容量64，仅接收煤。回收后销毁空容器，不累积废弃槽；世界替换统一清实例、设备与库存。
- 设备是2×2格非实体设施，不提供角色碰撞，也不改变材料模拟的运动规则。Construction 拒绝与地形、角色或其他设备重叠的放置，拒绝在已占用格放土。流动材料仍可能进入设备范围；安全边界检测到非空气会取消工作实例并报告环境等待。

## 动作与原子性

`PlaceDevice` 花费4石头+2铜矿。先检查2×2驻留空气、角色/设备占用、设备/容器上限，再预留整批物料，建立燃料容器与设备，消费预留。缺料、占用、未驻留、无玩家权限和容量上限返回明确拒绝，不生成实体或扣料。预检后违反内部不变量直接失败，不用恢复分支隐藏程序错误。

`SupplyDevice` 向拥有的设备整批转移1–64煤，检查版本与容量。`SetDeviceEnabled` 只改变开关；工作只在成功固定步中推进。`RemoveDevice` 在检查背包可容纳建造成本及所有剩余煤后，取消设备承载者、转回燃料、退建造成本、销毁空容器及设备。背包不够时全部保留，不能只回收一部分。已消费的煤不退。

动作沿用最近64回执去重；同一请求重复不会重复扣料/回收，新的请求使用过期DeviceRef会返回PreconditionFailed。停机与移除按Device承载者种类+设备ID取消，不能误删相同数字ID的玩家/天然源/测试夹具。通用夹具的Start/Cancel/Wake端口不能冒充Device。

## 运行合同

每台设备安装独立不可变的 `device/force-emitter/2` 程序（ProgramRef资源3、版本2）：

```text
process 1;
emit field(cells(16), acceleration(3600), acceleration(0), ticks(31));
await result within ticks(1);
let source = effect_handle();
wait ticks(29);
emit stop_field(source);
```

输出圆心为设备格框中心，圆盘内均匀向右加速度3600格/秒²；与BaseForce和风响应合成，不写回BaseForce。一份煤支付30个成功环境步，首个下一步接收安装回执，再等29步通过句柄停止。来源31步寿命提供独立上限；正常设备在到期前停止。随后若仍启用且有煤，同一提交中启动新周期。授予Field能力、一次付费效果额度和16格半径限制，stop不收费；同设备最多一个实例。玩家Apply不更换设备程序。

设备范围需要驻留空气，完整圆盘必须位于当前模拟窗口。无需Coarse或任何风后端。状态为Stopped、Running、NeedsFuel、WaitingForEnvironment、OutsideActiveArea、WaitingForCapacity；旧OutputUnresolved状态删除。来源容量256、会话运行根16、每帧激活8。启动前检查来源容量，再预留燃料；程序拒绝时释放预留，不能扣煤或留下实例。暂停/预算等待不推进。

离开活动窗口或失去驻留时冻结实例及其全部来源，保留开关、剩余周期和未用煤；重返活动域后接续同一实例和来源，不重新扣煤。恢复时核对设备身份、启用状态和Owner Actor；地形阻塞时取消旧周期，Owner消失则停机。关闭或回收立即清理持续来源，已经发生的材料移动不回滚。`Clear running`同时关闭设备以阻止自动重启。持续场的详细所有权、单位和提交规则见[FieldSources](FieldSources.md)，活动时间与验证见[区域冻结恢复](RegionActivity.md)。

## 事实与呈现

动作回执和领域事实包含拥有值的DeviceChanged（placed/removed/supplied/enabled/state_changed），包含DeviceRef、Owner、坐标、燃料、状态和实例身份。主动动作原因是RequestKey；成功步中的状态事实原因是DeviceCause。安装/更新/停止记录FieldSourceChanged（句柄与操作），以资源3+实例身份关联设备。事实表示操作已提交，F1与已提交力场值显示实际作用。LLMView既有全域事实权限可读取这些事实；设备topic仍留M4.7。

`CommittedWorldView` 保存独立设备摘要，`DeviceFrame` 仅按已提交值生成呈现几何。颜色区分状态，右侧小矩形标出输出端口。远坐标先相对整数参考再转浮点。渲染复用现有合成目标，顺序为世界→风→设备→构式球→人物→后处理→UI；F1隐藏设备图形，数据仍保留。CPU快照纳入发布预留，几何内存单独统计；没有新增GPU目标。

B切换设备布置工具，左键放置；设备范围内右键启停、Shift+右键回收。F2 Devices可输入坐标放置、选择设备、补1煤、Enable/Disable、Recycle，显示建造成本、燃料、状态及操作拒绝。布置模式有常驻操作提示。测试夹具可直接给库存注入资源；正常游戏仍需开采。

默认暂停只允许配置和建造。放置、补煤、启用后单步或取消暂停；F1可看到局部方向变化，Dirt按合成力逐步移动。设备不改变Water或Actor的既有运动规则。

## 验证入口

- `Pixel.Devices`：缺料/占用/未驻留/权限拒绝、占用不编码地形、过期引用、重复动作、整批燃料转移、背包满回收拒绝、完整回收/容器释放、工作周期、持续推力实际Dirt运动、来源容量竞争、区域退出不重放、玩家程序隔离、设备归属与事实、停机/销毁/清空和世界替换。
- `Pixel.DeviceRendering`：远坐标精度、状态颜色、独立发布、移除与F1；`Pixel.Input`：B、非重复按键及设备鼠标路由。
- `PixelRuntimeSmoke`：设备body/port真实GPU颜色读回与状态变化、frame slots、F1和移除；F2真实点击放置/补煤/启停/回收，确认没有输入穿透。

M4.4最终验证与性能记录见[FieldSources](FieldSources.md)。M4.3旧脉冲设备的75项CPU与GPU验证属于提交d5fa1d0；当前设备行为按本页更新，不能用旧测量替代新验证。
