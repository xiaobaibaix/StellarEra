# 矿物透视观察（H 键）设计

- 日期：2026-10-25
- 分支：`feat/planet-character-spaceship`
- 替代：`docs/superpowers/specs/2026-10-24-mineral-cutaway-editor-design.md`（切开半球那套）
- 修订：2026-10-25 —— 矿物几何由「稀疏方块」改为「每元素等值面（marching cubes）」，
  见 §2 / §4.2 / §5.2；方块路径的代码与测试一并删除

## 1. 目标

星图里选中体素行星后按 **H**，把行星变成一颗**半透明球壳**，同时把地质场里的矿物
以**不透明矿体**（每元素一张等值面网格）的形式显示在球体内部；观察者可以按元素筛选，
只看一个、几个或全部矿物。再按 H 退出，一切恢复原样。

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

因此本模式在进入时按地质场**解析采样**一张三维标量场，再对每个元素抽等值面，
在球内生成成团的矿体。场的数据源（`FPlanetGeologyField` 的每元素强度 `ElementNorm`）、
深度定义（基准球面为 0、向内为正、单位体素）与地表着色完全一致，所以矿体位置与
地表矿脉是同一套判据。

第一版曾用「稀疏方块」近似（每元素一组 ISM 立方体，边长只有采样间距的 0.45 倍），
PIE 里看成一粒粒彼此分离的小正方体，不符合「矿脉应是一团一团」的预期，已整体替换。

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

### 4.2 矿物矿体（每元素等值面）

- 标量场：`f_i(方向, 深度) = ElementNorm_i` ∈ [0,1] —— 该元素各矿带的
  `Gaussian × 脉强度` 取最大值。`Accumulate` 内部本来就算这个量，只是此前只暴露
  最大值 `DominantNorm`；本次新增按元素的公开出口（§5.2）。
- 等值线：`Appearance.ColorThreshold`（earth 预设 0.05），与地表着色共用同一份参数，
  不新增可调项。
- 网格：以球心为中心、边长 `2 × SurfaceRadiusCm` 的立方体，铺 **40³** 均匀格
  （41³ = 68,921 个角点）。球外（深度 < 0）与深度 > `MaxReachDepth` 的点直接置 0，
  不花噪声求值。
- 抽取：先一次遍历算出所有元素的场值，再逐元素跑
  `UE::Geometry::FMarchingCubes`（`GeometryCore` 运行时模块）——`Implicit` 走网格
  三线性插值、`bEnableValueCaching = false`、`bParallelCompute = true`。
- **各元素场独立取面**：两种元素同时高于门限的少数地点，两块矿体会互相穿插，
  视作共生矿脉。不采用 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
- 渲染：每个元素一个 `UDynamicMeshComponent`（`GeometryFramework` 运行时模块），
  设 `NoCollision`、`CastShadow = false`；顶点直接用世界厘米坐标（相对球心），
  与球壳同一坐标系，沿用「行星 Actor 等比缩放 S = 1」假设。
- 材质 `M_MineralBlock`：仍为 Unlit、`BlockColor` 向量 → Emissive，每元素一个 MID，
  颜色 = 元素 `Color`，`Brightness` 1.4。为了在纯色下看出体积，Emissive 乘一个轻量
  法线项 `(0.6 + 0.4 × saturate(dot(N, V)))`，整体亮度量级不变。
- 进入耗时预算：**首进 ≤ 0.3 秒**（40³、earth 6 元素量级，预计 0.1–0.3 s）。
  结果按（行星弱指针、`SurfaceRadiusCm`、行星 `GetActorScale3D()`）缓存在组件上，
  同一颗行星第二次进入直接复用；三者任一变化则重算。

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
- 鼠标左键点击元素行切换；数字键 `1..9` 切换前 9 行（顺序 = 图例行顺序 =
  `GetSampledElementDefinitions()` 的运行期顺序，不是 JSON 元素表顺序）；
  `0` 全部打开。元素多于 9 个时，其余元素仍可点击图例切换。
- 观察模式下左键不再选中天体，只用于图例命中。

## 5. 架构

### 5.1 组件与 Actor

```
AVoxelPlanetActor
└─ UPlanetMineralViewComponent   (替换 UPlanetCutawayComponent)
       │ Activate 时 Spawn
       └─ APlanetMineralViewActor
            ├─ UStaticMeshComponent        球壳
            └─ UDynamicMeshComponent × N   每个元素一份矿体网格
```

- 观察 Actor **不能**挂在行星 Actor 下面：进入模式会隐藏行星 Actor，
  子组件会跟着一起隐藏。
- 观察 Actor 每帧把位置同步为行星位置；不复制旋转/缩放（N 体只平移）。
- 退出时 `Destroy()` 观察 Actor，同时恢复行星与大气。

### 5.2 等值面构建器（纯函数）

`Source/StellarEraPlanet/Public/PlanetMineralSurface.h`：

```cpp
struct FPlanetMineralMesh
{
    TArray<FVector3f> VerticesCm;   // 相对球心
    TArray<int32> Triangles;
    TArray<FVector3f> Normals;
    int32 OccupiedCells = 0;        // 场值高于等值线的格数，供日志
};

namespace PlanetMineralSurface
{
    struct FBuildParams
    {
        float SurfaceRadiusCm = 0.f;
        float VoxelSizeCm = 0.f;
        int32 GridResolution = 40;
        float IsoValue = 0.f;       // 调用方传 Field.GetAppearance().ColorThreshold
    };

    struct FBuildResult
    {
        // 下标与 Field.GetSampledElementDefinitions() 对齐；空元素保留空网格
        TArray<FPlanetMineralMesh> MeshesByElement;
        int32 GridPoints = 0;
        double BuildSeconds = 0.0;
    };

    void Build(const FPlanetGeologyField& Field, const FBuildParams& Params, FBuildResult& Out);
}
```

