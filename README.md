# AxiomPixel

游戏总纲：**在一个会回应你的世界里，理解规律、创造构式，把陌生之地逐渐变成自己愿意生活的地方。** 后续玩法、流程与系统设计的高层指导见[游戏设计总纲](Docs/GameDesign/GameDesignVision.md)。

游戏设计、构式说明与策划参考资料集中在 [Docs/GameDesign](Docs/GameDesign/README.md)。

后续技术演进见[面向游戏愿景的架构规划](Docs/Architecture/GameplayArchitecturePlan.md)：按独立子系统考虑构式语言、LLMView 与游戏领域，给出引擎边界及分阶段迁移方案；它是规划提案，以下仍描述当前实现。

首个执行切片的观测、开采/激活、动作回执、版本和提交语义见[贯穿场景与 GameAPI 契约](Docs/Architecture/FirstGameplaySliceContract.md)；统一动作入口、固定步提交、事实和LLMView已实现；当前用法见[游戏动作入口](Docs/Architecture/GameActions.md)。

基于 Axiom 的早期像素游戏，从引擎 `2DRender` 的 `5c8af21` 拆出。`Axiom/` 是跟踪 `PixelMain` 的 Git 子模块，父仓库 gitlink 与 [AxiomVersion.json](AxiomVersion.json) 锁定准确提交；AET 和通用引擎代码留在子模块。相邻 `../Axiom` 仅作只读参考。

Core 的[Allocator与容器后端接入](.awtcontext/GlobalDoc/CoreAllocatorAndContainers.md)区分内存资源和存储算法：默认Array/Map使用标准库后端与Axiom池分配器，自定义后端按模板显式选择。Pixel区块保留独立64 KiB对齐资源，材料scratch扩容及时释放旧池；池缓存寿命和引用失效规则见该合同。

世界现按需加载1024×1024物理块，由稀疏四叉树组织64×64区域，采用同步材料更新、文件存档和跟随玩家的开放风域。现行接口、存储布局和失败处理见 [世界表示约定](Docs/WorldRepresentation.md)，本轮实测及验证限制见 [验证与性能报告](Docs/WorldRepresentationValidation.md)。早期文档中的整图连续数组、移动链和四张固定世界纹理已被替代。

新建游戏默认采用Natural世界生成策略：起伏地形、固定土岩、洞穴、五种矿石、有限湖泊/地下积水及干燥地表草皮。保留Superflat调试策略；F2的World generation选择下一世界的生成器和种子，按R创建新世界（当前不保存进度）。入口、版本兼容和资源配置见 [世界生成与自然材质](.awtcontext/GlobalDoc/WorldGenerationImplementation.md)。

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
| `Runtime/Gameplay/Actors, Camera/` | Actors管理角色身份与生命周期，Body负责身体运动与碰撞，PlayerInput表达运动意图；相机跟随/缩放/选格 |
| `Runtime/Gameplay/UI/` | 原生背包UI的状态、库存投影、布局、命中测试与像素画布；不依赖Render/Input/ImGui |
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

`Render/GamePresentation`在帧首构造`WorldGridFrame`；`PixelRender::Draw`仅编排`Render/Passes`中的上传、世界、风、场景物体、角色和后处理Pass。具体Pass封装纹理状态切换、复制、管线绑定及绘制命令；场景物体Pass合成设备、育圃和构式弹体，后处理Pass完成后在呈现目标内调用原生背包UI，再调用开发调试UI叠加；UI不受场景调色影响。共享GPU资源仍由PixelRender管理生命周期，区域上传版本与首次美术上传标记仅在整帧录制成功后提交。

`System/IO/Input`拥有键鼠事件解释、按键状态和控制模式，通过GameSession动作端口及GameCamera驱动游戏；`Gameplay/Actors/PlayerInput.h`保留角色需要的横向移动与跳跃意图。输入适配可以依赖Gameplay，Gameplay核心、Simulation及其余System服务不反向依赖Input；Input不包含Render实现或ImGui，退出、调试面板及画面尺寸由Application注入回调。

