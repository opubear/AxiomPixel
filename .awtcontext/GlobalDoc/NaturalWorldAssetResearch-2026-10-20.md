# 初版自然世界素材候选（2026-10-20）

调研日期：2026-10-20。任务：`免费可商用游戏素材调研`。当前用户优先级为：**免费可试用、效果好、自然舒适、适合长时间游玩；初版暂不要求商用授权，后续可替换。** 任务最初名称保留，筛选已按后续要求调整。

调研阶段核对了来源与美术方向，没有导入素材或修改游戏、引擎。价格、免费范围和作者声明取自本次访问的原始素材页；后续按用户要求下载至 ArtRaw，下载及包内说明核对结果见文末。尚未验证图集切分或游戏内显示。

## 项目适配依据

- 依据工程 README、AWT 项目说明与 `GlobalDoc/FormulaSystem.md`：当前为横版像素世界，材料按格更新，角色独立绘制，构式表现尚未接入。
- 用户明确希望世界偏自然、舒适，避免持续紧张；因此森林、草地、山景、植物、水边优先，工业科幻仅保留为局部装置参考。
- 以下“舒服、耐看、适合”的判断是素材预览与项目需求之间的美术建议，不是玩家测试结论。
- 世界物理格、素材像素、屏幕像素应分别确定比例。现有角色仅 2×4 格，不能直接把 16×32 或 32×32 精灵按一像素一格塞入当前世界。

## 优先试用的自然素材

