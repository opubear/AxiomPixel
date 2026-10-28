# 美术资源接入与缓存

2026-10-20。本轮选用 ArtRaw 中 NaturalParallax2K 的一组四层山林背景，复制整理后的四张 2560×1440 RGBA PNG 到 `Pixel/Content/Art/NaturalParallax/`。原件不改动，复制件的 SHA-256 与 `Layers2K/` 一致。运行时没有 ArtRaw 路径依赖。本轮替换背景，泥土与角色沿用现有外观；构式效果尚未接入真实游戏，不在本次资源组范围内。

## 自定义 JSON

入口是 `Pixel/Content/ArtAssets.json`。`Version` 为整数1；`Background.HeightFraction` 在[0.25,2]内，表示背景高度与framebuffer高度之比；`Layers` 是从远到近的1～4层。`Path` 是相对 Content 的PNG路径，`RepeatCells` 是完整镜像双周期对应的世界格数（正整数，最大2²⁴）。周期越小，水平视差越快；Y方向不重复，以屏幕顶部为锚点。背景缩放随窗口高度变化，不跟随世界缩放改变大小。

```json
{
  "Version": 1,
  "Background": {
    "HeightFraction": 0.65,
    "Layers": [
      { "Path": "Art/NaturalParallax/00_far.png", "RepeatCells": 65536 },
      { "Path": "Art/NaturalParallax/01_mid_far.png", "RepeatCells": 16384 },
      { "Path": "Art/NaturalParallax/02_mid.png", "RepeatCells": 8192 },
      { "Path": "Art/NaturalParallax/03_near.png", "RepeatCells": 4096 }
    ]
  }
}
```

`PixelGame` 构造时注入 Axiom 已有的 `AssetLoader::LoadContentFile`，`ArtAssetStore` 复用 `JsonParser::Parse` 解释游戏自己的结构。引擎原本就支持对任意JSON传入解析回调，无需给 Root/GameConfig 增加 Pixel 字段或修改子模块。后续可在游戏资产模块增加其他清单类型，解析和业务格式归游戏所有。

相对路径归一化后作为缓存键；绝对路径、盘符、反斜杠、NUL和`..`被拒绝。实际运行时的 Axiom Content loader 还校验规范化目标及符号链接不越出 Content。失败信息包含清单和资源路径；启动即报告缺失文件、无效JSON、PNG错误或尺寸不一致，不静默回退。单图宽高上限4096，所有背景层必须等尺寸；stb使用引擎锁定版本且仅启用PNG，不引入或修改第三方源码。

## 所有权与渲染

`PixelGame` 持有 `ArtAssetStore`，在游戏生命周期内按规范化路径永久保留已成功解析的清单和解码图片；多个清单共享同一路径的不可变图片。`RenderSetup` 工厂从游戏资产仓库取图，临时工厂销毁不会丢失缓存。加载同步发生于应用线程，Draw和帧发布不会读盘或解码。失败的清单不进入成功缓存，允许修正后重试；已成功解码的图片仍可复用。替换磁盘文件需重新启动，目前不支持热更新或逐出。

`PixelRender` 借助共享所有权持有资产，将四张图写入一个RGBA8SRGB纹理数组的上传缓冲；第一次成功录制绘制时上传一次。窗口资源重建保留该纹理和上传标记，清理渲染策略释放GPU与上传缓冲，但共享CPU图片仍由资产仓库持有。若整个渲染策略被重建，CPU缓存复用，新的GPU资源需要重新上传。

shader在普通视图的空气/透明材质后合成背景，按sRGB采样转换到线性空间做straight-alpha叠加，最后经既有后处理/呈现编码；不透明泥土跳过背景采样。F1保持纯力场像素，未驻留区域保持原有棋盘占位。世界、风、人物、后处理和UI的顺序不变，背景不写World/材料/碰撞/存档。

相机快照只提供呈现数据；CPU先对int64世界参考坐标按重复周期取模，再加局部偏移，GPU仅接收[0,2)相位，避免远处转换为float丢失移动精度。画面左右采用镜像重复，原图不是无缝贴图；镜像构图可能出现对称重复。垂直方向是屏幕背景，不模拟地理海拔或地下景观。

四张解码贴图共56.25 MiB，策略持有的上传缓冲另56.25 MiB，GPU数组约56.25 MiB；不含解码瞬时工作内存、驱动开销和现有场景目标。这些为美术资源成本，不计入World的512 MiB CPU预算。全量缓存符合当前资源规模；继续添加大图时应先衡量内存再考虑异步加载或逐出。

## 验证入口与范围

