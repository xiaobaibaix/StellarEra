# 矿物透视观察（H 键）设计

- 日期：2026-10-25
- 分支：`feat/planet-character-spaceship`
- 替代：`docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md`（切开半球那套）

## 1. 目标

星图里选中体素行星后按 **H**，把行星变成一颗**半透明球壳**，同时把地质场里的矿物
以**不透明方块**的形式显示在球体内部；观察者可以按元素筛选，只看一个、几个或全部矿物。
再按 H 退出，一切恢复原样。

这是纯观察模式：不改体素、不碰碰撞、不写存档、不影响 N 体状态。

### 非目标

- 不保留原来的「切开半球 + 剖面纹理 + 三个旋转环」。
- 不把真实体素网格改成半透明材质（会触发 `RecreateRender` 全量重建，见 §8）。
- 不做体积渲染、不做可下潜水体、不剖大气壳。
- 不为非体素天体（飞船、恒星）提供该模式。

## 2. 为什么需要生成矿物几何

体素世界只对地表做等值面抽取，球体内部没有网格。`Content/Geology/Planets/earth.json`
里 `peakDepthRatio` 为 0.06 / 0.17 / 0.29 / 0.41 / 0.55 / 0.85 的矿带在旧剖面里
只是 2D 纹理上的颜色，并没有 3D 几何。

因此本模式在进入时按地质场**解析采样**，在球内生成一组稀疏方块。方块的数据源
（`FPlanetGeologyField::Sample`）、深度定义（基准球面为 0、向内为正、单位体素）
与地表着色完全一致，所以看到的矿带位置与地表矿脉是同一套判据。

## 3. 进入与退出

| 动作 | 结果 |
|---|---|
| 星图里按 H，选中天体是 `AVoxelPlanetActor` 且地质场非空 | 进入矿物透视 |
| 星图里按 H，没有选中 / 不是体素行星 / 地质场为空 | 不进入，屏幕提示 2 秒（沿用现有文案） |
| 非星图里按 H | 忽略（不提示） |
| 观察中再按 H | 退出并恢复 |
| 观察中按 M（退出星图）/ 选中被清空 / Pawn 改变 / 关卡结束 | 先退出观察，再执行原动作 |

进入时的相机动作：

1. `StarMapPawn->FocusOn(Planet, 3 × 表面半径)`（沿用现有距离夹取与平滑拉远）。
2. 不强制改变视角朝向；用户按 H 前看哪儿，进入后还是那个角度。

退出时恢复：行星网格可见性、碰撞、大气壳的 `bAtmosphereEnabled` / 可见性
（沿用旧剖面的保存-恢复逻辑）。

## 4. 视觉规格

### 4.1 半透明球壳

- 网格：`/Engine/BasicShapes/Sphere`（内置 100 cm 直径球），缩放到直径 = `2 × GetSurfaceRadiusCm()`。
- 位置：行星球心；每帧跟随行星位置。
- 材质 `M_PlanetGhost`：`BLEND_Translucent`、Unlit、Two Sided。
  - `ShellColor` 默认取行星 `SurfaceColor` 调暗（约 0.35 倍）；
  - `Opacity` 默认 0.12；
  - 菲涅尔边缘把不透明度提到 0.35，让球体轮廓清楚。
- 球壳不接收投影、不参与碰撞（`SetCollisionEnabled(NoCollision)`）。

### 4.2 矿物方块

- 每个元素一个 `UInstancedStaticMeshComponent`，共用 `/Engine/BasicShapes/Cube`。
- 方块材质 `M_MineralBlock`：Unlit、`BlockColor` 向量参数 → Emissive；
  每个元素一个 MID，颜色 = 元素 `Color`，亮度略高于 1（约 1.4 倍）以便透过球壳读清。
- 方块尺寸：`BlockSizeRatio × 采样间距`，默认 `BlockSizeRatio = 0.45`；
  默认采样间距 `R/16`（1 km 行星约 62 m，方块约 28 m）。
- 朝向：在采样点处围绕随机轴做 ±25° 以内的随机旋转，避免整片方块像规则网格。
- 位置：采样点本身（世界坐标 = 球心 + 方向 × 半径）。
- 只有 `DominantNorm > Appearance.ColorThreshold` 的采样点才生成方块；
  元素归属 = `Sample.DominantIndex`。与地表着色的阈值、淡出范围一致。

### 4.3 筛选

