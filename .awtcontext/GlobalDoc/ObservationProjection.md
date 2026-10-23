# 有界领域观测与提交事实

M2.4 实现入口为 `Pixel/Source/Observation/`、`GameAPI/Observations.*`、`GameAPI/Facts.h` 和 `Gameplay/SessionObservation.cpp`。这是单写者 Session 的内部观测基座；外部主体权限、view 租约、分页游标与 JSON 适配属于后续 M2.5。

## 发布与所有权

`GameSession::Observe(NativeCaller, Rect)` 返回拥有自身内存的冻结 DTO。每个原生调用者占一个宿主缓存槽，共三个；新范围替换旧范围，`CloseObservation` 释放槽。首次观察只在完整边界采样，之后每次成功提交更新已登记范围。不会请求加载、触发流式服务、激活模拟或制造新 revision，也不持有渲染 frame slot、WorldMapView 或领域指针。

暂停编辑、构式 Apply/Clear、成功模拟步和接纳加载均沿 Session 原有提交边界发布。材料按 64 格区域版本增量更新；风版本变化时重采样范围内空气格，材料变化也失效对应区域。移除块会覆盖为未知，重新驻留必须重新采样。当前各主题精确对应同一提交，字段保留实际采样 revision，未变化值可来自较早的采样点。

PendingStep 中，同槽同范围只能复制最后完整投影；未登记或更换范围返回 `NotSampled`。`ObservationService()` 单独返回实时 Ready/WaitingForBudget 与最后提交版本，不把服务等待混入冻结领域 DTO。替换世界清空投影与事实环；旧 DTO 仍是旧 epoch 的自有数据，不能用于推断新世界。

## 内容与采样

Snapshot 携带 schema、WorldId/epoch/tick/revision、范围、分辨率、事实高水位、暂停状态和来源标识。材料为逐格可选 ID，缺失不是 Air；快照显式提供 ResidentCellCount 与 Available/Partial，覆盖分母是请求面积；空气风速单位 cell/s，在已知空气格中心调用原始 `SampleWind`，不使用材料响应、合成力或渲染颜色。风元数据包含配置后端、开关、域、均匀后端网格间距或自适应最大叶边长；Hybrid/Procedural 标明局部障碍修正。

风样本原因区分 NotResident、NotApplicable、Sampled、Disabled、OutsideDomain、EmptyBackend。已知非空气没有风数值，缺失也没有数值。已知空气在 disabled、域外或 Empty 时为明确的零（原因按此顺序优先）。坐标保持 int64 格锚点和 [0,1) 小数，正 Y 向下。

`SummarizeWind` 是只依赖标准库的纯值聚合。16×16 分箱以世界零点对齐、负坐标向下取整，只统计与范围交集；返回 requested/resident/air/nonAir/unknown 数量。覆盖分母是 requested，风均值分母仅为 air，X/Y 分别等权平均。没有空气但有未知时为 Partial 且无均值；全部已知非空气为 NotApplicable。返回最早/最新实际采样 revision。

玩家矩形与范围相交时返回世界内 ActorId、位置、尺寸与着地状态；弹体中心在范围内时返回会话 epoch 下不重用的激活 ID、弹体 ID、程序版本、位置、速度和半径。程序摘要含当前应用版本及会话级运行根/弹体/排队数量，不包含 AST 或源码。所有对象值在提交时复制。M4.7已增加库存、设备、过程实例与场源投影，具体内容及授权见[GameplayObservation](GameplayObservation.md)；温度、生态与知识仍为Unsupported，不能用零值代替。Unknown 是以后“系统存在但尚未发现”的独立语义。

## 事实与因果

Session 在完整提交后、发布投影前分配事实序号。原生开采/放置、程序激活、风配置保存实际结果和请求键；回执 `FactRange` 引用连续事实区间，重复请求返回原区间，不重复执行或发事实。空程序仍可有一次激活事实。

FormulaRuntime 每次 Cast/Step 导出有界成功效果记录，Session 写入正式 epoch 与提交 stamp。挖掘只在实际 changed>0 时输出一条圆域摘要；风只在后端接受时输出 `WindPulseSubmitted`，记录请求圆和 deltaVelocity，不声称测得风速改变。原因携带 instance、程序版本、根内 emission 序号和最初激活请求，跨 tick 保留来源。未实现/拒绝/零变化效果不会伪造成功事实；材料每格物理移动不写事实。

