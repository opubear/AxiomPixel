# LLMView 只读查询与离线贯穿样例

M2.5 于 2026-10-22 完成。LLMView 提供进程内只读服务和 JSON 适配，消费完整提交的领域值；静态授权由宿主装配，不接模型、HTTP 或供应商 SDK。前置提交为 `e3ef2a5`，包含 M2.3 保存边界和 M2.4 领域投影。

## 所有权与依赖

`PixelLLMView` 链接标准库值目标 `PixelObservationValues`，JSON 使用已有 nlohmann 头文件；不链接 World、Formula、ImGui、RHI 或引擎运行时。`GameAPI/ObservationSource.h` 是宿主线程上的只读端口，提供有界捕获、提交外服务状态和固定上界事实读取。GameSession 实现该端口。

GameSession 保留原生 Input/Debug/Driver 三个观察槽，另用第四槽完成外部捕获。捕获返回独立拥有内存的 DTO 后立即关闭第四槽，避免过期外部观察继续按每个提交采样。捕获不调用呈现层 CreateReadView/RefreshReadView，也不创建、加载或激活块。PendingStep 时新捕获返回 NotSampled，旧 view 仍可查询；失败 OpenView 保留原 view。

`PixelGame` 在组合根拥有 ViewService，每次 FrameBegin 调用一次 AdvanceServiceFrame。暂停、空闲或预算等待均计数，查询次数和模拟 Tick 不计租约。headless 宿主显式推进服务帧。服务只允许宿主线程访问；它不是跨线程队列。异步消费者只在 Deliver 成功后取得拥有内存的值，不能把服务本身或 Source 交给工作线程。

## 端口与授权

| 入口 | 行为 |
| --- | --- |
| RegisterObserver / SetPolicy / RemoveObserver | 仅宿主 C++ 装配；返回凭据、更新授权版本、回收主体，不在 JSON 中开放 |
| DescribeCapabilities | 只列授权主题、实现状态、单位、来源、分辨率与限额 |
| OpenView | 冻结 CommitStamp、范围、所有主题及事实高水位；成功时替换该主体旧 view |
| Query | 在冻结副本中按主题、矩形和分辨率查询，返回交付编号 |
| ReadFacts | 只读取 view 创建时 highWatermark 内的事实，返回交付编号 |
| Deliver | 再检查主体、授权版本、epoch、租约后移交数据；每编号仅交付一次 |
| CloseView | 释放冻结副本、游标与待交付数据 |

Policy.Topics 为显式授权集合，默认全拒绝；Region 可限制空间矩形。先验证请求完全位于授权区和 view 区内，再切片和聚合。当前 Programs、Runtime 的数量是全会话统计，Facts 也只支持全会话授权；带 Region 的策略不得授予这些能力，错误宿主配置直接抛异常。局部观察者只能获取已授权的空间主题，不收到隐藏的总数或 factHighWatermark。源码和动作权限没有隐含授权。M4.7的库存数量另需显式AllInventories或绑定epoch的OwnerActor授权；Facts包含私有设备煤数及M5.1玩家观测，现同时要求AllInventories与AllKnowledge。M5.1增加Knowledge/History授权、ReadHistory冻结增量和独立派生摘要；历史档案需全局完整授权，规则见[发现知识与历史](KnowledgeAndHistory.md)，设备规则见[设备库存观测](GameplayObservation.md)。

SetPolicy 每次增加版本，即使更新后权限更宽，也撤销旧活动 view、游标和未交付结果（ViewRevoked）。epoch 更换、关闭、成功替换或第120个服务帧到达后旧 view 返回 ViewExpired。移除主体后凭据无效，统一 PermissionDenied。已交付值无法追溯收回。

## 资源和分页