| 优先级 | 素材与作者原页 | 内容、尺度及建议用途 | 免费范围与简短授权记录 |
|---|---|---|---|
| 首选主体 | [Legacy Fantasy — High Forest / Anokolisa](https://anokolisa.itch.io/sidescroller-pixelart-sprites-asset-pack-forest-16x16) | 16×16 森林体系；草地、树木、湖边、洞穴、遗迹、背景，另有角色与生物。适合先搭一片能探索和停留的森林。 | `High Forest 2.0` 与 `Debug Map` 免费；作者正文说明可免费使用和商用。页面另有 Terms，本次未取得其 PDF 正文。 |
| 首选山林 | [Taiga / vnitti](https://vnitti.itch.io/taiga-asset-pack) | 16×16 松树、草石、石屋，含山景与森林中景。自然朴素，适合山麓和林间居所。 | 自定价可免费；作者在原页回复中明确允许署名商用，详情随包提供。已核对免费领取页。 |
| 首选远景 | [Grassy Mountains / vnitti](https://vnitti.itch.io/grassy-mountains-parallax-background) | 384×216 分层 PNG/PSD，含明亮版本。适合开阔草原、山坡远景，让视野留出天空。 | 自定价可免费；作者原页回复允许商用时付费或署名。不能仅依据名称把它标为 CC0。 |
| 柔和远景 | [Free Simple Pastel Backgrounds / Eder Muniz](https://edermunizz.itch.io/free-simple-pastel-backgrounds) | 6 种背景、4 套瓦片；背景 256×224、瓦片 16×16，含森林、山、昼夜天空。适合试柔和且简洁的画面。 | 免费自定价；要求署名，允许项目内修改；禁止作为素材再分发或转卖。 |
| 生活感补充 | [Pixel Art Platformer — Village Props / Cainos](https://cainos.itch.io/pixel-art-platformer-village-props) | 32×32 体系，含植物、树、岩石、草地及村落道具；有独立纹理。适合营地、花草和有人居住的区域。 | 免费自定价；允许免费及商用项目、修改；署名可选，禁止素材再分发或转卖。与 16×16 主体混用前需统一尺度。 |
| 温暖丘陵 | [Free Pixel Art Hill / Eder Muniz](https://edermunizz.itch.io/free-pixel-art-hill) | 6 层丘陵背景，PNG/PSD，适合试树木、暖色山景与日落环境。 | 免费自定价，作者要求署名；具体随包条款尚未核对。 |
| 季节扩展 | [Four Seasons Platformer / RottingPixels](https://rottingpixels.itch.io/four-seasons-platformer-tileset-16x16free) | 16×16 四季地形，用于比较季节颜色和小规模生境变化，适合快速原型。 | 免费自定价；作者允许免费及商用项目、修改，署名可选。页面没有明确标准许可编号，不直接标为 CC0。 |
| 简洁备选 | [Nature Platformer / RottingPixels](https://rottingpixels.itch.io/nature-platformer-tileset) | 16×16 自然横版瓦片，适合快速搭草地、土层和小场景；画风偏明快卡通。 | 免费自定价；允许项目内使用和修改，署名可选。 |
| 整套开发占位 | [SunnyLand / Ansimuz](https://ansimuz.itch.io/sunny-land-pixel-game-art) | 横版自然场景、角色、敌人、道具与效果，适合迅速验证角色移动和动画；画面偏活泼。 | 基础美术文件免费，页面标 CC0；`Sunny Land Plus` 为付费内容。音乐单独列出，不能把美术许可概括到所有音乐。 |

## 建议先比较的两种组合

1. **森林探索版：High Forest 自带的地形、植物、背景作为主体。** 先用同包角色占位，减少风格拼接；露天区域保留天空和远景，降低大面积深色遮挡，湖边或林间空地作为可停留区域。后续再评估补入 Cainos 的道具。
2. **开阔山林版：Taiga 地形与松树，配 Grassy Mountains 远景。** 两者同一作者，更容易比较配色与细节密度；应在游戏内验证缩放和远近层次后决定是否组合。

Pastel Backgrounds 是另一种简洁柔和的方向，适合单独做对照场景。素材库之间不要先大规模混搭；统一像素密度、光源方向、轮廓粗细、颜色与草地边缘，再决定保留哪些。

## 对逐格材料世界的使用方式

- 背景、树木、花草、营地道具最容易作为独立表现层使用；不会要求把物理世界改为传统固定瓦片地图。
- 泥土、沙、岩石的内部颜色与纹理可以从主素材提取视觉方向；表面草边、转角与洞口需要根据实际材料轮廓生成，不能把完整平台块当作可逐格破坏地形直接铺上。
- 建议先做一个小场景，包含露天草坡、树、石头、洞口、角色和一个构式效果，检查移动时的细节闪烁、遮挡、轮廓可读性及停留时的舒适感。
- 这是后续接入建议，本次未实现；World 继续保持纯 CPU，素材和外观映射属于 Pixel 表现层。

## 构式、界面与声音的配套候选

| 用途 | 来源 | 适用与限制 |
|---|---|---|
| 小型拾取、攻击效果 | [Lucifer — Effects / Foozle](https://foozlecc.itch.io/lucifer-effects) | 免费 CC0，附精灵和 .ase 源文件；可作为效果占位后统一颜色。 |
| 爆炸、能量、命中效果 | [Free VFX Asset Pack / CodeManu](https://codemanu.itch.io/vfx-free-pack) | 免费，22 种效果，PNG 图集与单帧。正文说公有领域免署名、元数据却标 CC BY 4.0；本阶段保留来源和署名，后续核对包内许可。大效果需控制尺寸与亮度。 |
| 像素面板 | [UI Pack — Pixel Adventure / Kenney](https://kenney.nl/assets/ui-pack-pixel-adventure) | 免费 CC0，500 个文件；可做节点、槽位和菜单的占位。 |
| 按键提示 | [Input Prompts Pixel / Kenney](https://kenney.nl/assets/input-prompts-pixel) | 免费 CC0，16×16；键鼠与手柄提示。 |
| 构式图标 | [Game-icons.net](https://game-icons.net/about.html) | CC BY 3.0，按具体作者署名；原始为矢量风格，缩到小像素尺寸需重整轮廓。 |
| 中文像素字体 | [Fusion Pixel Font / TakWolf](https://github.com/TakWolf/fusion-pixel-font) | 字体 OFL-1.1，支持中文与其他 CJK 字形；随游戏保留相应字体版权及许可文件。构建程序的 MIT 不能代替字体许可。 |
| 安静区域音乐 | [Meadow Thoughts / Écrivain](https://opengameart.org/content/meadow-thoughts) | CC0，2:29 独奏竖琴。可作为试听候选，本次未听音频或确认无缝循环。 |
| 户外环境声 | [Outdoor ambiance / vi1e8](https://opengameart.org/content/outdoor-ambiance) | CC BY 3.0，页面说明为户外短循环。本次未试听，需在实际混音中检查循环与重复感。 |
| 装置、能量音效 | [Sci-fi Sounds / Kenney](https://kenney.nl/assets/sci-fi-sounds) | 免费 CC0；适合局部装置或构式，使用强度需服从世界舒适氛围。 |

## 降低优先级的候选

- [Warped Caves](https://ansimuz.itch.io/warped-caves) 与 [RottingPixels Sci-Fi](https://rottingpixels.itch.io/sci-fi-tileset)：来源免费且有项目使用许可，但较偏异星/工业氛围，按用户补充不再作为世界主体。Warped 基础美术与付费扩展、音乐授权需要分别看待。
- [Mossy Cavern / Maaot](https://maaot.itch.io/mossy-cavern)：免费，植物和有机洞穴可供氛围参考；它是 512×512 的手绘素材，画法和现有像素体系差异很大，不建议直接混用。演示里的后处理和粒子没有包含在包中。
- [Free Pixel Art Forest / Eder Muniz](https://edermunizz.itch.io/free-pixel-art-forest)：免费九层森林，视觉更偏阴暗浓密，本轮不列为舒适主世界首选。页面元数据与作者自定义修改条款也存在差异。
- [Oak Woods](https://brullov.itch.io/oak-woods)、[Legacy Adventure — Forest](https://anokolisa.itch.io/legacy-adventure-pack-forest)、[Henry Pixel Effects](https://henrysoftware.itch.io/pixel-effects)、[NYKNCK +40 Effects](https://nyknck.itch.io/40pixeleffectspack)：本次作者页均有最低付费价格，不能沿用旧清单把它们列成免费包。未购买。

## 来源保存与验证范围

- 本次核对作者原页的素材内容、价格/免费入口和可见授权声明；浏览器目视检查了 High Forest、Taiga、Grassy Mountains 与 Pastel 页面，匹配程度仍需游戏内对照。
- 建议实际取用时保存作者、原页地址、下载日期、原始文件名及随包文本，便于初版后替换或发布前核对；当前不为许可证不确定项阻断视觉筛选。
- 自定价素材可在领取页使用 `No thanks, just take me to the downloads`；付费扩展不包含在零元范围内。
- 未将任何素材纳入游戏资源目录，未调整角色比例、图集、动画和地形表现，未运行构建或 GPU 验证。本次完成的是研究清单，不是素材接入。

## 下载记录（2026-10-20）

用户要求将推荐素材下载到工程 `ArtRaw/`。已保存上轮回复中的六组自然环境素材（High Forest、Taiga、Grassy Mountains、Pastel Backgrounds、Village Props、Pixel Art Hill）及两组效果素材（Lucifer Effects、CodeManu VFX）。

- 入口：[ArtRaw/README.md](../../ArtRaw/README.md)，含各素材本地目录及作者原页。
- 共 8 组、9 个原始 ZIP/RAR，保留原包并解压，2,363 个文件。Taiga 取 v2；High Forest 包含免费 Debug Map。
- 原始压缩包合计 101,597,834 字节；逐包 SHA-256 与来源记录见 `ArtRaw/manifest.json` 和各组 `SOURCE.json`。
- ZIP 已做 CRC 校验，RAR 已成功解码解压。未购买素材，未运行包内项目或接入游戏。
- 包内 Taiga 与 Grassy Mountains 的许可明确为 CC BY 4.0；其他随包条款保留。Lucifer 的包内作者署名与网页存在差异，已记录。Village Props 与 CodeManu 包中未找到单独许可文件，仍保留原页地址。
