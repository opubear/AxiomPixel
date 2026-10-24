# M6 运行时逻辑构建与 DSL 阶段验收

2026-10-24。代码基线 `74f78b08e05954651973e41359fe02a2ad3ff8d4`。本阶段复核已完成的 M6.1 必要性实验及其生产接入边界；本轮没有生产代码、测试或引擎改动，保留进入任务时尚未提交的 M5 文档。

## 采用决定与 V1 范围

**当前暂缓独立、可安装且持久运行的 BehaviorDSL。** 已实现的育圃援助可由原生函数组合选择一次提案，再由现有 Formula 程序维持作用。有限决策图在同一场景没有增加所需表达能力；其安装、状态和迁移合同也尚未实现。阶段验收接受这一有证据的暂缓决定，不把实现一门新语言作为完成条件。

实验设计、诊断、三轮初测及重启门槛见 [M6.1 实验报告](../../Docs/Architecture/BehaviorDecisionExperiment.md)。本次复核如下：

| 目标 | 当前证据与结论 |
| --- | --- |
| 同场景比较 | NativeAidDecision 与 DecisionGraph 使用相同 ModelInput、DecisionAPI 和真实 WorldWill/GameSession；已知/未知格、可见/缺失设备得到相同提案与说明 |
| 真实玩法结果 | 两种表示各跑援助、撤销观察、旧 epoch、过期程序、越界提案、槽位耗尽六条路径；验证 Ready 不提前执行、真实场源与植物成长、保护撤销后不继续成长 |
| 有界候选与诊断 | 最多16节点、前向边和1024字节保守说明上限；环、越界边、第二终结提案、未知操作/版本及超量输入有明确失败；产物拥有自己的指令与文本 |
| 宿主保留裁决 | 模型只选择 None/AtCell/AtDevice；调用者、程序版本、资源版本和作用域由 WorldWill 装配，GameSession 按当前状态裁决；更换表示不增加权限 |
| 持续执行与恢复 | 既有 Formula Process、ProcessPersistence、RegionActivity、Authority、InterventionCheckpoint 回归覆盖预算、挂起、冻结、存档与归属清理；这些能力属于生产 Formula/宿主，并非候选图能力 |
| 表达缺口 | “植物开花后停止”先缺授权植物观测和停止动作；跨请求等待、重新观测、依次尝试多个目标仍无公开过程合同，不能仅靠新语法补齐 |

实验的 ProbeModel 是可控的延后交付夹具，不启动模型线程；真实线程等待与取消由另行重跑的 WorldWillWorker 测试覆盖。有限图不含变量、等待、订阅、子进程、文件/网络或任意原生调用，也没有生产安装、持久状态、内容哈希或热迁移接口。

若后续出现真实的跨决策玩法，先定义观测、动作、时钟与所有权，再比较 WorldWill 局部状态机与独立有界过程。决定采用持久模块时，另行验证内容/能力/状态版本、每片工作和内存/队列预算、撤权覆盖全部后代，以及保存恢复不重放。当前模型服务未联网，不评价真实模型的决策质量。

## 模块与所有权

实验实现只在 [UnitTest/Experiments](../../UnitTest/Experiments/CMakeLists.txt) 的 `BUILD_TESTING` 分支构建。`PixelBehaviorDecisionExperiment` 仅链接 `PixelLLM`；测试执行器额外链接 `PixelWorldWill` 和 `PixelGameplay`，基准只链接实验库。生产模块没有链接或包含实验实现。

生产仍由 PseudoLLM 随机选择普通函数组合，经 DecisionAPI 形成一次提案。WorldWill 拥有请求及待交付结果，Session/Authority 负责当前权限、资源与租约；Formula 实例和 ProcessHost 负责过程与效果归属。实验确定性的援助策略没有替换默认随机兜底。

World 仅链接 PixelPerformance 与 Axiom Core；Formula Process 只依赖标准库与自己的宿主合同。没有向 World 或 Axiom 引入决策/游戏策略。引擎工作区无改动，子模块 HEAD、父仓库 gitlink 和 AxiomVersion.json 均为 `3676a9f4dfa7f64c056aeb5294b5c681c64411b0`。

## 本次运行证据

独立验证 agent 在 macOS/Darwin 27 arm64、AppleClang 21.0.0.21000334 下运行，CMake 实际使用 `/usr/bin/c++`，区块边长1024。下表时间为 CTest 汇总时间，基准时间为程序墙钟时间。

| 验证 | 结果 |
| --- | --- |
| runtime OFF、Debug 配置及相关目标构建 | 成功 |
| Debug 专项集合 | 16/16，3.02秒 |
| runtime OFF、Release 实验测试与基准构建 | 成功 |
| Release Pixel.BehaviorDecision | 1/1，0.16秒 |
| Release 基准连续三轮 | 每轮约0.41秒；校验值均为36736600 |
| 独立 BUILD_TESTING=OFF 配置及目标清单 | 配置成功；没有 PixelBehaviorDecision* 目标；未构建该配置全部目标 |