System中的通用服务不包含角色、设备、世界或权限类型；`System/IO/Input`单独承担依赖Gameplay的输入适配，构建为独立的`PixelInput`，文件服务`PixelIO`不链接输入模块。`System/LLM/ModelService`仅传递自持有的文本、请求ID和响应上限；`Gameplay/LLM/Services/ServiceLLM`将已授权观测编码成请求，再将回包校验为游戏提案。LLMView、DecisionAPI、PseudoLLM及WorldWill都属于Gameplay。组合根可将`ServiceLLM`作为现有LLM实现注入；默认仍使用离线PseudoLLM，尚未接入真实网络提供者。

文件原子发布、摘要校验与可复用文本格式位于System；世界区块格式、checkpoint世代策略及领域DTO仍由对应的Simulation/Gameplay模块定义。构式语言不依赖游戏，游戏参数校验和世界适配留在Gameplay；纯树编辑器属于Dev/Debug。

Dev/Debug保留当前可运行的开发功能。各领域允许调用无UI的Performance插桩；GameSession在显式选择育圃场景后调用Garden规则，仍由Session持有场景状态并按原存档格式保存。Garden只接收所需世界/角色/构式对象，不访问GameSession私有实现。它依赖底层`PixelFormulaBridge`，不会反向链接`PixelGameplay`；DebugUI由Application装配。这些诊断/场景接点不改变Simulation的单向边界。

核心构建依赖为`PixelGameplay → PixelActors → PixelWorld`、`PixelGameplay → PixelFormulaBridge → PixelFormulaProcess`和`PixelPresentation → PixelGameplay/PixelCamera`；`System/IO/Input`构建的`PixelInput`仅链接Gameplay与Camera。`Pixel.System`单独链接System工具验证可复用边界，`Pixel.ModelService`验证协议适配/取消/容量/错误，`Pixel.ModuleBoundaries`检查System通用服务、Simulation、Gameplay与IO/Input的引用方向。旧模块设计图中的目录与世界所有权以此处及[世界表示约定](Docs/WorldRepresentation.md)为准。

角色身份与生命周期见 [Actors 合同](.awtcontext/GlobalDoc/Actors.md)：玩家已移出 World，Resident 验证第二对象的物理与生命周期；当前呈现仍仅玩家；M4.6检查点已保存所有角色的身份和完整运动状态。

M4.2 已加入[物品与库存](.awtcontext/GlobalDoc/Items.md)：玩家背包容量4096，初始为空；开采土、石或矿石得到对应物品，玩家构式也遵循容量规则。满包保留材料，左键放土消耗一份土料，缺料不改变地形。原生库存支持版本化整批转移和预留/释放/消费；首个设备成本为4石头和2铜矿，M4.3已接入设备放置。M4.6已保存库存及身份高水位，R创建的新世界仍从空背包开始；M4.7已接入F2库存界面和按授权公开的只读查询。

原生游戏UI由 `Gameplay/UI` 独立维护：左上常驻7个快捷格，Tab在下方展开5行7列背包。数字键1–7或点击快捷格选择，点击背包物品将其绑定到当前快捷格；Escape先关闭背包。图标、总量及可用数量读取与场景相同的已提交库存，预留资源用金色小标记表示。当前仍采用已有4096总量容器，35格是展示布局，快捷格是引用；本轮未加入格子堆叠上限、拖拽、装备或物品使用动作，左键放土等世界规则保持现有合同。详细边界与验证见[原生背包UI](.awtcontext/GlobalDoc/NativeInventoryUI.md)。

M4.3 的设备在 M4.4 接入了[持续推力源](.awtcontext/GlobalDoc/FieldSources.md)：B切换布置工具，左键花费4石头+2铜矿放置2×2推力发生器；设备内右键启停，Shift+右键回收。F2 Devices提供补煤、停机原因、程序等待和场源归属；Inventory查看背包与煤槽，Programs and fields查看运行状态，均读取同一已发布帧。每煤维持30个成功tick的局部向右推力，半径16格，无需开启风后端；F1查看场方向，Dirt受合成力推动。取消暂停或单步才工作，关闭/拆除立即清理来源。回收退建造物料与未用煤；背包满则拒绝整个回收。设备是非实体设施，动态材料进入设备占用时取消工作。详见[设备合同](.awtcontext/GlobalDoc/Devices.md)。设备、燃料和来源通过[M4.6一致检查点](.awtcontext/GlobalDoc/Checkpoints.md)保存；[M4.5区域冻结恢复](.awtcontext/GlobalDoc/RegionActivity.md)已实现：离开后保留剩余周期，返回接续且不重扣煤，重启后继续已支付周期。M4.7的设备、库存、程序与场源查询遵守独立库存授权和冻结分页，见[观测与表现](.awtcontext/GlobalDoc/GameplayObservation.md)。

