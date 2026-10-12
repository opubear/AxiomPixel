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
换成存储行号：`y < 424` 有风，`y >= 424` 仅受原有方向场影响，土地从 `y = 512` 开始。左键点击空格放置一个土块，右键点击移除一个土块。每格的 `CellState::Force` 保留兼容方向接口；连续基础受力由独立 `ForceField` 保存，Environment 叠加风贡献后移动土块（见逐像素基准章节）。
Environment 使用固定 1/60 秒步长，单帧最多补算 0.25 秒；玩家使用独立矩形碰撞体和重力，NPC 仍未实现。
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

## 地图块静态信息

`CellTypeId` 是强类型枚举，目前定义 `Air = 0` 和 `Dirt = 10`；`CellTypes::Empty` 是空气的兼容名称。每格的材料只保存此枚举，不保存质量、信息指针或类实例。原有 `Force` 仍属于空间状态。

`World/CellType.h` 中的 `CellDefinition<Type>` 模板特化各自拥有一份 `inline static constexpr CellInfo`，唯一属性为 `Mass`。目前采用游戏相对质量：空气为 0、泥土为 1，尚不参与运动或风场计算，也不代表真实空气密度。

```cpp
const auto &dirt = pixel::CellDefinition<pixel::CellTypeId::Dirt>::Info;
const auto &info = pixel::GetCellInfo(world.GetCell(x, y).Type);
float mass = info.Mass;
```

运行时查询返回对应模板特化的静态实例引用；未知枚举值抛出 `std::invalid_argument`。世界存储仍可承载显式转换的未注册 ID（用于现有障碍/渲染测试），但它们没有材料信息。新增正式材料时需增加枚举、模板特化及查询分支；外观映射由表现层单独维护。

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

`World` 默认关闭风，`PixelGame` 显式启用。`ConfigureWind` 重建风速，种子相同可复现；R 会恢复默认参数与初始风速。模拟使用现有 60Hz 固定步长，先推进风，再移动方块，最后统一施加泥土反馈并进行压力投影。风速单位是世界格/秒；移动时计算 `a_base / 60 + wind`（旧方向接口下等价于 `60 * Force + wind`），每分量大于 30 取 +1、小于 -30 取 -1，其余取 0。因此弱风不会移动静止物体，更强风也不会让方块单步跨越多格。

泥土反馈按横向和纵向分别计算投影遮挡：横向气流统计被泥土占据的行高并集，纵向气流统计列宽并集，不再用泥土面积占比作为两个方向共同的阻力。这样，伸入风区的高而薄土堆也能阻挡横向气流。实际泥土移动速度仍按占据面积平均到 MAC 面；未移动的泥土目标速度为零。所有移动链完成后才反馈，避免前面的方块改变后面方块在同一步采样到的风。

局部阻力按 `m = max(0.001, exp(-βB·dt))` 松弛到泥土速度，再使用相同响应权重求解 `div(M·grad(p)) = div(u*)`，以 `u = u* - M·grad(p)` 修正速度，使压力修正遵守方向性阻力。有限的最小响应保持压力系统连通；这仍是允许穿流的游戏近似。放置、移除、移动后的占据每步重算；移除泥土不再施加旧阻力，但已产生的气流扰动继续演化。

边界与限制：

- 风网格四边周期相接，采样位置超出活动矩形时返回零。周期边界只影响风，方块仍受世界边界阻挡。
- 泥土与风有双向影响，但属于粗网格阻力近似：允许有限穿流，不是逐像素硬墙或精确固体边界。障碍形状在粗网格上只保留两个轴的投影并集，不能恢复全部像素几何；不模拟浮力、温度或方块惯性，也不保证整个耦合系统的动量守恒。
- 仅活动风区中的泥土参与反馈；默认风区仍为 `y<424`，地面 `y>=512` 在风区之外，原有88格稳定天空保留。若在自定义世界调用 `ConfigureWind(worldHeight, settings)` 覆盖全世界，地面也会参与反馈，相应稳定天空不再保持纯重力。当前仅泥土类型参与，其他材料未定义风阻。
- 压力每次投影迭代 80 次，扩散迭代 20 次，属于游戏近似；高分辨率或极端长宽比下不保证很小的散度误差。
- 网格各维支持 2～128；背景风/脉冲每分量及扰动幅度上限为 1000，黏性范围 0～1,000,000，衰减率范围 0～100，泥土阻力范围 0～1000。反复脉冲限制实际收到非零增量的网格分量为 ±1000，零脉冲及范围外分量保持原值；压力投影在下一模拟步执行。非法参数拒绝且保留旧场。

