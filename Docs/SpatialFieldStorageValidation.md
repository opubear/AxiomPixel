# 空间场存储验证与性能

2026-10-29，Apple M5 / macOS，系统页大小 16384 字节。改造前基线为 `8b3bd530e37786da5454ea33c8be28814bec216f`，改造后为本次工作区实现。合同与覆盖范围见 [SpatialFieldStorage.md](SpatialFieldStorage.md)。引擎子模块未修改。

## 正确性与边界

| 验证 | 最终结果 |
|---|---|
| 默认 1024 chunk，Debug CPU 全量 | 117/117，77.35 秒 |
| 64 chunk，Release 专项 | 11/11，9.02 秒 |
| ASan + UBSan 空间存储、世界缓冲、生成、风策略/风域、区块 | 6/6，57.69 秒 |
| Pixel 与 PixelRuntimeSmoke 原生目标 | 构建成功 |
| Apple M5 原生元素专项 | 通过；水/蒸汽/火运动、alpha、变化区域上传 |
| Apple M5 最终 `--fields-only` | 通过；风模式/折射/遮罩/frame slot，F1方向/翻页/动态风/材料块与窄堆叠 |
| Apple M5 最终 `--lifecycle-only` | 两轮通过；纹理上传、alpha、缩放/裁剪、resize/minimize恢复 |
| Apple M5 完整 PixelRuntimeSmoke | 中途版本通过；最后的连续行打包版本两次停在批量鼠标输入断言，见下文 |

