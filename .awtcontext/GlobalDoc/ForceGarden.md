# 力场育圃贯穿场景（M4.8）

这个小场景把天然场源、独立构式、真实开采、设备供给、生态响应与一致检查点串起来。植物是虚构的力敏植物，其成长规则明确读取当前水平加速度。没有温度、湿度、完整生态系统或远处后台生产，也不将加速度称为空气风速。

## 试玩入口

F2展开 `Force garden`，点击 `Save and create force garden`。现有世界先按正常检查点规则保存，再建立一个独立世界；保存失败或正在等待预算时保留当前会话并报告原因。场景创建时安装96格宽的平整试验区，其他部分沿用所选生成器。模拟窗口需至少128格。新场景默认暂停；R创建普通世界，不复制育圃。

1. 左侧青色晶体中心为 `(480, surface-4)`，野生花床为 `(484, surface-1)`。晶体每600个活动步启动一次相同构式：半径8、向右3600格/秒²的持续场。天然承载者是本场景的可再生来源，每个过程只获得一次付费效果额度；它不消耗玩家物品。
2. `Apply crystal program` 应用晶体的同一个不可变编译产物。玩家走到右侧花床8格以内，朝空地右键，在人物前方生成临时场。点击位置决定方向，不远程生成来源。面板展示完整源码；F4编辑器可进行进一步试验。玩家与天然源共享同一个field原语、半径、强度和寿命；玩家已有实例保留旧版本。
3. 地表高度以面板为准。`surface+2`行的x516–519为4石头，520–521为2铜矿，522–537为16煤。Shift+右键或面板按钮通过相同MineRegion入口开采；第二次开采不能再得到资源。初始背包为空。
4. 在 `(530, surface-2)` 放置现有推力发生器，消耗4石头+2铜；补8煤、启用。设备半径16，向右3600格/秒²，每煤30个成功环境步，沿用现有库存预留、拒绝、回收和程序所有权规则。
5. 取消暂停或单步。右侧花床 `(540, surface-1)` 累计180个健康步后开花；左侧野生花床用于对照。F1显示合成力方向，F2显示精确采样、条件、成长和设备燃料。两个重叠向右场可能超过阈值，面板会解释暂停成长的原因。
6. 离开后返回，或Save world后退出并重启，成长、已开采地形、库存、设备周期和场源身份都会接续。Clear running停止所有设备与程序，并禁用本场景天然晶体的自动重启；花床积累保留。

## 生态规则与阶段

`Ecology/ForceGarden.h` 保存小型值合同；`Gameplay/Home/ForceGarden.cpp` 在Session内组合当前领域能力。只维护两个固定花床的有界状态，不建立通用生态框架。状态包括场景启用、地表位置、晶体开关与两个0–180成长计数。

花床的采样点必须驻留、在玩家活动岛内、材料为空气，下方一格必须能支撑角色。采样使用 `World::GetForce` 的X分量，单位格/秒²，含基础力、现有风响应与持续来源。正向1800–5400（含端点）时每成功步增加1，反向、不足、过强、阻挡、无支撑或离域时停长；既有进度不减少，成熟封顶180。

阶段为环境→角色→构式→设备→育圃→统一提交。育圃读取本步构式/设备已经提交的来源，因此新来源可以立即满足植物条件；材料运动仍从下一环境步开始使用新来源。暂停、Skip和预算等待不推进成长。域外时间不补算；晶体和设备实例、归属来源沿用M4.5的整体冻结合同。

晶体不进入材料表或碰撞系统，苗株也不写地形；地形阻挡决定成长条件，画面只是其当前进度的表现。本轮生态收益是可保存的成熟花圃，不产出额外物品。后续若加入收获或更多生态规则，需要独立定义资源与生命周期。

## 发布与持久化

完整原生 `GameplayObservation.Garden` 与设备/库存/程序/来源共享CommitStamp；UI和世界几何只消费帧首发布副本，Draw不读实时World。预算等待期间继续显示最后完整提交。F1隐藏晶体和植物图形。受范围/授权约束的ObservationProjector和LLMView目前不暴露植物topic，设备与场源查询继续按现有权限工作。

育圃状态写入同一个不可变检查点内的 `PixelGameplay 2` 值数据；读取仍支持 `PixelGameplay 1`（没有育圃）。旧v2–v5导入、current指针原子发布和失败保留上一完整检查点的边界不变。重载不调用场景初始化，不重新铺地、不补矿、不重复启动已存在的天然过程。各程序continuation和来源继续由原有跨领域状态保存，花床进度与它们取自同一个提交。