| 资源 | 上限 |
| --- | --- |
| 注册主体 | 3，满额 RegisterObserver 返回0 |
| 活动 view | 每主体1 |
| 矩形 | 每边64 cells，最多4096 cells |
| 精细材料/空气风页 | 每页256 cells；每个查询最多16页 |
| 游标 | 每 view 64；重放同一查询页复用游标 |
| 待交付结果 | 每主体1，Query、ReadFacts 与 ReadHistory 共用 |
| 保留字节 | 每主体 view、副本元数据、游标与待交付结果合计≤1 MiB |
| 租约 | 120宿主服务帧 |
| 事实页 | 256条；来源事实环仍遵守256条/128 KiB上限 |
| JSON | 请求≤16 KiB，嵌套≤16层，响应≤1 MiB |

游标是不透明的单调编号，服务保存主体/view 下的完整 QuerySpec 和下一页 offset。更换 topic、范围、resolution、pageSize 或 schema 不能续接旧页；不按外部 offset 直接访问。超出页数、矩形、游标或字节预算显式拒绝，不降采样或逐渐扩大范围。

Statistics 计入容器 capacity、字符串 capacity、固定对象、游标和未交付数据。该指标是服务保留量，不是进程 RSS；调用者已取得的 DTO、短期捕获副本、JSON DOM/输出和宿主事实环另属各自所有者。拒绝、撤权、过期与关闭都会释放对应动态数据。

## 观测语义

材料逐格给出坐标、可选 materialId 和实际采样 revision；没有 materialId 是 not_resident。风仅提供已知空气格中心的原始 cell/second 速度。逐格响应区分 Sampled、Disabled、OutsideDomain、EmptyBackend、NotApplicable、NotResident；Disabled、OutsideDomain、EmptyBackend 返回已知零速度，并保留明确分类。

风只支持 resolutionCells=1 或16。16格按世界原点对齐、负坐标向下取整；先截取查询范围，再调用纯值 SummarizeWind。每箱记录 requested、resident、air、nonAir、unknown 数量，均值只用 air 分母。缺失不当作零，非空气不进入平均；没有空气且没有未知时为 not_applicable。风元数据保留后端、合同版本、域、求解网格间距等，1格输出不承诺1格求解器。

Player 用角色锚点是否位于查询范围决定可见性；程序返回已应用版本、会话数量和范围内持续过程的状态/等待，Runtime 另外返回冻结暂停状态及范围内弹体。运行时 live waiting/ready 状态仍单独走 Source.ObservationService，不混入冻结页。M4.7已实现Items、Devices、FieldSources主题及按记录分页；私有库存先过滤再统计，未授权Fuel缺省。温度、生态仍Unsupported；JSON不输出虚假采样值。

所有页共享 view 的完整 CommitStamp。页范围/覆盖、来源、单位、availability/reason、最早/最新实际采样 revision 与 nextCursor 都在结果中。事实响应另外携带 FactView 描述符，保留 cause 的 RequestKey 或程序实例/激活请求链。环淘汰返回 Gap 与 FirstAvailableSequence；消费者需要重新打开快照，不能从缺口拼出完整历史。

## JSON 使用

JsonAdapter::Handle(observer, request) 中 observer 是宿主绑定参数。JSON 不接受 observerId、权限字段或动作方法。Query/ReadFacts/ReadHistory 在 Handle 内立即 Deliver，然后序列化；以后若接异步队列，应保留交付编号并在发送时调用 Deliver；Handle 的同步 JSON 不能充当可撤销的待发送缓存。

M4.1 的 player 对象增加 `actorId`，表示世界内稳定身份，结合 view 的世界/epoch 使用。

64位坐标、ID、tick、revision、sequence 和字节计量都用规范十进制字符串，uint32 范围/枚举数值保持 JSON 整数；位置使用 anchorCell + double offset。拒绝64位 numeric、前导零、负零、溢出、浮点 uint32、重复键和未知字段，不把内部异常伪装成普通查询失败。

```json
{"method":"OpenView","scope":{"origin":{"x":"504","y":"504"},"width":32,"height":16}}
```

```json
{"method":"Query","viewId":"1","topic":"materials","scope":{"origin":{"x":"504","y":"504"},"width":32,"height":16},"resolutionCells":1,"pageSize":256,"schemaVersion":1,"cursor":"0"}
```