M4.8 提供可保存的「力场育圃」小场景：F2展开 `Force garden`，点击 `Create force garden` 创建独立场景。当前游戏不保存进度，重新启动会创建新世界；原有持久化能力仅保留在显式持久化会话与测试中。观察左侧晶体和野生植株，应用同一个构式并走近右侧育圃试验；开采面板标出的真实石头、铜和煤，建造、供煤并启用推力设备，取消暂停后观察开花。成长按实际水平加速度与地形条件累计，离开活动区冻结；当前默认游戏结束后清空。创建需要至少128格模拟窗口；R仍创建普通世界。操作、规则与验证见[力场育圃贯穿场景](.awtcontext/GlobalDoc/ForceGarden.md)。

M4 的[阶段验收](.awtcontext/GlobalDoc/M4Acceptance.md)已完成：核对八项子任务的整合边界，复验默认CPU、64格区块专项及真实GPU育圃/UI/关闭保存，并完成独立review。记录包含一个跨区块坐标测试修复、保存中断的验证方式与当前限制。

M5.1 已建立[发现知识与重要历史](.awtcontext/GlobalDoc/KnowledgeAndHistory.md)：显式ObserveForce动作记录32格内的真实加速度，只授予“已观测”；重要开采、设备操作和首次发现保留原始来源。LLMView增加独立授权的Knowledge/History冻结查询，模型摘要另存并引用已交付历史。M5.4已把知识和重要历史接入磁盘检查点；尚无独立知识UI或网络模型。

M5.2 已建立[区域控制与干预撤销](.awtcontext/GlobalDoc/Authority.md)：宿主分配世界意志身份，设备保护模块阻断区域观察与干预，撤权清理实例、后续事件和实际场源，同时使旧查询与待交付结果失效。M5.4已保存保护模块、活跃/冻结干预、剩余租约及非默认能力，提供育圃保护按钮。

M5.3 已建立[异步 WorldWill 与离线模型闭环](.awtcontext/GlobalDoc/WorldWill.md)：授权观测复制给有界 worker，主线程在安全边界核验设备、程序和干预资源，保留真实回执与事实。应用已接服务帧和关闭生命周期，不自动发起祈祷，也没有网络模型。

M5.4 的[持久援助与自主权](.awtcontext/GlobalDoc/PersistentInterventions.md)提供试玩入口：F2创建力场育圃，开采并建造家园设备后，点击 `Pray for home aid`，获得援助提案后恢复模拟，即可经正式动作入口产生临时推力（当前随机兜底也可能选择NoAction）。安装保护并启用设备会撤销援助；已积累成长、援助次数、自主选择及带来源的祈祷档案可保存。未完成推理加载后标为Interrupted，不自动重放。模型说明与实际动作、实例和历史证据分别展示。

M5.5 [可替换模型接口与祈祷交互](.awtcontext/GlobalDoc/ModelServiceIntegration.md)增加有界玩家原文、建议未执行的Ready状态、显式取消与跨世界核验，以及独立的受限JSON提案协议。程序可由组合根注入LLM实现。按本轮用户要求暂不接实际模型，网络适配、服务配置和真实连通验收留待后续。

用户追加的 [LLM与PseudoLLM](.awtcontext/GlobalDoc/LLMAndPseudoLLM.md) 将模型抽象独立到 `PixelLLM`。未注入模型时使用 `PseudoLLM`，随机选择“援助”或“等待”的函数组合，通过共用 `DecisionAPI` 提出建议；因此祈祷可能返回 `NoAction`，可以再次请求。实际援助仍经WorldWill与GameAPI核验，测试专用FakeModel保留受控延迟/重复回包能力。

M5 的[阶段验收](.awtcontext/GlobalDoc/M5Acceptance.md)汇总六项能力的异步、授权、祈祷追踪与持久化合同，以及两配置CPU专项、真实worker演示和GPU/UI祈祷场景的验证与独立review。真实模型服务仍未接入；当前模型输入也尚未自动加入知识历史或长期关系。

