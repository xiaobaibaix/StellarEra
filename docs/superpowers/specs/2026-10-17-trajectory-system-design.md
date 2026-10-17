# 星球轨迹系统 — 设计

- **日期**：2026-10-17
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：已实现
- **前置**：`2026-10-17-nbody-initial-state-from-level.md`
  （天体初态 = 关卡摆放位置 + `InitialVelocity`，组件不生成初态）

---

## 1. 目标

给 N 体系统里的天体画出三样东西：

| # | 画什么 | 颜色 | 数据来源 |
|---|---|---|---|
| 1 | **预测轨迹** —— 未来一段时间的路径，补一点成闭合 | 浅（默认浅蓝） | 复制求解器、推进副本、记录目标天体 |
| 2 | **拖尾** —— 运行以后实际走过的路径 | 深（默认深蓝） | 按模拟时间采样的历史位置 |
| 3 | **初始速度矢量** —— 出发时朝哪、多快 | 琥珀 | `InitialVelocity` + 开局位置 |

三样都是**只读视图**：不改初态、不改模拟状态、不改 Actor 位置。

## 2. 已定的四个决策

| 决策 | 选择 | 理由 |
|---|---|---|
| 渲染方式 | 引擎 `ULineBatchComponent` | 逐线颜色与粗细、自动合批、**零资产**、纯 C++，且 Shipping 里依然在 —— 被编译掉的只是 `DrawDebugLine` 那层辅助函数 |
| 预测窗口 | 逐天体，默认 `1.05 × 估算周期` | 「补一点成闭合」；时长暴露成可调属性 |
| 代码归属 | 新增 `UCosmosTrajectoryComponent`（哑渲染器），参数挂在已有的 `UCosmosOrbitComponent` 上 | 详情面板里有取色器、逐天体可覆盖，且**零编辑器侧手工操作**（不用动蓝图、不用手动加组件） |
| 拖尾长度 | 默认 `0.25 × 预测时长`，容量固定 512 点 | 论证见 §6 |

## 3. 数据层：`FNBodySimulator` 新增三个方法

`FNBodySimulator` 是**值类型**（`TArray<FCosmosBodyState>` + 一堆 POD），所以「预测」
就是**复制一份、推进副本、记录目标天体**，原对象一个字节都不动。

```cpp
/** 一对天体相互绕转的周期：T = 2π√(r³ / (G·(m_i + m_j)))。r 取当前间距。 */
static double PairOrbitalPeriod(const FCosmosBodyState& A, const FCosmosBodyState& B);

/** 天体 i 参与的最短动力学时标 = min over j≠i 的 PairOrbitalPeriod。无解返回 0。 */
double EstimateOrbitalPeriod(int32 BodyIndex) const;

/** 预测天体 i 未来 Horizon 的轨迹，输出 AU 坐标序列。**不修改本对象**。 */
bool PredictTrajectory(int32 BodyIndex, double Horizon, int32 NumSamples,
                       TArray<FVector3d>& OutPositions) const;
```

### 3.1 为什么是「配对最小值」而不是「相对最重的那个天体」

恒星自己也该有轨迹（它在质心附近晃）。用配对最小值的话，恒星与它最近的行星算出的
是同一个周期 —— 因为那是**同一对天体** —— 两者画出的弧长因此一致，正是想要的。

如果改成「相对最重的那个天体」，恒星会去找「比它轻的行星」算周期，得到一个小得
离谱的窗口，画出来是一小段乱弧。

这个公式**子系统里已经有一份**：`FinalizeRegistration` 算 `BaseTimeStep` 用的就是它。
抽成 `static PairOrbitalPeriod` 之后两处共用，顺手消掉重复。
`ShortestPeriod` 变成 `min over i` 的 `EstimateOrbitalPeriod(i)`。

### 3.2 预测步长：`Horizon / NumSamples`

每个采样点走一步，即预测步长 ≈ `周期 / 256`，而模拟用的是 `周期 / 20000`
（`StepsPerOrbit`）。两个**不同步长的离散轨道**并不严格重合。

差多少：辛积分器的单圈相对误差随 `(h/T)²` 走，`(1/256)² ≈ 1.5e-5`。
本关卡轨道半径 7.18 公里，对应约 **11 厘米** —— 肉眼不可见。

