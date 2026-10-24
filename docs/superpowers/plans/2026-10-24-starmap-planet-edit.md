# 星图大尺度行星编辑实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 `UPlanetEditComponent` 同时服务角色与星图相机，并在星图里用 8–128 体素笔刷直接挖 / 填行星的 LOD0 体素。

**Architecture:** 组件保留「状态 + 屏幕射线 + 笔刷预览」，把「屏幕射线 → 命中哪颗星」抽成可替换的 `FPlanetEditTargetResolver`：角色装碰撞射线版，星图装解析射线版。星图没有碰撞（细 invoker 碰撞半径 3000 cm < 最低高度 5000 cm），因此新增 `AVoxelPlanetActor::RaycastSurfaceCm`，用「解析射线-变半径球求交 + 体素符号行进」代替 `LineTrace`。体素工具调用抽成 `PlanetBrushApplicator::Apply`，两条路径共用。

**Tech Stack:** Unreal Engine 5.8.3 / C++ / VoxelFree 插件（只调用，不修改）/ UE Automation Tests。

**Spec:** `docs/superpowers/specs/2026-10-24-starmap-planet-edit-design.md`

## Global Constraints

- 自定义代码只写进 `Source/StellarEra`、`Source/StellarEraCore`、`Source/StellarEraPlanet`、`Source/StellarEraVehicle`；**不得修改 `Plugins/VoxelFree/` 与 `Plugins/VibeUE/`**。
- 依赖分层：`StellarEra → StellarEraVehicle → StellarEraPlanet → Voxel`，`StellarEra → StellarEraCore`。`StellarEraVehicle` **不能**依赖 `StellarEra`。本计划**不需要改任何 `Build.cs`**。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**（`head -c 3 <file> | od -An -tx1` → `ef bb bf`）。
- 不要统一换行符：仓库本来就是 CRLF/LF 混合。
- 构建只能用捆绑 dotnet 直调 UBT，**构建前必须关闭编辑器**，cwd 在项目外：
  ```bash
  cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- 自动化测试（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="<临时日志>"
  ```
- 提交必须带 pathspec（索引里有常驻暂存文件）：`git commit -m "..." -- <paths>`。本计划的 commit 步骤都写成「只提交本任务触碰的源文件」。
- 单位：世界空间 = 厘米；1 AU = 1e6 cm；`VoxelSize` = 单个体素的厘米尺寸；体素空间坐标以体素为单位。
- 星图笔刷**不改渲染 LOD**：`UVoxelSphereTools` / `UVoxelBoxTools` 写的永远是基础数据（LOD0），LOD 只影响网格化。
- `FVoxelIntBox` 的 `Max` 是**开区间**；`FPlanetEditBox` 的 `Max` 是**闭区间**，换算必须走 `PlanetEditBrush::MakeVoxelBox`。

---

## 文件结构

| 文件 | 动作 | 职责 |
|---|---|---|
| `Source/StellarEraVehicle/Public/PlanetEditBrush.h` | 修改 | 增加带 `Min/Max` 的尺寸钳制重载 |
| `Source/StellarEraVehicle/Private/PlanetEditBrush.cpp` | 修改 | 同上 |
| `Source/StellarEraVehicle/Public/PlanetBrushApplicator.h` | **新建** | 「命中点 → 体素工具」的唯一出口 |
| `Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp` | **新建** | 同上 |
| `Source/StellarEraVehicle/Public/PlanetEditComponent.h` | 修改 | 目标解析器、网格归属、尺寸区间、并行开关、提示文案 |
| `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp` | 修改 | 删除 `ResolvePlanet` / `TraceEditTarget`，改为解析器 |
| `Source/StellarEraVehicle/Private/PlanetCharacter.cpp` | 修改 | 装碰撞射线解析器，网格归属改用 `SetGridPlanet` |
| `Source/StellarEraPlanet/Public/PlanetSurfaceRay.h` | **新建** | 射线-球、变半径球的不动点求交（纯函数） |
| `Source/StellarEraPlanet/Private/PlanetSurfaceRay.cpp` | **新建** | 同上 |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` | 修改 | 声明 `RaycastSurfaceCm` |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 修改 | 实现解析求交 + 体素行进 |
| `Source/StellarEra/Public/StarMapCameraPawn.h` | 修改 | 编辑组件成员、`BeginPlay`、解析器 |
| `Source/StellarEra/Private/StarMapCameraPawn.cpp` | 修改 | 同上；`RaySphereFirstHitCm` 改为委托 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | 修改 | 泛化取组件、新增 `IA_BrushSize` / `OnBrushSize` |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | 修改 | 星图编辑输入、笔刷尺寸键、退出时关编辑 |
| `Source/StellarEra/Public/StellarEraHUD.h` | 修改 | 取「当前在看的编辑组件」 |
| `Source/StellarEra/Private/StellarEraHUD.cpp` | 修改 | 星图也画编辑面板；提示文案用 `PlaceActionHint` |
| `Source/StellarEra/Private/PlanetEditTests.cpp` | 修改 | 新增 3 个自动化测试（笔刷区间、128 盒、组件区间） |
| `Source/StellarEra/Private/PlanetSurfaceRayTests.cpp` | **新建** | 新增 5 个解析射线测试（含中段打空） |
| `Source/StellarEra/Private/StarMapTests.cpp` | 修改 | 新增 1 个星图组件的 CDO 参数测试 |

---

### Task 1: `PlanetEditBrush` 增加带区间的重载

**Files:**
- Modify: `Source/StellarEraVehicle/Public/PlanetEditBrush.h:49-79`
- Modify: `Source/StellarEraVehicle/Private/PlanetEditBrush.cpp`
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`

**Interfaces:**
- Produces:
  - `static int32 PlanetEditBrush::ClampSize(int32 SizeVoxels, int32 MinSize, int32 MaxSize)`
  - `static float PlanetEditBrush::GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm, int32 MinSize, int32 MaxSize)`
  - `static FVector PlanetEditBrush::GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal, int32 SizeVoxels, float VoxelSizeCm, bool bPlace, int32 MinSize, int32 MaxSize)`
  - `static FPlanetEditBox PlanetEditBrush::MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels, int32 MinSize, int32 MaxSize)`
  - `constexpr int32 PlanetEditBrush::MinStarMapSizeVoxels = 8;`
  - `constexpr int32 PlanetEditBrush::MaxStarMapSizeVoxels = 128;`
- 原有的 1 参 / 2 参 / 5 参版本**保持不变**（内部委托到区间版，区间取 1..8），既有测试与行为不受影响。

- [ ] **Step 1: 写失败测试**

在 `Source/StellarEra/Private/PlanetEditTests.cpp` 的 `#endif` 之前、`FStellarEraPlanetSaveObjectPathTest` 之后追加：

```cpp
// 星图笔刷是 8..128，与角色的 1..8 是两套区间。旧的 1 参 ClampSize 会把
// 32 悄悄夹成 8，所以「带区间的重载」必须是独立函数，不能只改常量。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushClampSizeRangeTest,
	"StellarEra.Planet.EditBrush.ClampSizeRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushClampSizeRangeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("0 夹到下限 8"), PlanetEditBrush::ClampSize(0, 8, 128), 8);
	TestEqual(TEXT("999 夹到上限 128"), PlanetEditBrush::ClampSize(999, 8, 128), 128);
	TestEqual(TEXT("32 原样通过"), PlanetEditBrush::ClampSize(32, 8, 128), 32);
	TestEqual(TEXT("8 正好在下限上"), PlanetEditBrush::ClampSize(8, 8, 128), 8);
	TestEqual(TEXT("128 正好在上限上"), PlanetEditBrush::ClampSize(128, 8, 128), 128);
	TestEqual(TEXT("上限小于下限时以下限为准"), PlanetEditBrush::ClampSize(0, 10, 2), 10);
	TestEqual(TEXT("上限小于下限时不越过下限"), PlanetEditBrush::ClampSize(99, 10, 2), 10);
	TestEqual(TEXT("下限 0 被抬到 1"), PlanetEditBrush::ClampSize(0, 0, 4), 1);
	return true;
}

// 128 体素的方块必须真的是 128 格宽。MakeBox 内部若仍走 1..8 的钳制，
// 这里会得到 8 格 —— 星图方块笔刷「永远只有 8 格」就是这么来的。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBoxBounds128Test,
	"StellarEra.Planet.EditBrush.BoxBounds128",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBoxBounds128Test::RunTest(const FString& Parameters)
{
	const FPlanetEditBox Big = PlanetEditBrush::MakeBox(
		FVector(1000.f, 1000.f, 1000.f), 128,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);

	// Low = (128-1)/2 = 63，High = 128-1-63 = 64。
	TestEqual(TEXT("128 Min.X"), Big.Min.X, 1000 - 63);
	TestEqual(TEXT("128 Max.X"), Big.Max.X, 1000 + 64);
	TestEqual(TEXT("128 X 方向宽度（含端点）"), Big.Max.X - Big.Min.X + 1, 128);
	TestEqual(TEXT("128 Y 方向宽度（含端点）"), Big.Max.Y - Big.Min.Y + 1, 128);
	TestEqual(TEXT("128 Z 方向宽度（含端点）"), Big.Max.Z - Big.Min.Z + 1, 128);

	const FPlanetEditBox Mid = PlanetEditBrush::MakeBox(
		FVector(1000.f), 32,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);
	TestEqual(TEXT("32 X 方向宽度（含端点）"), Mid.Max.X - Mid.Min.X + 1, 32);

	// 半边长 = 尺寸 × 体素尺寸 / 2。行星 3 的体素是 50 cm，128 格 = 6400 cm 直径。
	TestEqual(TEXT("128 × 50cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(128, 50.f,
			PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels),
		3200.f);

	// 放的偏移必须跟着区间版走，否则 128 的方块会只偏移 4 格。
	const FVector Hit(0.f, 0.f, 1000.f);
	const FVector Placed = PlanetEditBrush::GetEditCenterWorld(
		Hit, FVector(0.f, 0.f, 1.f), 128, 50.f, /*bPlace=*/true,
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels);
	TestTrue(TEXT("放的球心沿法线外移 3200cm"), Placed.Equals(Hit + FVector(0.f, 0.f, 3200.f)));

	return true;
}
```

- [ ] **Step 2: 运行测试确认失败（编译失败）**

Run: 上文 Global Constraints 里的 `Automation RunTests StellarEra` 命令。
Expected: **构建阶段失败**，报 `no matching function for call to 'ClampSize(int, int, int)'`。这就是红。

- [ ] **Step 3: 改头文件**

把 `PlanetEditBrush.h:49-61` 这段：

```cpp
namespace PlanetEditBrush
{
	/** 笔刷尺寸下限（体素数，直径 / 边长）。 */
	constexpr int32 MinSizeVoxels = 1;

	/** 笔刷尺寸上限（体素数，边长）。 */
	constexpr int32 MaxSizeVoxels = 8;

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	STELLARERAVEHICLE_API int32 ClampSize(int32 SizeVoxels);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	STELLARERAVEHICLE_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);
```

整体替换为：

```cpp
namespace PlanetEditBrush
{
	/** 角色笔刷的尺寸下限 / 上限（体素数，直径 / 边长）。 */
	constexpr int32 MinSizeVoxels = 1;
	constexpr int32 MaxSizeVoxels = 8;

	/**
	 * 星图笔刷的尺寸区间。
	 *
	 * 与角色的 1..8 是**两套**，不是同一个常量的不同取值：角色贴着地表看的是
	 * 脚下几米，星图看的是整颗星的地貌。带区间的重载是为此存在的 ——
	 * 旧的 1 参 ClampSize 会把 32 悄悄夹成 8。
	 */
	constexpr int32 MinStarMapSizeVoxels = 8;
	constexpr int32 MaxStarMapSizeVoxels = 128;

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	STELLARERAVEHICLE_API int32 ClampSize(int32 SizeVoxels);

	/**
	 * 夹到 [MinSize, MaxSize]。
	 *
	 * 下限被抬到至少 1（尺寸 0 会造出空盒）；MinSize > MaxSize 时以下限为准，
	 * 而不是像 FMath::Clamp 那样断言 —— 这两个值是可配置项，写反了不该在
	 * 运行时崩。
	 */
	STELLARERAVEHICLE_API int32 ClampSize(int32 SizeVoxels, int32 MinSize, int32 MaxSize);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	STELLARERAVEHICLE_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);

	/** 区间版：先按 [MinSize, MaxSize] 夹尺寸，再算半边长。 */
	STELLARERAVEHICLE_API float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm,
		int32 MinSize, int32 MaxSize);
```

