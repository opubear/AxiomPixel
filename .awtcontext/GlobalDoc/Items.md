# 物品、库存与开采资源合同

M4.3补充：真实设备现已接入原子建造/回收与煤槽转移，`Inventory::Destroy` 供拥有者销毁空容器；材料放置也检查设备占用。当前设备合同见 [Devices](Devices.md)，以下M4.2验证记录保留为该阶段历史证据。

M4.2 新增只依赖标准库的 `PixelItems`，由 GameSession 持有库存。World 继续只拥有材料与环境，Formula 语言不认识物品；游戏宿主将开采接到 Items。当前是单写者、进程内资源操作，尚未实现装备、掉落实体、合成界面；M4.6已接库存存档。

## 定义与身份

`Items/Item.h` 定义七种稳定物品 ID：soil=1001、stone=1010、coal=1020、copper_ore=1030、iron_ore=1040、tungsten_ore=1050、gold_ore=1060。定义版本均为 1。ID 不使用材料编号、纹理层或容器下标。首批定义以原生常量装配；`Definition` 对未知内容引用抛出逻辑错误，启动校验重复 ID/名字、空名字/版本和设备成本引用。

容器 ID 在一个 Inventory 内单调分配，属于当前会话；与物品语义 ID 分开。`ContainerRef` 是 `{Id, Version}`，不是内存指针。调用方必须保留所属 Session/epoch，不能跨库存或世界使用本地 ID。`Clear` 不倒退分配器；重置世界创建新的玩家背包 ID。ReservationId 另带 Inventory 所有者身份，跨集合、已释放、已消费或 Clear 后的 token 均不能再次消费。

## 数量、容量与预留

容量表示所有物品数量之和，不是格数或重量。玩家背包初始为空，容量 4096；库最多 256 个容器、64 个同时存在的预留，每个栈最多一种已定义物品。数量为 uint32，求总和使用 uint64，预检容量后才转换。

`Inspect` 返回拥有值的快照，包含版本、容量、总数量和各物品 Quantity/Reserved；`Available = Quantity - Reserved`。数量包括已经预留的部分，预留不会腾出容量。快照不借用内部 map。

原生领域操作如下：

- `Add(ref, amounts)` 注入领域已经产生的物品，供开采及可信内容装配使用。
- `Transfer(sourceRef, targetRef, amounts)` 同时检查双方版本、来源可用量和目标容量；全部满足才转移整批物品。拒绝时双方数量、预留、版本都不变化。
- `Reserve(ref, amounts)` 为一批现有物料分配 token，整批缺料时不部分预留；预留数量不能被另一预留或转移使用。
- `Release(token)` 释放锁定数量；`Consume(token)` 扣除锁定物料；二者只能成功一次。token 完成不重新检查容器版本，因为其锁定份额已受保护，不应被无关入库或其他预留失效。

成功的增量操作提升受影响容器版本；失败不提升。空批次、零数量、重复物品项和同容器转移返回 InvalidAmount；未知请求物品返回 UnknownItem，过期版本返回 StaleVersion。可信内容引用错误与正常缺料/满包拒绝保持区别。分配失败及预检后不变量错误直接传播，不尝试恢复半提交游戏。

预留是宿主显式拥有的资源：后续 Devices 在空间等预检失败时 Release，全部预检成功后 Consume 并创建实体。当前没有通用跨域事务或自动回滚。转移和预留只作为 Items 的类型化原生 API；Session 对外仍只读暴露 Inventory，没有新增可变通道或外部传输协议。M4.3 在设备动作中接入这些能力。

## 开采与放置

`Gameplay/Actions/Mining` 是共同开采规则。每格土、固定土或草皮产一份 soil；石头产 stone；五种矿石各产相应物品。空气和水不可开采。规则放游戏层，World 不依赖 Items。

Input、Debug、Driver 的 MineRegion 和玩家的树形/过程构式挖掘，都先检查产物容量，再在同一同步提交段入库并删除材料。MineRegion 满包返回 InventoryFull，保留材料和库存，不增加 Session commit；构式激活本身仍按既有合同提交，挖掘失败通过过程的 Capacity 回执或树形构式的 inventoryFull 诊断报告。MaterialResult 带 Collected/Consumed，重复 request 仍返回原回执，不再次产出。

