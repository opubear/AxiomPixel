# 构式函数组合与世界绑定验证记录

日期：2026-10-26。验证对象为工作区中未提交的 formula/2 函数组合、formula/2 运行时来源作用域、Gameplay Actor 绑定和宿主 checkpoint 变更。检查时保留任务开始前已有的文档与 `Axiom` gitlink 修改；未修改游戏实现、测试预期、引擎子模块或 AWT 状态。

## 环境

- macOS 27.0，arm64（Apple Silicon）。
- CMake 4.4.0；AppleClang 21.0.0。
- 仓库 `build-cpu` 与独立 sanitizer 构建均为 Debug、Unix Makefiles。
- sanitizer 构建只配置 `Pixel/Source/Runtime/System/Formula` 标准库-only 模块及其 6 项独立 CTest，不链接 Gameplay、Axiom、World、窗口或 GPU。

## 实际执行与结果

仓库 CPU 全量构建及测试：

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
```

构建成功，所有目标完成（100%）。CTest 通过 94/94 项，耗时 99.96 秒；包括 `Pixel.FormulaGameplay`、`Pixel.FormulaBridge`、`Pixel.CompositionBinding`、`Pixel.FormulaRendering`、`Pixel.Formula.Composition` 与旧过程状态、持久化测试。

这是本轮后续小改动之前的 CPU 全量结果。随后增加的 checkpoint pinned 状态校验与回执 identity 容量统计没有重跑 94 项全量测试；对这两项改动的最新 CPU 验证见下文专项结果。

独立 ASan+UBSan Debug 构建及测试：

```sh
cmake -S Pixel/Source/Runtime/System/Formula -B /private/tmp/pixel-formula-upgrade-sanitize -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /private/tmp/pixel-formula-upgrade-sanitize -j4
ctest --test-dir /private/tmp/pixel-formula-upgrade-sanitize --output-on-failure
```

配置和构建成功；ASan 与 UBSan 下 6/6 项通过（Process、State、Field、Composition、Hit、Rejected），耗时 3.72 秒，未报告 sanitizer 错误。

复核新增 checkpoint 约束后，在同一 sanitizer 构建目录增量重建并再次运行同一完整独立套件：

```sh
cmake --build /private/tmp/pixel-formula-upgrade-sanitize -j4
ctest --test-dir /private/tmp/pixel-formula-upgrade-sanitize --output-on-failure
```

构建成功，6/6 项通过，耗时 3.62 秒，未报告 sanitizer 错误。新增用例分别反转挂起点的 `OriginPinned` 和作用域栈的 `Pinned` 标志，恢复均正确拒绝。

完成两项小改动后的仓库 CPU 增量构建和专项测试：

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure -R 'Formula|Composition|ProcessPersistence'
```

构建成功，专项测试 11/11 项通过，耗时 6.45 秒；包含 composition/runtime、Gameplay formula、Actor binding、rendering 和 process persistence。此结果覆盖最新验证代码；未据此将先前的 94/94 结果扩展到后续改动。

## 边界检查

抽查了组合解析、类型检查、描述求值及其到过程编译器的 lowering；源码 identity 与诊断位置映射；执行时 origin scope 栈的挂起、恢复与命中点固定；ProcessHost 对绑定 Actor 的解析、移动跟随、失效取消、活动区冻结与效果归属清理；以及 host v1/v2、instance schema v2/v3 的 checkpoint 读写边界。后续增量复核确认恢复会将 schema 3 快照中的当前固定标志和每层来源栈标志与对应挂起指令的编译期状态对照，拒绝改写命中定位语义的快照；回执统计也计入新增程序资源名和 SHA-256 字符串容量。最新的两项 checkpoint 反转用例、Formula 全套与专项 CPU 测试均已运行。

在这些检查范围和通过的测试中，没有找到可用具体执行路径证明的逻辑缺陷。旧 `process/1` 身份仍与 formula/2（semantics v2、host contract v3）分流；formula/2 的命中作用域快照与 Actor 稳定身份在宿主 checkpoint 中保存/恢复。

## 限制

ASan/UBSan 只覆盖独立 Formula 模块；Gameplay、World 和完整 Pixel 集成的 94 项 CPU 结果属于后续两项改动前的基线，最新核心改动由未启用 sanitizer 的 11 项专项测试覆盖。上述独立 subagent 未运行 AET、GPU 构建或 GPU smoke；主 agent 的结果单列如下。上述检查是针对本次变更的代码审查与指定测试，不代表对任意源码、存档或所有运行时交错的形式化证明。

## 主 agent 集成验证与成本补充

以下结果由主 agent 执行，不属于上述独立审查范围。最终构建与真实窗口检查：

```sh
cmake --build build --target Pixel Axiom PixelRuntimeSmoke PixelCloseSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --formula-only)
(cd Pixel && ../build/tests/PixelCloseSmoke)
```

目标构建均成功。后续存档校验和 UI smoke 补充后重新构建相应目标，最后再次链接 Pixel 与 Axiom，均成功。macOS 窗口检查需在允许访问窗口服务的环境运行。

- 构式 GPU：三弹体、三个承载者、黄色像素回读、槽位、单步、F1、resize、设备主体/端口和持续场安装移除的像素回读通过。
- 编辑器：实际输入 formula/2 函数源码，Compile/Apply、非法单位草稿、旧新实例版本隔离、Actor 附着/移动跟随/清理、右键捕获和 F2/F4 开关通过。移动 Actor 后核对已发出的弹体保持世界位置。
- 关闭：保存 pending/failed 拒绝、可见提示、带活动默认 formula/2 弹体的显式重试、保存恢复与仅释放资源的 GameEnd 通过。

首次窗口验证在 F4 编辑器打开断言失败，后续复查焦点并更新实际 formula/2 UI 用例后通过；未确定首次失败的根因。沙箱内第一次窗口启动未完成，已停止该测试进程并以允许窗口访问的方式执行。最终 GPU 和关闭命令均退出 0。日志明确报告 `VK_LAYER_KHRONOS_validation` 不可用，因此本轮没有 Vulkan validation layer 覆盖。未重跑完整 Pixel GPU 生命周期、育圃/祈祷全部 UI 或 AET 运行测试；AET 仅编译。

Release 成本使用 `PixelFormulaCompositionTests --benchmark`，原始结果保存于 [FormulaCompositionCost.txt](FormulaCompositionCost.txt)。三轮各500次固定示例编译为10.8—13.7微秒/次；包装与展开各2000次首片段测量，P50为0.208—0.250微秒，P95为0.209—0.250微秒，每轮两种形式相同，均17 work/22指令。不包含实例创建、世界提交或GPU，不能据此推算整场景吞吐。

本轮未修改引擎源码；保留原先已检出的 `Axiom` 子模块提交 `3676a9f4dfa7f64c056aeb5294b5c681c64411b0`，并将 `AxiomVersion.json` 同步到该提交。相邻 `../Axiom` 未进行修改或构建。