再把 `GetEditCenterWorld` 与 `MakeBox` 的声明后面各加一个区间版：

```cpp
	STELLARERAVEHICLE_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace);

	/** 区间版：放的偏移量与笔刷半边长一致（用同一套 Min/Max 夹）。 */
	STELLARERAVEHICLE_API FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace, int32 MinSize, int32 MaxSize);
```

```cpp
	STELLARERAVEHICLE_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);

	/** 区间版：盒宽 = 夹到 [MinSize, MaxSize] 之后的尺寸，每轴都是 Width 格。 */
	STELLARERAVEHICLE_API FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels,
		int32 MinSize, int32 MaxSize);
```

- [ ] **Step 4: 改实现**

`PlanetEditBrush.cpp` 改为（保留原有函数体，新增区间版并让旧版委托）：

```cpp
int32 PlanetEditBrush::ClampSize(const int32 SizeVoxels)
{
	return ClampSize(SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
}

int32 PlanetEditBrush::ClampSize(const int32 SizeVoxels, const int32 MinSize, const int32 MaxSize)
{
	// 尺寸 0 造出空盒，下限至少 1；上下限写反时保下限。
	const int32 Low = FMath::Max(MinSize, 1);
	const int32 High = FMath::Max(MaxSize, Low);
	return FMath::Clamp(SizeVoxels, Low, High);
}

float PlanetEditBrush::GetHalfExtentCm(const int32 SizeVoxels, const float VoxelSizeCm)
{
	return GetHalfExtentCm(SizeVoxels, VoxelSizeCm, MinSizeVoxels, MaxSizeVoxels);
}

float PlanetEditBrush::GetHalfExtentCm(const int32 SizeVoxels, const float VoxelSizeCm,
	const int32 MinSize, const int32 MaxSize)
{
	return ClampSize(SizeVoxels, MinSize, MaxSize) * VoxelSizeCm * 0.5f;
}

FVector PlanetEditBrush::GetEditCenterWorld(
	const FVector& HitLocation,
	const FVector& HitNormal,
	const int32 SizeVoxels,
	const float VoxelSizeCm,
	const bool bPlace)
{
	return GetEditCenterWorld(HitLocation, HitNormal, SizeVoxels, VoxelSizeCm, bPlace,
		MinSizeVoxels, MaxSizeVoxels);
}

FVector PlanetEditBrush::GetEditCenterWorld(
	const FVector& HitLocation,
	const FVector& HitNormal,
	const int32 SizeVoxels,
	const float VoxelSizeCm,
	const bool bPlace,
	const int32 MinSize,
	const int32 MaxSize)
{
	if (!bPlace)
	{
		return HitLocation;
	}

	// 法线为零（退化命中）时不偏移，总比把方块放到 inf/nan 强。
	const FVector Normal = HitNormal.GetSafeNormal();
	return HitLocation + Normal * GetHalfExtentCm(SizeVoxels, VoxelSizeCm, MinSize, MaxSize);
}

FPlanetEditBox PlanetEditBrush::MakeBox(const FVector& LocalCenterVoxels, const int32 SizeVoxels)
{
	return MakeBox(LocalCenterVoxels, SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
}

FPlanetEditBox PlanetEditBrush::MakeBox(const FVector& LocalCenterVoxels, const int32 SizeVoxels,
	const int32 MinSize, const int32 MaxSize)
{
	const int32 Size = ClampSize(SizeVoxels, MinSize, MaxSize);
	// FVector 的分量是 double，FMath::RoundToInt(double) 返回 int64；
	// FIntVector 收 int32。本项目警告即错误，必须显式窄化。
	const FIntVector Center(
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.X)),
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.Y)),
		static_cast<int32>(FMath::RoundToInt(LocalCenterVoxels.Z)));

	const int32 Low = (Size - 1) / 2;
	const int32 High = Size - 1 - Low;

	FPlanetEditBox Box;
	Box.Min = Center - FIntVector(Low);
	Box.Max = Center + FIntVector(High);
	return Box;
}
```

- [ ] **Step 5: 构建并跑测试**

Run: 先构建（编辑器已关闭），再跑自动化测试。
Expected: 构建成功；`StellarEra.Planet.EditBrush.*` 全部通过，含新增两项；既有 `ClampSize` / `BoxBounds` / `VoxelBox` 仍通过。

- [ ] **Step 6: 提交**

```bash
git commit -m "feat(行星编辑): 笔刷几何支持 8-128 体素区间，为星图大笔刷铺路" -- Source/StellarEraVehicle/Public/PlanetEditBrush.h Source/StellarEraVehicle/Private/PlanetEditBrush.cpp Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 2: 抽出 `PlanetBrushApplicator::Apply`

**Files:**
- Create: `Source/StellarEraVehicle/Public/PlanetBrushApplicator.h`
- Create: `Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp`
- Modify: `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp:101-179`
- Modify: `Source/StellarEraVehicle/Public/PlanetEditComponent.h:102-107`

**Interfaces:**
- Consumes: `PlanetEditBrush::GetHalfExtentCm(..., Min, Max)`、`MakeBox(..., Min, Max)`、`MakeVoxelBox`
- Produces:
  ```cpp
  namespace PlanetBrushApplicator {
      bool Apply(AVoxelPlanetActor& Planet, const FVector& CenterWorld,
                 EPlanetBrushShape Shape, int32 SizeVoxels,
                 int32 MinSizeVoxels, int32 MaxSizeVoxels,
                 bool bPlace, bool bMultiThreaded);
  }
  ```
  返回「是否真的改到了体素」；内部负责 `NotifyVoxelEdited()`。

这是一次**纯重构**：行为必须与改动前逐字等价，既有测试与 PIE 观感都不该变。

- [ ] **Step 1: 新建头文件**

`Source/StellarEraVehicle/Public/PlanetBrushApplicator.h`（UTF-8 with BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 一笔挖 / 放的执行：命中点 → Voxel 工具 → 通知重建与保存。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 角色走碰撞射线、星图走解析射线，两条路径的**命中点来源**完全不同，
 * 但「命中点 → 体素工具 → 通知重建/保存」这一段逐字相同。
 *
 * 留在这段代码在组件里的话，组件就得同时懂两种求交；抽出来之后，
 * 两边都只需要产出一个 (Planet, CenterWorld)。
 */
namespace PlanetBrushApplicator
{
	/**
	 * 在 CenterWorld 落一笔，返回是否真的改到了体素。
	 *
	 * CenterWorld 由调用方按 GetEditCenterWorld 算好：挖 = 命中点本身，
	 * 放 = 沿命中法线外移一个半边长。
	 *
	 * 空操作（在岩石里放石头、挖空气）返回 false，且**不**触发网格重建与
	 * 存档 —— 判据是工具写回的 OutModifiedValues 数量：它只记录值真的变了的
	 * 体素。插件的 OutEditedBounds 不能用，宏会无条件把它设成请求包围盒。
	 */
	STELLARERAVEHICLE_API bool Apply(
		AVoxelPlanetActor& Planet,
		const FVector& CenterWorld,
		EPlanetBrushShape Shape,
		int32 SizeVoxels,
		int32 MinSizeVoxels,
		int32 MaxSizeVoxels,
		bool bPlace,
		bool bMultiThreaded);
}
```

- [ ] **Step 2: 新建实现文件**

`Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp`（UTF-8 with BOM）。函数体是
`PlanetEditComponent.cpp` 里 `ApplyBrushAtScreenPosition` 第 117 行往后的**原样搬运**，
只把 `Planet` 指针换成引用、把硬编码的 `false` 换成 `bMultiThreaded`、把尺寸换成区间版：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetBrushApplicator.h"

#include "VoxelPlanetActor.h"
#include "VoxelIntBox.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelVector.h"

bool PlanetBrushApplicator::Apply(
	AVoxelPlanetActor& Planet,
	const FVector& CenterWorld,
	const EPlanetBrushShape Shape,
	const int32 SizeVoxels,
	const int32 MinSizeVoxels,
	const int32 MaxSizeVoxels,
	const bool bPlace,
	const bool bMultiThreaded)
{
	const float VoxelSize = FMath::Max(Planet.VoxelSize, 1.f);

	// 工具的 OutModifiedValues 只记录**值真的变了**的体素（VoxelDataImpl::Set 里
	// `OldValue != NewValue` 才追加），这正是「是否真的改到了体素」的判据。
	TArray<FModifiedVoxelValue> ModifiedValues;

	if (Shape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			SizeVoxels, VoxelSize, MinSizeVoxels, MaxSizeVoxels);
		if (bPlace)
		{
			UVoxelSphereTools::AddSphere(&Planet, CenterWorld, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelSphereTools::RemoveSphere(&Planet, CenterWorld, Radius,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bConvertToVoxelSpace=*/true, /*bUpdateRender=*/true);
		}
	}
	else
	{
		const FVoxelVector LocalCenter = Planet.GlobalToLocalFloat(CenterWorld);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z),
			SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
		// FPlanetEditBox 的 Min / Max 两端都含（MakeBox 的尺寸就是 Max-Min+1），
		// 而 FVoxelIntBox 的 Max 是开区间。直接透传会让方块笔刷少一格 ——
		// 换算收在 MakeVoxelBox 里（有单测），这里拿到的 MaxExclusive 已加过 1。
		const FPlanetEditVoxelBox VoxelBox = PlanetEditBrush::MakeVoxelBox(Box);
		const FVoxelIntBox Bounds(VoxelBox.Min, VoxelBox.MaxExclusive);

		if (bPlace)
		{
			UVoxelBoxTools::AddBox(&Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelBoxTools::RemoveBox(&Planet, Bounds,
				/*OutModifiedValues=*/&ModifiedValues, /*OutEditedBounds=*/nullptr,
				bMultiThreaded, /*bUpdateRender=*/true);
		}
	}

	// 空操作不重建网格、也不触发保存。
	if (ModifiedValues.Num() == 0)
	{
		return false;
	}

	Planet.NotifyVoxelEdited();
	return true;
}
```

- [ ] **Step 3: 让组件调用它**

`PlanetEditComponent.cpp` 的 `ApplyBrushAtScreenPosition` 从第 117 行（`const float VoxelSize = ...`）到第 178 行（`return true;`）整段替换为：

```cpp
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Hit.Location, Hit.ImpactNormal, BrushSizeVoxels, VoxelSize, bPlace,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);

	return PlanetBrushApplicator::Apply(*Planet, Center, BrushShape, BrushSizeVoxels,
		MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits);
```

同时在文件头加上 `#include "PlanetBrushApplicator.h"`，并删掉不再直接使用的
`#include "VoxelIntBox.h"`、`#include "VoxelTools/Gen/VoxelBoxTools.h"`、
`#include "VoxelTools/Gen/VoxelSphereTools.h"`
（预览的方块路径仍在用 `FVoxelVector` —— **保留 `VoxelVector.h`**）。

> 注意：`MinBrushSizeVoxels` / `MaxBrushSizeVoxels` / `bMultiThreadedEdits` 在 Task 3
> 才需要完整语义。**本任务先按下面的最小版本加这三个成员，放在 `MaxEditDistanceCm`
> 旁边（public 区）** —— 星图 Pawn 的构造函数要直接给 `bMultiThreadedEdits` 赋值，
> 私有的话 Task 7 还得再加 setter。Task 3 不再重复添加：
>
> ```cpp
> 	/** 笔刷尺寸下限 / 上限（体素数）。角色 1..8，星图 8..128。 */
> 	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
> 	int32 MinBrushSizeVoxels = 1;
>
> 	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
> 	int32 MaxBrushSizeVoxels = 8;
>
> 	/** 体素工具是否并行。小笔刷单线程延迟更低，128 体素的大笔刷必须并行。 */
> 	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
> 	bool bMultiThreadedEdits = false;
> ```
>
> 并把 `BrushSizeVoxels` 的 `meta` 里的 `ClampMax = "8"` **删掉**（只留
> `ClampMin = "1"`）：上限现在由 `MinBrushSizeVoxels` / `MaxBrushSizeVoxels` 决定，
> 而这两个字段是每个 Pawn 各自配的 —— 写死一个 128 会让角色的细节面板显示一个
> 它永远达不到的上限。运行时由 `GetHalfExtentCm` / `MakeBox` 内部的 `ClampSize` 兜底。