观察默认风：先按 F3 切换自由调试相机，再按 PageDown 切换到顶部区域，或按 W 向上平移到 `y < 424` 后放置土块。F1 可以观察泥土周围合成场方向的变化；它只显示方向，纯减速而方向不变时颜色不会改变。默认视角仍对准地面，那里保持重力下落；当前没有风向箭头叠加。

## 力场调试视图

启动后显示 ImGui **Pixel Debug → Environment** 面板，**F2** 可隐藏或重新打开。`Wind start height` 是从世界底部计算的风场起始高度，默认 **600**：拖动滑块或 **Ctrl+单击**（macOS 为 **Command+单击**）输入整数即可实时生效。**0** 表示整个世界都有风，**1024** 表示关闭风；输入超出范围会被限制在合法范围。降低到 **512 以下**即可让部分地面进入风区，观察泥土与气流的双向影响。面板显示当前覆盖的行数，并提供默认高度按钮和力场方向视图复选框。

调整高度会按已有风参数重新初始化风速，保留泥土、基础力场、视角及显示模式；拖动过程中每次高度变化都会重新初始化。关闭再开启仍保留风参数。R 恢复默认世界和600高度。操作面板时鼠标和键盘由 ImGui 接收，不会穿透到世界编辑或视角移动；在面板外仍可正常操作游戏。UI 叠加在后处理之后，不受世界调色影响。

按 **F1** 在材质视图与力场方向视图之间切换（部分 Mac 键盘需按 Fn+F1）。调试模式会给整个世界着色，包括空格和土块所在格；模拟和鼠标编辑照常进行；F3 自由调试相机模式中 WASD 平移、`+/-` 缩放、PageDown 切区域仍可用。R 重建世界和视角，保留当前显示模式。默认地面附近是向下重力，颜色相同；向上移到 `y < 424` 可观察动态风造成的方向变化。

显示的是 Environment 用于移动的连续合成场 `v = GetForce / 60`（仅使用旧方向接口时等价于 `60 * CellState::Force + wind`），在格子中心采样、应用移动死区之前取值。先计算单位方向 `d = v / |v|`，再转换为线性 RGB：

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

启动默认进入标准模式，角色出现在地面中央。角色为 2×4 格的矩形，使用纯色头部与身体纹理；只写入渲染快照，不占用地图材料格。

- **标准模式**：A/D 左右移动，Space 跳跃，相机跟随角色。角色受重力影响，可站在泥土上，与墙壁、天花板和世界边界碰撞；挖掉脚下泥土后会下落。
- **F3**：切换标准模式与自由调试相机模式，也可使用面板底部的 `Free debug camera`。调试模式中 WASD 平移相机、PageDown 切换世界区域，输入不移动角色；世界和角色的重力模拟继续运行。切回标准模式重新跟随角色，并清空旧的按键状态。
- **通用操作**：`+/-` 缩放（每格 1～64 个 framebuffer 像素），F1 切换力场视图，F2 显隐面板，T 切换调色（调试相机模式中 Space 也保留旧调色操作），R 重建世界与角色并重置视角、保留当前控制模式，Escape 退出。

角色速度为 16 格/秒，跳跃初速度 32 格/秒，重力 90 格/秒²；碰撞按不超过 0.25 格的位移分段检测。角色目前不受风场影响，也不向泥土和风施力；泥土进入角色区域时优先向上脱离，整列无空间时停留等待空间恢复。鼠标不能直接把泥土放进角色内部。F1 是纯力场诊断图，不叠加角色外观；切回材质视图重新显示角色。
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

## 逐像素基础力场与风场 reference

`ForceField` 是与世界等宽等高的连续向量网格，独立于风求解器。`World::GetBaseForceField()` 保存持久基础加速度（像素/秒²，单位质量，正 Y 向下），可叠加重力、爆炸等来源；`GetForce(x,y)` 返回基础场与当前风贡献的合成值。风存储空气速度（像素/秒），由 Environment 调用 `FieldResponse` 的适配规则 `a_wind = 60 * u_air` 转换，关闭或重建风不会修改基础场。

