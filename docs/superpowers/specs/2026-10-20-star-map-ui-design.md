# 星图 UI — 设计

- **日期**：2026-10-20
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-17-starmap-mode-design.md`（星图模式、拾取与选中状态已实现）

---

## 1. 目标

在星图模式下加两层信息 UI：

| # | 需求 |
|---|---|
| 1 | 选中的天体显示名字与完整属性面板，贴在**天体中心右侧** |
| 2 | 鼠标悬停的天体显示同样的属性面板，贴在**鼠标右侧**；鼠标离开即隐藏 |
| 3 | 这些 UI 封装进一个独立的「UI 控制系统」，不散落在 HUD 里 |

用户原话：

> 给星图模式添加ui显示，主要是选中的天体显示出他的天体S属性，同时还有是显示天体的名字，
> 除此之外是鼠标悬停在天体上，对应天体的ui属性窗口也要显示，离开以后就隐藏，
> 为了方便开发，希望做一个ui控制的系统，就在这个系统里买高这些ui部分，开worktree进行开发

## 2. 已定决策

| # | 问题 | 选择 |
|---|---|---|
| 1 | UI 技术 | **纯 C++ Canvas**，沿用现有 `AHUD` 体系；不建 UMG 资产，`Build.cs` 不加 UMG/Slate |
| 2 | 封装方式 | 新建 `UStarMapUISystem`（UObject），由 `AStellarEraHUD` 持有 |
| 3 | 显示字段 | 身份与质量、运动状态、物理量、调试信息，全部显示 |
| 4 | 悬停窗口内容 | 与选中面板完全相同的字段，只差锚点 |
| 5 | 锚点 | 悬停窗口贴鼠标右侧；选中面板贴天体投影中心右侧 |
| 6 | 悬停选中体 | 抑制悬停窗口（同内容已在选中面板里，避免重叠） |
| 7 | 面板布局 | 右侧放不下翻到左侧，再整体夹进视口；选中体不在屏幕内时整块不画 |

## 3. 架构与数据流

新增 `UStarMapUISystem : UObject`（StellarEra 模块，纯 C++）。它不 Tick、不保存跨帧状态，
只在被调用时拉数据、算布局、画。`AStellarEraHUD` 以 `UPROPERTY(Transient)` 持有它，
首次 `DrawHUD` 时惰性创建。

每帧数据流（全部在 `DrawHUD` 内完成）：

```
AStellarEraHUD::DrawHUD
  └─ 若当前 Pawn 是 AStarMapCameraPawn（星图模式）
       └─ UStarMapUISystem::Draw(HUD)
            1. PC->GetMousePosition() → PC->PickBodyUnderScreenPosition(MousePos)   // 悬停
            2. Subsystem->GetSelectedBody()                                         // 选中
            3. BuildBodyInfo(Actor) × 0..2 → 名称/质量/位置/速度/周期/半径/引力/调试
            4. 纯函数：格式化文本行、测尺寸、右侧锚定 + 视口夹紧
            5. Canvas 画选中面板与悬停面板
