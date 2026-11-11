# AxiomPixel

游戏总纲：**在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。**

策划与项目管理从 [游戏文档总入口](Docs/GameDesign/README.md) 进入，按总纲、系统、现状与待办、技术合同逐层阅读。完整检索见 [AWT 文档索引](.awtcontext/GlobalDoc/DocIndex.md)。

当前是可运行的开发原型：默认 Natural v2 新世界并运行，普通启动、R 重置和关闭均不保存进度。底层检查点只在显式持久会话与测试中启用。以下“已实现”表示当前工作区能力，不表示完整游戏内容或发布验收完成。

| 游戏系统 | 当前范围与详细入口 |
| --- | --- |
| [世界、环境与生态](.awtcontext/GlobalDoc/SystemsWorld.md) | 森林/沙漠/雪山、昼夜四季、地形水体、元胞材料、风/力场、地区生物生成与休眠 |
| [角色与战斗](.awtcontext/GlobalDoc/SystemsActors.md) | Actor组件、玩家移动、固定木头人、八种怪物与构式伤害；居民已有简易几何呈现 |
| [构式、物品与背包](.awtcontext/GlobalDoc/SystemsFormulaItems.md) | formula/2、木棍装载施放、14类方块资源、掉落拾取、7快捷格与35展示格 |
| [设备、家园与会话](.awtcontext/GlobalDoc/SystemsHome.md) | 推力设备、煤燃料、力场育圃、活动区冻结与底层一致检查点 |
| [知识、世界意志与自主权](.awtcontext/GlobalDoc/SystemsWorldWill.md) | 授权观测、知识历史、离线PseudoLLM祈祷、受限干预与保护撤销 |
| [画面、交互与内容资源](.awtcontext/GlobalDoc/SystemsPresentation.md) | 群落背景/装饰、人物怪物动画、原生库存UI、独立显示缩放与资源来源 |

规划中的通用装备与资源掉落、完整居民生活、真实网络模型等尚未交付，决策与依赖见 [产品现状与待办](.awtcontext/GlobalDoc/ProductStatus.md)。2026-10 的 [架构规划](Docs/Architecture/GameplayArchitecturePlan.md) 和阶段验收归入历史依据，不是当前路线图或当前测试结果。

## 工程架构与模块

基于 Axiom 的早期像素游戏，从引擎 `2DRender` 拆出。`Axiom/` 是跟踪 `PixelMain` 的 Git 子模块，父仓库 gitlink 与 [AxiomVersion.json](AxiomVersion.json) 锁定准确提交；AET 和通用引擎代码留在子模块。相邻 `../Axiom` 仅作只读参考。

[世界表示](Docs/WorldRepresentation.md) 与 [空间场存储](Docs/SpatialFieldStorage.md) 维护当前分块、预算与发布接口；[单位边界](Docs/WorldUnits.md) 规定游戏float厘米与模拟格单位的显式转换。[Core容器接入](.awtcontext/GlobalDoc/CoreAllocatorAndContainers.md) 和 [Platform查询](Axiom/Source/HAL/Platform/README.md) 维护引擎消费边界。

引擎已提供 [FreeType/HarfBuzz 字体与文字塑形](Axiom/Source/Render/Text/README.md)：依赖直接挂在 `Axiom/ThirdParty`，CPU 布局可独立使用，原生文字由 Runtime 的 RHI renderer 绘制。当前 Pixel 菜单／背包继续使用已有像素画布文字，尚未迁移到该字体模块。

[引擎本地化](Axiom/Source/Localization/README.md) 提供 `LocalizedString` 和语言目录，`Axiom/Content/Localization` 保存英文／中文及地区示例表。业务文本可按稳定键解析并随语言切换；Pixel 自身转换表和语言选择界面尚未接入。

`Pixel/Source` 按运行时与开发支持分成两个根目录；以下五个领域是源码归属入口。

```text
Pixel/Source/
├── Runtime/
│   ├── Gameplay/     # 游戏规则、角色、相机、物品、设备、构式宿主与世界意志
│   ├── Simulation/   # 世界状态、材料/水体、力场/风场、生成及流式
│   ├── Render/       # 呈现快照、美术资源、GPU渲染和Shader
│   └── System/       # 通用LLM、I/O、序列化、独立Formula及IO/Input输入适配
└── Dev/
    └── Debug/        # Garden、性能采样、调试UI、编辑器、fake模型与观察demo
```

| 目录（相对 `Pixel/Source`） | 模块与职责 |
|---|---|
| `Runtime/Gameplay/` | 固定步协调与完整提交状态视图 |
| `Runtime/Gameplay/GameSession, Actions, Construction/` | 会话生命周期、动作、建造与跨领域提交；主动查询和影响Simulation |
| `Runtime/Gameplay/Actors, Camera/` | Actors管理身份与生命周期；主角/居民/木头人统一为Actor，以BodyComponent和可选FormulaComponent组合能力，Body负责运动碰撞，PlayerInput表达意图；相机跟随/缩放/选格 |
| `Runtime/Gameplay/Ecology/` | 群落适生率、地区候选记录、Actor休眠与恢复；筛选构建在PixelActors，生命周期构建在PixelGameplay |
| `Runtime/Gameplay/UI/` | 原生背包和ESC菜单状态、库存投影、布局、命中测试与共享像素画布；不依赖Render/Input/ImGui |
| `Runtime/Gameplay/Items, Devices, Authority, Knowledge/` | 游戏物品、设备、权限、知识与重要历史 |
| `Runtime/Gameplay/GameAPI, Observation, LLMView/` | 类型化动作/回执、领域观测投影、授权冻结查询与模型派生记忆 |
| `Runtime/Gameplay/LLM, WorldWill/` | 游戏目标/提案、DecisionAPI、PseudoLLM、游戏JSON协议、服务适配与世界意志行为 |
| `Runtime/Gameplay/FormulaBridge, Persistence/` | 构式游戏宿主和额度规则、领域保存DTO/编解码、SessionStore存档世代策略 |
| `Runtime/Gameplay/Application/` | 组合根：装配游戏、Input、系统提供者、Render与DebugUI，编排引擎生命周期 |
| `Runtime/Simulation/World/` | WorldMap、WorldStreamer、Generation、Environment；被动接收修改/推进并提供状态查询 |
| `Runtime/Render/` | CPU呈现与外观映射、快照和GPU缓存；`Assets/`管理游戏美术清单，`Shader/`保存Pixel shader |
| `Runtime/System/LLM, IO, Serialization/` | 与游戏领域无关的异步文本模型接口、文件发布/校验/错误处理、有界文本序列化 |
| `Runtime/System/Formula/` | 独立C++20过程构式语言、编译产物、运行时与CLI，可单独构建 |
| `Runtime/System/IO/Input/` | GameInput处理键鼠、控制模式和设备布置；PresentationControl表达显示意图，宿主回调提供应用操作 |
| `Dev/Debug/Garden, Performance/` | 显式育圃贯穿场景；无UI采样核心与运行时性能报告 |
| `Dev/Debug/Formula/` | formula/2演示预设、启动配置与原生Session动作接入；不进入游戏规则或语言模块 |
| `Dev/Debug/UI, LLM, Tools/` | ImGui面板与过程源码编辑器、测试专用FakeModel和观察demo |