- [ ] **Step 4: 构建并跑全部测试，确认行为没变**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；`StellarEra` 全部测试通过（这是重构，不该有红→绿变化）。

- [ ] **Step 5: 提交**

```bash
git commit -m "refactor(行星编辑): 体素工具调用抽成 PlanetBrushApplicator" -- Source/StellarEraVehicle/Public/PlanetBrushApplicator.h Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp Source/StellarEraVehicle/Public/PlanetEditComponent.h Source/StellarEraVehicle/Private/PlanetEditComponent.cpp
```

---

### Task 3: 让 `UPlanetEditComponent` 通用化（射线解析器可注入）

**Files:**
- Modify: `Source/StellarEraVehicle/Public/PlanetEditComponent.h`（整文件替换）
- Modify: `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp`（分段替换）
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`（追加到 `#endif` 之前）

**Interfaces:**
- Consumes: Task 1 的区间版 `PlanetEditBrush::ClampSize / GetHalfExtentCm / GetEditCenterWorld`；
  Task 2 的 `PlanetBrushApplicator::Apply` 与 `MinBrushSizeVoxels` / `MaxBrushSizeVoxels` / `bMultiThreadedEdits`。
- Produces:
  - `struct FPlanetEditTarget { AVoxelPlanetActor* Planet; FVector HitLocation; FVector HitNormal; }`
  - `using FPlanetEditTargetResolver = TFunction<bool(const FVector& RayOrigin, const FVector& RayDirection, float MaxDistanceCm, FPlanetEditTarget& OutTarget)>`
  - `void SetTargetResolver(FPlanetEditTargetResolver InResolver)`
  - `void SetGridPlanet(AVoxelPlanetActor* Planet)`
  - `void ConfigureBrushRange(int32 InMinSize, int32 InMaxSize, int32 InitialSize)`
  - `bool TryResolveTarget(const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const`
  - `int32 GetMinBrushSizeVoxels() const` / `int32 GetMaxBrushSizeVoxels() const`
  - `const FString& GetPlaceActionHint() const`
  - 成员 `PlaceActionHint`（`EditAnywhere`，默认 `TEXT("右键点放")`）
  - `virtual void BeginPlay() override`

这个任务的核心是**把「怎么找行星」从组件里拿出去**。改完组件只管两件与 Owner 无关的事：
把屏幕射线变成目标、拿目标去改体素。角色注入 LineTrace 解析器，星图注入体素行进解析器。
它也是一次**行为保持的重构**：角色的一切表现（笔刷 1..8、预览线、编辑网格）必须与改动前逐项相同。

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEra/Private/PlanetEditTests.cpp` 末尾（`#endif` 之前）追加：

```cpp
// 笔刷尺寸区间：角色 1..8、星图 8..128 共用同一份夹取逻辑。
// 星图默认 32，而旧的 1 参 ClampSize 会把它悄悄夹成 8 —— 这就是本测试要钉的回归。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditComponentRangeTest,
	"StellarEra.Planet.EditComponent.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditComponentRangeTest::RunTest(const FString& Parameters)
{
	UPlanetEditComponent* Edit = NewObject<UPlanetEditComponent>();

	// 默认就是角色的区间与提示：老组件不该因为新增配置项改变行为。
	TestEqual(TEXT("默认下限 1"), Edit->GetMinBrushSizeVoxels(), 1);
	TestEqual(TEXT("默认上限 8"), Edit->GetMaxBrushSizeVoxels(), 8);
	TestEqual(TEXT("默认提示"), Edit->GetPlaceActionHint(), FString(TEXT("右键点放")));
	Edit->AdjustBrushSize(100);
	TestEqual(TEXT("+100 夹到 8"), Edit->GetBrushSizeVoxels(), 8);
	Edit->AdjustBrushSize(-100);
	TestEqual(TEXT("-100 夹到 1"), Edit->GetBrushSizeVoxels(), 1);

	// 星图：8..128、默认 32。
	Edit->ConfigureBrushRange(8, 128, 32);
	TestEqual(TEXT("星图默认 32"), Edit->GetBrushSizeVoxels(), 32);
	TestEqual(TEXT("星图下限 8"), Edit->GetMinBrushSizeVoxels(), 8);
	TestEqual(TEXT("星图上限 128"), Edit->GetMaxBrushSizeVoxels(), 128);
	Edit->AdjustBrushSize(1000);
	TestEqual(TEXT("+1000 夹到 128"), Edit->GetBrushSizeVoxels(), 128);
	Edit->AdjustBrushSize(-1000);
	TestEqual(TEXT("−1000 夹到 8"), Edit->GetBrushSizeVoxels(), 8);

	// 区间写反时不崩、保下限（与 PlanetEditBrush::ClampSize 的约定一致）。
	Edit->ConfigureBrushRange(10, 2, 0);
	TestEqual(TEXT("上下限写反保下限"), Edit->GetBrushSizeVoxels(), 10);
	TestEqual(TEXT("上下限写反时上限被抬到下限"), Edit->GetMaxBrushSizeVoxels(), 10);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Run: 构建（见 Global Constraints 的 UBT 命令）。
Expected: **编译失败**，报 `no member named 'GetMinBrushSizeVoxels'` / `'ConfigureBrushRange'` /
`'GetPlaceActionHint'`。这就是红。

- [ ] **Step 3: 整文件替换头文件**

`Source/StellarEraVehicle/Public/PlanetEditComponent.h` 全量替换为：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星编辑组件：模式、笔刷状态、屏幕射线解析、体素工具调用。
//
// 角色与星图相机共用本组件。两者唯一的差别是「怎么把屏幕射线变成编辑目标」：
// 角色离地表几米，用 LineTrace；星图离地表几千公里、碰撞体根本够不着，得用
// 解析球面 + 体素行进。这个差别由 Owner 通过 SetTargetResolver 注入。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetEditBrush.h"
#include "Templates/Function.h"
#include "PlanetEditComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;

/** 一次屏幕射线解析出的编辑目标。全部是世界空间。 */
struct FPlanetEditTarget
{
	AVoxelPlanetActor* Planet = nullptr;
	FVector HitLocation = FVector::ZeroVector;
	FVector HitNormal = FVector::UpVector;
};

/**
 * 屏幕射线 → 编辑目标。
 *
 * 返回 false 表示没打到可编辑表面。实现方负责自己尊重 MaxDistanceCm ——
 * 调用方把「最远打多远」原样传进来，不会替实现方截断射线。
 */
using FPlanetEditTargetResolver = TFunction<bool(
	const FVector& RayOrigin, const FVector& RayDirection, float MaxDistanceCm,
	FPlanetEditTarget& OutTarget)>;

UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAVEHICLE_API UPlanetEditComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetEditComponent();

	//~ Begin UActorComponent Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	bool IsEditMode() const { return bEditMode; }
	EPlanetBrushShape GetBrushShape() const { return BrushShape; }
	int32 GetBrushSizeVoxels() const { return BrushSizeVoxels; }
	int32 GetMinBrushSizeVoxels() const { return MinBrushSizeVoxels; }
	int32 GetMaxBrushSizeVoxels() const { return MaxBrushSizeVoxels; }
	const FString& GetPlaceActionHint() const { return PlaceActionHint; }

	/**
	 * 装「屏幕射线 → 编辑目标」的解析器。由 Owner 在 BeginPlay 里调一次。
	 *
	 * 没装时 TryResolveTarget 恒 false：组件只显示预览、不落笔。
	 */
	void SetTargetResolver(FPlanetEditTargetResolver InResolver);

	/**
	 * 编辑网格归哪颗行星。角色在 BeginPlay / 换星时更新；
	 * 星图从不调用 —— 星图里不该出现贴着地表的网格。
	 */
	void SetGridPlanet(AVoxelPlanetActor* Planet);

	/**
	 * 配置笔刷尺寸区间与初始尺寸。星图 Pawn 在构造函数里调（8..128，默认 32）；
	 * 角色不调，用成员默认值 1..8。
	 */
	void ConfigureBrushRange(int32 InMinSize, int32 InMaxSize, int32 InitialSize);

	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/** ±1（滚轮 / 方括号）；内部夹到 [MinBrushSizeVoxels, MaxBrushSizeVoxels]。 */
	void AdjustBrushSize(int32 Delta);

	bool DigAtScreenPosition(const FVector2D& ScreenPosition);
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/**
	 * 屏幕位置 → 编辑目标。只解析，不写体素。
	 *
	 * 控制器用它判断「这次点击该不该归编辑管」：解析成功就消费掉这次点击，
	 * 失败才交回给星图的天体选择。预览也走同一条路。
	 */
	bool TryResolveTarget(const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const;

	void SetPreviewPlacing(bool bPlacing);

	/** 射线最远距离（厘米）。角色默认 1000 米；星图设 50 AU。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

	/** 笔刷尺寸区间（体素数，直径 / 边长）。角色 1..8，星图 8..128。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MinBrushSizeVoxels = 1;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "128"))
	int32 MaxBrushSizeVoxels = 8;

	/** 体素工具是否并行。小笔刷单线程延迟更低，128 体素的大笔刷必须并行。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	bool bMultiThreadedEdits = false;

	/** HUD 提示行里的「怎么放」。角色「右键点放」，星图「Shift+左键填」。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	FString PlaceActionHint = TEXT("右键点放");

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	void UpdateBrushPreview();
	void EnsurePreviewLineBatch();
	void HideBrushPreview();

	FPlanetEditTargetResolver TargetResolver;
	TWeakObjectPtr<AVoxelPlanetActor> GridPlanet;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> PreviewLineBatch;

	bool bPreviewPlacing = false;
	bool bEditMode = false;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/**
	 * 直径 / 边长（体素数）。区间由 Min/Max 决定，BeginPlay 再夹一次。
	 * 这里刻意不写 ClampMax：上限是每个 Pawn 配的，元数据只能写死一个值。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 BrushSizeVoxels = 2;
};
```

> `TWeakObjectPtr<AVoxelPlanetActor>` 用前向声明就够：UE 5.8 的
> `WeakObjectPtrTemplates.h:66` 明确写了要支持 `TWeakObjectPtr<UUndefinedClass>`。

- [ ] **Step 4: 改实现（六处）**

**(a) 头文件包含**：`.cpp` 开头（第 3–15 行）整段替换为：

```cpp
#include "PlanetEditComponent.h"

#include "PlanetBrushApplicator.h"
#include "VoxelPlanetActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "VoxelVector.h"
```

删掉的是：`PlanetCharacter.h`（不再认识角色类型）、`CollisionQueryParams.h` 与 `SceneTypes.h`
（射线搬到角色侧）、`VoxelIntBox.h` / `VoxelBoxTools.h` / `VoxelSphereTools.h`（Tool 调用搬到
`PlanetBrushApplicator`）。`VoxelVector.h` 必须留 —— 预览的方块路径还在用 `FVoxelVector`。

**(b) 替换 `EndPlay` 与 `ResolvePlanet`**：把从 `void UPlanetEditComponent::EndPlay(` 到
`AVoxelPlanetActor* UPlanetEditComponent::ResolvePlanet() const { ... }` 的整段
（当前第 28–42 行）替换为：

