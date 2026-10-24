# 星图大尺度行星编辑（挖掘 / 填充）— 设计

- **日期**：2026-10-24
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-22-planet-edit-mode-design.md`（角色的编辑模式、`UPlanetEditComponent`、存档链路）

---

## 1. 目标

在星图模式下对体素行星做**大尺度**挖掘与填充：

| # | 需求 |
|---|---|
| 1 | 星图模式内可进入编辑模式，左键挖、`Shift`+左键填 |
| 2 | 笔刷尺寸 8–128 体素（直径 / 边长），默认 32 |
| 3 | 编辑写入体素数据的 **LOD0（最大细分层）**，与渲染 LOD 无关 |
| 4 | 复用角色的编辑组件：把 `UPlanetEditComponent` 通用化，两个 Pawn 共用一套逻辑 |
| 5 | 复用现有存档链路，不新增资产格式 |

用户原话：

> 对了，星图模式下也搞一个挖掘和填充功能，且尺度更大，但是编辑的是lod最大细分层

代码落点由用户选定：**方案 B —— 通用组件**，不另起一个星图专用的编辑组件。

## 2. 已定决策

| # | 问题 | 选择 | 出处 |
|---|---|---|---|
| 1 | 代码落点 | **通用 `UPlanetEditComponent`，角色与星图 Pawn 各挂一份** | 用户选定（方案 B） |
| 2 | 笔刷尺寸 | **8–128 体素，默认 32**，`[` / `]` 调整 | 用户选定 |
| 3 | 挖 / 填键位 | **左键挖；`Shift`+左键填** | 用户选定 |
| 4 | 编辑距离 | **不设额外上限**；射线打到哪颗行星就编辑哪颗。组件距离上限与相机最远距离同量级（50 AU） | 用户选定 |
| 5 | 是否要求先选中 | **不要求**；选中只影响相机焦点 | 用户选定 |
| 6 | 滚轮 | 星图里仍是**缩放**（要拉近才看得见编辑结果） | 与现有星图输入一致 |
| 7 | 右键 | 仍是绕焦点旋转，不参与编辑 | 与现有星图输入一致 |
| 8 | 笔刷形状切换 | 沿用现有 HUD 两个按钮 | 与角色编辑一致 |
| 9 | 连发 | 一次点击一次编辑，不做按住连刷 | YAGNI |

## 3. 非目标

- 不做撤销 / 重做。
- 不做材质笔刷、平滑、平整等插件其它工具。
- 不做编辑时的行星表面网格基线（角色编辑那套 `UPlanetSurfaceGridComponent` 不在星图里显示；星图尺度下 5° 网格步长过大，是另一个 UX 决策）。
- 不改 `Plugins/VoxelFree/` 与 `Plugins/VibeUE/` 中任何文件。
- 不改角色的编辑手感：滚轮调尺寸、右键点放、1–8 体素全部保持。

## 4. 模块与文件

依赖方向不变：`StellarEra → StellarEraVehicle → StellarEraPlanet → Voxel`。

| 模块 | 文件 | 改动 |
|---|---|---|
| `StellarEraVehicle` | `Public/PlanetEditComponent.h`、`Private/PlanetEditComponent.cpp` | **通用化**：目标解析改成可注入的委托，新增尺寸区间 / 多线程 / 提示文案配置、`TryResolveTarget` |
| `StellarEraVehicle` | `Public/PlanetBrushApplicator.h`、`Private/PlanetBrushApplicator.cpp` | **新增**：把球 / 盒体素工具调用从组件里抽出的共享执行器 |
| `StellarEraVehicle` | `Public/PlanetEditBrush.h`、`Private/PlanetEditBrush.cpp` | 新增 `ClampSize(Size, Min, Max)` 重载 |
| `StellarEraVehicle` | `Private/PlanetCharacter.cpp` | `BeginPlay` 里给组件装「角色射线」目标解析器 |
| `StellarEraPlanet` | `Public/VoxelPlanetActor.h`、`Private/VoxelPlanetActor.cpp` | 新增 `RaycastSurfaceCm`（解析射线–球 + 体素数据修正） |
| `StellarEraPlanet` | `Public/PlanetSurfaceRay.h`、`Private/PlanetSurfaceRay.cpp` | **新增**：可变半径球面求交的纯函数，可无头测试 |
| `StellarEra` | `Public/StarMapCameraPawn.h`、`Private/StarMapCameraPawn.cpp` | 加一份 `UPlanetEditComponent`；`BeginPlay` 装「最近行星射线」目标解析器；`GetPlanetEditComponent()` |
| `StellarEra` | `Public/StellarEraPlayerController.h`、`Private/StellarEraPlayerController.cpp` | G 键分派、左键编辑分支、`[`/`]` 输入、退出星图时强制退出编辑 |
| `StellarEra` | `Public/StellarEraHUD.h`、`Private/StellarEraHUD.cpp` | 星图 Pawn 的编辑面板也画（复用现有布局） |
| `StellarEra` | `Private/PlanetEditTests.cpp` | 新增自动化测试 |

`StellarEra` 已私有依赖 `StellarEraPlanet` 与 `StellarEraVehicle`，`StellarEraVehicle` 已私有依赖 `StellarEraPlanet` 与 `Voxel`，**无需改 Build.cs**。

## 5. 通用编辑组件

### 5.1 唯一差异：怎么找到编辑目标

角色和星图只有「屏幕射线打到哪」不同，其余（模式、笔刷、预览、写入、存档通知）完全一样。所以把这一点抽成注入项，其余保持现成实现：

```cpp
/** 一次屏幕射线解析出的编辑目标。 */
struct FPlanetEditTarget
{
    AVoxelPlanetActor* Planet = nullptr;
    FVector HitLocation = FVector::ZeroVector;
    FVector HitNormal = FVector::UpVector;
};