`Pixel/Content` 和 `Pixel/Config` 保留运行时资源/配置，自动化测试继续集中在 `UnitTest`，引擎及AET留在 `Axiom` 子模块。

Gameplay拥有游戏意图和领域状态：角色、物品、设备、构式宿主查询Simulation的环境，并通过明确接口提交影响。Simulation不会调用Gameplay或Render，也不依赖Formula；材料、水体、力场和世界生成规则位于Simulation，玩家移动/跳跃/水体响应等角色规则位于Gameplay。`SimulationCoordinator`属于Gameplay，因为它协调多个游戏领域的固定步、暂停和完整提交。

Render从Simulation及Gameplay的已提交状态构造呈现快照，Draw只消费快照。相机投影和鼠标选格属于Gameplay；`System/IO/Input`通过`PresentationControl`表达显示意图，Render实现该接口，Application作为组合根负责接线。Gameplay核心不依赖Render或Input。

引擎采用[游戏与渲染隔帧并行](Docs/Architecture/ParallelFrames.md)，Pixel 另启用[独立模拟线程](Docs/Architecture/SimulationThread.md)。游戏线程从已提交世界计算角色呈现和相机，世界动作排队至下一固定步；模拟线程完成环境、权威角色、构式和设备后才发布世界。主线程绘制上一帧的自持有地形、风、角色和 UI 数据。当前帧末仍等待双方完成，窗口、输入、DebugUI、资源发布和关闭保留同步边界。PerfTest trace 分为渲染、游戏、模拟三条时间线；AET 保留原有双线程接线。

`Render/GamePresentation`在帧首构造`WorldGridFrame`；`PixelRender::Draw`仅编排`Render/Passes`中的上传、世界、风、场景物体、角色和后处理Pass。具体Pass封装纹理状态切换、复制、管线绑定及绘制命令；场景物体Pass合成设备、育圃和构式弹体，后处理Pass完成后在呈现目标内调用原生背包与ESC菜单UI，再调用开发调试UI叠加（菜单打开时隐藏开发面板）；UI不受场景调色影响。共享GPU资源仍由PixelRender管理生命周期，区域上传版本与首次美术上传标记仅在整帧录制成功后提交。

`System/IO/Input`拥有键鼠事件解释、按键状态和控制模式，通过GameSession动作端口及GameCamera驱动游戏；`Gameplay/Actors/PlayerInput.h`保留角色需要的横向移动与跳跃意图。输入适配可以依赖Gameplay，Gameplay核心、Simulation及其余System服务不反向依赖Input；Input不包含Render实现或ImGui，退出、调试面板及画面尺寸由Application注入回调。

System中的通用服务不包含角色、设备、世界或权限类型；`System/IO/Input`单独承担依赖Gameplay的输入适配，构建为独立的`PixelInput`，文件服务`PixelIO`不链接输入模块。`System/LLM/ModelService`仅传递自持有的文本、请求ID和响应上限；`Gameplay/LLM/Services/ServiceLLM`将已授权观测编码成请求，再将回包校验为游戏提案。LLMView、DecisionAPI、PseudoLLM及WorldWill都属于Gameplay。组合根可将`ServiceLLM`作为现有LLM实现注入；默认仍使用离线PseudoLLM，尚未接入真实网络提供者。

文件原子发布、摘要校验与可复用文本格式位于System；世界区块格式、checkpoint世代策略及领域DTO仍由对应的Simulation/Gameplay模块定义。构式语言不依赖游戏，游戏参数校验和世界适配留在Gameplay；源码编辑器属于Dev/Debug；旧树形编辑器已删除。

Dev/Debug保留当前可运行的开发功能。各领域允许调用无UI的Performance插桩；GameSession在显式选择育圃场景后调用Garden规则，仍由Session持有场景状态并按原存档格式保存。Garden只接收所需世界/角色/构式对象，不访问GameSession私有实现。它依赖底层`PixelFormulaBridge`，不会反向链接`PixelGameplay`；DebugUI由Application装配。这些诊断/场景接点不改变Simulation的单向边界。

核心构建依赖为`PixelGameplay → PixelActors → PixelWorld`、`PixelGameplay → PixelFormulaBridge → PixelFormulaProcess`和`PixelPresentation → PixelGameplay/PixelCamera`；`System/IO/Input`构建的`PixelInput`仅链接Gameplay与Camera。`Pixel.System`单独链接System工具验证可复用边界，`Pixel.ModelService`验证协议适配/取消/容量/错误，`Pixel.ModuleBoundaries`检查System通用服务、Simulation、Gameplay与IO/Input的引用方向。旧模块设计图中的目录与世界所有权以此处及[世界表示约定](Docs/WorldRepresentation.md)为准。

各系统的玩家规则、已实现范围与未交付项维护在上表的系统文档；历史 M1–M6 验收保留于 [AWT 索引](.awtcontext/GlobalDoc/DocIndex.md) 的“历史”类。

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

引擎最大帧率由 `Pixel/Config/GameConfig.json` 的顶层 `MaxFPS` 设置，缺省为 `60`，`0` 表示不限帧。命令行优先于配置文件，两种启动构建都适用：

```sh
./run-release.sh --max-fps 120
./run.sh --max-fps 0
```

