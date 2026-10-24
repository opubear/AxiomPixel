# M4 持久设备与家园阶段验收

2026-10-24。本次核对已完成的 M4.1–M4.8 在当前代码中的整合状态。验收基线为 `e9f24a2`，本轮仅修正一个测试坐标假设和文档，没有修改生产代码或引擎。M5 已加入的知识、权限及干预保存仍按各自合同解释，本记录不代替 M5 的阶段验收。

## 场景与模块边界

玩家从真实矿脉取得石头、铜矿和煤，用 4 石头、2 铜矿建造推力发生器，补煤后由同一 Formula field 原语持续改变局部加速度。育圃读取真实采样值和地形条件，累计 180 个健康步开花；离域冻结、返回接续，保存重建会话后继续原周期和成长。没有额外物品产出或远处补算。

| 子任务 | 当前合同与组合责任 | 本次核对的主要证据 |
| --- | --- | --- |
| M4.1 Actors | Session 拥有角色；持久 ID 与内存句柄分开；World 不拥有角色 | Actors 的多对象地形、冻结、身份复用、远坐标和发布副本测试 |
| M4.2 Items | 独立库存值与整批预留/转移；游戏开采入口负责材料和收益一致性 | Items、InventoryGameplay 的满包拒绝、资源计数和版本核验 |
| M4.3 Devices | 设备领域保存身份、承载状态和燃料引用；Construction 组合放置、供给、回收与程序 | Devices 的成本、周期、取消、阻塞、容量及整批回收；育圃真实开采 |
| M4.4 FieldSource | Environment 拥有有界来源；宿主以 owner/handle 控制生命周期，来源不改永久 BaseForce | FieldSources 的稳定叠加、归属、到期、预算等待、远坐标及 Dirt 响应 |
| M4.5 区域活动 | 程序与归属场整体冻结，保留身份/剩余时间；返回前复核承载者 | RegionActivity、Checkpoint 与 ForceGarden 的离开/返回、无重复扣煤和无补算 |
| M4.6 检查点 | Session 收集完整提交；SessionStore 写不可变世代，最后发布指针 | Checkpoint、ProcessPersistence、SessionStore 的真实目录、卸载脏块、发布失败/重试与跨域引用 |
| M4.7 观测 | 同一 CommitStamp 的拥有值副本；空间与库存权限分别裁剪；渲染和 UI 使用发布帧 | GameplayObservation、LLMView/JSON、DeviceRendering 与真实 GPU/UI smoke |
| M4.8 力场育圃 | Gameplay/Home 组合现有能力；两个固定花床，不在 World 中建立玩法规则 | ForceGarden 的真实矿脉、设备供煤、成长、冻结/返回、磁盘重建与晶体清理；garden GPU/UI smoke |

依赖核对：World 只链接 PixelPerformance 与 Axiom::Core；Actors 依赖 World；Items 为标准库模块；Devices 依赖 Items。跨域写入及成本、动作、冻结与检查点的编排在 Gameplay，World 不依赖 Formula、UI 或渲染。独立 Formula 库保留宿主查询/效果合同，Pixel 专用场景和渲染未进入引擎。

## V1：资源、冻结与保存恢复

- `Checkpoint::ActiveDomainsResumeOnce` 核对角色动力、库存、设备周期/燃料、实例身份/指令位置/额度、场源身份/剩余寿命及已提交效果；重建直接续接 continuation，不重新 Cast、重扣煤或重发已完成效果。
- `Checkpoint::FrozenAndUnloadedChangesAreAtomic` 先保存旧世代，再修改并卸载脏块、冻结设备；以阻塞临时指针写入模拟发布失败，确认旧地形和燃料仍可恢复；解除阻塞后重试保存并加载新世代，返回接续相同来源与周期。
- `ProcessPersistence` 覆盖等待/已缓存事件、弹体剩余寿命、locals/句柄及宿主事件/弹体归属与载荷拒绝；其中 `ImmutableArtifactsAndIdentities` 比较资源版本、不可变产物及身份高水位。非法跨域引用拒绝由 `Checkpoint::InvalidDomainLinksFailExplicitly` 覆盖；旧树形活跃实例不能保存，显式拒绝。
- `ForceGarden` 从实际矿脉获得建材和燃料，再放置与运行设备，检查真实力场下的成长；域外不推进，返回不补算，磁盘重建保留成长、资源、周期与场源，已开采矿脉不补充。测试以显式玩家位置夹具模拟远行，未声称人工走完全程。
- `PixelCloseSmoke` 验证 PendingStep 和存储失败拒绝关闭、可见错误、显式重试及成功保存后的释放；GameEnd 本身只做最终释放。

这些测试覆盖可控的保存发布失败与实际磁盘重建，不是进程随机强杀或断电试验。存储没有 fsync 耐久保证，不承诺电源故障后最后一次保存一定保留。

## 本次运行结果

独立 CPU 验证 agent 与独立 GPU 验证 agent 在本地运行。CMake 4.4.0，实际 C++ 编译器 `/usr/bin/c++`，AppleClang 21.0.0.21000334；macOS/Apple M5，GPU 为 MoltenVK 选择的 Apple M5。