- 左上角面板：

  ```
  矿物观察 — <行星名>
  ────────────────────────
  [色块] <元素显示名>   <丰度>%   [开/关]
  ...
  ────────────────────────
  H 退出   点击图例/数字键筛选   右键拖转视角   滚轮缩放
  ```

- 关闭的元素整行变灰（色块减到 35% 亮度，文字用暗色），开启的保持原色。
- 鼠标左键点击元素行切换；数字键 `1..9` 切换前 9 个元素（按元素表顺序）；
  `0` 全部打开。元素多于 9 个时，其余元素仍可点击图例切换。
- 观察模式下左键不再选中天体，只用于图例命中。

## 5. 架构

### 5.1 组件与 Actor

```
AVoxelPlanetActor
└─ UPlanetMineralViewComponent   (替换 UPlanetCutawayComponent)
       │ Activate 时 Spawn
       └─ APlanetMineralViewActor
            ├─ UStaticMeshComponent       球壳
            └─ UInstancedStaticMeshComponent × N   每个元素一组
```

- 观察 Actor **不能**挂在行星 Actor 下面：进入模式会隐藏行星 Actor，
  子组件会跟着一起隐藏。
- 观察 Actor 每帧把位置同步为行星位置；不复制旋转/缩放（N 体只平移）。
- 退出时 `Destroy()` 观察 Actor，同时恢复行星与大气。

### 5.2 采样器（纯函数）

`Source/StellarEraPlanet/Public/PlanetMineralBlocks.h`：

```cpp
struct FPlanetMineralBlock
{
    FVector LocationCm = FVector::ZeroVector; // 相对球心
    int32 ElementIndex = INDEX_NONE;
};

namespace PlanetMineralBlocks
{
    struct FBuildParams
    {
        float SurfaceRadiusCm = 0.f;
        float VoxelSizeCm = 0.f;
        float SpacingRatio = 1.f / 16.f;
        int32 Seed = 0;
    };

    struct FBuildResult
    {
        TArray<TArray<FPlanetMineralBlock>> BlocksByElement;
        int32 SampleCount = 0;
    };

    void Build(const FPlanetGeologyField& Field, const FBuildParams& Params, FBuildResult& Out);
}
```

算法：

1. 间距 `Step = SurfaceRadiusCm × SpacingRatio`。
2. 遍历 `[-R, R]³` 的格点，每个点加 `±0.5 × Step` 的抖动（`FRandomStream(Seed)`）。
3. 跳过 `|p| > R`（球外）与 `|p| < 0.05R`（球心附近方向退化）的点。
4. `Dir = p.GetSafeNormal()`，`Depth = (R - |p|) / VoxelSizeCm`。
5. `Field.Sample(Dir, Depth)`；`DominantNorm > Field.GetAppearance().ColorThreshold`
   且 `DominantIndex` 合法才生成方块，加入对应元素组。阈值与地表着色共用同一份
   `Appearance`，不另设参数。
6. `Out.SampleCount` 记录总采样点数，供日志与性能诊断。

默认参数下（间距 `R/16`）格点 33³ ≈ 3.6e4，球内约 1.9e4 个采样点，同步执行；
通过阈值的方块预计在 1e4 量级，每个元素一条 ISM。

### 5.3 组件接口

```cpp
class UPlanetMineralViewComponent : public USceneComponent
{
    bool Activate(AVoxelPlanetActor* Planet);
    void Deactivate();
    bool IsActive() const;

    int32 NumElements() const;
    bool IsElementVisible(int32 Index) const;
    void SetElementVisible(int32 Index, bool bVisible);
    void ToggleElement(int32 Index);
    void SetAllElementsVisible(bool bVisible);

    AVoxelPlanetActor* GetViewPlanet() const;
    TSharedPtr<const FPlanetGeologyField> GetGeologyField() const;
};
```

- `Activate` 失败原因只有两种：没有地质场、观察 Actor / 材质资产创建失败（打 Error）。
- 组件 Tick 只在激活期间开启（`TG_PostUpdateWork`），只做观察 Actor 跟随。
- `EndPlay` 兜底 `Deactivate`。

### 5.4 输入与 UI

- `StellarEraPlayerController`：
  - `IA_CutawayToggle` 改名为 `IA_MineralViewToggle`，仍映射 H；
  - `IA_CutawayDrag` 删除，`Mouse2D` 不再分派给剖面；
  - 新增 `IA_MineralFilter1..9`（数字键 1..9）与 `IA_MineralFilterAll`（0）；
  - 观察模式下 LMB `Started` 做图例命中，命中则切换对应元素。
