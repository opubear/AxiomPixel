# 设备、库存与持续程序观测（M4.7）

2026-10-23。在M4.6检查点之后补齐可观察性；不增加生产链、设备类型或微气候生态规则。`GameAPI/GameplayObservations.h` 提供拥有值的设备、容器、程序运行和场源合同，既不包含领域指针，也不包含程序源码、locals或指令位置。引擎和独立Formula语言未改动。

## 同一提交与两种发布

GameSession在完整提交的PublishCommit中生成有界GameplayObservation。玩家背包和设备煤槽包含持久容器身份/版本、Owner Actor、数量/容量和物品定义版本；未被游戏承载对象拥有的测试/内部容器不导出。设备包含身份、位置、启停、状态、当前实例、周期与煤数量。过程包含三类原型承载及真实设备的身份、位置、程序资源版本、活动步、剩余步、挂起和等待原因。场源包含句柄、数值Owner、可确认的程序/设备归属、中心/半径、加速度、剩余活动寿命、挂起和实际贡献资格。

数值Owner本身不证明程序归属；只有同一实例的存活效果引用匹配时才提供OwnerInstance和DeviceId。原生来源允许只有OwnerId。Contributing复用FieldSources自身的驻留、活动域和挂起判断，不把持有来源误报为正在施力。加速度单位是cell/second²，不与空气速度混用。

领域全量投影上限192KiB（M5.5计入满容量祈祷原文与说明），供有界读模型和观测槽复制；世界/设备/程序自身已有数量上限。ObservationProjector按设备锚点、容器承载者位置、程序当前位置和场源中心裁剪64×64范围。范围外来源即使作用圆相交也不出现在该对象列表，这不是任意点的完整贡献者查询。各对象样本使用完整CommitStamp，PendingStep只允许既有完整副本，新范围仍返回NotSampled；查询不加载或激活世界。

CommittedWorldView与领域查询从同一份GameplayObservation取值。GamePresentation在成功生成渲染帧时复制该值；F2的Inventory、Devices、Programs and fields只读取这份已发布副本。设备被删除或状态变化后下一次成功发布更新，旧帧仍自洽；预算拒绝发布时保留旧帧和对应UI值。Draw不重新采样这些领域，界面动作仍经Session动作入口，过期引用正常拒绝。发布副本的保留容量计入呈现预算。

一个GamePresentation对应一个逻辑发布帧，生产PixelRender遵守这一所有权关系；GPU多帧资源不创建额外逻辑发布帧。多个独立CPU消费者应各持有一个GamePresentation，不能把共享缓存当成每个publisher独有的历史副本。

## 查询与隐私

`items`、`devices`和`field_sources`成为已实现主题；`programs`补充持续过程列表。材料/风分页保持原合同，新增对象主题按记录分页；一条inventory记录包含一个完整容器及其栈。先按授权和空间裁剪，再计算记录总数和分页游标。所有页、重放页、删除前的旧view均固定于同一提交；新view才观察新状态。

Policy.Topics只是主题可用性。读取精确库存或设备煤数量还需宿主显式授予当前epoch下的OwnerActor库存权限，或AllInventories。授予Items主题本身不会暴露任何容器、数量或隐藏容器总数；无库存授权的设备仍能显示外部工作状态和停机原因，但Fuel缺省。新epoch不会继承旧OwnerActor授权。FullAccess是显式完整宿主权限。

完整Facts含设备煤数等私有事实，因此Facts要求AllInventories；仅某个Owner的授权不足以开放全会话事实。带Region的策略仍不能授予全会话Programs摘要、Runtime或Facts。SetPolicy撤销旧view、游标和未交付结果；每次交付重新核验。已经交付的值不能追溯收回。

公开程序数据不提供配方读取、复制、编辑能力。温度、生态仍为Unsupported；M5.1已加入独立授权的Knowledge/History主题，见[发现知识与重要历史](KnowledgeAndHistory.md)。JSON保持64位身份/坐标/tick/数量水位以十进制字符串编码；设备Fuel、物品Quantity等uint32仍是整数。能力目录声明分页单位、已实现主题和实际库存权限，接口不接模型SDK或网络。

## 玩家可见状态

设备仍是2×2格推力发生器，独立颜色区分停止、工作、缺煤、环境阻塞、活动域外和容量等待，右侧输出端口标出方向。F2 Devices给出状态原因、煤槽容量、当前周期、程序等待和对应场源；Inventory显示背包与煤槽各物品数量；Programs and fields列出持续过程和来源。F1继续查看实际合成力方向。

暂停时可以配置，成功单步才消费活动时间。等待原因区分delay/result/hit/wake；暂停和挂起不被误写成进度继续。外部状态可见不意味着有查看私有库存或程序内容的权限。此步保留既有设备操作，不提供新的合成或网络连接功能。

## 验证

2026-10-23最终实现验证：

- `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`、`cmake --build build-cpu -j4`及`ctest --test-dir build-cpu --output-on-failure`：81/81通过。新增GameplayObservation覆盖库存/设备/过程/场源同提交、删除和环境阻塞失效、原生场源归属、空间裁剪、挂起和epoch；LLMView/JSON覆盖授权、撤销、冻结分页、能力声明、精度与不暴露程序内部。
- `cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release`，构建对应测试并执行`ctest --test-dir build-chunk64 -R '^Pixel\.(GameplayObservation|Observation|DeviceRendering|LLMView|LLMJson|Devices|Checkpoint)$' --output-on-failure`：7/7通过。原有远坐标夹具从边界移入合法范围8192格，保留超双精度整数范围、正负坐标、.125偏移和不加载断言；修正后默认chunk配置下LLMView/Observation复验2/2通过。
- `cmake --build build --target Pixel PixelRuntimeSmoke -j4`通过。`cd Pixel`后执行`../build/tests/PixelRuntimeSmoke --debug-ui-only`通过：库存内容与容量、设备状态原因、程序等待、场源作用及发布后变更仍显示旧提交，下一帧才更新。测试直接注入库存后显式提交夹具改动，未让生产UI读取实时库存，也未放宽断言。
- `cd Pixel`后执行`../build/tests/PixelRuntimeSmoke`：完整GPU smoke退出0。通过材料PNG、水/透明度、图像镜像边缘、风可视化、基础交互、F1力场、F2设备库存UI、构式编辑器、设备本体/端口像素与持续场安装/移除、两轮窗口生命周期。
- 独立subagent完成生产改动审查与最终夹具复查，均无确定性缺陷；审查未代替运行验证。`git diff --check`通过，Axiom子模块无修改。

运行环境为Apple M5/macOS/MoltenVK；Vulkan validation layer不可用。未运行sanitizer、AET专项或新增性能基准，本任务未改引擎。CPU日志`/private/tmp/pixel-m47-final-cpu-ctest.log`，chunk64日志`/private/tmp/pixel-m47-chunk64-rerun-ctest.log`，UI日志`/private/tmp/pixel-m47-debug-ui-smoke.log`，完整GPU日志`/private/tmp/pixel-m47-runtime-smoke-final.log`。