| 配置与命令 | 结果 |
| --- | --- |
| `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`；`cmake --build build-cpu -j4` | 成功，构建 1.24 秒 |
| `ctest --test-dir build-cpu --output-on-failure -j4` | 94/94，47.79 秒；在下述测试断言修正前运行，生产代码未再变化 |
| `cmake -S . -B build-chunk64 -DCMAKE_BUILD_TYPE=Release -DAXIOM_BUILD_RUNTIME=OFF -DPIXEL_CHUNK_EDGE=64`；构建下列 11 项对应目标 | 成功，初次构建 10.88 秒 |
| chunk64 的 Actors、Items、InventoryGameplay、Devices、FieldSources、RegionActivity、Checkpoint、ProcessPersistence、GameplayObservation、ForceGarden、DeviceRendering | 初次 10/11，Actors 单独复跑同样失败；修正断言后 11/11，0.583 秒 |
| 修正后重建两配置的 `PixelActorTests`；默认 `ctest --test-dir build-cpu -R '^Pixel\.Actors$' --output-on-failure` | 两配置构建成功，默认 Actors 1/1，0.567 秒 |
| `cmake --build build --target Pixel PixelRuntimeSmoke PixelCloseSmoke -j4` | 成功 |
| 在 `Pixel/` 执行 `../build/tests/PixelRuntimeSmoke --garden-only` | 退出 0；晶体/花床/成熟花朵实际 GPU 读回、多个帧槽、F1 隔离；ImGui 真实创建/应用构式/开采/建造/供煤/启用/成长、回执与冻结发布 |
| 在 `Pixel/` 执行 `../build/tests/PixelRuntimeSmoke --debug-ui-only` | 退出 0；七风策略、64 位世界高度、UI 捕获、F1/F2、设备操作、重置、缩放、内存和 2D 生命周期 |
| 在 `Pixel/` 执行 `../build/tests/PixelCloseSmoke` | 退出 0；完整关闭保存协商 |

chunk64 集合的精确命令：

```sh
cmake --build build-chunk64 --target PixelActorTests PixelItemTests PixelInventoryGameplayTests PixelDeviceTests PixelFieldSourceTests PixelRegionActivityTests PixelCheckpointTests PixelProcessPersistenceTests PixelGameplayObservationTests PixelForceGardenTests PixelDeviceRenderingTests -j4
ctest --test-dir build-chunk64 -R '^Pixel\.(Actors|Items|InventoryGameplay|Devices|FieldSources|RegionActivity|Checkpoint|ProcessPersistence|GameplayObservation|ForceGarden|DeviceRendering)$' --output-on-failure
```

本机日志前缀 `/private/tmp/pixel-m4-acceptance-`：`cpu-{configure,build,ctest}.log`、`chunk64-{configure,build,ctest}.log`、`chunk64-actors-repeat.log`、`{cpu,chunk64}-actors-fix-build.log`、`cpu-actors-fix-ctest.log`、`chunk64-final-ctest.log`、`runtime-build.log`、`garden.log`、`debug-ui.log`、`close.log`。临时日志不是长期存档，本表保留本次命令与结论。

本次没有重跑完整长 GPU smoke、sanitizer、AET 或性能基准。GPU smoke 为实际窗口运行，Vulkan validation layer 不可用。Axiom 子模块没有改动，邻接参考仓库未写入。

## V2：独立 review 与问题闭环

独立 review agent 审阅当前整合代码，而非仅检查本轮文档 diff：Construction、Home、SessionPersistence/Publication、FormulaBridge、SessionStore、Actors/Items/Devices、FieldSources 及相应检查点、冻结、成长与观测测试。未发现有证据的生产 P1/P2 缺陷；阅读测试与运行测试分别记录。

本轮发现并处理：

1. chunk64 的 Actors 测试把绝对出生坐标 X=511 当作区块内坐标；64 格区块内应为 63。现在用 `WorldPosition::FromAbsolute` 构造预期，比较 Chunk 与 Local，同时保留持久 ID 和旧副本不变检查，并核对精确目的地。独立 review 确认未削弱行为断言；默认 Actors 与 chunk64 的 11 项复验通过。生产重定位逻辑未改动。
2. FieldSources 文档仍写来源/设备观测留待后续；GameplayObservation 文档仍写知识 Unsupported。已链接现行观测与知识合同，保留明确标注的历史测试记录。

审查者另核对本次 CPU、chunk64 与三项 GPU 日志，确认与表格一致，并确认阶段证据足够；最终两处测试证据归属建议已按实际用例名称修正。无待解决的生产问题。

## 当前边界与后续入口

- 游戏可见收益是两处花床开花；没有生产链或完整生态系统。育圃窗口至少 128 格。chunk64 是存储块大小，不表示本次验证了 64 格育圃活动窗口。
- 设备为非碰撞设施；水与角色不受这些持续加速度来源推动。动态风扰动不保存，不承诺跨平台精确物理重放。
- 离域冻结占用有界容量；不后台生产、不补算离线时间。查询与呈现加载不扩大活动区域。
- M4 历史文档中的 PixelGameplay 2 由后续 PixelGameplay 3 扩展；当前保存格式、知识和干预范围分别见[检查点](Checkpoints.md)、[持久援助与自主权](PersistentInterventions.md)及[模型接口](ModelServiceIntegration.md)。
- 本次完成 M4 整体验收；M5/M6 阶段收尾仍由各自任务维护。按用户要求，完成这一项后暂停。
