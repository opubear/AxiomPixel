# 世界空间场的统一存储

2026-10-29。实现位于 `Pixel/Source/Runtime/System/Memory/`，只依赖 C++ 标准库和系统页大小查询。Simulation 使用它管理空间数据，Render 使用它管理交换缓冲；World 不依赖 RHI。它替代旧的单个 64 KiB 对齐 `ChunkPayload` 数组。逻辑材料、数值精度、模拟窗口和文件格式保持原有约定。

## 所有权、对齐与容量

`PageBuffer<T>` 负责唯一所有权、分配、拷贝、移动和释放。底层起址按 `max(系统页大小, 128, alignof(T))` 对齐，申请字节数向系统页大小取整；释放后不保留池缓存。系统页大小来自 POSIX `sysconf` 或 Windows `GetSystemInfo`，不把某台机器的 4 KiB/16 KiB 写死为全平台规则。

`ChunkedGrid<T, TileEdge>` 在其上管理二维场。T 必须可平凡复制、可平凡析构并具有标准布局；每个元素严格满足 `alignof(T)`。材料为 uint32，兼容方向为对齐 2 字节的两个 int8，力与风速为对齐 8 字节的两个 float。每个元素不单独占一页或一条缓存行。

`UsedBytes()` 只计有效样本；`ReservedBytes()` 包含 tile 填充和尾页。World 的预算另计 `ChunkPayload` 控制对象、各列版本、快照及 COW 峰值，共享载荷按身份去重。四叉树的 used 计实际节点，reserved 按每块完整局部子树与最大根路径预留，排队加载与驻留采用同一口径，避免接纳加载时才出现索引费用。页容量使用函数内初始化，避免全局初始化顺序产生零预留。对齐不等于锁页，不承诺物理连续内存、huge page 或设备可直接导入。128 字节是容器选定的 tile 对齐合同，不是假设所有 CPU 的 cache line 都为 128 字节。

## 物理布局与四叉树

默认 tile 为 **64×64 个样本**，tile 之间按二维行序排列，tile 内 X 连续。整张场不足一个 tile 的维度按实际尺寸收紧；跨越多个 tile 时边缘 tile 保留填充。tile 步长以 128 个样本为单位取整，保证不同 T 在相同尺寸和 tile 配置下拥有相同的样本地址次序。tile 起址至少 128 字节对齐，行与元素满足 T 的对齐；不额外把每一行都扩到 128 字节。

材料、兼容方向和基础力继续采用分列表示。默认 1024×1024 WorldChunk 内有 16×16 个 tile；四叉树的 64×64 叶节点恰好对应每列的一个连续 tile。默认三列有效数据仍为每格 14 字节，共 14 MiB；小于系统页的列会增加页尾填充，预算使用实际容量。

活动叶扫描直接取得材料行和力行，避开逐格查树和读取无关方向列。双缓冲分别维护材料、方向、基础力版本，复用写槽时只同步改变的 tile/列。首次对共享载荷写入仍执行**整个 WorldChunk 的 COW**；本次没有引入逐 tile 的引用计数或 COW。

F1 场图打包也借用有界只读视图的基础力行，在行内合成风和场源；一行只校验一次驻留和视图边界。缺失块、有限世界边缘或跨物理块的行走原有逐格路径，保留加载占位和裁剪语义。

四叉树仍管理世界驻留、调度和失效。风后端自己的局部求解网格不新增一棵世界树；其 tile 按样本而非逻辑像素计。粗分辨率字段若需要和世界 64 格叶边界精确对应，可以选择 `TileEdge = 64 / sampleSpacing`，前提是所得边长为二次幂。

## 上层访问合同

- `At(x,y)`、逻辑行序 `operator[]` 和迭代器隐藏实际布局；检查边界的入口是 `at`、`AtWorld` 和行视图接口。`At`、`Address` 与 `operator[]` 是要求坐标有效的快速访问。
- `Row` 可以跨 tile；`RowSegment` 返回一个 tile 内的连续 span。视图只在字段未被修改、移动或重新分配期间有效。
- `ForEachTile` 暴露有效范围，`CopyTileFrom` 同步局部块。`ForEachGridRow` 把地址换算移到标量内层循环之外，并保持原先的全局行序归约。
- `GridAddress` 是不透明的临时样本地址，允许相同尺寸、相同 TileEdge 的不同字段复用邻居定位；不同尺寸或布局之间不得复用，也不得序列化。重置尺寸后重新取得地址。
- 字段不提供假装整图连续的 `data()` 或隐式 span。`CopyToRows`/`CopyFromRows` 用显式行步长跨越布局边界；更换 TileEdge 不改变其结果。

