# 怪物与动画接入

入口是 `Pixel/Content/Monsters.json`。正常游戏按 `Spawns` 提供蘑菇怪、哥布林、骷髅、飞眼、史莱姆、巨鼠、蝙蝠和宝箱怪候选；Natural v2 在固定世界地区锚点按当地群落适生率筛选，首次进入模拟区时生成。按 `R` 重建世界会重新生成；既有候选死亡后不补生，离区对象休眠并在重返时恢复。配置重启读取。

## 资源与配置

`RawData/Monsters/Sprites/` 中的 **54 张 PNG、370 帧**逐字节复制到 `Pixel/Content/Art/Monsters/<id>/`，保留 `CREDITS.txt`，`SOURCE.json` 记录原路径与每张 SHA-256。没有改动原图、缩放图条或裁掉透明边缘。运行时只读 Content；原始 ZIP、预览图和未确认切帧网格的四张附加特效仍在 RawData。PNG 与署名文本沿用仓库已有的本地素材忽略规则，JSON 清单可以纳入版本控制。来源许可见 [原始素材说明](../RawData/Monsters/README.md)。

`Version` 为整数 1，`Monsters` 为定义数组，`Spawns` 为地区候选列表（Flat／Natural v1 仍只用于初始场景）。`Id` 是 1～32 字符的小写字母、数字或下划线，不能重复。最多 32 种定义、64 个候选点；同时活动的角色受 Actors 的 256 个容量限制。生态对象离区后休眠，活动和休眠对象共用持久 ActorId。

| 配置 | 用途 |
| --- | --- |
| `Gameplay.BodySizeCells` | 碰撞盒宽高，单位世界格，每轴 0.5～8；运行时换算为厘米 |
| `Flying`、`SpeedCellsPerSecond` | 飞行/地面移动与速度；飞行仍检测地形和缺块边界 |
| `MaxHealth`、`Damage` | 生命值与一次近战命中的伤害 |
| `DetectionCells`、`AttackRangeCells`、`PatrolCells` | 察觉距离、身体边缘之间的攻击距离、出生点两侧巡逻距离 |
| `AttackWindupTicks`、`AttackDurationTicks`、`AttackCooldownTicks` | 命中时刻、攻击动作长度、从攻击开始计的冷却；每 tick 为 1/60 秒 |
| `HurtTicks`、`DeathTicks`、`WakeTicks` | 受击、死亡停留、宝箱怪唤醒时间；Wake 为 0 表示不潜伏 |
| `Visual.FrameSize`、`FootAnchor`、`PixelsPerCell` | 源画布、脚底锚点和外观比例；碰撞盒独立配置 |
| `Visual.Clips` | 每张单行图条的 Content 相对路径、帧数、fps、是否循环 |
| `Visual.States` | 把 Idle/Move/Attack/Hurt/Death 映射到 clip 名；潜伏怪还须配置 Dormant/Wake |
| `Gameplay.BiomeWeights` | 森林、沙漠、雪山的适生百分比，整数0～100，缺省全部100 |
| `Spawns[].Id/OffsetCells/HeightAboveGroundCells` | 种类、相对地区锚点的水平格偏移（每512格一个锚点，初始锚点x=511）、相对地面的悬空高度 |

图条必须恰好等于 `FrameCount × FrameSize.width`，高度等于帧高。越界路径、无效尺寸/时序、未知种类、缺失必需动作和图片会在加载时失败。当前 fps 和攻击命中时刻是本项目设定，作者没有提供正式播放时序。全部额外攻击、盾防、坠落和变形图条也登记在 `Clips`，当前行为通过 `States` 选择其中的动作；修改映射即可选用另一张图条。

生成会在指定横坐标±16格内寻找可容纳整个身体且有支撑的位置。完整搜索区尚未驻留、容量不足或临时Actor占位时保留候选等待；气候不适生或完整地形没有合法位置则记录为空。放置不挖空地形。飞行怪在找到地面后按配置抬高。

## 玩法与所有权

怪物是 `ActorKind::Monster`，组合 `BodyComponent`、`MonsterComponent` 和 `HealthComponent`。`MonsterCatalog` 是 Gameplay 的不可变定义；组合根注入 Content 文本，Simulation 不知道怪物或美术。

地面怪交替待机与巡逻，发现视线内玩家后追击，前方受阻且着地时尝试跳跃。飞眼和蝙蝠在空中追击，空闲时保持出生高度附近巡逻。宝箱怪保持关闭姿态，玩家进入察觉范围后播放变形再行动。当前八种都使用近战攻击，没有远程弹幕、盾牌格挡、掉落或周期重生。