全量 CPU 命令：

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j2
```

64 格专项：

```sh
cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-chunk64 --target PixelSpatialStorageTests PixelGenerationTests PixelLiquidTests PixelGenerationSessionTests PixelTerrainAssetTests PixelTests PixelChunkTests PixelPersistenceTests PixelWindDomainTests PixelWorldBufferTests PixelMemoryTests -j4
ctest --test-dir build-chunk64 -R '^Pixel\.(SpatialStorage|Generation|Liquids|GenerationSession|TerrainAssets|Cpu|Chunks|Persistence|WindDomains|WorldBuffers|Memory)$' --output-on-failure
```

消毒器：

```sh
cmake -S . -B build-storage-sanitize -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build-storage-sanitize --target PixelSpatialStorageTests PixelChunkTests PixelWorldBufferTests PixelWindDomainTests PixelWindStrategyTests PixelGenerationTests -j4
ctest --test-dir build-storage-sanitize -R '^Pixel\.(SpatialStorage|Chunks|WorldBuffers|WindDomains|WindStrategies|Generation)$' --output-on-failure
```

原生验证使用 `cmake --build build --target Pixel PixelRuntimeSmoke -j4`，再从 `Pixel/` 运行 `../build/tests/PixelRuntimeSmoke`。连续行打包优化前，一轮完整 smoke 退出码为 0，包含两轮 resize/minimize 恢复。最后版本的两轮全量复跑均在 `BasicWorldGameplay` 的 `batched clicks used the later cursor position` 断言结束，之前的库存、人物、地形、元素、背景、风可视化和木头人阶段通过。输入代码和原断言没有改动；失败原因尚未确认，不能把中途通过冒充最终完整回归通过。

新增 `--fields-only` 入口直接运行原有 `WindVisualizationGpu` 与 `ForceFieldDebugGameplay`，用于独立核验本次变化；默认全量入口继续执行原输入断言。Vulkan validation layer 在本机不可用，故不声明 validation layer 检查通过。

本轮发现并修复的回归：

- 最初生成器整图临时副本超出原 256 KiB 工作预算；改为直接写逻辑字段，恢复原有工作预算。
- 仅替换容器会让压力循环重复换算 tile 地址，出现明显退化；将矩阵内层改为连续行邻域，边缘保留一般路径，PCG 归约顺序保持不变。
- 运行时页容量参与跨编译单元全局初始化，会导致加载预留读到零值；改为函数内初始化。64 chunk 的紧预算场景同时验证加载和驻留的索引预留一致。

中途一轮并行测试在会话中断附近有三个超时（两项未改动引擎测试与 Pixel.Cpu）；三项单独复跑通过。上表是后续完整重跑的结果，不将超时算作通过。

## 测量方法与范围

改造前先构建 Release 并保存三个独立基准程序；改造后使用相同编译配置及原有基准源码。基准之间串行执行，不与构建、消毒器或 GPU 回归并行。每组交替运行 before/after 三轮，以三轮结果的中位数比较。

```sh
cmake -S . -B build-storage-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-storage-release --target PixelRegionBenchmark PixelWorldBufferBenchmark PixelWindStrategyBenchmark -j4
build-storage-release/bin/Release/PixelRegionBenchmark /private/tmp/regions.csv 60
build-storage-release/bin/Release/PixelWindStrategyBenchmark 128 12
build-storage-release/bin/Release/PixelWorldBufferBenchmark
```

Region 场景为 1024×1024，预热 8 步后测 60 步；Wind 为 128×128、12 步、空场/墙/狭缝；WorldBuffers 使用 512 域、8 步预热与64个计时样本，包含提交和 F1 数据打包。数值风不计入 WorldBuffers 的 World 预算，另由风组件计量。这些时间是 CPU 墙钟时间，没有读取硬件 cache-miss 计数，不能据此声称达到最大缓存命中率。

### 三轮结果

原始数据：[区域更新](SpatialStorage-regions-2026-10-29.csv)、[六种风策略](SpatialStorage-wind-2026-10-29.csv)、[提交与F1打包](SpatialStorage-buffers-2026-10-29.csv)。各行保留版本、轮次、原基准全部计数、容量与数值输出。区域材料 hash、F1 checksum 在所有对应轮次中相同；风速、散度、质量和稳定化计数在基准输出精度内相同。

策略编号为 0 粗风、1 参考、2 程序、3 Hybrid、4 Adaptive、5 LBM、6 Empty。区域场景编号为 0 静态、1 单粒、2 密集。

| 代表路径 | 改造前 | 改造后 | 耗时变化 |
|---|---:|---:|---:|
| 参考压力风 / 墙体，ms/步 | 19.4682 | 11.6401 | −40.2% |
| 参考压力风 / 狭缝，ms/步 | 41.3289 | 24.6765 | −40.3% |
| 粗风 / 1个读者，提交+打包，ms | 13.2324 | 11.6719 | −11.8% |
| 粗风 / 3个读者，提交+打包，ms | 28.7323 | 24.4632 | −14.9% |
| 程序风 / 1个读者，提交+打包，ms | 11.6317 | 10.2017 | −12.3% |
| 程序风 / 3个读者，提交+打包，ms | 26.3323 | 21.3765 | −18.8% |
| 运动材料 / 1个读者，提交+打包，ms | 0.47446 | 0.30896 | −34.9% |
| 密集材料区域更新，ms | 11.5963 | 12.2924 | +6.0% |
| 单粒材料区域更新，µs | 3.500 | 4.708 | +34.5%（+1.208 µs） |
| 空场参考风，ms/步 | 0.49969 | 0.67113 | +34.3%（+0.17144 ms） |
| LBM / 墙体，ms/步 | 2.13651 | 2.35745 | +10.3% |

静态休眠区域约 0.083 µs，基本不变。粗风/Hybrid 单独推进约增加 1%–3.4%，程序风增加 7.7%–12.4%，Adaptive 增加 3.7%–5.4%，LBM 三场景增加 9.6%–12%。空风无材料的提交本来仅约 4.5–6.4 µs，本轮增加约 0.5 µs。无读者程序风完整提交增加约 8%。这些退化仍存在，不将分块和页对齐等同于所有 CPU 访问都更快。

原有全图扫描参考基准仍以一次跨整个 1024 格宽行的逻辑视图逐格访问，增加约31%；它用于核对同步规则，未改成 tile 遍历。实际活动区域更新与场图打包已消费局部行；未来大范围扫描应使用 RowSegment/ForEachGridRow，避免在标量循环反复换算逻辑地址。

早期直接迁移后的 F1 打包曾慢 17%–19%。最终改为有界只读力行，保留缺失/跨块回退，得到上表的改善。压力路径采用连续 tile 内邻域，保留原归约和边界数值；没有为追求单一基准降低迭代精度或移除兼容检查。

## 已知边界

- 新存储默认页/块填充会增加某些小字段容量；World 的索引 reserved 也包含稀疏树最坏路径的预算余量，不能把 reserved 当作 RSS。
- 现有 WorldChunk COW 粒度仍为整个物理块；写槽复用阶段才按 tile/列增量复制。
- GPU 验证覆盖现有显示上传与读回；本次没有实现 GPU 物理模拟 kernel，也没有测设备端模拟带宽或 cache hit。
- 本机验证不代表 Windows/Linux 或独立显存设备的运行验证。传输描述与容器不假定 CPU/GPU 任意结构的 ABI 自动相同。
