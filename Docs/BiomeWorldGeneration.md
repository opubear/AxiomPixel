# 分层群落世界生成（Natural v2 / v3）

2026-11-16。新建游戏默认 Natural v3，F2 可选择 Natural 或 Superflat，种子决定同一完整世界；R 创建新世界。当前应用的临时世界不保存进度，显式持久化会话仍支持版本化保存。Natural v1/v2 与 Flat v1 保留原生成结果。v3 沿用 v2 的气候、地层、矿物与植被，扩展环境流体。

## v3 环境流体

- 地表湖泊仍按偏移 256 的 512 格网格确定候选，避让出生点 x=512 的 144 格干燥范围。半径 24～72 格、目标深度 6～12 格，实际深度限制在 2～16 格；岸边用 12 格平滑过渡到原地形。两岸高差最多 20 格、候选内原地形起伏最多 32 格、最大下切 48 格，拒绝断开的水面。
- 雪山权重大于 32768 时拒绝地表液态湖；沙漠权重大于 32768 时仅保留 1/4 候选；湿度指数低于 -12000 时另保留 1/4。使用固定整数散列，不保证每片地区都有湖泊或绿洲。
- 地下流体按 256×256 格完整特征规划，通道为 `environment-fluid-pockets-v3`。中心位于格内 96～160，横半径 24～46、纵半径 14～26。椭圆洞室下半部填液体，上半部保留空气；外围三格椭圆岩壁封住邻接洞穴，整个岩顶至少位于原地表下 96 格。Caves 阶段形成洞室，Water 阶段填入液体，各块只写完整计划的交集。
- 热层门槛为 `max(512, 704 - Temperature / 128)` 格深度，Temperature 为稳定气候指数，整数除法向零截断。中心达到门槛时保留 2/3 岩浆候选；深度比门槛至少少 128 格且湿度不低于 -8000 时，保留 3/4 地下水候选。中间 128 格隔离带避免初始水/岩浆直接相邻。
- 流体均为有限格数，开采围壁后按元胞规则流出，不由生成器补液。`WaterEnabled=false` 关闭湖水和地下水，地下水洞室仍为空，岩浆不受影响。负/远坐标、生成顺序和区块大小共用同一计划。
- `PlanGenerationFluids` 返回材料、类型、完整范围、液面与体积，`PlanGenerationWater` 只返回水；计划不创建运行时源。运动、凝固和保存见[元胞材料](../.awtcontext/GlobalDoc/CellularMaterials.md)。以下共有规则中的 v2 概率与旧地下水算法继续用于兼容世界。

## 分层规则

世界生成只依赖种子、版本和整数世界坐标。气候不是运行时天气，而是决定初始环境的稳定场。

| 层 | 采样尺度（逻辑格） | 控制内容 |
|---|---|---|
| 环境 | 温度/湿度 4096、抬升 2048 | 森林、沙漠、雪山的连续 Q16.16 权重，和为 65536 |
| 大地形 | 大陆起伏 2048、坐标扰动 1024 | 群落共同的地势与地形变化位置 |
| 地形细节 | 丘陵/山脊 512、沙丘 128、细节 64 | 林地丘陵、低缓沙丘、高山岩脊；按群落权重混合高度 |
| 地层 | 厚度 128、材质斑块 128×64/128×32/64×32 | 厚土、黏土透镜、厚沙/砂岩、薄雪与浅层冰体；交界用二维噪声交错 |
| 洞穴/矿物 | 独立通道，沿用现有尺度 | 林地洞室更宽、山地裂隙更窄，阈值平滑混合；五种矿石保留深度门槛 |
| 流体与植被 | 湖泊网格 512、v3地下流体网格 256、树木网格 128、林密度 512 | 林湖、低概率绿洲、地下积水与深层岩浆；有疏密变化的树干与树冠 |

温湿度与抬升噪声在第二轴使用固定非格点偏移，避免一维采样恰好落在 Perlin 格点行。高度在选择离散材质之前按权重混合，不会因为群落标签改变而形成垂直断崖。地层通过二维噪声交错过渡，深度 248 以下统一进入基础岩层及既有矿物/洞穴。

稳定气候及群落权重已提取到 `Pixel/Source/Runtime/Simulation/World/Environment/WorldBiome.cpp`，地形参数保留在 `Generation/BiomeTerrain.cpp`。二者共用采样，v2 数值保持不变。`WorldGenerator.cpp` 编排地形、湖盆、地层、洞穴、矿物、有限水、草皮和树木，公开 `SampleGenerationColumn` 的环境权重及 `SampleGeneration`/`PlanGenerationWater` 诊断接口。Simulation 不依赖 Render、Gameplay 或纹理 JSON。

## 行为与边界