**代价**：256 步 × 每个天体。N = 5 时一步是 20 个配对，故每个天体 5,120 次配对
求值；5 个天体合计 25,600 次，约 **0.3 毫秒**，即 60 fps 下约 **2% 的一个核**。
这个代价是**每帧**付的（理由见 §7），所以它是这套方案里唯一需要盯着的性能数字。
要降的话第一根旋钮是采样点数（256 → 128 直接砍半），第二根才是给重算加节流。

> 备注：一次 N 步积分本可以同时得到**所有**天体的预测轨迹 —— 那是
> `O(采样数 × N²)`，而逐天体各跑一遍是 `O(采样数 × N³)`，贵 N 倍。这里没这么做，
> 是因为各天体的窗口长短不同，共享一次积分要么按最长窗口采样、要么引入跨组件的
> 协调。N = 5 时 0.3 毫秒可以接受；天体数量上到几十个时这个立方项就会开始疼，
> 那时该做的就是改成共享一次积分。

### 3.3 算不出周期时的回退

`EstimateOrbitalPeriod` 在「求解器里只有一颗天体」或「所有配对都算不出有限正周期」
时返回 0。此时自动窗口回退到 `.cpp` 里的常量 `FallbackPredictionYears = 1.0` 年，
而不是拿 0 去当窗口（那会画出零长度的轨迹）。

手填 `PredictionYears > 0` 时永远走手填值，与估算完全无关 —— 自动与手填的分支只在
「属性是不是 0」这一处判断。

## 4. 子系统查询：`UCosmosSimulationSubsystem`

只加一个薄转发，渲染层不必碰求解器：

```cpp
/** 预测某个天体，输出**世界坐标（厘米）**。AU ↔ 厘米 的换算在这里做。 */
bool PredictTrajectoryWorld(int32 SimulatorIndex, double HorizonYears, int32 NumSamples,
                            TArray<FVector>& OutWorldPositions) const;

/** 已模拟的天文时间（年）。拖尾按模拟时间采样，不按帧。 */
double GetSimulatedTimeYears() const;
```

## 5. 逐天体参数（挂在 `UCosmosOrbitComponent` 上）

新增一个 `Cosmos|Trajectory` 分类。这些属性放在 Orbit 组件而不是轨迹组件上，
是因为 **Orbit 组件每个天体上都已经有了**，而轨迹组件是运行时创建的 ——
运行时创建出来的组件在编辑器里调不了，参数放上去等于没放。

| 属性 | 默认 | 说明 |
|---|---|---|
| `bDrawTrajectory` | `true` | 逐天体开关。飞船或恒星想关就关 |
| `PredictionColor` | 浅蓝 `(90, 170, 255)` | 预测轨迹，浅 |
| `TrailColor` | 深蓝 `(20, 50, 110)` | 拖尾，深 |
| `InitialVelocityColor` | 琥珀 `(255, 210, 80)` | 初始速度矢量 |
| `PredictionYears` | `0` = 自动 | 自动 = `1.05 × EstimateOrbitalPeriod` |
| `TrailYears` | `0` = 自动 | 自动 = `0.25 × 预测时长` |
| `bDrawInitialVelocity` | `true` | 箭头开关 |
| `VelocityArrowScaleCm` | `10000` | 每 (AU/年) 画多长，厘米 |

采样点数不进属性，是 `.cpp` 里的常量：预测 256 点、拖尾 512 点。需要更密就直接改
这两个常量（代价见 §3.2）。

## 6. 拖尾的采样论证

拖尾必须按**模拟时间**等间隔采样，不能按帧 —— 按帧的话，同样 512 个点覆盖多长
时间会随帧率漂移。

本关卡的数：`TimeScale = 0.00333` 年/秒（1 模拟年 = 300 秒真实时间）。
行星在 0.718 AU 上绕 1 M☉ 恒星，周期

```
T = 2π√( r³ / (G·(m_i + m_j)) )      项目里 G = 4π²，2π 与 G 抵消
  = √( r³ / (m_i + m_j) )
  = √( 0.718³ / 1 ) = 0.608 年
```

（子系统日志里那句「最短周期 ≈ 0.6085 年」就是这个数 —— 它看着小，是因为
游戏单位制把 G 定成了 4π²，而不是写错。）→ **一圈 ≈ 183 秒 ≈ 3 分钟**。
60 fps 下每帧推进 `0.0167 × 0.00333 = 5.56e-5` 年，即 4.1 米行程。

