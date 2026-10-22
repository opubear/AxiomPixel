# 正常关闭请求与最终释放

M2.2 为本地 Axiom/PixelMain 增加最小应用钩子。窗口关闭按钮与游戏程序请求统一在帧间裁决，引擎没有存档、预算或 Deferred 类型。

## 接口与顺序

- 游戏主线程通过 `Axiom::RequestClose()` 请求正常退出。Pixel 的 Escape 与自动性能捕获结束、AET 的 Escape 均使用此入口。
- `MainLoop` 在 PollEvents 后、FrameBegin 前调用 `ProcessCloseRequest()`。帧内发起的请求先完成该帧，在下一边界裁决；Skip 后也会返回该边界处理请求。
- Platform 消费并清除程序请求与 GLFW 原生关闭标志，再调用应用的 `OnCloseRequested()`。边界前重复请求合并为一次；回调内新发起的请求保留到下一边界。
- 默认钩子返回 true。返回 false 只拒绝当前请求，保持输入、渲染、游戏与引擎服务；不会自动再次询问。应用需要新的请求才会再次裁决。
- 接受时 MainLoop 返回，Run 调用最终 Shutdown。嵌入宿主必须在帧间使用 ProcessCloseRequest 的返回值决定退出，不能忽略接受后继续模拟。

`OnCloseRequested` 是 Axiom 应用派生类上的 protected 虚函数，裁决时尚未断开回调或执行 GameEnd。`Shutdown` 与析构只释放资源，保留原有回调断开、GameEnd、GPU idle、应用资源、Engine/窗口释放顺序；不调用关闭钩子。直接 Shutdown 不属于可拒绝的正常退出入口。内部错误继续传播，不转换为关闭被拒绝。

## Pixel 接入范围

PixelApplication 当前沿用默认接受；Session.End/GameEnd 保存语义保持原状。M2.3 再把可能失败或延期的保存决定移入关闭钩子，并迁移 v4 deferred 导入与正常退出/Reset 策略。本任务没有提前实现该行为。

父仓库 gitlink 与 AxiomVersion.json 锁定同一引擎提交；相邻 Axiom 参考仓库未改动。

## 验证入口

`Axiom.UnitTest.ApplicationClose` 是无窗口/GPU 初始化的模块联结测试，覆盖合并请求、拒绝后输入/绘制、无新请求不重试、钩子内再请求、默认接受、最终释放和内部错误传播。该目标需要启用 runtime 才可构建。

`AxiomCloseRequestSmoke` 在真实 GLFW/Vulkan 窗口通过原生 close flag 和程序入口双向验证拒绝、五帧输入/绘制、再次关闭和默认 AET 接受；同时运行既有 AxiomLifecycleSmoke 与 PixelRuntimeSmoke。命令见 README，具体结果和环境限制记录于本任务 AWT 日志。
