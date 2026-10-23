# 有归属的持续 FieldSource（M4.5更新）

Environment 拥有局部加速度来源，Formula 与设备持有不透明 EffectHandle。首个实际用途是给 Dirt 持续施加局部推力；Water 和 Actors 沿用各自运动规则。World 不依赖 Formula、设备、UI 或渲染，Axiom 没有改动。

## 来源、单位与容量

`FieldSourceHandle` 为 World 内单调 ID，同一个 World 重置后不复用；`Owner` 为宿主运行实例 ID，安装/更新/停止需对应所有者。来源描述包含 chunk-relative 圆心、半径、加速度和活动步寿命；快照包含剩余寿命。中心使用整数格加局部浮点，测试覆盖正负 2^55 格。

均匀圆盘在格中心采样，按递增来源 ID 合成，加在既有 BaseForce + air velocity × 60 的结果之后。加速度单位是格/秒²，空气速度和一次性风脉冲是格/秒。Dirt 的既有响应将合成加速度除以60得到方向判断速度，每成功步最多移动一格；来源从不写入永久 BaseForce，停止仅移除其贡献，已经发生的位移不回滚。

- 最大256个来源，固定内联有界存储（M4.5增加Suspended标志），零来源也保留该容量，不增加堆分配。
- 半径大于0且不超过16格，加速度模长不超过36000格/秒²，寿命1–3600个成功环境步。
- 完整圆盘必须在当前模拟域内，中心必须驻留；不裁剪圆盘，不通过呈现加载扩大活动域。
- 安装满容量/域外/未驻留拒绝；更新失败不改变旧来源。无效、过期或外部所有者句柄拒绝，重复清理无副作用。
- 领域API接收可信规格，非法规格抛错；FormulaBridge在进入World前将程序超限值转成Geometry拒绝。

## 固定步与清理

成功步中的顺序为：风推进 → 活动来源区域唤醒与材料提议/提交 → 风反馈 → 来源剩余寿命减1、到期清除 → Actors → Formula → Devices → 统一提交。Formula/设备在步末安装的来源从下一个环境步生效。采样与F1在安装后的提交即可观察配置。

预算不足时保留准备中的环境输入；来源不老化、不重装、不重复作用，重试成功后只消耗一步。pending期间来源安装/更新/停止及World重置为内部调用错误。暂停只允许编辑而不推进寿命。

来源圆盘离开完整活动域后不再贡献，保留身份和剩余活动寿命；活动域实际移动即更新来源版本，关闭风也能让已提交视图及F1及时失效。M4.5已接宿主实例与整组来源的[冻结恢复](RegionActivity.md)，冻结时不老化，恢复不重新安装或扣资源。

ProcessHost显式清理正常完成、stop、fault、取消及承载者移除的所有来源；清理不依赖C++析构。Clear/Cancel/OnWorldReplaced明确接收World。所有同批程序先求值和碰撞，再按稳定顺序提交效果；结束片段的效果提交后清理持续对象，已经完成的挖掘等一次性事实保留。

## Formula 与设备

`process 1`新增 `acceleration`、`field`、`effect_handle`、`update_field`、`stop_field`；不能用speed替代acceleration，不能从数字构造句柄。安装和更新成功的回执返回同一个token，`await result`之后读取；跨等待保存到local。更新需消耗1额度，停止不收费，零额度仍能释放已有来源。无效token收到Rejected/InvalidHandle。Capability::Field=32，AllCapabilities=63。

HostContractVersion与InstanceSnapshot.SchemaVersion升级为2，拒绝旧合同/快照；process语法及SemanticsVersion保留1。独立Formula库与fake host能验证快照中的句柄、缓存回执和挂起/恢复，游戏实际区域挂起已由M4.5接入。示例见 `Pixel/Formula/Examples/Process/field.formula`。

