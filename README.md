# AxiomPixel

基于 Axiom 的早期 Pixel 游戏，从引擎 `2DRender` 分支的 `5c8af21` 拆出。
`Axiom/` 是独立 Git 仓库，当前项目使用已移植通用渲染能力并合并远端修复的 **PixelMain** 分支；AET 样例与通用引擎代码留在该仓库。旧 PixelMain 分支已由当前合并状态替换。

| 路径 | 职责 |
|---|---|
| `Pixel/Source/World/` | 纯 CPU 世界、格子状态、双缓冲发布、人物和环境更新入口 |
| `Pixel/Source/PixelRender/` | 两遍渲染策略 `PixelRender`、渲染数据、世界到纹理的映射 |
| `Pixel/Source/PixelGame.*` | 游戏生命周期、输入、ImGui调试面板、世界所有权及快照发布 |
| `Pixel/Source/main.cpp` | 向引擎注入游戏和渲染策略 |
| `Pixel/Shader/` | Pixel 专用 Slang shader |
| `Pixel/Tests/` | 世界与渲染 mock 测试、真实 GPU smoke |
| `Axiom/` | 独立管理的引擎、RHI、AET 和引擎测试 |

世界为 1024×1024 格。游戏高度从底部向上计算，存储和渲染行号 `y` 从顶部向下计算，二者对应 `height = 1023 - y`。
底部 512 格为棕褐色土地；其上是蓝色天空。底部 600 格统一使用向下重力场，因此高度 `[512, 600)` 是 88 格稳定天空区；高度 `[600, 1024)` 叠加动态风场。
换成存储行号：`y < 424` 有风，`y >= 424` 仅受原有方向场影响，土地从 `y = 512` 开始。左键点击空格放置一个土块，右键点击移除一个土块。每格的 `CellState::Force` 保存该位置的基础受力方向，Environment 将它与风速合成后移动土块。
Environment 使用固定 1/60 秒步长，单帧最多补算 0.25 秒；人物/NPC 仍为空入口。
保留四张数据纹理（每张 1024×256）、每 Ready 帧全量上传、格子绘制和后处理两个 Pass。

方向场使用 `ForceDirection{X, Y}`，每个分量为 -1、0 或 1，支持八个邻格方向；无风时 `(0, 0)` 表示静止，正 Y 向下。方块每步最多移动到一个邻格，尚不保存自身速度或惯性；风场独立保存连续速度。
方向属于空间位置，不随土块移动；放置和移除土块也不会改变方向。方块进入新格后，下一个模拟步读取新位置的方向。越界、不可移动障碍和互相顶住的闭环会阻挡移动；连续排列的方块可跟随前方方块腾出的空位移动。争用目标格时，按行从上到下、从左到右发起的移动链依次处理，先到者占据目标格。

可通过纯 CPU World 接口修改场，无需改变渲染代码：

```cpp
world.SetForceDirection(x, y, {1, 0});  // 该位置向右，保留原有物体
world.FillForceDirection({0, -1});     // 整个世界向上，保留物体分布
auto force = world.GetCell(x, y).Force;
```

`SetCell` / `Fill` 替换完整格子状态（包含方向），保留正在运行的风。`GenerateTerrain` 生成地形、初始化向下场并清除风及模拟计时；游戏启动和 R 重置随后启用顶部 424 行风场。旧的 `RandomizeForceDirections(endRow, seed)` 仍可生成固定离散场，但不再用于默认游戏。基础方向随 WorldState 一起发布，风速属于 Environment；普通视图映射物体类型，F1 调试视图映射合成场的方向。

## 简化风场

`WindField` 是纯 CPU 的二维不可压缩 NS 近似，采用 [Stable Fluids](https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf) 的分步求解思路：半拉格朗日平流、隐式黏性扩散和压力投影。本实现使用交错 MAC 网格，使离散散度和压力梯度匹配。

求解的速度方程为 `du/dt + (u·∇)u = -∇p + ν∇²u + λ(U-u) + βB(v_dirt-u)`，目标为 `∇·u ≈ 0`。`U` 是背景风，`λ` 控制扰动向背景风的衰减；`B` 是按两个轴分别计算的泥土迎风遮挡比例，`v_dirt` 是泥土实际移动速度，`β` 控制泥土对风的阻力反馈。初始扰动为两组由种子控制相位的剪切波；之后由求解器推进，不逐帧随机。默认 32×16 网格覆盖 1024×424 个世界格，背景风向右 90 格/秒，扰动幅度 60 格/秒，黏性 8 格²/秒，衰减率 0.05/秒，泥土阻力系数 60/秒。

