# 简化 NS 风场

实现位于 `Pixel/Source/World/WindField.*`，由 Environment 持有；使用纯 CPU 标准库，不修改引擎或渲染层。完整参数和使用示例见工程根 README 的“简化风场”章节。

## 模型与更新

- 在周期边界的二维 MAC 网格上存储交错速度。默认 32×16 个网格覆盖顶部 1024×424 个世界格，正 Y 向下。
- 简化不可压缩 NS：`du/dt + (u·∇)u = -∇p + ν∇²u + λ(U-u) + βB(v_dirt-u)`。常密度归入压力变量，迭代投影近似满足散度为零。
- 每个固定 1/60 秒步执行背景风松弛、20 次隐式扩散迭代、80 次压力投影、半拉格朗日平流、再次投影，然后移动方块，最后统一施加泥土阻力反馈并再次投影。没有泥土或DirtDrag为0时跳过反馈。单帧补算最多 0.25 秒。
- 初始扰动由种子决定两组剪切波相位；之后按方程演化。默认背景风 `(90,0)` 格/秒、扰动幅度 60、黏性 8 格²/秒、松弛率 0.05/秒。
- 参考：[Jos Stam, Stable Fluids](https://www.dgp.toronto.edu/public_user/stam/reality/Research/pdf/ns.pdf)。

## 世界接口与语义

- `World::ConfigureWind(endRow, settings)` 重建覆盖顶部 `[0,endRow)` 的风，保留物体和基础 Force；普通 World 默认无风，PixelGame 显式开启。
- `World::GetWind().Sample(x,y)` 双线性采样，区域外返回零；`AddWindImpulse(x,y,radius,delta)` 增加周期平滑局部脉冲；`DisableWind()` 清除风。
- 方块使用 `60 * Force + sampledWind` 合成移动方向，每分量大于 30 取 +1、小于 -30 取 -1，否则取 0。继续遵守每步一个邻格、障碍/链/目标争用规则，不引入方块惯性。
- 风不改写 CellState::Force。WorldState 发布仍包含格子和基础方向，风归 Environment 所有；普通渲染映射物体类型，力场调试视图发布合成场颜色。
- GenerateTerrain 清除风和环境计时；PixelGame 启动/R 重置随后恢复相同种子的默认风。

## 泥土对风的反馈

`WindSettings::DirtDrag` 默认60/秒、范围0～1000，0关闭反向耦合。Environment仅记录成功移动的泥土及其最终位置和实际速度（每格/步对应60格/秒），被阻挡的移动不会产生虚假空气推进。完整移动批次结束后调用 `WindField::CoupleDirt`，期间不改变其他移动链采样的风。

`B` 是分轴的投影遮挡比例：横向统计一个风单元内被占据的行高并集，纵向统计列宽并集，并平均到交错网格面。高而薄的泥土堆因此可产生强横向阻力，不会被单元内的空白面积稀释；同一行/列重复泥土不会叠加迎风面积。像素跨风单元时按真实交叠长度分配，也适用于非整数或亚像素风网格。行占据计数依赖逐行扫描；列占据使用每个风网格行的世界列标记。

泥土实际速度仍按像素与风单元交叠面积平均，未移动的泥土速度为0。每个方向先计算 `m=max(0.001, exp(-βB·dt))`，然后 `u*=v_dirt+m·(u-v_dirt)`。压力阶段求解 `div(M·grad(p))=div(u*)`，最终 `u=u*-M·grad(p)`；阻力和压力梯度共享同一组面响应权重。最小响应保留有限渗透性，使密集/移动泥土不会令压力系统断开。每步根据当前世界重算占据；移除/填充/重置不留下旧阻力，已形成的扰动继续演化。

加权压力的固流耦合背景可参考 [Batty et al., A Fast Variational Framework for Accurate Solid-Fluid Coupling](https://www.cs.ubc.ca/labs/imager/tr/2007/Batty_VariationalFluids/)。这里采用游戏用的投影阻力与加权压力近似，并未实现该论文的切割单元或精确固体边界。窄土堆改进的前后测量见 `WindDirectionDiagnosis.md`。

## 高度边界与限制

游戏高度自底向上，存储行号自顶向下。风覆盖高度 `[600,1024)`，即 `y<424`；高度 `[512,600)` 对应 88 行稳定天空，地面仍从 `y=512` 开始。默认 Force 全部向下，旧固定随机场 API 保留但不用于默认游戏。

## 力场调试视图

按 F1 切换全世界方向着色，空格也可见。`World::GetFieldVelocity(x,y)` 与移动共用 Environment 的 `60 * Force + wind` 采样，取死区量化前的连续合成场。World 只提供数值，PixelWorldPresentation 将单位向量分量从 `[-1,1]` 映射到 `[0,1]`：`RGB=(0.5+0.5*dx,0.5+0.5*dy,0.5)`；零向量为灰色。固定蓝分量避免负方向被截断成黑色，颜色仅表达方向。

帧首生成独立 RGB8 快照，模式与数据一起发布；复用四张 R32UInt 数据纹理和两遍渲染。Draw 不读取实时 World/Wind。调试模式取消后处理调色，切回恢复；R 重置世界/视角但保留显示模式。仅在调试模式承担全世界采样和转换开销。使用说明及线性 RGB 方向表见 README 的“力场调试视图”。

泥土与风有双向反馈，但没有逐像素硬墙、精确固体边界、浮力或温度；粗网格允许残留穿流。方块仍采用离散移动，没有惯性，整体动量不保证守恒。粗网格只保留分轴遮挡，不能恢复全部像素几何；孤立像素的影响仍可能很小。周期边界仅用于求解风，方块不能绕过世界边界。压力为有限次迭代，高分辨率和极端长宽比不保证很小的残余散度。只有活动风区内的泥土参与反馈，默认地面不在风区内，原稳定天空区保留。F1仅显示方向，纯减速不一定改变颜色。

## 验证入口

`Pixel/Tests/PixelWindTests.cpp` 覆盖原求解与移动；`PixelDirtWindTests.cpp` 覆盖占据尺度、遮挡旋转对称性、1024世界窄高土堆的堆顶偏转/迎风减速、静止减速/偏转、实际位移反馈、被挡住的块、增删重置、关闭和输入校验。`Pixel/Tests/Pixel2DRuntimeSmoke.cpp` 对比泥土团和默认风下4格宽高土堆与空天空的风速/方向，并读回F1图像。各轮实际证据由AWT对应任务维护。

## 运行时调试面板

Pixel默认开启ImGui，Pixel Debug窗口的Environment分组提供Wind start height滑块（F2显隐）。高度以底部为原点，默认600，转换为endRow=worldHeight-height；0覆盖全世界，1024关闭风。可Ctrl+单击输入（macOS使用Command+单击），非法范围被夹紧。高度变化重建风速并保留材料/基础Force，禁用再启用保留当前风参数；R恢复默认600。面板还提供默认高度按钮和F1方向视图复选框。UI捕获鼠标/键盘，叠加在后处理之后；World不依赖ImGui。

PixelDebugUISmoke通过生产RenderSetup启动无3D场景的UI，测试实际平台点击/键盘事件、0/512/600/1024和越界高度、控件输入隔离、F1/F2/R、窗口resize与销毁。实际执行证据见对应AWT任务。
