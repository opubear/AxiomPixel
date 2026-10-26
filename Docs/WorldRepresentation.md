# 分块、四叉树与流式世界

2026-10-19。本文件是当前 World、风域、存档和发布接口的约定；早期设计文档中的整图数组、移动链、四张固定纹理与游戏周期边界已由本文替代。原算法及成本调查保留在 [MaterialUpdatePerformance.md](MaterialUpdatePerformance.md)，不能用于判断新材料规则的正确性。

## 配置与所有权

| 项目 | 默认值 | 入口 |
|---|---|---|
| 物理块 | 1024×1024 | CMake `PIXEL_CHUNK_EDGE` → `WorldStorageConfig.h` |
| 调度/版本区域 | 64×64 | `storage::RegionEdge` |
| 载荷起始对齐 | 64 KiB | `storage::ChunkAlignment`，首次分配检查系统页大小 |
| 玩家模拟窗口 | 1024×1024 | `WorldStreamSettings::WindowEdge`；游戏环境变量 `PIXEL_SIMULATION_WINDOW` |
| 世界 CPU 预算 | 512 MiB | `WorldStreamSettings::BudgetBytes`；游戏环境变量 `PIXEL_WORLD_BUDGET_MIB` |
| 保存目录 | `Saves/<world-id>/` | `PIXEL_SAVE_ROOT` 或 `WorldStreamSettings::Directory` |
| 地表 / 风边界 | Flat为512/424；Natural为起伏地表/360 | 版本化生成设置 / Environment 面板 `Wind boundary Y` |

块边长和区域边长须为二次幂且整除；游戏窗口为64的倍数，最大2048。预算至少容纳两个物理块及其工作余量；窗口跨块或复杂运动可能需要更多容量。运行时覆盖配置优先于存档设置。不同块尺寸的存档互不兼容。

`World` 编排 `WorldBuffers`、`Environment` 及 `WorldStreamer`。角色归属 Session 下的 `Actors`，通过只读地形视图推进。`WorldBuffers` 的每个版本拥有 `WorldMap`（材料、兼容方向、基础力）、动态风后端与场源；`Environment` 独占模拟版本的写权限，并拥有待提交环境步及材料临时集合；Session的SimulationCoordinator拥有固定步时钟和跨领域提交顺序。World 仅链接 Axiom::Core 和 CPU 性能采样模块，没有窗口、RHI、UI 或纹理类型。

`WorldChunk` 每块拥有独立 Axiom allocator。一份连续、64 KiB 对齐的 `axiom::Array<ChunkPayload, Allocator<..., alignment>>` 分配包含三个 `axiom::StaticArray` 列：32位材料ID、两个有符号8位方向、两个float基础力。有效数据正好14字节/格，默认14 MiB/块；区域计数、活动位与版本单独保存。块销毁同时释放allocator缓存。材料移动只写材料列；方向及基础力始终属于空间位置。

稀疏四叉树的根随驻留块扩展，1024层级关联物理块，细分到64层级；节点聚合材料数量、活动性与版本。插入/卸载重建稀疏索引并回收空分支；首次定位后材料扫描使用块内连续行，不逐格遍历树根。默认预算下只有数十块驻留，暂未引入树节点池或多线程材料计算。

## 坐标、访问与编辑

`CellCoord` 使用 `int64_t`。负坐标按向下取整分块，例如(-1,-1)位于块(-1,-1)的右下格。当前可访问格坐标范围为±2^60之内，避免邻域/树范围运算溢出。`WorldPosition` 保存块坐标及块内double连续坐标，角色和相机移动不先合并成绝对double。GPU只接收相机附近的相对坐标；远处采样使用 `SampleWind(CellCoord, offsetX, offsetY)`，先做整数差值再转浮点。

```cpp
const pixel::CellQuery query = world.QueryCell(-1, 511);
if (query.Residency == pixel::ChunkResidency::Resident)
    auto material = query.Cell->Type;
auto result = world.TryPlaceDirt(-1, 511); // Applied / Blocked / NotReady
world.FillRegion({-64, 0, 64, 64}, {pixel::CellTypes::Empty, {0, 1}});
world.FillBaseForceRegion({-64, 0, 64, 64}, {0, 3600});
```

查询区分Missing、Loading、Saving、Resident和Failed；`TryGetCell`/`TryAtLocal`为空表示不可用，不能解释为空气。强制读取/批量写入未驻留区域会拒绝操作，批量入口先核对整个范围再写。角色将不可用格视为障碍；材料目标必须驻留且在模拟窗口内。

`WorldMapView` 是借用只读视图，带原点、范围、坐标访问、区域版本；Map视图不能跨地图修改/卸载当作快照保存。`MaterialRow` 返回仅在本次稳定访问期间有效的块内连续材料span。写入走Map编辑接口，统一更新版本、数量和邻域唤醒；`GetMutableMap` 和块载荷仅用于CPU集成/批量生成，直接写载荷后必须 `Recount` 和 `Invalidate`。