M6.1 的 [BehaviorDSL必要性实验](Docs/Architecture/BehaviorDecisionExperiment.md) 用同一育圃场景比较原生函数组合与独立有限决策图，验证真实援助、撤销、版本和容量边界，并保存三轮Release测量。当前暂缓持久BehaviorDSL，继续采用DecisionAPI提案与Formula执行；实验代码仅在测试目标构建。

M6 的[阶段验收](.awtcontext/GlobalDoc/M6Acceptance.md)记录当前暂缓决定的适用范围、16项CPU专项、Release复测与实验构建隔离。跨请求等待、重新观测和持久行为安装仍未实现，未来先用真实玩法证明需要，再定义相关合同。

六阶段组合合同与当前验证见[模块化架构总验收](.awtcontext/GlobalDoc/GameplayArchitectureAcceptance.md)：CPU全量94项、Formula独立7项、64格区块专项32项及完整Pixel GPU/关闭与AET验证。验收覆盖观测、构式、设备家园和世界意志切片；游戏总纲的远期玩法仍按需推进。

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

M2.2 已加入[可协商的关闭请求](Docs/Architecture/CloseRequests.md)：游戏使用 `Axiom::RequestClose()`，应用可覆盖 `OnCloseRequested()` 接受或拒绝。当前默认临时游戏直接接受关闭，不执行保存；显式持久化测试会话仍在该钩子中保存，保存失败或 Deferred 时拒绝退出。

当前暂时停用游戏存档：每次启动均创建新的自然世界，不读取或修改 `Pixel/Saves`，`PIXEL_SAVE_ROOT` 不影响默认游戏。R和育圃入口直接创建新世界，退出不保存，调试面板隐藏 `Save world`。区块流式加载继续使用本次会话独占的系统临时目录；重建世界或正常结束会话后删除，后续启动不会恢复它。已有存档保留。仍可覆盖模拟窗口和预算：

```sh
(cd Pixel && PIXEL_SIMULATION_WINDOW=1024 PIXEL_WORLD_BUDGET_MIB=512 \
  ../build/bin/Debug/Pixel)
```

窗口边长须为64的倍数且不超过2048；预算包含地图、索引、缓存、材料scratch、CPU快照和流式工作余量，风后端与GPU另计。大视口会在预算内缩小发布范围，其余位置显示加载占位。缺失地图阻挡人物与材料，编辑返回NotReady；I/O失败在Environment面板显示并可重试。模拟步等待容量时，R返回Deferred，完成该步后需再次发起重建；默认临时游戏仍可直接退出。显式持久化会话的Save/退出同样受完整提交边界限制，失败保留内存数据并报告原因，中断写入保留上一完整检查点；尚未加入fsync。

存档实现暂时保留，以下仅适用于显式持久化会话及其回归测试：`SessionStore` 只处理文件和值数据，不持有 GameSession 或修改 World。GameSession在完整提交点捕获所有领域，文件全部写入并生成校验清单后才原子发布检查点；已卸载的脏块也包含在新代，旧代不被流式覆盖。v2–v5导入时保留原文件与完整余量，下一次保存升级为checkpoint/1。当前全量复制块、同步保存，成本随存档规模增长。格式、验证与边界见[一致检查点](.awtcontext/GlobalDoc/Checkpoints.md)和[保存退出边界](Docs/Architecture/SessionSaveBoundary.md)。

## 世界与操作

世界正Y向下，任意正负坐标按需延伸。Natural通过版本化种子按Chunk生成，出生点有干燥安全平台；Superflat严格保留y<512空气、y>=512泥土，旧v2存档也继续使用该平坦策略。玩家模拟窗口默认1024×1024，按64格对齐并在玩家离开中央512×512后重新居中；远处冻结，卸载的修改暂存于本次会话区块缓存。自由相机只改变显示加载范围。

`PIXEL_WORLD_GENERATOR=natural|flat`和`PIXEL_WORLD_SEED=<uint64>`可设置新世界，未指定种子时每次启动随机生成。显式持久化会话才以已有存档为准，其世界元数据v6保存全局暂停；v2–v5中的旧环境暂停标记在导入时忽略，旧世界不会因此全局冻结。R使用面板当前选定的生成器和种子。