也接受 `--max-fps=120`；值须为非负32位整数，重复参数取最后一个。`0` 跳过限帧休眠和自旋，GPU/交换链同步仍保留，实际 FPS 可能受显示和工作负载限制。启动日志包含最终 `max_fps` 和 `max_fps_source`。

同一文件的顶层 `VSync` 控制呈现同步，必须为 JSON 布尔值，缺省为 `true`；修改后重启生效。`true` 保留 Mailbox 优先、FIFO 回退的同步策略；`false` 优先使用 Immediate，允许应用帧率超过屏幕刷新率，但可能出现撕裂。设备不支持 Immediate 时回退到 Mailbox／FIFO 并记录警告。窗口重建沿用启动配置，日志 `event=swapchain_created` 显示实际 `present_mode`。

`VSync` 与 `MaxFPS` 独立；关闭垂直同步并取消主动限帧，在原配置中设置：

```json
"MaxFPS": 0,
"VSync": false
```

也可以只设置 `"VSync": false`，启动时用 `--max-fps 0` 覆盖帧率上限。`VSync` 当前仅通过配置文件设置。

手动构建AET可运行 `cmake --build build --config Debug --target Axiom -j4`。项目不自动追踪远端引擎最新提交；升级引擎时同时更新gitlink与AxiomVersion.json。修改只在本地 `Axiom/` 的PixelMain进行，保留AET；Pixel专用shader、缓存和测试放父仓库。

M2.2 已加入[可协商的关闭请求](Docs/Architecture/CloseRequests.md)：游戏使用 `Axiom::RequestClose()`，应用可覆盖 `OnCloseRequested()` 接受或拒绝。当前默认临时游戏直接接受关闭，不执行保存；显式持久化测试会话仍在该钩子中保存，保存失败或 Deferred 时拒绝退出。

当前暂时停用游戏存档：每次启动均创建新的自然世界，不读取或修改 `Pixel/Saves`，`PIXEL_SAVE_ROOT` 不影响默认游戏。R和育圃入口直接创建新世界，退出不保存，调试面板隐藏 `Save world`。区块流式加载继续使用本次会话独占的系统临时目录；重建世界或正常结束会话后删除，后续启动不会恢复它。已有存档保留。仍可覆盖模拟窗口和预算：

```sh
(cd Pixel && PIXEL_SIMULATION_WINDOW=1024 PIXEL_WORLD_BUDGET_MIB=512 \
  ../build/bin/Debug/Pixel)
```

窗口边长须为64的倍数且不超过2048；预算包含地图、索引、缓存、材料scratch、CPU快照和流式工作余量，风后端与GPU另计。大视口会在预算内缩小发布范围，其余位置显示加载占位。缺失地图阻挡人物与材料，编辑返回NotReady；I/O失败在Environment面板显示并可重试。模拟步等待容量时，R返回Deferred，完成该步后需再次发起重建；默认临时游戏仍可直接退出。显式持久化会话的Save/退出同样受完整提交边界限制，失败保留内存数据并报告原因，中断写入保留上一完整检查点；尚未加入fsync。

存档实现暂时保留，以下仅适用于显式持久化会话及其回归测试：`SessionStore` 只处理文件和值数据，不持有 GameSession 或修改 World。GameSession在完整提交点捕获所有领域，文件全部写入并生成校验清单后才原子发布检查点；已卸载的脏块也包含在新代，旧代不被流式覆盖。v2–v6导入时保留原文件与完整余量，下一次保存升级为checkpoint/1。当前全量复制块、同步保存，成本随存档规模增长。格式、验证与边界见[一致检查点](.awtcontext/GlobalDoc/Checkpoints.md)和[保存退出边界](Docs/Architecture/SessionSaveBoundary.md)。

## 世界与操作

昼夜与季节默认从春季第1天08:00开始，20分钟一天、每季7天。场景光照随晨昏平滑变化，四季具有不同日长和色调；快捷栏顶部显示季节、日期、时刻。F2 → `Day / night and seasons` 可切换时刻/季节、调节日长与速度；暂停和预算等待不走时，单步只推进一次，R重置日历。季节目前影响日长与画面，天气/积雪/生物作息尚未接入。规则、world.meta v7兼容与验证见[昼夜与季节](.awtcontext/GlobalDoc/WorldCalendar.md)。

世界正Y向下，任意正负坐标按需延伸。Natural通过版本化种子按Chunk生成，出生点有干燥安全平台；Superflat严格保留y<512空气、y>=512泥土，旧v2存档也继续使用该平坦策略。玩家模拟窗口默认1024×1024，按64格对齐并在玩家离开中央512×512后重新居中；远处冻结，卸载的修改暂存于本次会话区块缓存。自由相机只改变显示加载范围。

`PIXEL_WORLD_GENERATOR=natural|flat`和`PIXEL_WORLD_SEED=<uint64>`可设置新世界，未指定种子时每次启动随机生成。显式持久化会话才以已有存档为准，其世界元数据v7保存全局暂停与日历状态；v2–v5中的旧环境暂停标记在导入时忽略，旧世界不会因此全局冻结。R使用面板当前选定的生成器和种子。

| 操作 | 行为 |
|---|---|
| A / D，Space | 标准模式移动、跳跃/水中上浮；相机平滑跟随 |
| F3 | 切换自由调试相机，清理持续按键状态 |
| WASD，+ / -，PageDown | 自由模式平移、缩放、切到下一区域 |
| 左键（含Shift＋左键） | 使用当前高亮的手持物：木棍朝鼠标施放自己的构式，方块在面前放置并扣除一份；空手不施法 |
| Tab | 展开或收起5行7列背包 |
| Escape | 编辑调试文本时先取消编辑；否则优先收起背包，再打开游戏菜单；再次按下关闭菜单，点击菜单中的 `Quit game` 才退出 |
| 1–7 / 快捷格左键 | 选择左上角常驻的7个快捷格，退出元素笔刷/设备布置；背包中点击物品绑定到当前快捷格 |
| Shift+2 / Shift+3 / Shift+4，Shift+1 | 选择水 / 蒸汽 / 火调试笔刷，左键投放一团；Shift+1恢复普通工具；F2也可选择 |
| 右键 / Shift＋右键 | 设备内启停 / 回收；其他位置均开采固体 |
| B | 切换设备布置工具；开启时左键放置推力发生器 |
| F1 | 材料 / 合成力场方向视图 |
| F2 | 显示或隐藏Pixel Debug；`Drop wooden stick`在右侧掉落默认构式木棍，走近自动拾取 |
| F4 | 显示或隐藏Formula Editor，预设／源码应用到当前手持木棍 |
| R | 创建新世界，重置视角；默认临时会话丢弃当前进度，保留显示/控制模式 |

