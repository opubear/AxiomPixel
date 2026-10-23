# 发现知识、重要历史与派生记忆（M5.1）

M5.1 为世界意志建立可解释的背景输入；区域控制见[Authority](Authority.md)，现已接通的离线祈祷及存档见[持久援助](PersistentInterventions.md)。世界已提交结果、玩家学到的内容和模型生成的解释分别归属，互不替代。

| 数据 | 所有者与入口 | 含义 |
| --- | --- | --- |
| DomainFact | Session的RecordFact；256条短期环 | 已发生的实际结果，包含请求/程序/设备原因和完整提交版本 |
| HistoryRecord | Knowledge/Journal；最近128条重要记录 | 选取真实开采、设备放置/回收/补给/启停、首次显式力场观察、程序激活及控制权变化；保留原始事实和版本 |
| Discovery | Knowledge/Journal；最多128个玩家与位置组合 | 玩家曾观测此处合成加速度；首次证据不变，不代表当前读数或完整理解 |
| DerivedMemory | LLMView/DerivedMemoryStore；最多16条 | 模型对已交付历史的非权威解释，强制带来源并标记model_derived |

没有记录逐格材料运动、每步设备状态、零变化挖掘效果或虚构居民/温湿度/远古事件。短期环和重要档案是两个不同的保留窗口；重要档案不承诺无限地方史。

## 明确的玩家发现动作

`api::ObserveForce{Target}` 通过现有GameSession动作入口执行。目标必须在活动域内且驻留，距玩家锚点不超过32格。成功时读取实际 `World::GetForce`，返回 `ForceObserved`，单位为格/秒²；没有把加速度解释为空气速度。

观察消耗调用者序号，在一个完整提交中产生事实与知识。相同请求返回原回执；同一玩家重复观测同一位置可以得到新读数和短期事实，但不重复创建发现或重要历史。发现达到128项后，新的位置返回LimitExceeded，既有位置仍可观测。预算等待期间返回WaitingForBudget，不读取准备中的环境。

观察仅设置Observed；Analyzed、CanCopy、CanEdit均为false，也不授予设备或区域控制。解析/解锁机制留待后续玩法。普通Observe/CaptureObservation、LLMView查询和调试绘制不会自动学习。育圃现有“Apply crystal program”和源码面板仍是原生Debug示例权限，不由发现记录授权。

本轮提供CPU动作及查询合同，尚未增加玩家知识面板或观测快捷键。可执行场景在Pixel.Knowledge测试中：创建力场育圃→Input观测左侧晶体→得到实际水平3600样本→开采4石2铜→放置设备→查询带原因的历史。

```cpp
session.Control(NativeCaller::Driver, api::CreateForceGarden{});
auto measurement = session.Execute(NativeCaller::Input, api::ObserveForce{{480, 508}});
auto snapshot = session.Observe(NativeCaller::Input, {{464, 496}, 64, 32});
// 上述坐标用于默认有限Flat测试场景；自然生成场景使用实际地表坐标。
```

## 提交、身份与查询

Session每次追加DomainFact时直接交给Journal，再发布读模型，避免一个批次超过短期环容量时漏掉前面的重要事实。已消费来源序号按重复供给忽略；新来源漏序、版本/tick倒退或跨世界供给立即报错，没有异常吞掉或半提交恢复。记录从提交路径产生，模型没有RecordFact或Journal写入口。

重要记录身份为稳定WorldId加History.Sequence，与短期FactId、数组槽和运行epoch分开。保留的原始FactId只标识其原运行；跨运行引用用历史身份及其原始CommitStamp。有限CPU世界也使用不同WorldId，避免多会话引用碰撞。

Knowledge和History是新增LLMView主题。视图冻结知识及历史副本，与其他领域保持同一CommitStamp；后来的动作不会混入旧页。Knowledge按主题、主体epoch/actor和区域过滤，再计数分页。需要独立AllKnowledge或KnowledgeOwner授权，库存权限不代替知识权限。

首版History是全局档案，不按Query.Scope筛历史；需History主题、Facts、AllInventories、AllKnowledge且没有Region限制。未授权主体看不到该能力或历史序号/保留统计。仅能读取自己发现的主体，其EvidenceSequence在副本中隐去，JSON返回null和permission_denied，避免从全局序号推算其他隐藏事件数。原始发现中的引用不改动。

