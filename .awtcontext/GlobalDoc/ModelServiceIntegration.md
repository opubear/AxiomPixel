# 可替换模型接口与祈祷交互（M5.5）

M5.4 基线已提交为 `95c826f`。用户于2026-10-24明确本轮“暂不接入模型，定义好接口即可”，因此交付范围是可替换模型端口、受限提案协议和祈祷交互。后续用户追加的[LLM与PseudoLLM](LLMAndPseudoLLM.md)已将运行默认值改为随机PseudoLLM，没有网络请求；真实服务、配置和凭据接入属于后续工作。

## 接口边界

- `PixelGame` 可接收一个由组合根创建的 `llm::LLM`，继续由 `WorldWill` 调度。未传实现时创建PseudoLLM；服务选择与凭据不会进入 Session、World 或存档。
- `LLM::DisplayName()` 供 UI 标示当前适配器，模型接口仍只接收有界、自有的 `ModelInput`，不能借用会话或可变领域对象。
- 玩家祈祷原文最多1024字节，使用合法UTF-8，允许换行和制表但拒绝其他控制字符。原文属于玩家数据，不授予新权限、目标范围或程序编辑能力。
- 设备家园的祈祷入口仍使用已批准的固定援助程序。文本输入是该次请求的描述，所有提案仍经过既有观察权限、程序、资源和设备前置条件检查。

## 交互和保存

祈祷档案分别展示玩家原文、模型说明和已提交动作的证据。Thinking 表示等待模型；Ready 表示收到有效提案但尚未执行；Applied 表示当时发生了真实动作，当前效果是否仍存在另看干预实例。拒绝、无动作、超时、取消和中断均不得显示成已产生援助。

Ready 只在完整提交边界发布一次。玩家可以显式取消 Thinking/Ready 请求，worker立即取消，准备中的模拟步不被修改；取消终态等待安全边界发布。迟到回包不能提交，旧世界祈祷ID也不能取消新世界请求。

`PixelWorldWill 2` 增加玩家原文、Ready和Cancelled，继续读取版本1。Thinking/Ready在加载后均转为Interrupted；不恢复worker、模型回包或自动重发。存档仍使用原有同世代检查点，不保存提供方配置或凭据。

## 受限结构化提案

`PixelModelProtocol` 是不含I/O的独立外围目标，定义 `pixel.model-observation/1` 输入与 `pixel.proposal/1` 输出。输入只编码传入的授权材料格、设备、观察提交、玩家原文、目标和已批准程序身份；未知材料用null，未获授权的库存字段不输出。大整数均编码为十进制字符串。

输出必须是一个JSON对象，恰好六个字符串字段：`schema`、`kind`、`x`、`y`、`deviceId`、`explanation`。`kind` 只能是 `none`、`at_cell`、`at_device`。说明最多1024字节，完整提案最多16KiB。数字须为对应64位类型可表示的规范十进制字符串，额外字段、重复字段、非字符串数字、Markdown包裹、越界和非法输出明确返回Invalid，不修补后执行。

解析器通过与PseudoLLM相同的DecisionAPI生成结果。解析结果始终绑定本地分配的决策ID，不能从模型文本读取caller、epoch、动作序号、程序源码或权限。协议解析只验证形状；作用范围及当前游戏条件仍由WorldWill和GameAPI在实际提交时裁决。

## 后续服务接入

后续接入实际服务时，需核对官方接口合同，在外围适配目标实现请求编码、受限响应解析、连接/响应期限、取消和关闭。真实服务的慢响应、断网、非法输出及连通检查不在本轮范围；没有供应商SDK、网络库或默认外部地址。

## 验证与审查

2026-10-24，macOS：独立验证agent完成CPU全量 **92/92**、chunk64 Release 的WorldWill/WorldWillWorker/InterventionCheckpoint/WorldWillCodec/ModelProtocol **5/5**，并成功构建 `Pixel` 与 `PixelRuntimeSmoke`。

独立UI agent在真实Apple M5/MoltenVK上运行 `--garden-aid-only` 与 `--debug-ui-only`，均exit 0；实际平台输入覆盖玩家原文、受控fake请求取消、组合根模型注入、Ready无已提交动作、恢复后援助及保护撤销，保留GPU像素读回与已有面板/设备/缩放回归。本机缺少Vulkan validation layer，没有以软件模拟替代GPU验证。

独立review发现满容量档案可能使合法读模型超过原128KiB：128字节世界标识、32条双1024字节文本及其他领域最大容量同时存在时，实际保留 **133160字节**。已把GameplayObservation及其引用的呈现预留预算调为192KiB，并增加按实际vector/string容量计量的最大组合回归。临时计量探针单独经ASan/UBSan检查；这不代表完整游戏通过sanitizer。

修复后的CPU定向回归 **5/5**、chunk64定向 **4/4** 通过，`Pixel`/`PixelRuntimeSmoke`重新构建成功，最大组合实测 **133160 / 196608字节**。独立增量review确认预算传递与测试覆盖，所有意见闭环。文档本地链接和 `git diff --check` 通过。本轮按用户确认的接口范围完成，M6未启动。