Input、Debug和CPU driver的开采、放置与激活共用Session动作入口，返回明确结果；调用者token与epoch在世界替换时失效，每调用者保留最近64条回执用于同步去重。手持行为、物品实例及保存迁移见[手持物品合同](.awtcontext/GlobalDoc/HandheldItems.md)。F2的 `Game actions` 可输入格坐标执行显式调试操作并查看Input/Debug回执。风设置、会话控制及Formula工具栏也经Session端口；Session对外只提供World/Runtime的const引用；内部运行错误直接传播，存档I/O与领域拒绝保留明确结果。公开契约、权限与排队语义见[动作实现说明](Docs/Architecture/GameActions.md)。

启动和R后默认运行；暂停由GameSession统一管理，Simulation不再维护独立暂停开关。F2主面板顶部的 `Pause game` 冻结角色、材料、风场、构式、设备、育圃和世界意志结果交付，世界修改动作返回 `GamePaused`；`Step game 1/60 second` 可显式推进一个完整固定步。暂停不积累游戏时间或缓存跳跃，F3 DebugCamera及显示控制仍可使用；保存、重置和I/O管理仍可操作。引擎顺序保持帧首快照发布 → 输入与游戏推进 → Draw消费发布数据；Skip不推进，Draw不读取实时World。

固定步时钟由Session的SimulationCoordinator管理；成功环境→角色→构式→设备一步才增加Tick。运行时世界动作先返回`Queued`，完成模拟后通过`FindAction`取得携带CommitStamp的最终回执；元数据和宿主管理仍使用同步边界。预算等待期间呈现继续读取最后完整副本，命令不重复执行，流式加载与步首输入保持冻结；细节见[固定步与提交](Docs/Architecture/SimulationCommits.md)及[线程合同](Docs/Architecture/SimulationThread.md)。

`GameSession::Observe` 提供独立的有界领域投影：每个原生调用者保留一个最多64×64格范围，返回冻结材料、空气风速、玩家和构式摘要；观察不触发加载，预算等待只返回最后完整副本。动作回执关联因果事实，256条事实环在淘汰时明确报告缺口；风摘要分别报告驻留覆盖和空气样本分母。DTO与事实值库只依赖标准库。M2.5 已加入独立的 LLMView 权限、冻结分页、120服务帧租约与精确整数 JSON 接口；只读服务不加载世界，待交付数据在撤权后失效。合同、容量统计与性能入口见[有界领域观测与提交事实](.awtcontext/GlobalDoc/ObservationProjection.md)。


`PixelObservationDemo` 是无窗口、无模型的贯穿样例：查询材料/风/玩家/程序摘要，执行一次开采，验证旧页冻结、新视图变化及回执与事实的因果一致性，输出 JSON。使用静态观察权限策略；LLMView 不提供源码或动作权限。接口与资源限制见 [LLMView 实现](.awtcontext/GlobalDoc/LLMView.md)。

```sh
cmake --build build-cpu --target PixelObservationDemo -j4
./build-cpu/bin/Debug/PixelObservationDemo > /private/tmp/pixel-observation-demo.json
# 显式 Release 测量：三个观察者每帧完整读取 64×64 材料及16格风摘要，并序列化 JSON
cmake --build build-region-release --target PixelLLMViewBenchmark -j4
./build-region-release/bin/Release/PixelLLMViewBenchmark /private/tmp/pixel-llm-view.csv
```

`PixelWorldWillDemo` 使用真实 fake-model 工作线程：模型挂起期间模拟继续，释放后提交受限干预并输出真实场源和事实。无需窗口、模型密钥或网络。

```sh
cmake --build build-cpu --target PixelWorldWillDemo -j4
./build-cpu/bin/Debug/PixelWorldWillDemo
ctest --test-dir build-cpu -R '^Pixel\.(WorldWill|WorldWillWorker|Authority|AuthorityView)$' --output-on-failure
```

材料采用60 Hz同步规则：只向步首为空的邻格申请；同目标按源坐标(y,x)裁决；统一提交后反馈风场。长链空位通过多个步传播，规则不依赖四叉树遍历顺序。静止无风区域休眠；编辑、材料成功移动、基础力变化和加载唤醒邻域。方块不保存速度/惯性，空间力不随材料搬迁。

`CellTypeId`为32位强类型枚举，保留Air=0、Dirt=10，固定土/草皮20/21、岩石30、矿石40～44、水50、蒸汽51和火52。静态定义区分角色碰撞、风障碍及开采；固定地层不自动坍塌，Dirt保留原规则。Water下落、斜落与铺展，保持格数；Steam上升扩散，Fire上窜摆动，两者按可复现概率消退，水接触火时灭火。三种元素共用世界格、同步裁决、提交与存档，不增加压力、氧气或热量字段。角色可入水，风域将水视为边界，蒸汽和火可穿行。操作、规则与验证见[元胞材料模拟](.awtcontext/GlobalDoc/CellularMaterials.md)。排空块会保存，加载不补水；存档拒绝未知材料ID，缺失区域继续作为障碍。外观映射只在表现层。

```cpp
world.SetForceDirection(-1, 500, {1, 0}); // 兼容八方向；不改变材料
world.SetBaseForce(-1, 500, {600, 0});    // 空间加速度，格/秒²
world.AddBaseForce(-1, 500, {0, 120});
auto force = world.GetForce(-1, 500);    // 基础力 + 当前材料受风贡献
// 无限地图批量修改必须指定已驻留范围
world.FillRegion({-64, 0, 64, 64}, {pixel::CellTypes::Empty, {0, 1}});
```

方向压缩为两个int8分量（-1/0/1），兼容方向写入映射为 `3600 * direction` 基础力。实际移动速度按 `baseForce / 60 + wind` 换算，各分量超过±30才移动，最多一邻格。直接编辑基础力不重写兼容方向，查询实际场使用GetForce。`World(width,height)`仍可用于有限小地图测试；全图Fill仅对有限地图开放。