唯一设备从旧风脉冲发生器更新为推力发生器，类型3001保持，安装程序资源3版本2。每煤维持30个成功tick，程序显式存储安装回传的句柄并停止；设备关闭/拆除还有宿主所有权清理。半径16格、向右3600格/秒²，不要求开启Coarse风。成本、补煤和整批回收规则不变；计数字段Pulses改为Cycles，删除不再适用的OutputUnresolved状态。完整设备合同见[Devices](Devices.md)。

`FieldSourceChanged`事实记录安装/更新/停止的句柄和操作，承接程序/设备因果。到期、取消后的实际场由已提交投影反映；完整来源/设备观察topic留后续任务。

## 验证与测量

2026-10-23，Apple M5/macOS。独立验证agent完成：

- Formula独立构建与CTest：7/7。
- CPU全套：77/77。新FieldSources测试覆盖生命周期、所有者、稳定叠加、远坐标、容量、真实Dirt运动与预算重试；FormulaBridge与Devices测试覆盖三类承载者及Device、句柄更新/停止、故障/取消、一次性作用保留、燃料周期与容量拒绝。
- Runtime构建Pixel与PixelRuntimeSmoke通过；`--formula-only`与`--debug-ui-only`通过，实际MoltenVK/Apple M5上检查F1全部采样值和GPU颜色读回、多个帧槽、设备图形和F2真实操作。
- Review发现关闭风时模拟域移动未使固定视窗缓存失效；新增回归先复现精确失败，再在World域移动处显式通知Environment更新来源版本。修复后CPU全套77/77和Formula GPU重跑通过，独立审查复核关闭P2。审查另外建议小窗口量化后边界不变时不要刷新；已限制为实际域变化才通知，新增64格小窗口回归1/1通过并重新构建Runtime，最终复核无未解决发现。最后这处性能修复未重复全量CPU/GPU。

F1截图来自真实GPU读回，再将线性RGBA编码为sRGB。测试先在灰色方形夹具内清零BaseForce：安装后出现粉红圆盘（向右），拆除后恢复灰色；绿色外围为既有向下力。颜色仅表示方向，不表示强度。

| 安装前 | 活动中 | 拆除后 |
|---|---|---|
| ![安装前](../../Docs/Validation/FieldSources/before.png) | ![活动中](../../Docs/Validation/FieldSources/active.png) | ![拆除后](../../Docs/Validation/FieldSources/stopped.png) |

Release benchmark：256×256有限世界、1024个Dirt、重叠圆盘、240轮；有源时每轮唤醒4个region/16384格，无源基线休眠，不宜把差值全部归因于合成循环。每来源的反向加速度按数量分摊以保持同一静态物料场景。采样一栏为1024格逐点查询的每格中位数。

| 来源数 | 环境步中位数 µs | P95 µs | 每格采样中位数 µs |
|---:|---:|---:|---:|
| 0 | 0.208 | 0.292 | 0.0128 |
| 1 | 36.125 | 45.291 | 0.0164 |
| 8 | 40.000 | 46.250 | 0.0234 |
| 32 | 65.875 | 76.959 | 0.0501 |
| 64 | 92.541 | 98.417 | 0.0758 |
| 256 | 304.167 | 321.792 | 0.2690 |

各档World reserved均为14735032字节，来源容量不随活动数扩容。原始[CSV](../../Docs/Validation/FieldSources/benchmark.csv)随代码保存。基准只覆盖当前有界规模，不推断多岛或持久化性能。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j6
ctest --test-dir build-cpu --output-on-failure -j4
cmake --build build --target Pixel PixelRuntimeSmoke -j6
(cd Pixel && PIXEL_FIELD_CAPTURE_DIR=/private/tmp/pixel-fields ../build/tests/PixelRuntimeSmoke --formula-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke --debug-ui-only)
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelFieldSourceBenchmark -j6
./build-region-release/bin/Release/PixelFieldSourceBenchmark /private/tmp/pixel-fields.csv
```

本轮未跑sanitizer、完整长GPU smoke或AET；引擎未修改，Vulkan validation layer不可用。游戏来源、设备和库存仍未存盘；跨域checkpoint属于M4.6，当前重启清空。没有实现水/角色受推力、设备碰撞或完整观察topic。