```

退出星图时不需要清理：悬停与选中面板都是当帧现算的，Pawn 一换就不再进入绘制分支。

`DrawHUD` 的绘制顺序：星图面板 → `DrawNotice()`（提示框永远在最上层）。
星图模式下当前 Pawn 是 `AStarMapCameraPawn`，`GetViewedShip()` 返回空，飞船状态条本来就不画。

## 4. 数据模型与格式

数据层是纯 struct，不含 UObject：

```cpp
struct FStarMapBodyInfo
{
    FString Name;                      // GetActorNameOrLabel()
    double  MassEarth = 0.0;           // UCosmosOrbitComponent::MassEarth
    double  MassSolar = 0.0;           // CosmosUnits::EarthMassToSolarMass(MassEarth)
    FVector PositionAu;                // UCosmosOrbitComponent::PositionAu
    FVector VelocityKmPerSecond;       // UCosmosOrbitComponent::SolverVelocityKmPerSecond
    double  OrbitalPeriodYears = 0.0;  // UCosmosSimulationSubsystem::GetOrbitalPeriodYears
    double  RadiusKm = 0.0;            // 行星 GetSurfaceRadiusCm()，其它简单碰撞半径
    double  SurfaceGravityMps2 = 0.0;  // G · M / r²，由质量与半径算出
    int32   SimulatorIndex = INDEX_NONE;
    bool    bSimulated = false;
    bool    bExertsGravity = false;
};
```

`MassSolar` 由 `MassEarth` 现算，不读 `MassSolarReadout` —— 后者是逐帧回显，
未注册或时序不对时会停在旧值/零。

面板行由纯函数 `FormatBodyInfoLines(const FStarMapBodyInfo&)` 生成，选中与悬停共用：

```
地球
─────────────────────────
质量        1 M⊕（3.003e-06 M☉）
位置        (0.8732, 0.0000, 0.0000) AU
速度        29.790 km/s
轨道周期    1.000 年
半径        6371.0 km
表面引力    9.822 m/s²（1.002 g）
Simulator   #0
模拟        是    引力源    是
```

格式规则：

| 字段 | 格式 | 缺省 |
|---|---|---|
| 质量 | `%.4g M⊕（%.4g M☉）` | — |
| 位置 | `(%.4f, %.4f, %.4f) AU` | — |
| 速度 | 模长 `%.3f km/s` | — |
| 轨道周期 | `%.3f 年` | `<= 0` → `未知` |
| 半径 | `%.1f km` | 无效 → `—` |
| 表面引力 | `%.3f m/s²（%.3f g）`，`g₀ = 9.80665` | 质量或半径无效 → `—` |
| Simulator | `#%d` | `INDEX_NONE` → `#-1（未注册）` |
| 模拟 / 引力源 | `是` / `否` | — |

表面引力是**天体自身的表面重力**，不是求解器里的引力合力：

```
M_kg = MassEarth × CosmosUnits::EarthMass × CosmosUnits::SolarMassToKilogram
r_cm = RadiusKm × 1e5
g    = CosmosUnits::GravitationalConstant_SI × M_kg / r_cm² / 100
```

其中 `GravitationalConstant_SI = 6.67430e-5` 的单位是 `cm³/(kg·s²)`，除以 100 得 `m/s²`。
地球参数（1 M⊕、6371 km）应得约 `9.822 m/s²` —— 这就是自动化测试的锚点。

## 5. 布局、锚定与边界

样式沿用现有 HUD：`GEngine->GetMediumFont()`、黑底半透明 `(0,0,0,0.65)`、
金色标题与边框 `(1, 0.85, 0.30)`、白色正文；内边距 10 px、行距 4 px、
标题与正文之间画一条金色细分隔线。

布局是纯函数：

```cpp
struct FStarMapPanelRect
{
    float X = 0.f;
    float Y = 0.f;
    float W = 0.f;
    float H = 0.f;
};

FStarMapPanelRect PlacePanelBesideAnchor(
    const FVector2D& AnchorPx,        // 天体投影中心，或鼠标位置
    float AnchorRadiusPx,             // 天体的屏幕轮廓半径；鼠标为 0
    const FVector2D& PanelSizePx,
    const FVector2D& ViewportSizePx,
    float GapPx,                      // 12 px
    float MarginPx);                  // 8 px
```

算法：

1. 首选右侧：`X = Anchor.X + AnchorRadiusPx + GapPx`，`Y = Anchor.Y - H/2`。
2. 右侧溢出视口且左侧放得下：翻到左侧
   `X = Anchor.X - AnchorRadiusPx - GapPx - W`。
3. 两个方向都放不下或翻过去仍溢出：保持右侧结果，然后整体夹紧。
4. 最终 `X ∈ [Margin, max(Margin, ViewportW - Margin - W)]`，`Y` 同理。
   视口比面板还小时，以左上边距为准（`max` 保证不会出现负坐标）。

