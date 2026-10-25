# BehaviorDSL 必要性实验（M6.1）

日期：2026-10-24。基线：`0733557089ef321e52595c23b64972e8e6dfdc35`。

M6阶段对当前整合代码的复核与独立复测另见[阶段验收](../../.awtcontext/GlobalDoc/M6Acceptance.md)。本文保留M6.1实验设计与初测记录。

## 结论与适用范围

**暂缓独立、可安装且持久运行的 BehaviorDSL。** 当前 M5 育圃援助需要的是“根据授权观测选择一个目标，提出一次批准程序的激活建议”；已有原生函数组合、DecisionAPI 和 Formula continuation 可以完成这一流程。本实验新增有限决策图进行同场景对照，没有改变游戏默认 PseudoLLM、模型协议、权限或存档格式。

这个结论只覆盖当前援助/等待场景，不意味着结构化单动作足以表达未来全部行为。跨决策等待、重新观测后的分支、多动作有序执行尚未交付。当前没有实际模型服务，也没有要求这些能力的已实现玩法；不能把推测中的缺口当作建设完整编译器的依据。

## 从当前代码区分三种缺口

| 实际需求或候选变化 | 当前可表达性 | 缺口所在 |
| --- | --- | --- |
| 对已观察到的设备或材料格提出援助；目标未知则不行动 | 原生函数可条件分支，DecisionAPI 选择一次终结提案；协议可传递相同结果 | 无须新增语言 |
| 援助持续600成功tick；离域冻结，加载后不重发 | 已批准 GardenAidProgram 的 field/await/wait 和 FormulaBridge continuation；租约由游戏核验 | 已有独立 Formula 与宿主生命周期 |
| “植物开花后停止援助” | ModelInput 仅带 Materials/Devices，未带植物成长；Proposal 也没有停止既有干预的动作 | 先定义授权植物观测及停止动作；单加 if/wait 语法无法实现 |
| “先帮助设备A，若失败再帮助B；以后重新观察” | C++可表达，但一次 DecisionAPI 只能终结一次；跨请求状态、重观测与时间语义目前没有公开合同 | 若玩法确需，先比较 WorldWill 小型状态机与独立有界过程 |
| 从模型动态安装任意逻辑 | 现有提案不能选择新源码、能力、caller、执行额度或任意程序 | 尚无产品必要性；属于新授权与执行合同，不是 JSON 扩展即可完成 |

证据入口：[LLM 输入值](../../Pixel/Source/Runtime/Gameplay/LLM/LLM.h)、[DecisionAPI](../../Pixel/Source/Runtime/Gameplay/LLM/DecisionAPI.cpp)、[WorldWill](../../Pixel/Source/Runtime/Gameplay/WorldWill/WorldWill.cpp)、[援助程序](../../Pixel/Source/Dev/Debug/Garden/GardenAidProgram.h)、[干预入口](../../Pixel/Source/Runtime/Gameplay/GameSession/SessionAuthority.cpp)。植物在原生 [GameplayObservation](../../Pixel/Source/Runtime/Gameplay/GameSession/SessionGameplayObservation.cpp) 中存在，不能因此假定它已授权给模型。

## 可运行对照

实现全部位于 [UnitTest/Experiments](../../UnitTest/Experiments/CMakeLists.txt)，只有 `BUILD_TESTING` 开启时构建，不被生产模块链接，也不与 Formula 合并。

- `NativeAidDecision`：普通C++条件、目标查找及 DecisionAPI 函数组合。策略确定性地尝试援助，排除 PseudoLLM 随机选中等待所造成的测量噪声；它不是对随机兜底的修改。
- `DecisionGraph`：独立、版本为1的类型化决策表示和小执行器。节点只可测试目标类型/可见性、追加说明以及提出设备/格子/无动作提案。9个节点表达同一策略，每次只执行其中4个节点。
- 两者读同一自有 `ModelInput`，产生相同 `ModelReply`，经真实 WorldWill 与 GameSession 执行同一个 `GardenAidProgram`。持续作用、时钟、资源、权限和保存仍由同一生产路径负责。

候选图编译时最多16节点、所有说明文本及分隔符合计最多1024字节，只允许前向边且所有路径必须终结。自循环/回边、越界目标、终结后的第二提案、未知操作、超量文本/指令、未知版本均返回带节点位置的诊断。输入限制沿用4096格、64设备、512KiB。运行中最多16次分派，每个可见性节点最多扫描对应输入；最坏不是固定4次，不声称按像素运行适宜。

编译产物拥有指令和文本，修改输入草稿不影响它。说明只在本地 DecisionAPI 累积，终结前没有世界作用。候选图没有文件/网络/原生函数调用、变量、递归、子进程、订阅、等待队列或跨帧状态；字段中不含宿主凭据或自定义效果。缺失设备目标返回Invalid；已经编译的内部结构违约直接抛出逻辑错误。

这是用于必要性判断的有限决策 DAG，**没有生产安装入口、内容哈希、磁盘格式、跨版本热迁移或挂起恢复**。不把这些未实现能力计为候选图验收通过，也不允许将它直接用作持久 BehaviorDSL。持续行为恢复的证据来自本次重跑的现有 Formula/干预测试。