算法：

1. `Bounds` = 球心为中心、边长 `2 × SurfaceRadiusCm` 的立方体；
   `CubeSize = 2 × SurfaceRadiusCm / GridResolution`。
2. 在 `(GridResolution + 1)³` 个角点上并行（`ParallelFor`）求出所有元素的 `ElementNorm`，
   存成 `[点][元素]` 的稠密数组；`|p| > R` 或 `Depth > MaxReachDepth` 的点按 0 处理，
   不调噪声。
3. 每个元素用同一张稠密网格跑 `FMarchingCubes`：`IsoValue = Params.IsoValue`、
   同一 `Bounds` / `CubeSize`、`bEnableValueCaching = false`、`bParallelCompute = true`；
   `Implicit` 只做网格三线性插值，不再调地质场。
4. 按元素下标收集网格；空元素照样占一个空槽，保证下标与图例、数字键对齐。
5. `OccupiedCells` = 该元素场值高于等值线的角点数；`BuildSeconds` 只进日志。

地质场新增按元素的公开出口（内部由现有 `Accumulate` 一次循环填充，旧调用点不变）：

```cpp
void SampleElementNorms(const FVector& UnitDir, double Depth, TArray<float>& OutNorms) const;
```

默认参数下 41³ = 68,921 个角点 × 元素数（earth = 6）次噪声求值，预计 20–40 ms；
40³ 上的 MC 逐元素并行，整体在 0.1–0.3 s 量级。单次构建的场数组约 1.6 MB，构建结束即释放。

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
- 构建结果按（行星弱指针、`SurfaceRadiusCm`、行星 `GetActorScale3D()`）缓存；
  `Deactivate` 不清缓存（再进即时出图），`EndPlay` 随组件销毁。
- `EndPlay` 兜底 `Deactivate`。

### 5.4 输入与 UI

- `StellarEraPlayerController`：
  - `IA_CutawayToggle` 改名为 `IA_MineralViewToggle`，仍映射 H；
  - `IA_CutawayDrag` 删除，`Mouse2D` 不再分派给剖面；
  - 新增 `IA_MineralFilter1..9`（数字键 1..9）与 `IA_MineralShowAll`（0）；
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
| `StellarEraPlanet/Private/PlanetCutawayTests.cpp` | 删除，换成 `PlanetMineralSurfaceTests.cpp` |
| `StellarEra/Private/CutawayLegendTests.cpp` | 删除，换成 `MineralLegendTests.cpp` |
| `AVoxelPlanetActor::GetCutawayComponent()` | 改名 `GetMineralViewComponent()` |
| `M_PlanetSection`、`MPC_CutawayOrigin/Normal` | 资产保留在磁盘，代码不再引用 |
| `PlanetMineralBlocks.*` 与 `PlanetMineralBlocksTests.cpp` | 2026-10-25 修订后由 `PlanetMineralSurface.*` 与 `PlanetMineralSurfaceTests.cpp` 取代并删除 |

`M_PlanetVertexColor` 里的 MPC 裁剪接线保留但不再写入（`Active` 恒为 0，等价于不裁剪）。

## 7. 测试

自动化（`-ExecCmds="Automation RunTests StellarEra"`）：

- `StellarEra.Planet.MineralSurface.*`
  - 所有顶点都在球内（`|p| <= R`）且深度落在 `[0, MaxReachDepth]`；
  - 顶点处的 `ElementNorm` 等于等值线（三线性插值容差内）——证明抽的是那条等值面；
  - 结果下标与 `GetSampledElementDefinitions()` 对齐（用「JSON 数组顺序 ≠ 哈希顺序」的夹具）；
  - 两份仅 `appearance.colorThreshold` 不同的地质场（高阈值那份）总三角形数不增加；
  - 空地质场 / 全部门限以下 → 成功但零网格，不崩溃；
  - 同参数两次 Build 顶点集一致（并行不影响确定性）。
- `StellarEra.StarMap.MineralLegend.*`
  - 行矩形数量 = 元素数，且互不重叠；
  - 点击行中心命中该行，行间空隙与面板外返回 `INDEX_NONE`；
  - 开关态文本包含 `开` / `关`；
  - 空元素表只画标题、占位行与提示行。

PIE 实测（`UniverseMap`）：

1. 选中行星按 H：行星消失、玻璃球出现、球内能看到成团的不规则矿体（不再是分离的立方体）；
2. 点击图例与数字键 1..6：对应元素的整份矿体显隐，其余不受影响；0 全部恢复；
3. 观察期间天体在 N 体下移动，球壳与矿体跟随，不漂移；
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
| 超大行星 / 加密网格导致进入卡顿 | 日志输出角点数、每元素占据格数与三角形数、构建秒数；必要时降到 32³ 或把构建拆到异步任务并先显示球壳 |
| 透明球壳与不透明矿体排序异常 | 球壳只有一层、矿体不透明；如仍异常，球壳材质关闭「响应深度测试写入」即可 |
| 40³ 下矿体边缘仍有棱角感 | 提高 `GridResolution`（56³ 约 0.3–0.8 s）或对网格做一次 Laplacian 平滑；缓存机制不变 |
| 元素数量超过 9，数字键覆盖不全 | 数字键只覆盖前 9 个；其余元素仍可点击图例切换 |