| 操作 | 行为 |
|---|---|
| A / D，Space | 标准模式移动、跳跃/水中上浮；相机平滑跟随 |
| F3 | 切换自由调试相机，清理持续按键状态 |
| WASD，+ / -，PageDown | 自由模式平移、缩放、切到下一区域 |
| 左键 | 消耗一份土料放置Dirt；先开采取得土料，不覆盖水或人物 |
| Tab / Escape | 展开或收起5行7列背包；Escape优先收起背包，关闭后再次按下退出 |
| 1–7 / 快捷格左键 | 选择左上角常驻的7个快捷格，退出元素笔刷/设备布置；背包中点击物品绑定到当前快捷格 |
| Shift+2 / Shift+3 / Shift+4，Shift+1 | 选择水 / 蒸汽 / 火调试笔刷，左键投放一团；Shift+1恢复普通工具；F2也可选择 |
| 右键 / Shift＋右键 | 设备内启停 / 回收；其他位置激活当前构式 / 开采固体 |
| B | 切换设备布置工具；开启时左键放置推力发生器 |
| F1 | 材料 / 合成力场方向视图 |
| F2 | 显示或隐藏Pixel Debug |
| F4 | 独立显示或隐藏Formula Editor |
| R | 保存当前世界，创建新世界，重置视角；保留显示/控制模式 |

Input、Debug和CPU driver的开采、放置与激活共用Session动作入口，返回明确结果；调用者token与epoch在世界替换时失效，每调用者保留最近64条回执用于同步去重。F2的 `Game actions` 可输入格坐标执行同类操作并查看Input/Debug回执。风设置、会话控制及Formula工具栏也经Session端口；Session对外只提供World/Runtime的const引用；内部运行错误直接传播，存档I/O与领域拒绝保留明确结果。公开契约、权限与M1/M2边界见[动作实现说明](Docs/Architecture/GameActions.md)。

启动和R后默认运行；暂停由GameSession统一管理，Simulation不再维护独立暂停开关。F2主面板顶部的 `Pause game` 冻结角色、材料、风场、构式、设备、育圃和世界意志结果交付，世界修改动作返回 `GamePaused`；`Step game 1/60 second` 可显式推进一个完整固定步。暂停不积累游戏时间或缓存跳跃，F3 DebugCamera及显示控制仍可使用；保存、重置和I/O管理仍可操作。引擎顺序保持帧首快照发布 → 输入与游戏推进 → Draw消费发布数据；Skip不推进，Draw不读取实时World。

固定步时钟由Session的SimulationCoordinator管理；成功环境→角色→构式→设备一步才增加Tick，暂停编辑只增加CommitRevision，动作回执携带CommitStamp。预算等待期间呈现继续读取最后完整副本，流式加载与步首输入保持冻结；细节及后续边界见[固定步与提交](Docs/Architecture/SimulationCommits.md)。

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

角色碰撞为独立2×4格矩形，速度16格/秒、跳跃32格/秒、重力90格/秒²，按≤0.25格分段碰撞。角色不受风，不向泥土施力；地图缺失作为实体阻挡。相机与人物使用块坐标加块内double，GPU接收相机附近相对值，远距离仍保留亚格精度。相机跟随系数默认8/s，可在面板调整；玩家采用脚底对齐的分层像素人物，不写入地图材料。

