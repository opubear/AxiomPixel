# AxiomPixel 独立全量审查

**后续修复状态：两项缺陷均已修复；原 Astra 已复核最终代码与独立验证证据，确认没有未决审查意见。** [最终复核报告](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/MathFixes/AstraRecheck.md)。下方原始发现保留为历史依据。

审查日期：2026-10-05。审查者：独立 `gpt-6-astra` 子代理，推理强度 `xhigh`，`fork_turns=none`；从当前源码、声明、调用方、测试和工程正式说明形成判断，没有读取历史 CodeReview/WindDirectionDiagnosis 报告或继承实现会话的 findings。应用了 `/Users/opubear/.codex/skills/code-design-style/SKILL.md` 及 `references/collaboration-and-review.md`。这是代码审查，不是所有输入、设备和执行路径的形式化正确性证明。

本轮确认 **2 项缺陷：P2 × 1、P3 × 1**，均在引擎公开数学接口。未确认 P0/P1，也未确认当前 Pixel 泥土/风场/F1/高度 UI 实现存在新的功能缺陷。后者不等于已经完成所有真实 GPU、交互和跨平台验证。

## 确认缺陷（按严重度）

### 1. [P2] TVector 的成员 Cross 无法实例化，公开接口调用直接编译失败

- 精确位置：[Vector.h:46](/Users/opubear/src/AxiomPixel/Axiom/Source/Core/Math/Vector.h:46)，成员声明位于第 43 行。
- 触发条件：下游通过已声明的成员接口调用 `Vector3f{1,0,0}.Cross(Vector3f{0,1,0})`。不需要特殊数值、运行环境或 GPU。
- 影响：使用该成员接口的翻译单元无法编译。当前引擎调用方主要使用全局 `Cross(a,b)`，所以现有应用/CPU 测试编译成功不代表成员接口可用。
- 原因：成员体中的 `Cross(*this, v)` 首先查找到自身的单参数成员，遮蔽了后面声明的同名自由函数；编译器报告传入两个参数而函数只接受一个参数。
- 实测：`c++ -std=c++20 -fsyntax-only -I Axiom/Source /Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/vector-cross.cpp` 失败，报错指向 `Vector.h:46:29`，包含 `too many arguments to function call, expected single argument 'v', have 2 arguments`。主代理另行最小复现得到同一错误。
- 证据：[探针](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/vector-cross.cpp)、[原始编译输出](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/vector-cross.txt)。
- 修复方向：直接在成员中实现叉积，或者先声明自由函数并通过明确的全局限定调用它；仅在目前的位置盲加 `::` 仍需处理声明可见性。补一个会真正实例化成员的编译/数值检查，现有 `CpuTests.cpp` 只验证自由函数路径。

### 2. [P3] Quaternion 对大而有限的输入正规化时溢出，丢失合法旋转

- 精确位置：[quaternion.h:103](/Users/opubear/src/AxiomPixel/Axiom/Source/Core/Math/quaternion.h:103)，涉及第 103–112 行；平方和溢出的源头在 [quaternion.h:96](/Users/opubear/src/AxiomPixel/Axiom/Source/Core/Math/quaternion.h:96)。
- 触发条件：`Quaternion{1e20f, 0, 0, 1e20f}`。所有分量有限，其真实范数约 `1.414e20`，仍在 `float` 可表示范围内；它与 `{1,0,0,1}` 表示同一旋转。公开类型没有要求调用方只能提供单位四元数，转换函数本身会调用 `Normalize()`。
- 影响：`Normalize()` 返回零四元数，`RotationVector(+X)` 变成零向量；`ToMatrix3x3()`/`ToMatrix4x4()` 则把本应为绕 Z 轴 90° 的旋转静默变成单位矩阵。这个错误可以沿公开场景接口进入渲染：`SceneTransform::IsFinite()`（[SceneSnapshot.h:34](/Users/opubear/src/AxiomPixel/Axiom/Source/Scene/SceneSnapshot.h:34)）允许该输入，`CWorld::SetTransform()`（[World.cpp:34](/Users/opubear/src/AxiomPixel/Axiom/Source/GamePlay/EngineTest/World.cpp:34)）存储它，快照最终交给 `RenderScene::ModelMatrix()`（[RenderScene.cpp:16](/Users/opubear/src/AxiomPixel/Axiom/Source/Render/Scene/RenderScene.cpp:16)）调用 `ToMatrix3x3()`。
- 原因：先在 `float` 中平方求和得到 `infinity`，再取倒数得到 0，有限原分量乘以 0 后全部归零。
- 实测：探针通过真实 `CWorld::CreateMesh → SetTransform → ExportSnapshot`，比较两组等价输入，并调用生产的 `ToMatrix3x3()`：

  ```text
  scale=1 accepted=1 normalized norm=1 RotationVector(+X)≈0,1,0 ModelMatrix rotation(+X)≈0,1,0
  scale=1e+20 accepted=1 normalized norm=0 RotationVector(+X)=0,0,0 ModelMatrix rotation(+X)=1,0,0
  ```

