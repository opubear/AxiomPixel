# 世界改造验证与性能（2026-10-19）

实现契约见 [WorldRepresentation.md](WorldRepresentation.md)。材料行为以新的同步参考为准，旧移动链只保留历史性能调查；本报告不会把旧/新规则的耗时差异当作等价算法收益。

## 实施范围

完成WorldState→WorldMap、独立对齐SoA块、稀疏四叉树/64格活动与版本、三阶段材料更新、int64及分段连续坐标、CPU预算、后台文件流式、存档恢复/新世界、移动开放风域、区域快照和每slot纹理缓存。局部Axiom的通用RHI区域上传修改已提交到PixelMain `150ea98`，AxiomVersion.json与父仓库gitlink同步；未推送远端。

## 可重复基准

Apple M5，macOS arm64，Clang，CMake Release，1024×1024有限地图，无风，不含角色、文件I/O或GPU。每种配置先预热8步，再测40步，独立进程重复3轮；表中时间是三轮各自中位数/P95的算术均值。原始数据：[round-1](Performance/2026-10-19-regions/round-1.csv)、[round-2](Performance/2026-10-19-regions/round-2.csv)、[round-3](Performance/2026-10-19-regions/round-3.csv)。

| 场景 | 新规则全扫描 median / P95 ms | 区域版 median / P95 ms | 区域版访问格 / 活动节点 |
|---|---:|---:|---:|
| 静止平坦地面 | 4.08981 / 4.22819 | 0.000083 / 0.000111 | 0 / 0 |
| 单格下落 | 4.05643 / 4.19539 | 0.002444 / 0.003473 | 4096 / 1 |
| 密集交替行 | 6.34037 / 6.64120 | 6.66468 / 10.76412 | 1048576 / 256 |

全扫描每步访问1048576格。三种场景及三轮最终材料hash分别完全一致；独立随机参考还逐步对照80步（129×97非整块地图，混合八方向和争用）。密集调度中位耗时增加5.11%，达到不超过10%的目标。同方向平移候选无需目标争用表，是这一热点的局部优化；混合方向仍使用坐标优先级裁决。

静止极短计时接近时钟/调用底噪，不能据此推算整帧FPS或宣称48000倍游戏加速。区域版密集场景第一轮P95为18.0967ms，另两轮为7.18617/7.0095ms，三轮均值10.76412ms，原始数据保留，未删除尾部样本；当前证据只支持中位开销目标，不能声称P95同样在10%以内。CSV reserved_bytes为World持有容量，不包括全扫描工具自身的Winners/Proposals；区域生产World约14.03 MiB静止、30.03 MiB密集（包含其scratch），不能用这两列直接比较参考工具的全部内存。

```sh
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelRegionBenchmark -j4
./build-region-release/bin/Release/PixelRegionBenchmark /private/tmp/pixel-regions.csv 40
```

## 正确性与资源检查

- `Pixel.Chunks`：正负块边界、14字节SoA、64 KiB地址对齐、独立snapshot/卸载、有限padding、不同Map身份、随机同步参考、静止休眠、力编辑唤醒及动态风仅重评其覆盖区域、文件往返/截断拒绝、预算/写失败保留，以及预算延期后只提交一次。
- `Pixel.Persistence`：负坐标内容/基础力/兼容方向、玩家分段小数、设置及时间余量往返；程序风运行后切入再保存相位；R保留旧存档；50 MiB发布预算与frame独立计量；预算延期时保存/恢复；手动保存元数据失败后继续保留地图并重试。
- `Pixel.WindDomains`：七策略均匀/反向/纵向/零风、障碍、开放边界、净通量、LBM质量与lattice余量、扰动迁移、空域恢复和±2^55坐标的相邻采样/1格及64格迁移。程序风/LBM重叠误差阈值1e-6，投影/重网格后端为1格/秒RMS；属于测试容差，不承诺所有场景等精度。
- 原有Gameplay、相机、输入、F1、frame slot、资源重建及风数值CPU回归保留，改用新坐标/区域接口。区域上传mock在130×70图、三个slot预热后测得稳定帧新增上传0字节；(65,65)编辑只提交64×6×4=1536字节的边缘区域，基础力元数据变化不重传材料plane。Axiom VulkanBufferMock包含合法区域上传及源/目标/层/容量越界拒绝。
- 远坐标风专项以独立ASan+UBSan构建运行，严格警告检查通过；没有将此声明扩大为整套GPU或全项目sanitizer验证。

