# 原生背包 UI

现状核对：2026-11-10。现行玩法摘要见[构式、资源与背包系统](SystemsFormulaItems.md)。

本次范围是游戏内 UI 基座与库存界面：左上角常驻七个快捷格，Tab在其下方展开五行七列库存，布局从左上角按视口缩放。原有背包装备设计仍是未来领域规则提案，通用装备、拖拽和格子持久化仍为提案；木棍独立实例、掉落拾取和统一手持使用见[手持物品合同](HandheldItems.md)。

## 交互与领域边界

2026-11-11起，快捷栏顶行显示同一已提交帧的季节、季内日期与时刻；无日历发布时保留 `QUICK SLOTS`。日历规则和调试入口见[昼夜与季节](WorldCalendar.md)，UI按显示分钟缓存，不每模拟步重画。

- 1–7或左键点击快捷格选择；展开后左键点击物品绑定到选中快捷格。同一资源可绑定多个快捷格，均引用同一实际数量。
- Tab每次press开合一次，repeat/release不重复切换。Escape优先关闭背包；背包关闭时开合[游戏菜单](../../Docs/Architecture/CloseRequests.md)，选择 `Quit game` 才退出。
- 快捷格选择会返回普通工具；原有元素笔刷移到Shift+1–4，F2入口保留。开包不暂停游戏，面板外仍接受世界输入。
- 面板内左右键（含空格、边框与说明区）均被消费，隐藏的背包区不截获点击。输入使用事件自带的窗口/帧缓冲尺寸换算，不套用之后的窗口尺寸。
- 读取GamePresentation已发布帧中玩家ContainerId对应库存；不通过UI查询或写入可变Inventory。缺少发布时显示空值，世界epoch/身份或容器替换时重置绑定和展开状态。
- 现行Inventory按十四类资源汇总数量，并保存最多21个独立木棍实例，容量仍为4096，每根木棍占1。35格为稳定展示槽位，七快捷格为引用，不能相加视为42个拥有槽。没有凭空生成物品；数量归零保留快捷引用，重新取得时恢复显示。
- 快捷选择决定左键使用的ItemRef：木棍按实例施放，方块共享放置实现。当前有可用物品的持有格及其背包格显示金色高亮；空手和全预留不高亮。新拾取木棍自动绑定并选中可用快捷格；选择和绑定仍不保存。

## 模块与绘制

`Gameplay/UI/InventoryUI` 管理状态、稳定槽位映射与布局命中；`InventoryPainter`复用`CanvasPainter`原生矩形与5×7位图字形，结合资源图标产生自持有RGBA像素画布，构建为PixelUI。画布固定396×466逻辑像素，关闭时背包区域透明；视口缩放与命中使用同一布局合同。

Application持有UI并将其注入System/IO/Input。Input先处理UI命中，再调用原有游戏动作。UI不依赖Input或Render；Simulation和Items无需知道界面存在。

`Render/UI/CanvasOverlay` 只负责上传与GPU绘制。Render.FrameBegin在世界快照发布后取得UI画布，Draw不读取可变游戏状态。每个in-flight帧槽持有独立纹理/上传资源与revision；未变的画布不重绘、不上传。缓存版本仅在完整帧录制成功后提交，失败仍重试上传。

顺序是世界、风、场景物体、角色、场景后处理、原生背包、原生ESC菜单、开发调试overlay（菜单打开时隐藏）。UI在present pass中使用自己的shader和sRGB纹理，透明像素discard，背包每帧一次UI draw，ESC菜单可见时另一次；游戏调色不影响UI。重建交换链时重建present pipeline/descriptor，恢复全屏viewport后再调用debug overlay。未使用ImGui或引擎UI draw list，无需修改Axiom子模块。

## 验证入口

```sh
cmake --build build-cpu --target PixelInventoryUITests PixelInputTests PixelTests -j4
ctest --test-dir build-cpu -R '^Pixel\.(InventoryUI|Input|Actions|ModuleBoundaries)$' --output-on-failure
build-cpu/bin/Debug/PixelTests --rendering-only
cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j4
(cd Pixel && PIXEL_UI_CAPTURE_DIR=/private/tmp/pixel-ui ../build/tests/PixelRuntimeSmoke --inventory-ui-only)
```

CPU检查真实数量投影、绑定/耗尽/重新取得、世界替换、空发布、35格和7快捷格、缩放命中、Tab重复事件、Escape、点击不穿透，以及后处理之后的绘制顺序、三帧槽缓存、录制失败重试和resize。GPU专项使用生产组合根，并将实际UI shader叠加到可读场景后处理目标，逐像素校验颜色、透明区和调色隔离；可导出开合PPM画面。具体运行结果见本任务AWT记录。