```cpp
void UPlanetEditComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 角色被销毁时把网格收掉，否则切换 Pawn 的路径漏了退出就会留下网格。
	if (bEditMode)
	{
		SetEditMode(false);
	}

	// 解析器通常捕获 Owner 自己；Owner 正在销毁，留着就是悬空引用。
	TargetResolver = nullptr;
	GridPlanet.Reset();

	Super::EndPlay(EndPlayReason);
}

void UPlanetEditComponent::BeginPlay()
{
	Super::BeginPlay();

	// BrushSizeVoxels 是 EditAnywhere：蓝图或关卡实例里可能存着区间外的旧值
	// （例如从 1..8 的组件改成 8..128 的星图 Pawn），进游戏时按区间夹一次。
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		BrushSizeVoxels, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}

void UPlanetEditComponent::SetTargetResolver(FPlanetEditTargetResolver InResolver)
{
	TargetResolver = MoveTemp(InResolver);
}

void UPlanetEditComponent::SetGridPlanet(AVoxelPlanetActor* Planet)
{
	if (GridPlanet.Get() == Planet)
	{
		return;
	}

	// 换星时先把旧星上的网格收掉，再决定新星要不要亮。这条路径原先写在
	// APlanetCharacter::ReevaluatePlanet 里，现在收进组件：角色调它，星图不调。
	if (bEditMode)
	{
		if (AVoxelPlanetActor* Previous = GridPlanet.Get())
		{
			if (IsValid(Previous))
			{
				Previous->SetEditGridVisible(false);
			}
		}
		if (IsValid(Planet))
		{
			Planet->SetEditGridVisible(true);
		}
	}

	GridPlanet = Planet;
}

void UPlanetEditComponent::ConfigureBrushRange(
	const int32 InMinSize, const int32 InMaxSize, const int32 InitialSize)
{
	MinBrushSizeVoxels = FMath::Max(InMinSize, 1);
	MaxBrushSizeVoxels = FMath::Max(InMaxSize, MinBrushSizeVoxels);
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		InitialSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}
```

**(c) `SetEditMode`**：尾部的行星块替换（`ResolvePlanet()` 已经不存在）：

```cpp
	// 网格归属由 SetGridPlanet 决定（角色 = 脚下行星，星图 = 空）。
	// 缓存的行星在拆卸阶段可能已经失效，先验一下。
	if (AVoxelPlanetActor* Planet = GridPlanet.Get())
	{
		if (IsValid(Planet))
		{
			Planet->SetEditGridVisible(bEditMode);
		}
	}
```

**(d) `AdjustBrushSize`**：

```cpp
void UPlanetEditComponent::AdjustBrushSize(const int32 Delta)
{
	BrushSizeVoxels = PlanetEditBrush::ClampSize(
		BrushSizeVoxels + Delta, MinBrushSizeVoxels, MaxBrushSizeVoxels);
}
```

**(e) `ApplyBrushAtScreenPosition`** 整个函数替换为：

```cpp
bool UPlanetEditComponent::ApplyBrushAtScreenPosition(
	const FVector2D& ScreenPosition,
	const bool bPlace)
{
	FPlanetEditTarget Target;
	if (!TryResolveTarget(ScreenPosition, Target))
	{
		return false;
	}

	const float VoxelSize = FMath::Max(Target.Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Target.HitLocation, Target.HitNormal, BrushSizeVoxels, VoxelSize, bPlace,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);

	return PlanetBrushApplicator::Apply(*Target.Planet, Center, BrushShape, BrushSizeVoxels,
		MinBrushSizeVoxels, MaxBrushSizeVoxels, bPlace, bMultiThreadedEdits);
}
```

**(f) 在 `DigAtScreenPosition` / `PlaceAtScreenPosition` 之后插入 `TryResolveTarget`**：

```cpp
bool UPlanetEditComponent::TryResolveTarget(
	const FVector2D& ScreenPosition, FPlanetEditTarget& OutTarget) const
{
	if (!bEditMode || !TargetResolver)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	if (!PlayerController)
	{
		return false;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	// Deproject 的参数是 float；本项目警告即错误，double → float 必须显式转。
	if (!PlayerController->DeprojectScreenPositionToWorld(
		static_cast<float>(ScreenPosition.X), static_cast<float>(ScreenPosition.Y),
		WorldOrigin, WorldDirection))
	{
		return false;
	}
	if (WorldDirection.IsNearlyZero())
	{
		return false;
	}

	OutTarget = FPlanetEditTarget();
	if (!TargetResolver(WorldOrigin, WorldDirection, MaxEditDistanceCm, OutTarget))
	{
		return false;
	}

	// 解析器只负责「往哪打」，可编辑性在这里统一把关：行星必须还有体素世界。
	return IsValid(OutTarget.Planet) && OutTarget.Planet->IsCreated();
}
```

**(g) `UpdateBrushPreview`**：把「取行星 → 射线 → 算中心」那段（当前第 251–262 行，
从 `AVoxelPlanetActor* Planet = ResolvePlanet();` 到 `... bPreviewPlacing);`）替换为：

```cpp
	FPlanetEditTarget Target;
	if (!TryResolveTarget(FVector2D(MouseX, MouseY), Target))
	{
		HideBrushPreview();
		return;
	}

	AVoxelPlanetActor* const Planet = Target.Planet;
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Target.HitLocation, Target.HitNormal, BrushSizeVoxels, VoxelSize, bPreviewPlacing,
		MinBrushSizeVoxels, MaxBrushSizeVoxels);
```

同一函数里球体分支的半径调用（当前第 267 行）也要带区间，否则 128 会被算成 8 的一半：

```cpp
		const float Radius = PlanetEditBrush::GetHalfExtentCm(
			BrushSizeVoxels, VoxelSize, MinBrushSizeVoxels, MaxBrushSizeVoxels);
```

> 删掉 `ResolvePlanet` 之后，`Planet` 在本函数里变成了 `Target.Planet` 的别名 ——
> 下面方块分支的 `Planet->GlobalToLocalFloat` / `LocalToGlobalFloat` 都不用改。

- [ ] **Step 5: 构建 + 跑全部测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；`StellarEra.Planet.EditComponent.Range` 通过；原有
`StellarEra.Planet.EditComponent.State` / `.Defaults` / `.SpherePreview` / `.BoxPreview` 仍全过
（角色行为没变）。

- [ ] **Step 6: 提交**

```bash
git commit -m "refactor(行星编辑): 组件通用化，射线解析器改由 Owner 注入" -- Source/StellarEraVehicle/Public/PlanetEditComponent.h Source/StellarEraVehicle/Private/PlanetEditComponent.cpp Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 4: 角色侧注入 LineTrace 解析器

**Files:**
- Modify: `Source/StellarEraVehicle/Private/PlanetCharacter.cpp`

**Interfaces:**
- Consumes: Task 3 的 `SetTargetResolver` / `FPlanetEditTarget` / `FPlanetEditTargetResolver` / `SetGridPlanet`。
- Produces: 无新对外接口。角色从此完全靠注入的解析器找编辑目标。

这一步**没有自动化测试**：它需要真实世界 + 体素碰撞体，而自动化 runner 跑的是 editor world、
不执行 BeginPlay，`UniverseMap` 也起不来碰撞。它的验收放在 Task 10 的 PIE 清单里
（角色回归：滚轮 1..8、右键放、预览与网格不变）。本任务的红/绿是**构建 + 全量测试仍全过**，
以及下面逐条对照旧实现的两通道射线语义。

- [ ] **Step 1: 加射线辅助函数**

`PlanetCharacter.cpp` 顶部那个匿名 `namespace { ... }`（第 21–26 行，当前只有
`FallbackMeshPath`）里追加：

```cpp
	/**
	 * 角色的「屏幕射线 → 编辑目标」：普通碰撞射线。
	 *
	 * 角色站在地表、射线只打一千米，LineTrace 最直接：它天然尊重体素碰撞，
	 * 也天然被地形挡住。星图不能这么做 —— 那里离地表几千公里，碰撞范围只有
	 * 几十米，射线什么也打不到。这正是把解析器做成可注入的原因。
	 *
	 * 逐字保留旧 TraceEditTarget 的语义：Visibility 优先，整条射线打空才退到
	 * WorldStatic；命中的 Actor 必须是脚下那颗行星。
	 */
	bool TraceTargetForCharacter(
		APlanetCharacter& Character,
		AVoxelPlanetActor* Planet,
		const FVector& RayOrigin,
		const FVector& RayDirection,
		const float MaxDistanceCm,
		FPlanetEditTarget& OutTarget)
	{
		if (!IsValid(Planet) || !Planet->IsCreated())
		{
			return false;
		}

		const UWorld* World = Character.GetWorld();
		if (!World)
		{
			return false;
		}

		FCollisionQueryParams Params(
			SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
		Params.AddIgnoredActor(&Character);

		const FVector End = RayOrigin + RayDirection * MaxDistanceCm;

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_Visibility, Params))
		{
			Hit = FHitResult();
			if (!World->LineTraceSingleByChannel(Hit, RayOrigin, End, ECC_WorldStatic, Params))
			{
				return false;
			}
		}

		if (Hit.GetActor() != Planet)
		{
			return false;
		}

		OutTarget.Planet = Planet;
		OutTarget.HitLocation = Hit.Location;
		OutTarget.HitNormal = Hit.ImpactNormal;
		return true;
	}
```

同时补三个包含（`PlanetCharacter.cpp` 当前没有）：

```cpp
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneTypes.h"
```

- [ ] **Step 2: 在 `BeginPlay` 里装解析器**

`APlanetCharacter::BeginPlay` 里，`PlanetActor = FindNearestPlanet();` 之前插入：

```cpp
	// 解析器必须在「找不到行星」的早退之前装好：它读的是 PlanetActor 的当前值，
	// 而不是安装时的快照，所以先装后查没有时序问题；反过来（查到之后才装）
	// 一旦此刻没找到行星，早退就把安装也跳过了，再也补不上。
	if (PlanetEdit)
	{
		PlanetEdit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return TraceTargetForCharacter(
					*this, PlanetActor.Get(), RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}
```

紧跟 `PlanetActor = FindNearestPlanet();` 之后、`if (!PlanetActor)` 之前加：

```cpp
	if (PlanetEdit)
	{
		// 网格只跟角色走。星图 Pawn 从不调这个，所以星图里没有地表网格。
		PlanetEdit->SetGridPlanet(PlanetActor);
	}
```

- [ ] **Step 3: `ReevaluatePlanet` 改成调组件**

`APlanetCharacter::ReevaluatePlanet`（当前第 441–474 行）整函数替换为：

```cpp
void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* Nearest = FindNearestPlanet();
	if (Nearest == PlanetActor)
	{
		return;
	}

	// 换星：旧星已经不在脚下了（或者本来就没找到）。把「已落地」清掉 ——
	// 新星的地表还没流送出来，得让 Tick 末尾的兜底逻辑重新把角色按到球面上。
	PlanetActor = Nearest;
	bHasLanded = false;

	if (PlanetActor)
	{
		// 内部会把 LastSurfaceUp 清零，相机基座下一帧重建。
		SnapToPlanetSurface();
	}

	// 编辑网格跟着角色走：组件内部会先把旧星的网格收掉再点亮新星。
	if (PlanetEdit)
	{
		PlanetEdit->SetGridPlanet(PlanetActor);
	}
}
```

> **必须删掉旧函数体第一行的 `AVoxelPlanetActor* const OldPlanet = PlanetActor;`**。
> 网格块搬进组件后它没有别的用处，留着就是一个未使用变量 —— 本工程警告即错误，
> 构建会直接失败（C4189）。

- [ ] **Step 4: 构建 + 跑全部测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；全量测试通过。

- [ ] **Step 5: 提交**

```bash
git commit -m "refactor(行星角色): 编辑目标改由角色注入 LineTrace 解析器" -- Source/StellarEraVehicle/Private/PlanetCharacter.cpp
```

---

### Task 5: `PlanetSurfaceRay`（解析球面求交，纯数学）

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetSurfaceRay.h`
- Create: `Source/StellarEraPlanet/Private/PlanetSurfaceRay.cpp`
- Create: `Source/StellarEra/Private/PlanetSurfaceRayTests.cpp`
- Modify: `Source/StellarEra/Private/StarMapCameraPawn.cpp`（`RaySphereFirstHitCm` 改为转发）

**Interfaces:**
- Consumes: 无（纯数学，只依赖 Core）。
- Produces:
  - `double PlanetSurfaceRay::RaySphereFirstHit(const FVector& RayOrigin, const FVector& UnitDir, const FVector& SphereCenter, double SphereRadiusCm)`
  - `bool PlanetSurfaceRay::SolveAnalyticHit(const FVector& LocalOrigin, const FVector& LocalDir, double BaseRadiusCm, int32 Iterations, TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection, double& OutDistanceCm)`

为什么先写这个：星图的射线不能靠碰撞（见 spec §6）。解出的距离是**体素行进**的起点，
两者分层 —— 这一层完全没有 Unreal 状态，可以无头钉死。

