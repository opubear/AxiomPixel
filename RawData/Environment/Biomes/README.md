# 群落背景原件

2026-11-10 使用内置 `image_gen` 生成沙漠与雪山两张低对比横版像素全景；完整提示词在 `prompts.json`，来源与校验值在 `SOURCE.json`。

根目录 PNG 是 1672×941 原图；`Runtime2560` 为匹配既有背景纹理数组而使用 macOS `sips -z 1440 2560` 缩放的制品，尺寸 2560×1440。未改变图像内容。森林仍复用 `ArtRaw/Environment/NaturalParallax2K` 的四层背景。

运行 `python3 RawData/Environment/Biomes/install_runtime.py` 校验 SHA-256 并复制至 `Pixel/Content/Art/BiomeBackgrounds`。程序只读 Content，原件和运行制品随 RawData 保留；Content PNG 按既有规则忽略。

图片左右采用镜像重复，当前各一层，未拆分独立远山、云层和近景。群落混合由 `ArtAssets.json`、`BiomeFrame` 和背景 shader 接通。

`Preview/runtime-desert.png` 是最终 Apple M5 原生 GPU 专项捕获的 seed 42 实际场景，不是生成概念图。