压力求解器的矩阵乘法在 tile 内按连续行读取五点邻域，只有 tile 两侧及世界边界走一般地址路径。点积仍保持全局行序，未改变收敛容差或迭代上限。

## 采样分辨率与世界坐标

`GridGeometry` 独立记录 int64 原点和正的有限 double 采样间距，支持非整数间距。世界坐标先求整数差，再转换计算采样格，避免 ±2^55 附近相邻像素丢失精度；负方向使用 floor。

例如一个力元素覆盖 4×4 个逻辑像素，64 个样本覆盖 256 格宽的世界区域：

```cpp
memory::ChunkedGrid<ForceVector, 16> force(64, 64, {0, 3600},
                                         {originX, originY, 4, 4});
const auto value = force.AtWorld(originX + 7, originY + 9); // 样本 (1,2)
```

这里 16×16 样本 tile 覆盖 64×64 逻辑像素。通用存储负责覆盖关系；插值、MAC 面偏移和边界条件由求解器负责。当前游戏的基础力仍保持逐像素语义，粗风场使用自身原有 SpacingX/Y；它们不会因为更换容器而自动改变采样密度。

## 覆盖范围

| 数据 | 当前存储 |
|---|---|
| WorldChunk 材料、方向、基础力 | 三列 ChunkedGrid |
| 独立 ForceField、WorldSnapshot 区域 | ChunkedGrid |
| 粗风的速度、压力、散度、阻力和覆盖计数 | ChunkedGrid，附原有采样间距 |
| 逐像素参考风的速度、压力、PCG 工作场与障碍掩码 | ChunkedGrid |
| LBM 分布、下一步分布、速度、障碍 | ChunkedGrid；CPU 保留 D2Q9 单格记录 |
| 程序风脉冲、Hybrid 的局部几何、Adaptive 的像素归属/障碍 | ChunkedGrid；Empty 无空间载荷 |
| 逐格材料争用 winner/epoch 临时表 | ChunkedGrid |
| 渲染材料/风交换 plane、自适应速度迁移临时 plane | PageBuffer，显式行序交换 |

自适应求解器的叶节点/面图和其按节点索引的工作向量、BFS 队列、可变长材料提案及场源列表是非规则拓扑或事件集合，不伪装成二维场。

## GPU 交换边界

支持三种明确选择：

1. `StorageBytes()` 加只读 `GridLayout`：直接导出 CPU tile 布局。描述包含元素大小/对齐、tile 尺寸、有效范围、行步长及 tile 步长；shader 按描述寻址，只处理有效格。描述本身不是可直接上传的 C++ shader struct。
2. `GridTransfer<T>`：拥有页对齐的行序 staging，行 pitch 可由调用方传入设备约束，默认 256 字节。不同 tile 布局可通过它交换；`CopyTo` 校验尺寸和世界域，避免把旧域结果写入移动后的风场。
3. `PlanarGridTransfer<Scalar,N>`：把 `std::array<Scalar,N>` 记录导出为分量平面，支持独立行/平面对齐，适用于 D2Q9。相邻 GPU lane 处理相邻 X 样本时可连续访问同一分量；CPU 可以继续使用自己的格内记录布局。

上传的 staging 或直接导出时的只读租约必须保留到 GPU fence 完成。修改后的数据通过显式读回写入拥有写权限的字段，并由 World 编辑/Invalidate 接口发布版本。容器不自行跨线程同步、不启动 GPU kernel，也不替代 GPU 内存分配器。当前 Render 的 row-major plane 已使用 PageBuffer；尚未接入 GPU 模拟后端。

不能仅凭 C++ `alignas` 推断任意 shader ABI；后端应明确标量格式、结构 stride 和设备限制。参考 [Vulkan shader 内存布局](https://docs.vulkan.org/guide/latest/shader_memory_layout.html)、[Vulkan 设备地址对齐](https://docs.vulkan.org/guide/latest/buffer_device_address_alignment.html)及 [CUDA 合并访问建议](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html)。

## 存档与验证

区块文件继续使用 v1 头部和按全局行序排列的材料、方向、基础力三列；读取和写入逐行转换，不把 tile 填充或 C++ 控制对象写入文件。测试直接按旧偏移读取文件字节，避免仅靠同一套新编码器/解码器往返而漏掉兼容问题。

`Pixel.SpatialStorage` 覆盖页/元素/tile 对齐、部分 tile、窄字段、不同 TileEdge 互传、分量平面、GPU pitch、越界/溢出、4×4 覆盖、分数间距、远负坐标、深拷贝、复用、移动和释放。WorldBuffers 专项检查各列独立变化、跨 tile 更新和慢读者隔离。实测命令及性能取舍见 [SpatialFieldStorageValidation.md](SpatialFieldStorageValidation.md)。