**选中面板**：锚点 = `AStellarEraPlayerController::ProjectBodyToScreen`
返回的天体投影中心与轮廓半径。投影失败或中心不在视口内 → 整块不画，
不把面板钉在屏幕边缘制造「天体在那边」的误导。

**悬停面板**：仅当被悬停天体有效、不是选中天体、且鼠标在视口内时画。
锚点 = 鼠标位置，`AnchorRadiusPx = 0`。绘制在选中面板之后，保证在最上层。

**字体测量**：`AHUD::GetTextSize` 先量标题与每一行，面板宽 = 最宽内容 + 2×内边距，
高 = 标题高 + 行距 + 各行高与行距 + 2×内边距。不写死宽度。

## 6. 投影复用（PlayerController 改动）

现在 `AStellarEraPlayerController::PickBodyUnderCursor()` 是私有函数，
内部自己遍历已注册天体、逐个投影、再调用纯函数 `PickBodyAtScreenPosition`。
悬停与选中面板都需要同一套投影，因此抽出一个逐候选函数并公开两个入口：

```cpp
/** 把一颗天体投到屏幕；成功时 OutCenter 为中心，OutRadiusPx 为轮廓像素半径。 */
bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const;

/** 光标/任意屏幕点下的天体。悬停与点击共用。 */
AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const;
```

- 私有 `bool MakeScreenCandidate(AActor* Body, FStarMapScreenCandidate& Out) const`：
  现有「中心投影 + 视线右向边缘投影 × `SilhouetteRadiusScale`」逻辑原样搬入。
- `ProjectBodyToScreen` 与 `PickBodyUnderScreenPosition` 都调用它；
  后者遍历全部已注册天体后仍走纯函数 `PickBodyAtScreenPosition`。
- `PickBodyUnderCursor()` 删除，`OnStarMapSelectCompleted` 改为
  `PickBodyUnderScreenPosition(FVector2D(MouseX, MouseY))`。
- 行为不变：同样的候选、同样的 `SelectionPixelRadius`、同样的边界含规则。

## 7. HUD 集成

`AStellarEraHUD` 新增：

```cpp
UPROPERTY(Transient)
TObjectPtr<UStarMapUISystem> StarMapUI;
```

`DrawHUD` 在 `DrawNotice()` 之前：

```cpp
if (APlayerController* PC = GetOwningPlayerController())
{
    if (Cast<AStarMapCameraPawn>(PC->GetPawn()))
    {
        if (!StarMapUI)
        {
            StarMapUI = NewObject<UStarMapUISystem>(this);
        }
        StarMapUI->Draw(*this);
    }
}
```

`UStarMapUISystem::Draw(AStellarEraHUD& Hud)` 内部通过 `Hud.GetOwningPlayerController()`
与 `Hud.GetWorld()` 取 PC / 子系统，再调用 `Hud.DrawRect` / `Hud.DrawText` /
`Hud.GetTextSize`。

## 8. 测试

新增自动化测试文件 `Source/StellarEra/Private/StarMapUISystemTests.cpp`，
测试名统一 `StellarEra.StarMap.UI.*`，全部是纯函数，editor world 可跑：

**8.1 `StellarEra.StarMap.UI.SurfaceGravity`**

- 1 M⊕ + 6371 km → `9.822 ± 0.05 m/s²`。
- 质量 `<= 0`、半径 `<= 0`、NaN → 返回 `0`（面板据此显示 `—`）。
- 质量翻倍、半径不变 → 引力翻倍。

**8.2 `StellarEra.StarMap.UI.Formatting`**

- 构造地球样板数据，断言输出行包含：
  名称、`1 M⊕`、`3.003e-06 M☉`、`29.790 km/s`、`1.000 年`、
  `6371.0 km`、`9.822 m/s²`、`1.002 g`、`#0`、`模拟`。