## 行为与诊断验证

`Pixel.BehaviorDecision` 对照已知空气、未知/缺失格、可见/缺失设备、说明与终结值，验证编译诊断及产物不借用草稿。实际育圃场景先开采材料再建造设备，两个后端分别走下列六条路径：

1. 模型未完成时模拟继续；暂停收到提案只进入Ready；恢复后产生真实场源、回执和植物成长；保护撤销后后续成功步不再成长。
2. 撤销观察权限后拒绝待交付提案。
3. 换世界后丢弃旧epoch回包。
4. 更换已批准程序后明确返回StaleProgram。
5. 故意篡改适配器回包为范围外目标，WorldWill返回InvalidProposal。
6. 四次实际援助占满槽；第五次返回LimitExceeded，不增加援助计数。

两种决策表示都不能绕过游戏的最终裁决。候选图的环在编译期拒绝；持续 Formula 的无限循环、输出超额、剩余等待、保存恢复、冻结与后代清理由既有 `Pixel.Formula.Process*`、`Pixel.InterventionCheckpoint`、`Pixel.ProcessPersistence`、`Pixel.RegionActivity` 和 `Pixel.Authority` 测试验证。

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4

cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelBehaviorDecisionBenchmark -j4
./build-region-release/bin/Release/PixelBehaviorDecisionBenchmark > /private/tmp/behavior-decision.csv
```

## 运行成本与验证记录

基准每后端/场景预热100次，采样2000批、每批32次求值；每次使用4096格和64设备的最大数量输入，目标在末项或缺失。计时包含相同输入边界检查、目标扫描、提案字符串构造及返回值消费，不包括观察投影、模型I/O、图编译、物理推进或存档。CSV的p50_ns/p95_ns是每批耗时除以32后，2000个批均值的P50/P95，不能解释为单次决策延迟的尾分位。另输出输入保留字节与图保留字节；原生代码体积和分配峰值没有测量，0表示没有独立运行时程序数据，不能解释为总成本为0。

2026-10-24，macOS/Darwin 27 arm64，两个CMake构建实际使用 `/usr/bin/c++`（AppleClang 21.0.0.21000334）。独立验证agent完成 Debug 配置/全量构建、新测试 **1/1**（2.04秒）和相关回归 **15/15**（3.04秒）；Release配置/实验目标构建及新测试通过。最终补全显式include后，增量构建和 **CPU全量94/94**（45.91秒）、**Release新测试1/1**（0.24秒）通过。`BUILD_TESTING=OFF` 的独立配置目标清单不含任何 `PixelBehaviorDecision*`，未构建该配置的全部目标。

三轮Release原始数据保存在 [CSV](BehaviorDecisionBenchmark-2026-10-24.csv)，每轮校验和均为36736600。下表为三轮批均值分布各自P50/P95的最小–最大值，单位为微秒/次，不是单次延迟分位或游戏帧耗时：

| 场景 | 原生P50 / P95 | 决策图P50 / P95 |
| --- | --- | --- |
| 4096格末项命中 | 1.547–1.707 / 1.965–3.108 | 1.523–1.536 / 1.600–1.714 |
| 4096格末项未知 | 1.542–1.565 / 1.628–1.724 | 1.520–1.565 / 1.680–1.698 |
| 64设备末项命中 | 0.0378 / 0.0391–0.0443 | 0.0403–0.0417 / 0.0417–0.0469 |
| 64设备均不匹配 | 0.0378–0.0391 / 0.0391–0.0403 | 0.0391–0.0417 / 0.0404–0.0417 |

两者使用同一201990字节输入保留量；候选图额外保留770字节（保守计量包含小字符串的重复计数）。输入扫描主导格子场景；设备分支差异仅数纳秒，不能据此推断总体性能优势。原生先于图测量、编译器内联及缓存都可能影响微基准。当前不足以用成本差异支持新语言；决定暂缓的主要理由是相同场景没有新增表达能力，却需要另行维护安装/状态合同。

独立review未发现正确性、权限或边界问题，独立运行新增测试1/1、相关回归12/12及Release基准通过；指出批次平均耗时分位不能称为单次延迟分位，本文已修正。完整审查记录见AWT任务Doc索引。

本轮未改生产调用路径、渲染或引擎，没有运行GPU/UI/AET、真实模型网络或完整sanitizer。候选图编译及执行不接受外部文本，也未作通用解析器或持久执行器验收。

## 重新考虑采用的门槛

只有出现可复现的跨决策过程，且普通有限动作组合/WorldWill局部状态机明显难以维护时，再提出独立过程候选。下一次实验应固定一个真实场景，明确新观测、新动作、作用时间和所有权，然后比较表达、诊断与运行成本。

若决定安装持久行为模块，必须另行完成内容/能力/状态版本、每片工作与内存及队列预算、宿主提供的时钟和随机源、撤权及全部后代作用范围、检查点恢复且不重放的合同。模型不能编辑这些限制。Formula继续独立；没有证据表明本次需要新增 Axiom 通用能力。