- 森林：土层与黏土、浅层苔石；林密度场决定疏密。树木为可开采的真实方块，树干阻挡角色，树叶可穿过且不挡风。
- 沙漠：固结沙层、砂岩，v2绿洲候选保留 1/8，v3按上节气候筛选。沙土当前为固定地层，不具有逐粒下落模拟。
- 雪山：高山岩脊、雪层及冰体；地表液态湖候选被拒绝。积雪和冰是静态材料，本轮没有融化、结冰或天气循环。
- 湖泊保持完整、有界的封闭性检查；v1/v2地下水沿用原四邻接分量检查，v3改用完整有界洞室。开采后不会自动补液；水开关只关闭水。矿石只能替换岩层，不填洞穴、不替换冰体。
- 树木按完整特征锚点计算，每个查询带 22 格横向余量，写入与当前区块的交集；竖向裁剪不改变树冠。树木不会覆盖土岩或水，不包含生长、落叶或砍树后整体倒塌模拟。
- 出生点 x=512 保留 33 格平整平台和过渡区，湖泊和树木避让。平台可采用所在群落的材质。
- 地表草皮沿用现有 exposed-surface 呈现规则。2026-11-10 已接入同源群落查询、初始生物适生率、地区装饰和三类背景混合，见[世界生态群落](../.awtcontext/GlobalDoc/WorldBiomes.md)；该扩展不改变本页 v2 地形和树木生成结果。

## 材料与资源

| 材料 | CellTypeId | 纹理层 | 开采产物 |
|---|---:|---:|---|
| Sand / Clay | 22 / 23 | 13 / 14 | Sand / Clay |
| MossStone / Sandstone | 31 / 32 | 15 / 16 | Stone / Sandstone |
| Snow / Ice | 60 / 61 | 17 / 18 | Snow / Ice |
| Wood / Leaves | 70 / 71 | 19 / 20 | Wood / Leaves |
| Lava（v3生成） | 53 | 12（复用火体纹理） | 无；遇水形成的 Stone 可按石头开采 |

v2新增物品 ID 为 1070～1130，原有材料/物品 ID 保持不变；背包、存档、开采和 UI 已接通。普通放置使用快捷栏选中的方块资源并扣料。岩浆不新增背包物品或建造入口。

五张新原图由内置 image_gen 生成，沙土/黏土/苔石复用现有素材；完整提示词、原图与 64×64 制品见 [RawData 素材说明](../RawData/Materials/Biomes/README.md)。运行时只读 `Pixel/Content/Art/Biomes`，由 `ArtAssets.json` 映射，21 层包含透明空气层。Content PNG 按既有规则被 Git 忽略，分发时需同步原文件；`SOURCE.json` 与 RawData 制品用于校验/恢复。

恢复新增运行时素材：`python3 RawData/Materials/Biomes/install_runtime.py`。原有 NaturalBlocks、Elements、角色和背景素材仍遵循各自的现有复制流程。

## 版本与复现

Natural支持 Version=1/2/3，Flat只接受 Version=1。应用默认值与F2选择Natural使用v3；底层 `WorldGenerationSettings` 默认Flat v1，以保留CPU有限测试夹具。metadata指纹包含版本；读档继续尊重存档的生成版本，不升级旧世界尚未生成的区块。改变既有版本生成结果的参数调整必须增加版本。

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

## 历史验证（2026-10-29，v2）

当时CPU全量114/114、64格Release专项5/5通过。覆盖v1/v2固定结果、三群落/八材质、负/远坐标、不同裁剪与真实区块交集、跨竖向区块树冠、出生净空、有限水封闭性、新材料开采与存档。种子42在x=[-16384,16384)、y=[0,3072)内接受11个湖泊、9个地下积水分量；该数量是此范围结果，不是密度保证。

Pixel、AET、PixelRuntimeSmoke 构建通过；Apple M5/MoltenVK 的真实 GPU 地形专项通过，覆盖全部 20 种非空气材质、1/2/4/8/16 像素比例、PNG、透明水/表面条带、帧槽及远坐标 UV。首次沙箱内运行无法连接 macOS 窗口服务，沙箱外同一命令成功。Vulkan validation layer 不可用；本轮未重跑完整 GPU 生命周期或 AET 运行，也没有跨平台验证整数 golden vectors。


## 本轮验证（2026-11-16，v3）

- 最终默认CPU全量132/132通过。Generation保留v1/v2固定结果并新增v3固定区域摘要、三类流体、有限体积与封闭性、水开关、出生净空、实际区块交集、负/远坐标及种子差异；Biomes使用v3验证同源气候和地区内容，GenerationSession验证v1/v2/v3与Lava保存恢复。
- 64格Release专项7/7通过（Generation、Liquids、Elements、GenerationSession、TerrainAssets、Biomes、ModuleBoundaries）；最后新增固定摘要与边界断言后，Generation/Elements再次2/2通过。
- Elements验证岩浆四步一动、体积守恒、静止休眠、开口排流唤醒、水接触凝固、负坐标跨块、窗口外冻结与保存后无风续算一致。
- 最终Pixel、AET（Axiom）与PixelRuntimeSmoke构建通过；Apple M5/MoltenVK原生窗口 `--terrain-only` 通过，覆盖Lava在内的21种非空气材料、多比例、PNG、水面/透明、帧槽与远坐标UV。沙箱内首次窗口初始化卡住，已终止该测试进程；允许原生窗口访问后同一专项成功。未重跑完整GPU生命周期、AET运行或跨平台验证；Vulkan validation layer不可用。
- `PixelGenerationPreview`现输出三群落以及lake/groundwater/lava的真实材料预览。种子42在x=[-8192,8192)、y=[0,3072)内接受3个湖泊、60处地下积水和346处岩浆池；此数量仅描述该诊断范围，不承诺密度。v3固定区域x=[-129,127)、y=[440,1464)摘要为6575692422298785517。
- 本轮未修改引擎子模块；保留原有工作区未提交内容。相关文档本地链接检查、AWT索引检查和 `git diff --check` 通过。