没有修改Axiom或独立语言语义；World继续不依赖Formula、Ecology或渲染。天然与设备的运行额度、资源和生命周期是游戏宿主策略。

## 验证入口

```sh
cmake --build build-cpu --target PixelForceGardenTests -j4
ctest --test-dir build-cpu -R '^Pixel.ForceGarden$' --output-on-failure
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --garden-only)
cmake --build build-region-release --target PixelForceGardenBenchmark -j4
./build-region-release/bin/Release/PixelForceGardenBenchmark /private/tmp/pixel-force-garden.csv
```

## 本轮验证结果（2026-10-23）

- CPU完整回归：`cmake --build build-cpu -j4` 后执行 `ctest --test-dir build-cpu --output-on-failure -j4`，82/82通过。最后调整64格窗口拒绝用例的准备方式后，单独重跑 `Pixel.ForceGarden`，1/1通过。覆盖实际开采与重复开采、设备成本和供煤、正反向/过强/阻挡条件、真实预算等待、离域冻结与返回、检查点重载、旧版值数据兼容及天然晶体跨600步续期。
- 物理块64格的Release构建：`Pixel.ForceGarden`、`Pixel.Checkpoint`、`Pixel.RegionActivity`、`Pixel.Devices`、`Pixel.GameplayObservation`，5/5通过。育圃仍要求活动窗口至少128格。
- 实际原生窗口与GPU：`PixelRuntimeSmoke --garden-only`、`--debug-ui-only`、`--formula-only`均退出0。包括真实ImGui按钮、平台鼠标/键盘输入、构式右键启动，以及4个帧槽的幼苗/成熟读回和F1切换返回。Debug UI测试的焦点准备改为ImGui明确聚焦，避免滚动后误点折叠标题；控件操作仍走实际平台输入。
- 独立subagent完成实现审查及最终复核，追踪天然过程、开采/设备资源守恒、提交观察和检查点恢复；没有未决重要问题或有依据的P1/P2功能缺陷。审查发现的测试身份断言、预算等待夹具和基准测量范围问题均已修正，主执行与验证agent运行上述最终测试。

GPU验证环境为Apple M5/MoltenVK；当前环境没有 `VK_LAYER_KHRONOS_validation`，未覆盖Vulkan验证层。本轮运行三组专项GPU/UI测试，未宣称完整GPU总套件、sanitizer或引擎AET回归。Axiom源码、gitlink及版本锁定均未改动。

### Release基准

固定有限Flat 1024×1024场景，关闭天然晶体，只比较同一台设备关闭/开启；每轮预热8步，再测量240步，每组3轮。每帧调用BeginFrame，计时范围为Tick；16煤保证开启组全程有源。基准使用显式库存夹具，真实开采守恒另由CPU贯穿测试验证。结果不代表整个GPU帧或无限世界流送性能。

| 设备状态 | P95范围（ms/步） | 平均活跃源 | 保留源/程序峰值 | World预留字节 | 原生Gameplay投影字节 | 右侧成长 |
| --- | --- | --- | --- | --- | --- | --- |
| 关闭 | 0.002833–0.002834 | 0 | 0 / 0 | 14,737,384 | 511 | 0 |
| 开启 | 0.042875–0.048166 | 1 | 1 / 1 | 14,737,384 | 727 | 180 |

原始数据：[benchmark.csv](../../Docs/Validation/ForceGarden/benchmark.csv)。World预留容量相同；投影字节统计来自现有RetainedBytes口径，不代表进程全部内存。

### 图像与已知边界

GPU读回样例使用纯色材料测试夹具：[幼苗](../../Docs/Validation/ForceGarden/seedlings.png)、[成熟](../../Docs/Validation/ForceGarden/mature.png)、[F1切回](../../Docs/Validation/ForceGarden/restored.png)。最后一张是显示模式返回，不是磁盘恢复截图；存档重启由CPU检查点贯穿测试验证。

本轮发现既有普通世界初始化在“物理块64格且活动窗口64格”组合下，可能因出生点/支撑跨块而触发 `generated spawn is not dry and clear`。本任务未修改World对齐规则；小窗口拒绝测试先用正常窗口保存，再以64格窗口重载，确认育圃创建被拒绝且旧检查点不变。128格以上育圃窗口及64格物理块专项均通过。