运行时的材料、基础力、风和场源由 `WorldBuffers` 统一双缓冲；模拟独占写槽，呈现/观测共享上一完整提交的只读租约。详见[固定步与提交](Architecture/SimulationCommits.md)。

`WorldSnapshot` 仅保留为测试/工具显式导出接口，运行时不再调用它。它独立拥有区域副本，按64格区域版本增量复制；后续编辑与卸载不改变已发布内容。调用 `Update` 是下一次显式发布，会替换相应副本；借用的snapshot view有效至下一次发布。原来的 `WorldStateView::Cells` 连续数组接口已移除。

`World(width,height)` 保留有限小地图/非整块测试入口；无参World提供无限Map，游戏会启用流式加载。`Fill`、全图方向/基础力填充只适用于有限地图；无限世界必须指定范围。

## 同步材料规则

固定60 Hz，每步按以下顺序执行：推进当前窗口风场；从步首地图读取候选；同目标按源坐标(y,x)选唯一获胜者；统一提交材料；将成功移动反馈给风场。目标必须在步首为空，因此连续长链每步只向后传播一格空位。方块不保存惯性，每步最多移动八邻域中的一格。

泥土合成速度为 `a_base / 60 + u_wind`，每分量大于30取+1、小于-30取-1，其他为0。兼容方向映射为基础加速度 `3600 * direction`；直接编辑连续力后，方向元数据仍保留最后一次兼容接口写入值。固定地层/矿物不移动；Water向下、斜下或横向铺展，只申请Air目标。Steam与Fire上升、侧向漂移，并以可复现概率消退为Air，Fire遇水也消退。运动和消退与Dirt在同一批次提交，详见[元胞材料模拟](../.awtcontext/GlobalDoc/CellularMaterials.md)。没有每格惯性、压力、热量或体积分数积分；空间力不随材料搬迁。

活动集合以64格区域维护，当前集合取出后，编辑、成功移动、力变化和加载写入下一集合；单格编辑唤醒半径4格内的规则读者，批量移动唤醒相邻区域，覆盖Water的落点搜索范围。没有材料的区域不扫描，静止无依赖变化后休眠；Steam/Fire继续唤醒自身区域直到消退，动态风窗口中的材料区域逐步重新评估。同方向候选属于一一平移，可跳过争用表；混合方向用epoch目标表和显式坐标比较裁决，结果不依赖区域访问顺序。

Session 提供玩家所在的活动中心，World 的模拟窗口对齐64格；活动中心离开中央一半范围后居中。窗口外冻结，跨边界移动阻挡。自由相机只请求显示范围。跳过的游戏帧不推进游戏；暂停仍允许流式加载与相机移动。

## 流式加载、预算与失败

后台单线程只执行生成和块文件读写，主线程在安全边界接纳结果、修改树和释放载荷。玩家窗口优先，其次可见范围，最后一圈块的预取；最多两个并行排队请求。预算不足先取消可取消的预取，再淘汰非必要干净缓存；脏缓存进入保存队列，写出完成后由主线程释放。

预算核算包含块的对齐容量、索引/容器、材料临时数组、双缓冲及仍被读者持有的材料/基础力历史版本、CPU显示plane和I/O工作余量；共享载荷去重，数值风求解器与GPU资源沿用单独计量。每个驻留/排队块保留128 KiB工作余量，避免转入保存队列时临时增加预算；Natural加载请求另预留生成器WorkingBytes（默认256 KiB），直到结果接纳或取消后释放，Flat无额外scratch。材料数组扩容以独立allocator替换，旧池及时释放。发布前预留新旧缓冲共存的峰值，视口过大时缩小发布范围，其他位置显示加载占位；帧和只读版本的生命周期分别计量。

材料扩容若等待缓存写出，只收集保存结果，不能中途接纳新地图数据。真正无法容纳时保留尚未提交的固定步、冻结角色和时钟，并在Environment面板报告等待预算；下一帧重试不重复推进本步风。等待期间流式服务只处理保存完成，不接纳新地图；呈现使用最后完整提交的有界只读视图，见[固定步与提交](Architecture/SimulationCommits.md)。存在延期步时保存、正常退出和重置返回Deferred，不写检查点、不补步；完成该步后需重新发起操作，见[保存边界](Architecture/SessionSaveBoundary.md)。需要更多空间时可减少显示范围/窗口，或用运行时设置增加预算。

加载失败记为Failed，不每帧反复请求；面板 `Retry world I/O` 可重试。保存失败保留脏块与错误，不能淘汰其数据。显式Save失败向调用方报告异常；面板通过TrySave保留并显示错误，允许修复I/O后重试，不终止游戏。正常退出和R重置先保存，R创建新世界且保留旧目录。

## 文件约定