游戏连续空间采用[float厘米制](Docs/WorldUnits.md)，一格为50厘米。角色碰撞体宽100厘米、高200厘米，速度800厘米/秒、跳跃1600厘米/秒、重力4500厘米/秒²，按≤12.5厘米分段碰撞；原2×4格尺寸和16格/秒的玩法尺度保持不变。角色不受风，不向泥土施力；地图缺失作为实体阻挡。相机与人物使用整数块坐标加块内float厘米，GPU接收相机附近相对值。相机跟随系数默认8/s，可在面板调整；玩家采用脚底对齐的分层像素人物，不写入地图材料。

人物配置为 `Pixel/Content/CharacterAssets.json`，默认使用 GandalfHardcore 的身体、裤子、上衣、鞋子与头发五层原图复制件。A/D 播放跑步并切换朝向，静止播放待机，Space 上升/下落分别选择跳跃/下落动画；全局暂停冻结帧号。帧网格、脚底锚点、显示比例、图层顺序、动作帧数/速度均可配置，重启加载。来源、许可、配置合同与验证见[美术资源接入](Docs/ArtAssetIntegration.md#人物分层动画2026-10-27)。

全局暂停的原生窗口专项：`(cd Pixel && ../build/tests/PixelRuntimeSmoke --pause-only)`，覆盖默认移动、真实暂停复选框、暂停中的DebugCamera和恢复跳跃。

## 风场和显示

### 逻辑像素与渲染像素

一个逻辑像素就是一个世界格，物理边长50厘米。人物与相机的连续位置、尺寸和运动使用float厘米；地形索引、既有网格求解、构式/查询协议和磁盘格式通过[单位边界](Docs/WorldUnits.md)保留格单位。渲染像素指帧缓冲中的一个像素，默认一个逻辑像素显示为 **20×20** 个渲染像素（SceneScale=1，相当于旧基准4像素下的5倍显示）。相机投影、可见区域、地形、人物、设备、构式特效和鼠标选格共用 `GridView2D::CellSizePixels`，横纵值必须相等，取值范围为1～64，支持小数。

`Pixel/Config/GameConfig.json` 的顶层 `SceneScale` 和 `UIScale` 默认为 `1.0`，重启读取；F2 → **Display scale** 可即时调整，两者独立。面板修改仅本次运行生效，R新建世界保留倍率，`Reset display scales` 将两者恢复为1。

| 参数 | 作用 | 范围 |
|---|---|---|
| `SceneScale` | 场景统一投影倍率；每格渲染像素数 = 编译基准 × SceneScale | 默认基准20时为0.05～3.2；通用范围为1/基准～64/基准 |
| `UIScale` | 快捷栏、背包、文字/图标与开发调试UI的独立显示倍率 | 0.25～4；原生背包超出窗口时等比适配 |

例如 `SceneScale: 2.0` 将默认地形格放大到40×40渲染像素，同屏宽高各减半；`UIScale: 1.5` 将原生UI在原有视口适配结果上放大1.5倍。默认800×600窗口在2×Retina上实际为1600×1200，SceneScale为1和2时，同屏分别为40×30米和20×15米。场景缩放不改变物理尺寸、碰撞或移动速度；背景继续采用屏幕视差布局。小数倍率保留最近邻采样，因此部分格子会占据不同的整数像素宽度。

组合根提供 `PixelGame::SetSceneScale/GetSceneScale`、`SetUIScale/GetUIScale`；相机与原生UI各自校验倍率。非法值（包括零、负数、NaN、无穷及越界值）被拒绝；配置缺省保留1，字符串和布尔值不作为倍率。原生UI的绘制与命中使用同一布局，UIScale随画布发布到帧槽；仅修改倍率不重绘/重传画布纹理。

`PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL` 是编译宏，CMake提供同名缓存参数，例如：

```sh
# 每个逻辑像素显示为8×8；新建构建目录省略参数时默认20×20
cmake -S . -B build -DPIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL=8
cmake --build build --target Pixel -j4
# 恢复默认配置（CMake会保留上次显式设置）
cmake -S . -B build -DPIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL=20
```

1、2、4、8、16、20均可配置，32和64也可用于放大观察。非CMake构建可统一定义该宏，未定义时头文件默认20；所有使用相机/视图的编译单元必须一致。自由相机的`+/-`按两倍调整SceneScale（最终每格限制在1～64渲染像素），只改变显示比例。调整窗口或DPI不改变SceneScale，鼠标事件先从窗口单位换算到事件对应的帧缓冲像素，再按该像素中心选取逻辑格。

美术纹理的texel密度与此比例独立，最近邻采样将纹理映射到逻辑世界；人物的`PixelsPerCell`仍表示素材texel/逻辑格。背景和UI沿用各自的屏幕布局规则。改变比例不会重采样世界数据，也不会更改物理数值或存档格式。

`Pixel.DisplayScale`验证配置、独立倍率、小数投影、远坐标重定位、缩放命中和重建世界保留倍率；`Pixel.PixelScale`验证多倍率投影、方形约束、远坐标与HiDPI拾取；`Pixel.PixelScaleConfig1/2/4/8/16/20`分别编译对应宏并检查默认行为。`PixelTests --rendering-only`检查UI倍率的帧快照及纹理缓存；`PixelRuntimeSmoke --terrain-only`读回包含3.3/5.5在内的七种比例并检查地形采样；`--inventory-ui-only`检查多UIScale的GPU合成。

普通视图已接入四层山林背景，使用 `Pixel/Content/ArtAssets.json` 配置内容目录内的相对PNG路径、镜像重复周期和显示高度。原件留在 `ArtRaw/`；运行时只读取 `Pixel/Content/Art/NaturalParallax/` 的复制件。清单和解码图片在应用生命周期内缓存，背景GPU纹理只在策略初始化后首次绘制上传一次，切换frame slot或调整窗口不重复读取/上传。F1诊断视图关闭背景；背景不参与材料、碰撞或存档。接口、内存成本及替换方法见 [美术资源接入](Docs/ArtAssetIntegration.md)。

Environment可在Empty、Coarse NS、Pixel reference、Procedural、Hybrid、Adaptive NS、LBM D2Q9七策略间切换，默认Empty。Reference、Adaptive和LBM计算成本较高，可用全局暂停和单步进行诊断；切换风后端不再自动改变暂停状态。切换重建当前风后端，保留地图、基础力及物理时钟；不保存每策略的独立运行历史。

`Wind boundary Y`是世界坐标：Superflat默认424，Natural默认360，后者按地表最高可能位置上方88格设置；它仍是水平风域，不贴合每个山坡。旧档保留已保存的边界。风域随玩家移动，迁移重叠速度/分布，新进入区域由背景风初始化；离开窗口的动态扰动丢弃。正常游戏使用流入/流出开放边界，周期模式仅在显式数值测试中开启。

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

F1在格中心把合成速度单位方向映射为 `RGB=(.5+.5*dx, .5+.5*dy, .5)`；零向量灰色，颜色不表示强度，跳过人物、构式球与风效。普通视图顺序为世界 → 风效 → 设备 → 构式球 → 人物 → 后处理 → UI。Wind appearance可选Off、Short trails、Air refraction，调可见度/折光/视觉流速不改变物理。

世界材料、基础力、风后端和场源由 `WorldBuffers` 统一双缓冲。模拟独占可写槽，呈现和观测共享完整提交的只读版本；读者持有期间不覆盖旧槽。材料载荷在写入时分离并复用旧槽的脏区域，观测只在查询时采样。生命周期、预算及GPU接入边界见[固定步与提交](Docs/Architecture/SimulationCommits.md)。

渲染打包只复制可见64格区域的变化版本，额外覆盖32格风回溯边界；每个frame slot独占动态尺寸纹理与版本。稳定材料区域不重复映射或上传；F1/外观切换、进入新区、加载/卸载及GPU重建分别失效。风纹理每分量signed16、单位1/32格/秒，固体特殊标记；shader和缓存均属于Pixel。本地引擎只增加通用纹理区域上传接口，保留整图入口。

## 验证和性能

```sh
# 无窗口/GPU：Pixel、Formula和Axiom CPU/mock回归
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure

# 生成、液体及存储/流式契约使用另一块尺寸
cmake -S . -B build-chunk64 -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-chunk64 --target PixelGenerationTests PixelLiquidTests PixelGenerationSessionTests PixelTerrainAssetTests PixelTests PixelChunkTests PixelPersistenceTests PixelWindDomainTests -j4
ctest --test-dir build-chunk64 -R '^Pixel\.(Generation|Liquids|GenerationSession|TerrainAssets|Cpu|Chunks|Persistence|WindDomains)$' --output-on-failure

# 需要窗口服务与Vulkan驱动；不注册到默认CTest
cmake -S . -B build -DPIXEL_BUILD_SMOKE=ON -DAXIOM_BUILD_LIFECYCLE_SMOKE=ON
cmake --build build --target Pixel Axiom PixelRuntimeSmoke PixelCloseSmoke AxiomLifecycleSmoke AxiomCloseRequestSmoke AxiomApplicationCloseTest -j4
ctest --test-dir build -R '^Axiom\.UnitTest\.ApplicationClose$' --output-on-failure
(cd Pixel && ../build/tests/PixelRuntimeSmoke)
# 实际并行游戏推进、上一帧GPU像素、暂停、resize、重置与关闭
(cd Pixel && ../build/tests/PixelRuntimeSmoke --parallel-only)
(cd Pixel && ../build/tests/PixelCloseSmoke)
# 内建ESC菜单；验证有/无调试UI、活跃文本编辑捕获、面板恢复、按钮与输入拦截
(cd Pixel && ../build/tests/PixelCloseSmoke --menu-only)
# 单独检查实际地形PNG、表面条带和远坐标UV
(cd Pixel && ../build/tests/PixelRuntimeSmoke --terrain-only)
# 所有配置人物帧的PNG/GPU读回与正式游戏移动、跳跃、暂停
(cd Pixel && ../build/tests/PixelRuntimeSmoke --character-only)
# 原生背包UI、Tab/Escape、GPU逐像素及后处理隔离；可选导出开合画面
(cd Pixel && PIXEL_UI_CAPTURE_DIR=/private/tmp/pixel-ui ../build/tests/PixelRuntimeSmoke --inventory-ui-only)
# 连续144步的水、蒸汽、火及动态区域上传；可选导出每24步截图
(cd Pixel && PIXEL_ELEMENT_CAPTURE_DIR=/private/tmp/pixel-elements ../build/tests/PixelRuntimeSmoke --elements-only)
# 统一存储、风可视化与F1力场连续行打包专项
(cd Pixel && ../build/tests/PixelRuntimeSmoke --fields-only)
# 持续场安装/清理的F1实际GPU读回，可导出对比截图
(cd Pixel && PIXEL_FIELD_CAPTURE_DIR=/private/tmp/pixel-fields ../build/tests/PixelRuntimeSmoke --formula-only)
# 只验证本轮背景PNG、sRGB/alpha、视差与F1（可选输出PPM截图）
(cd Pixel && PIXEL_ART_CAPTURE=/private/tmp/pixel-art.ppm ../build/tests/PixelRuntimeSmoke --art-only)
(cd Axiom && ../build/tests/AxiomLifecycleSmoke --skip-minimize)
(cd Axiom && ../build/tests/AxiomCloseRequestSmoke)

# AET仍可单独构建
cmake -S Axiom -B build-engine -DAXIOM_BUILD_FEATURE_SHOW=OFF
cmake --build build-engine --target Axiom -j4

# 显式Release基准，不加入默认构建/CTest
cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelRegionBenchmark -j4
./build-region-release/bin/Release/PixelRegionBenchmark /private/tmp/pixel-regions.csv 40
cmake --build build-region-release --target PixelFieldSourceBenchmark -j4
./build-region-release/bin/Release/PixelFieldSourceBenchmark /private/tmp/pixel-fields.csv
cmake --build build-region-release --target PixelWorldBufferBenchmark -j4
./build-region-release/bin/Release/PixelWorldBufferBenchmark > /private/tmp/pixel-world-buffers.csv
```

Environment显示驻留/排队块、预算与I/O错误；Performance显示阶段时间、P95、材料访问格数、活动区域、上传量及内存，支持CSV。内存为所有权容量与工作预留，不等于进程RSS；风求解器单列，GPU不计。自动捕获和扩展方式见 [采样模块](Pixel/Source/Dev/Debug/Performance/README.md)。下列结果及 [世界改造验证](Docs/WorldRepresentationValidation.md) 均为对应日期的历史证据；最新功能证据按系统查阅 AWT 任务记录，本次文档整理没有重跑游戏测试。早期测量保留于Docs/PerformanceProfiling.md、MemoryProfiling.md和MaterialUpdatePerformance.md。

2026-10-26世界双缓冲：CPU 97/97、64格专项12/12、Apple M5元素GPU专项及独立review通过。动态风配合1–3个读视图的CPU提交与场图打包总耗时下降19%–26%；无读者及运动材料的时间/内存取舍见[双缓冲验证与三轮性能对照](Docs/WorldBuffersValidation.md)。

2026-10-29统一空间场存储：CPU 117/117、64格专项11/11、ASan/UBSan 6/6通过；Apple M5最终风/力场及两轮生命周期专项通过。最后版本完整GPU回归两次停在批量鼠标输入断言，原因未确认；三轮Release数据和各路径的性能取舍见[存储验证报告](Docs/SpatialFieldStorageValidation.md)。

Linux自动鼠标检查需要X11/XWayland的DISPLAY；GLFW Wayland不能设置光标，可设置 `XDG_SESSION_TYPE=x11` 运行smoke。2026-10-19历史RTX 3070/X11完整Pixel smoke曾在后续生命周期阶段报 `no drawable 2D frame`；Axiom `--skip-minimize`两轮通过，该历史结果不覆盖最小化恢复。本机Apple M5完整Pixel smoke通过（含两轮最小化恢复），AET --skip-minimize两轮通过；validation layer不可用，完整范围见验证报告。

2026-10-25模块整理验证：默认CPU 97项中96项首轮通过，育圃预算夹具在原始HEAD也会解引用空快照；改为先发布基线、等待流式完成再压缩预算后，育圃专项通过。64格区块Release专项14项、独立Formula 7项和ApplicationClose通过；Pixel、AET及GPU smoke目标编译通过。RTX 3070/X11的地形、背景、风、基础交互、力场、构式、设备/育圃/祈祷UI均通过；完整Pixel smoke随后在生命周期阶段报`no drawable 2D frame`，因此不声明完整生命周期通过。PixelCloseSmoke及AET `--skip-minimize`两轮通过。

## 构式（Formula）与编辑器

当前默认采用 `formula 2;`：带类型参数的函数与不可变 action 描述，可顺序、延迟、重复或按命中/唤醒事件组合。`process 1;` 源码及已有运行存档保持原语义；未知格式明确拒绝，不删除存档或重新执行。旧 cast 和树形编辑器已删除。完整合同见[构式函数组合与世界绑定](Docs/Architecture/FormulaComposition.md)。

玩家通过手持木棍装载自己的程序；F2点击 `Drop wooden stick` 后走近拾取，快捷栏会自动选中。左键从人物朝鼠标一侧的身体外缘（弹体半径外再留12.5 cm）激活，方向为鼠标位置减去实际发射起点。右键开采，Shift＋左键同样使用手持物；持有方块时在人物面前放置。木棍默认程序为黄色球：伤害元数据10、速度60格/秒、寿命120活动步、半径0.35格。它显式等待命中或到期，程序结束清理归属效果。球使用圆形扫掠碰撞；作用区域不活跃或必要块缺失时冻结，返回后按剩余活动时间继续。

新世界默认在玩家出生点右侧12格放置一个木色木头人（Natural、Flat和育圃均有）。拾取并持有木棍后，对准它左键即可测试构式弹体；弹体会在2×4格身体范围内命中，`on_hit` 后续动作在实际命中位置执行，地形遮挡仍有效。木头人固定位置、不攻击、不可被击杀；挖掉脚下地形、进入水或火、连续高伤害命中都不会让它移动或消失。它是独立Actor，不写入材料地图，也不装配怪物AI或生命组件；当前没有伤害飘字。F1隐藏外观，R创建新世界时重建一个；显式持久会话保存其身份和位置，旧存档不额外补生。

怪物已通过 [Monsters.json](Pixel/Content/Monsters.json) 接入八种素材与行为。新世界自动生成，包含巡逻/追击、近战前摇、飞行、宝箱怪唤醒、构式伤害、受击和死亡动画；配置、保存和范围见 [怪物接入说明](Docs/Monsters.md)。CPU 专项为 `Pixel.Monsters`，GPU 专项为 `(cd Pixel && ../build/tests/PixelRuntimeSmoke --monsters-only)`。

木头人验证：`ctest --test-dir build-cpu -R '^Pixel.TrainingDummy$' --output-on-failure`；原生画面专项为 `(cd Pixel && ../build/tests/PixelRuntimeSmoke --dummy-only)`，可通过 `PIXEL_DUMMY_CAPTURE=/private/tmp/pixel-dummy.ppm` 导出画面。

F2控制Pixel Debug，F4独立控制 `Formula Editor`。持有木棍时可编辑和应用其程序；未持有木棍时禁用应用。可直接修改 `Process text` 或载入三脉冲草稿，依次点击 `Compile process`、`Apply process`。编辑源码会撤销编译结果，错误显示行列；Apply仅影响后续实例，运行中的实例保留原版本。`Load applied process`恢复当前木棍源码，`Clear running`清理运行实例及归属效果。

F2菜单点击 **Formula presets**，或直接按F4，在顶部选择五套函数演示：`Mining volley`（三连开采弹）、`Delayed excavation`（命中后两阶段延时扩挖）、`Alternating force`（交替推力场）、`Beacon pulses`（定点信标脉冲）、`Wake launcher`（事件唤醒发射器）。`Load preset source`将源码放入编辑区，`Equip preset`将程序装载到当前手持木棍，不自动运行；在世界内左键才施放。编辑器的`Compile process`、`Apply process`同样只编译/装载，已运行实例保留旧程序。`Stop player formulas`取消主角当前运行的构式及归属效果，保留装载并允许再次左键施放；`Clear running`清理全部构式。唤醒预设左键启动等待后按`Wake player formulas`触发；力场用F1观察。开采实际改变地形；暂停或世界未就绪时装载按钮禁用。新游戏通过F2掉落默认构式木棍；显式持久会话用PixelGameplay 6保存每根木棍，旧人物程序迁移成木棍。物品接口、掉落拾取和存档合同见[手持物品](.awtcontext/GlobalDoc/HandheldItems.md)。演示配置与实现入口见[构式合同的预设说明](Docs/Architecture/FormulaComposition.md#debug函数预设)。

`Process: three carriers`可从玩家附近启动NaturalSource和FixedFixture调试承载者，使用同一程序和宿主规则；额度分别为6、4，玩家为64。真实Device另有物品和燃料规则。`Start actor attachment` 将同一构式附着玩家，后续请求跟随移动，来源与附着身份独立。Actor销毁或离开授权范围取消，离开活动区冻结。实例观察显示绑定、位置与方向，最近回执显示源码位置和拒绝码。这些承载者共享 `ProcessHost`，World不依赖构式或渲染。

游戏默认运行。GameSession全局暂停冻结角色、构式、环境、设备和育圃，DebugCamera仍可移动；单步推进完整的1/60秒。预算等待不推进实例，并拒绝新激活和编辑。显式持久会话的检查点保存已应用程序、运行实例、事件、弹体及持续场源；默认球存活时也能保存并正常关闭。普通游戏仍不保存进度。未应用编辑草稿不保存，动态风扰动仍按配置和相位重建。

实现入口：`Runtime/System/Formula`提供独立编译器、运行时和草稿；`Runtime/Gameplay/FormulaBridge`负责承载者、领域权限、效果提交及快照；`Dev/Debug/UI/FormulaDebugUI`提供源码编辑界面。语言和领域合同见[模块README](Pixel/Source/Runtime/System/Formula/README.md)、[过程运行时](.awtcontext/GlobalDoc/FormulaProcessRuntime.md)和[FormulaBridge](.awtcontext/GlobalDoc/FormulaBridge.md)。

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B build-formula-system -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula-system -j4
ctest --test-dir build-formula-system --output-on-failure
./build-formula-system/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Composition/mining.formula --steps 12 --hit-at 3

cmake --build build --target PixelRuntimeSmoke PixelCloseSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --formula-only)
(cd Pixel && ../build/tests/PixelCloseSmoke)
```

2026-10-25移除旧构式后的验证：CPU全量92/92、独立语言5/5通过；Pixel、PixelRuntimeSmoke和PixelCloseSmoke编译通过。RTX 3070/X11的构式GPU/源码编辑器、育圃UI及带活动默认弹丸的保存关闭恢复专项通过；本轮未重跑完整GPU生命周期或AET。

formula/2 的 `field(cells(r), acceleration(ax), acceleration(ay), ticks(life))` 返回有限寿命 action，加速度按实例角度旋转。描述等待该寿命，结束、故障、撤销或设备拆除清理归属场源。示例见[push.formula](Pixel/Source/Runtime/System/Formula/Examples/Composition/push.formula)。旧 process/1 仍保留显式句柄更新/停止接口。

2026-10-26 升级验证：CPU全量基线94/94通过，后续存档校验与内存统计小改动由最新11/11专项及独立ASan+UBSan 6/6复验；Pixel、AET及smoke目标编译成功。真实窗口构式GPU、formula/2编辑与Actor附着、活动默认弹体关闭保存恢复均通过。Release函数封装与展开轨迹相同、首片段均17 work/22指令。本轮未重跑完整GPU生命周期或AET运行，Vulkan validation layer不可用；实际验证范围见[验证报告](Docs/Architecture/FormulaCompositionVerification.md)。

AWT入口为 `../AIWorkTask/awt task list --project .awtcontext`，工程文档见[索引](.awtcontext/GlobalDoc/DocIndex.md)。

### 性能采样的引擎归属

通用采样核心、会话、CSV 与可选 Performance 面板已迁入 [Axiom Diagnostics](Axiom/Source/Core/Diagnostics/README.md)，Pixel 只保留领域指标、世界内存提供者和 PIXEL_PROFILE_* 场景配置。Axiom 统一管理 Ready 帧边界并细分限帧、交换链等待、输入、提交和 Present。新增 `PIXEL_PROFILE_MEMORY_INTERVAL` 可降低内存查询频率，查询成本单独报告；指标名称兼容，CSV 按表头读取。当前口径及历史差异见 [性能模块说明](Pixel/Source/Dev/Debug/Performance/README.md)。

本轮迁移范围和静态/动态库、CPU、原生窗口验证结果见 [迁移验收记录](Docs/DiagnosticsMigration.md)。

Performance 现仅在启动参数包含 `--PerfTest`（亦接受 `PerfTest`、`-PerfTest`）时启用。已有阶段宏会额外写入独立 Chrome Trace JSON，默认 `Pixel/pixel-performance.trace.json`，可用 `PIXEL_PROFILE_TRACE` 改路径；构造时立即写入纳秒开始时间，析构或提前结束时写入结束时间与持续时间，以 `B`/`E` 事件配对，每条记录刷新文件，不经过 Log。自定义事件与提前结束宏见 [性能模块说明](Pixel/Source/Dev/Debug/Performance/README.md#新增插桩)。未带参数时环境变量和面板都不会启动采样。

trace 同时自动输出每帧的 `frame.begin` / `frame.end`，携带连续帧序号和纳秒时间；结束标记包含区间耗时，未完成帧标记为已取消。

Axiom 每个运行实例保存不可变的游戏名和 GUID（`GetGameName()` / `GetRunGuid()`）；Pixel 登记为 `Pixel`，引擎样例登记为 `AET`。日志同时写控制台和 `Logs/<游戏名>_<YYYYMMDD_HHMMSS_毫秒>_<GUID>.log`，时间为本地启动时间、目录相对工作目录；`GetLogFilePath()` 返回绝对路径。日志头和 trace 头包含相同的 `run_guid` / `game_name`，可据此关联一次运行的数据。运行日志不依赖 `PerfTest` 开关。

日志中的 `steady_ns` 与 trace 头的 `clock_origin_steady_ns` 使用同一个单调时钟；两者做整数相减即为日志在 trace 时间轴上的纳秒位置。标准 trace `ts` 则以微秒表示。换算说明见 [性能模块说明](Pixel/Source/Dev/Debug/Performance/README.md)。