攻击有前摇；命中 tick 再检查玩家当前位置、身体间距、朝向和地形视线。视线逐格检查线段经过的地形；线段内部恰好经过格点时，两侧接触格也会遮挡，端点按向下取整后的所属格判定。离开范围或隔墙不会受伤，同一挥击只结算一次。受击中断怪物动作，致死后播放死亡动画并保留最后一帧至 `DeathTicks`，随后删除 Actor。尸体不再拦截构式弹体。

构式 `bolt` 的 damage 参数现在对活怪物结算，包含出生重叠与高速扫掠接触；原有 `on_hit` 事件和地形优先规则保留。木头人不装配生命组件，仍可反复测试；Player/Resident 不作为构式弹体的友伤目标。任意 Actor 的构式瞄准使用其实际 Body 尺寸。

启用怪物目录的会话为主角装配 100 生命值。受伤后 45 tick 无敌；生命耗尽立即回到保存的出生位置、恢复生命并获得 120 tick 保护。主角和活怪物的生命条使用同一提交的快照。未注入目录的 CPU/有限世界夹具沿用原来无怪物的场景；正常游戏默认启用目录。

只有成功固定步推进 AI、攻击和生命计时。暂停、环境预算重试和身体范围内缺失地形不会消耗怪物动作时间。Render 只读已提交的状态、朝向和动作 tick，重复绘制不推进动画。

## 呈现与保存

运行时将全部帧复制排列为一张 RGBA8 sRGB 图集，保留原色和 alpha。脚底锚点对齐实际碰撞盒底部中心，翻转时同步镜像锚点，贴图使用最近邻采样。源图含半透明受击/死亡帧；场景对象 pass 使用两张颜色目标交替合成可见怪物，正确保留重叠对象和透明边缘，随后绘制生命条、构式弹体与主角。F1 中不显示怪物或生命条。窗口资源重建保留图集，重建帧槽目标与描述符。

当前解码源图约 27.10 MiB，4056×2340 图集约 36.21 MiB，CPU 图集、上传缓冲、GPU 图集各持有一份；合计约 135.72 MiB，不含临时解码内存。每个帧槽另增一张 framebuffer 大小的 RGBA8 合成目标。每个可见怪物需要一次场景合成，奇数个时再复制回固定输出；当前每地区配置八个怪物候选，尚未做大量同屏怪物的性能优化。

`PixelGameplay 4` 增加怪物定义 ID、巡逻原点、动作/计时/朝向，以及生命、无敌时间和复活位置；v5增加生态候选、死亡墓碑和休眠对象；当前写入v6并读取v1～v5，另增[手持物品存档](../.awtcontext/GlobalDoc/HandheldItems.md)。恢复时从当前目录重新装配组件和身体尺寸，未知种类、生命上限不匹配或非法状态会失败；存档应配套同一份内容配置。旧档中的主角在启用怪物目录时获得生命组件，以载入位置为复活点；旧出生候选按固定 x=511 迁移，不重置死亡记录，新探索地区仍按群落生成。正常游戏仍采用现有临时世界模式，磁盘持久化由显式持久会话使用。

## 验证入口

```sh
cmake --build build-cpu --target PixelMonsterTests -j 6
ctest --test-dir build-cpu -R '^Pixel.Monsters$' --output-on-failure
cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j 6
(cd Pixel && PIXEL_MONSTER_CAPTURE=/private/tmp/pixel-monsters.ppm ../build/tests/PixelRuntimeSmoke --monsters-only)
```

`Pixel.Monsters` 检查真实清单/图片缓存、370 帧图集、错误配置与修正重试、攻击前摇/冷却/躲避/隔墙、飞行碰撞、宝箱怪唤醒、缺地形冻结、死亡清理、主角复活、构式扫掠与出生重叠伤害、值状态与磁盘恢复、新世界生成、镜像/暂停/F1 呈现。`--monsters-only` 对源图和 GPU 像素比较，覆盖每个帧的两个方向、半透明叠加、帧槽和 F1，再使用正式组合根验证新世界的八只怪物、移动和暂停。