事实环保留最近 256 条且最多 128 KiB（含固定槽和 WorldId 字符串容量）。`ReadFacts(epoch, after, high, limit)` 固定上界、最多256条；淘汰导致 Gap，不跨缺口返回看似完整的增量。旧 epoch 拒绝；新事实不会进入旧 highWatermark。事实环不持久化；M5.1已增加独立128条重要历史档案、玩家发现和带版本保存DTO，磁盘接线尚未实施，见[发现知识与历史](KnowledgeAndHistory.md)。

## 预算与测量口径

单范围每边最多64格、最多4096格、256弹体；M4.7另有最多128KiB的已提交GameplayObservation，由各有界域生成并按对象锚点裁剪；投影payload硬上限1 MiB/槽。最多三个槽，无观察时不分配格缓存。查询返回的一份副本最多1 MiB，事实读取最多256条；调用方主动长期保留多份返回值的空间由调用方负责，外部服务总租约由 M2.5 约束。替换缓存先释放旧槽。

`ObservationStats()` 报告宿主缓存、事实环和构式固定效果暂存的保留容量/上限、活跃范围、材料/风采样格数、写入格 DTO 字节与导出 DTO 字节。它是所有权容量计数，保守计入字符串 SSO，不是 RSS、allocator 元数据或 CPU 总线流量；查询副本和摘要返回值不计入宿主保留量。计数不含既有世界、渲染与构式解释器资源。这些观测资源有独立固定上限，不通过世界预算预留驱逐地图。

`PixelObservationValues` 只编译 DTO 聚合和事实环，依赖标准库；投影适配器编入 PixelGameplay，World 与独立 Formula 不反向依赖观测。引擎无需新增能力。

## 验证入口

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelObservationBenchmark -j4
./build-region-release/bin/Release/PixelObservationBenchmark
```

### 2026-10-22 实际结果

环境为 Darwin 27.0.0 arm64、Apple clang 21.0.0、CMake 4.4.0。独立验证 Subagent 在最终代码上完成 CPU 全量构建与 CTest，**62/62 通过**，其中新增 Pixel.Observation 覆盖冻结副本、增量采样与驻留覆盖、±2^55 附近的驻留格和 .125 玩家小数、块移除/重插、实际预推进风的半步隔离与恢复、激活身份不重用、事实去重/来源/固定上界/gap、成功与跳过的构式效果、风均值及容量边界。Pixel 应用最终增量编译通过；链接器仅报告重复 spdlog 库警告。没有修改渲染或引擎，不在此任务重跑 GPU/视觉 smoke。

Release 基准三轮，各模式128帧，使用同一1024²有限Flat场景、固定输入和Procedural种子。观测范围为 `(480,200)` 的64×64格；动态模式在计时外确认存在 `AirKind::Sampled`。耗时包含 Tick 和（有投影时）一次冻结DTO导出，未减去分配或拷贝；初次捕获也计入。无并行编译。三轮原始数据保存在 [CSV](ObservationBenchmark-2026-10-22.csv)。

| 场景 | 无投影 P50 / P95（ms） | 有投影 P50 / P95（ms） |
|---|---|---|
| Empty静态风 | 0.000958–0.000959 / 0.003375–0.004250 | 0.002833–0.002834 / 0.005542–0.005625 |
| Procedural动态风 | 3.480042–3.520959 / 3.560583–3.581458 | 3.522708–3.562958 / 3.564292–3.710792 |

每轮有投影时导出21,015,424 B；静态格DTO写入163,840 B，动态写入20,971,520 B。无投影保留100,480 B（事实环与效果暂存等），单活跃槽保留287,239 B，总固定预算上限3,312,800 B。数值是这一小范围与固定场景的局部测量，不外推所有后端或多范围负载。

另一位独立 Subagent 审查了生产实现、Session接线、事实来源与缓冲、测试和基准。发现动态基准原范围完全在风域外，未测真实风采样；已移动到风域内、增加Sampled断言并重新完成三轮测量。驻留计数增量逻辑和最终测试补充也已复核，**无未决审查意见**。审查者执行 diff 检查，构建和运行证据由独立验证 Subagent 与主 Agent 提供。


## M2.5 接入

LLMView 通过 GameAPI/ObservationSource 读取专用第四采样槽。该槽仅在 OpenView 同步捕获期间存在，返回拥有数据的副本后立即释放；原有三个原生槽保持独立。PendingStep 下新捕获返回 NotSampled，既有外部冻结 view 继续可读。服务授权、分页、租约和最终测量见 [LLMView实现](LLMView.md)；上文三槽的M2.4测量保留为当时证据。
