# 世界生成与自然材质：实现及使用

2026-10-20。实现基于分块世界生成方案，保留超平坦调试策略。World没有新增渲染或JSON资源依赖；本轮未修改Axiom引擎。

## 选择地图生成器

游戏首次新建世界默认使用Natural；已有存档始终使用存档中保存的生成器。F2打开Pixel Debug，在World generation中选择Next world（Natural或Superflat）、输入64位World seed，再按R保存旧世界并创建新目录。R使用当前选定的种子，便于复现；要不同地形可修改种子。

也可以在启动新存档前设置：

```sh
PIXEL_SAVE_ROOT=/private/tmp/pixel-natural PIXEL_WORLD_GENERATOR=natural PIXEL_WORLD_SEED=42 ./run-debug.sh
PIXEL_SAVE_ROOT=/private/tmp/pixel-flat PIXEL_WORLD_GENERATOR=flat PIXEL_WORLD_SEED=42 ./run-debug.sh
```

这些环境变量只决定新世界的候选设置，不能改变已经加载世界的生成身份。Debug/Release均提供两种策略，调试地图无需维护另一份加载逻辑。CPU有限地图与`GameSession()`/`GameSession(true)`测试夹具默认Flat；应用组合入口显式采用`GameSession::DefaultGeneration()`，因此游戏默认Natural。

## 生成和持久化

`IWorldGenerator`通过`CreateWorldGenerator`选择Flat或Natural。后台Streamer优先读取已有Chunk文件，仅文件确实不存在才调用同一策略生成完整Chunk；生成失败不接纳半成品。原有Missing/Loading/Failed、预算、窗口与主线程整块发布规则保留。

- Flat严格使用`y<SurfaceY`为空气、`y>=SurfaceY`为Dirt，默认SurfaceY=512。种子与水开关不改变Flat的材料结果。
- Natural按世界坐标执行地表/出生平台与湖盆、固结土岩、洞穴、五种矿物、水体、干燥地表草皮，再统计并提交。生成只写候选载荷，不访问邻块驻留状态。
- 湖泊使用独立且偏移的512格特征网格；地下积水完整检查有界128×128四邻接分量，拒绝侧/底泄漏。每个Chunk写入同一计划的交集，因此水位和材料不依赖生成先后。
- 噪声为带稳定标签与整数格点哈希的Q16.16 Perlin；远坐标先做整数商余数分解。种子、版本与固定区域材质均有golden vectors。

准确接口和被冻结的全部常量见[生成API](../../Pixel/Source/World/Generation/WorldGenerator.h)及[Version1合同](../../Pixel/Source/World/Generation/Version1.md)。实现使用稳定Kind值Flat=0/Natural=1，两种策略的Version均为1；原方案中的`flat/0`是兼容策略的概念命名，不是当前存档字段。

metadata升级为v3，保存Kind、Version、Seed、SurfaceY、WaterEnabled及覆盖全部可配置字段的指纹，并继续保存人物位置、暂停/时钟与风设置。不可配置常量由Version1合同冻结；修改常量必须保留旧版本实现并增加新版本，不能原地改变Version1结果。

旧v2 metadata自动映射到Flat/version1/SurfaceY512，未生成块继续平坦延伸，保存时写为v3。Chunk仍为原v1布局，材料列继续32位，整个载荷仍14字节/格。未知版本、材料、坏文件或权限错误会报错，不能退化为重新生成。

块文件、metadata及current.txt先完成同目录临时文件写入再替换。新世界提交前先保存旧世界；候选失败从旧目录恢复完整会话。恢复也失败时，销毁候选/部分恢复的Streamer和Map，禁止编辑、推进与保存；Debug面板的Retry world recovery在磁盘问题解除后显式恢复。错误包含候选与恢复两个阶段。不承诺跨文件断电事务。

## 材料、水体与操作

