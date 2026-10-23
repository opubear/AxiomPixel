# 多领域一致检查点（M4.6）

2026-10-23。GameSession在完整提交点捕获世界、角色、库存、设备、程序和场源；新一代文件全部写好后才发布 `checkpoint.current`。这是会话内区域冻结恢复之后的磁盘保存入口。World没有Formula/设备依赖，语言格式由独立Formula运行时定义，引擎未改动。

## 文件与发布

每个世界保持 `Saves/current.txt` 选中的稳定世界目录。在其中：

- `working/session-*/chunks/` 是当前会话的可变工作数据。流式卸载及普通块保存只写此处；重新打开世界从已发布检查点复制得到新的工作目录，忽略遗留工作目录。
- `checkpoints/checkpoint-*/` 是独立完整的一代，包含 `world.meta`、`gameplay.state`、`chunks/` 和 `chunks.index`。所有块独立复制，因此已卸载的修改也被纳入，后续工作文件覆盖不能改变旧代。
- `checkpoint.current` 使用 `AxiomPixelCheckpoint 1` 和单个世代名称。写入新代及其索引完成后，用临时文件加rename发布该指针；新世界首存完成后才替换上层current.txt。

`chunks.index` 记录两个状态文件及全部块的名称、长度和FNV-1a校验值。载入先验证清单和文件，缺块、缺程序载荷、损坏内容、未知格式或非法路径明确失败，不将缺失检查点块当作新地形生成。FNV用于损坏检测，不是对抗恶意篡改的签名。

失败候选、旧代和遗留工作目录保留，不自动删除或回收。首次实现保存与启动均复制全部已有块，I/O及磁盘空间随探索规模增长；后续可在有性能证据时改为不可变块共享。本轮验证进程中断/写失败下的原子发布；没有增加fsync，不宣称系统断电后的持久性保证。

## 捕获与恢复内容

保存仍拒绝PendingStep，返回Deferred，不写新代、不补步。正常保存先等候工作块写入及已完成加载，在安全边界同步活动资格和读视图，再复制各域的值。捕获及写入同步执行，不引入后台事务框架。各域容量仍使用既有上限，游戏载荷最大8MiB、程序载荷最大2MiB。

| 内容 | 保存规则 |
| --- | --- |
| 世界 | 生成描述与版本、所有已保存工作块、材料和基础加速度、活动窗口配置 |
| 时钟 | 成功Tick、CommitRevision、完整余量、暂停；恢复启动新的运行epoch和调用者身份 |
| Actors | 持久ID/类型、位置、垂直速度、着地状态及next-ID；重建内存句柄 |
| Items | 容器ID/版本/容量、语义物品ID及定义版本、数量、身份高水位；瞬时预留必须已经完成 |
| Devices | 类型3001/v1、身份/版本、位置/Owner、启停/周期、程序实例ID及燃料容器引用；未引入尚不存在的网络连接 |
| 程序 | 选择的程序、每个实例实际使用的不可变产物及精确内容身份、PC/locals/等待/事件水位/能力/credits、宿主队列和弹体位置/剩余寿命 |
| FieldSources | 原句柄、owner、定义、剩余活动寿命、Suspended及next-ID；不重新安装 |

恢复直接装载continuation，没有激活求值，不重扣煤、不重发弹体、不重挖已提交材料。宿主事件和弹体同实例一起恢复；原生FieldSource的数值owner由原生宿主管理，不要求存在对应的语言实例。活动/冻结标志在当前玩家窗口和驻留状态下重新核验。设备与程序身份、燃料快照与库存、现存效果句柄与owner必须一致。过期或已停止的场句柄可以继续留在语言locals中，后续更新正常返回InvalidHandle。

程序资源保存其源文和内容/语义身份，载入重新编译并匹配身份，再恢复运行状态；缺失或不支持的程序版本明确拒绝。旧版树形编辑描述能够保存；**仍有活跃旧树实例时保存明确失败，须先完成或Clear running**。旧树Simulator没有状态导出，本轮不静默丢弃它或从头重放。process/1的Sleeping/Awaiting安全边界完整支持，包括区域冻结实例。

