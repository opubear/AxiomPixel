# 群落方块素材

2026-10-29。用于 Natural v2 的森林、沙漠和雪山。

- `Originals/`：内置 `image_gen` 生成的五张完整原图，未覆盖原输出。
- `prompts.json`：每张图的完整提示词、工具、日期、原图相对路径及 SHA-256。
- `Textures64/`：运行时使用的 64×64 PNG；新原图通过 macOS `sips -z 64 64` 缩放。沙土、黏土、苔石原样复制自已有 `ArtRaw/Materials/NaturalBlocks/Textures64/` 的 `04_sand.png`、`05_clay.png`、`06_moss_stone.png`；其原来源继续见该素材包的 `RESOURCE.md`。
- `Preview/`：实际生成器的固定种子截图及气候采样 CSV，由 `PixelGenerationPreview` 输出后转换；不是概念美术。
- 运行时副本：`Pixel/Content/Art/Biomes/`；`SOURCE.json` 记录每张图的原路径、尺寸及 SHA-256，CPU 测试逐字节核验副本。

新增：砂岩 `sandstone`、雪 `snow`、冰 `ice`、树干 `wood`、树叶 `leaves`。复用：沙土 `sand`、黏土 `clay`、苔石 `moss_stone`。均为不透明材质，64 个逻辑格重复；树冠轮廓由世界格子决定。

这些图像是生成素材或既有生成素材的复用，不宣称具有第三方认证许可。提示词请求双向铺贴；本轮检查了实际缩小图和游戏铺贴，未将提示词当作数学意义上接缝完全相等的保证。

运行时文件遵循仓库现有 Content 二进制忽略规则。本目录保留了全部可复制的 64×64 文件，避免分发时依赖本机的生成工具缓存。

恢复运行时副本：`python3 RawData/Materials/Biomes/install_runtime.py`。脚本先验证全部来源的 SHA-256，再复制，不重新生成或缩放图片。