其他请求是 `{"method":"DescribeCapabilities"}`、`{"method":"ReadFacts","viewId":"1","after":"0","limit":256}`、`{"method":"CloseView","viewId":"1"}`。viewId 必须使用本次 OpenView 返回的值。所有方法和字段区分大小写。

## 离线演示与验证

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
./build-cpu/bin/Debug/PixelObservationDemo > /private/tmp/pixel-observation-demo.json
```

演示打开32×16 view，读材料第一页和风/玩家/程序/Runtime 摘要，经 Driver 动作端口开采(520,512)，再读旧材料第二页，确认仍是Dirt；新 view 观察到Air，并验证事实与动作回执的 RequestKey 和 CommitStamp 一致。输出可直接读取的 JSON，不需要模型或窗口。Release 演示也已实际执行，保留量27,590 bytes。

独立验证 agent 完成全 CPU/mock 构建及65/65 CTest（67.98秒），Pixel运行时构建成功。审查修复后重新构建并执行 Pixel.LLMView、Pixel.LLMJson、Pixel.Observation、Pixel.ObservationDemo，4/4通过（2.29秒），Pixel最终重建成功。新增用例包含同 view 跨提交分页、所有游标绑定维度、真实预算等待、撤权与待交付数据、暂停服务帧过期、epoch切换、事实固定high/gap、区域过滤、远处不加载、64游标上限、超额快照及合计字节拒绝，以及±2^55+17/offset=.125的JSON精度。

强模型独立 review 核对依赖与各边界，发现 SummarizeWind 的 INT64_MAX 附近范围验证比 ViewService 更严格。修复为按宽高保留16格分箱余量；增加 INT64_MIN+16、INT64_MAX-20 极值查询测试。审查复核无未决重要意见。未重跑GPU smoke；本轮仅在帧首增加服务帧计数，无输入或显示行为更改，Axiom子模块未修改。Pixel链接有既有重复spdlog库警告。

## 最终 Release 测量

```sh
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelLLMViewBenchmark -j4
./build-region-release/bin/Release/PixelLLMViewBenchmark /private/tmp/pixel-llm-view.csv
```

本机三轮、每场景128帧，无预热；baseline推进世界，对照组每帧为三个观察者重开64×64 view，分别读完16页材料、读取16格风摘要并序列化全部 JSON。静态Empty和动态Procedural使用同样输入；动态样本实际覆盖风域。原始数据见 [LLMViewBenchmark-2026-10-22.csv](LLMViewBenchmark-2026-10-22.csv)。

| 场景 | baseline帧P95 | 三观察者全量JSON帧P95 | 三次Open合计P95 | 查询及JSON合计P95 |
| --- | --- | --- | --- | --- |
| 静态Empty | 0.003458–0.004334 ms | 7.763584–7.984208 ms | 0.064792–0.069292 ms | 7.693084–7.891417 ms |
| 动态Procedural | 3.588917–4.070250 ms | 11.396791–11.475458 ms | 0.180125–0.183418 ms | 7.665209–7.728999 ms |

三活动 view 加单个待交付页峰值520,121 bytes；全部主体回收后2520 bytes固定服务状态，宿主外部采样槽始终释放。每128帧分别输出125,864,853和126,105,672 bytes JSON。主要成本是每帧12,288格完整文本化，约7.7 ms；这是压力负载，不应视为每帧轮询策略。实际宿主空闲时不创建观察者、快照或JSON；已打开view只消费自己的冻结值，按需请求摘要和有限页即可。

M2 至此形成不依赖真实模型的完整只读切片。此处为M2历史记录；M5.2已接入动态Authority授权，异步模型接入仍待后续任务。

## M5.2 当前授权

Policy.AuthoritySubject 将区域观察者绑定宿主主体；Open/Query/ReadFacts/ReadHistory/Deliver及服务推进使用ObservationSource实时授权版本。撤权或版本变化释放旧视图和pending并返回ViewRevoked；区域主体不能取得全局事实/历史/运行统计。具体设备保护、动态与静态策略边界见[区域控制与干预撤销](Authority.md)。