CPU构建和默认CTest包含 `Pixel.ArtAssets`，覆盖真实四层PNG、清单/图片共享、路径与schema拒绝、失败重试、负坐标与2⁶⁰格附近视差。`PixelTests` 的mock覆盖背景首帧上传、跨slot复用、resize保留、F1关闭和CPU缓存生命周期。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON -DAXIOM_BUILD_LIFECYCLE_SMOKE=ON
cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --art-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
```

2026-10-20本机Apple M5：CPU/mock 46/46通过；美术GPU两种模式分别通过34,182和38,250个PNG像素样本，覆盖线性sRGB/alpha、全空气底边、精确镜像折点、地形遮挡、帧slot稳定、镜头移动和F1。完整Pixel GPU回归通过，含风场、玩法、UI和两轮窗口最小化恢复；正常Pixel入口启动4帧后成功退出。Astra ultra独立审查发现的采样边界和缓存所有权问题已修复并交回复核，无未决确定缺陷。验证层不可用，不能据此宣称通过Vulkan validation；未验证Linux/Windows GPU。本轮未修改Axiom，Pixel与AET编译均通过。

## 人物分层动画（2026-10-27）

正式游戏默认玩家已替换为 GandalfHardcore 男性人物，取自本地 `ArtRaw/Characters/GandalfHardcoreModular/`。只复制身体、裤子、上衣、鞋子、头发五张 800×448 PNG 到 `Pixel/Content/Art/Characters/Gandalf/`，原件不变。`SOURCE.json` 保存每张原件地址和 SHA-256，`LICENSE.txt` 保留随包许可。游戏运行仅依赖 Content，PNG与许可复制件沿用既有 Content 本地素材忽略规则。

配置入口为 `Pixel/Content/CharacterAssets.json`，`Version` 为整数1，`Character` 包含：

| 字段 | 约定 |
|---|---|
| `FrameSize` | `[80,64]`，每帧画布；必须整除所有图层宽高 |
| `FootAnchor` | `[40,64]`，帧内脚底锚点；对齐原2×4格碰撞盒底部中心，镜像时同步镜像锚点 |
| `PixelsPerCell` | `10.5`，原图像素与世界格比例；保持约42像素身高对应4格，透明边缘不压缩 |
| `Layers` | 从后到前的PNG相对路径，默认身体→裤子→上衣→鞋子→头发；1～8层、同尺寸同网格 |
| `Animations` | 必须定义 `Idle/Run/Jump/Fall`，每项指定 `Row/FirstFrame/FrameCount/FramesPerSecond/Loop` |

行和首帧从0计数。当前待机是第0行5帧、6 fps循环；跑步是第2行8帧、12 fps循环；跳跃是第3行4帧、10 fps并停在末帧；下落是第4行4帧、10 fps循环。路径复用现有 Content 校验，帧范围必须位于图内，帧率在(0,60]、像素比例在[1,256]。缺图、尺寸不符或配置错误会在启动时报错。修正失败配置后可重试，已成功配置需重启加载；没有热更新。

`ArtAssetStore` 共用PNG缓存。`CharacterAnimation` 属于Render，按已提交人物位移、Grounded与模拟时间选择动作；静止保留朝向，暂停及同一提交的重复绘制不推进时间，更换世界/角色或时间回退重置动画。状态由每个帧发布者持有，CPU资产由发布者及渲染策略共享。动作切换从首帧开始，不读取输入或可写物理对象，不改变Gameplay/Simulation、角色碰撞或存档。

组合根 `PixelRuntime` 将同一份配置显式交给发布者和 `PixelRender`。独立的测试/诊断发布者可省略人物美术，继续使用原矩形呈现。正常玩家使用96字节 `CharacterDraw` 快照传递连续屏幕范围、图集帧和翻转标记。GPU在风和场景物体之后、后处理之前，以nearest texel读取sRGB图层并在线性空间按straight alpha合成。透明边缘保留背景，F1不绘制人物。此处像素人物使用最近点采样，最低缩放级别会丢失细节。

五层解码数据合计6.84 MiB，上传缓冲及GPU图集各约6.84 MiB；缓存和纹理仅初始化一次，切frame slot或resize不重新解码/上传；失败录制会重试上传。本轮只接入玩家运动动画，保留现有木头人外观。工具使用、攻击、死亡动画和游戏内换装尚未绑定，未导入其他候选人物或Spine样例。

本轮验证：8项CPU/mock专项通过（`CharacterAnimation/Cpu/ArtAssets/TerrainAssets/TrainingDummy/Simulation/ModuleBoundaries/FormulaRendering`）。覆盖真实资源、缓存复用、失败配置重试、越界拒绝、远坐标、动作切换、暂停、世界重置、首帧上传/失败重试、跨slot及resize复用。Pixel与PixelRuntimeSmoke构建通过。

Apple M5实际GPU检查了全部21个动作帧的双向215,040个源PNG采样点，验证图层、alpha、sRGB、镜像、跨slot和F1；正式生产装配验证了走停、跳落、落地、暂停及F1，并保存画面目视核对脚底对齐。木头人GPU、暂停UI专项通过；正常Pixel入口完成1预热帧和4采样帧后成功退出。首次沙箱运行无法访问macOS窗口服务，原生窗口测试已重跑通过。未修改引擎，未执行全量CTest或其他平台GPU验证；本机缺少Vulkan validation layer。

```sh
cmake --build build-cpu --target PixelCharacterAnimationTests PixelTests PixelArtAssetTests PixelTerrainAssetTests PixelTrainingDummyTests PixelSimulationTests PixelFormulaRenderingTests -j4
ctest --test-dir build-cpu -R '^Pixel\.(CharacterAnimation|Cpu|ArtAssets|TerrainAssets|TrainingDummy|Simulation|ModuleBoundaries|FormulaRendering)$' --output-on-failure
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --character-only)
# 可选导出生产游戏呈现画面（线性读回转sRGB PPM）
(cd Pixel && PIXEL_CHARACTER_CAPTURE=/private/tmp/pixel-character.ppm ../build/tests/PixelRuntimeSmoke --character-only)
```
