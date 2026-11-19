# 最终色调映射（Tonemapping）

状态：现行实现合同。核对日期：2026-11-16。

## 作用时机与颜色空间

普通场景的顺序为世界／背景 → 风 → 场景物体／怪物 → 人物 → 环境与局部照明、用户乘色 → tonemapping → 原生背包／ESC菜单 → 开发UI → sRGB呈现编码。

`Render/Passes/PostProcessPass`使用既有最终全屏Pass。`Pixel2DPostProcess.Frag.slang`读取场景并乘以`ColorMultiplier.rgb`。有局部光时，`Pixel2DLightPostProcess.Frag.slang`会额外采样光照图，再使用同一条ACES近似曲线。计算在线性RGB空间，sRGB呈现目标执行显示编码，输出alpha固定为1。局部照明的上传与混合见[火把与局部光照](LocalLighting.md)。

曲线逐通道计算，`x = clamp(scene * multiplier, 0, 16)`：

```text
mapped = clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0, 1)
```

输入上界16用于避免多项式溢出，此时曲线已饱和为白色；负值映射为0。线性0保持0，0.18约为0.267，1约为0.804；高亮在最终写入前压缩。普通场景默认启用，重置显示状态仍启用；没有自动曝光、曝光配置或持久化状态。

## 参数与隔离

`PostProcessParameters`保持64字节push constant ABI：`Values[0..3]`是已有乘色，`Values[4]`是`TonemappingBypass`，`Values[5..15]`保留。shader在旁路值大于0.5时只执行已有乘色。

最终Pass复制已发布的参数，根据冻结的`DisplayMode`设置旁路：普通Materials为0，ForceField为1，不修改已发布帧。F1继续使用精确方向到RGB的映射。原生UI与开发UI在曲线之后绘制，颜色不受映射影响。测试将已映射图像再次呈现时必须显式旁路，避免二次映射及影响UI。

窗口重建复用既有后处理pipeline／descriptor重建路径，各帧槽的场景输入选择和资源生命周期不变。实现及shader属于Pixel，不修改引擎或World。

## 当前范围与验证入口

场景中间目标仍为线性`RGBA8UNorm`，源美术通过`RGBA8SRGB`采样转换为线性。最终曲线可处理乘色后大于1的值，但不能恢复中间目标已截断的高亮；本次没有引入浮点HDR场景缓冲。

```sh
cmake --build build-cpu --target PixelTests -j4
ctest --test-dir build-cpu -R '^Pixel\.(Cpu|ModuleBoundaries|ModuleBoundaryFixtures)$' --output-on-failure
cmake --build build --target Pixel PixelRuntimeSmoke -j4
(cd Pixel && ../build/tests/PixelRuntimeSmoke --tonemapping-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke --inventory-ui-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke --calendar-only)
```

CPU/mock覆盖三帧槽、resize、F1切换及退出后恢复映射，并检查发布帧不被修改和UI顺序。GPU专项使用实际shader读回RGB8渐变，检查默认曲线、乘色顺序、黑白、高亮、负值、显式旁路与不透明输出；UI和日历专项逐像素核对场景曲线与UI隔离。本轮实际执行结果写入任务记录，不以测试入口存在表示通过。