最终构建与测试结果：

| 验证 | 结果 |
|---|---|
| 完整CPU/mock CTest | 45/45通过，55.26秒 |
| PIXEL_CHUNK_EDGE=64 的Chunks/Persistence/WindDomains | 3/3通过，2.03秒 |
| Pixel、Axiom(AET)、PixelRuntimeSmoke、AxiomLifecycleSmoke构建 | 通过 |
| Apple M5真实Pixel GPU smoke | 完整通过：风效、基础玩法、F1、UI和两轮生命周期（含缩放/最小化恢复） |
| Apple M5 AET `--skip-minimize` | 两轮通过；不覆盖最小化/恢复 |

## 独立审查

按code-design-style使用干净上下文的独立subagent审查并交回同一审查者复核。首轮确认并修复：分配后才统计预算；程序风存档使用不同时间轴；ARM64远坐标先转double丢失相邻格。后续还修复了脏缓存写出期间预算回退、缩小视口旧snapshot峰值、延期固定步存档与小块对齐统计。

审查者独立probe：原50 MiB场景降至50,677,824字节，程序风往返差值0，2^55各格局部/世界采样一致；3脏块压力下成功保存后提交，写失败则保留脏数据/延期；双publisher和18次大/小视口切换均在预算内；World先销毁再销毁帧/发布器无崩溃。最小帧、有限地图padding、对齐计量、时间兼容、手动保存错误处理、风域唤醒范围和新UI测试再次交回同一审查者复核，无未决实质意见。

## 边界与限制

没有承诺CPU逐像素、复杂自适应或LBM实时运行。预算是所有权和保守工作预留，不是RSS；风求解器和GPU另计。远处冻结，不补算离线时间；动态风不写盘。简单文件存档没有跨文件断电恢复。

真实GPU使用MoltenVK；本机Vulkan validation layer不可用，像素读回不能替代完整validation。最初沙箱运行不能连接macOS窗口服务，改在有窗口服务的宿主环境执行。Pixel本机完整smoke含最小化恢复两轮通过；AET本轮显式跳过最小化，范围不同。历史RTX/X11完整Pixel smoke的 `no drawable 2D frame` 生命周期失败保留为历史记录，不能与本轮数值fixture失败混同，也不能宣称历史问题已修复。


最终命令（完整输出位于本次工作环境的/private/tmp/pixel-final-*.log，关键结果与CSV已写入本报告）：

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
cmake --build build-chunk64 --target PixelChunkTests PixelPersistenceTests PixelWindDomainTests -j4
ctest --test-dir build-chunk64 -R 'Pixel.(Chunks|Persistence|WindDomains)' --output-on-failure
cmake --build build --target Pixel Axiom PixelRuntimeSmoke AxiomLifecycleSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
(cd Axiom && ../build/tests/AxiomLifecycleSmoke --skip-minimize)
```

GPU旧窄堆测试的固定采样点在开放边界下角差仅0.002224rad；同场景邻域峰值为0.123702rad（527,371），旧周期模式原点为0.181498rad。测试保留>0.1阈值，改验限定邻域峰值并核对该峰GPU像素；没有降低物理/显示断言。UI测试改用真实InputScalar、控件ID/实际矩形定位和平台输入，精确验证-2147483649及9007199254740993。`--debug-ui-only`仅提供显式诊断入口，默认全套顺序保留。