- [ ] **Step 1: 写失败的测试**

新建 `Source/StellarEra/Private/PlanetSurfaceRayTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面解析射线：纯数学，无 UObject、无世界。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetSurfaceRay.h"

// 定半径：就是一个普通球面求交。起于球心正下方 10 cm、朝 +Z 打半径 5 的球，
// 穿入点在 z = −5，所以距离 5。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayConstantRadiusTest,
	"StellarEra.Planet.SurfaceRay.ConstantRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayConstantRadiusTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

	double Distance = -1.0;
	const bool bHit = PlanetSurfaceRay::SolveAnalyticHit(
		Origin, Direction, /*BaseRadiusCm=*/5.0, /*Iterations=*/3,
		[](const FVector&) { return 5.0; }, Distance);

	TestTrue(TEXT("定半径命中"), bHit);
	TestTrue(TEXT("距离 ≈ 5"), FMath::IsNearlyEqual(Distance, 5.0, 1e-6));
	return true;
}

// 方向相关半径：朝 −Z 看时半径只有 2（5 + 3 × (−1)），射线从 z = −10 打到
// z = −2，距离正好 8。这一项钉的是「迭代真的按新半径重算了距离」。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayVaryingRadiusTest,
	"StellarEra.Planet.SurfaceRay.VaryingRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayVaryingRadiusTest::RunTest(const FString& Parameters)
{
	const FVector Origin(0.0, 0.0, -10.0);
	const FVector Direction(0.0, 0.0, 1.0);

	// 极轴方向鼓 3 cm。命中点方向恒为 (0,0,−1)，所以两次迭代后半径稳定在 2。
	const auto RadiusAtDirection = [](const FVector& UnitDirection)
	{
		return 5.0 + 3.0 * UnitDirection.Z;
	};

	double Distance = -1.0;
	const bool bHit = PlanetSurfaceRay::SolveAnalyticHit(
		Origin, Direction, /*BaseRadiusCm=*/5.0, /*Iterations=*/3,
		RadiusAtDirection, Distance);

	TestTrue(TEXT("变半径命中"), bHit);
	TestTrue(TEXT("距离 ≈ 8"), FMath::IsNearlyEqual(Distance, 8.0, 1e-6));
	return true;
}

// 拒绝路径：擦不到、起点在球内、半径 0、方向 0、整个球在背后。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRayRejectsTest,
	"StellarEra.Planet.SurfaceRay.Rejects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRayRejectsTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);
	const auto ConstantRadius = [](const FVector&) { return 5.0; };

	double Distance = 123.0;
	TestFalse(TEXT("擦不到球"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 10.0, -10.0), Forward, 5.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("起点在球内"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector::ZeroVector, Forward, 5.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("半径为 0"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, -10.0), Forward, 0.0, 3, ConstantRadius, Distance));
	TestFalse(TEXT("方向为 0"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, -10.0), FVector::ZeroVector, 5.0, 3, ConstantRadius, Distance));

	// 球在射线背后：从 z = +10 往 +Z 打，球心在原点。
	TestFalse(TEXT("球在背后"),
		PlanetSurfaceRay::SolveAnalyticHit(
			FVector(0.0, 0.0, 10.0), Forward, 5.0, 3, ConstantRadius, Distance));
	return true;
}

// RaySphereFirstHit 自身的边界：与 SolveAnalyticHit 分开测，因为星图的
// 轨道遮挡计算也直接用它。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraSurfaceRaySphereHitTest,
	"StellarEra.Planet.SurfaceRay.SphereHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraSurfaceRaySphereHitTest::RunTest(const FString& Parameters)
{
	const FVector Forward(0.0, 0.0, 1.0);
	const FVector Center = FVector::ZeroVector;

	TestTrue(TEXT("穿入点 5"),
		FMath::IsNearlyEqual(
			PlanetSurfaceRay::RaySphereFirstHit(
				FVector(0.0, 0.0, -10.0), Forward, Center, 5.0),
			5.0, 1e-6));
	TestTrue(TEXT("擦不到 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 10.0, -10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("球在背后 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 0.0, 10.0), Forward, Center, 5.0) < 0.0);
	TestTrue(TEXT("半径 0 → −1"),
		PlanetSurfaceRay::RaySphereFirstHit(
			FVector(0.0, 0.0, -10.0), Forward, Center, 0.0) < 0.0);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Run: 构建。
Expected: **编译失败**，`PlanetSurfaceRay.h` 不存在。

- [ ] **Step 3: 写头文件**

`Source/StellarEraPlanet/Public/PlanetSurfaceRay.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星表面的解析射线：球面求交 + 逐方向半径的不动点迭代。
// 纯数学，不碰 UObject，全部可无头测试。

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

namespace PlanetSurfaceRay
{
	/**
	 * 射线与球面的第一个穿入点距离。打不到 / 起点在球内 / 整个球在背后 → −1。
	 *
	 * 本函数原来叫 AStarMapCameraPawn::RaySphereFirstHitCm（星图摄像机用它做
	 * 行星遮挡夹取）；搬到本模块后两边共用一个实现，星图那个函数只做转发。
	 */
	STELLARERAPLANET_API double RaySphereFirstHit(
		const FVector& RayOrigin, const FVector& UnitDir,
		const FVector& SphereCenter, double SphereRadiusCm);

	/**
	 * 球面不平（地形起伏）时的解析命中距离。
	 *
	 * 先用 BaseRadiusCm 求一个球面交点，再按该方向上的真实半径重算交点，迭代
	 * Iterations 次。地形起伏远小于半径，固定点收敛得很快；某次迭代算出的半径
	 * 让射线打空时保留上一次的距离，而不是整体失败。
	 *
	 * 坐标：调用方给**同一原点的局部坐标**（起点、方向、半径都是），本函数不关心
	 * 它是世界空间还是体素空间。半径采样回调吃单位方向、吐半径。
	 */
	STELLARERAPLANET_API bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		double BaseRadiusCm,
		int32 Iterations,
		TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm);
}
```

- [ ] **Step 4: 写实现**

`Source/StellarEraPlanet/Private/PlanetSurfaceRay.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSurfaceRay.h"

namespace PlanetSurfaceRay
{
	double RaySphereFirstHit(
		const FVector& RayOrigin,
		const FVector& UnitDir,
		const FVector& SphereCenter,
		const double SphereRadiusCm)
	{
		if (SphereRadiusCm <= 0.0)
		{
			return -1.0;
		}

		const FVector Direction = UnitDir.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return -1.0;
		}

		// 射线 P(t) = Origin + Direction × t。把球心投到射线上：投影长度 Projection
		// 对应最近点，垂距平方 PerpSq = |ToCenter|² − Projection²。
		const FVector ToCenter = SphereCenter - RayOrigin;
		const double Projection = ToCenter.Dot(Direction);
		const double PerpSq = ToCenter.SizeSquared() - Projection * Projection;
		const double RadiusSq = SphereRadiusCm * SphereRadiusCm;
		if (PerpSq > RadiusSq)
		{
			return -1.0; // 射线擦不到球
		}

		// 半弦长：Entry 是穿入点，Exit 是穿出点。
		const double HalfChord = FMath::Sqrt(RadiusSq - PerpSq);
		const double Entry = Projection - HalfChord;
		const double Exit = Projection + HalfChord;

		// Exit ≤ 0：整个球在射线背后；Entry ≤ 0 < Exit：起点在球内。
		// 两种都不给约束 —— 返回 −1，避免调用方把缩放锁死在 0。
		return (Exit > 0.0 && Entry > 0.0) ? Entry : -1.0;
	}

	bool SolveAnalyticHit(
		const FVector& LocalOrigin,
		const FVector& LocalDir,
		const double BaseRadiusCm,
		const int32 Iterations,
		const TFunctionRef<double(const FVector& UnitDirection)> RadiusAtDirection,
		double& OutDistanceCm)
	{
		if (BaseRadiusCm <= 0.0)
		{
			return false; // 半径为 0 的球根本挡不住射线
		}

		const FVector Direction = LocalDir.GetSafeNormal();
		if (Direction.IsNearlyZero())
		{
			return false;
		}

		// 起点在基准球内：没有「穿入点」。调用方永远在地表外，走到这里说明参数错了。
		if (LocalOrigin.SizeSquared() < BaseRadiusCm * BaseRadiusCm)
		{
			return false;
		}

		double DistanceCm = RaySphereFirstHit(
			LocalOrigin, Direction, FVector::ZeroVector, BaseRadiusCm);
		if (DistanceCm < 0.0)
		{
			return false;
		}

		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			const FVector Hit = LocalOrigin + Direction * DistanceCm;
			const FVector HitDirection = Hit.GetSafeNormal();
			if (HitDirection.IsNearlyZero())
			{
				break;
			}

			const double LocalRadius = RadiusAtDirection(HitDirection);
			if (LocalRadius <= 0.0)
			{
				break;
			}

			const double NextDistance = RaySphereFirstHit(
				LocalOrigin, Direction, FVector::ZeroVector, LocalRadius);
			if (NextDistance < 0.0)
			{
				// 这个方向的半径让射线打空（地形凹得太深）：保留上一次的距离。
				break;
			}

			const double Change = FMath::Abs(NextDistance - DistanceCm);
			DistanceCm = NextDistance;
			if (Change < 0.001) // 0.01 毫米，已经远小于体素
			{
				break;
			}
		}

		OutDistanceCm = DistanceCm;
		return true;
	}
}
```

- [ ] **Step 5: 星图 Pawn 的旧实现改为转发**

`Source/StellarEra/Private/StarMapCameraPawn.cpp`：加 `#include "PlanetSurfaceRay.h"`，
然后把 `AStarMapCameraPawn::RaySphereFirstHitCm` 的函数体（当前第 185–221 行整段）
替换为一行转发：

```cpp
double AStarMapCameraPawn::RaySphereFirstHitCm(
	const FVector& RayOrigin,
	const FVector& UnitDir,
	const FVector& SphereCenter,
	double SphereRadiusCm)
{
	// 实现搬到了 StellarEraPlanet/PlanetSurfaceRay：行星编辑的射线也要用它。
	// 这里保留同名静态函数，星图现有的调用点与测试都不用改。
	return PlanetSurfaceRay::RaySphereFirstHit(RayOrigin, UnitDir, SphereCenter, SphereRadiusCm);
}
```

- [ ] **Step 6: 构建 + 跑测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；`StellarEra.Planet.SurfaceRay.*` 四项通过；原有 `StellarEra.StarMap.*`
全过（`RaySphereFirstHitCm` 换了实现但行为逐字相同 —— 这一步就是它的回归证明）。

- [ ] **Step 7: 提交**

```bash
git commit -m "feat(行星): 抽出 PlanetSurfaceRay 解析球面求交，星图复用" -- Source/StellarEraPlanet/Public/PlanetSurfaceRay.h Source/StellarEraPlanet/Private/PlanetSurfaceRay.cpp Source/StellarEra/Private/PlanetSurfaceRayTests.cpp Source/StellarEra/Private/StarMapCameraPawn.cpp
```

---

### Task 6: `AVoxelPlanetActor::RaycastSurfaceCm`（解析 + 体素符号行进）

**Files:**
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`（在 `GetSurfaceRadiusCmAtDirection` 之后加声明）
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`（加实现与包含）

**Interfaces:**
- Consumes: Task 5 的 `PlanetSurfaceRay::SolveAnalyticHit`。
- Produces: `bool AVoxelPlanetActor::RaycastSurfaceCm(const FVector& RayOriginWorld, const FVector& RayDirectionWorld, float MaxDistanceCm, FVector& OutHitLocationWorld, FVector& OutHitNormalWorld, float& OutDistanceCm) const`
  —— **只在返回 true 时写 Out 参数**。

这是星图编辑能工作的关键：解析给出「未编辑过的理想地表」，体素行进把上一刀挖出来的坑
找回来。**四层单位在这里汇合**，写代码时盯住两件事：解析在厘米空间，行进在体素空间。

- [ ] **Step 1: 加声明**

`Source/StellarEraPlanet/Public/VoxelPlanetActor.h`，紧接 `GetSurfaceRadiusCmAtDirection`
的声明（当前第 105 行）之后插入：

