# 行星编辑模式（挖掘 / 添加）— 设计

- **日期**：2026-10-22
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-19-units-design.md`（世界尺度换算）、`2026-10-21-lod-transition-optimization-design.md`（调用器与体素 LOD）

---

## 1. 目标

角色状态下用鼠标对体素行星做**挖掘**与**添加**：

| # | 需求 |
|---|---|
| 1 | 笔刷只支持**球体**与**正方体**两种 |
| 2 | 屏幕 UI 上放两个图标按钮，鼠标点击切换笔刷 |
| 3 | 按 **G** 进入编辑模式；**只有**在这个模式下才显示这两个按钮 |
| 4 | 编辑模式下给球体加一层「戴森球计划」式的表面网格基线 |

用户原话：

> 好的，你现在可以给在角色状态下，鼠标对星球的挖掘和添加功能？现在暂时只只支持球体和正方体，
> 选择方式通过ui界面添加两个图标按钮，分别表示球体和正方体，按g键进入这个模式，才会显示这个按钮图标，
> 且进入这个模式以后，看看是否可以给球体添加一个类似戴森球计划的那种表面网格基线？

## 2. 已定决策

| # | 问题 | 选择 | 出处 |
|---|---|---|---|
| 1 | 鼠标映射 | **左键挖；右键「点击」放；右键按住拖动仍是转视角** | 用户选定 |
| 2 | 笔刷尺寸 | **滚轮调，默认 2 体素，范围 1–8** | 用户选定 |
| 3 | 网格样式 | **贴合地形高度**（含体素编辑后的真实表面） | 用户选定 |
| 4 | 持久化 | 写入行星的 **`SaveObject`** | 用户选定 |
| 5 | 代码落点 | **方案 B：`UPlanetEditComponent` 挂在角色上** | 用户选定 |
| 6 | UI 技术 | 纯 C++ Canvas，沿用现有 `AHUD`；不建 UMG 资产 | 与现有 HUD 一致 |
| 7 | 方块朝向 | 轴对齐行星体素空间（不随命中法线旋转） | 与体素地形一致 |
| 8 | 连发 | 单击一次编辑一次；不做按住连发 | YAGNI |

## 3. 非目标

- 不做撤销/重做（不启用 `bEnableUndoRedo`）。
- 不做材质笔刷、平滑、平整等插件自带的其它工具。
- 不做多人同步。
- 不做编辑时的笔刷预览高亮（只在命中失败/成功时保持静默）。
- 不改 `Plugins/VoxelFree/` 与 `Plugins/VibeUE/` 中任何文件。

## 4. 模块与文件

依赖方向不变：`StellarEra → StellarEraVehicle → StellarEraPlanet → Voxel`。

| 模块 | 文件 | 改动 |
|---|---|---|
| `StellarEraVehicle` | `Public/PlanetEditComponent.h`、`Private/PlanetEditComponent.cpp` | **新增** `UPlanetEditComponent`（模式、笔刷、射线、执行） |
| `StellarEraVehicle` | `Public/PlanetEditBrush.h`、`Private/PlanetEditBrush.cpp` | **新增** 无状态纯函数：球心偏移、方块体素盒、尺寸夹取 |
| `StellarEraVehicle` | `Public/PlanetCharacter.h`、`Private/PlanetCharacter.cpp` | 新增组件成员并在构造函数里创建 |
| `StellarEraPlanet` | `Public/PlanetSurfaceGridComponent.h`、`Private/PlanetSurfaceGridComponent.cpp` | **新增** 网格采样、缓存、逐帧重画 |
| `StellarEraPlanet` | `Public/VoxelPlanetActor.h`、`Private/VoxelPlanetActor.cpp` | 新增网格组件、`SetEditGridVisible`、`NotifyVoxelEdited`、`SaveEditsToSaveObject` |
| `StellarEra` | `Public/StellarEraPlayerController.h`、`Private/StellarEraPlayerController.cpp` | G 键、左右键分派、滚轮分派、按钮命中 |
| `StellarEra` | `Public/StellarEraHUD.h`、`Private/StellarEraHUD.cpp` | 编辑模式面板 |
| `StellarEra` | `Public/PlanetEditUI.h`、`Private/PlanetEditUI.cpp` | **新增** 按钮布局与命中纯函数 |
| `StellarEra` | `Private/PlanetEditTests.cpp` | **新增** 自动化测试 |

`StellarEraVehicle.Build.cs` 已私有依赖 `Voxel`（调用器用），本次复用，无需新增依赖。

## 5. 编辑模式与输入

### 5.1 状态

状态全部在 `UPlanetEditComponent`：

- `bEditMode`（默认 false）
- `EPlanetBrushShape BrushShape`（`Sphere` / `Cube`，默认 Sphere）
- `int32 BrushSizeVoxels`（默认 2，夹到 [1, 8]）

HUD 与控制器都通过角色身上的组件读，不复制状态。

### 5.2 进入 / 退出

| 事件 | 行为 |
|---|---|
| 按 G（Possess 的是 `APlanetCharacter`） | `ToggleEditMode()` |
| 按 G（飞船 / 星图相机） | 忽略 |
| 上船（`BoardShip`） | 强制 `SetEditMode(false)` |
| 进星图（`ToggleStarMap` 进入分支） | 强制 `SetEditMode(false)` |
| 换 Pawn（`OnPossess`） | 新 Pawn 不是该角色则状态自然失效；旧角色不保留 UI |

进入时：显示该角色 `PlanetActor` 的表面网格。
退出时：隐藏网格。

### 5.3 鼠标射线

```
ScreenPos (double,double) ← PlayerController::GetMousePosition()
WorldOrigin, WorldDir    ← DeprojectScreenPositionToWorld(ScreenPos)
End                      ← WorldOrigin + WorldDir * MaxEditDistanceCm
LineTraceSingleByChannel(ECC_Visibility)
```

- 命中 Actor **必须等于**角色当前的 `PlanetActor`，否则视为未命中（前方的飞船、角色自己、其它 Actor 都挡编辑）。
- `MaxEditDistanceCm` 默认 `1.0e5`（1000 m）：行星直径量级为百米，足够覆盖任意可瞄准的表面。
- 若 `ECC_Visibility` 无命中，再尝试 `ECC_WorldStatic`（体素碰撞的 `CollisionPresets` 由关卡里配置，两条通道取更近的行星命中）。两次都失败 → 本次操作无效。

### 5.4 按键分派

| 输入 | 动作 | 编辑模式下的差别 |
|---|---|---|
| `IA_StarMapSelect`（左键）Started | 星图：记录按下位置 | 先做按钮命中；未命中按钮则立即挖一次 |
| `IA_StarMapSelect` Completed | 星图：点击拾取 | 无动作 |
| `IA_LookActivate`（右键）Started | `bLookHeld = true` | 额外记录按下位置 |
| `IA_LookActivate` Completed | `bLookHeld = false` | 若松开位移 ≤ `ClickMoveThresholdPx`（沿用现有 5 px）且按下点在行星上 → 放一次 |
| `IA_Zoom`（滚轮） | 相机缩放 | 改 `BrushSizeVoxels`（`Value > 0` +1，否则 -1） |

按钮命中优先于挖掘：左键落在图标上只切笔刷，不挖。

## 6. 笔刷几何（`PlanetEditBrush` 纯函数）

### 6.1 尺寸

`BrushSizeVoxels` 是**直径/边长**（体素数）。体素边长取行星的 `VoxelSize`（默认 100 cm）。

```
RadiusCm    = BrushSizeVoxels * VoxelSize / 2        // 球体半径
HalfSideCm  = BrushSizeVoxels * VoxelSize / 2        // 方块半边长
```

默认 2 → 球半径 1 体素、方块边长 2 体素。

### 6.2 球体

| 操作 | 中心 | 调用 |
|---|---|---|
| 挖 | `Hit.Location` | `UVoxelSphereTools::RemoveSphere(Planet, Center, RadiusCm, ...)` |
| 放 | `Hit.Location + Hit.Normal * RadiusCm` | `UVoxelSphereTools::AddSphere(Planet, Center, RadiusCm, ...)` |

放球时球心沿法线外移一个半径，球体与地表相切而非半埋。

### 6.3 正方体

方块用轴对齐的 `FVoxelIntBox`，坐标在行星体素空间：

```
LocalCV     = Planet->GlobalToLocalFloat(CenterWorld)   // FVoxelVector，单位：体素（可为小数）
CenterVoxel = 逐分量 RoundToInt(LocalCV)
Low         = (BrushSizeVoxels - 1) / 2      // 整数除法
High        = BrushSizeVoxels - 1 - Low
Min         = CenterVoxel - Low
Max         = CenterVoxel + High
Bounds      = FVoxelIntBox(Min, Max)         // Min / Max 两个端点都含
```

**不能用 `CenterVoxel ± Size/2`**：`FVoxelIntBox` 的 Min 与 Max 都含在范围内，尺寸 2 会算出 3 个体素。上面的 Low/High 让尺寸 2 得到 `Low=0, High=1`（恰好 2 格），尺寸 1 退化为 1×1×1，尺寸 8 得到 `Low=3, High=4`（恰好 8 格）。

| 操作 | 中心 | 调用 |
|---|---|---|
| 挖 | `Hit.Location` | `UVoxelBoxTools::RemoveBox(Planet, Bounds, ...)` |
| 放 | `Hit.Location + Hit.Normal * HalfSideCm` | `UVoxelBoxTools::AddBox(Planet, Bounds, ...)` |

所有工具调用：`bMultiThreaded = false`、`bUpdateRender = true`。渲染与碰撞由插件自动刷新。

### 6.4 尺寸夹取

`ClampBrushSize(int32)`：夹到 `[1, 8]`。滚轮直接改当前值，不缓存累积量。

## 7. HUD 按钮（`PlanetEditUI` 纯函数 + `AStellarEraHUD` 绘制）

### 7.1 布局

- 视口底部居中，两个按钮横向排列。
- 按钮边长 `48 px`，间距 `8 px`，距底边 `40 px`。
- 顺序：先球体、后正方体。
- 按钮下方一行提示：`笔刷 {N} 体素 · 左键挖 / 右键放 · G 退出`。

`PlanetEditUI` 提供的纯函数：

```cpp
// 返回两个按钮的左上角坐标（像素）。
void GetBrushButtonRects(const FVector2D& ViewportSize,
                         FVector2D& OutSphereRect, FVector2D& OutCubeRect);