- 周期 `<= 0` → 含 `未知`；半径 `<= 0` → 半径与引力两行都含 `—`。
- `SimulatorIndex == INDEX_NONE` → 含 `未注册`。
- `bSimulated = false` / `bExertsGravity = false` → 对应行显示 `否`。

**8.3 `StellarEra.StarMap.UI.Placement`**

- 右侧放得下：`X == Anchor.X + Radius + Gap`，`Y == Anchor.Y - H/2`。
- 右侧溢出、左侧放得下：翻到左侧，`X == Anchor.X - Radius - Gap - W`。
- 靠近顶部/底部：`Y` 夹到 `Margin` / `ViewportH - Margin - H`。
- 视口比面板小：结果钉在左上边距，不出现负坐标。
- 鼠标（半径 0）与天体（半径非 0）走同一函数，只差半径参数。

**验不了、必须人看的**：面板观感、悬停跟随手感、拖拽转视角时面板是否稳定跟随。
归 PIE 人工验收。

## 9. 涉及文件

全部进 `Source/StellarEra/`。`StellarEra.Build.cs` **不用动**。

| 文件 | 动作 |
|---|---|
| `Source/StellarEra/Public/StarMapUISystem.h` | 新建：`FStarMapBodyInfo`、`FStarMapPanelRect`、`UStarMapUISystem` |
| `Source/StellarEra/Private/StarMapUISystem.cpp` | 新建：数据采集、格式化、布局、Canvas 绘制 |
| `Source/StellarEra/Private/StarMapUISystemTests.cpp` | 新建：§8 三组测试 |
| `Source/StellarEra/Public/StellarEraHUD.h` | 加 `StarMapUI` 成员 |
| `Source/StellarEra/Private/StellarEraHUD.cpp` | 星图模式下调用 `StarMapUI->Draw` |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | 公开 `ProjectBodyToScreen`、`PickBodyUnderScreenPosition` |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | 抽 `MakeScreenCandidate`，删除 `PickBodyUnderCursor` |

## 10. 已知取舍

| # | 取舍 | 说明 |
|---|---|---|
| 1 | 选中的天体离开屏幕就整块不画 | 避免把面板钉在边缘误导方向；代价是选中体被挡在身后时看不到属性 |
| 2 | 悬停选中体不弹第二个窗口 | 同内容已在选中面板，避免两块面板叠在鼠标旁 |
| 3 | Canvas 没有自动换行与 DPI 缩放 | 本面板字段短、单行可控；与现有 HUD 一致，不为本轮引入 Slate |
| 4 | 文本写死中文 | 项目现有 HUD 与提示均为中文，无本地化管线 |
| 5 | 表面引力用球对称公式，不含自转 | 项目天体无自转参数；公式与物理教科书一致，测试可锚定 |
| 6 | UI 系统不做命中测试、不接收鼠标事件 | 本轮只读显示；面板不拦截点击，点击仍落到天体拾取 |

## 11. 数据源事实（本设计依赖）

| 事实 | 出处 |
|---|---|
| `MassEarth` 单位 M⊕；`MassSolarReadout` 是瞬时回显 | `CosmosOrbitComponent.h`（Body / Readout 段） |
| `PositionAu` 单位 AU、`SolverVelocityKmPerSecond` 单位 km/s | `CosmosOrbitComponent.h` 只读回显段 |
| `GetOrbitalPeriodYears(int32)` 返回年 | `CosmosSimulationSubsystem.h` |
| `GetRegisteredBodies(TArray<AActor*>&)` 返回已注册天体 | `CosmosSimulationSubsystem.h` |
| `EarthMass = 3.0034893e-6`、`SolarMassToKilogram = 1.98892e30`、`GravitationalConstant_SI = 6.67430e-5` | `CosmosTypes.h` |
| 拾取的轮廓半径用 `SilhouetteRadiusScale(d, R)` | `StarMapCameraPawn.h`（星图模式设计 §4.2） |
| `AHUD::DrawRect` / `DrawText` / `GetTextSize` 已在本项目 HUD 使用 | `StellarEraHUD.cpp` |