```cpp
	/**
	 * 世界空间射线 → 行星**体素表面**的命中点（LOD0 数据，与渲染 LOD 无关）。
	 *
	 * 星图用：那里摄像机离地表几千公里，体素碰撞只覆盖几十米，LineTrace 什么也打不到。
	 * 两步：先用随方向变化的半径解析求交（PlanetSurfaceRay），再从该点沿射线做体素
	 * 符号行进，找空/实翻转的格子。误差 ≤ 1 体素。
	 *
	 * 只在返回 true 时写 Out 参数。
	 *
	 * @param MaxDistanceCm        命中点超过这个距离就返回 false（调用方靠它做最近命中剪枝）。
	 * @param OutHitLocationWorld  命中点（世界空间）。
	 * @param OutHitNormalWorld    径向外法线（世界空间）。
	 * @param OutDistanceCm        射线原点到命中点的距离（厘米）。
	 */
	bool RaycastSurfaceCm(const FVector& RayOriginWorld, const FVector& RayDirectionWorld,
		float MaxDistanceCm, FVector& OutHitLocationWorld, FVector& OutHitNormalWorld,
		float& OutDistanceCm) const;
```

- [ ] **Step 2: 加包含**

`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` 的包含区（第 1–35 行）追加：

```cpp
#include "PlanetSurfaceRay.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelIntBox.h"
#include "VoxelValue.h"
#include "VoxelVector.h"
```

- [ ] **Step 3: 写实现**

`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`，在 `GetSurfaceRadiusCmAtDirection`
定义结束（当前第 734 行）之后插入：

```cpp
bool AVoxelPlanetActor::RaycastSurfaceCm(
	const FVector& RayOriginWorld,
	const FVector& RayDirectionWorld,
	const float MaxDistanceCm,
	FVector& OutHitLocationWorld,
	FVector& OutHitNormalWorld,
	float& OutDistanceCm) const
{
	const FVector Direction = RayDirectionWorld.GetSafeNormal();
	if (Direction.IsNearlyZero() || MaxDistanceCm <= 0.f || !IsCreated())
	{
		return false;
	}

	// 体素空间与世界空间只差一个等比缩放和行星位置，没有旋转 —— 与
	// GetSurfaceRadiusCmAtDirection 里那条说明同源。解析这一步全程用厘米。
	const FVector PlanetCenterWorld = GetActorLocation();
	const FVector RelativeOrigin = RayOriginWorld - PlanetCenterWorld;
	if (RelativeOrigin.IsNearlyZero())
	{
		return false;
	}

	constexpr int32 AnalyticIterations = 3;

	double AnalyticDistanceCm = -1.0;
	if (!PlanetSurfaceRay::SolveAnalyticHit(
		RelativeOrigin, Direction, static_cast<double>(GetSurfaceRadiusCm()),
		AnalyticIterations,
		[this](const FVector& UnitDirection)
		{
			return static_cast<double>(GetSurfaceRadiusCmAtDirection(UnitDirection));
		},
		AnalyticDistanceCm))
	{
		return false;
	}

	if (AnalyticDistanceCm < 0.0 || AnalyticDistanceCm > MaxDistanceCm)
	{
		return false;
	}

	// 解析结果是「生成器的理想地表」，实际数据可能被挖低了整整一笔刷，也可能因为
	// 量化差一格。从解析命中点出发沿射线找空/实翻转。窗口取 128 体素，与星图笔刷
	// 的尺寸上限一致：一笔刷能挖多深，就能从多深的地面重新瞄回来。更深的竖井需要
	// 从坑沿往下打第二次 —— 这是刻度，不是 bug。
	constexpr int32 SurfaceRayWindowVoxels = 128;

	// 解析点换算成体素坐标，之后的行进全在体素空间（单位 = 体素）。
	const FVoxelVector HitLocalVoxels = GlobalToLocalFloat(
		RayOriginWorld + Direction * AnalyticDistanceCm);
	const FVector HitLocal(HitLocalVoxels.X, HitLocalVoxels.Y, HitLocalVoxels.Z);
	const FVector LocalDir = GetActorTransform()
		.InverseTransformVectorNoScale(Direction).GetSafeNormal();

	// 锁只盖住行进窗口（±Window），不盖整条射线：星图的射线长 50 AU，按射线原点
	// 与终点算锁会锁住整颗行星的大半个八叉树。
	const FVector MarchPlus = HitLocal + LocalDir * SurfaceRayWindowVoxels;
	const FVector MarchMinus = HitLocal - LocalDir * SurfaceRayWindowVoxels;
	const auto FloorVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::FloorToDouble(Value));
	};
	const auto CeilVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::CeilToDouble(Value));
	};

	const FIntVector ScanMin(
		FloorVoxel(FMath::Min3(HitLocal.X, MarchPlus.X, MarchMinus.X)) - 2,
		FloorVoxel(FMath::Min3(HitLocal.Y, MarchPlus.Y, MarchMinus.Y)) - 2,
		FloorVoxel(FMath::Min3(HitLocal.Z, MarchPlus.Z, MarchMinus.Z)) - 2);
	const FIntVector ScanMax(
		CeilVoxel(FMath::Max3(HitLocal.X, MarchPlus.X, MarchMinus.X)) + 2,
		CeilVoxel(FMath::Max3(HitLocal.Y, MarchPlus.Y, MarchMinus.Y)) + 2,
		CeilVoxel(FMath::Max3(HitLocal.Z, MarchPlus.Z, MarchMinus.Z)) + 2);

	// FVoxelIntBox 的 Max 是开区间，所以 +1。
	const FVoxelIntBox ScanBounds(ScanMin, ScanMax + FIntVector(1, 1, 1));

	const FVoxelReadScopeLock Lock(GetData(), ScanBounds, TEXT("PlanetSurfaceRaycast"));

	const auto SampleEmpty = [this, &HitLocal, &LocalDir](const int32 Step) -> bool
	{
		const FVector Sample = HitLocal + LocalDir * Step;
		const FIntVector Voxel(
			static_cast<int32>(FMath::FloorToInt(Sample.X)),
			static_cast<int32>(FMath::FloorToInt(Sample.Y)),
			static_cast<int32>(FMath::FloorToInt(Sample.Z)));
		return GetData().GetValue(Voxel, /*LOD=*/0).IsEmpty();
	};

	// 从解析点往里走是「空 → 实」（正常俯视），往外走是「实 → 空」（估计高了一格）。
	// 两个方向都试，失败面就只剩「窗口不够」这一种。
	const bool bStartsEmpty = SampleEmpty(0);
	const int32 StepSign = bStartsEmpty ? 1 : -1;

	int32 BoundaryStep = 0;
	for (int32 Step = 1; Step <= SurfaceRayWindowVoxels; ++Step)
	{
		if (SampleEmpty(StepSign * Step) != bStartsEmpty)
		{
			BoundaryStep = Step;
			break;
		}
	}

	if (BoundaryStep == 0)
	{
		return false;
	}

	// 边界取两个采样点的中点：真实表面一定在这两格之间，误差 ≤ 1 体素。
	const FVector BoundaryLocal = HitLocal
		+ LocalDir * (static_cast<double>(StepSign) * (BoundaryStep - 0.5));
	const FVector BoundaryWorld = LocalToGlobalFloat(FVoxelVector(BoundaryLocal));
	const float BoundaryDistanceCm =
		static_cast<float>((BoundaryWorld - RayOriginWorld).Size());
	if (BoundaryDistanceCm > MaxDistanceCm)
	{
		return false;
	}

	// **只在成功时写 Out 参数**：调用方拿上一次的最好距离做剪枝，失败时残留的
	// 旧值会被当成命中距离。
	OutHitLocationWorld = BoundaryWorld;
	OutDistanceCm = BoundaryDistanceCm;
	OutHitNormalWorld = (BoundaryWorld - PlanetCenterWorld).GetSafeNormal();
	if (OutHitNormalWorld.IsNearlyZero())
	{
		OutHitNormalWorld = -Direction;
	}
	return true;
}
```

> **为什么要 `FVoxelReadScopeLock`**：`GetData().GetValue` 读的是共享八叉树，
> 渲染线程同时在读写；锁保证这次行进看到的是一致的 LOD0 数据。
> `FVoxelReadScopeLock` 的构造签名是 `(TData&, const FVoxelIntBox&, const FName&)`，
> Read 版 `TData = const FVoxelData`，所以直接传 `GetData()` 即可。
>
> **为什么不用 4 次细化的二分**：`FVoxelValue::GetValue` 只接受 `FIntVector`，体素数据
> 本身就是量化的，二分采不到更细的信息。1 体素步进 + 中点插值已经是这套数据能给到的
> 最高精度。

- [ ] **Step 4: 构建 + 跑测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；全部测试通过。这个函数本身没有自动化测试（需要真实行星与体素数据），
它的验收在 Task 10 的 PIE 清单第 1、2、4 条。

- [ ] **Step 5: 提交**

```bash
git commit -m "feat(行星): RaycastSurfaceCm 解析求交 + 体素符号行进" -- Source/StellarEraPlanet/Public/VoxelPlanetActor.h Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
```

---

### Task 7: 星图相机 Pawn 挂上编辑组件

**Files:**
- Modify: `Source/StellarEra/Public/StarMapCameraPawn.h`
- Modify: `Source/StellarEra/Private/StarMapCameraPawn.cpp`
- Test: `Source/StellarEra/Private/StarMapTests.cpp`（追加到 `#endif` 之前）

**Interfaces:**
- Consumes: Task 3 的 `UPlanetEditComponent`（`SetTargetResolver` / `ConfigureBrushRange` / `bMultiThreadedEdits` / `PlaceActionHint` / `MaxEditDistanceCm`）；Task 6 的 `AVoxelPlanetActor::RaycastSurfaceCm`。
- Produces: `UPlanetEditComponent* AStarMapCameraPawn::GetPlanetEditComponent() const`

星图 Pawn **不 Tick**，编辑组件的预览 Tick 不受影响：`UPlanetEditComponent` 自己在
`TG_PostUpdateWork` 里 tick，与 Owner 是否 tick 无关。

- [ ] **Step 1: 写失败的测试**

`Source/StellarEra/Private/StarMapTests.cpp` 末尾（`#endif` 之前）追加（文件头需要加
`#include "PlanetEditComponent.h"`）：

```cpp
// 星图 Pawn 的编辑组件参数是构造函数里配的，所以 CDO 上就能读到。
// 这一项钉的是「8..128、默认 32、Shift 填、50 AU」四个数字 —— 它们只能在一个地方
// 出现（构造函数），测试替 HUD 与输入层看着它们。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapEditComponentDefaultsTest,
	"StellarEra.StarMap.EditComponentDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapEditComponentDefaultsTest::RunTest(const FString& Parameters)
{
	const AStarMapCameraPawn* PawnCDO = GetDefault<AStarMapCameraPawn>();
	TestNotNull(TEXT("星图 Pawn 有 CDO"), PawnCDO);
	if (!PawnCDO)
	{
		return false;
	}

	const UPlanetEditComponent* Edit = PawnCDO->GetPlanetEditComponent();
	TestNotNull(TEXT("星图 Pawn 带编辑组件"), Edit);
	if (!Edit)
	{
		return false;
	}

	TestEqual(TEXT("默认 32 体素"), Edit->GetBrushSizeVoxels(), 32);
	TestEqual(TEXT("下限 8 体素"), Edit->GetMinBrushSizeVoxels(), 8);
	TestEqual(TEXT("上限 128 体素"), Edit->GetMaxBrushSizeVoxels(), 128);
	TestEqual(TEXT("提示为 Shift 填"), Edit->GetPlaceActionHint(), FString(TEXT("Shift+左键填")));
	TestTrue(TEXT("编辑距离 50 AU"),
		FMath::IsNearlyEqual(Edit->MaxEditDistanceCm, 5.0e7f));
	TestTrue(TEXT("大笔刷走并行"), Edit->bMultiThreadedEdits);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Run: 构建。
Expected: **编译失败**，`AStarMapCameraPawn` 没有 `GetPlanetEditComponent`。

- [ ] **Step 3: 改头文件**

`Source/StellarEra/Public/StarMapCameraPawn.h`：

(a) 前向声明区加一行：

```cpp
class UPlanetEditComponent;
```

(b) 类里 `AStarMapCameraPawn();` 之后加：

```cpp
	virtual void BeginPlay() override;

	/** 星图模式的行星编辑组件。参数（尺寸区间、提示、距离）在构造函数里配好。 */
	UPlanetEditComponent* GetPlanetEditComponent() const;