已提交动作回执缓存、观察租约、UI草稿和外部输入不跨进程恢复；运行epoch改变后旧请求失效。动态风只保存策略、参数和已提交相位，恢复背景场，不保存流体扰动/压力等求解器状态，不承诺精确物理重放。

## 旧档导入

没有checkpoint.current、也没有checkpoints目录且存在合法旧world.meta时，按v2–v5读取已有生成/玩家/时钟/风配置，将块复制到新工作目录。旧文件保持原样，首次升级先原子建立显式legacy指针，再创建候选世代，最后发布新指针；中途失败仍能读取旧完整档。升级后缺失指针明确失败，避免静默回退旧库存。下一次成功保存发布checkpoint/1。旧格式未存库存、设备或程序，所以这些域初始为空；v2–v4的deferred欠步保留为余量，从完整环境步重新执行。无效的新指针不会回退旧档或创建新世界。

## 验证

2026-10-23，macOS/Apple M5，本地子模块未改动。独立验证agent在最终代码冻结后执行：

- `cmake --build build-cpu -j4`，`ctest --test-dir build-cpu --output-on-failure`：80/80通过，48.82秒。包括新增Checkpoint、ProcessPersistence和扩展SessionStore测试。
- `build-chunk64` 的Checkpoint、ProcessPersistence、Persistence、SessionStore、GenerationSession、RegionActivity、FieldSources重新构建，`ctest --test-dir build-chunk64 -R '^Pixel\.(Checkpoint|ProcessPersistence|Persistence|SessionStore|GenerationSession|RegionActivity|FieldSources)$' --output-on-failure`：7/7通过，5.50秒。PublicationBudgetAndOwnership的64边长夹具预算由50MiB改为128MiB，保留原内存上限断言；生产预算未改动。
- `cmake --build build --target Pixel PixelRuntimeSmoke PixelCloseSmoke -j4`通过；主agent在Pixel目录执行 `../build/tests/PixelCloseSmoke`，实际窗口/MoltenVK运行退出码0，覆盖PendingStep拒绝关闭、写失败提示、显式修复重试、成功保存和仅析构的GameEnd。环境缺少Vulkan validation layer，此项不替代GPU验证层检查。

真实临时目录用例覆盖：旧代不可变、卸载脏块、发布中断和重试、旧档首次迁移各发布阶段、冻结设备/来源、队列中与已缓存的命中事件、已支付credits与燃料、弹体位置/寿命、已提交开采不重放，以及删除对象后的ID高水位。程序Codec验证未知版本、缺失或不匹配产物、各类locals及大坐标；跨域引用不一致明确失败。

独立review发现旧档升级后指针丢失会静默回退根元数据（P2）。现以迁移前原子legacy指针与缺失指针检查修复；审查者重新链接原复现，确认保存的7份GoldOre不会静默变成0，缺指针明确失败。新增存储及完整会话回归通过，review复核关闭，无剩余阻断项。

Release模式、默认ChunkEdge=1024、256格活动窗口运行 `build-region-release/bin/Release/PixelCheckpointTests` 三次。原始数值保存在[测量CSV](../../Docs/Validation/Checkpoints/capture.csv)。测量位置为包含库存、居民、工作设备、场源和等待回执的活动程序的保存点；各轮使用新的临时目录。

| 轮次 | 捕获与编码（μs） | 世代写入（ms） | 保存总停顿（ms） | 测试进程峰值RSS（bytes） |
| --- | ---: | ---: | ---: | ---: |
| 1 | 30.833 | 24.9792 | 30.9371 | 167641088 |
| 2 | 34.792 | 20.6910 | 26.2189 | 168394752 |
| 3 | 29.792 | 18.8896 | 23.8390 | 167280640 |

该点游戏载荷为1723bytes；总停顿包括世界块写入等待、同步、捕获和发布，RSS是整套测试进程的峰值，不是保存新增内存。三次完整测试的外部monotonic时间为1.218355/0.394966/0.344138秒。此小型夹具不证明大规模存档性能；全量复制仍随已存块数增长。测试模拟写入失败和未发布候选，没有执行系统断电实验。本轮未运行sanitizer或AET，且没有修改引擎。