**为什么默认只画 0.25 圈**：如果拖尾也画满一圈，512 个点摊在 183 秒上，
采样间隔就是 0.36 秒 —— 拖尾每 0.36 秒才往前跳一段，跳幅 0.70°（约 88 米），
肉眼能看出它在「一跳一跳」。要把跳幅压到看不见就得把点数加到几千，
那就变成每帧重画上万条线。

取 0.25 圈则：

| 量 | 值 |
|---|---|
| `TrailYears`（自动） | `0.25 × 1.05 × 0.608 ≈ 0.160` 年 |
| 采样间隔 | `0.160 / 512 ≈ 3.12e-4` 年 |
| 对应真实时间 | `3.12e-4 / 0.00333 ≈ 0.094` 秒 ≈ **5.6 帧** |
| 每段跳幅 | `3.12e-4 / 0.608` 圈 = **0.185°**，弧长 **23 米**（r = 7.18 km） |
| 弦高（可见的折角） | 约 **1 厘米** |

23 米的跳幅在 7.18 公里的轨道上是 0.18°，看不见；弦高 1 厘米更是彻底平坦。
观感是一条跟着星球生长的**彗尾**，而不是一段一段跳的折线。采样间隔只有 5.6 帧，
所以拖尾实际上几乎每帧都在长。

环形缓冲容量 512，满了丢最旧的 —— 于是拖尾成为「最近 0.25 圈」的滑动窗口。
开局时它从零长起来，正是「运行以后产生的拖尾」。

## 7. 渲染组件 `UCosmosTrajectoryComponent`

由 `UCosmosOrbitComponent::BeginPlay` 用 `NewObject` + `RegisterComponent` 创建，
挂到 owner 的根组件下，并**显式调用一次初始化**（不依赖引擎是否替我们调 `BeginPlay`）。
它**没有面向用户的属性** —— 参数全从兄弟 Orbit 组件读。

内部状态：

| 成员 | 用途 |
|---|---|
| `ULineBatchComponent* LineBatch` | 画线（Engine 模块自带，**不需要改任何 Build.cs**） |
| `TArray<FVector> TrailBuffer` + 写指针 | 环形缓冲，容量 512 |
| `TArray<FVector> PredictedGameUnit` | 复用的输出数组，避免每帧重新分配 |
| `TArray<FVector> PredictedWorld` | 同上（AU → 厘米 转换后） |
| `FVector InitialWorldLocation` | 开局位置，速度箭头的锚点 |
| `double LastTrailSampleTime` | 上次采样的模拟时间 |
| `TArray<FBatchedLine> Lines` | 复用的提交缓冲，填好后一次交给 `DrawLines` |

> **实现偏差（指针）**：实际实现用 FIFO（`TArray` + `RemoveAt(0)`）而非环形缓冲，裁剪按
> **模拟时间**而非点数。理由见 `docs/superpowers/plans/2026-10-17-trajectory-system.md`
> 的「与 spec 的三处有意偏差」第 1 条 —— 本节表格描述的是**设计原意**，不是最终实现。

每帧：

1. `SimulatedTime - LastTrailSampleTime >= TrailInterval` → 把 owner 当前位置推进
   环形缓冲，更新 `LastTrailSampleTime`（间隔 5.6 帧，所以约每 6 帧推一次）
2. 重算预测（`PredictTrajectoryWorld`）
3. `Flush()` + 重画全部线

**为什么预测每帧重算，而不是像最初设想的那样节流到 0.2 秒**：拖尾是按模拟时间
采样的，间隔只有 5.6 帧，所以它本来就几乎每帧都在变 —— 重画这一步节流省不下什么。
而预测一旦节流，画出来的椭圆就会比星球的真实位置落后，滞后量 =
`节流时长 / 一圈时长 × 周长`：节流 0.2 秒对应 `0.2 / 183 × 45,113 米 ≈ 49 米`。
在 7 公里的轨道全景里看不出，但**凑近星球看时那条线明显接不上**。改成每帧重算，
滞后缩到 1 帧（4 米）。既然拖尾本来就要每帧重画，节流也省不下重画开销，索性连
重算一起每帧做，把「线接不接得上」这个问题整个消掉，代价就是 §3.2 里那 0.3 毫秒。