左键 PlaceMaterial 仍只支持 Dirt，现在消耗一份 soil；先检查材料和角色占用，再预留资源、放置并消费。缺料返回 InsufficientItems，目标仍为空。初始先开采再放置，避免免费放置后反复开采生成资源；三种原生来源适用同一规则。Debug 回执显示失败原因。

圆形构式挖掘按 `(y,x)` 遍历、逐格提交，允许部分成功：每个成功格产物恰好入库一次；后续装不下的格保持原样。FormulaDigResult 分别统计 Applied、Blocked、NotReady、InventoryFull；过程请求全部因满包失败时返回 Capacity。部分成功继续按现有过程语义返回 Applied。暂停激活与完整模拟步沿用已有提交时机，PendingStep 拒绝修改。

天然源与固定测试装置当前没有物品容器，使用无收集者的环境挖掘：同样检查材料可开采性，删除材料但不奖励玩家。独立 FormulaBridge/WorldAdapter 默认也是显式无收集者的宿主模式；生产 GameSession 在世界替换后、启动实例前绑定玩家背包。固定装置的供料、真实设备归属属于 M4.3；M3 的有限 credits 保持独立预算，未冒充真实库存。

## 设备建设成本与阶段边界

`Gameplay/Construction/DeviceCosts.h` 给首个 WindEmitter 定义 v1 成本：4 stone + 2 copper_ore。成本属于游戏建设规则，Items 只处理物品批次。CPU 场景验证开采到这些物料、整批预留、取消返还、消费以及第二次缺料不部分扣除；本步没有创建设备实体。

M4.6已保存容器ID/版本/容量、物品语义ID与版本、数量和身份高水位；恢复重新分配预留Owner，旧token失效。预留只存在于同步动作内部，捕获时有未完成预留直接报告内部阶段错误。R建新世界背包为空，正常重启保留库存；合同见[一致检查点](Checkpoints.md)。M4.7再增加完整库存/设备的观测与UI。

## 验证入口

`Pixel.Items` 单独链接 PixelItems，覆盖整批转移、容量与缺料拒绝不修改、双方版本、预留隔离/释放/消费、重复 token、跨 Inventory token、Clear、UINT32_MAX 数量、内容错误、容器/预留上限。

`Pixel.InventoryGameplay` 覆盖三原生入口的全部开采映射、设备成本、重复请求、满包、放土消耗、PendingStep、重置、两版玩家构式、天然源无玩家产出和圆形部分成功次序。旧 Actions/Actors/Input 及 GPU 点击夹具补充合法物料来源，仍保留原本坐标、占用和渲染断言。

2026-10-23，独立验证 Subagent 执行 `cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF`、`cmake --build build-cpu -j6` 和 `ctest --test-dir build-cpu --output-on-failure -j4`，CPU/mock 全量 73/73 通过（46.03 秒）。生产代码此后未改；最终只补强 GPU 测试夹具和文档。

`cmake --build build --target Pixel PixelRuntimeSmoke -j6` 成功。Apple M5/macOS 沙箱内首次运行无法连接窗口服务，结束该测试进程后在沙箱外执行。首轮 GPU 在 UI 旧夹具未提供土料处失败，补齐来源后完整 `PixelRuntimeSmoke` 退出 0：材料、背景、风、游戏点击/移动、F1、构式、ImGui及两轮 resize/minimize 生命周期均通过。之后补强 UI 捕获夹具，重新构建并执行最终 `PixelRuntimeSmoke --debug-ui-only`，退出 0。该最后修改只涉及 UI 夹具，CPU及其他GPU段结果仍适用。Vulkan validation layer 不可用；未运行 Release/sanitizer，未修改引擎或重跑 AET。

独立 Review 重跑 Items、InventoryGameplay、Actions、FormulaGameplay、Input，5/5 通过，并用临时独立程序检查预留期间转移剩余物料、无关入库、交错预留及消费/释放守恒。发现一项 P2 测试覆盖退化：空背包会令泄漏到游戏的 UI 点击因缺料失败，掩盖输入捕获错误。已在首次 UI 操作前真实开采供料，并在 UI 字段编辑及每个风策略切换后检查完整库存快照不变；outside click 消费同一份土料。审查者复核关闭 P2，无新增问题。另将“满包不增加提交”明确限定于 MineRegion，保留构式激活自身的提交语义。`git diff --check` 与 AWT `check` 通过。
