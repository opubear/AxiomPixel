# AxiomPixel

基于 Axiom 的早期像素游戏，从引擎 `2DRender` 的 `5c8af21` 拆出。`Axiom/` 是跟踪 `PixelMain` 的 Git 子模块，父仓库 gitlink 与 [AxiomVersion.json](AxiomVersion.json) 锁定准确提交；AET 和通用引擎代码留在子模块。相邻 `../Axiom` 仅作只读参考。

世界现按需加载1024×1024物理块，由稀疏四叉树组织64×64区域，采用同步材料更新、文件存档和跟随玩家的开放风域。现行接口、存储布局和失败处理见 [世界表示约定](Docs/WorldRepresentation.md)，本轮实测及验证限制见 [验证与性能报告](Docs/WorldRepresentationValidation.md)。早期文档中的整图连续数组、移动链和四张固定世界纹理已被替代。

| 路径 | 职责 |
|---|---|
| `Pixel/Source/Application/` | 应用装配、引擎输入转接、生命周期与运行时渲染策略 |
| `Pixel/Source/Gameplay/` | GameSession持有世界，管理存档、暂停、单步、时间及风配置 |
| `Pixel/Source/World/` | 纯CPU WorldMap、分块/四叉树、流式与角色；Environment管理材料及风 |
| `Pixel/Source/Input/` | GameInput处理键鼠、控制模式、编辑、相机和重置 |
| `Pixel/Source/Camera/` | 分段连续世界坐标、跟随、平移、缩放和事件选格 |
| `Pixel/Source/PixelRender/` | 帧首区域快照、外观映射、每frame slot缓存及GPU上传 |
| `Pixel/Source/Debug/` | ImGui调试与性能面板 |
| `Pixel/Source/Performance/` | 无渲染依赖的采样核心及独立运行时报告 |
| `Pixel/Shader/` | Pixel专用Slang shader |
| `Pixel/Formula/` | 独立构式DSL、不可变组合、CPU模拟器和CLI原型 |
| `UnitTest/` | CPU/mock、Gameplay、Rendering、Runtime、Formula及显式离线基准 |
| `Axiom/` | 引擎、RHI、AET及引擎测试 |

核心依赖为 `PixelGameplay → PixelWorld → PixelPerformance`，World另外链接 `Axiom::Core` 使用容器和对齐allocator，不依赖窗口、RHI或UI。PixelPresentation在CPU上转换快照；只有运行时层接入引擎和ImGui。模块边界沿用 [模块化系统设计](Docs/Architecture/PixelModularDesign.docx)，其旧世界所有权细节以新世界约定为准。

## 构建和运行

需要CMake 3.23+、C++20、引擎锁定子模块及运行时Slang/Vulkan工具链。新应用通过 `CreateApplication → PixelApplication::CreateGame → PixelGame::ConfigureEngine` 接入引擎；窗口配置位于 `Pixel/Config/GameConfig.json`。

```sh
git submodule update --init --recursive
./build-debug.sh
./run-debug.sh

./build-release.sh
./run-release.sh
```

构建与启动入口已拆分，替代原 `build-and-run.sh`。Debug使用 `build/`，Release使用独立的 `build-release/`；构建脚本只配置并编译Pixel。启动脚本自动切到 `Pixel/`，透传启动参数和环境变量，缺少程序时提示对应构建命令。脚本支持从任意工作目录调用；`run.sh` 保留为 `run-debug.sh` 的兼容入口。

手动构建AET可运行 `cmake --build build --config Debug --target Axiom -j4`。项目不自动追踪远端引擎最新提交；升级引擎时同时更新gitlink与AxiomVersion.json。修改只在本地 `Axiom/` 的PixelMain进行，保留AET；Pixel专用shader、缓存和测试放父仓库。

游戏默认从运行目录的 `Saves/current.txt` 恢复当前世界；首次生成新世界。每块文件位于 `Saves/<world-id>/chunks/`，正常退出和面板 `Save world` 写盘；R先保存再创建新世界，旧目录保留。存档已加入忽略规则。可在启动前覆盖：

```sh
(cd Pixel && PIXEL_SAVE_ROOT=/private/tmp/pixel-saves \
  PIXEL_SIMULATION_WINDOW=1024 PIXEL_WORLD_BUDGET_MIB=512 \
  ../build/bin/Debug/Pixel)
```