```cpp
pixel::WindSettings settings;
settings.Ambient = {90, 0};
settings.GustSpeed = 60;             // 设为0可从均匀风开始
settings.Viscosity = 8;
settings.Relaxation = 0.05f;
settings.DirtDrag = 60;              // 泥土对风的反馈；设为0可关闭反向耦合
settings.Seed = 20261004;
world.ConfigureWind(424, settings); // 顶部[0,424)行，保留基础Force和物体
auto velocity = world.GetWind().Sample(512.5f, 200.5f);
world.AddWindImpulse(512, 200, 64, {0, -120}); // 半径64格的向上脉冲
world.DisableWind();
```

`World` 默认关闭风，`PixelGame` 显式启用。`ConfigureWind` 重建风速，种子相同可复现；R 会恢复默认参数与初始风速。模拟使用现有 60Hz 固定步长，先推进风，再移动方块，最后统一施加泥土反馈并进行压力投影。风速单位是世界格/秒；移动时计算 `60 * Force + wind`，每分量大于 30 取 +1、小于 -30 取 -1，其余取 0。因此弱风不会移动静止物体，更强风也不会让方块单步跨越多格。

泥土反馈按横向和纵向分别计算投影遮挡：横向气流统计被泥土占据的行高并集，纵向气流统计列宽并集，不再用泥土面积占比作为两个方向共同的阻力。这样，伸入风区的高而薄土堆也能阻挡横向气流。实际泥土移动速度仍按占据面积平均到 MAC 面；未移动的泥土目标速度为零。所有移动链完成后才反馈，避免前面的方块改变后面方块在同一步采样到的风。

局部阻力按 `m = max(0.001, exp(-βB·dt))` 松弛到泥土速度，再使用相同响应权重求解 `div(M·grad(p)) = div(u*)`，以 `u = u* - M·grad(p)` 修正速度，使压力修正遵守方向性阻力。有限的最小响应保持压力系统连通；这仍是允许穿流的游戏近似。放置、移除、移动后的占据每步重算；移除泥土不再施加旧阻力，但已产生的气流扰动继续演化。

边界与限制：

- 风网格四边周期相接，采样位置超出活动矩形时返回零。周期边界只影响风，方块仍受世界边界阻挡。
- 泥土与风有双向影响，但属于粗网格阻力近似：允许有限穿流，不是逐像素硬墙或精确固体边界。障碍形状在粗网格上只保留两个轴的投影并集，不能恢复全部像素几何；不模拟浮力、温度或方块惯性，也不保证整个耦合系统的动量守恒。
- 仅活动风区中的泥土参与反馈；默认风区仍为 `y<424`，地面 `y>=512` 在风区之外，原有88格稳定天空保留。若在自定义世界调用 `ConfigureWind(worldHeight, settings)` 覆盖全世界，地面也会参与反馈，相应稳定天空不再保持纯重力。当前仅泥土类型参与，其他材料未定义风阻。
- 压力每次投影迭代 80 次，扩散迭代 20 次，属于游戏近似；高分辨率或极端长宽比下不保证很小的散度误差。
- 网格各维支持 2～128；背景风/脉冲每分量及扰动幅度上限为 1000，黏性范围 0～1,000,000，衰减率范围 0～100，泥土阻力范围 0～1000。反复脉冲限制实际收到非零增量的网格分量为 ±1000，零脉冲及范围外分量保持原值；压力投影在下一模拟步执行。非法参数拒绝且保留旧场。

观察默认风：按 PageDown 切换到顶部区域，或按 W 向上平移到 `y < 424` 后放置土块。F1 可以观察泥土周围合成场方向的变化；它只显示方向，纯减速而方向不变时颜色不会改变。默认视角仍对准地面，那里保持重力下落；当前没有风向箭头叠加。

## 力场调试视图

启动后显示 ImGui **Pixel Debug → Environment** 面板，**F2** 可隐藏或重新打开。`Wind start height` 是从世界底部计算的风场起始高度，默认 **600**：拖动滑块或 **Ctrl+单击**（macOS 为 **Command+单击**）输入整数即可实时生效。**0** 表示整个世界都有风，**1024** 表示关闭风；输入超出范围会被限制在合法范围。降低到 **512 以下**即可让部分地面进入风区，观察泥土与气流的双向影响。面板显示当前覆盖的行数，并提供默认高度按钮和力场方向视图复选框。

调整高度会按已有风参数重新初始化风速，保留泥土、基础力场、视角及显示模式；拖动过程中每次高度变化都会重新初始化。关闭再开启仍保留风参数。R 恢复默认世界和600高度。操作面板时鼠标和键盘由 ImGui 接收，不会穿透到世界编辑或视角移动；在面板外仍可正常操作游戏。UI 叠加在后处理之后，不受世界调色影响。

按 **F1** 在材质视图与力场方向视图之间切换（部分 Mac 键盘需按 Fn+F1）。调试模式会给整个世界着色，包括空格和土块所在格；模拟和鼠标编辑照常进行，WASD 平移、`+/-` 缩放、PageDown 切区域仍可用。R 重建世界和视角，保留当前显示模式。默认地面附近是向下重力，颜色相同；向上移到 `y < 424` 可观察动态风造成的方向变化。