`current.txt` 指向当前world-id，其 `checkpoint.current` 发布完整的不可变世代。每代包含v5 `world.meta`、`gameplay.state`、全部块及校验清单；世界元数据保存生成配置、玩家位置、完整时钟余量和风设置，各游戏域及程序continuation由独立载荷保存。旧v2–v5文件可导入，旧v2映射到Flat/version1。格式解析、工作目录、完整世代校验和指针发布由 [SessionStore](../Pixel/Source/Runtime/Gameplay/Persistence/SessionStore.h) 处理，GameSession在完整提交点捕获各域，全部文件写好后才发布指针，见[多领域一致检查点](../.awtcontext/GlobalDoc/Checkpoints.md)。`chunks/<chunk-x>_<chunk-y>.bin` 仍为版本1小端二进制，后接材料/方向/基础力三列；尺寸、长度、注册材料ID、方向和非有限基础力均校验。

未存在块由世界描述指定的IWorldGenerator生成：Superflat保留y<512空气、其余Dirt；Natural生成土岩、洞穴、矿物、有限水体与干燥草皮。基础力均向下。已有块直接恢复文件，不补水、补矿或重新覆草。卸载、手动保存和正常退出写文件；远处不补算时间。动态风不写盘，重建为所选背景状态；程序风使用世界坐标和持久物理时钟保持相位。生成器、材料纹理与液体接入的现行细节见 [世界生成与自然材质](../.awtcontext/GlobalDoc/WorldGenerationImplementation.md)。

流式写入只修改私有工作目录，检查点全量复制块，因此进程中断或写入失败保留上一完整代；未发布的候选代不参与加载。当前没有fsync，不保证系统断电后的持久性；也未自动回收旧代或工作目录。版本不符、已发布数据缺失或损坏均拒绝读取，不静默重置或回退旧存档。

## 移动风域

`WindDomain` 保存世界原点与尺寸，等于玩家窗口与世界y<boundary的交集。默认boundary=424，地表512之间保留88格无风带。七策略统一接收区域地图视图、背景风和边界模式；数值网格由后端持有。

MAC迁移重叠面速度并投影，自适应网格按覆盖体积迁移，LBM迁移分布函数及lattice时间余量，程序风按整数世界坐标周期及物理时间连续采样；新区域用背景风初始化。离开窗口的动态扰动丢弃，切策略会重建后端而不保存各策略历史。地图缺失区域视为固体。

游戏默认开放边界：NS系列规定流入、开放流出及对应压力条件，LBM使用速度入口/压力出口的[Zou–He关系](https://arxiv.org/abs/comp-gas/9611001)并处理侧边及角点。反向背景风改变入口方向。邻接、插值、平流和脉冲不在游戏模式周期绕回；`WindBoundaryMode::Periodic` 仅作为显式数值测试选项保留。

粗NS仍是有限阻力近似；逐像素/自适应/LBM将材料当分步静态障碍，不实现移动壁面的完整动量耦合。程序风/局部模型不保证无散。自适应重网格及MAC投影存在迁移误差；LBM弱可压缩且有稳定化计数，不能以小型均匀场测试替代所有开放流数值正确性。逐像素、复杂自适应和CPU LBM不承诺实时。

## 发布和显示

帧首从已提交世界只读版本生成渲染格式数据，包含风回溯所需32格边界；不再预生成材料/Air/Field消费者快照。材料、F1方向颜色和纯风均发布区域数据；Draw只消费已发布plane、角色和投影参数。风速仍是signed16×2、1/32格/秒量化，固体使用特殊标记；F1不合成角色/风效。

每个frame slot独立拥有区域纹理、版本和上传缓冲。材料版本、外观版本、显示模式及新区域触发对应64格区域更新；基础力变化不造成相同材料颜色重复上传。稳定区域不重新映射/上传，已有缓存范围内相机移动只改投影。风更新按后端revision失效。窗口改变、slot首次使用或GPU资源重建会重新同步。

本地Axiom新增通用 `RHITextureCopyRegion`、`WriteRegion`、`CopyBufferToTextureRegion`，校验源偏移、目标矩形、层范围和容量，旧整图入口保留。Pixel缓存/shader/smoke均在父仓库；AET保持独立构建。

## 验证与测量

独立同步参考、随机逐步对照、负坐标/跨块/非整块/对齐/释放、存档失败与恢复、预算延期、不同块尺寸、七策略开放边界/平移、区域缓存及RHI范围校验由CPU/mock回归覆盖。±2^55远坐标专项风测试另经过ASan/UBSan。具体最终命令、数据、GPU限制及审查记录见 [实施验证与性能](WorldRepresentationValidation.md)。

运行时Performance采样记录 `material_cells_scanned`、`active_regions`、`world_upload_bytes`、`wind_upload_bytes`、阶段中位/P95所需原始CSV及驻留容量；Environment面板显示驻留/排队块和预算占用。统计是所有权/预留容量，包含工作余量，不等于RSS。