/** 射线 → 目标。返回 false = 没打到可编辑的表面。 */
using FPlanetEditTargetResolver = TFunction<bool(
    const FVector& RayOrigin, const FVector& RayDirection, float MaxDistanceCm,
    FPlanetEditTarget& OutTarget)>;
```

组件新增：

| 成员 | 默认 | 说明 |
|---|---|---|
| `SetTargetResolver(...)` | 空 | 由 Owner 在 `BeginPlay` 装；空 = 永不命中 |
| `TryResolveTarget(ScreenPosition, Out)` | — | 屏幕反投影 + 调解析器。**不写体素**，供控制器判断「这次点击该不该归编辑管」 |
| `MinBrushSizeVoxels` | 1 | 角色 CDO 用默认；星图 Pawn 构造里设 8 |
| `MaxBrushSizeVoxels` | 8 | 星图 Pawn 构造里设 128 |
| `bMultiThreadedEdits` | false | 星图 Pawn 构造里设 true |
| `PlaceActionHint` | `右键点放` | HUD 提示行用；星图设 `Shift+左键填` |

`AdjustBrushSize` 改用 `PlanetEditBrush::ClampSize(Size, MinBrushSizeVoxels, MaxBrushSizeVoxels)`。`BrushSizeVoxels` 的 `ClampMax=8` 元数据删掉，改为运行时按上面两个字段夹取。

`TraceEditTarget` 改成：

1. `PlayerController->DeprojectScreenPositionToWorld` 得到射线；
2. 调 `TargetResolver(Origin, Dir, MaxEditDistanceCm, Out)`；
3. 不再自己 `LineTrace`，也不再 `Cast<APlanetCharacter>`。

`ResolvePlanet()` 删除；`ApplyBrushAtScreenPosition` 改用解析器给出的 `FPlanetEditTarget`。`EndPlay` 里把解析器清空，避免 Owner 已销毁时残留捕获。

### 5.2 角色侧

`APlanetCharacter::BeginPlay`：

```cpp
PlanetEdit->SetTargetResolver([this](const FVector& Origin, const FVector& Dir,
    float MaxCm, FPlanetEditTarget& Out)
{
    return ResolveEditTargetByTrace(Origin, Dir, MaxCm, Out);
});
```

`ResolveEditTargetByTrace` 是角色新增的私有方法，内容就是现在组件里的 `TraceEditTarget` 的射线部分：先 `ECC_Visibility`，完全没有命中再补 `ECC_WorldStatic`；命中 Actor 必须是 `PlanetActor`；`MaxEditDistanceCm` 语义不变。

行为与现有版本逐条一致，唯一变化是「屏幕反投影」从组件内部提到解析器之前（同一个函数里）。

### 5.3 星图侧

`AStarMapCameraPawn`：

- 构造函数里 `CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"))`，并设：
  `MinBrushSizeVoxels=8`、`MaxBrushSizeVoxels=128`、`BrushSizeVoxels=32`、
  `bMultiThreadedEdits=true`、`PlaceActionHint=TEXT("Shift+左键填")`、
  `MaxEditDistanceCm=5.0e7`（50 AU，与相机最远距离同量级）。