```

(c) `ComputeOrbitDistanceLimitsCm` 声明之后加：

```cpp
	/**
	 * 屏幕射线 → 最近一颗被射线打到的体素行星的表面点。
	 *
	 * 逐颗行星调用 AVoxelPlanetActor::RaycastSurfaceCm，用「当前最近距离」当
	 * 上限剪枝 —— 射线没穿过的行星在解析那一步就返回了，不会付体素行进的开销。
	 * 不是行星的天体（飞船、角色）直接跳过：它们没有可编辑的体素数据。
	 */
	bool ResolveEditTargetByPlanets(const FVector& RayOrigin, const FVector& RayDirection,
		float MaxDistanceCm, FPlanetEditTarget& OutTarget) const;
```

(d) 前向声明区下方（`FStarMapObstacle` 之后、`UCLASS` 之前）加：

```cpp
struct FPlanetEditTarget;
```

(e) 组件成员：放在 `VoxelInvokerLOD5` 之后：

```cpp
	/**
	 * 行星编辑器。与角色共用同一个组件类，差别只在参数与射线解析器：
	 * 角色 1..8 体素、LineTrace；星图 8..128 体素、解析 + 体素行进。
	 *
	 * 挂在本 Pawn 上而不是行星上：换焦点、换行星都不影响它的生命周期，
	 * 而且它不参与 N 体搬运。
	 */
	UPROPERTY(VisibleAnywhere, Category = "StarMap|Edit")
	TObjectPtr<UPlanetEditComponent> PlanetEdit;
```

- [ ] **Step 4: 改实现**

`Source/StellarEra/Private/StarMapCameraPawn.cpp`：

(a) 包含区加：

```cpp
#include "PlanetEditComponent.h"
```

(b) 构造函数末尾（`VoxelInvokerLOD5` 那段之后、函数结束大括号之前）加：

```cpp
	// --- 行星编辑：与角色共用组件，参数按星图尺度调大 ---
	//
	// 尺寸 8..128 体素：1 体素 = 100 cm（前两颗行星），128 = 128 米级的陨石坑。
	// 默认 32 是「能看出是一个坑、又不至于一点就挖穿」的折中。
	// `[` / `]` 每次 ±1（与角色一致，走 AdjustBrushSize）。
	PlanetEdit = CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"));
	// 非场景组件，不能 SetupAttachment —— UActorComponent 没有这个接口。
	PlanetEdit->ConfigureBrushRange(
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels,
		/*InitialSize=*/32);
	// 128 体素的球在体素空间里要改百万格量级，单线程会卡住整帧。
	PlanetEdit->bMultiThreadedEdits = true;
	// 右键在星图里已经归绕轨，所以「填」改用 Shift+左键。
	PlanetEdit->PlaceActionHint = TEXT("Shift+左键填");
	// 与 MaxOrbitDistanceCm 同一尺度：从最远 50 AU 处也该打得到行星。
	PlanetEdit->MaxEditDistanceCm = 5.0e7f;
```

(c) 构造函数之后（`EnterFrom` 之前）加：

```cpp
void AStarMapCameraPawn::BeginPlay()
{
	Super::BeginPlay();

	// 解析器捕获 this：Pawn 与组件同生共死，组件在 EndPlay 里会清掉它。
	if (PlanetEdit)
	{
		PlanetEdit->SetTargetResolver(
			[this](const FVector& RayOrigin, const FVector& RayDirection,
				const float MaxDistanceCm, FPlanetEditTarget& OutTarget)
			{
				return ResolveEditTargetByPlanets(
					RayOrigin, RayDirection, MaxDistanceCm, OutTarget);
			});
	}
}

UPlanetEditComponent* AStarMapCameraPawn::GetPlanetEditComponent() const
{
	return PlanetEdit;
}
```

(d) 在 `ComputeOrbitDistanceLimitsCm` 之后加：

```cpp
bool AStarMapCameraPawn::ResolveEditTargetByPlanets(
	const FVector& RayOrigin,
	const FVector& RayDirection,
	const float MaxDistanceCm,
	FPlanetEditTarget& OutTarget) const
{
	const UWorld* World = GetWorld();
	const UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return false;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 取最近的一颗：每颗都按「当前最好距离」剪枝，更远的行星连体素行进都不用做。
	float BestDistanceCm = MaxDistanceCm;
	bool bFound = false;
	FVector BestHitLocation = FVector::ZeroVector;
	FVector BestHitNormal = FVector::UpVector;

	for (AActor* Body : Bodies)
	{
		AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body);
		if (!IsValid(Planet))
		{
			// 飞船、角色没有体素数据，不是编辑目标。
			continue;
		}

		FVector HitLocation = FVector::ZeroVector;
		FVector HitNormal = FVector::UpVector;
		float DistanceCm = 0.f;
		if (!Planet->RaycastSurfaceCm(
			RayOrigin, RayDirection, BestDistanceCm, HitLocation, HitNormal, DistanceCm))
		{
			continue;
		}

		BestDistanceCm = DistanceCm;
		BestHitLocation = HitLocation;
		BestHitNormal = HitNormal;
		OutTarget.Planet = Planet;
		bFound = true;
	}

	if (!bFound)
	{
		return false;
	}

	OutTarget.HitLocation = BestHitLocation;
	OutTarget.HitNormal = BestHitNormal;
	return true;
}
```

- [ ] **Step 5: 构建 + 跑测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；`StellarEra.StarMap.EditComponentDefaults` 通过；星图原有测试全过。

- [ ] **Step 6: 提交**

```bash
git commit -m "feat(星图): 相机 Pawn 挂上大尺度行星编辑组件" -- Source/StellarEra/Public/StarMapCameraPawn.h Source/StellarEra/Private/StarMapCameraPawn.cpp Source/StellarEra/Private/StarMapTests.cpp
```

---

### Task 8: 控制器分派（左键挖 / Shift 填 / `[` `]` 改尺寸）

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`

**Interfaces:**
- Consumes: Task 3 的 `TryResolveTarget`（间接）、Task 7 的 `GetPlanetEditComponent`、Task 3 的 `ConfigureBrushRange` 结果。
- Produces: 无新对外接口；输入表新增 `IA_BrushSize`（`[` / `]`）。

**这个任务最容易引入回归**，三条各自独立：

1. 右键在星图里是**转视角**。若 `OnLookActivateStarted/Completed` 继续用通用取组件函数，
   星图右键松开会被当成「放」—— 必须给它们一个只认角色的取法。
2. 滚轮在星图里是**缩放**。笔刷尺寸改用 `[` / `]`，不碰 `IA_Zoom`。
3. 星图里左键**同时**是「挖」和「选天体」。编辑模式下必须先问
   `TryResolveTarget`：打到了才把这次点击判给编辑，没打到才让它继续走选中逻辑 ——
   否则编辑模式会剥夺选天体的能力（spec §7.1 明确要求保留）。

- [ ] **Step 1: 改头文件**

`Source/StellarEra/Public/StellarEraPlayerController.h`：

(a) 替换 `GetEditComponentInEditMode` 的声明与注释：

```cpp
	/**
	 * 当前 Pawn 上、且编辑模式开着的编辑组件，否则 nullptr。
	 * 角色与星图相机各有一个这样的组件，**两者都会命中**。
	 *
	 * 不能用来进入编辑模式：模式关着时它返回空。G 键的切换走 GetEditComponentForPawn。
	 */
	UPlanetEditComponent* GetEditComponentInEditMode() const;

	/**
	 * 只认角色：当前 Possess 的是角色、且编辑模式开着时返回它的组件。
	 *
	 * 右键「点放」的按下/松开用它。**不能用通用版** —— 星图里右键是转视角，
	 * 通用版会让松开右键变成 PlaceAtScreenPosition。
	 */
	UPlanetEditComponent* GetCharacterEditComponentInEditMode() const;
```

(b) `ForceExitEditMode` 的注释改一行（现在两种 Pawn 都管）：

```cpp
	/** Pawn 上开着的编辑模式一律退出。上船 / 进出星图 / 调试换 Pawn 共用。 */
```

(c) `OnEditModeToggle` 的注释替换：

```cpp
	/** G 键：切换编辑模式。角色与星图相机都有编辑组件；飞船等没有，按 G 无反应。 */
```

(d) `OnEditModeToggle` 之后加：

```cpp
	/** `[` / `]`：编辑模式下调笔刷尺寸（±1）。滚轮在两种模式下都有别的用途。 */
	void OnBrushSize(const FInputActionValue& Value);
```

(e) `IA_EditModeToggle` 之后加：

```cpp
	/** 笔刷尺寸（`[` / `]`）。Axis1D，只绑 Started，不随按住每帧刷。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_BrushSize;
```

- [ ] **Step 2: 取组件函数**

`StellarEraPlayerController.cpp` 顶部匿名 `namespace` 里（`MakeSwizzleYXZ` 之后）加：

```cpp
	/** 任意 Pawn 上的编辑组件，不判模式。角色与星图相机各有一个。 */
	UPlanetEditComponent* GetEditComponentForPawn(APawn* InPawn)
	{
		if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(InPawn))
		{
			return PawnCharacter->GetPlanetEditComponent();
		}
		if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(InPawn))
		{
			return StarMap->GetPlanetEditComponent();
		}
		return nullptr;
	}
```

把 `GetEditComponentInEditMode` 与 `ForceExitEditMode` 替换为：

```cpp
UPlanetEditComponent* AStellarEraPlayerController::GetEditComponentInEditMode() const
{
	UPlanetEditComponent* Edit = GetEditComponentForPawn(GetPawn());
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

UPlanetEditComponent* AStellarEraPlayerController::GetCharacterEditComponentInEditMode() const
{
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	UPlanetEditComponent* Edit = PawnCharacter ? PawnCharacter->GetPlanetEditComponent() : nullptr;
	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}

void AStellarEraPlayerController::ForceExitEditMode(APawn* InPawn)
{
	// 形参不能叫 Pawn：AController 自己有个 Pawn 成员，遮蔽会被当成错误（C4458）。
	UPlanetEditComponent* Edit = GetEditComponentForPawn(InPawn);
	if (Edit && Edit->IsEditMode())
	{
		Edit->SetEditMode(false);
	}
}
```

> 注意 `GetEditComponentForPawn` 在匿名 namespace 里、`GetEditComponentInEditMode` 是
> `const` 成员 —— 传进去的是 `GetPawn()` 的返回值（`APawn*`，不是 const），能编译。

- [ ] **Step 3: 右键只认角色**

`OnLookActivateStarted` 与 `OnLookActivateCompleted` 里各有一行
`UPlanetEditComponent* Edit = GetEditComponentInEditMode();`，都改成：

```cpp
	// 只认角色：星图里右键是转视角，通用版会让松开右键变成「放」。
	UPlanetEditComponent* Edit = GetCharacterEditComponentInEditMode();
```

- [ ] **Step 4: G 键两种 Pawn 都管**

`OnEditModeToggle` 整个函数替换为：

```cpp
void AStellarEraPlayerController::OnEditModeToggle(const FInputActionValue& Value)
{
	// 角色与星图相机各有一个编辑组件；飞船等其他 Pawn 上没有，返回空。
	if (UPlanetEditComponent* Edit = GetEditComponentForPawn(GetPawn()))
	{
		Edit->ToggleEditMode();
	}
}
```

- [ ] **Step 5: 星图退出时关掉编辑模式**

`ToggleStarMap` 的退出分支里，`PreviousPawn.Reset();` 之前插入：

```cpp
		// 星图的编辑模式不跟着换 Pawn 走：Possess 回飞船后组件已不在手上，
		// 预览 Tick 与网格都会留在原地。退出即关，重进再按 G。
		ForceExitEditMode(CurrentStarMapPawn);
```

- [ ] **Step 6: 左键按下就编辑，没打到才落回选择**