窗口边长须为64的倍数且不超过2048；预算包含地图、索引、缓存、材料scratch、CPU快照和流式工作余量，风后端与GPU另计。大视口会在预算内缩小发布范围，其余位置显示加载占位。缺失地图阻挡人物与材料，编辑返回NotReady；I/O失败在Environment面板显示并可重试。保存失败保留内存数据并报告异常，本版没有跨文件崩溃恢复。

## 世界与操作

默认无限平坦世界：世界y<512为空气，y>=512为泥土；正Y向下，任意正负X都延伸。玩家模拟窗口默认1024×1024，按64格对齐并在玩家离开中央512×512后重新居中；远处冻结并卸载保存。自由相机只改变显示加载范围。

| 操作 | 行为 |
|---|---|
| A / D，Space | 标准模式移动、跳跃；相机平滑跟随 |
| F3 | 切换自由调试相机，清理持续按键状态 |
| WASD，+ / -，PageDown | 自由模式平移、缩放、切到下一区域 |
| 左键 / 右键 | 放置 / 移除一个土块；人物内部不能放置 |
| F1 | 材料 / 合成力场方向视图 |
| F2 | 显示或隐藏Pixel Debug |
| R | 保存当前世界，创建新世界，重置视角；保留显示/控制模式 |

启动和R后默认暂停。Environment的 `Pause simulation` 和 `Step 1/60 second` 控制运行；暂停冻结材料、风和动画，相机/加载仍工作。引擎顺序保持帧首快照发布 → 输入与游戏推进 → Draw消费发布数据；Skip不推进，Draw不读取实时World。

材料采用60 Hz同步规则：只向步首为空的邻格申请；同目标按源坐标(y,x)裁决；统一提交后反馈风场。长链空位通过多个步传播，规则不依赖四叉树遍历顺序。静止无风区域休眠；编辑、材料成功移动、基础力变化和加载唤醒邻域。方块不保存速度/惯性，空间力不随材料搬迁。

`CellTypeId`为32位强类型枚举，当前Air=0、Dirt=10。`CellDefinition<Type>::Info`是静态质量信息（空气0、泥土1），尚不参与动力学；未知材料可作为障碍存储，但GetCellInfo会拒绝未知ID。外观映射只在表现层。

```cpp
world.SetForceDirection(-1, 500, {1, 0}); // 兼容八方向；不改变材料
world.SetBaseForce(-1, 500, {600, 0});    // 空间加速度，格/秒²
world.AddBaseForce(-1, 500, {0, 120});
auto force = world.GetForce(-1, 500);    // 基础力 + 当前材料受风贡献
// 无限地图批量修改必须指定已驻留范围
world.FillRegion({-64, 0, 64, 64}, {pixel::CellTypes::Empty, {0, 1}});
```

方向压缩为两个int8分量（-1/0/1），兼容方向写入映射为 `3600 * direction` 基础力。实际移动速度按 `baseForce / 60 + wind` 换算，各分量超过±30才移动，最多一邻格。直接编辑基础力不重写兼容方向，查询实际场使用GetForce。`World(width,height)`仍可用于有限小地图测试；全图Fill仅对有限地图开放。

角色为独立2×4格矩形，速度16格/秒、跳跃32格/秒、重力90格/秒²，按≤0.25格分段碰撞。角色不受风，不向泥土施力；地图缺失作为实体阻挡。相机与人物使用块坐标加块内double，GPU接收相机附近相对值，远距离仍保留亚格精度。相机跟随系数默认8/s，可在面板调整；角色使用连续屏幕矩形覆盖合成，不写入地图材料。

## 风场和显示

Environment可在Empty、Coarse NS、Pixel reference、Procedural、Hybrid、Adaptive NS、LBM D2Q9七策略间切换，默认Empty。Reference、Adaptive和LBM切换后自动暂停，建议先单步。切换重建当前风后端，保留地图、基础力及物理时钟；不保存每策略的独立运行历史。

`Wind boundary Y`是世界坐标，默认424：风域为玩家窗口与y<424的交集，地表512上方保留88格无风带。风域随玩家移动，迁移重叠速度/分布，新进入区域由背景风初始化；离开窗口的动态扰动丢弃。正常游戏使用流入/流出开放边界，周期模式仅在显式数值测试中开启。

```cpp
pixel::WindStrategySettings settings;
settings.Kind = pixel::WindStrategyKind::Coarse;
settings.Coarse.Ambient = {90, 0};
settings.Coarse.GustSpeed = 60;
world.ConfigureWindDomain(424, settings);
auto air = world.SampleWind(pixel::CellCoord{-1, 200}); // 远坐标精确格中心采样
world.AddWindImpulse(100, 200, 64, {0, -120});
```