- `BeginPlay` 里装解析器 → 私有 `ResolveEditTargetByPlanets`：

```cpp
// 遍历 UCosmosSimulationSubsystem::GetRegisteredBodies，只取 AVoxelPlanetActor，
// 逐颗 RaycastSurfaceCm，取距离最近的一次命中。
```

- 仍不 Tick。组件的 Tick 只在编辑模式打开（`bStartWithTickEnabled=false`，进入时启用，`TickGroup=TG_PostUpdateWork`），Pawn 自身的 CalcCamera 零滞后特性不受影响。
- 新增 `UPlanetEditComponent* GetPlanetEditComponent() const;`（**声明在头、定义在 .cpp**，理由同 `APlanetCharacter`：头里只有前向声明）。

星图相机在 50 m 最低高度时，细调用器的碰撞范围只有 30 m（`VoxelInvoker->CollisionsRange = 3000` 厘米），**这个高度上没有任何碰撞体**，所以星图解析器不能用 `LineTrace`，必须用第 6 节的表面射线。

## 6. 行星表面射线

### 6.1 `AVoxelPlanetActor::RaycastSurfaceCm`

```cpp
bool RaycastSurfaceCm(
    const FVector& RayOriginWorld,
    const FVector& RayDirectionWorld,
    float MaxDistanceCm,
    FVector& OutHitLocationWorld,
    FVector& OutHitNormalWorld,
    float& OutDistanceCm) const;
```

算法：

1. 世界 → 行星局部：`GlobalToLocalFloat`（等比缩放，无旋转；方向归一化后不变）。`IsCreated()` 为假直接返回 false。
2. 解析初值：`PlanetSurfaceRay::SolveAnalyticHit` 用 `GetSurfaceRadiusCmAtDirection` 作为「方向 → 半径」采样器，固定点迭代 3 次。地形是径向位移，收敛快；切向射线不收敛时退回基准球命中。相机在表面球内 → false。
3. 体素修正：从解析交点沿射线走。步长 1 体素，先在初值处采样 `GetValue(P, 0).IsEmpty()`：
   - 空 → 向**外**走找第一个实体（挖过的坑：解析面在真实坑底之上）；
   - 实 → 向**内**走找第一个空（填过的地形：真实表面在解析面之上）。
   最多 ±128 体素（最大笔刷半径 64 + 余量），找到翻转后二分 4 次。
4. 整个搜索区间套 `FVoxelReadScopeLock`，边界由「射线起点 + 终点」的体素包围盒外扩 2 格得到（与 `PlanetSurfaceGridComponent::RebuildCache` 同一模式）。
5. 法线 = 命中点的径向单位方向。地表本来就是径向位移；生成器不暴露解析梯度，重新做 6 点中心差分不值得。

`MaxDistanceCm` 只用于在 `OutDistanceCm > MaxDistanceCm` 时判失败；搜索本身由解析命中点界定。

### 6.2 纯函数 `PlanetSurfaceRay::SolveAnalyticHit`

```cpp
bool SolveAnalyticHit(
    const FVector& Origin, const FVector& UnitDirection, const FVector& Center,
    float BaseRadiusCm,
    TFunctionRef<float(const FVector& UnitDirectionFromCenter)> RadiusAtDirection,
    float& OutDistanceCm);
```

只做「射线 vs 半径随方向变化的球面」的固定点迭代，不碰 Actor、不碰体素。无头测试直接喂常量半径与扰动半径两个采样器。

### 6.3 为什么不用屏幕拾取

`PickBodyUnderScreenPosition` 按**屏幕距离容差**选，不保证射线真的穿过该天体，也不处理遮挡。编辑必须精确到表面，所以星图解析器独立地遍历所有体素行星、按射线命中距离取最近的一颗。