| 材料 | ID | 行为 |
|---|---:|---|
| Air | 0 | 空间 |
| Dirt | 10 | 原有动态泥土，受基础力与风影响 |
| GroundSoil / GrassSoil | 20 / 21 | 固定且可开采的土层/覆草土 |
| Stone | 30 | 固定可开采岩层 |
| Coal/Copper/Iron/Tungsten/Gold Ore | 40～44 | 固定可开采含矿岩层 |
| Water | 50 | 有限体积液体，不阻挡角色，阻挡空气风域 |

右键开采固体可开采材料；左键仍放置Dirt，不覆盖水或角色。水不作为矿块直接删除。人物有水中阻尼与浮力，Space可上浮，无溺水伤害。固定地层不自动坍塌；Dirt暂不与水交换沉降。

水与Dirt在同一个60 Hz步首状态上提出移动、统一裁决目标再提交。Water只向Air传输，先下落、再按水头/落差横移；禁止同一步整列同步追随，窄槽可沉降并休眠。材料移动保留源类型，基础力留在原空间格；风运动反馈只接收Dirt。

初始水为有限水量，没有无限水源或补水回调。开采湖底可排水；源/目标块都标脏，排空块也保存。暂停、未驻留边界和模拟窗外冻结；邻块接纳后唤醒交界。重载已有文件不补水、不补矿、不重新长草。液体不是带压力/体积分数的完整流体求解器。

## Content与JSON资源

运行时入口为[ArtAssets.json](../../Pixel/Content/ArtAssets.json)的Terrain对象。原件保留在ArtRaw，实际使用的8张自然材质和2张水体PNG原样复制到Content/Art/NaturalBlocks和Content/Art/Elements；来源与SHA-256见这两个目录的ORIGIN.md与SOURCE.json。

```json
{
  "Terrain": {
    "CellsPerTile": 64,
    "Images": [{ "Layer": 1, "Path": "Art/NaturalBlocks/01_soil.png" }],
    "Materials": [{ "TypeId": 20, "Layer": 1 }]
  }
}
```

上面只是字段示意，完整清单必须覆盖所有运行时材料。Images决定图层与Content相对PNG路径；Materials把Gameplay TypeId映射到图层；草皮和水还配置SurfaceLayer，水面的SurfaceStrip=3选择原图第24～31行，避免采到透明天空。加载器拒绝路径越界、错误尺寸、重复层/材料和缺失必需映射。

PNG和解析结果由应用级ArtAssetStore共享缓存，GPU图集首次使用上传一次。纹理一张覆盖64物理格，CPU先用整数模计算负/远坐标相位；GPU不接收巨大绝对浮点坐标。表面由已发布快照的上邻格决定，并计入邻Region版本；body连续铺贴，草皮/水面仅取配置的水平条带。纯色catalog继续供CPU/mock及既有诊断使用。

## 验证入口与范围

默认CPU回归新增Generation、Liquids、GenerationSession、TerrainAssets，覆盖实际Chunk与不同tile、负/远坐标、版本、生成内容、有限水体、材质配置和缓存、液体守恒/排水/静止、生成器往返及失败恢复。64格构建复用这些契约。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --terrain-only)
```

terrain-only使用实际Content PNG检查全部材质、sRGB/alpha、表面条带、frame slots及负/远相位；完整smoke还覆盖既有背景、角色/风、UI及生命周期。

本机最终CPU全量50/50通过；64格Release的相关8项与两种块尺寸的渲染检查通过；Apple M5/MoltenVK完整GPU smoke通过，包含两轮缩放/最小化恢复。实际Natural/seed42游戏入口短启动和v3保存也通过。独立Astra/xhigh审查的3项P2均修复并复核。命令、日志及覆盖边界见[最终验证](../实现策略化世界生成与材质接入Task/Doc/Validation.md)，发现与修订见[独立审查](../实现策略化世界生成与材质接入Task/Doc/CodeReview.md)。

跨平台golden vectors尚需在其他平台执行；水体密度、单机生成耗时及液体模型不代表河网、海洋、压力流或两相动量求解。所有参数和后续格式演进必须保持版本可追溯。