// 命中返回 true 并写按钮下标（0 = 球体，1 = 正方体）；边界含（压线算命中）。
bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
                        int32& OutButtonIndex);
```

**参数用 `int32` 下标而不是 `EPlanetBrushShape`**：`PlanetEditUI.h` 是 `StellarEra` 的公开头，而 `EPlanetBrushShape` 定义在 `StellarEraVehicle`；后者是 `StellarEra` 的**私有**依赖，公开头引用它就得把依赖提升为公开，整个层级都要改（与 `StellarEraHUD.h` 里前置声明飞船是同一个理由）。控制器在 .cpp 里把下标映射成枚举。

`EPlanetBrushShape` 定义在 `PlanetEditBrush.h`（Vehicle 公开头），因为组件和纯函数都要用它。

### 7.2 绘制

- 背景：半透明黑 `(0,0,0,0.55)`，1 px 边框。
- 选中：金色 `(1,0.85,0.30)` 边框；未选中：灰白。
- 球体图标：半径 14 px 的多边形圆（24 段）。
- 正方体图标：等轴测六边形 + 三条内线（顶面与两条竖棱）。
- 全部用 `AHUD::DrawRect` / `DrawLine`，不引入贴图资产。

## 8. 表面网格（`UPlanetSurfaceGridComponent`）

### 8.1 为什么不是纯解析采样

`AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection` 只问地形模块，**读不到体素编辑**。挖出的洞、加上的球在解析函数里不存在，网格会悬在旧地表上。因此：

1. 用解析半径作为**初值**（地形起伏的量级已知）；
2. 沿半径方向用 `FVoxelData::GetValue` 找符号翻转（正 = 球外、负 = 球内，见 `VoxelPlanetSphereTerrain.cpp`）；
3. 翻转后二分 4 次收敛到体素级精度。

步进上限覆盖「地形振幅 + 最大笔刷直径」，超过仍未翻转则回退到解析半径（说明该方向没有编辑）。

### 8.2 几何

- 经纬网格，默认步长 **5°**（`GridStepDegrees`，`EditAnywhere`）。
- 顶点：同一纬度上的经度序列、同一经度上的纬度序列。
- **采样在体素空间进行**（`FVoxelData::GetValue` 的输入单位是体素）：顶点方向 `UnitDir` 沿半径找符号翻转。
- 缓存的是**体素空间半径**；绘制时先 `× VoxelSize` 得行星局部厘米、再乘行星变换得世界坐标。
- 每段只要有一个端点朝相机（球心→顶点方向与球心→相机方向点积 > 0）就提交；整段在背面则跳过。
- 挖掘后防抖 0.3 s 重建缓存。

### 8.3 绘制

- 组件挂在 `AVoxelPlanetActor` 上，内部创建一个 `ULineBatchComponent`。
- 线段是**世界空间**坐标（`FLineBatcherSceneProxy` 不做组件变换），而行星在 N 体里每帧都在动，所以可见期间每帧：
  1. 缓存局部顶点 × 行星变换 → 世界坐标；
  2. 背面剔除；
  3. `Flush()` + `DrawLines()`。
- 关闭时组件 Tick 关掉，不做任何工作。
- `SDPG_World` 深度优先级，线段会被山体正常遮挡。

### 8.4 参数

| 参数 | 默认 |
|---|---|
| `GridStepDegrees` | 5 |
| `SurfaceOffsetCm` | 3 |
| `MaxSearchStepsVoxels` | 32 |
| `RebuildDebounceSeconds` | 0.3 |

## 9. 持久化

### 9.1 写入

`AVoxelPlanetActor::SaveEditsToSaveObject()`：

1. `SaveObject` 为空 → 记一条 Warning（每个行星只记一次），直接返回；
2. `UVoxelDataTools::GetSave(this, Save)`；
3. `UVoxelSaveUtilities::CompressVoxelSave(Save, SaveObject->Save)`；
4. `SaveObject->CopyDepthFromSave()`、`SaveObject->MarkPackageDirty()`。

触发时机：

- 每次编辑后 **2 s 防抖**（`SaveDebounceSeconds`）；
- `EndPlay` 时立即写一次。

插件在 `CreateWorldInternal` 里会自动 `LoadFromSaveObject`，所以下次打开地图/重启 PIE 会读到这次写下的数据。

### 9.2 关卡数据要求

UniverseMap 里每颗 `AVoxelPlanetActor` 需要一个 `UVoxelWorldSaveObject` 资产并赋给 `SaveObject`。当前是空的，实施时用编辑器脚本创建并赋值（保存走 `execute_python_code`，注意 OFPA 保存限制，必要时由用户手动保存关卡）。

### 9.3 固有限制（写进交付说明）

- PIE 中写入的是**内存中的资产副本**；要在编辑器里 Save All 才落盘到磁盘。
- 打包运行时该资产只读，运行时编辑无法跨进程保存。要真正的运行时存盘，需要另接 `bAutomaticallySaveToFile` + `SaveFilePath`（本次不做）。
- 改动地形模块参数并重建行星后，旧的编辑数据仍会叠加在新生地形上，可能出现悬空/嵌入。重建前应先清空 SaveObject（本次不做，只在文档里注明）。

## 10. 测试

### 10.1 自动化

| 测试名 | 内容 |
|---|---|
| `StellarEra.Planet.EditBrush.SphereCenter` | 放球中心 = 命中点 + 法线 × 半径；挖球中心 = 命中点 |
| `StellarEra.Planet.EditBrush.BoxBounds` | 世界点 → 体素盒：尺寸 1/2/8 的 Min/Max；法线偏移只作用于放置 |
| `StellarEra.Planet.EditBrush.ClampSize` | 0/1/8/9/-3 的夹取结果 |
| `StellarEra.UI.EditBrushButtons` | 两个按钮矩形不重叠、顺序正确；命中边界含（压线算命中）；按钮外返回 false |
| `StellarEra.Planet.EditGrid.Geometry` | 给定恒定半径函数：5° 网格顶点/线段数量符合公式；背面剔除函数在相机另一侧返回空 |

全部为纯函数断言，不依赖世界与 PIE。

### 10.2 手动验证（PIE，UniverseMap）

1. 下船站在行星上，按 G：按钮与网格出现。
2. 左键点地形：出现球形坑。
3. 右键轻点：出现球体凸起；右键按住拖动：视角旋转且不放置。
4. 点正方体按钮：按钮高亮切换；左键挖出方块坑，右键放方块。
5. 滚轮：笔刷尺寸变化，HUD 数字跟着变；相机臂长不变。
6. 按 G 退出：按钮与网格消失。
7. 上船 / 按 M 进星图：编辑模式自动退出，按钮与网格消失。
8. 挖完等 2 s，退出 PIE 再进：地形保持（需编辑器 Save All）。
9. 网格在洞里下凹、在方块堆上起伏；转到行星背面时背面网格不显示。

## 11. 风险与边界

| 风险 | 处理 |
|---|---|
| 体素碰撞 `CollisionPresets` 不响应 `ECC_Visibility` | 射线先试 Visibility 再试 WorldStatic；都失败时本次编辑无效（不会误挖其它 Actor） |
| 编辑后网格重建耗时 | 5° 网格 5184 个采样点 / 5112 段（72 条经线 × 37 点 + 35 条纬线 × 72 点），每点通常 1–8 次密度采样、最坏 2×32 次；只在编辑后防抖重建一次 |
| 行星每帧移动导致网格拖影 | 可见期间逐帧变换缓存顶点并重画；网格只在编辑模式可见，成本可接受 |
| 挖穿薄壳后角色掉出星球 | 属于玩家行为，不做限制；`SnapToPlanetSurface` 仍可用于恢复 |
| SaveObject 为空 | 只警告不崩溃；仍可编辑，只是不持久化 |
| 新增 `Voxel` 头文件进公开头 | `PlanetEditComponent.h` 只前向声明体素类型；Voxel 头只在 .cpp include |

## 12. 验收标准

1. 上述自动化测试全部通过，且既有 `StellarEra.*` 套件无回归。
2. PIE 手动验证 1–9 全部可复现。
3. 构建 `StellarEraEditor Win64 Development` 成功（编辑器关闭状态下）。
4. 未修改 `Plugins/VoxelFree/`、`Plugins/VibeUE/`。