`OnStarMapSelectStarted` 里，把编辑分支的取组件改成通用版，并把「无条件挖一次」
换成「先 `TryResolveTarget` 判断这一下归不归编辑」。**分支开头到按钮命中段落一字不动**，
只删掉末尾的 `Edit->DigAtScreenPosition(MousePosition); return;` 并换成：

```cpp
		// 这次点击归不归编辑，由「射线打没打到行星」回答。不能用 DigAtScreenPosition
		// 的返回值判断：打到了行星但没改到任何体素（比如往空气里挖）也会返回 false，
		// 那一下就会漏到松开的选中逻辑里。
		FPlanetEditTarget Target;
		if (Edit->TryResolveTarget(MousePosition, Target))
		{
			// 星图：Shift+左键填。角色仍只有右键点放（Shift 对角色不新增行为）。
			const bool bStarMap = Cast<AStarMapCameraPawn>(GetPawn()) != nullptr;
			const bool bPlacing = bStarMap
				&& (IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift));
			if (bPlacing)
			{
				Edit->PlaceAtScreenPosition(MousePosition);
			}
			else
			{
				Edit->DigAtScreenPosition(MousePosition);
			}
			return;
		}

		// 没打到行星：星图继续走下面的「记录按下位置」，松开时照常拾取天体；
		// 角色没有选择逻辑，会被下面的 `if (!Cast<AStarMapCameraPawn>(GetPawn())) return;`
		// 接住 —— 与改动前一致。
	}
```

同时把分支顶上的注释从「角色 + 编辑模式：按钮优先……」改成
「编辑模式（角色或星图）：按钮优先，其次一次笔刷操作。按钮命中不能顺带挖到脚下的地形。」

> `TryResolveTarget` 与随后的 `Dig/Place` 各自解析一次射线。多出来的一次
> `RaycastSurfaceCm` 相对一次百万体素写入可以忽略；换来的是「这次点击归谁」
> 由一个问题回答，而不是靠写入函数的返回值推断。

`OnStarMapSelectCompleted` **不改**：编辑已经在按下时消费掉了，松开只会走原有的
拖拽阈值 + 天体拾取逻辑。

- [ ] **Step 7: 输入资产与回调**

`BuildInputAssets` 里，`IA_EditModeToggle` 的 `MakeAction` 之后加：

```cpp
	IA_BrushSize = MakeAction(this, TEXT("IA_BrushSize"), EInputActionValueType::Axis1D);
```

`MapKey(MappingContext, IA_EditModeToggle, EKeys::G);` 之后加：

```cpp
	// 笔刷尺寸：滚轮在角色（拉镜头）与星图（缩放）里都有别的用途，尺寸单独占
	// 一对键。左括号接 Negate，于是 ] = +1、[ = −1。
	MapKey(MappingContext, IA_BrushSize, EKeys::RightBracket);
	MapKey(MappingContext, IA_BrushSize, EKeys::LeftBracket, { MakeNegate(this) });
```

`SetupInputComponent` 里，`IA_EditModeToggle` 的 `BindAction` 之后加：

```cpp
	// 只绑 Started：Axis1D 的 Triggered 在按住时每帧触发一次，笔刷会瞬间冲到上限。
	EnhancedInput->BindAction(IA_BrushSize, ETriggerEvent::Started, this,
		&AStellarEraPlayerController::OnBrushSize);
```

回调实现（放在 `OnEditModeToggle` 之后）：

```cpp
void AStellarEraPlayerController::OnBrushSize(const FInputActionValue& Value)
{
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		const float Axis = Value.Get<float>();
		if (!FMath::IsNearlyZero(Axis))
		{
			Edit->AdjustBrushSize(Axis > 0.f ? 1 : -1);
		}
	}
}
```

- [ ] **Step 8: 构建 + 跑全部测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；全部测试通过。交互部分（右键仍转视角、滚轮仍缩放）在 Task 10 的
PIE 清单第 7 条验。

- [ ] **Step 9: 提交**

```bash
git commit -m "feat(星图): 左键挖 / Shift 填 / 方括号改笔刷尺寸" -- Source/StellarEra/Public/StellarEraPlayerController.h Source/StellarEra/Private/StellarEraPlayerController.cpp
```

---

### Task 9: HUD 提示与状态面板跟随两种 Pawn

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraHUD.h`
- Modify: `Source/StellarEra/Private/StellarEraHUD.cpp`

**Interfaces:**
- Consumes: Task 7 的 `AStarMapCameraPawn::GetPlanetEditComponent`；Task 3 的 `GetPlaceActionHint`。
- Produces: 无新对外接口。

HUD 原先只认识角色（`GetViewedCharacter`），星图里按 G 之后**什么都不画** —— 玩家
看不到笔刷尺寸，也看不到「怎么填」。这一任务把面板的选择条件从「Pawn 是角色」
换成「Pawn 上有开着的编辑组件」，提示行里的「怎么放」交给组件回答。

- [ ] **Step 1: 换掉访问器声明**

`Source/StellarEra/Public/StellarEraHUD.h` 第 116–117 行：

```cpp
	/** 当前 Possess 的 Pawn 是角色就返回它，否则返回空。 */
	APlanetCharacter* GetViewedCharacter() const;
```

替换为：

```cpp
	/**
	 * 当前视角下开着的编辑组件：角色或星图相机上的那个，没开着就 nullptr。
	 *
	 * 两种 Pawn 的组件类型相同，面板与提示行不需要分两套代码。
	 */
	const UPlanetEditComponent* GetViewedEditComponent() const;
```

`GetViewedCharacter` 全仓库只有一处调用（下面的 `DrawHUD`），随面板条件一起删掉，
不要留下死代码。

- [ ] **Step 2: 替换面板选择条件**

`Source/StellarEra/Private/StellarEraHUD.cpp` 的 `DrawHUD` 里，把「编辑模式面板」
那一段（当前第 98–108 行）替换为：

```cpp
	// 编辑模式面板：角色与星图相机共用同一个组件，谁开着就画谁的。
	if (const UPlanetEditComponent* Edit = GetViewedEditComponent())
	{
		DrawPlanetEditPanel(*Edit);
	}
```

顺手把 `StellarEraHUD.h` 里 `DrawPlanetEditPanel` 的注释从「只在角色的编辑模式下调用」
改成「角色与星图的编辑模式下都会调用」。

- [ ] **Step 3: 替换访问器实现**

把 `GetViewedCharacter` 的实现（当前第 172–178 行）整段替换为：

```cpp
const UPlanetEditComponent* AStellarEraHUD::GetViewedEditComponent() const
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	APawn* ViewedPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!ViewedPawn)
	{
		return nullptr;
	}

	const UPlanetEditComponent* Edit = nullptr;
	if (APlanetCharacter* Character = Cast<APlanetCharacter>(ViewedPawn))
	{
		Edit = Character->GetPlanetEditComponent();
	}
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(ViewedPawn))
	{
		Edit = StarMap->GetPlanetEditComponent();
	}

	return Edit && Edit->IsEditMode() ? Edit : nullptr;
}
```

- [ ] **Step 4: 提示行的「怎么放」改为组件提供**

`DrawPlanetEditPanel` 末尾的提示行（当前第 266–268 行）替换为：

```cpp
	// 「怎么放」由组件说：角色是右键点放，星图是 Shift+左键填。写死在 HUD 里
	// 就等于把两种输入方案各抄一份，改一处忘一处。
	const FString Hint = FString::Printf(
		TEXT("笔刷 %d 体素 · 左键挖 / %s · G 退出"),
		Edit.GetBrushSizeVoxels(), *Edit.GetPlaceActionHint());
```

- [ ] **Step 5: 构建 + 跑全部测试**

Run: 构建 + `Automation RunTests StellarEra`。
Expected: 构建成功；全部测试通过（`StellarEra.UI.EditBrushButtons` 不涉及提示文案）。

- [ ] **Step 6: 提交**

```bash
git commit -m "feat(星图): HUD 编辑面板同时跟随角色与星图相机" -- Source/StellarEra/Public/StellarEraHUD.h Source/StellarEra/Private/StellarEraHUD.cpp
```

---

### Task 10: 全量验证

**Files:** 无（只跑命令与 PIE）

- [ ] **Step 1: 关掉编辑器，完整构建**

编辑器必须完全退出（Live Coding 会锁 DLL）。

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" StellarEraEditor Win64 Development -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: 输出含 `Result: Succeeded`（`Total execution time` 行）。**`Result: Failed` 也要看
是不是只有 DLL 链接失败** —— 那种情况编译其实过了，重跑一次即可；真正的编译错误会
带 `error C....`。

- [ ] **Step 2: 无头跑全量自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "F:/workspace/ue/StellarEra/StellarEra.uproject" -ExecCmds="Automation RunTests StellarEra" -TestExit="Automation Test Queue Empty" -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/starmap-edit-tests.log"
```

Expected: 日志里 `Automation Test Queue Empty`，且没有 `Failed` 测试。
新增应包含：`StellarEra.Planet.EditBrush.ClampSizeRange`、`.BoxBounds128`、
`StellarEra.Planet.EditComponent.Range`、`StellarEra.Planet.SurfaceRay.*`（5 项）、
`StellarEra.StarMap.EditComponentDefaults`。

- [ ] **Step 3: 中文源文件 BOM 检查**

```bash
for f in Source/StellarEraVehicle/Public/PlanetBrushApplicator.h Source/StellarEraVehicle/Private/PlanetBrushApplicator.cpp Source/StellarEraPlanet/Public/PlanetSurfaceRay.h Source/StellarEraPlanet/Private/PlanetSurfaceRay.cpp Source/StellarEra/Private/PlanetSurfaceRayTests.cpp; do printf '%s ' "$f"; head -c 3 "$f" | od -An -tx1; done
```

Expected: 每行都是 ` ef bb bf`。缺 BOM 的文件 MSVC 会按 GBK 读，产生假 IntelliSense 错误。

- [ ] **Step 4: 确认没有意外改动**

```bash
git status --short
```

Expected: 只多出本计划列出的文件。**不要把 `Content/` 下的资产改动混进来** —— 那是
此前会话遗留的，与本计划无关。

- [ ] **Step 5: PIE 验收（UniverseMap）**

打开编辑器、加载 `UniverseMap`，按顺序验：

1. PIE 开始 → 按 `M` 进星图 → 按 `G`：HUD 出现球/方按钮与「笔刷 32 体素 · 左键挖 /
   Shift+左键填 · G 退出」。
2. 滚轮拉近到一颗行星，鼠标指着一块陆地单击：地表出现一个坑，大小约 32 体素。
3. 按住左键拖一下：只在**按下那一刻**挖一下，拖拽过程中不会再挖（一次点击一次编辑，
   不做按住连刷）。
4. 连续按 `]`：数字涨到 32→33→…；按住不放也只涨 1（Started 事件）。
   调到 128 再点一下：明显更大的坑。
5. `Shift`+左键：坑被填回去，预览是青绿色。
6. 等 2 秒（存档防抖），按 `M` 退出再进星图：坑还在。
7. 停止 PIE，重新开始 PIE：坑还在（已经写进 `/Game/Saves/PlanetVoxel/` 的行星存档资产）。
8. 把视角转向另一颗行星，同样能挖（不需要先选中它）。
9. 右键拖拽：视角照转，**不填土**。滚轮：仍然缩放，不改笔刷。
10. 回到角色：滚轮仍是 1..8；`G` 进编辑模式后右键点放、预览线、编辑网格与改动前一致。

若第 2 条（第一次挖）没有任何反应，先看日志有没有 `能量漂移`（三体在 t≈0.84 年炸掉
会让行星飞出射线范围 —— 那是已知问题，与本次改动无关），再确认射线打到的 Actor 确实是
`AVoxelPlanetActor`。

- [ ] **Step 6: 汇报**

把上面每条命令的**实际输出**（Succeeded / 测试计数 / BOM 十六进制 / git status）
写进完成汇报。PIE 部分没有命令，逐条写「看到 / 没看到」。

---

## 完成标准

- 构建 `Result: Succeeded`，`Automation RunTests StellarEra` 全过（含本计划新增 9 项）。
- 星图里 `G` → 左键挖 / `Shift`+左键填 / `[` `]` 改 8..128，编辑写 LOD0。
- 角色侧无回归：滚轮 1..8、右键点放、预览与网格与改动前一致。
- 所有含中文的新源文件带 UTF-8 BOM。