线用 `LifeTime = 0` 画上去（语义见 §7.1），每帧 `Flush()` 后再画一遍，所以画面
是连续的。每次重画的线数：每天体 256（预测）+ 512（拖尾）+ 3（箭头）= 771 条，
5 个天体共 3,855 条，即约 23 万条/秒的结构体追加。那是纯粹的内存拷贝，现代 CPU
上可忽略；真正要盯的是渲染线程重建这些线段的开销 —— 所以 §11 把 `stat unit`
列为必查项，而不是可选项。

**重画用 `DrawLines` 而不是逐条 `DrawLine`**：`DrawLines(TArrayView<FBatchedLine>)`
的存在理由就写在它自己的注释里 ——「比多次调用 `DrawLine` 快」。它只做一次 `Append`
和一次 `MarkRenderStateDirty`，而不是 771 次。所以维护一个复用的
`TArray<FBatchedLine> Lines` 成员，填好后一次交出去。

**预测的滞后**：每帧重算，所以画出来的椭圆最多比星球的真实位置落后 1 帧的行程 =
4 米。任何视距下都接得上。

### 7.1 已对着引擎源码确认的 `ULineBatchComponent` 行为

下面这些是写这份 spec 时从 UE 5.8 源码里读实的，不是猜的：

| 事实 | 出处 | 对我们的意义 |
|---|---|---|
| `UCLASS(MinimalAPI)`，但 `DrawLine` / `DrawLines` / `Flush` / `CreateSceneProxy` / `CalcBounds` / `TickComponent` 各自带 `ENGINE_API` | `Classes/Components/LineBatchComponent.h:127` | 跨模块可用 —— `MinimalAPI` 导出 UClass（`NewObject` 要的 `StaticClass()`），逐个方法导出符号。头文件在 `Classes/` 下，属于 Engine 模块的公开包含路径 |
| `DrawLine` 把 `LifeTime` **原样**写进 `FBatchedLine::RemainingLifeTime`，**不**回退到 `DefaultLifeTime` | `.cpp:158-160` + `FBatchedLine` 构造函数 | `DefaultLifeTime`（构造时置 1.0 秒）只被 `DrawBox` / `DrawCircle` 那批形状辅助函数使用，`DrawLine` 完全不碰它 |
| `TickComponent` 只递减 `RemainingLifeTime > 0` 的线 | `.cpp` `TickComponent` | 传 `LifeTime = 0` 的线**永不过期**，一直留到 `Flush()`。这正是我们需要的：画一次、要清的时候整体清掉 |
| `Flush()` 清空三个缓冲并 `MarkRenderStateDirty()` | `.cpp` `Flush` | 每次重画前调一次即可 |
| 线走 `PDI->DrawTranslucentLine(Start, End, Color, DepthPriority, Thickness)` | `.cpp:78` | 逐线**颜色与粗细都生效**；`bDepthTest` 默认 `true`，所以星球能正常遮挡它背后的轨道线 |
| `bCalculateAccurateBounds` 默认 `true`，`CalcBounds` 遍历所有线求紧包围盒 | `.cpp` 构造函数 + `CalcBounds` | 771 条线 × 60 Hz 的遍历可忽略，保留默认值 |
| `Thickness = 0` 走 `LineVertices`（真正的 1 像素 GPU 线）；`Thickness > 0` 走 `FBatchedThickLines`（**世界单位**的四边形，因为行批把 `bScreenSpace` 写死成 `false`） | `BatchedElements.cpp:165` + `PrimitiveDrawInterface.h:65` | 所以线宽传 **0** —— 1 像素、与距离无关，1 公里外和 100 公里外看一样清楚。传正值得到的是「厘米宽」的四边形：近了变成粗带、远了直接消失 |
| 行批的 `FBatchedLine` 构造参数序是 `(Start, End, Color, LifeTime, Thickness, DepthPriority, BatchID)`，而 `DrawLine` 的函数参数序是 `(..., DepthPriority, Thickness, LifeTime, ...)` | `LineBatchComponent.h` 两处声明 | **两者相反**。自己填 `FBatchedLine` 数组时按前者；写反了不会编译报错（都是 float），但会把「寿命」当「粗细」用 |

**引擎给这个类自己的注释是**：「This can be useful for debug drawing, but is not very
performant for runtime use.」我们每帧重画、每次不到 4 千条线、每帧一次 `Flush()` +
一次 `Append` —— 这个用量落在它撑得住的范围内（同类做法在游戏里画几千条调试线是
常态），但它确实踩在这句警告的边界上，所以 §11 把帧时间检查列为必做。将来若把采样
点数或天体数量调大一个量级，第一件该回头看的事就是这句警告。

