# 正常关闭请求与最终释放

M2.2 为本地 Axiom/PixelMain 增加最小应用钩子。窗口关闭按钮与游戏程序请求统一在帧间裁决，引擎没有存档、预算或 Deferred 类型。

## 接口与顺序

- 游戏主线程通过 `Axiom::RequestClose()` 请求正常退出。Pixel 游戏菜单中的 `Quit game` 与自动性能捕获结束、AET 的 Escape 均使用此入口。
- `MainLoop` 在 PollEvents 后、FrameBegin 前调用 `ProcessCloseRequest()`。帧内发起的请求先完成该帧，在下一边界裁决；Skip 后也会返回该边界处理请求。
- Platform 消费并清除程序请求与 GLFW 原生关闭标志，再调用应用的 `OnCloseRequested()`。边界前重复请求合并为一次；回调内新发起的请求保留到下一边界。
- 默认钩子返回 true。返回 false 只拒绝当前请求，保持输入、渲染、游戏与引擎服务；不会自动再次询问。应用需要新的请求才会再次裁决。
- 接受时 MainLoop 返回，Run 调用最终 Shutdown。嵌入宿主必须在帧间使用 ProcessCloseRequest 的返回值决定退出，不能忽略接受后继续模拟。

`OnCloseRequested` 是 Axiom 应用派生类上的 protected 虚函数，裁决时尚未断开回调或执行 GameEnd。`Shutdown` 与析构只释放资源，保留原有回调断开、GameEnd、GPU idle、应用资源、Engine/窗口释放顺序；不调用关闭钩子。直接 Shutdown 不属于可拒绝的正常退出入口。内部错误继续传播，不转换为关闭被拒绝。

## Pixel 接入范围

### Escape 游戏菜单（2026-11-11）

Escape不直接请求退出：背包展开时优先收起背包，否则打开居中的 `Game menu` 窗口。再次按Escape或点击 `Return to game` 关闭菜单；只有点击 `Quit game` 才调用正常关闭入口。按键repeat/release不切换菜单，菜单不改变会话的全局暂停状态。

菜单关闭时仍遵守引擎的ImGui输入捕获优先级：正在编辑调试面板文本时，第一次Escape由编辑控件消费并取消编辑，松开后再按Escape才进入上述背包／菜单流程。菜单打开后由内建UI接管输入，不受调试面板捕获阻挡。

`Gameplay/UI/GameMenuUI`持有可见状态、居中布局、按钮命中和300×196原生像素画布；与背包共用`CanvasPainter`矩形与位图文字，不使用ImGui构造。`Render/UI/CanvasOverlay`按帧槽冻结画布可见性和UIScale，复用既有UI shader在场景后处理之后叠加。菜单跟随UIScale缩放并限制在视口内，隐藏时不上传或绘制；即使关闭引擎调试UI仍可使用。

`GameInput`先处理菜单键鼠，再处理背包与世界；仅退出选项经宿主回调请求关闭。打开或关闭菜单时清除持续移动与待消费跳跃；菜单打开时拦截角色、相机、工具、背包和世界操作，左右键在窗口外也不穿透。点击以事件自带的窗口/帧缓冲尺寸换算，与绘制使用同一布局。Application在菜单打开时暂时隐藏开发面板并让内建UI接管输入，关闭后保留原F2/F4可见设置。

菜单退出仍经过下述关闭协商；拒绝后菜单保持打开并显示 `EXIT BLOCKED - TRY AGAIN`；玩家可以按Escape返回游戏查看保存错误详情，修复后再选择退出。原生窗口关闭按钮和性能捕获结束沿用直接发起关闭请求的行为。

### 会话关闭协商

当前默认Temporary会话在PixelApplication::OnCloseRequested直接接受退出，不保存进度。显式Persistent会话才调用SaveWorld，保存成功才接受正常退出；Deferred、存储失败或会话不可用均拒绝本次请求，显示结果并保留服务。M5接入后，GameEnd关闭WorldWill及模型工作并清理输入，不再保存。保存的Thinking/Ready记录在下次加载时变为Interrupted，不恢复旧推理或重放提案；保存与旧档转换见[保存边界](SessionSaveBoundary.md)。

父仓库 gitlink 与 AxiomVersion.json 锁定同一引擎提交；相邻 Axiom 参考仓库未改动。

## 验证入口

`Axiom.UnitTest.ApplicationClose` 是无窗口/GPU 初始化的模块联结测试，覆盖合并请求、拒绝后输入/绘制、无新请求不重试、钩子内再请求、默认接受、最终释放和内部错误传播。该目标需要启用 runtime 才可构建。

`AxiomCloseRequestSmoke` 在真实 GLFW/Vulkan 窗口通过原生 close flag 和程序入口双向验证拒绝、五帧输入/绘制、再次关闭和默认 AET 接受；同时运行既有 AxiomLifecycleSmoke 与 PixelRuntimeSmoke。命令见 README，具体结果和环境限制记录于本任务 AWT 日志。

`Pixel.Input`覆盖菜单开合、重复键、背包优先级、输入拦截和清理。`PixelCloseSmoke`使用真实窗口与生产PixelApplication钩子验证Escape开合、返回/退出按钮、无ImGui运行、真实Formula Editor文本控件的首次Escape捕获与后续菜单开合、开发面板可见状态恢复、Pending与保存失败的关闭拒绝、隐藏调试面板时的提示、显式菜单退出重试及GameEnd不重复写盘。

2026-11-11菜单实现阶段验证：Debug的Pixel、PixelCloseSmoke、PixelRuntimeSmoke构建通过；CPU的Input、Actions、InventoryUI、DisplayScale、ParallelFrame、ModuleBoundaries六项及`PixelTests --rendering-only`通过。macOS / Apple M5 / MoltenVK原生窗口中，完整PixelCloseSmoke及`PixelRuntimeSmoke --inventory-ui-only`通过；菜单分别在有/无ImGui环境下验证，并覆盖0.5、1、1.5、4倍UIScale的GPU像素合成与画面检查。关闭测试固定为平坦世界以避免随机地形阻挡旧施放目标，恢复时携带同一怪物目录。此阶段未跑全量CPU、其他GPU专项或AET。

2026-11-11联合审查补充：新增真实Formula Editor编辑捕获用例，重编后的完整`PixelCloseSmoke`及`--menu-only`均通过。Astra ultra复核、构建脚本修复及独立CPU/GPU验证范围见[联合审查记录](../../.awtcontext/GlobalDoc/CodeReview-FontsLocalizationMenu-2026-11-11.md)；本机Vulkan validation layer不可用。