```cpp
world.FillBaseForce({0, 3600});       // 兼容旧下落速度的基础重力
world.AddBaseForce(300, 200, {600, 0}); // 持久外力贡献；源撤销由调用方负责
world.SetBaseForce(301, 200, {0, 0});
auto acceleration = world.GetForce(300, 200);
pixel::PixelWindSettings reference;
reference.DivergenceTolerance = 1e-6;
world.ConfigurePixelWind(424, reference);
auto report = world.GetPixelWind().GetReport();
```

`WindSimulation` 独占一个风场后端，统一配置、采样与材料更新前后的推进；切换先完成新场构造，失败保留原场。`WindTypes` 定义共享数据类型，两个数值求解器互不依赖。`FieldResponse` 集中维护玩法单位换算和移动死区，固体表面受风采样位于适配层。`GetWind()` / `GetPixelWind()` 只提供诊断视图，返回的引用不可跨风场重配置或关闭操作保存。

旧 `SetForceDirection` / `FillForceDirection` / `SetCell` / `Fill` 将方向映射为 `3600 * direction`，以保持旧行为。直接编辑连续基础场后，`CellState::Force` 只保留最后一次旧接口写入的方向元数据；查询实际受力必须用 `GetForce`。`ReadState` / `PublishState` 仍只发布原格子状态，连续场不随材料或格子缓冲交换；F1 在帧首通过 `GetFieldVelocity = GetForce / 60` 生成独立渲染快照。旧的移动死区 ±30 和每步最多一邻格继续保留，没有新增物体速度积分或惯性。

`PixelWindField` 以一像素一个压力单元、共享面速度构成 MAC 网格，支持每维 1～2048。非空材料是不可穿透固体；界面法向面速度为零，切向采用 free-slip 近似。外边界四边周期相接，与旧场保持相同拓扑；不是开放进出口或封闭世界边界。显式黏性扩散后执行半拉格朗日平流，内部步长满足每轴位移不超过0.5像素以及 `ν*dt <= 0.2`，每子步以双精度 PCG 投影到散度容差。`GetReport()` 提供 RMS/最大散度、迭代次数、子步数与 `Converged`；达到迭代上限不会伪报收敛。

每个固定步开始及材料移动后同步障碍；固体内部的纯风采样为零，物体受风使用相邻暴露空气的平均速度。此版本将移动物体作为分步更新的静态几何，不实现移动壁面的排流、压力牵引或严格双向动量守恒。它适用于像素几何与静态流场对照；动态物体轨迹不应被当作物理真值。黏性上限100、松弛率上限100，风/脉冲分量上限1000；参数非法时保留原状态。

ImGui Environment 面板的 **Wind strategy → Pixel reference (1:1)** 启用该基准并自动暂停，可用 **Step 1/60 second** 单步，或取消 **Pause simulation** 连续运行。切换/调整高度会重建风；R恢复原默认粗场和运行状态。逐像素 CPU 压力求解成本高，尤其狭缝，不承诺实时。

```sh
cmake --build build-cpu --target PixelBaselineTests PixelWindBenchmark -j4
ctest --test-dir build-cpu --output-on-failure
# 性能测量请使用 Release/RelWithDebInfo 或 -O2，而非 Debug 数字
./build-cpu/bin/Debug/PixelWindBenchmark 64 48 4 /private/tmp/wind-samples.csv
# 大尺度初始化（steps=0）；非空复杂场景推进可能很慢
./build-cpu/bin/Debug/PixelWindBenchmark 1024 424 0
```

比较工具固定均匀初态，导出静态空场、整高单像素墙和单像素缝的求解成本、散度、固体面通量、旧粗场相对中心速度 RMSE，末尾可选参数导出逐像素 CSV。粗场使用有限阻力，reference 使用硬边界，误差包含模型差异，不是仅由分辨率引起的误差。`SetFaceVelocities` 支持确定的自定义 MAC 初态。测试另外验证离散 Fourier 黏性衰减、纯梯度投影和时间步细化。

