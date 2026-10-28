# 星图连续挖掘 + 笔刷范围可调 — 设计

- **日期**：2026-10-28
- **项目**：StellarEra（Unreal Engine 5.8）
- **状态**：设计已确认，待实现
- **前置**：`2026-10-24-starmap-planet-edit-design.md`（星图编辑模式、`UPlanetEditComponent`、`AStarMapCameraPawn`）

---

## 1. 目标

| # | 需求 |
|---|---|
| 1 | 星图编辑模式下，`Shift`+左键**按住**可连续挖掘（移动光标即沿路径挖） |
| 2 | 挖掘范围（笔刷直径）的上下限与默认值可在**细节面板**调整，不用重编译 |
| 3 | 默认笔刷从 32 体素提到 64 体素（用户反馈「范围有点小」） |
| 4 | 现有「填」功能不丢，键位挪到 `Ctrl`+左键 |

用户原话：

> 现在的星图模式，的挖掘的范围有点小，且不能连续，加一个shift和左键连续挖掘的功能，且范围变为细节面板可调

## 2. 已定决策

| # | 问题 | 选择 | 出处 |
|---|---|---|---|
| 1 | `Shift`+左键与现有「填」冲突 | **填挪到 `Ctrl`+左键**，`Shift`+左键独占连续挖 | 用户选定 |
| 2 | 「细节面板」指哪 | **新建 `BP_StarMapCameraPawn`**，控制器按类名找它生成（与 `BP_PlanetCharacter` / `BP_SpaceshipPawn` 同套路） | 用户选定 |
| 3 | 连续挖的驱动 | **按住期间按固定间隔重复挖**，间隔可调（默认 0.12 s） | 本设计 |
| 4 | 连续挖时相机 | **拖鼠标不转视角**（光标用于在球面上移动挖掘点） | 本设计 |
| 5 | 角色编辑模式 | **完全不动**（左键挖、右键填、滚轮调尺寸） | 本设计 |

## 3. 非目标

- 不做「连续填」（按住 `Ctrl`+左键仍只填一次）。
- 不改笔刷形状、预览配色、存档链路。
- 不改 `Plugins/VoxelFree/` 与 `Plugins/VibeUE/`。
- 不改角色的编辑手感与按键。

## 4. 模块与文件

依赖方向不变，无需改任何 `Build.cs`（`StellarEra` 已依赖 `StellarEraUI` 与 `StellarEraPlanetEdit`）。

| 模块 | 文件 | 改动 |
|---|---|---|
| `StellarEraPlanetEdit` | `Public/PlanetEditBrush.h`、`Private/PlanetEditBrush.cpp` | 新增纯函数 `ShouldDigNow(Accumulator, DeltaSeconds, Interval)` |
| `StellarEraPlanetEdit` | `Public/PlanetEditComponent.h`、`Private/PlanetEditComponent.cpp` | 新增连续挖状态、间隔参数、Tick 内驱动 |
| `StellarEraUI` | `Private/StarMapCameraPawn.cpp` | 默认笔刷 32 → 64；`PlaceActionHint` 改 `Ctrl+左键填` |
| `StellarEra` | `Public/StellarEraPlayerController.h`、`Private/StellarEraPlayerController.cpp` | 输入分派（Shift 连挖 / Ctrl 填）、松开清理、`OnLook` 抑制、按蓝图类生成星图 Pawn |
| `StellarEra` | `Private/PlanetEditTests.cpp`、`Private/StarMapTests.cpp` | 新增 / 更新自动化测试 |
| `Content/Code` | 新建 `BP_StarMapCameraPawn.uasset` | 星图相机蓝图子类（细节面板入口） |

## 5. 连续挖掘

### 5.1 组件状态机

`UPlanetEditComponent` 新增：

```cpp
/** 按住连续挖的间隔（秒）。太小会把体素工具刷爆，太大手感发涩。 */
UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.01"))
float ContinuousDigIntervalSeconds = 0.12f;

void SetContinuousDigging(bool bEnabled);
bool IsContinuousDigging() const { return bContinuousDigging; }
```

私有成员：`bool bContinuousDigging = false;`、`float ContinuousDigAccumulatorSeconds = 0.f;`。

- `SetContinuousDigging(true)`：置位并把累加器清零（按下那一下由控制器立即挖一次，Tick 不重复补刀）。
- `SetContinuousDigging(false)`：清位并清零累加器。
- `SetEditMode(false)`：**顺带** `SetContinuousDigging(false)` —— 退出编辑、切 Pawn、进出星图都不会留下「隐形连挖」。
- 间隔下限（0.01 s）的夹取**只在 `ShouldDigNow` 内部做一次**，不在 `BeginPlay` 另做一遍：
  蓝图里填 0 也能被纯函数兜住，且这条分支可无头测试。

### 5.2 Tick 驱动

组件已在编辑模式下每帧 Tick（`TG_PostUpdateWork`，`bStartWithTickEnabled=false`）。`TickComponent` 末尾：