## 7. 输入与 HUD

### 7.1 输入

| 输入 | 星图（编辑模式） | 角色（编辑模式，不变） |
|---|---|---|
| `G` | 进 / 出编辑模式 | 进 / 出编辑模式 |
| 左键 | 挖 | 挖 |
| `Shift` + 左键 | 填 | — |
| 右键 | 绕焦点旋转 | 按住预览填，松开点放 |
| 滚轮 | 缩放 | 笔刷尺寸 |
| `[` / `]` | 笔刷尺寸 | 笔刷尺寸（新增的等价入口） |
| HUD 球 / 方块按钮 | 切形状 | 切形状 |

控制器改动：

- `GetEditComponentInEditMode()` **通用化**为「当前 Pawn 的编辑组件，且处于编辑模式」。
- **新增** `GetCharacterEditComponentInEditMode()`，`OnLookActivateStarted/Completed`（右键）改用它。**这是本次最容易踩的坑**：直接把通用的取到右键分支里，星图编辑模式下按右键会变成「点放」而抢掉绕轨。
- `OnEditModeToggle`：角色 / 星图 Pawn 都走 `ToggleEditMode()`。
- `OnStarMapSelectStarted`：编辑模式下先命中 HUD 笔刷按钮，再 `TryResolveTarget`：
  - 命中 → `Shift` 按下则 `PlaceAtScreenPosition`，否则 `DigAtScreenPosition`，**消费点击**；
  - 没命中 → 继续走现有「记录按下位置」逻辑，松开时照常拾取天体（编辑模式不剥夺选天体的能力）。
- 新增 `IA_BrushSize`（Axis1D）：`RightBracket` 为 +1，`LeftBracket` 加 `MakeNegate` 为 -1；只在编辑模式生效。
- 退出星图：`CurrentStarMapPawn->GetPlanetEditComponent()->SetEditMode(false)`，并隐藏预览线框。

### 7.2 HUD

`AStellarEraHUD::DrawHUD` 新增分支：当前 Pawn 是 `AStarMapCameraPawn` 且其编辑组件处于编辑模式时，调用现有 `DrawPlanetEditPanel`。

`DrawPlanetEditPanel` 已经是通用签名（只读 `GetBrushShape` / `GetBrushSizeVoxels`），只改提示行：

```cpp
FString::Printf(TEXT("笔刷 %d 体素 · 左键挖 / %s · G 退出"),
    Edit.GetBrushSizeVoxels(), *Edit.GetPlaceActionHint())
```

布局与命中继续用 `PlanetEditUI` 的纯函数，不新增 UI 资产。

## 8. 笔刷执行与性能

`PlanetBrushApplicator::Apply`：

```cpp
bool Apply(AVoxelPlanetActor& Planet, const FVector& CenterWorld,
    EPlanetBrushShape Shape, int32 SizeVoxels, bool bPlace, bool bMultiThreaded);
```

内容是从 `UPlanetEditComponent::ApplyBrushAtScreenPosition` 原样搬出的球 / 盒工具调用：

- 球：`UVoxelSphereTools::AddSphere` / `RemoveSphere`，半径 `GetHalfExtentCm`；
- 盒：`PlanetEditBrush::MakeBox` → `MakeVoxelBox` → `FVoxelIntBox`，`UVoxelBoxTools::AddBox` / `RemoveBox`；
- 固定参数：`bConvertToVoxelSpace=true`、`bUpdateRender=true`、`OutModifiedValues`；
- 返回 `OutModifiedValues.Num() > 0`。调用方只有返回 true 才 `Planet->NotifyVoxelEdited()`。

量级（直径 128 体素）：球约 110 万体素、盒 210 万体素 → 星图路径 `bMultiThreaded=true`；角色 1–8 体素保持 `false`，手感不变。

**写入永远是 LOD0 体素数据**（插件工具直接改数据层），与渲染 LOD 独立。星图相机的三档调用器是 LOD1 / LOD3 / LOD5（范围 80 m / 300 m / 1 km），所以：

- 编辑结果要拉近到地表附近才看得清；
- 128 体素的坑在 LOD1 下有 64 个渲染体素宽，8 体素的坑只有 4 个；
- 这是渲染 LOD 的既有行为，不通过「锁 LOD」绕过（锁定会牺牲整颗行星的渲染性能）。

