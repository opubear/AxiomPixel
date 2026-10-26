# 构式语言：formula/2 与 process/1

独立 C++20 编译器、不可变 `ProgramArtifact`、有界 `Instance`、源码草稿及 fake-host CLI。不依赖 Axiom、World、窗口或 GPU；游戏通过 Gameplay/FormulaBridge 提供世界上下文、权限与效果提交。

新程序使用不可变函数组合：

```text
formula 2;
fn shot(velocity: speed, impact: action) -> action =
    on_hit(bolt(10, velocity, ticks(90)), impact);
let mining = dig(cells(2));
run repeat(3, ticks(30), shot(speed(60), mining));
```

`fn` 的参数和返回值有类型，定义先于引用；`let` 绑定不可变值，唯一 `run` 指定根描述。基础函数与用户函数按同一规则调用，返回的描述可以继续传参、封装和组合。编译或构造描述不执行世界操作；每次激活拥有独立状态。函数展开只发生在编译阶段。

类型为 number/cells/speed/acceleration/ticks/action。基础作用为 dig、wind、bolt、field；组合为 sequence、delay、repeat、on_hit、if_credits、on_wake、nothing。纯数字计算使用 add/multiply，单位构造使用 cells/speed/acceleration/ticks。没有递归、可变赋值、并行散射或外部系统操作。

普通作用等待准入结果，拒绝停止根；bolt 等待命中/到期，field 保持有限寿命。on_hit 子描述固定命中点，完成后恢复外层空间上下文。活动步、预算、冻结/暂停与事件水位都由现有过程执行核心管理。元信息不写入 DSL：Gameplay 装配 World 或 Actor 绑定、方向、独立来源与授权；已发射弹体和已安装场源保持世界定位。

完整函数表、精确时间语义、额度、绑定失效和版本策略见[formula/2 合同](../../../../../Docs/Architecture/FormulaComposition.md)。

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B build-formula-system -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula-system -j4
ctest --test-dir build-formula-system --output-on-failure
./build-formula-system/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Composition/mining.formula --steps 12 --hit-at 3
```

独立测试共6项：Process、State、Field、Composition、Hit、Rejected。`CompositionTests` 检查封装/展开请求轨迹、类型/作用域、跨实例隔离、命中上下文与保存、拒绝、预算及畸形输入；传 `--benchmark` 可测量编译和首片段成本。

## 旧程序和保存

`process 1;` 仍按原语义编译与恢复：let/var、emit、显式 wait/await 和条件循环不变。旧示例位于 `Examples/Process`；field 的显式 effect_handle/update_field/stop_field 接口仍可读取，旧 cast 已删除。

ProgramRecord 保存原始源码和完整内容身份。process/1 保持语义1/宿主2/实例格式2，formula/2 使用语义2/宿主3/实例格式3，额外保存命中上下文栈。导入重新编译后核对身份；恢复必须使用原产物，未知版本拒绝，不自动重启、不重播。宿主必须同时保存事件、归属弹体和场源；仅恢复解释器不构成完整游戏恢复。

| 目录 | 职责 |
|---|---|
| `Include/Formula/Process` | 共用编译、产物、实例、宿主和状态 API |
| `Source/Language` | 函数组合前端、过程编译、内容身份和草稿 |
| `Source/Runtime` | 有界执行、事件、空间作用域和 continuation 恢复 |
| `Tools` | 无引擎 fake host 与 CLI |
| `Examples/Composition` | 新版开采弹与有限周期推力示例 |
| `Examples/Process` | 旧过程语法及句柄操作样例 |
| 仓库 `UnitTest/Formula` | 独立语言、组合、状态回归 |

游戏集成与历史过程合同见 [FormulaBridge](../../../../../.awtcontext/GlobalDoc/FormulaBridge.md)、[过程执行核心](../../../../../.awtcontext/GlobalDoc/FormulaProcessRuntime.md)、[状态与编辑](../../../../../.awtcontext/GlobalDoc/FormulaStateAndEditing.md)。这些历史文档中的旧测试结果不替代[本次验证](../../../../../Docs/Architecture/FormulaCompositionVerification.md)。