## 8. 初始速度矢量

| 量 | 取值 |
|---|---|
| 起点 | `InitialWorldLocation`（开局缓存的 `GetActorLocation()`），**静止不动** |
| 方向 | `Simulator.ToWorldVelocity(FVector3d(InitialVelocity))` 归一化 |
| 长度 | `VelocityArrowScaleCm × InitialVelocity.Size()` |
| 箭头 | 末端两条短线，长度 = 杆长的 15%，与杆成 ±150° |

默认值下的样子：`10000 × 7.416 = 74,157` 厘米 = **742 米**，是 7.18 公里轨道半径的
约 10%。够显眼，又不会盖住轨道。（这里的初速是 0.718 AU 圆轨道的
`2π√(M/r) = 7.416` AU/年，不是地球的 6.283 —— 那个数对应 1 AU。）

箭头**钉在出发点**而不是跟着星球走：它记录的是「从这儿出发、朝这个方向、这个速度」。
星球飞走之后箭头留在原地，正好标出起点。

## 9. 测试

`EstimateOrbitalPeriod` / `PredictTrajectory` 是纯数学、不依赖世界，是唯一能真正
验证的地方。在 Core 模块加一个 `WITH_DEV_AUTOMATION_TESTS` 保护下的自动化测试，
三条断言：

1. **周期正确**：圆轨道两天体，`EstimateOrbitalPeriod` 与
   `2π√(r³ / (G·(m_i+m_j)))` 差 < 1%（`G = 4π²`，所以 1 M☉ + 0.718 AU 的这个值
   就是 §6 里的 0.608 年 —— 测试里写成完整公式，别抄化简后的形式）
2. **闭合**：取 `Horizon` **正好等于一圈**（`EstimateOrbitalPeriod`），末点与首点的
   距离应 < 轨道半径的 1%。注意这里不能用绘制窗口那个 `1.05 × 周期` —— 1.05 圈对应
   18° 的弧，末点离首点 31% 的半径，那是**故意**画的「补一点」，不是闭合误差。
3. **不改原对象**：调用 `PredictTrajectory` 前后，求解器的 `GetSimulatedTime()`、
   每个天体的位置与速度、能量漂移比全部逐位相同

第 3 条挡的是最危险的那个 bug —— 预测把真实模拟带跑偏。这种错误在 PIE 里表现为
「轨道慢慢变形」，很容易被当成积分器精度问题查半天。

## 10. 涉及文件

**新增（2）**

- `Source/StellarEra/Public/CosmosTrajectoryComponent.h`
- `Source/StellarEra/Private/CosmosTrajectoryComponent.cpp`

**修改（6）**

- `Source/StellarEraCore/Public/NBodySimulator.h` —— 三个方法声明
- `Source/StellarEraCore/Private/NBodySimulator.cpp` —— 三个方法实现
- `Source/StellarEra/Public/CosmosOrbitComponent.h` —— 8 个属性 + 缓存的初始位置
- `Source/StellarEra/Private/CosmosOrbitComponent.cpp` —— BeginPlay 里创建渲染组件
- `Source/StellarEra/Public/CosmosSimulationSubsystem.h` —— 两个查询
- `Source/StellarEra/Private/CosmosSimulationSubsystem.cpp` —— 两个实现 +
  `FinalizeRegistration` 改用 `PairOrbitalPeriod`

**可选（1）**：`Source/StellarEraCore/Private/NBodySimulatorTests.cpp` —— 见 §9。
放在 Core 模块是因为被测的纯数学在那里；`WITH_DEV_AUTOMATION_TESTS` 在 Development
构建里为真，测试会出现在 Session Frontend 的自动化列表里。

**不需要动**：任何 `Build.cs`（`ULineBatchComponent` 在 Engine 模块里）、
`.uproject`、`Config/*.ini`、任何蓝图或资产。**不需要 CoreRedirect** ——
全是新增类，没有东西换模块。

## 11. 验证

1. 关编辑器 → 用捆绑 dotnet 直调 `UnrealBuildTool.dll` → 编译通过，无新增警告
2. PIE：
   - 浅色椭圆**闭合**（末点接上首点附近）
   - 深色拖尾从星球身后生长成彗尾
   - 出发点有琥珀色箭头，方向与初速度一致
