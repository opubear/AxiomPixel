# 昼夜与季节系统

状态：现行实现。核对日期：2026-11-11。所属系统：[世界与生态](SystemsWorld.md)、[呈现](SystemsPresentation.md)、[会话与家园](SystemsHome.md)。

## 玩家规则与入口

- 新世界从第1年春季第1天08:00开始；一天为20分钟有效模拟时间，每季7天，春→夏→秋→冬循环，一年28天。日期从1显示。
- 快捷栏顶行持续显示季节、季内日期和24小时制时刻，例如 `Spring 1 / 08:00`；不必打开调试UI。年份、时段与日出日落时刻在F2面板中查看。
- 普通场景随时间平滑变亮、变暗，晨昏偏暖，夜间保留蓝色环境光。四季分别采用春绿、夏暖、秋金与冬冷色调；每季最后四分之一平滑过渡到下一季，跨年也连续。
- 季初春/夏/秋/冬的白昼分别为12/14/12/10小时，以正午为中心。日出、日落前后各半小时为平滑明暗过渡；季末日长随季节混合连续变化。
- F2 → `Day / night and seasons` 可调整时刻、跳转晨/午/昏/夜、选择季节、设置日长/季长/速度或重置日历。季节选择跳到当前年对应季节的第1天，保留日内时刻。

晨昏快捷按钮按目标时刻对应的季节混合求日出/日落，选择最接近的可表示刻度；同一天内目标不受点击前时刻影响，重复点击不漂移。中午/午夜分别为12:00/00:00；所有快捷按钮仅改变当天时刻。极短日长下按日历刻度量化，不要求无法表示的精确半亮度。

`Seconds per day` 接受1–86400秒，`Days per season`接受1–365天，`Calendar speed`接受1–3600倍。UI输入越界时限制在范围内；原生控制接口直接拒绝非法状态。改变日长保留当天进度；改变季长按原绝对日重新计算年月季。日历速度只影响日历、光照和季节，不改变角色、材料、设备、构式或生态的步速。

## 时间与模块合同

纯CPU规则在 `Runtime/Simulation/World/Environment/WorldCalendar`，不依赖Gameplay、Render、系统墙钟或相机。`WorldCalendarState` 是保存源：零起始绝对日 `Day`、当日刻度 `TickOfDay`、日长、季长与倍率。每正常模拟秒60刻度，一天为 `SecondsPerDay × 60` 刻度；整数除余实现跨日、跨多季和跨年，无逐日循环或浮点累计误差。负数存档、越界配置、日内刻度越界和溢出明确失败；失败恢复/推进不修改日历。

GameSession拥有日历实例，协调器的每个成功1/60秒固定步在环境、角色和玩法阶段完成后推进一次，再与同一Tick/Revision发布：

- 全局暂停冻结日历；单步推进一次。暂停时允许原生调试日历编辑，这属于显式设置，不是模拟推进。
- 材料预算等待、重试和不够一个固定步的帧不推进；渲染Skip不额外推进。帧率或系统时间不参与日历计算。
- `SetWorldCalendar` 复用原生SessionControl权限，仅Debug/Driver可调用；Input及WorldWill不获此能力。未就绪拒绝，PendingStep返回WaitingForBudget，排队世界动作/延后发布返回Deferred/NotReady；调用者显式重试。
- 有效编辑只增加一次Revision，Tick不变；相同值不产生提交。R/新世界/新育圃重置日历。
- 日历是整个会话的全局时间。角色、设备和材料的远区冻结仍按原活动区规则，返区读取当前日历，不补算离区或离线时间。

`CommittedWorldView::Calendar()` 提供同提交的自持有日历快照。Render从它生成 `PostProcessParameters`，并保留 `PublishedCalendar` 给UI。Draw不读活的GameSession；游戏和模拟worker之间仍遵守帧间join边界。

`WorldCalendar::TickAtPhase` 是无副作用的当天时段目标查询。季末日长依赖日内进度，不能直接用跳转前快照的日出/日落设置时间；查询通过目标快照计算时刻与日出/日落的差，在单调区间内二分整数刻度，并从相邻刻度选择最小差值。查找次数由日长上限约束，不使用无界迭代，也不改动日期或正常推进规则。

`Render/CalendarLighting` 将季节/昼夜乘色与用户已有的场景调色组合，不重传地形。既有后处理统一作用于背景、地形、角色和场景物体。原生UI和DebugUI在后处理后绘制；F1保持原方向诊断颜色。HUD只在显示到分钟的文字变化时更新像素画布，不因每个日历刻度重画。

## 保存与兼容

`world.meta` 写版本7，保留v6已有字段，在风配置后新增一行：

```text
Day TickOfDay SecondsPerDay DaysPerSeason TimeScale
```

五项全为十进制整数。v7检查负数、数值范围、缺失项与尾随内容；损坏文件报告存储错误，不静默回落到默认日历。日历与世界/Gameplay在同一个checkpoint世代捕获，保留精确刻度和自定义节奏。`PixelGameplay 6`、块格式与生成器版本均不变化。