## 9. 存档

完全复用：`NotifyVoxelEdited()` → 2 秒防抖 → `SaveEditsToSaveObject()` → `/Game/Saves/PlanetVoxel/<Map>/<ActorLabel>`。不新增任何资产格式，不改存档路径。

## 10. 测试

### 10.1 无头自动化（`StellarEra.*`）

- `StellarEra.Planet.EditBrush.ClampSizeRange`：`(0,8,128)→8`、`(999,8,128)→128`、`(32,8,128)→32`；`Min > Max` 时先归一化成 `Max = Min`，再夹（`(0,10,2)→10`、`(99,10,2)→10`）。
- `StellarEra.Planet.EditBrush.BoxBounds128`：`MakeBox` / `MakeVoxelBox` 在 Size=128 时 `GetSize()` 每轴恰好 128。
- `StellarEra.Planet.SurfaceRay.AnalyticHit`：
  - 常量半径：命中距离与解析解一致（容差 1e-3）；
  - 扰动半径：迭代后采样点半径与命中方向上的半径一致（容差 1e-3）；
  - 射线背离 / 完全在球外：返回 false。

现有 `StellarEra.Planet.EditBrush.ClampSize`（1–8 默认区间）与 `StellarEra.Planet.EditComponent.Defaults`（角色组件默认值）**必须继续通过**，作为「角色侧零回归」的机械证据。

### 10.2 PIE 手动（UniverseMap）

1. `M` 进星图 → `G` 进编辑 → 滚轮拉到离地最近 → 左键挖一次、`Shift`+左键填一次。
2. 等 2 秒防抖 → 退出星图 → 重新进星图 → 坑 / 堆还在。
3. 结束 PIE 再启动一次，确认存档跨会话有效。
4. 对着另一颗行星重复一次，确认「射线打到谁就编辑谁」。
5. 星图编辑模式下按右键，确认是绕轨旋转而不是填。
6. 左键点空白，确认仍是选中 / 取消选中的原有行为。
7. 角色编辑模式回归：滚轮 1–8、右键点放、预览与网格不变。

无头 runner 跑的是 editor world，`BeginPlay` 不会执行，星图 Pawn 的解析器装配只能在 PIE 里验。

## 11. 风险与已知坑

- **右键分支必须留在角色**：见 §7.1，通用化 `GetEditComponentInEditMode()` 后右键两个处理函数必须用角色专用访问器，否则星图编辑模式下右键会填地形。
- **解析半径不含编辑**：`GetSurfaceRadiusCmAtDirection` 只看生成器，不知道挖填。影响两处：相机最低高度始终相对原始地表（挖出 64 m 深坑后相机仍停在地表上方 50 m，能看到坑底，但下不去）；`ComputeOrbitDistanceLimitsCm` 的防穿地也按原始地表算。本设计接受这两点，不在本次修。
- **`GetValue` 可能触发按需生成**：编辑远处的行星时首次查询会同步生成该区块，可能卡一帧。编辑本来就要靠近才能看见，接受。
- **锁**：表面射线与体素工具都会锁数据；射线用 `FVoxelReadScopeLock`，工具内部自行加锁，二者不会同时持有。
- **中文源文件**：新增 / 修改的 `.h` / `.cpp` 必须存成 UTF-8 **带 BOM**。
- **构建**：关编辑器，用 CLAUDE.md 里的捆绑 dotnet 直调 UBT；构建前先查 DLL 时间戳（本项目有过「代码没编译就调试」的前科）。

## 12. 验收标准

- [ ] 星图 `G` 可进 / 出编辑模式；退出星图自动退出编辑。
- [ ] 左键挖、`Shift`+左键填；右键仍是绕轨；滚轮仍是缩放。
- [ ] 笔刷 8–128 体素，默认 32，`[` / `]` 可调；HUD 按钮可切球 / 方块。
- [ ] 写入 LOD0 数据；拉近到地表后能看见坑 / 堆。
- [ ] 编辑后 2 秒写入行星存档；退出重进、重启 PIE 后仍在。
- [ ] 角色编辑模式行为零回归（滚轮尺寸、右键点放、表面网格、存档）。
- [ ] 自动化测试全过；构建零警告（本项目警告即错误）。
