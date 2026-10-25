# Core Allocator 与容器后端：Pixel 接入与验收

2026-10-24。父工程基线 `2918879`；本地 `Axiom/` 的 PixelMain HEAD、父仓库 gitlink 与 `AxiomVersion.json` 均为 `3676a9f4dfa7f64c056aeb5294b5c681c64411b0`。

AWT 历史任务“Core标准Allocator与容器后端选择”只有标题，尚未登记需求和验收；当前锁定引擎已经包含该能力（引擎历史 `93260da` 引入，后续已有修订与独立审查）。本轮按当前实现补齐任务合同、核对 Pixel 消费方式并重新验证。没有重新实现分配器、改变默认后端或迁移游戏容器，也没有修改引擎、生产代码或测试。

## 两个独立的选择

**存储算法**由末尾模板参数在编译期选择；**分配接口与传播规则**由 allocator 类型决定，具体资源由 allocator 实例携带。容器后端选择没有全局开关或运行时分派；PMR 的 memory_resource 则在运行时指定，并通过虚调用分配。

```cpp
#include "Core/Containers/Containers.h"

axiom::Array<int> pooled; // std::vector + axiom::Allocator<int>
axiom::Array<int, std::allocator<int>> system; // std::vector<int>
axiom::Array<int, axiom::Allocator<int>, axiom::custom::Array> custom;
axiom::custom::Array<int> sameCustom;

using Storage = axiom::Deque<int, axiom::Allocator<int, 64>, axiom::custom::Deque>;
axiom::Queue<int, Storage, axiom::custom::Queue> queue;
axiom::Ring<int, Storage, axiom::custom::Ring> ring(16);
```

| 场合 | 当前合同 |
| --- | --- |
| 默认 `axiom::Array/Map/...` | 使用标准库算法和 Axiom 池分配器；现有 Pixel 调用保持这个选择 |
| 显式 `std::allocator` 或 PMR/scoped allocator | 只改变资源与传播语义，不隐式改变容器算法 |
| 显式 `axiom::custom` | 选择对应自定义存储实现；使用前核对共同接口、复杂度与引用失效规则 |
| Queue/Stack/Ring | 存储类型和适配器模板分别指定；选择适配器不会改写显式存储类型 |
| 独立 Core 消费者 | `add_subdirectory(Axiom/Source/Core ...)` 后链接 `Axiom::Core`，自动带入 C++20、`AxiomCoreMemory` 及线程链接要求 |

11类自定义后端包括 Array、StaticArray、Deque、List、Map、Set、UnorderedMap、UnorderedSet、Queue、Stack 和 Ring。它们不是 STL 全接口替代品。尤其自定义 Deque 是可扩容循环数组，扩容会移动元素；Array/Deque 中间插入即使容量足够也可能分配并使全部引用失效。自定义 Ring 预留额外槽位，正常写入不再做容器分配，元素内部仍可能分配。完整参数、接口和异常保证以[引擎容器合同](../../Axiom/Source/Core/Containers/README.md)为准。

本轮不作“池分配器/自定义容器更快”的结论。后端替换需要具体调用方的语义与测量证据，不能从名字或已有基准程序推导性能收益。独立 Formula 继续使用自身标准库依赖，不为统一容器命名引入 Axiom。

## 分配器边界

`axiom::Allocator<T, Alignment>` 提供标准容器所需的类型、`allocate/deallocate`、`rebind`、相等比较和传播 traits。实际起始地址满足 `max(Alignment, alignof(T))`，元素间距仍为 `sizeof(T)`；Alignment 必须是非零二次幂。分配只取得存储并建立数组对象，不构造元素。

每次默认构造创建独立共享资源；复制、移动和 rebind 共享资源身份，移动后的 allocator 仍可使用。复制/移动赋值及 swap 的传播 traits 均为 true，`is_always_equal` 为 false。对要求相等 allocator 的节点转移，应由来源的 `get_allocator()` 构造接收者；不能把两个独立默认资源当作可互换。

资源按“总字节数、实际对齐”懒建固定块池，归还的块保留到最后一个资源拥有者销毁。容器 `clear()` 或换小容量不等于释放池缓存；容器复制和 `get_allocator()` 副本也可能延长资源寿命。共享资源内部有锁，容器自身仍需外部同步；内部池不直接提供线程安全或游戏生命周期策略。

`allocate(0)` 返回空指针，超量请求抛 `bad_array_new_length`，系统分配失败传播 `bad_alloc`。`deallocate` 为 noexcept，必须归还原始指针、元素数及相等资源；违约可能终止进程。既有失败清理负责未发布页面的资源安全，不把程序错误转为静默降级。本轮没有增加异常捕获或重试。详细规则见[内存合同](../../Axiom/Source/Core/Memory/README.md)与[实现](../../Axiom/Source/Core/Memory/Allocator.h)。

## Pixel 当前消费者