v2–v6没有日历，导入时使用春季第1天08:00、20分钟/日、7天/季、1倍；保留原物理Tick与余量，不根据历史模拟时间反算日期。v6保留全局暂停，v2–v5仍按已有兼容规则忽略旧环境暂停。下一次保存升级world.meta为v7。

普通游戏仍为Temporary会话：启动与R都是新世界，退出不保存。持久化仅用于显式Persistent会话与测试；日历功能不改变产品的存档开关。

## 范围与后续边界

本轮交付时间、日长、光照、季节色调、UI和存档。尚未包含天气、温度/湿度随季节变化、降雪积雪、融冰、农作物季节生长、昼夜怪物出没、太阳/月亮实体、局部光源、洞穴遮光或离线推进。群落气候与生成材料继续使用原稳定规则；不能从画面颜色推导领域效果。游戏未来需要季节玩法时，应查询日历事实并定义对应领域的提交规则。

## 验证入口

```sh
cmake --build build-cpu -j4
ctest --test-dir build-cpu --output-on-failure
cmake --build build --target Pixel Axiom PixelRuntimeSmoke -j4
(cd Pixel && PIXEL_UI_CAPTURE_DIR=/private/tmp/pixel-calendar ../build/tests/PixelRuntimeSmoke --calendar-only)
(cd Pixel && ../build/tests/PixelRuntimeSmoke --calendar-ui-only)
```

`Pixel.Calendar` 覆盖整数跨日/季/年、批量与逐步等价、日长/晨昏/季节色调、非法/溢出不修改、同步及独立worker下暂停/单步/控制权限/重置、冻结呈现、HUD缓存、真实检查点续接及损坏日历拒绝。`Pixel.Simulation`覆盖真实预算等待期间的日历冻结和控制拒绝；`Pixel.SessionStore/Persistence`覆盖v2–v6导入与v7线格式。

GPU专项通过生产应用发布四季正午及春季晨昏夜晚，使用实际后处理shader逐像素核对场景乘色和UI隔离，并检查F1。截图可通过上述环境变量导出；实际执行结果另在本任务记录与下方带日期证据中报告。

## 2026-11-11验证记录

- macOS / Apple M5：默认Debug CPU构建成功；129项CTest中128项首轮通过，`GenerationSession`的旧档夹具因写死v6失败。修正为解析版本字段后该项复测通过；`Pixel.Calendar`追加排队控制拒绝用例后单独复测通过。`Persistence`同类旧档夹具也已修正，并在全套中通过。
- 64格区块Release重新配置并构建，`Pixel.Calendar / Simulation / SessionStore`三项全部通过，覆盖另一块尺度。
- `Pixel`、`Axiom`、`PixelRuntimeSmoke`构建成功；引擎子模块无改动。
- `--calendar-only`通过：四季正午、春季晨/昏/夜、生产帧与HUD逐像素合成、UI后处理隔离及F1；导出七张1600×1200 PPM。已查看春季正午/午夜、秋季/冬季正午的PNG转换图。
- `--calendar-ui-only`通过：真实原生鼠标/键盘操作时刻按钮、季节选择、日长、速度、时刻越界限制与重置。
- 原有`--fields-only`回归通过：风可视化、F1方向颜色、手动调色恢复、重置、动态风及材料阻挡；新增日历没有改变诊断颜色。
- `git diff --check`和AWT `check`通过；本轮涉及的10份系统/入口/合同共165个本地链接核查通过。
- GPU首次在沙箱内无法连接macOS窗口服务，终止该测试后以获准的沙箱外进程完成验证。环境没有`VK_LAYER_KHRONOS_validation`，因此上述结果不包含Vulkan验证层检查；未执行完整GPU生命周期、全平台或长时运行验收。

## 2026-11-11晨昏跳转修复与复核

独立Astra ultra审查发现：直接用跳转前的日长设置时刻，会使默认春季第7天午夜的Dusk仍落在Day，也会使1天季长的夏/冬末Dawn落在Day/Night。已新增`TickAtPhase`按目标时刻求解晨昏，UI统一使用该查询；正常推进、存档格式和默认节奏不变。

- 本轮CPU构建及`Calendar / Simulation / SimulationThread / ModuleBoundaries / SessionStore`五项通过。新增日历测试覆盖每季1/2/7/365天、每日1/7/1200/86400秒的全年日期，检查正确时段、误差、相邻刻度最优、无副作用、起始时刻无关与重复不漂移。
- Pixel和PixelRuntimeSmoke构建通过；原生`--calendar-ui-only`追加点击三个复现场景及重复按钮，全部通过。
- 同一Astra ultra审查者复核通过，无未决意见。独立执行`Pixel.Calendar`通过；534,360个查询覆盖全部1–365天季长与全年日期，最短日长逐刻穷举确认最近目标；最大日长/最快季节变化逐相邻刻度验证残差单调，另检查最大合法日期和速度。原三例Daylight为0.499891/0.5/0.5，均进入正确晨昏。
- `git diff --check`和AWT一致性检查通过。此修复未重跑完整129项CPU、64格构建及完整GPU场景/生命周期；以上述专项和独立边界验证为本轮证据。