人物配置为 `Pixel/Content/CharacterAssets.json`，默认使用 GandalfHardcore 的身体、裤子、上衣、鞋子与头发五层原图复制件。A/D 播放跑步并切换朝向，静止播放待机，Space 上升/下落分别选择跳跃/下落动画；全局暂停冻结帧号。帧网格、脚底锚点、显示比例、图层顺序、动作帧数/速度均可配置，重启加载。来源、许可、配置合同与验证见[美术资源接入](Docs/ArtAssetIntegration.md#人物分层动画2026-10-27)。

全局暂停的原生窗口专项：`(cd Pixel && ../build/tests/PixelRuntimeSmoke --pause-only)`，覆盖默认移动、真实暂停复选框、暂停中的DebugCamera和恢复跳跃。

## 风场和显示

### 逻辑像素与渲染像素

一个逻辑像素就是一个世界格；材料、碰撞、角色尺寸、速度、力场、构式半径和存档全部使用逻辑单位。渲染像素指帧缓冲中的一个像素，默认一个逻辑像素显示为 **4×4** 个渲染像素。相机投影、可见区域、地形、人物、设备、构式特效和鼠标选格共用 `GridView2D::CellSizePixels`，横纵值必须相等，取值范围为1～64的整数。

`PIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL` 是编译宏，CMake提供同名缓存参数，例如：

```sh
# 每个逻辑像素显示为8×8；省略参数时默认4×4
cmake -S . -B build -DPIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL=8
cmake --build build --target Pixel -j4
# 恢复默认配置（CMake会保留上次显式设置）
cmake -S . -B build -DPIXEL_RENDER_PIXELS_PER_LOGICAL_PIXEL=4
```

1、2、4、8、16均可配置，32和64也可用于放大观察。非CMake构建可统一定义该宏，未定义时头文件默认4；所有使用相机/视图的编译单元必须一致。`+/-`仍然按两倍放大/缩小（限制在1～64），只改变显示比例；R重置世界时恢复编译配置。调整窗口或DPI不改变比例，鼠标事件先从窗口单位换算到事件对应的帧缓冲像素，再选取逻辑格。

美术纹理的texel密度与此比例独立，最近邻采样将纹理映射到逻辑世界；人物的`PixelsPerCell`仍表示素材texel/逻辑格。背景和UI沿用各自的屏幕布局规则。改变比例不会重采样世界数据，也不会更改物理数值或存档格式。

`Pixel.PixelScale`验证多倍率的投影、方形约束、远坐标与HiDPI拾取；`Pixel.PixelScaleConfig1/2/4/8/16`分别编译对应宏并检查默认/重置行为；`PixelRuntimeSmoke --terrain-only`读回五种比例的真实GPU地形并检查采样颜色。

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
(cd Pixel && ../build/tests/PixelCloseSmoke)
# 单独检查实际地形PNG、表面条带和远坐标UV
(cd Pixel && ../build/tests/PixelRuntimeSmoke --terrain-only)
# 所有配置人物帧的PNG/GPU读回与正式游戏移动、跳跃、暂停
(cd Pixel && ../build/tests/PixelRuntimeSmoke --character-only)
# 原生背包UI、Tab/Escape、GPU逐像素及后处理隔离；可选导出开合画面
(cd Pixel && PIXEL_UI_CAPTURE_DIR=/private/tmp/pixel-ui ../build/tests/PixelRuntimeSmoke --inventory-ui-only)
# 连续144步的水、蒸汽、火及动态区域上传；可选导出每24步截图
(cd Pixel && PIXEL_ELEMENT_CAPTURE_DIR=/private/tmp/pixel-elements ../build/tests/PixelRuntimeSmoke --elements-only)
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

Environment显示驻留/排队块、预算与I/O错误；Performance显示阶段时间、P95、材料访问格数、活动区域、上传量及内存，支持CSV。内存为所有权容量与工作预留，不等于进程RSS；风求解器单列，GPU不计。自动捕获和扩展方式见 [采样模块](Pixel/Source/Dev/Debug/Performance/README.md)。本轮结果以 [新规则基准和最终验证](Docs/WorldRepresentationValidation.md) 为准，早期测量保留于Docs/PerformanceProfiling.md、MemoryProfiling.md和MaterialUpdatePerformance.md。

2026-10-26世界双缓冲：CPU 97/97、64格专项12/12、Apple M5元素GPU专项及独立review通过。动态风配合1–3个读视图的CPU提交与场图打包总耗时下降19%–26%；无读者及运动材料的时间/内存取舍见[双缓冲验证与三轮性能对照](Docs/WorldBuffersValidation.md)。

Linux自动鼠标检查需要X11/XWayland的DISPLAY；GLFW Wayland不能设置光标，可设置 `XDG_SESSION_TYPE=x11` 运行smoke。2026-10-19历史RTX 3070/X11完整Pixel smoke曾在后续生命周期阶段报 `no drawable 2D frame`；Axiom `--skip-minimize`两轮通过，该历史结果不覆盖最小化恢复。本机Apple M5完整Pixel smoke通过（含两轮最小化恢复），AET --skip-minimize两轮通过；validation layer不可用，完整范围见验证报告。

2026-10-25模块整理验证：默认CPU 97项中96项首轮通过，育圃预算夹具在原始HEAD也会解引用空快照；改为先发布基线、等待流式完成再压缩预算后，育圃专项通过。64格区块Release专项14项、独立Formula 7项和ApplicationClose通过；Pixel、AET及GPU smoke目标编译通过。RTX 3070/X11的地形、背景、风、基础交互、力场、构式、设备/育圃/祈祷UI均通过；完整Pixel smoke随后在生命周期阶段报`no drawable 2D frame`，因此不声明完整生命周期通过。PixelCloseSmoke及AET `--skip-minimize`两轮通过。

## 构式（Formula）与编辑器

当前默认采用 `formula 2;`：带类型参数的函数与不可变 action 描述，可顺序、延迟、重复或按命中/唤醒事件组合。`process 1;` 源码及已有运行存档保持原语义；未知格式明确拒绝，不删除存档或重新执行。旧 cast 和树形编辑器已删除。完整合同见[构式函数组合与世界绑定](Docs/Architecture/FormulaComposition.md)。

右键从人物前方向鼠标激活当前程序，Shift＋右键开采。默认程序为黄色球：伤害元数据10、速度60格/秒、寿命120活动步、半径0.35格。它显式等待命中或到期，程序结束清理归属效果。球使用圆形扫掠碰撞；作用区域不活跃或必要块缺失时冻结，返回后按剩余活动时间继续。

新世界默认在玩家出生点右侧12格放置一个木色木头人（Natural、Flat和育圃均有）。对准它右键即可测试构式弹体；弹体会在2×4格身体范围内命中，`on_hit` 后续动作在实际命中位置执行，地形遮挡仍有效。木头人固定位置、不攻击、不可被击杀；挖掉脚下地形、进入水或火、连续高伤害命中都不会让它移动或消失。它是独立Actor，不写入材料地图；当前没有怪物AI、生命值或伤害飘字。F1隐藏外观，R创建新世界时重建一个；显式持久会话保存其身份和位置，旧存档不额外补生。

木头人验证：`ctest --test-dir build-cpu -R '^Pixel.TrainingDummy$' --output-on-failure`；原生画面专项为 `(cd Pixel && ../build/tests/PixelRuntimeSmoke --dummy-only)`，可通过 `PIXEL_DUMMY_CAPTURE=/private/tmp/pixel-dummy.ppm` 导出画面。

F2控制Pixel Debug，F4独立控制 `Formula Editor`。编辑器初始化为已应用程序，可直接修改 `Process text` 或载入三脉冲草稿，依次点击 `Compile process`、`Apply process`。编辑源码会撤销编译结果，错误显示行列；Apply仅影响后续实例，运行中的实例保留原版本。`Load applied process`恢复已应用源码，`Clear running`清理运行实例及归属效果。

F2菜单点击 **Formula presets**，或直接按F4，在顶部选择五套函数演示：`Mining volley`（三连开采弹）、`Delayed excavation`（命中后两阶段延时扩挖）、`Alternating force`（交替推力场）、`Walking beacon`（角色附着信标）、`Wake launcher`（事件唤醒发射器）。`Load preset source`将源码放入编辑区，`Apply & run preset`直接应用并在玩家附近运行；后续右键也使用该程序。重新运行只替换同一预设，`Stop preset`只停止所选演示，`Clear running`清理全部构式。唤醒预设运行后按`Wake preset`触发；力场用F1观察，信标配合A/D移动观察。开采实际改变地形；暂停或世界未就绪时运行按钮禁用。演示配置、观察方法与实现入口见[构式合同的预设说明](Docs/Architecture/FormulaComposition.md#debug函数预设)。

`Process: three carriers`可从玩家附近启动NaturalSource和FixedFixture调试承载者，使用同一程序和宿主规则；额度分别为6、4，玩家为64。真实Device另有物品和燃料规则。`Start actor attachment` 将同一构式附着玩家，后续请求跟随移动，来源与附着身份独立。Actor销毁或离开授权范围取消，离开活动区冻结。实例观察显示绑定、位置与方向，最近回执显示源码位置和拒绝码。这些承载者共享 `ProcessHost`，World不依赖构式或渲染。

游戏默认运行。GameSession全局暂停冻结角色、构式、环境、设备和育圃，DebugCamera仍可移动；单步推进完整的1/60秒。预算等待不推进实例，并拒绝新激活和编辑。检查点保存已应用程序、运行实例、事件、弹体及持续场源；默认球存活时也能保存并正常关闭。未应用编辑草稿不保存，动态风扰动仍按配置和相位重建。

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