- `UStarMapUISystem`：
  - `FCutawayLegendEntry` 改名 `FMineralLegendEntry`，增加 `bool bVisible`；
  - `FormatCutawayLegendLines` 改名 `FormatMineralLegendLines`，增加开关态文本（`开` / `关`）；
  - 新增纯函数 `ComputeMineralLegendLayout(ViewportSize, EntryCount, OutPanel, OutRowRects)`
    与 `HitTestMineralLegendRow(RowRects, ScreenPos)`；
  - `DrawCutawayLegend` 改名 `DrawMineralLegend`，复用同一份 layout，绘制开关态与灰化。
  - `AStellarEraPlayerController::GetActiveCutaway()` 改名 `GetActiveMineralView()`。
- 布局函数不保存跨帧状态：绘制与输入各自调用一次，结果一致。

## 6. 删除清单

| 文件 | 处理 |
|---|---|
| `StellarEraPlanet/Public|Private/PlanetCutawayComponent.*` | 删除，由 `PlanetMineralViewComponent` 取代 |
| `StellarEraPlanet/Public|Private/PlanetCutawayMath.*` | 删除 |
| `StellarEraPlanet/Public|Private/PlanetSectionImage.*` | 删除 |
| `StellarEraPlanet/Private/PlanetCutawayTests.cpp` | 删除，换成 `PlanetMineralBlocksTests.cpp` |
| `StellarEra/Private/CutawayLegendTests.cpp` | 删除，换成 `MineralLegendTests.cpp` |
| `AVoxelPlanetActor::GetCutawayComponent()` | 改名 `GetMineralViewComponent()` |
| `M_PlanetSection`、`MPC_CutawayOrigin/Normal` | 资产保留在磁盘，代码不再引用 |

`M_PlanetVertexColor` 里的 MPC 裁剪接线保留但不再写入（`Active` 恒为 0，等价于不裁剪）。

## 7. 测试

自动化（`-ExecCmds="Automation RunTests StellarEra"`）：

- `StellarEra.Planet.MineralBlocks.*`
  - 所有方块都在球内（`|p| <= R`）；
  - `ElementIndex ∈ [0, N)`，且每个元素组内的方块归属与 `DominantIndex` 一致；
  - 同种子两次 Build 结果逐项相同；
  - 两份仅 `appearance.colorThreshold` 不同的地质场（高阈值那份）方块数不增加；
  - 空地质场 → 所有组为空且不崩溃。
- `StellarEra.StarMap.MineralLegend.*`
  - 行矩形数量 = 元素数，且互不重叠；
  - 点击行中心命中该行，行间空隙与面板外返回 `INDEX_NONE`；
  - 开关态文本包含 `开` / `关`；
  - 空元素表只画标题、占位行与提示行。

PIE 实测（`UniverseMap`）：

1. 选中行星按 H：行星消失、玻璃球出现、球内能看到分层矿物块；
2. 点击图例与数字键 1..6：对应元素整组显隐，其余不受影响；0 全部恢复；
3. 观察期间天体在 N 体下移动，球壳与矿物块跟随，不漂移；
4. 再按 H：行星、大气、碰撞全部恢复；
5. 编辑器后台注意 3 FPS 限制：性能判断必须让编辑器在前台。

## 8. 为什么不用「真实地形半透明」

`AVoxelWorld::OnApplyObjectToActor` 在材质变化时调用 `RecreateRender()`，整颗行星的
渲染数据会重建。对一个只是按一下 H 的观察模式来说，进出各卡 1–3 秒不可接受。
代理球壳没有这个问题，且视觉上更接近用户要的「玻璃球」。

## 9. 风险与兜底

| 风险 | 兜底 |
|---|---|
| `SetActorHiddenInGame` 对体素渲染不生效，或 chunk 重建后自己冒出来 | 退回全局 MPC 全遮：把 `Origin` 放到相机反方向 4R、球半径写 8R，让所有体素 `Mask = 0`；该遮罩只覆盖目标行星附近，不影响别的天体 |
| 超大行星 / 加密集采样导致进入卡顿 | Build 日志输出 `SampleCount` 与方块数；必要时把采样拆到异步任务并先显示球壳 |
| 透明球壳与不透明方块排序异常 | 球壳只有一层、方块不透明；如仍异常，球壳材质关闭「响应深度测试写入」即可 |
| 元素数量超过 9，数字键覆盖不全 | 数字键只覆盖前 9 个；其余元素仍可点击图例切换 |