- 证据：[探针](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/quaternion-normalize.cpp)、[原始输出](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/quaternion-normalize.txt)。该探针在增强为 World 调用链之前，直接数学版本也得到主代理独立复现。
- 严重度边界：这是已确认的公开接口数值错误。当前 AET 相机从 `Identity` 进行小量旋转，未发现正常游戏操作产生此尺度的路径；没有声称当前 Pixel 常规运行已被影响，因此标 P3。
- 修复方向：和现有 `TVector::NormalizeInPlace()` 一样先按最大绝对分量缩放，或使用不会在 float 平方阶段溢出的范数算法，再统一处理零值/非有限值策略。增加大有限四元数与等价单位四元数的向量/矩阵结果一致性检查。这里不把明确的 epsilon 小值策略单独当成第二个缺陷。

## 已核实的限制与排除项

- **非均匀缩放的默认法线变换是已明示限制，不计 finding。** `SceneTransform.Scale` 是三分量且 World 仅检查有限值；`RenderScene::ModelMatrix` 确实允许非均匀缩放，而 [Default.Vert.slang:38](/Users/opubear/src/AxiomPixel/Axiom/Shader/Default.Vert.slang:38) 直接用 model 变换法线。真实 World 快照加几何探针表明，`Scale=(2,1,1)` 时法线与曲面单位切线点积为 `0.6`（正确值为 0），当前 Lambert 光照为 `0.643925`，正确几何法线应得到 `0.331266`。但当前 [S22SampleGuide.md](/Users/opubear/src/AxiomPixel/Axiom/Content/Engine/DefaultAssets/S22SampleGuide.md) 明确说明法线变换适用于旋转和统一缩放，样例只提供平移。建议后续使公共接口的限制更明确，或扩展 inverse-transpose 支持；本轮不将已声明能力边界升级为 bug。[探针](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/nonuniform-normal.cpp)、[输出](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/nonuniform-normal.txt)。没有执行 GPU 法线像素测试。
- **AET 禁用 UI 的空指针候选不计 finding。** 引擎 `RenderSetup.EnableUI=false` 是有效通用能力，Pixel 也有相应守卫和测试；AET 样例自身依赖 UI。没有找到正式承诺 AET 样例在关掉 UI 后仍可运行，不能把引擎开关等同于每个样例的支持契约。
- **MSAA e1 resolve 候选已排除。** 不能假设合规普通颜色/深度 attachment 设备只支持 e1；Vulkan 的必需 limits 包含 1 和 4。普通 attachment 格式的 sampleCounts 要求为相应全局 mask 的超集，因此也不能直接断言所选颜色/深度格式比全局 mask 更少。进一步的 combined depth/stencil 猜测没有得到规范支持，也未构造可达失败。依据：[Required Limits](https://docs.vulkan.org/refpages/latest/refpages/source/Required_Limits.html)、[Supported Sample Counts](https://docs.vulkan.org/spec/latest/chapters/capabilities.html)；没有将这些已排除猜测计入缺陷数量。
- **风场模型限制不计 bug。** 当前实现是有限分辨率、周期边界、固定迭代次数的简化速度/压力解算；泥土使用覆盖率、阻力和实际位移反馈，不是完整的逐格无滑移固体流体求解器。逐格移动采用确定性处理顺序，有方向死区，不能仅凭与连续刚体/流体直觉不同判错。

## 原始需求与关键调用链

| 范围 | 本轮核对内容及证据边界 |
|---|---|
| 1024×1024 泥土/天空、逐格空间方向场 | `WorldState/World/Environment` 的尺寸、地形边界、空间力与材料分离、发布快照、固定步进、链式搬移、冲突/环/守恒均读源码并有通过的 CPU 测试；World 不引入 Render/ImGui/Vulkan。 |
| 简化 NS 风与泥土双向反馈 | `WindField` 的 staggered 采样、扩散、对流、压力投影、配置验证、覆盖率和移动速度反馈已追踪；Pixel CPU 测试验证有限性、方向改变、窄障碍和守恒。默认风下生产尺度窄堆角度 `>0.1` 的断言存在于 GPU smoke 源码，本轮未执行该窗口测试，不能将其当成本轮实测。 |
| F1 正负方向到 0～1 | 归一化、零向量灰色、RGB8 打包、frame 快照模式与 shader 解码一致；CPU 映射与模拟场颜色测试通过，真实 GPU 输出 smoke 已读但未运行。 |
| ImGui 从底部设风场高度 | 跟踪 PixelGame 控件到 `endRow=1024-height` 的转换、0/1024 边界、关闭/恢复、Reset 和 UI 捕获；`PixelDebugUISmoke` 对真实控件输入、上下界、地形不变和 resize 有覆盖，但本轮没有运行 ImGui 窗口。 |
| 分层与独立 AET | PixelGame/Render/shader/测试留在 Pixel；引擎有独立 AET 入口、World 快照、RenderSetup/RHI 注入；CPU header compile 目标通过。未修改独立引擎分支，当前为 PixelMain。 |
| 资源与同步 | 已追踪 GameEngine 初始化/回滚/End、FrameProtocol、两帧槽、按 swapchain image 的 present semaphore、旧 generation 延寿、上传失败资源保留、descriptor/ImGui slot 复用与退役；相关 CPU mock 通过。真实驱动和故障条件仍见未覆盖项。 |

## 覆盖清单

审查开始的基线为 238 个文件、20,519 行。扣除 8 个嵌入第三方/历史探针文件后，对基线中 230 个自有文件、约 19,983 行完成源码和上下游审查；计数包含声明、注释、空实现及测试，不是“每行已证明正确”的度量。逐文件路径、分类、行数、哈希见 [coverage.tsv](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/coverage.tsv)。另外检查了基线外的脚本、配置、旧 shader 源文本、构建集成和资源生成器。

| 模块 | 覆盖 |
|---|---|
| Pixel/Source | 全部 17 个源/头文件：启动、PixelGame、WorldState/World/Environment/WindField、WorldGridFrame、PixelWorldPresentation、PixelRender。 |
| Pixel/Shader、Pixel/Tests | 三个 Slang；九个测试/编译检查文件，包含全部 CPU、真实 GPU、Debug UI smoke 源码。 |
| Axiom 启动/基础 | main、Axiom、Engine、Config、Command、Core Math/Time、UniformName、全局空骨架。 |
| Axiom 资产 | AssetManager、描述符、路径边界、Assimp IO/几何导入、Ppm、SceneAssetStore、解析器骨架；资产错误保持旧场景的链路。 |
| Axiom 游戏/场景 | AET Game/World/PlayerMotion、Camera/Actor/StaticMesh 和 GameplayEgnine Components/GameObject；SceneSnapshot 与稳定 ID、事务替换、材质编辑。 |
| Axiom 输入/UI/平台 | GLFW callbacks/输入枚举、FrameInput、MousePointerState、InputManager、ImguiManager、FrameBufferCache、ImGui material/input 事件衔接。 |
| Axiom Render | Scene、RenderObject/GpuMesh、ViewPort、RenderSetup、Forward policy、pipeline/material manager 和接口；空置/旧 Triangle 骨架也已检查，不为其声明完整运行功能。 |
| Axiom HAL | RHI/FrameProtocol/raster resources、shader reflection/interface/library、Vulkan instance/device/physical device、swapchain/recreation/presentation state、command/buffer/texture/descriptors、pipeline、raster passes、scene/UI bindings、loader/factory/vertex layout。 |
| Axiom Shader/Tests | 六个运行时 Slang、未进入当前构建的 model.vert/model.frag；测试框架、全部八个当前测试/编译检查文件和 SPIR-V reflection fixture 文本。 |
| 构建/配置/辅助 | 根/Pixel/Axiom/UnitTest CMake，Axiom/cmake 全部，ThirdParty 和 MoltenVKSupport 的自有集成 CMake，CheckBuildTargets.sh/ShaderCompile.bat，FeatureShow 小样例，AxiomVersion/配置与资源 JSON、ignore/submodules/format 配置，solid-color PNG 生成器及资源约定。 |

第三方库调用与集成在审查范围内，第三方实现本身没有逐行审查。资源 fixture 由资产测试实际解析，PNG 占位图没有逐个做视觉审查。

## 实际验证

完整的实际命令、工作目录和结果见 [commands.md](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/commands.md)。

- 新建独立 `/tmp/axiompixel-astra-audit-cpu`，从当前工程配置 Debug CPU 模式、构建并运行 CTest：**5/5 通过，9.25 秒**。[日志](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/ctest-cpu.log)。
- 新建独立 `/tmp/axiompixel-astra-audit-sanitizers`，同样配置 Debug，并给编译/链接加入 AddressSanitizer 和 UndefinedBehaviorSanitizer：**5/5 通过，29.65 秒，日志未报告 sanitizer 错误**。[日志](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/ctest-sanitizers.log)。平台 AppleClang 21、C++20。这里不额外声称完成平台未启用的 LeakSanitizer 检查。
- 额外将 `MeshImportTests/AssetBoundaryTests/AssetManager/AssimpParser/SceneAssetStore/AssetLoader` 六个当前自有 `.cpp` 编译为独立 ASan+UBSan 探针，复用已有第三方 Assimp/spdlog 静态库与生成头，执行通过：`mesh import fixture passed`，退出码 0。[日志](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/mesh-import-sanitized.txt)。第三方库本身未重新插桩。
- `Cross` 负向编译探针确认声明接口不可用；Quaternion 和已知非均匀缩放限制分别保存了可重复运行的数值探针。
- 结束时重新计算基线全部 **238 个文件的 SHA-256，238/238 一致**。[核对结果](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/baseline-check.json)。当前根分支 main、嵌套引擎 PixelMain（HEAD `bfb26c97ff1cea01346e4c0442c93ee5b4c43fbe`）；审查的是其现有未提交工作区，不是仅 HEAD 或最新 diff。

## 未覆盖与剩余不确定性

- 没有运行真正的 Pixel/AET GPU 窗口、没有重启/终止/操作用户 PID 242，没有获取其画面或改动其场景。没有在本轮重新链接整个 runtime、重建 Slang 或重新生成所有 shader。GPU smoke/lifecycle 测试只完成源码审查，未将历史通过记录算作本轮通过。
- 因此真实 Vulkan validation、不同驱动/设备、present fence 扩展与 fallback 的长期交替路径、真实 device lost/OOM、最小化/焦点/HiDPI 的操作系统行为和 GPU 像素结果仍需设备验证。Mock 和静态推导不能替代这些覆盖。
- 排除逐行审查的基线文件是 `Axiom/Source/Core/Hash/murmur3/*`、`Axiom/Source/HAL/Vulkan/Test_Vulkan.{h,cpp}`、`Axiom/UnitTest/Legacy/*`；第三方 vendored 源码、构建产物、历史诊断文件/历史审查报告也未逐行审查。未扫描相邻只读参考引擎来代替当前源码。
- 没有进行广泛随机模糊测试、长期内存/性能压力测量、线程并发证明或 Windows/Linux 全新构建。当前接口主要依赖单线程游戏循环；不把未承诺的并发能力当成缺陷。
- 没有自动修复生产/测试代码、切分支、提交或写 AWT；主代理负责本次 AWT。CPU/资产测试自身会创建并清理短期路径边界 fixture；最终自有源码基线未变化。所有新增审查探针和报告位于 `/tmp`，没有在相邻 `/Users/opubear/src/Axiom` 或 `../AIWorkTask` 写入。

可读性与分层方面，Pixel 的 World/Environment 与显示转换之间、引擎资产/场景快照与 Render 之间都有明确的当前边界。主要维护成本集中在公共数学接口缺少全面实例化测试，以及引擎渲染层仍有 Vulkan 具体类型和历史空骨架；后两者已能从当前架构说明和代码辨认，本轮不以风格本身制造额外 findings。

## 主代理复核与交付

主代理独立复现了 Cross 编译失败和四元数大有限值归一化错误，复核调用路径后认可两项结论及严重度。复现材料保存在本任务 Artifacts；首次交付时只做审查，P2/P3 当时尚未修复；后续修复闭环见下。全报告和证据已从临时目录归档至工程 AWT 上下文，命令清单保留实际执行时的原路径，重新运行时可将探针前缀换为归档目录。

## 修复与复核闭环（2026-10-05）

用户授权修复或反驳后，两项证据均成立，直接修复，无需反驳。

| 原问题 | 最终处理 | 原 Astra 结论 |
|---|---|---|
| P2 Cross 成员无法编译 | 成员实现叉积，全局函数委托成员；补充 const float/double 实例化、右手规则、反交换、平行及零输入回归。 | 接受，关闭。 |
| P3 Quaternion 大有限值正规化溢出 | Norm 与 Normalize 共用 double 中间范数；正规化不经过可能溢出的 float 范数，保留 epsilon 小值策略。非有限正规化输入显式抛 invalid_argument，与向量/旋转构造接口一致。 | 接受，关闭，无新增异常契约问题。 |

新增回归在旧实现上先失败；修复后独立验证代理运行全部 CPU 和 ASan/UBSan，各5/5通过。Pixel/Axiom runtime 目标编译链接成功，原审查两个探针重编译运行通过，World 中 scale=1 与1e20的旋转结果一致。代码和验证/复核指纹完全一致；只修改两个数学头文件及CpuTests.cpp，保留已有其他改动。

原 Astra 最终确认：两项原意见均关闭，没有重要或其他待处理的审查意见。既有非均匀缩放等已明示能力限制不属于本轮剩余意见。未运行GPU窗口，审查者确认这不是关闭本轮CPU数学修复所必需的验证。

[最终复核与双方结论](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/MathFixes/AstraRecheck.md) · [独立验证命令及结果](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/MathFixes/Validation.md) · [本轮补丁](/Users/opubear/src/AxiomPixel/.awtcontext/Astra独立全代码审查Task/Artifacts/MathFixes/Fixes.patch)