显示的是 Environment 用于移动的连续合成场 `v = 60 * CellState::Force + wind`，在格子中心采样、应用移动死区之前取值。先计算单位方向 `d = v / |v|`，再转换为线性 RGB：

```text
R = 0.5 * (d.x + 1)
G = 0.5 * (d.y + 1)
B = 0.5
```

所有分量均在 `[0,1]` 内；零向量单独显示为 `(0.5,0.5,0.5)` 灰色。负方向经过平移缩放，不会直接截断成黑色。

| 方向（正 Y 向下） | 线性 RGB |
|---|---|
| 右 / 左 | `(1, 0.5, 0.5)` / `(0, 0.5, 0.5)` |
| 下 / 上 | `(0.5, 1, 0.5)` / `(0.5, 0, 0.5)` |
| 左上 / 右下 | 约 `(0.146, 0.146, 0.5)` / `(0.854, 0.854, 0.5)` |
| 零 | `(0.5, 0.5, 0.5)` |

调试模式跳过后处理调色，切回后恢复游戏的调色设置。颜色编码保留方向，不表示力的大小；低于移动死区的非零向量仍显示方向，不代表方块一定移动。颜色量化为 RGB8，最终屏幕通过已有 sRGB 输出流程显示。

World 仅通过 `GetFieldVelocity(x,y)` 提供数值，颜色转换位于 PixelWorldPresentation。帧首采样并写入渲染拥有的快照，Draw 不读取实时 World/Wind。模式和数据一起发布，复用四张 R32UInt 数据纹理；普通模式存材质 ID，调试模式存 RGB8，不增加纹理数量或绘制 Pass。调试模式有额外的全世界采样与颜色转换开销，关闭后不执行该转换。

## 构建和运行

需要 CMake 3.23+、C++20 工具链，以及 `Axiom/` 已包含通用渲染能力的 PixelMain 引擎工作副本。
引擎版本见 [AxiomVersion.json](AxiomVersion.json)，该版本包含 ImGui 面板所需的 UI 与 3D 上下文解耦支持。
引擎仓库由本项目 `.gitignore` 排除，需单独保存和交付；未经拆分的 `2DRender` 仍包含游戏目标，不应直接替代当前引擎版本。原始 devmain `de013e1` 缺少所需接口，本地 `c7fbb59` 已补齐并重新通过 CPU/GPU 验证。

```bash
git -C Axiom submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target Pixel Axiom -j4
cmake --build build --target run-pixel
```

默认使用引擎内置 Slang 编译器。若已有兼容的编译器，可在配置时指定
`-DAXIOM_SLANGC_EXECUTABLE=/absolute/path/to/slangc`。
`AXIOM_USE_PRECOMPILED_SHADERS=ON` 需要显式提供包含引擎六个和 Pixel 三个产物的目录；引擎原有的六个预编译文件不足以运行 Pixel。

`run-pixel` 使用 `Pixel/` 作为工作目录，以加载自己的 `Config/GameConfig.json`。
直接运行可用 `(cd Pixel && ../build/bin/Debug/Pixel)`；运行 AET 则用
`(cd Axiom && ../build/bin/Debug/Axiom)`。多配置生成器需在构建时增加 `--config Debug`。
Pixel 的素材仍由代码生成，`Content/Root.json` 只声明游戏名称，尚无资产或关卡读取逻辑。

启动时视角居中对准 512 行分界线。WASD 平移，`+/-` 以视口中心缩放（每格 1～64 个 framebuffer 像素），F1 切换力场调试视图，F2 显隐调试面板，Space 切换后处理调色（仅材质视图可见），PageDown 切换世界区域，R 重建初始世界并重置视角，Escape 退出。
鼠标每次按下只编辑一格，支持平移、缩放及高分屏坐标；世界或窗口外的点击忽略。光标位置、窗口和 framebuffer 尺寸在事件采集时一起保存，同一批多个点击分别使用自己的位置，不会跟随后续光标移动。

## 验证

```bash
# 不初始化窗口或GPU：包含Pixel和AET的CPU/mock回归
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

# 需要实际窗口服务和Vulkan驱动；不自动注册到默认CTest
cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON -DAXIOM_BUILD_LIFECYCLE_SMOKE=ON
cmake --build build --target PixelRuntimeSmoke AxiomLifecycleSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
(cd Axiom && ../build/tests/AxiomLifecycleSmoke)

# 引擎本身仍能独立构建与运行AET
cmake -S Axiom -B build-engine -DAXIOM_BUILD_FEATURE_SHOW=OFF
cmake --build build-engine --target Axiom -j4
```

迁移来源、模块边界和本轮验证见 [迁移记录](.awtcontext/GlobalDoc/PixelMigration.md)。
AWT 工具位置由本次环境提供，在当前布局可用 `../AIWorkTask/awt task list --project .awtcontext`。
