# 分层群落世界生成（Natural v2）

2026-10-29。新建游戏默认 Natural v2，F2 可选择 Natural 或 Superflat，种子决定同一完整世界；R 创建新世界。当前应用的临时世界不保存进度，显式持久化会话仍支持版本化保存。既有 Natural v1 与 Flat v1 的生成结果不变。

## 分层规则

世界生成只依赖种子、版本和整数世界坐标。气候不是运行时天气，而是决定初始环境的稳定场。

| 层 | 采样尺度（逻辑格） | 控制内容 |
|---|---|---|
| 环境 | 温度/湿度 4096、抬升 2048 | 森林、沙漠、雪山的连续 Q16.16 权重，和为 65536 |
| 大地形 | 大陆起伏 2048、坐标扰动 1024 | 群落共同的地势与地形变化位置 |
| 地形细节 | 丘陵/山脊 512、沙丘 128、细节 64 | 林地丘陵、低缓沙丘、高山岩脊；按群落权重混合高度 |
| 地层 | 厚度 128、材质斑块 128×64/128×32/64×32 | 厚土、黏土透镜、厚沙/砂岩、薄雪与浅层冰体；交界用二维噪声交错 |
| 洞穴/矿物 | 独立通道，沿用现有尺度 | 林地洞室更宽、山地裂隙更窄，阈值平滑混合；五种矿石保留深度门槛 |
| 水与植被 | 有限水体特征网格 512、树木网格 128、林密度 512 | 林湖、低概率绿洲、地下封闭积水；有疏密变化的树干与树冠 |

温湿度与抬升噪声在第二轴使用固定非格点偏移，避免一维采样恰好落在 Perlin 格点行。高度在选择离散材质之前按权重混合，不会因为群落标签改变而形成垂直断崖。地层通过二维噪声交错过渡，深度 248 以下统一进入基础岩层及既有矿物/洞穴。

群落参数集中在 `Pixel/Source/Runtime/Simulation/World/Generation/BiomeTerrain.cpp`。`WorldGenerator.cpp` 编排地形、湖盆、地层、洞穴、矿物、有限水、草皮和树木，公开 `SampleGenerationColumn` 的环境权重及 `SampleGeneration`/`PlanGenerationWater` 诊断接口。Simulation 不依赖 Render、Gameplay 或纹理 JSON。

## 行为与边界

- 森林：土层与黏土、浅层苔石；林密度场决定疏密。树木为可开采的真实方块，树干阻挡角色，树叶可穿过且不挡风。
- 沙漠：固结沙层、砂岩，绿洲候选减少到常规概率的 1/8。沙土当前为固定地层，不具有逐粒下落模拟。
- 雪山：高山岩脊、雪层及冰体；地表液态湖候选被拒绝。积雪和冰是静态材料，本轮没有融化、结冰或天气循环。
- 湖泊与地下水沿用完整、有界的封闭性检查，开采后不会自动补水；水开关只关闭水体。新增矿石只能替换岩层，不填洞穴、不替换冰体。
- 树木按完整特征锚点计算，每个查询带 22 格横向余量，写入与当前区块的交集；竖向裁剪不改变树冠。树木不会覆盖土岩或水，不包含生长、落叶或砍树后整体倒塌模拟。
- 出生点 x=512 保留 33 格平整平台和过渡区，湖泊和树木避让。平台可采用所在群落的材质。
- 地表草皮沿用现有 exposed-surface 呈现规则；群落背景图仍使用原有山林视差背景，本轮变化是可交互的前景世界。

## 材料与资源

| 材料 | CellTypeId | 纹理层 | 开采产物 |
|---|---:|---:|---|
| Sand / Clay | 22 / 23 | 13 / 14 | Sand / Clay |
| MossStone / Sandstone | 31 / 32 | 15 / 16 | Stone / Sandstone |
| Snow / Ice | 60 / 61 | 17 / 18 | Snow / Ice |
| Wood / Leaves | 70 / 71 | 19 / 20 | Wood / Leaves |

新增物品 ID 为 1070～1130，原有材料/物品 ID 保持不变；背包、存档、开采和 UI 已接通。当前普通放置工具仍放泥土，未新增各类材料的建造选择器。

五张新原图由内置 image_gen 生成，沙土/黏土/苔石复用现有素材；完整提示词、原图与 64×64 制品见 [RawData 素材说明](../RawData/Materials/Biomes/README.md)。运行时只读 `Pixel/Content/Art/Biomes`，由 `ArtAssets.json` 映射，21 层包含透明空气层。Content PNG 按既有规则被 Git 忽略，分发时需同步原文件；`SOURCE.json` 与 RawData 制品用于校验/恢复。

恢复新增运行时素材：`python3 RawData/Materials/Biomes/install_runtime.py`。原有 NaturalBlocks、Elements、角色和背景素材仍遵循各自的现有复制流程。

## 版本与复现

Natural v2 只接受 Version=2，Natural v1 / Flat v1 继续保留。应用默认值与 F2 选择 Natural 使用 v2；底层 `WorldGenerationSettings` 默认 Flat v1，以保留 CPU 有限测试夹具。metadata 指纹包含版本；读档继续尊重存档的生成版本，不把 v1 旧世界中尚未生成的区块切到 v2。后续改变 v2 参数需要增加版本。

种子 42 的预览中心：森林 x=-14720、沙漠 x=-10848、雪山 x=-15792。群落混合区可能出现在截图边缘，这些是实际世界坐标采样。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4
./build-cpu/bin/Debug/PixelGenerationPreview /private/tmp/pixel-biomes-preview

cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-chunk64 --target PixelGenerationTests PixelGenerationSessionTests PixelTerrainAssetTests PixelInventoryGameplayTests -j4
ctest --test-dir build-chunk64 -R '^Pixel\.(Generation|GenerationSession|TerrainAssets|InventoryGameplay|ModuleBoundaries)$' --output-on-failure

cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON
cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --terrain-only)
```

本轮 CPU 全量 114/114、64 格 Release 专项 5/5 通过。覆盖 v1 固定结果、v2 固定结果、三群落/八材质、负/远坐标、不同裁剪与真实区块交集、跨竖向区块树冠、出生净空、有限水封闭性、新材料开采与存档。种子 42 在 x=[-16384,16384)、y=[0,3072) 的诊断范围内接受 11 个湖泊、9 个地下积水分量；该数量是此范围结果，不是密度保证。

Pixel、AET、PixelRuntimeSmoke 构建通过；Apple M5/MoltenVK 的真实 GPU 地形专项通过，覆盖全部 20 种非空气材质、1/2/4/8/16 像素比例、PNG、透明水/表面条带、帧槽及远坐标 UV。首次沙箱内运行无法连接 macOS 窗口服务，沙箱外同一命令成功。Vulkan validation layer 不可用；本轮未重跑完整 GPU 生命周期或 AET 运行，也没有跨平台验证整数 golden vectors。