粗NS是MAC平流/扩散/投影与有限泥土阻力；reference为逐像素MAC/PCG；程序风与Hybrid使用局部障碍模型；Adaptive是有限体积CPU原型；LBM为弱可压缩BGK并有稳定化计数。数值误差、迁移方法及适用边界见世界约定。复杂自适应、1:1和CPU LBM不承诺实时，也不实现完整移动壁面动量守恒。

F1在格中心把合成速度单位方向映射为 `RGB=(.5+.5*dx, .5+.5*dy, .5)`；零向量灰色，颜色不表示强度，跳过人物与风效。普通视图顺序为世界 → 风效 → 人物 → 后处理 → UI。Wind appearance可选Off、Short trails、Air refraction，调可见度/折光/视觉流速不改变物理。

发布只复制可见64格区域的变化版本，额外覆盖32格风回溯边界；每个frame slot独占动态尺寸纹理与版本。稳定材料区域不重复映射或上传；F1/外观切换、进入新区、加载/卸载及GPU重建分别失效。风纹理每分量signed16、单位1/32格/秒，固体特殊标记；shader和缓存均属于Pixel。本地引擎只增加通用纹理区域上传接口，保留整图入口。

## 验证和性能

```sh
# 无窗口/GPU：Pixel、Formula和Axiom CPU/mock回归
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

# 同一存储/流式/风域测试使用另一块尺寸
cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-chunk64 --target PixelChunkTests PixelPersistenceTests PixelWindDomainTests -j4
ctest --test-dir build-chunk64 -R 'Pixel.(Chunks|Persistence|WindDomains)' --output-on-failure

# 需要窗口服务与Vulkan驱动；不注册到默认CTest
cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON -DAXIOM_BUILD_LIFECYCLE_SMOKE=ON
cmake --build build --target Pixel Axiom PixelRuntimeSmoke AxiomLifecycleSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
(cd Axiom && ../build/tests/AxiomLifecycleSmoke --skip-minimize)

# AET仍可单独构建
cmake -S Axiom -B build-engine -DAXIOM_BUILD_FEATURE_SHOW=OFF
cmake --build build-engine --target Axiom -j4

# 显式Release基准，不加入默认构建/CTest
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelRegionBenchmark -j4
./build-region-release/bin/Release/PixelRegionBenchmark /private/tmp/pixel-regions.csv 40
```

Environment显示驻留/排队块、预算与I/O错误；Performance显示阶段时间、P95、材料访问格数、活动区域、上传量及内存，支持CSV。内存为所有权容量与工作预留，不等于进程RSS；风求解器单列，GPU不计。自动捕获和扩展方式见 [采样模块](Pixel/Source/Performance/README.md)。本轮结果以 [新规则基准和最终验证](Docs/WorldRepresentationValidation.md) 为准，早期测量保留于Docs/PerformanceProfiling.md、MemoryProfiling.md和MaterialUpdatePerformance.md。

Linux自动鼠标检查需要X11/XWayland的DISPLAY；GLFW Wayland不能设置光标，可设置 `XDG_SESSION_TYPE=x11` 运行smoke。2026-10-19历史RTX 3070/X11完整Pixel smoke曾在后续生命周期阶段报 `no drawable 2D frame`；Axiom `--skip-minimize`两轮通过，该历史结果不覆盖最小化恢复。本机Apple M5完整Pixel smoke通过（含两轮最小化恢复），AET --skip-minimize两轮通过；validation layer不可用，完整范围见验证报告。

## 构式（Formula）原型与AWT

构式是不可变、可嵌套的行为描述。独立 `Pixel/Formula` 实现let复用、管道修改、散射、延迟重复与命中子构式，输出弹体/风/挖掘事件；当前仍未接入真实World、碰撞、伤害或费用系统。

```sh
cmake -S Pixel/Formula -B build-formula -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula -j4
ctest --test-dir build-formula --output-on-failure
./build-formula/PixelFormulaDemo Pixel/Formula/Examples/impact_burst.formula --ticks 30 --hit-at 6
```

`--hit-at`是显式命中注入。语法见 [Formula README](Pixel/Formula/README.md)，架构与实现边界见 [.awtcontext/GlobalDoc/FormulaLanguageArchitecture.md](.awtcontext/GlobalDoc/FormulaLanguageArchitecture.md)。

本布局AWT入口为 `../AIWorkTask/awt task list --project .awtcontext`；状态通过CLI维护，工程文档见 [.awtcontext/GlobalDoc/DocIndex.md](.awtcontext/GlobalDoc/DocIndex.md)。