`ReadHistory(observer, view, after, limit)` 固定读取该view的上界，最多128条。请求落在已淘汰区间时返回Gap和FirstSequence，不悄悄跳过；完整Query则标记partial/history_retention_gap。来源被淘汰后，发现仍然存在，引用解析必须承认缺失。所有Query/ReadHistory准备结果都在Deliver再次核验授权；撤权使旧页、游标和待交付结果失效。

历史引用、身份、坐标和版本在JSON中用字符串保留64位精度。Temperature与Ecology的LLM主题继续unsupported，不因为新增历史而生成这些领域的假数据。

## 派生摘要

`DerivedMemoryStore::Remember(deliveredResponse, text, sources)` 只从成功交付的历史响应选择来源。内部凭据由ViewService生成并计入预算，公开构造Response或修改其公开payload不能伪造证据。引用包括WorldId、历史序号、原FactId和原CommitStamp。

每条文本最多1024字节、1–8个不重复来源；总计最多16条和128KiB。没有来源、来源不在交付结果中或超限会明确拒绝。该验证只证明模型读过所引事实，不证明其解释正确。摘要没有Journal、动作入口或程序源码引用，不能增加事实、知识或权限。已交付数据形成的记忆不因之后撤权而假装被收回；后续模型动作仍须由当前Authority动作权限裁决。

## 保存DTO与当前边界

`knowledge::SaveData`版本1提供拥有数据的Export/Restore；Session提供CaptureKnowledgeData。记录、发现、NextSequence和来源消费水位都在DTO中，修改导出副本不能回写权威数据。

Restore校验版本、数量、连续保留后缀、发现去重与证据、当前运行来源顺序以及revision/tick/sequence水位。RuntimeFirstSequence分隔当前运行历史后缀；BeginRuntime即使收到重用的epoch数值也清空来源消费水位，历史身份继续增长。内部Capture不能把未来来源放进较早截面。

M5.4已通过 `PixelKnowledge 1` 接入 `PixelGameplay 3` 的同提交磁盘检查点。恢复保留稳定历史/发现身份，验证WorldId与提交上界，再重建运行来源消费边界。`ProgramActivated`也进入重要档案，为持久祈祷结果提供稳定证据；模型说明另存，不成为事实。下文M5.1记录仍是历史DTO验证范围，新增磁盘恢复与故障验证见[持久援助](PersistentInterventions.md)。新建世界Reset仍清空这些状态。

## 验证

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -j4
ctest --test-dir build-cpu -R '^Pixel\.(Knowledge|KnowledgeView|LLMView|LLMJson)$' --output-on-failure
```

2026-10-23最终验证由独立验证subagent执行：

- `cmake --build build-cpu -j4` 成功；完整CTest **84/84通过**，约45秒。新增Pixel.Knowledge和Pixel.KnowledgeView覆盖实际开采/设备/力场观察、动作及来源去重、同提交发布、128条淘汰缺口、owner/区域过滤、撤权、冻结页、摘要来源/大小、64位JSON与保存DTO恢复校验。
- 真实streaming预算等待夹具验证观测拒绝、知识水位不变、缓存快照与最后完整提交一致；释放预算后只完成一个步骤，随后新的位置观测恰好增加一次发现。测试基线取在夹具最后一次合法提交之后。
- `build-chunk64`以Release、`PIXEL_CHUNK_EDGE=64`构建，Pixel.Knowledge、Pixel.KnowledgeView、Pixel.Observation、Pixel.LLMView、Pixel.LLMJson **5/5通过**。
- `cmake --build build --target Pixel -j4` 成功，确认新增动作/事实值没有破坏Debug和运行时编译。
- 独立强模型subagent审查并复核最终实现。发现的全局历史序号隐私泄漏已通过授权副本中的证据引用脱敏修复；保存DTO消费水位缺口已通过运行边界与来源顺序、版本/tick交叉校验修复。相应回归通过，无未决重要审查意见。

旧LLMJson用例已将Knowledge由unsupported改为可用的真实空集合，Temperature/Ecology仍检查unsupported；LLMView预算用例改为寻找实际DTO容量边界，继续验证可打开快照而查询响应超限的行为，没有扩大生产预算。

本轮没有修改World、Formula或Axiom，也没有新增UI/GPU行为；未运行GPU smoke、AET、sanitizer或新的性能基准。运行时构建的既有CPM缓存路径/Bullet最低版本警告未阻断构建。保存验证仅为DTO往返和校验，不是磁盘保存重启。

M5.2将AuthorityChanged和InterventionStopped纳入重要档案，原始来源为实际请求或Authority版本；保护变化、当前能力及撤销/租约到期原因可以追溯。M5.4已持久化这些记录。