```cpp
if (bContinuousDigging)
{
    if (PlanetEditBrush::ShouldDigNow(
            ContinuousDigAccumulatorSeconds, DeltaTime, ContinuousDigIntervalSeconds))
    {
        // 与预览同源：控制器自己的 GetMousePosition（Canvas 只在 PostRender 有效）
        float MouseX = 0.f, MouseY = 0.f;
        APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
        if (PC && PC->GetMousePosition(MouseX, MouseY))
        {
            DigAtScreenPosition(FVector2D(MouseX, MouseY));
        }
    }
}
```

**单帧最多一次**：`ShouldDigNow` 在累加器超过间隔时只返回一次 true 并把累加器清零（不做 `while` 补偿）。掉帧 / 卡顿后不会一次性补挖一串。

### 5.3 纯函数（可无头测）

`PlanetEditBrush` 命名空间新增：

```cpp
/**
 * 累计 DeltaSeconds，跨过间隔返回 true 并把累加器清零。
 * Interval <= 0 时按下限 0.01 处理，保证不会每帧都返回 true。
 */
STELLARERAPLANETEDIT_API bool ShouldDigNow(
    float& AccumulatorSeconds, float DeltaSeconds, float IntervalSeconds);
```

## 6. 输入

星图编辑模式（`G` 进入）下的左键分派，改 `AStellarEraPlayerController::OnStarMapSelectStarted`：

```cpp
const bool bShift = IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
const bool bCtrl  = IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);

if (bStarMap && bShift)
{
    Edit->SetContinuousDigging(true);
    Edit->DigAtScreenPosition(MousePosition);   // 按下立即挖一次
}
else if (bStarMap && bCtrl)
{
    Edit->PlaceAtScreenPosition(MousePosition); // 原 Shift 填的功能
}
else
{
    Edit->DigAtScreenPosition(MousePosition);   // 角色与星图共用
}
```

`Shift` 优先于 `Ctrl`（同时按住按连续挖处理）。

`OnStarMapSelectCompleted` **最前面**（早退之前）无条件清理：

```cpp
if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
{
    Edit->SetContinuousDigging(false);
}
```

放在最前的原因：连续挖按下时走的是「命中行星 → 消费点击」分支，`bStarMapSelectHeld` 没有被置位，函数会在现有的 `if (!bStarMapSelectHeld) return;` 处提前返回，标志就清不掉。

`OnLook` 的星图分支加一条抑制：

```cpp
else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
{
    // 连续挖期间拖鼠标 = 移动光标，不转视角；否则无法在球面上画挖掘路径。
    if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
    {
        if (Edit->IsContinuousDigging())
        {
            return;
        }
    }
    // …原有 ActiveMineralView 判断与 OrbitLook
}
```

星图的鼠标位移只在**按住鼠标**（`CaptureDuringMouseDown`）时才有值，所以这条抑制正好覆盖「Shift+左键拖拽」这一种情况，不影响无按键时移动光标瞄准。

| 输入（星图编辑模式） | 行为 |
|---|---|
| 左键 | 挖一次 |
| `Shift` + 左键按住 | 连续挖（按下即挖一次，之后每 `ContinuousDigIntervalSeconds` 一次） |
| `Ctrl` + 左键 | 填一次 |
| 右键 | 绕焦点旋转（不变） |
| 滚轮 | 缩放（不变） |
| `[` / `]` | 笔刷尺寸（不变） |

角色编辑模式不变。

## 7. 细节面板

### 7.1 蓝图子类

`Content/Code/BP_StarMapCameraPawn`，父类 `AStarMapCameraPawn`。

控制器生成星图 Pawn 时优先用它（`AStellarEraPlayerController` 构造函数）：

```cpp
StarMapPawnClass = AStarMapCameraPawn::StaticClass();
static ConstructorHelpers::FClassFinder<AStarMapCameraPawn> StarMapClassFinder(
    TEXT("/Game/Code/BP_StarMapCameraPawn.BP_StarMapCameraPawn_C"));
if (StarMapClassFinder.Succeeded())
{
    StarMapPawnClass = StarMapClassFinder.Class;
}
else
{
    UE_LOG(LogTemp, Warning,
        TEXT("AStellarEraPlayerController: 找不到 BP_StarMapCameraPawn，将使用原生 AStarMapCameraPawn（参数只能在 C++ 里改）。"));
}
```

`SpawnActor` 改用 `StarMapPawnClass`（空时退回 `AStarMapCameraPawn::StaticClass()`）。

新增成员（头文件里 `AStarMapCameraPawn` 已有前向声明）：

```cpp
UPROPERTY()
TSubclassOf<AStarMapCameraPawn> StarMapPawnClass;
```

### 7.2 可调参数

`UPlanetEditComponent` 上已有的 `EditAnywhere` 参数，在蓝图里选中 `PlanetEdit` 组件即可调：

| 参数 | 含义 |
|---|---|
| `MinBrushSizeVoxels` / `MaxBrushSizeVoxels` | 笔刷直径区间（默认 8 / 128） |
| `BrushSizeVoxels` | 默认直径（默认改为 64） |
| `ContinuousDigIntervalSeconds` | 连续挖间隔（默认 0.12 s） |
| `MaxEditDistanceCm` | 编辑射线最远距离（默认 5e7 = 50 AU） |
| `bMultiThreadedEdits` | 体素工具并行（默认 true） |
| `PlaceActionHint` | HUD 提示文案（星图默认 `Shift+左键按住连续挖 · Ctrl+左键填`） |