3. 日志：每个天体打印估算周期与选中的预测时长
4. `stat unit` 确认没有帧时间回归
5. 逐项关掉 `bDrawTrajectory` / `bDrawInitialVelocity`，确认开关生效

## 12. 已知取舍

1. **拖尾默认只画 0.25 圈。** 想要更长就调大 `TrailYears`，代价是同样 512 个点摊得
   更开、每段跳幅变大（见 §6 的表可以自己反推）。要同时长又平滑，就得调大采样点数
   常量，那会线性增加每帧线数。
2. **预测用的步长比模拟粗约 80 倍**（`周期/256` 对 `周期/20000`）。论证见 §3.2 ——
   差在 11 厘米级（见 §3.2）。若将来把 `StepsPerOrbit` 调小到接近 256，两者会开始
   接近，需要重新评估。
3. **`EstimateOrbitalPeriod` 拿当前间距当半径**，偏心轨道上会低估周期。
   这与 `BaseTimeStep` 的既有近似完全一致 —— 一致性优先于精度，而且它是自动值，
   不满意可以直接手填 `PredictionYears`。
4. **每个天体一个 `ULineBatchComponent`**，即 N 个 draw call。N ≤ 10 时无所谓；
   天体数量上到几十个时应改成子系统持有单个 batch（见 §3.2 的备注）。
5. **运行期增删天体不在本次范围。** 与既有设计一致 —— 本项目的天体整局不增不减
   （`UnregisterBody` 也刻意不从求解器里摘）。
6. **`UCosmosOrbitComponent` 从「只存物理参数」变成「存物理 + 视图参数」。**
   它依然只存参数、依然不 tick，但名字里的「Orbit」不再覆盖它的全部内容。
   接受这个代价，换来的是零编辑器侧手工操作。
7. **`bSimulated = false` 的天体，拖尾与预测会互相矛盾。** 拖尾采样的是 Actor 的
   **实际**位置（那种天体不动，所以拖尾缩成一个点），而预测读的是**求解器**里的
   位置（照样在漂，所以画出一条椭圆）。两者本来就在描述不同的东西 ——
   这与 `bSimulated` 的既有语义一致（见 `CosmosOrbitComponent.h` 里那段
   「两边会分叉」的说明），不是本次引入的新问题。视觉上是「一个不动的点 +
   一条椭圆」，真觉得别扭就把那种天体的 `bDrawTrajectory` 关掉。

## 附录：随轨迹提交一并落地的三处改动（无独立 spec，补记于此）

这三处改动随轨迹系统的提交一并落地，当时没有独立 spec。经用户确认是有意要求的，
补记于此，以免将来有人改 PlayerStart 语义或换关卡时不知道要守住哪些不变量。

### 1. `AStellarEraGameMode`：飞船位置改由 PlayerStart 决定

- **受 `bAutoPlaceSpaceshipAtStart`（默认 `true`）门控**。关掉它 = 飞船完全由
  关卡里的手摆位置决定，GameMode 不插手 —— `EnsureSpaceshipExists()` 会直接早退
  （`StellarEraGameMode.cpp:143`）。
- 打开时：出生点只有 `PlayerStart` 一个来源（`FindSpawnPlayerStart()`；关卡里没有
  PlayerStart 就只记 Warning 并跳过摆放）。关卡里手摆的飞船被摆到那里，只提供
  **模型与参数**；关卡里没有飞船就在那里生成一艘。
- **要守住的不变量**：开关打开时，飞船的位置来源只有一个 —— PlayerStart。改
  PlayerStart 语义或换关卡时，飞船位置会跟着变，这是设计意图，不是 bug。

### 2. `AStellarEraHUD`：新增 HUD 类

- 用途：常驻的飞船状态条（当前 Possess 的是飞船时才画）+ 按 Tab 等操作弹出的短暂提示
  （`ShowNotice`）。用 `AHUD` + Canvas 直接画，不建 UMG 资产 —— 与「输入资产也在
  C++ 里造」的做法一致。绘制内容见 `StellarEraHUD.h` 类注释。

### 3. `AStellarEraPlayerController`：走路模式与鼠标模式

- 控制器新增了走路/飞船/星图三种鼠标模式的切换。星图模式的那部分已在
  `2026-10-17-starmap-mode-design.md` 里，此处只记它早于星图存在。
