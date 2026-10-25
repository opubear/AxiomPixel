# LLM抽象与随机PseudoLLM（M5.6插入需求）

2026-10-24，用户要求独立LLM类、没有实际模型时的PseudoLLM兜底，并将决策表示为共用API的函数组合。真实服务仍未接入，不需要网络、凭据或提供方配置。

## 所有权与边界

`Pixel/Source/Runtime/Gameplay/LLM/LLM.h` 定义抽象类 `pixel::llm::LLM`，统一 `TryStart / Poll / Cancel / Close / DisplayName` 生命周期，以及自有的输入、提案和回复值。`PixelGame` 以 `unique_ptr<LLM>` 持有实现；未传入时构造 `PseudoLLM`。`WorldWill` 借用LLM，只负责授权观测、祈祷状态和提案在游戏安全边界的执行。

模型类型从 `pixel::will` 移至 `pixel::llm`；旧ModelPort接口已删除。`PixelLLM` 只链接观测值库；`PixelModelProtocol` 只链接PixelLLM，JSON头为私有依赖。两者均不链接WorldWill、Gameplay、World或引擎运行时。游戏语义仍在Pixel，不将祈祷/援助规则搬入引擎。

`LLM/Testing/FakeModel` 保留可控worker、延迟、取消、重复和不可用回包，放入独立 `PixelFakeModel` 测试目标；它不再是游戏默认模型。已有异步测试继续使用它，避免把随机兜底和测试故障注入混在一起。

## 同一套可调用API

`DecisionAPI` 的 `Explain`、`NoAction`、`ProposeAtCell`、`ProposeAtDevice` 是当前双方共同的能力。多个Explain按调用顺序累积文本，随后必须恰好选择一次终结提案，最后Finish生成同一种ModelReply。内部重复终结、未选择就完成、超长说明等编程错误直接抛出，不静默修正。

真实模型将来返回受限 `pixel.proposal/1`；现有解析器先验证外部数据，再调用上述同一API。PseudoLLM则直接调用。该API仅构造提案，不持有Session、World、授权身份或当前动作序号；权限、程序和资源报价由WorldWill重新装配，GameAPI检查后才产生真实回执和效果。文本说明不构成已发生的领域事实。

当前合同是一组说明调用加一个游戏动作提案；尚不定义多动作事务或运行时行为DSL。增加新能力时，应同时扩展DecisionAPI、模型协议和游戏执行合同。

## 随机决策组合

`PseudoLLM.cpp` 用普通C++函数封装两条决策：

- `Wait`：说明随机选择等待 → 说明本次不干预 → NoAction。
- `OfferAid`：说明随机选择援助 → 在自有观测中找请求目标 → 说明拟议动作 → ProposeAtCell或ProposeAtDevice。目标未出现在观测中则说明原因并NoAction。

每次请求用均匀分布随机选一个完整函数，不根据祈祷语义推理或排序。目标查找只负责给已经选中的决策提供合法参数。以后可把新的函数组合加入同一目录；当前已有真实援助内容，因此无需用控制台打印代替动作。说明仍在祈祷UI中显示，并明确标识PseudoLLM。

默认以系统随机源初始化；测试可显式传入32位种子。同一实现、种子及输入序列可复现，不承诺不同标准库之间的随机序列一致，也不保存随机状态或恢复未完成请求。

决策仅扫描至多4096材料格/64设备并生成最多1024字节说明，在TryStart内完成有界本地计算；通过Poll交付，游戏动作仍等WorldWill服务边界。无需worker或模拟网络延迟。最多保留2条未领取回复；取消删除待领取回复，关闭清空且不可再启动。取消、暂停、超时、撤权和世界更替继续沿用WorldWill合同。

## 验证

`Pixel.PseudoLLM` 检查固定种子复现、两条决策和两种目标、函数调用顺序、与模型JSON协议的一致结果、不可见目标、容量、取消、关闭，以及真实GameSession中暂停不执行、恢复后真实援助、取消与撤权。运行时smoke核对默认模型名称，受控祈祷UI测试仍显式注入FakeModel。

2026-10-24，macOS，独立验证agent完成 `build-cpu` 构建和全量 **93/93**；chunk64 Release 的PseudoLLM、WorldWill、WorldWillWorker、ModelProtocol、InterventionCheckpoint、WorldWillCodec **6/6**。`Pixel`、`PixelRuntimeSmoke` 构建成功。初次新测试错误地从区域观测读取全局祈祷档案；改为CommittedWorldView并重建后，定向及完整回归均通过。

实际Apple M5/MoltenVK `--debug-ui-only` 已通过，包含默认PseudoLLM断言和原UI回归。首次 `--garden-aid-only` 暴露旧GPU夹具默认请求却固定要求Applied的随机失败；该固定援助/成长夹具现已显式注入FakeModel，保留真实场源、成长和像素断言。修复后仅重建受影响的PixelRuntimeSmoke，最终 `--garden-aid-only` **exit 0**，覆盖无煤援助成熟、GPU像素/帧槽、文本输入、Ready无动作、取消、保护撤销和冻结快照。

独立干净上下文review检查本次精确工作区基线后的改动，涵盖共享API、随机选择、生命周期、授权、CMake和默认接线，未发现阻塞代码问题；GPU夹具修复也已完成增量复核，无未决意见。71个文档本地链接和 `git diff --check` 通过。真实模型、网络、AET和完整sanitizer未验证；本机Vulkan validation layer不可用。
