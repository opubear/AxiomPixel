# 游戏空间单位

游戏连续空间使用 **32 位 `float`，单位为厘米**，一格固定为 **50 cm**。
`Runtime/Simulation/World/WorldUnits.h` 提供 `CentimetersPerCell`、
`CellsToCentimeters` 和 `CentimetersToCells`，渲染倍率不改变这个物理尺度。

## 坐标和运动

`WorldPosition` 保留整数 `Chunk`，`Local` 为块内 `std::array<float, 2>` 厘米坐标。
`FromAbsolute` 和 `RelativeTo` 的浮点输入/输出均为厘米；`Cell()` 返回整数世界格。
默认 1024 格的块宽 51,200 cm，归一化后 `Local` 位于 `[0, 51200)`，负坐标向下分块。
大世界通过整数块寻址，不能把远处绝对坐标先合并成 `float` 再相减。
默认块内最大的相邻 `float` 间距约为 0.00390625 cm（0.0391 mm），不随区块离原点的距离增大。

`Body::X/Y`、`BodyState::VerticalSpeed`、相机位置和位移都使用 `float`。
时间、固定步累加器、无量纲比例和通用数值算法不受空间单位迁移影响。

| 人物参数 | 厘米制值 | 原玩法尺度 |
|---|---:|---:|
| 宽度 / 高度 | 100 / 200 cm | 2 / 4 格 |
| 横向速度 | 800 cm/s | 16 格/s |
| 跳跃初速度 | 向上 1600 cm/s | 向上 32 格/s |
| 重力 | 向下 4500 cm/s² | 向下 90 格/s² |
| 最大下落速度 | 4000 cm/s | 80 格/s |
| 碰撞最大分段距离 | 12.5 cm | 0.25 格 |
| 接地探测距离 | 0.5 cm | 0.01 格 |

人物仍每秒执行 60 个固定步，内部使用最多 1/120 秒的子步；水中减速、浮力和阻尼保持原参数。
`float` 运算有舍入误差；速度验证使用厘米容差，不再要求原有 `double` 的 1e-10 精度。

## 格子、渲染与兼容边界

地形存储、材料单元、流式窗口、世界生成、整数区域及设备占格仍以格为索引。
`FromCell`、`FromCells`、`FromChunkCells` 把格坐标转换为游戏位置；
`RelativeCellsTo` 把游戏位置转换为某个整数格参考点附近的格坐标。
这些命名明确的适配器是厘米与既有网格接口之间的边界。

`GridView2D::CellSizePixels` 继续表示每格的渲染像素数。
投影先把相机附近的厘米距离转换为格距离，再乘此倍率；鼠标连续选点返回厘米位置，
整数选格返回 `CellCoord`。人物素材的 `PixelsPerCell` 仍表示纹理像素/格。

既有 GameAPI/LLM 的整数格及格内 offset 协议、Formula 的 `cells(...)` 与速度/场参数、
网格风/材料求解器保留其明确的格单位合同。构式实体的位置存储为 `WorldPosition`，
通过 `FormulaWorldAdapter::OffsetCells` 等边界转换；不把既有脚本数字直接解释成厘米。
网格协议和通用求解器的 `double` 计算不会改变人物/相机的 `float` 厘米存储。

## 存档

`world.meta` v2–v6、`PixelGameplay` v1–v3 和构式检查点保留原有格单位磁盘格式。
读取位置与人物竖直速度时乘 50，写出时除 50；转换前按旧协议读取数值，转换后验证有限性和坐标范围。
旧地图无需重采样；旧人物不会变小、移动到另一格，恢复时保持相同运动尺度。
旧 `double` 小数读取后按 `float` 厘米精度舍入，超出新表示范围的值会拒绝加载。

## 验证入口

`Pixel.WorldUnits` 覆盖类型约束、负坐标、远处块边界、毫米级局部位移、跨块行走、
30/60/144 帧输入下的 60 步 800 cm 位移、独立模拟线程与人物呈现一致性，以及旧存档单位转换。
原有 Body、Actors、Camera、Input、PixelScale、Formula、Persistence 和 SessionStore 测试继续验证对应接口。

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
```

2026-11-01 验证记录：CPU 套件共 122 项，迁移后修正旧单位测试数据及边界容差，失败项复测全部通过。
另以 `PIXEL_CHUNK_EDGE=64` 验证 WorldUnits、Body、Camera、PixelScale、Persistence 和 SessionStore，六项全部通过。
30/60/144 FPS 下均执行 60 个固定步，实测横向位移 799.922 cm，与设定 800 cm 的差值约为 0.078 cm。

正式 `Pixel` 和 `PixelRuntimeSmoke` 编译通过。原生 GPU 测试覆盖人物、地形、风场、世界交互、
相机、背包、构式、花园、调试 UI，以及两轮 resize/minimize/shutdown 生命周期，全部通过。
全量进程连续创建窗口后出现一次鼠标无法到达 UI 控件的环境问题；该组及后续组以独立进程复测通过。
本机未安装 `VK_LAYER_KHRONOS_validation`，本次 GPU 结论来自实际图像读回和交互断言，不包含 Vulkan 验证层检查。