- [WorldChunk](../../Pixel/Source/Runtime/Simulation/World/WorldMap.h) 用 `axiom::Array<ChunkPayload, axiom::Allocator<ChunkPayload, storage::ChunkAlignment>>` 保存一个连续载荷，明确指定64 KiB起始对齐。每块拥有独立 allocator；块销毁后释放该资源缓存，未把所有区块接到长寿命全局池。
- [WorldMap/WorldStreamer](../../Pixel/Source/Runtime/Simulation/World/WorldStreamer.h) 的索引、请求和完成队列使用默认 Array/Map，即标准库后端；World 仍只依赖 Core 与 CPU 性能模块。
- [Environment::AppendScratch](../../Pixel/Source/Runtime/Simulation/World/Environment/Environment.cpp) 扩容先核算新旧容量共存，再创建独立资源、复制并交换；旧容器离开作用域后释放旧池。保持该资源边界才能避免每次增长留下旧尺寸池。
- [区块测试](../../UnitTest/World/PixelChunkTests.cpp) 直接检查真实载荷地址对齐与容量统计；Cpu、Persistence、WindDomains、Memory 回归覆盖这些容器的实际世界使用路径。组件容量统计不等于进程 RSS，也不覆盖池的全部管理开销，详见[内存统计口径](../../Docs/MemoryProfiling.md)及[世界预算](../../Docs/WorldRepresentation.md)。

通用分配器、存储算法和相关测试留在 Axiom；区块布局、预算和模拟策略留在 Pixel。当前任务没有修改子模块或其锁定提交，也没有在相邻 `../Axiom` 执行构建或写操作。

## 本轮验证

独立验证 agent 在 macOS/arm64、Apple clang 21.0.0（`/usr/bin/c++`）上完成下表。时间为最终落盘日志中的CTest汇总时间。历史[Core审查记录](../../Axiom/.awtcontext/GlobalDoc/CoreContainersMemoryReview-2026-10-05.md)仅用于定位已知合同及既有回归，不能算作本轮运行结果。

| 范围 | 结果 |
| --- | --- |
| 父工程 Debug/runtime OFF：CoreContainers、CustomContainers、Allocator、MemoryPool及Pixel消费专项 | 31/31通过，51.75秒；26项Core及5项Pixel测试 |
| 独立 Axiom/UnitTest，ASan/UBSan：Core与分配器/双后端专项 | 26/26通过，4.91秒，包含两项非法对齐编译拒绝；无sanitizer报告 |
| 仅链接 Axiom::Core 的外部 CMake 消费者 | Debug构建运行成功；默认/显式标准allocator与自定义Array/Map/Queue/Ring、64 KiB对齐断言通过；仅链接AxiomCoreMemory，无运行时或第三方库 |

相关已有测试覆盖默认/显式后端类型、STL与自定义差分、对齐与非法对齐编译拒绝、共享资源/传播/rebind、只可移动值与PMR/scoped、跨线程分配、逐次分配失败与最终拥有者释放。此前修复的容量增长、allocator-aware resize和嵌套关联容器复制能力已保留在 ReviewRegressions 中。

从父工程根复跑专项：

```sh
cmake -S . -B build-cpu -DAXIOM_BUILD_RUNTIME=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cpu --target AxiomCoreContainersTest AxiomCustomContainersTest AxiomCoreAllocatorTest AxiomAllocatorFailureTest AxiomMemoryPoolTest PixelTests PixelChunkTests PixelPersistenceTests PixelWindDomainTests PixelMemoryTests -j4
ctest --test-dir build-cpu -R '^(Axiom\.UnitTest\.(CoreContainers|CustomContainers\..*|Allocator\..*|MemoryPool)|Pixel\.(Cpu|Chunks|Persistence|WindDomains|Memory))$' --output-on-failure

cmake -S Axiom/UnitTest -B /private/tmp/pixel-core-allocator-sanitize -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build /private/tmp/pixel-core-allocator-sanitize --target AxiomCoreContainersTest AxiomCustomContainersTest AxiomCoreAllocatorTest AxiomAllocatorFailureTest AxiomMemoryPoolTest -j4
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1 ctest --test-dir /private/tmp/pixel-core-allocator-sanitize -R 'CustomContainers|CoreContainers|Allocator|MemoryPool' --output-on-failure
```

外部消费者的CMake只包含 `project(... LANGUAGES CXX)`、`add_subdirectory(<当前仓库>/Axiom/Source/Core axiom-core)`、可执行目标和 `target_link_libraries(... PRIVATE Axiom::Core)`；运行时断言核对实际内容和对齐。源码、Debug配置及verbose链接日志保存在本机 `/private/tmp/pixel-core-allocator-consumer/`；主要专项日志在 `/private/tmp/pixel-core-allocator-validation-main/` 与上述sanitizer目录。这些临时材料不是仓库构建依赖。验证agent未修改仓库文件。

本轮不重新运行 GPU/AET、全量游戏测试或性能基准：生产与引擎未变化，上一项架构验收的GPU/AET结果仍是其历史证据。ASan/UBSan不代表TSan、跨平台或其他标准库验证；本机LeakSanitizer不受支持，验证时明确关闭。

## 独立审查

独立subagent已核对Allocator、资源与池、公共别名、Core CMake、ownership及关键allocator-aware实现、既有专项回归，以及WorldChunk、WorldStreamer和AppendScratch，未确认代码缺陷。唯一P3文档意见指出原文把allocator资源也说成纯类型级选择并不准确；已区分编译期容器算法和运行时资源实例，明确PMR的虚调用，并经原审查者复核关闭。

最终31项、26项与独立消费者的日志、Debug断言、链接依赖及文档表述均已复核，无未决意见。审查者只读检查，未重复构建或运行；没有穷举全部容器内部、混合传播策略、其他平台/STL、TSan、GPU或性能。任务需求与V1/V2有当前证据支持，完成后按用户要求暂停。