GPU 新世界夹具固定使用 Natural、种子 42、512 格模拟窗口和 512 MiB 世界预算。只在构造游戏时覆盖这些环境变量，构造后恢复调用方环境，再启动模拟线程；任意自然种子仍可按产品规则跳过无法放置的怪物。`--monster-gameplay-only` 单独执行这一生产场景，便于验证外部种子和环境设置不影响夹具。

2026-11-10 初次接入的本机 macOS / Apple M5 / MoltenVK 验证：

- 资源核对：54 张 PNG 的文件内容与 SHA-256 均与 RawData 一致。
- CPU 全量：`ctest --test-dir build-cpu --output-on-failure -j4`，125/125 通过。随后修正恢复后的怪物无敌计时递减，并重跑 `Pixel.Monsters`、`Pixel.Actors`、`Pixel.Checkpoint`，3/3 通过。
- ASan/UBSan：上述三个专项在最终代码上 3/3 通过。
- 原生怪物 GPU：54 个 clip、370 帧的两个方向通过源图像素对照，32,780,000 个采样点包含 90 个半透明采样；叠加、帧槽、正式新世界生成、移动、暂停和 F1 通过。
- 旧场景 GPU：并行发布、背包画布、主角动画/移动、地形、元素、背景、风场、木头人、构式、两种育圃及两轮 resize/minimize/生命周期像素检查通过。
- `Pixel`、`Axiom`（AET）和 `PixelRuntimeSmoke` 构建通过；`Pixel.ModuleBoundaries`、头文件隔离、`git diff --check` 与 AWT 一致性检查通过。引擎源码与子模块锁定提交未变更。

完整 `PixelRuntimeSmoke` **未全通过**：旧世界批量点击及 Debug UI/构式编辑器/育圃 UI 的原生点击夹具无法获得窗口焦点，`glfwSetCursorPos` 后坐标保持不变。世界夹具请求 `(354.5,315.5)`，实际始终为 `(408.371094,444.921875)`，`focused=0`；单独执行也复现。已对齐放土测试的 Shift 修饰键，并为世界夹具增加焦点请求、坐标校验和明确错误信息；未改游戏输入或引擎来绕过该限制。分别运行 `--fields-only`、`--formula-only`、`--garden-only`、`--garden-aid-only`、`--lifecycle-only`，完成未受影响的渲染检查；带 UI 的命令仍因上述鼠标夹具返回失败。

当前没有可用的 Vulkan validation layer，未执行 Windows/Linux 原生窗口或 AET 窗口验证。正式游戏画面和验证日志保存在本地构建产物 `build/monsters/`。

2026-11-10 独立审查后的修正与复核：

- `gpt-6-astra / Ultra` 发现斜向视线漏格和随机 GPU 夹具两项 P2。视线现按格边距离的交叉乘积选择下一格，不累加浮点边界时刻；同时修正复核发现的非 45° 精确格角漏检。
- 新回归先在缺陷实现上确认失败，最终覆盖默认蘑菇攻击的四方向遮挡与无障碍控制、水平/垂直、零长度、格线终点、正反向 1:7/7:1 格角，以及近角无遮挡。
- 最终 CPU `Monsters / Actors / Checkpoint / ModuleBoundaries` 4/4 通过，ASan/UBSan `Pixel.Monsters` 1/1 通过；`Pixel` 和 `PixelRuntimeSmoke` 构建通过。
- 固定夹具的完整 `--monsters-only` 通过。最后一次视线调整后，分别以外部 seed 14（并注入 flat、128 格窗口与 128 MiB 预算）、seed 23、未设置 seed/generator 复跑 `--monster-gameplay-only`，三组均通过。
- 原 Astra Ultra 审查者重编原始复现：穿石格攻击的玩家生命保持 100；非 45° 格角的蝙蝠在无遮挡时移动、有阻挡时保持待机。独立重跑 CPU 4/4，通过最终复核，全部审查问题关闭。

修正验证日志使用 `build/monsters/review-*` 前缀。此前完整原生鼠标/UI 回归的焦点限制保持单独记录，本轮未重跑该部分。

## 群落适生率（2026-11-10）

`Gameplay.BiomeWeights` 按森林、沙漠、雪山配置整数百分比。Natural v2 在地区首次激活时按当地连续权重筛选候选，再检查地形与 Actor 占用；因此不保证每个新世界都会放置全部八种怪物。Flat 与 Natural v1 保留原候选行为，存档恢复活动／休眠Actor、候选完成记录和死亡墓碑。分布合同、居民与区域生命周期见[世界生态群落](../.awtcontext/GlobalDoc/WorldBiomes.md)。
