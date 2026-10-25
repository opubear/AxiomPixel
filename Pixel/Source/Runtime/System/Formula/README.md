# 构式过程语言（process/1）

本模块提供独立C++20编译器、不可变`ProgramArtifact`、有界`Instance`运行时、源码草稿及fake host演示。不依赖Axiom、World、窗口或GPU。游戏通过Gameplay的`FormulaBridge`与`ProcessHost`接入真实世界；旧cast语言和树形Simulator已删除。

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B build-formula-system -DCMAKE_BUILD_TYPE=Debug
cmake --build build-formula-system -j4
ctest --test-dir build-formula-system --output-on-failure
./build-formula-system/PixelFormulaProcessDemo Pixel/Source/Runtime/System/Formula/Examples/Process/hit.formula --steps 12 --hit-at 3
```

独立测试包含Process、State、Field、Hit和Rejected共5项。游戏构建沿用`PixelFormulaProcess`目标。

```text
process 1;
emit bolt(10, speed(60), ticks(120));
await hit within ticks(121);
if outcome() == hit {
    emit dig(cells(2));
}
```

文件以`process 1;`开始。`let`定义不可变值，`var`定义可赋值局部；条件、循环和算术组成显式控制流。`cells`、`speed`、`acceleration`和`ticks`带单位检查。`emit`提交dig、wind、bolt、field、update_field或stop_field效果值；宿主另行校验领域权限、区域和资源。

`wait`和`await`结束当前片段，等待期间不阻塞线程；下一活动边界继续。`await result`、`await hit`和`await wake`均要求有限超时。每次emit的回执与载体终态按请求标识关联，早到事件可以缓存；每次等待仍占独立片段。程序结束、取消或故障会清理所属弹体和持续场源，已经完成的挖掘与风脉冲不回滚。因此发球后需要显式wait或await保持实例存活。

`field(cells(radius), acceleration(x), acceleration(y), ticks(lifetime))`表示持续加速度来源。`await result`后`effect_handle()`读取不透明句柄，跨等待使用需保存到local。`update_field(handle, field(...))`更新来源，`stop_field(handle)`停止来源；宿主负责归属和存活检查。参见`Examples/Process/field.formula`。

`ProgramRecord`保存源码和精确内容身份，`InstanceSnapshot`保存PC、局部值、剩余等待和事件水位；Import/Restore拒绝不支持的版本与身份不匹配。当前语义版本1，宿主合同和实例快照版本2。游戏还要保存宿主事件、归属效果和世界源，不能只恢复解释器。

| 目录 | 职责 |
| --- | --- |
| `Include/Formula/Process` | 编译、实例、宿主合同、状态和编辑API |
| `Source/Language` | 解析、类型检查、不可变产物和草稿 |
| `Source/Runtime` | 有界片段求值、等待和状态恢复 |
| `Tools` | 无引擎fake host与CLI |
| `Examples/Process` | 开采、命中、回执、持续场等可执行示例 |
| 仓库`UnitTest/Formula` | 独立语言与状态回归 |

完整语法、额度和错误合同见[过程运行时](../../../../../.awtcontext/GlobalDoc/FormulaProcessRuntime.md)；游戏适配见[FormulaBridge](../../../../../.awtcontext/GlobalDoc/FormulaBridge.md)，源码编辑与状态见[编辑合同](../../../../../.awtcontext/GlobalDoc/FormulaStateAndEditing.md)。