设计比较与实际证据见 [设计报告](Docs/PixelWindDesignReport.docx)、[实现与审查记录](.awtcontext/GlobalDoc/PixelWindBaseline.md)。数值参考：[Bridson / Müller-Fischer, Fluid Simulation](https://www.cs.ubc.ca/~rbridson/fluidsimulation/)。


## 可切换风场策略（首版）

Environment 面板的 **Wind strategy** 可在六个版本间切换。切换重建该风场的内部状态，保留材料、基础力场及其他外力；不是保存并恢复每个版本的运行进度。关闭再开启保留所选策略，R 恢复默认粗 NS。Reference、自适应 NS 和 CPU LBM 选中后自动暂停，建议单步观察。

| 策略 | 实现与首版边界 |
|---|---|
| Coarse NS | 保留原粗 MAC 网格、方向性泥土阻力与实际运动反馈 |
| Pixel reference (1:1) | 保留逐像素 MAC + PCG baseline，用于对照 |
| Procedural + obstacles | 随时间连续变化的剪切阵风、脉冲及像素障碍近邻减速/偏转/尾流；没有全局压力求解 |
| Coarse NS + local model | 粗 NS 保留传播历史与泥土反馈，局部模型只修正采样，不将同一阻力再次反馈 |
| Adaptive NS (CPU prototype) | 混合障碍块递归细分，统一有限体积共享面通量与 PCG 压力，上风动量输运；首阶、叶内常量采样，数值扩散较强 |
| LBM D2Q9 (CPU prototype) | 逐像素 BGK 碰撞与周期 streaming、固体 bounce-back；弱可压缩，尚无 GPU 后端 |

```cpp
pixel::WindStrategySettings settings;
settings.Kind = pixel::WindStrategyKind::Hybrid;
settings.Local.Radius = 6;
world.ConfigureWindStrategy(424, settings);
auto air = world.SampleWind(512.5f, 200.5f); // 纯空气速度，不含重力
world.AddBaseForce(512, 200, {0, 120});      // 独立外力，切换风场不会清除
auto total = world.GetForce(512, 200);       // 基础场 + 材料受风贡献
```

`WindStrategy` 仅管理空气模拟、几何同步和空气/材料表面采样；`WindSimulation` 事务式独占策略。`ForceField` 不引用风类型，`FieldResponse` 负责单位换算，Environment 合成受力。新增策略需实现该接口、在 `MakeWindStrategy` 注册并补充选择项，数值求解不进入 World 或引擎。旧 `ConfigureWind`、`ConfigurePixelWind` 和诊断接口继续可用；`GetWind()` 仅诊断旧粗场，跨策略纯风查询使用 `SampleWind()`。

新模型均使用周期外边界；非空材料作为新模型的障碍。程序化/混合的局部作用半径为 1～32 像素，使用近似最近障碍距离，不保证无散、精确无穿透或孔洞通风。自适应方案保留像素几何，粗细面共享通量，但中心连线近似、首阶输运及重网格会引入误差；报告的散度是共享面通量散度，不是显示采样向量的散度。密集碎片可能细化到接近全像素，当前不设截断细化预算。三种硬障碍模型将移动物体按分步静态几何处理，没有移动壁面的排流与总动量守恒。

LBM 默认风速为 `(30,0)`、阵风 10，使用自己的低马赫数参数，并非其他版本默认 `(90,0)`、阵风 60。固定 lattice 时间默认 1/600 秒，初始 lattice 速度上限 0.2；`tau` 下限 0.51，面板报告实际黏性。脉冲/演化中平衡分布速度受限，负分布恢复至平衡分布并累计 `Stabilizations`，发生恢复时不可视为未经修正的物理解。静态障碍可检验空气质量守恒；编辑障碍会移除或补入空气。CPU LBM 与复杂自适应场不保证实时。

```sh
cmake --build build-cpu --target PixelWindStrategyTests
ctest --test-dir build-cpu --output-on-failure
cmake --build build-runtime --target PixelWindStrategyBenchmark
./build-runtime/bin/RelWithDebInfo/PixelWindStrategyBenchmark 64 4
./build-runtime/bin/RelWithDebInfo/PixelWindStrategyBenchmark 1024 1 skip-reference
```

比较工具使用相同低速均匀初态、静态空域/整高薄墙/单像素缝，计时包括策略的几何同步与反馈，不包括材料运动、渲染和 GPU。版本编号依次为表中 0～5；不适用的诊断值为 -1。单次测量仅作成本样本，不能当作等精度或完整游戏性能比较。实现、测试和审查记录见 [风场策略首版](.awtcontext/GlobalDoc/WindStrategiesV1.md)。