原生组件在蓝图子类里以「原生组件」形式出现在 Components 面板，其模板属性可以编辑并随蓝图序列化，不需要把组件指针改成 `EditAnywhere`。

`BeginPlay` 已有的 `BrushSizeVoxels` 区间夹取保留：蓝图里把默认值填到区间外也能自动纠正。

### 7.3 资产创建方式

用无头 Python（`UnrealEditor-Cmd -run=pythonscript`）创建蓝图资产并保存：

```python
factory = unreal.BlueprintFactory()
factory.set_editor_property("parent_class",
    unreal.load_class(None, "/Script/StellarEraUI.StarMapCameraPawn"))
tools = unreal.AssetToolsHelpers.get_asset_tools()
bp = tools.create_asset("BP_StarMapCameraPawn", "/Game/Code", unreal.Blueprint, factory)
unreal.EditorAssetLibrary.save_loaded_asset(bp)
```

失败时（编辑器脚本环境不可用）退回：由用户在编辑器里手建，实施记录里写清步骤。

## 8. HUD

提示行改为：

```
笔刷 64 体素 · 左键挖 / Shift+左键按住连续挖 · Ctrl+左键填 · G 退出
```

实现上仍是 `PlaceActionHint` 一个字段：星图 Pawn 构造里写
`TEXT("Shift+左键按住连续挖 · Ctrl+左键填")`，HUD 的
`"笔刷 %d 体素 · 左键挖 / %s · G 退出"` 不用改格式。

角色仍显示 `右键点放`。

## 9. 测试

### 9.1 无头自动化（`StellarEra.*`）

- `StellarEra.Planet.EditBrush.ShouldDigNow`：
  - 累加不足间隔返回 false，累加器保留；
  - 跨过间隔返回 true 且累加器清零；
  - 一帧巨大 `DeltaSeconds` 只返回一次 true（不补偿多次）；
  - `Interval <= 0` 时按 0.01 处理，不出现「每帧都 true」。
- `StellarEra.Planet.EditComponent.ContinuousDig`：
  - 默认 `IsContinuousDigging() == false`；
  - `SetContinuousDigging(true)` 后为 true；
  - `SetEditMode(false)` 后自动为 false；
  - `SetContinuousDigging(false)` 幂等。
- 更新现有断言：`StellarEra.StarMap.*` 里默认笔刷 32 → 64、提示文案 `Shift+左键填` → 新文案。

### 9.2 PIE 手动（UniverseMap）

1. `M` 进星图 → `G` 进编辑 → `Shift`+左键按住，光标在球面上拖动：出现连续坑道，相机不转。
2. 松开左键：连挖立即停止；再点一次只挖一个坑。
3. `Ctrl`+左键：填一个堆；`Shift` 松开后 `Ctrl` 行为不受影响。
4. 按 `[` / `]` 仍能改尺寸；HUD 提示文案正确。
5. 打开 `BP_StarMapCameraPawn` 的细节面板，改默认直径 / 上限 → 重新 PIE 生效（不重编译）。
6. 角色编辑模式回归：左键挖、右键填、滚轮调尺寸不变。
7. 连续挖中途按 `G` 退出编辑或 `M` 退出星图：不会残留连续挖掘。

## 10. 风险与已知坑

- **`OnStarMapSelectCompleted` 的清理位置**：必须在所有早退之前，否则连续挖停不下来（见 §6）。
- **相机抑制范围**：只在 `IsContinuousDigging()` 期间抑制；改成「编辑模式就抑制」会破坏现有的左键拖拽绕轨。
- **性能**：128 体素球约 110 万体素 / 次，0.12 s 间隔在 `bMultiThreadedEdits=true` 下是保守值；蓝图里调小时要自己承担。
- **蓝图资产依赖生成顺序**：`BP_StarMapCameraPawn` 不存在时退回原生类并 Warning，功能不坏但细节面板不可调。
- **中文源文件**：修改 / 新增的 `.h` / `.cpp` 存 UTF-8 **带 BOM**。
- **构建**：先关编辑器，用 CLAUDE.md 的捆绑 dotnet 直调 UBT；构建后先比 DLL 时间戳再调。
- **提交**：pathspec 挂在 `git commit` 上，别裸提交（索引里有常驻暂存文件）。

## 11. 验收标准

- [ ] 星图编辑模式：`Shift`+左键按住连续挖，拖鼠标沿球面挖且相机不转。
- [ ] 松开左键立即停止；`Ctrl`+左键填一次；左键单击仍是挖一次。
- [ ] 默认笔刷 64 体素；`[` / `]` 可调。
- [ ] `BP_StarMapCameraPawn` 存在，细节面板能改笔刷区间 / 默认值 / 连续挖间隔，改完不重编译即生效。
- [ ] HUD 提示文案正确。
- [ ] 角色编辑模式零回归。
- [ ] 自动化测试全过；构建零警告。