可重跑命令：

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpu --target PixelBehaviorDecisionTests PixelFormulaProcessTests PixelFormulaProcessStateTests PixelFormulaProcessFieldTests PixelFormulaProcessDemo PixelAuthorityTests PixelAuthorityViewTests PixelWorldWillTests PixelWorldWillWorkerTests PixelWorldWillCodecTests PixelPseudoLLMTests PixelModelProtocolTests PixelInterventionCheckpointTests PixelProcessPersistenceTests PixelRegionActivityTests -j4
ctest --test-dir build-cpu -R '^Pixel\.(BehaviorDecision|Formula\.Process(\.(State|Field|Hit|Rejected))?|Authority|AuthorityView|WorldWill|WorldWillWorker|WorldWillCodec|PseudoLLM|ModelProtocol|InterventionCheckpoint|ProcessPersistence|RegionActivity)$' --output-on-failure -j4

cmake -S . -B build-region-release -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-region-release --target PixelBehaviorDecisionTests PixelBehaviorDecisionBenchmark -j4
ctest --test-dir build-region-release -R '^Pixel\.BehaviorDecision$' --output-on-failure
for run in 1 2 3; do
  ./build-region-release/bin/Release/PixelBehaviorDecisionBenchmark > "/private/tmp/pixel-m6-acceptance-benchmark-${run}.csv"
done

cmake -S . -B /private/tmp/pixel-m6-no-tests -DBUILD_TESTING=OFF -DAXIOM_BUILD_UNIT_TESTS=OFF -DAXIOM_BUILD_RUNTIME=OFF
cmake --build /private/tmp/pixel-m6-no-tests --target help
```

Hit/Rejected 是调用 PixelFormulaProcessDemo 的 CTest 项，无同名独立测试目标。验证 agent 初次误用两个目标名导致构建命令失败，查明 CMake 定义并构建 Demo 后上述16项全部通过；未改测试或降低检查。

本次三轮完整原始数据另存为 [阶段复测 CSV](M6AcceptanceBenchmark-2026-10-24.csv)，保留 M6.1 初测数据。仍为每场景预热100次、2000批、每批32次的求值计时；P50/P95 是批均值分布，不是单次延迟分位，不含模型 I/O、编译、模拟或存档。

| 场景 | 原生 P50 / P95（微秒/次） | 决策图 P50 / P95（微秒/次） |
| --- | --- | --- |
| 4096格末项命中 | 1.540–1.555 / 1.719–1.888 | 1.521–1.526 / 1.656–1.694 |
| 4096格末项未知 | 1.542–1.543 / 1.603–1.721 | 1.520–1.522 / 1.622–1.702 |
| 64设备末项命中 | 0.0365–0.0391 / 0.0391–0.0404 | 0.0403 / 0.0404–0.0417 |
| 64设备均不匹配 | 0.0365–0.0378 / 0.0378–0.0404 | 0.0391–0.0403 / 0.0404–0.0417 |

各范围为三轮统计值的最小–最大值。输入保留量201990字节，图额外保留770字节；原生的0仅指没有独立运行时程序数据。微小差异受内联、缓存和固定测量顺序影响，不构成采用新语言的性能依据。复测支持继续暂缓的结论。

本机配置、构建、CTest、目标清单与基准日志位于 `/private/tmp/pixel-m6-acceptance-*`。本轮未执行全量CPU、chunk64、GPU/UI、AET、sanitizer或真实模型网络测试；M5和M6.1的历史结果仍归各自记录，不计作本次新验证。

## V2：独立审查

独立 review agent 已完成只读审查，未发现需要阻断 M6 收尾的正确性、权限或合同问题，无待修复意见。审查确认暂缓决定符合当前育圃场景与阶段条件，未将未来持久行为安装算作已交付。

实际检查包括两份设计正文、M6.1实验与本阶段变更、四个实验源码及CMake、LLM/DecisionAPI/PseudoLLM和提案协议、WorldWill、SessionAuthority、GardenAidProgram，以及ProcessHost清理和SessionPersistence恢复路径。核对了实例、弹体、事件及场源撤销，恢复时的权限/归属核验和Thinking/Ready转Interrupted；这些合同仍属于生产Formula/宿主。

Reviewer复核了本轮构建/CTest/基准/关闭测试目标清单、CSV统计与文档范围，并区分原有M5文档。没有另行构建或运行测试，不将只读代码检查算作额外运行结果。V1/V2均通过，本轮仅完成M6阶段；架构总任务的整体验收与历史Allocator任务仍待后续指令。
