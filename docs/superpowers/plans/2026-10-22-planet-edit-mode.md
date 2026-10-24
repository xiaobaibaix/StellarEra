# 行星编辑模式 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 角色状态按 G 进入编辑模式后，用左键挖、右键点放，笔刷支持球体与正方体（滚轮调尺寸），HUD 显示两个图标按钮，球体表面叠加一层贴合体素地形的经纬网格，编辑结果写入行星的 `SaveObject`。

**Architecture:** 编辑状态与射线/执行放在角色身上的 `UPlanetEditComponent`（`StellarEraVehicle`）；笔刷几何是无状态纯函数 `PlanetEditBrush`（同模块）。表面网格是行星上的 `UPlanetSurfaceGridComponent`（`StellarEraPlanet`），从 `FVoxelData` 采样而不是解析地形，因此能贴合编辑后的洞与凸起；逐帧把缓存的体素坐标换算成世界坐标后交给 `ULineBatchComponent` 绘制。输入分派与 HUD 在 `StellarEra`；按钮布局与命中是纯函数 `PlanetEditUI`。

**Tech Stack:** Unreal Engine 5.8.3（C++）、VoxelFree 插件（`AVoxelWorld` / `FVoxelData` / `UVoxelSphereTools` / `UVoxelBoxTools` / `UVoxelSaveUtilities`）、Enhanced Input、`AHUD` Canvas、`ULineBatchComponent`、Unreal Automation Test。

**Spec:** `docs/superpowers/specs/2026-10-22-planet-edit-mode-design.md`

## Global Constraints

- 目标平台 Windows / Win64，引擎 **UE 5.8.3**；编辑器是 VS Code，仓库没有 `.sln` / `.vcxproj` 是正常的。
- 模块依赖严格分层：`StellarEra → StellarEraVehicle → StellarEraPlanet → Voxel`，另有 `StellarEra → StellarEraCore`。**`StellarEraVehicle` 不能依赖 `StellarEra`。**
- `StellarEraVehicle` 的 `StellarEraPlanet` / `Voxel` 是**私有**依赖：本计划新增的 Vehicle 公开头**只允许** `CoreMinimal.h`、`PlanetEditBrush.h` 与前向声明，不得 include `VoxelWorld.h`、`VoxelPlanetActor.h` 或任何 `Voxel*` 头。
- **禁止修改** `Plugins/VoxelFree/` 与 `Plugins/VibeUE/` 下任何文件。
- 新建的含中文 `.h` / `.cpp` 必须存成 **UTF-8 with BOM**；已存在文件不要改换行符（仓库 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态）。
- 构建前**必须关闭编辑器**；只使用捆绑 dotnet 直调 UBT，不用 `Build.bat`。
- 自动化测试前缀 `StellarEra.`，用 `UnrealEditor-Cmd` 无头运行；editor world 不执行 `BeginPlay`，所以 BeginPlay 相关行为只能靠 `-game` 或 PIE 手验。
- `FVoxelIntBox(Min, Max)` 的 **Min 与 Max 都含在范围内**；方块边长必须按 `Low=(Size-1)/2, High=Size-1-Low` 计算。
- 单位约定：世界层厘米；`VoxelSize` 是单个体素的厘米数（默认 100）；`MassEarth` / `km/s` 的换算只走 `CosmosUnits::`，本功能不涉及。
- `ULineBatchComponent` 的线段是**世界空间**坐标，不经过组件变换；行星每帧被 N 体子系统搬运，所以网格顶点必须在绘制时换算。
- **不要自动提交。** 只有用户明确要求时才执行各任务的 commit 步骤，且必须把 pathspec 挂在 commit 上：`git commit -m "..." -- <paths>`，禁止裸 `git commit`。

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `Source/StellarEraVehicle/Public/PlanetEditBrush.h` | 笔刷形状枚举、方块体素盒结构、尺寸夹取 / 半边长 / 中心偏移 / 体素盒生成的声明 |
| `Source/StellarEraVehicle/Private/PlanetEditBrush.cpp` | 上述纯函数的实现，不碰世界 |
| `Source/StellarEraVehicle/Public/PlanetEditComponent.h` | 角色身上的编辑组件：模式、笔刷状态、对外入口 |
| `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp` | 屏幕射线、命中过滤、调用 Voxel 工具、通知行星 |
| `Source/StellarEraVehicle/Public/PlanetCharacter.h` | 暴露 `GetPlanetEditComponent()`，持有组件 |
| `Source/StellarEraVehicle/Private/PlanetCharacter.cpp` | 构造组件；换星时搬移网格可见性 |
| `Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h` | 网格线生成与背面剔除的纯函数声明 |
| `Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp` | 经纬线生成、点积剔除 |
| `Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h` | 网格组件：可见性、防抖重建、逐帧绘制 |
| `Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp` | 体素密度采样、缓存、`ULineBatchComponent` 绘制 |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` | `SetEditGridVisible` / `NotifyVoxelEdited` / `SaveEditsToSaveObject` / `EndPlay` |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 网格组件创建、编辑通知、保存防抖与写盘 |
| `Source/StellarEra/Public/PlanetEditUI.h` | 按钮矩形与命中的纯函数声明 |
| `Source/StellarEra/Private/PlanetEditUI.cpp` | 底部居中布局、含边界命中 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` | G 键动作、右键按下位置、新回调声明 |
| `Source/StellarEra/Private/StellarEraPlayerController.cpp` | 输入分派：按钮优先、左右键、滚轮、强制退出 |
| `Source/StellarEra/Public/StellarEraHUD.h` | 编辑面板绘制入口 |
| `Source/StellarEra/Private/StellarEraHUD.cpp` | 两个图标按钮、提示文字 |
| `Source/StellarEra/Private/PlanetEditTests.cpp` | 全部纯函数与本计划可无头验证的断言 |

不新增 `.uasset` 代码资产；唯一的资产是每颗行星一个 `UVoxelWorldSaveObject`（任务 6）。

---

### Task 1: 笔刷纯函数

**Files:**
- Create: `Source/StellarEraVehicle/Public/PlanetEditBrush.h`
- Create: `Source/StellarEraVehicle/Private/PlanetEditBrush.cpp`
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces:
  ```cpp
  enum class EPlanetBrushShape : uint8 { Sphere, Cube };
  struct FPlanetEditBox { FIntVector Min; FIntVector Max; };
  namespace PlanetEditBrush
  {
      constexpr int32 MinSizeVoxels = 1;
      constexpr int32 MaxSizeVoxels = 8;
      int32 ClampSize(int32 SizeVoxels);
      float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);
      FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
                                 int32 SizeVoxels, float VoxelSizeCm, bool bPlace);
      FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);
  }
  ```

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEra/Private/PlanetEditTests.cpp`：

```cpp
// 行星编辑模式的纯函数测试。
//
// 这里只放不需要世界、不需要 PIE 的断言：笔刷几何、按钮布局、网格几何。
// 需要真实体素数据的部分（密度采样、保存）只能 PIE 手验，见计划任务 8。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "PlanetEditBrush.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushSphereCenterTest,
	"StellarEra.Planet.EditBrush.SphereCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushSphereCenterTest::RunTest(const FString& Parameters)
{
	const FVector Hit(100.f, 200.f, 300.f);
	const FVector Normal(0.f, 0.f, 1.f);

	TestTrue(TEXT("挖：球心就是命中点"),
		PlanetEditBrush::GetEditCenterWorld(Hit, Normal, 2, 100.f, false).Equals(Hit));

	const FVector Expected = Hit + FVector(0.f, 0.f, 100.f);
	TestTrue(TEXT("放：球心沿法线外移一个半径"),
		PlanetEditBrush::GetEditCenterWorld(Hit, Normal, 2, 100.f, true).Equals(Expected));

	TestEqual(TEXT("尺寸 2、体素 100cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(2, 100.f), 100.f);
	TestEqual(TEXT("尺寸 1、体素 100cm 的半边长"),
		PlanetEditBrush::GetHalfExtentCm(1, 100.f), 50.f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushBoxBoundsTest,
	"StellarEra.Planet.EditBrush.BoxBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushBoxBoundsTest::RunTest(const FString& Parameters)
{
	const FPlanetEditBox One = PlanetEditBrush::MakeBox(FVector(10.4f, 10.4f, 10.4f), 1);
	TestEqual(TEXT("尺寸 1 Min.X"), One.Min.X, 10);
	TestEqual(TEXT("尺寸 1 Max.X"), One.Max.X, 10);

	const FPlanetEditBox Two = PlanetEditBrush::MakeBox(FVector(10.4f, -3.6f, 0.4f), 2);
	TestEqual(TEXT("尺寸 2 Min.X"), Two.Min.X, 10);
	TestEqual(TEXT("尺寸 2 Max.X"), Two.Max.X, 11);
	TestEqual(TEXT("尺寸 2 Min.Y"), Two.Min.Y, -4);
	TestEqual(TEXT("尺寸 2 Max.Y"), Two.Max.Y, -3);
	TestEqual(TEXT("尺寸 2 X 方向宽度（含端点）"), Two.Max.X - Two.Min.X + 1, 2);

	const FPlanetEditBox Eight = PlanetEditBrush::MakeBox(FVector(100.f, 100.f, 100.f), 8);
	TestEqual(TEXT("尺寸 8 Min.X"), Eight.Min.X, 97);
	TestEqual(TEXT("尺寸 8 Max.X"), Eight.Max.X, 104);
	TestEqual(TEXT("尺寸 8 X 方向宽度（含端点）"), Eight.Max.X - Eight.Min.X + 1, 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushClampSizeTest,
	"StellarEra.Planet.EditBrush.ClampSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushClampSizeTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("-3 夹到 1"), PlanetEditBrush::ClampSize(-3), 1);
	TestEqual(TEXT("0 夹到 1"), PlanetEditBrush::ClampSize(0), 1);
	TestEqual(TEXT("1 不变"), PlanetEditBrush::ClampSize(1), 1);
	TestEqual(TEXT("8 不变"), PlanetEditBrush::ClampSize(8), 8);
	TestEqual(TEXT("9 夹到 8"), PlanetEditBrush::ClampSize(9), 8);
	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认测试编译失败**

先确认编辑器已关闭，然后运行：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: FAIL，报 `PlanetEditBrush.h: No such file or directory`。

- [ ] **Step 3: 写最小实现**

创建 `Source/StellarEraVehicle/Public/PlanetEditBrush.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星编辑笔刷的纯几何：不碰世界，全部可无头测试。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.generated.h"

/** 笔刷形状。只有这两种。 */
UENUM(BlueprintType)
enum class EPlanetBrushShape : uint8
{
	Sphere UMETA(DisplayName = "球体"),
	Cube UMETA(DisplayName = "正方体")
};

/**
 * 轴对齐的体素方块范围。
 *
 * 用裸 FIntVector 而不是插件的 FVoxelIntBox：本头在 StellarEraVehicle 的公开
 * 目录里，而 Voxel 是该模块的私有依赖。转换成 FVoxelIntBox 的活放在 .cpp。
 *
 * **Min 与 Max 都含在范围内**（与 FVoxelIntBox 一致）。
 */
struct FPlanetEditBox
{
	FIntVector Min = FIntVector::ZeroValue;
	FIntVector Max = FIntVector::ZeroValue;
};

namespace PlanetEditBrush
{
	/** 笔刷尺寸下限（体素数，直径 / 边长）。 */
	constexpr int32 MinSizeVoxels = 1;

	/** 笔刷尺寸上限（体素数）。 */
	constexpr int32 MaxSizeVoxels = 8;

	/** 夹到 [MinSizeVoxels, MaxSizeVoxels]。 */
	int32 ClampSize(int32 SizeVoxels);

	/** 球半径 / 方块半边长，单位厘米。两者同式：Size * VoxelSize / 2。 */
	float GetHalfExtentCm(int32 SizeVoxels, float VoxelSizeCm);

	/**
	 * 编辑中心。
	 *
	 * 挖：就是命中点，笔刷半埋在表面里。
	 * 放：沿命中法线外移一个半边长，让球 / 方块与地表相切而不是半埋。
	 */
	FVector GetEditCenterWorld(const FVector& HitLocation, const FVector& HitNormal,
		int32 SizeVoxels, float VoxelSizeCm, bool bPlace);

	/**
	 * 把「行星体素空间里的浮点中心」换成含端点的整数盒。
	 *
	 * 尺寸 2 必须得到 2 格宽：Low = (Size-1)/2 = 0，High = Size-1-Low = 1。
	 * 用 ±Size/2 会得到 3 格——FVoxelIntBox 两个端点都含。
	 */
	FPlanetEditBox MakeBox(const FVector& LocalCenterVoxels, int32 SizeVoxels);
}
```

创建 `Source/StellarEraVehicle/Private/PlanetEditBrush.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditBrush.h"

int32 PlanetEditBrush::ClampSize(const int32 SizeVoxels)
{
	return FMath::Clamp(SizeVoxels, MinSizeVoxels, MaxSizeVoxels);
}

float PlanetEditBrush::GetHalfExtentCm(const int32 SizeVoxels, const float VoxelSizeCm)
{
	return ClampSize(SizeVoxels) * VoxelSizeCm * 0.5f;
}

FVector PlanetEditBrush::GetEditCenterWorld(
	const FVector& HitLocation,
	const FVector& HitNormal,
	const int32 SizeVoxels,
	const float VoxelSizeCm,
	const bool bPlace)
{
	if (!bPlace)
	{
		return HitLocation;
	}

	// 法线为零（退化命中）时不偏移，总比把方块放到 inf/nan 强。
	const FVector Normal = HitNormal.GetSafeNormal();
	return HitLocation + Normal * GetHalfExtentCm(SizeVoxels, VoxelSizeCm);
}

FPlanetEditBox PlanetEditBrush::MakeBox(const FVector& LocalCenterVoxels, const int32 SizeVoxels)
{
	const int32 Size = ClampSize(SizeVoxels);
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

- [ ] **Step 4: 给新文件加 UTF-8 BOM**

在仓库根目录运行（幂等，已有 BOM 会跳过）：

```bash
for f in Source/StellarEraVehicle/Public/PlanetEditBrush.h \
         Source/StellarEraVehicle/Private/PlanetEditBrush.cpp \
         Source/StellarEra/Private/PlanetEditTests.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
```

Expected: 无输出；三个文件开头都是 `efbbbf`。

- [ ] **Step 5: 构建并运行本任务的测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditBrush" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditBrushTests.log"
```

Expected: 构建成功；日志里三条测试 `Result={Success}`，没有 `Result={Fail}`。

- [ ] **Step 6: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEraVehicle/Public/PlanetEditBrush.h \
        Source/StellarEraVehicle/Private/PlanetEditBrush.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 笔刷几何纯函数与测试" -- \
  Source/StellarEraVehicle/Public/PlanetEditBrush.h \
  Source/StellarEraVehicle/Private/PlanetEditBrush.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 2: HUD 按钮布局与命中

**Files:**
- Create: `Source/StellarEra/Public/PlanetEditUI.h`
- Create: `Source/StellarEra/Private/PlanetEditUI.cpp`
- Modify: `Source/StellarEra/Private/PlanetEditTests.cpp`（追加测试）

**Interfaces:**
- Consumes: 无。
- Produces:
  ```cpp
  namespace PlanetEditUI
  {
      constexpr float ButtonSize = 48.f;
      constexpr float ButtonGap = 8.f;
      constexpr float ButtonBottomMargin = 40.f;
      constexpr float HintGap = 8.f;
      void GetBrushButtonRects(const FVector2D& ViewportSize,
                               FVector2D& OutSphereTopLeft, FVector2D& OutCubeTopLeft);
      bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
                              int32& OutButtonIndex);
  }
  ```
  下标约定：**0 = 球体，1 = 正方体**。`PlanetEditUI.h` 是 `StellarEra` 的公开头，不能引用 Vehicle 的 `EPlanetBrushShape`（那是私有依赖），所以这里只返回 `int32`。

- [ ] **Step 1: 追加失败测试**

在 `PlanetEditTests.cpp` 的 `#include "PlanetEditBrush.h"` 下一行加：

```cpp
#include "PlanetEditUI.h"
```

在 `#if WITH_DEV_AUTOMATION_TESTS` 内、最后一个 `#endif` 之前追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushButtonsTest,
	"StellarEra.UI.EditBrushButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushButtonsTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1920.f, 1080.f);
	FVector2D Sphere = FVector2D::ZeroVector;
	FVector2D Cube = FVector2D::ZeroVector;
	PlanetEditUI::GetBrushButtonRects(Viewport, Sphere, Cube);

	// FVector2D 的分量在 UE5 里是 double，显式转 float 再比，
	// 免得 TestEqual 的 float / double 重载挑不出来。
	TestEqual(TEXT("球体按钮 X"), static_cast<float>(Sphere.X), 908.f, 0.01f);
	TestEqual(TEXT("按钮顶边 Y"), static_cast<float>(Sphere.Y), 992.f, 0.01f);
	TestEqual(TEXT("两个按钮同高"),
		static_cast<float>(Sphere.Y), static_cast<float>(Cube.Y), 0.01f);
	TestEqual(TEXT("方块按钮紧跟球体按钮"),
		static_cast<float>(Cube.X),
		static_cast<float>(Sphere.X) + PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap,
		0.01f);
	TestTrue(TEXT("两个按钮不重叠"),
		Cube.X >= Sphere.X + PlanetEditUI::ButtonSize);

	int32 Index = INDEX_NONE;
	TestTrue(TEXT("球体中心命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + 24.f, Sphere.Y + 24.f), Index));
	TestEqual(TEXT("球体中心下标 = 0"), Index, 0);

	Index = INDEX_NONE;
	TestTrue(TEXT("正方体中心命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Cube.X + 24.f, Cube.Y + 24.f), Index));
	TestEqual(TEXT("正方体中心下标 = 1"), Index, 1);

	Index = INDEX_NONE;
	TestFalse(TEXT("两个按钮之间的空隙不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap * 0.5f,
			Sphere.Y + 24.f), Index));

	Index = INDEX_NONE;
	TestFalse(TEXT("按钮上方不命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + 24.f, Sphere.Y - 1.f), Index));

	Index = INDEX_NONE;
	TestTrue(TEXT("边界含：球体按钮右边缘命中"), PlanetEditUI::HitTestBrushButton(Viewport,
		FVector2D(Sphere.X + PlanetEditUI::ButtonSize, Sphere.Y + 24.f), Index));
	TestEqual(TEXT("右边缘仍算球体"), Index, 0);

	FVector2D NarrowSphere = FVector2D::ZeroVector;
	FVector2D NarrowCube = FVector2D::ZeroVector;
	PlanetEditUI::GetBrushButtonRects(FVector2D(100.f, 100.f), NarrowSphere, NarrowCube);
	TestEqual(TEXT("视口过窄时球体按钮贴左边"),
		static_cast<float>(NarrowSphere.X), 0.f, 0.01f);
	TestEqual(TEXT("视口过窄时方块按钮不越界"),
		static_cast<float>(NarrowCube.X),
		PlanetEditUI::ButtonSize + PlanetEditUI::ButtonGap, 0.01f);

	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

同 Task 1 Step 2 的构建命令。Expected: FAIL，报 `PlanetEditUI.h: No such file or directory`。

- [ ] **Step 3: 写实现**

创建 `Source/StellarEra/Public/PlanetEditUI.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 编辑模式两个笔刷按钮的布局与命中。纯函数，不碰 Canvas。

#pragma once

#include "CoreMinimal.h"

namespace PlanetEditUI
{
	/** 按钮边长（像素）。 */
	constexpr float ButtonSize = 48.f;

	/** 两个按钮之间的间距（像素）。 */
	constexpr float ButtonGap = 8.f;

	/** 按钮底边距屏幕底部的距离（像素）。 */
	constexpr float ButtonBottomMargin = 40.f;

	/** 按钮底边到提示文字的间距（像素）。 */
	constexpr float HintGap = 8.f;

	/**
	 * 两个按钮的左上角（像素）。顺序：先球体、后正方体。
	 * 视口窄到放不下时贴左边，宁可挤也不画到屏幕外。
	 */
	void GetBrushButtonRects(const FVector2D& ViewportSize,
		FVector2D& OutSphereTopLeft, FVector2D& OutCubeTopLeft);

	/**
	 * 命中返回 true 并写下标：0 = 球体，1 = 正方体。
	 * **边界含**：压线算命中；两个按钮之间的空隙不算。
	 *
	 * 返回 int32 而不是 EPlanetBrushShape：本头是 StellarEra 的公开头，
	 * 而该枚举属于私有依赖 StellarEraVehicle。调用方在 .cpp 里映射。
	 */
	bool HitTestBrushButton(const FVector2D& ViewportSize, const FVector2D& ScreenPosition,
		int32& OutButtonIndex);
}
```

创建 `Source/StellarEra/Private/PlanetEditUI.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditUI.h"

namespace
{
	// 参数用 double：FVector2D 的分量在 UE5 里是 double，直接传给 float 参数
	// 会触发 C4244，而本项目警告即错误。
	double GetLeft(const double ViewportWidth)
	{
		const double TotalWidth = PlanetEditUI::ButtonSize * 2.0 + PlanetEditUI::ButtonGap;
		return FMath::Max(0.0, (ViewportWidth - TotalWidth) * 0.5);
	}

	double GetTop(const double ViewportHeight)
	{
		return FMath::Max(0.0,
			ViewportHeight - PlanetEditUI::ButtonBottomMargin - PlanetEditUI::ButtonSize);
	}
}

void PlanetEditUI::GetBrushButtonRects(
	const FVector2D& ViewportSize,
	FVector2D& OutSphereTopLeft,
	FVector2D& OutCubeTopLeft)
{
	const double Left = GetLeft(ViewportSize.X);
	const double Top = GetTop(ViewportSize.Y);

	OutSphereTopLeft = FVector2D(Left, Top);
	OutCubeTopLeft = FVector2D(Left + ButtonSize + ButtonGap, Top);
}

bool PlanetEditUI::HitTestBrushButton(
	const FVector2D& ViewportSize,
	const FVector2D& ScreenPosition,
	int32& OutButtonIndex)
{
	const double Left = GetLeft(ViewportSize.X);
	const double Top = GetTop(ViewportSize.Y);

	// 先看是否在这一行按钮的纵向范围内。上下边界都含。
	if (ScreenPosition.Y < Top || ScreenPosition.Y > Top + ButtonSize)
	{
		return false;
	}

	if (ScreenPosition.X >= Left && ScreenPosition.X <= Left + ButtonSize)
	{
		OutButtonIndex = 0;
		return true;
	}

	const double CubeLeft = Left + ButtonSize + ButtonGap;
	if (ScreenPosition.X >= CubeLeft && ScreenPosition.X <= CubeLeft + ButtonSize)
	{
		OutButtonIndex = 1;
		return true;
	}

	return false;
}
```

- [ ] **Step 4: 加 BOM（新文件）**

```bash
for f in Source/StellarEra/Public/PlanetEditUI.h Source/StellarEra/Private/PlanetEditUI.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
```

- [ ] **Step 5: 构建并运行测试**

构建命令同 Task 1 Step 5，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.UI.EditBrushButtons" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditUITests.log"
```

Expected: `Result={Success}`。

- [ ] **Step 6: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEra/Public/PlanetEditUI.h \
        Source/StellarEra/Private/PlanetEditUI.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): HUD 笔刷按钮布局与命中" -- \
  Source/StellarEra/Public/PlanetEditUI.h \
  Source/StellarEra/Private/PlanetEditUI.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 3: 表面网格几何（经纬线 + 背面剔除）

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h`
- Create: `Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp`
- Modify: `Source/StellarEra/Private/PlanetEditTests.cpp`（追加测试）

**Interfaces:**
- Consumes: 无。
- Produces:
  ```cpp
  struct FPlanetEditGridLine { TArray<FVector> Directions; bool bClosed = false; };
  namespace PlanetEditGrid
  {
      void BuildLines(float StepDegrees, TArray<FPlanetEditGridLine>& OutLines);
      bool IsDirectionVisible(const FVector& UnitDirection, const FVector& CameraDirection);
      bool IsSegmentVisible(const FVector& UnitDirectionA, const FVector& UnitDirectionB,
                            const FVector& CameraDirection);
  }
  ```
  `BuildLines` 的输出顺序固定：先全部经线（每条不闭合），再全部纬线（每条闭合）。任务 4 依赖这个顺序只为把线段串起来，不依赖具体条数。

- [ ] **Step 1: 追加失败测试**

在 `PlanetEditTests.cpp` 的 `#include "PlanetEditUI.h"` 下一行加：

```cpp
#include "PlanetEditGridGeometry.h"
```

追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditGridGeometryTest,
	"StellarEra.Planet.EditGrid.Geometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditGridGeometryTest::RunTest(const FString& Parameters)
{
	TArray<FPlanetEditGridLine> Lines;
	PlanetEditGrid::BuildLines(5.f, Lines);

	// 5°：72 条经线 + 35 条纬线（两极不生成退化纬线）。
	TestEqual(TEXT("5° 网格线总数"), Lines.Num(), 107);

	int32 ClosedCount = 0;
	int32 MeridianPoints = 0;
	int32 ParallelPoints = 0;
	for (const FPlanetEditGridLine& Line : Lines)
	{
		if (Line.bClosed)
		{
			++ClosedCount;
			ParallelPoints += Line.Directions.Num();
		}
		else
		{
			MeridianPoints += Line.Directions.Num();
		}
	}
	TestEqual(TEXT("闭合纬线数"), ClosedCount, 35);
	TestEqual(TEXT("经线总采样点（72 × 37）"), MeridianPoints, 2664);
	TestEqual(TEXT("纬线总采样点（35 × 72）"), ParallelPoints, 2520);

	// 每个方向都是单位向量：采样半径按方向缩放，方向长度必须为 1。
	for (const FVector& Dir : Lines[0].Directions)
	{
		TestTrue(TEXT("方向是单位向量"), FMath::IsNearlyEqual(Dir.Size(), 1.f, 1.e-3f));
	}

	TArray<FPlanetEditGridLine> Coarse;
	PlanetEditGrid::BuildLines(90.f, Coarse);
	TestEqual(TEXT("90° 网格线总数（4 经线 + 1 纬线）"), Coarse.Num(), 5);
	TestEqual(TEXT("90° 经线各 3 点"), Coarse[0].Directions.Num(), 3);
	TestTrue(TEXT("90° 纬线闭合"), Coarse[4].bClosed);

	TestTrue(TEXT("同向可见"),
		PlanetEditGrid::IsDirectionVisible(FVector(1.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f)));
	TestFalse(TEXT("背向不可见"),
		PlanetEditGrid::IsDirectionVisible(FVector(-1.f, 0.f, 0.f), FVector(1.f, 0.f, 0.f)));
	TestFalse(TEXT("两端都在背面时整段剔除"),
		PlanetEditGrid::IsSegmentVisible(FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
			FVector(0.f, 1.f, 0.f)));
	TestTrue(TEXT("一端在正面就保留整段"),
		PlanetEditGrid::IsSegmentVisible(FVector(1.f, 0.f, 0.f), FVector(-1.f, 0.f, 0.f),
			FVector(1.f, 0.f, 0.f)));

	TArray<FPlanetEditGridLine> Clamped;
	PlanetEditGrid::BuildLines(0.f, Clamped);
	TestEqual(TEXT("步长 0 被夹到 1°：360 经线 + 179 纬线"), Clamped.Num(), 539);

	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Expected: FAIL，报 `PlanetEditGridGeometry.h: No such file or directory`。

- [ ] **Step 3: 写实现**

创建 `Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 表面网格的几何生成与背面剔除：纯函数，不碰体素数据。

#pragma once

#include "CoreMinimal.h"

/**
 * 一条经纬网格线。
 *
 * Directions 是单位球方向（行星体素空间；世界与体素空间只差等比缩放，方向可直接共用）。
 * bClosed = true 表示首尾相接（纬线）；经线是开放的折线。
 */
struct FPlanetEditGridLine
{
	TArray<FVector> Directions;
	bool bClosed = false;
};

namespace PlanetEditGrid
{
	/**
	 * 按步长生成经纬线。步长夹到 [1, 90] 度。
	 *
	 * 经线：固定经度，纬度从 -90° 到 +90°（含两极），每条 LatCount+1 个点。
	 * 纬线：跳过两极（在极点会退化成重复点），每条 LonCount 个点并闭合。
	 * 数量：经线 LonCount 条、纬线 LatCount-1 条。
	 */
	void BuildLines(float StepDegrees, TArray<FPlanetEditGridLine>& OutLines);

	/** 顶点方向与「球心 → 相机」方向同向（点积 > 0）即可见。 */
	bool IsDirectionVisible(const FVector& UnitDirection, const FVector& CameraDirection);

	/** 线段只要有一端可见就整段提交；两端都在背面才剔除。 */
	bool IsSegmentVisible(const FVector& UnitDirectionA, const FVector& UnitDirectionB,
		const FVector& CameraDirection);
}
```

创建 `Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditGridGeometry.h"

namespace
{
	FVector MakeDirection(const float LatDeg, const float LonDeg)
	{
		const float LatRad = FMath::DegreesToRadians(LatDeg);
		const float LonRad = FMath::DegreesToRadians(LonDeg);
		const float CosLat = FMath::Cos(LatRad);
		return FVector(
			CosLat * FMath::Cos(LonRad),
			CosLat * FMath::Sin(LonRad),
			FMath::Sin(LatRad));
	}
}

void PlanetEditGrid::BuildLines(const float StepDegrees, TArray<FPlanetEditGridLine>& OutLines)
{
	OutLines.Reset();

	const float Step = FMath::Clamp(StepDegrees, 1.f, 90.f);

	// 用 RoundToInt 而不是 floor：360/90 = 4 必须得到 4 条经线。
	// 实际步长重新均分，保证正好绕一圈。
	const int32 LonCount = FMath::Max(1, FMath::RoundToInt(360.f / Step));
	const int32 LatCount = FMath::Max(1, FMath::RoundToInt(180.f / Step));
	const float LonStep = 360.f / LonCount;
	const float LatStep = 180.f / LatCount;

	// ── 经线 ──
	for (int32 LonIndex = 0; LonIndex < LonCount; ++LonIndex)
	{
		const float LonDeg = -180.f + LonIndex * LonStep;

		FPlanetEditGridLine Line;
		Line.bClosed = false;
		Line.Directions.Reserve(LatCount + 1);
		for (int32 LatIndex = 0; LatIndex <= LatCount; ++LatIndex)
		{
			Line.Directions.Add(MakeDirection(-90.f + LatIndex * LatStep, LonDeg));
		}
		OutLines.Add(MoveTemp(Line));
	}

	// ── 纬线 ──
	// 跳过两极：极点处的纬线所有点重合，画出来是一条零长度线段。
	for (int32 LatIndex = 1; LatIndex < LatCount; ++LatIndex)
	{
		const float LatDeg = -90.f + LatIndex * LatStep;

		FPlanetEditGridLine Line;
		Line.bClosed = true;
		Line.Directions.Reserve(LonCount);
		for (int32 LonIndex = 0; LonIndex < LonCount; ++LonIndex)
		{
			Line.Directions.Add(MakeDirection(LatDeg, -180.f + LonIndex * LonStep));
		}
		OutLines.Add(MoveTemp(Line));
	}
}

bool PlanetEditGrid::IsDirectionVisible(
	const FVector& UnitDirection,
	const FVector& CameraDirection)
{
	// 严格大于 0：正好切于屏幕边缘的方向不画，避免接缝处一条线闪。
	return FVector::DotProduct(UnitDirection, CameraDirection) > 0.0;
}

bool PlanetEditGrid::IsSegmentVisible(
	const FVector& UnitDirectionA,
	const FVector& UnitDirectionB,
	const FVector& CameraDirection)
{
	return IsDirectionVisible(UnitDirectionA, CameraDirection) ||
		IsDirectionVisible(UnitDirectionB, CameraDirection);
}
```

- [ ] **Step 4: 加 BOM**

```bash
for f in Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h \
         Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
```

- [ ] **Step 5: 构建并运行测试**

构建命令同 Task 1 Step 5，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditGrid.Geometry" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditGridGeometryTests.log"
```

Expected: `Result={Success}`。

- [ ] **Step 6: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h \
        Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 表面网格经纬线几何" -- \
  Source/StellarEraPlanet/Public/PlanetEditGridGeometry.h \
  Source/StellarEraPlanet/Private/PlanetEditGridGeometry.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 4: 表面网格组件与行星挂载

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h`
- Create: `Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`
- Modify: `Source/StellarEra/Private/PlanetEditTests.cpp`（追加组件默认值测试）

**Interfaces:**
- Consumes: `PlanetEditGrid::BuildLines` / `IsSegmentVisible`（Task 3）。
- Produces:
  ```cpp
  class UPlanetSurfaceGridComponent : public UActorComponent
  {
  public:
      void SetGridVisible(bool bVisible);
      bool IsGridVisible() const;
      void NotifyVoxelEdited();
      virtual void TickComponent(...) override;
      UPROPERTY(EditAnywhere) float GridStepDegrees = 5.f;
      UPROPERTY(EditAnywhere) float SurfaceOffsetCm = 3.f;
      UPROPERTY(EditAnywhere) int32 MaxSearchSteps = 32;
      UPROPERTY(EditAnywhere) float RebuildDebounceSeconds = 0.3f;
      UPROPERTY(EditAnywhere) FLinearColor GridColor;
      UPROPERTY(EditAnywhere) float LineThickness = 1.5f;
  };
  // AVoxelPlanetActor 新增
  void SetEditGridVisible(bool bVisible);
  void NotifyVoxelEdited();
  ```
- 采样约定：`FVoxelData::GetValue(FIntVector, LOD=0)` 的坐标是**体素空间**；`IsEmpty()` 为真表示球外（正密度），为假表示球内（负密度）。初值取 `GetSurfaceRadiusCmAtDirection(Dir) / VoxelSize`，沿半径向内外各扫 `MaxSearchSteps` 步（每步 2 体素），首次符号翻转后二分 4 次。

- [ ] **Step 1: 追加失败测试**

在 `PlanetEditTests.cpp` 的 `#include "PlanetEditGridGeometry.h"` 下一行加：

```cpp
#include "VoxelPlanetActor.h"
#include "PlanetSurfaceGridComponent.h"
```

追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditGridDefaultsTest,
	"StellarEra.Planet.EditGrid.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditGridDefaultsTest::RunTest(const FString& Parameters)
{
	const AVoxelPlanetActor* PlanetCDO = GetDefault<AVoxelPlanetActor>();
	if (!TestNotNull(TEXT("AVoxelPlanetActor CDO"), PlanetCDO))
	{
		return false;
	}

	const UPlanetSurfaceGridComponent* Grid = Cast<UPlanetSurfaceGridComponent>(
		PlanetCDO->GetDefaultSubobjectByName(TEXT("PlanetEditGrid")));
	if (!TestNotNull(TEXT("行星 CDO 上的 PlanetEditGrid 组件"), Grid))
	{
		return false;
	}

	TestEqual(TEXT("默认网格步长"), Grid->GridStepDegrees, 5.f, 0.01f);
	TestEqual(TEXT("默认表面偏移"), Grid->SurfaceOffsetCm, 3.f, 0.01f);
	TestEqual(TEXT("默认搜索步数"), Grid->MaxSearchSteps, 32);
	TestEqual(TEXT("默认重建防抖"), Grid->RebuildDebounceSeconds, 0.3f, 0.01f);
	TestFalse(TEXT("默认不可见"), Grid->IsGridVisible());
	TestFalse(TEXT("不可见时不 Tick"), Grid->PrimaryComponentTick.bStartWithTickEnabled);

	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Expected: FAIL，报 `PlanetSurfaceGridComponent.h: No such file or directory`。

- [ ] **Step 3: 写网格组件**

创建 `Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 编辑模式下贴在行星表面的经纬网格。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "PlanetSurfaceGridComponent.generated.h"

class AVoxelPlanetActor;
class ULineBatchComponent;

/**
 * 采样式表面网格。
 *
 * 不能用解析地形半径：GetSurfaceRadiusCmAtDirection 读不到体素编辑，
 * 挖出的洞和加上的凸起在它眼里不存在。这里以解析半径为初值，沿半径方向
 * 用 FVoxelData::GetValue 找符号翻转，因此网格跟着真实体素表面走。
 *
 * 采样与缓存都在**体素空间**（VoxelSize 无关）；绘制时每帧乘 VoxelSize、
 * 再乘行星变换。行星在 N 体里每帧移动，所以缓存不能是世界坐标。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetSurfaceGridComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetSurfaceGridComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 显示 / 隐藏。显示时立即重建一次缓存。 */
	void SetGridVisible(bool bVisible);
	bool IsGridVisible() const { return bGridVisible; }

	/** 体素被编辑后调用：按 RebuildDebounceSeconds 防抖重建缓存。 */
	void NotifyVoxelEdited();

	/** 经纬线步长（度）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid",
		meta = (ClampMin = "1.0", ClampMax = "90.0"))
	float GridStepDegrees = 5.f;

	/** 沿半径方向的额外外移（厘米），避免线被地表网格 z-fighting 吃掉。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid")
	float SurfaceOffsetCm = 3.f;

	/** 从解析半径向外 / 向内最多扫多少步（每步 2 体素）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "1"))
	int32 MaxSearchSteps = 32;

	/** 编辑后重建网格的防抖时间（秒）。 */
	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "0.0"))
	float RebuildDebounceSeconds = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid")
	FLinearColor GridColor = FLinearColor(1.f, 0.85f, 0.30f, 0.35f);

	UPROPERTY(EditAnywhere, Category = "Planet|EditGrid", meta = (ClampMin = "0.1"))
	float LineThickness = 1.5f;

private:
	AVoxelPlanetActor* GetPlanet() const;
	void EnsureLineBatch();
	void RebuildCache();
	void DrawCachedLines();

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> LineBatch = nullptr;

	FTimerHandle RebuildTimer;

	/** 每个顶点的单位方向（球面方向，体素空间）。 */
	TArray<FVector> CachedDirections;

	/** 每个顶点在体素空间中的位置（体素单位）。与 CachedDirections 等长。 */
	TArray<FVector> CachedVoxelPositions;

	/** 线段两端在 CachedDirections 里的下标。 */
	TArray<FIntPoint> CachedSegments;

	bool bGridVisible = false;
};
```

创建 `Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetSurfaceGridComponent.h"

#include "PlanetEditGridGeometry.h"
#include "VoxelPlanetActor.h"
#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SceneTypes.h"
#include "TimerManager.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelVector.h"

UPlanetSurfaceGridComponent::UPlanetSurfaceGridComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 必须排在 N 体子系统之后：子系统在 FTickableGameObject::TickObjects 里把
	// 行星搬到本帧位置，而相机更新在它之后。排进 TG_PostUpdateWork 的组件
	// 能读到本帧最终位置，绘制出的网格不会比行星慢一帧。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

AVoxelPlanetActor* UPlanetSurfaceGridComponent::GetPlanet() const
{
	return Cast<AVoxelPlanetActor>(GetOwner());
}

void UPlanetSurfaceGridComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RebuildTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UPlanetSurfaceGridComponent::SetGridVisible(const bool bVisible)
{
	if (bGridVisible == bVisible)
	{
		return;
	}

	bGridVisible = bVisible;
	SetComponentTickEnabled(bVisible);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RebuildTimer);
	}

	if (bVisible)
	{
		EnsureLineBatch();
		RebuildCache();
		if (LineBatch)
		{
			LineBatch->SetVisibility(true);
		}
	}
	else if (LineBatch)
	{
		// Flush 清掉上一帧提交的线；隐藏后再画也不会有残留。
		LineBatch->Flush();
		LineBatch->SetVisibility(false);
	}
}

void UPlanetSurfaceGridComponent::NotifyVoxelEdited()
{
	if (!bGridVisible)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FTimerManager& Timers = World->GetTimerManager();
	Timers.ClearTimer(RebuildTimer);

	if (RebuildDebounceSeconds <= 0.f)
	{
		RebuildCache();
		return;
	}

	Timers.SetTimer(RebuildTimer,
		FTimerDelegate::CreateUObject(this, &UPlanetSurfaceGridComponent::RebuildCache),
		RebuildDebounceSeconds, false);
}

void UPlanetSurfaceGridComponent::EnsureLineBatch()
{
	if (LineBatch)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	LineBatch = NewObject<ULineBatchComponent>(Owner, TEXT("PlanetSurfaceGridLines"));
	LineBatch->SetupAttachment(Owner->GetRootComponent());
	LineBatch->RegisterComponent();
}

void UPlanetSurfaceGridComponent::RebuildCache()
{
	CachedDirections.Reset();
	CachedVoxelPositions.Reset();
	CachedSegments.Reset();

	AVoxelPlanetActor* Planet = GetPlanet();
	if (!Planet || !Planet->IsCreated())
	{
		return;
	}

	FVoxelData& Data = Planet->GetData();
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const float BaseRadiusVoxels = Planet->GetSurfaceRadiusCm() / VoxelSize;
	const int32 SearchSteps = FMath::Max(1, MaxSearchSteps);

	TArray<FPlanetEditGridLine> Lines;
	PlanetEditGrid::BuildLines(GridStepDegrees, Lines);

	// 锁必须覆盖所有采样点：向外 / 向内各 SearchSteps 步、每步 2 体素。
	const int32 BoundsExtent =
		FMath::CeilToInt(BaseRadiusVoxels + SearchSteps * 2.f + 16.f);
	const FVoxelIntBox Bounds(FIntVector(-BoundsExtent), FIntVector(BoundsExtent));
	FVoxelReadScopeLock Lock(Data, Bounds, TEXT("PlanetSurfaceGrid"));

	const auto SampleEmpty = [&Data](const FVector& Direction, const float RadiusVoxels) -> bool
	{
		// Direction 的分量是 double，RoundToInt(double) 返回 int64；显式窄化。
		const FIntVector P(
			static_cast<int32>(FMath::RoundToInt(Direction.X * RadiusVoxels)),
			static_cast<int32>(FMath::RoundToInt(Direction.Y * RadiusVoxels)),
			static_cast<int32>(FMath::RoundToInt(Direction.Z * RadiusVoxels)));
		return Data.GetValue(P, 0).IsEmpty();
	};

	const auto FindRadiusVoxels = [&](const FVector& Direction) -> float
	{
		const float Guess = Planet->GetSurfaceRadiusCmAtDirection(Direction) / VoxelSize;
		const bool bGuessEmpty = SampleEmpty(Direction, Guess);

		// 向外、向内各扫一遍，取先遇到的符号翻转。
		for (const float Sign : {1.f, -1.f})
		{
			float PrevR = Guess;
			bool bPrevEmpty = bGuessEmpty;

			for (int32 Step = 1; Step <= SearchSteps; ++Step)
			{
				const float R = Guess + Sign * Step * 2.f;
				if (R < 2.f)
				{
					break;
				}

				const bool bEmpty = SampleEmpty(Direction, R);
				if (bEmpty != bPrevEmpty)
				{
					float EmptyR = bPrevEmpty ? PrevR : R;
					float SolidR = bPrevEmpty ? R : PrevR;

					// 4 次二分 → 体素级精度；再多也不会让 5° 网格更平滑。
					for (int32 Iter = 0; Iter < 4; ++Iter)
					{
						const float Mid = (EmptyR + SolidR) * 0.5f;
						if (SampleEmpty(Direction, Mid))
						{
							EmptyR = Mid;
						}
						else
						{
							SolidR = Mid;
						}
					}

					return (EmptyR + SolidR) * 0.5f;
				}

				PrevR = R;
				bPrevEmpty = bEmpty;
			}
		}

		// 该方向没有编辑（或超出了搜索范围）：退回解析半径。
		return Guess;
	};

	const float OffsetVoxels = SurfaceOffsetCm / VoxelSize;

	for (const FPlanetEditGridLine& Line : Lines)
	{
		const int32 Num = Line.Directions.Num();
		if (Num < 2)
		{
			continue;
		}

		const int32 Base = CachedDirections.Num();
		CachedDirections.Reserve(Base + Num);
		CachedVoxelPositions.Reserve(Base + Num);

		for (const FVector& Direction : Line.Directions)
		{
			const float RadiusVoxels = FindRadiusVoxels(Direction) + OffsetVoxels;
			CachedDirections.Add(Direction);
			CachedVoxelPositions.Add(Direction * RadiusVoxels);
		}

		// 经线是开放折线（Num-1 段）；纬线首尾相接（Num 段）。
		const int32 SegmentCount = Line.bClosed ? Num : Num - 1;
		CachedSegments.Reserve(CachedSegments.Num() + SegmentCount);
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			CachedSegments.Add(FIntPoint(Base + Index, Base + (Index + 1) % Num));
		}
	}
}

void UPlanetSurfaceGridComponent::DrawCachedLines()
{
	AVoxelPlanetActor* Planet = GetPlanet();
	UWorld* World = GetWorld();
	if (!bGridVisible || !Planet || !LineBatch || !World || CachedSegments.Num() == 0)
	{
		return;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);

	const FVector CameraDirection =
		(ViewLocation - Planet->GetActorLocation()).GetSafeNormal();
	if (CameraDirection.IsNearlyZero())
	{
		return;
	}

	TArray<FBatchedLine> Lines;
	Lines.Reserve(CachedSegments.Num());

	for (const FIntPoint& Segment : CachedSegments)
	{
		const FVector& DirectionA = CachedDirections[Segment.X];
		const FVector& DirectionB = CachedDirections[Segment.Y];
		if (!PlanetEditGrid::IsSegmentVisible(DirectionA, DirectionB, CameraDirection))
		{
			continue;
		}

		// 缓存是体素空间；LocalToGlobalFloat 已经包含 VoxelSize 与 Actor 变换，
		// 所以这里直接传体素坐标。
		const FVector A = Planet->LocalToGlobalFloat(
			FVoxelVector(CachedVoxelPositions[Segment.X]));
		const FVector B = Planet->LocalToGlobalFloat(
			FVoxelVector(CachedVoxelPositions[Segment.Y]));

		// LifeTime < 0：永不自动过期；本组件每帧 Flush + 重画。
		Lines.Emplace(A, B, GridColor, -1.f, LineThickness, static_cast<uint8>(SDPG_World));
	}

	LineBatch->Flush();
	LineBatch->DrawLines(Lines);
}

void UPlanetSurfaceGridComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bGridVisible)
	{
		DrawCachedLines();
	}
}
```

- [ ] **Step 4: 挂到行星上**

在 `VoxelPlanetActor.h` 顶部前向声明区加：

```cpp
class UPlanetSurfaceGridComponent;
```

在 `public:` 的 `InvalidateAndRebuild()` 之后加：

```cpp
/** 显示 / 隐藏编辑模式下的表面网格。 */
void SetEditGridVisible(bool bVisible);

/** 体素被编辑后调用：通知网格重建（防抖），并触发保存。 */
void NotifyVoxelEdited();
```

在 `AtmosphereComponent` 的属性块之后加：

```cpp
/** 编辑模式下的表面网格基线。构造时自动创建。 */
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Edit")
TObjectPtr<UPlanetSurfaceGridComponent> EditGrid;
```

在 `VoxelPlanetActor.cpp` 的 include 区加：

```cpp
#include "PlanetSurfaceGridComponent.h"
```

在构造函数末尾（`AtmosphereComponent->SetupAttachment(RootComponent);` 之后）加：

```cpp
// 表面网格：默认隐藏、不 tick，编辑模式打开时才工作。
EditGrid = CreateDefaultSubobject<UPlanetSurfaceGridComponent>(TEXT("PlanetEditGrid"));
EditGrid->SetupAttachment(RootComponent);
```

在 `InvalidateAndRebuild()` 之后加：

```cpp
void AVoxelPlanetActor::SetEditGridVisible(const bool bVisible)
{
	if (EditGrid)
	{
		EditGrid->SetGridVisible(bVisible);
	}
}

void AVoxelPlanetActor::NotifyVoxelEdited()
{
	if (EditGrid)
	{
		EditGrid->NotifyVoxelEdited();
	}
}
```

- [ ] **Step 5: 加 BOM**

```bash
for f in Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h \
         Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
```

- [ ] **Step 6: 构建并运行组件测试**

构建命令同 Task 1 Step 5，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditGrid.Component" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditGridComponentTests.log"
```

Expected: `Result={Success}`。若构建报 `GetData` 需要非 const，检查 `FVoxelData& Data = Planet->GetData();` 是否误写成 `const`。

- [ ] **Step 7: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h \
        Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp \
        Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
        Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 贴合体素地形的表面网格组件" -- \
  Source/StellarEraPlanet/Public/PlanetSurfaceGridComponent.h \
  Source/StellarEraPlanet/Private/PlanetSurfaceGridComponent.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 5: 角色编辑组件（模式 / 射线 / 执行）

**Files:**
- Create: `Source/StellarEraVehicle/Public/PlanetEditComponent.h`
- Create: `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp`
- Modify: `Source/StellarEraVehicle/Public/PlanetCharacter.h`
- Modify: `Source/StellarEraVehicle/Private/PlanetCharacter.cpp`
- Modify: `Source/StellarEra/Private/PlanetEditTests.cpp`（追加默认值测试）

**Interfaces:**
- Consumes: `PlanetEditBrush`（Task 1）、`AVoxelPlanetActor::SetEditGridVisible` / `NotifyVoxelEdited`（Task 4）。
- Produces:
  ```cpp
  class UPlanetEditComponent : public UActorComponent
  {
  public:
      bool IsEditMode() const;
      EPlanetBrushShape GetBrushShape() const;
      int32 GetBrushSizeVoxels() const;
      void SetEditMode(bool bEnabled);
      void ToggleEditMode();
      void SetBrushShape(EPlanetBrushShape NewShape);
      void AdjustBrushSize(int32 Delta);
      bool DigAtScreenPosition(const FVector2D& ScreenPosition);
      bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);
  };
  // APlanetCharacter 新增
  UPlanetEditComponent* GetPlanetEditComponent() const;
  ```

- [ ] **Step 1: 追加失败测试**

在 `PlanetEditTests.cpp` 的 `#include "PlanetSurfaceGridComponent.h"` 下一行加：

```cpp
#include "PlanetEditComponent.h"
#include "PlanetCharacter.h"
```

追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditComponentDefaultsTest,
	"StellarEra.Planet.EditComponent.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditComponentDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlanetEditComponent* EditCDO = GetDefault<UPlanetEditComponent>();
	if (!TestNotNull(TEXT("UPlanetEditComponent CDO"), EditCDO))
	{
		return false;
	}

	TestFalse(TEXT("默认不在编辑模式"), EditCDO->IsEditMode());
	TestTrue(TEXT("默认笔刷是球体"),
		EditCDO->GetBrushShape() == EPlanetBrushShape::Sphere);
	TestEqual(TEXT("默认笔刷尺寸 2 体素"), EditCDO->GetBrushSizeVoxels(), 2);
	TestEqual(TEXT("默认最大编辑距离 1000 米"),
		EditCDO->MaxEditDistanceCm, 1.0e5f, 1.f);

	const APlanetCharacter* CharacterCDO = GetDefault<APlanetCharacter>();
	if (!TestNotNull(TEXT("APlanetCharacter CDO"), CharacterCDO))
	{
		return false;
	}

	const UPlanetEditComponent* CharacterEdit = Cast<UPlanetEditComponent>(
		CharacterCDO->GetDefaultSubobjectByName(TEXT("PlanetEdit")));
	TestNotNull(TEXT("角色 CDO 上挂了 PlanetEdit 组件"), CharacterEdit);

	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

Expected: FAIL，报 `PlanetEditComponent.h: No such file or directory`。

- [ ] **Step 3: 写编辑组件**

创建 `Source/StellarEraVehicle/Public/PlanetEditComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 角色身上的行星编辑组件：模式、笔刷状态、屏幕射线与体素工具调用。

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.generated.h"

class APlanetCharacter;
class AVoxelPlanetActor;

/**
 * 编辑模式的状态与执行。
 *
 * 挂在 APlanetCharacter 上，不挂在行星上：模式属于「谁在编辑」，角色换星、
 * 上下船、进星图时状态要跟着角色走。行星只负责网格与保存。
 *
 * 本头不 include 任何 Voxel / Planet 头：Voxel 与 Planet 都是本模块的私有依赖。
 * AVoxelPlanetActor 只前向声明，所有插件调用都在 .cpp。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAVEHICLE_API UPlanetEditComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlanetEditComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent Interface

	bool IsEditMode() const { return bEditMode; }
	EPlanetBrushShape GetBrushShape() const { return BrushShape; }
	int32 GetBrushSizeVoxels() const { return BrushSizeVoxels; }

	/** 进入 / 退出编辑模式。进入时显示当前行星的网格，退出时隐藏。 */
	void SetEditMode(bool bEnabled);
	void ToggleEditMode() { SetEditMode(!bEditMode); }

	void SetBrushShape(EPlanetBrushShape NewShape);

	/** 滚轮 ±1；内部夹到 [1, 8]。 */
	void AdjustBrushSize(int32 Delta);

	/** 屏幕位置挖一次。返回是否真的改到了体素。 */
	bool DigAtScreenPosition(const FVector2D& ScreenPosition);

	/** 屏幕位置放一次。返回是否真的改到了体素。 */
	bool PlaceAtScreenPosition(const FVector2D& ScreenPosition);

	/** 射线最远距离（厘米）。默认 1000 米：足够覆盖任意可瞄准的表面。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "100.0"))
	float MaxEditDistanceCm = 1.0e5f;

private:
	bool ApplyBrushAtScreenPosition(const FVector2D& ScreenPosition, bool bPlace);
	AVoxelPlanetActor* ResolvePlanet() const;

	/** 是否在编辑模式。不进反射：这是运行时状态，不是可配置项。 */
	bool bEditMode = false;

	UPROPERTY(EditAnywhere, Category = "PlanetEdit")
	EPlanetBrushShape BrushShape = EPlanetBrushShape::Sphere;

	/** 直径 / 边长（体素数）。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1", ClampMax = "8"))
	int32 BrushSizeVoxels = 2;
};
```

创建 `Source/StellarEraVehicle/Private/PlanetEditComponent.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetEditComponent.h"

#include "PlanetCharacter.h"
#include "VoxelPlanetActor.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelTools/Gen/VoxelSphereTools.h"
#include "VoxelVector.h"

UPlanetEditComponent::UPlanetEditComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlanetEditComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 角色被销毁时把网格收掉，否则切换 Pawn 的路径漏了退出就会留下网格。
	if (bEditMode)
	{
		SetEditMode(false);
	}
	Super::EndPlay(EndPlayReason);
}

AVoxelPlanetActor* UPlanetEditComponent::ResolvePlanet() const
{
	const APlanetCharacter* Character = Cast<APlanetCharacter>(GetOwner());
	return Character ? Character->PlanetActor.Get() : nullptr;
}

void UPlanetEditComponent::SetEditMode(const bool bEnabled)
{
	if (bEditMode == bEnabled)
	{
		return;
	}

	bEditMode = bEnabled;

	if (AVoxelPlanetActor* Planet = ResolvePlanet())
	{
		Planet->SetEditGridVisible(bEditMode);
	}
}

void UPlanetEditComponent::SetBrushShape(const EPlanetBrushShape NewShape)
{
	BrushShape = NewShape;
}

void UPlanetEditComponent::AdjustBrushSize(const int32 Delta)
{
	BrushSizeVoxels = PlanetEditBrush::ClampSize(BrushSizeVoxels + Delta);
}

bool UPlanetEditComponent::DigAtScreenPosition(const FVector2D& ScreenPosition)
{
	return ApplyBrushAtScreenPosition(ScreenPosition, /*bPlace=*/false);
}

bool UPlanetEditComponent::PlaceAtScreenPosition(const FVector2D& ScreenPosition)
{
	return ApplyBrushAtScreenPosition(ScreenPosition, /*bPlace=*/true);
}

bool UPlanetEditComponent::ApplyBrushAtScreenPosition(
	const FVector2D& ScreenPosition,
	const bool bPlace)
{
	if (!bEditMode)
	{
		return false;
	}

	AVoxelPlanetActor* Planet = ResolvePlanet();
	UWorld* World = GetWorld();
	if (!Planet || !Planet->IsCreated() || !World)
	{
		return false;
	}

	const APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!PlayerController)
	{
		return false;
	}

	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	// Deproject 的参数是 float；本项目警告即错误，double→float 必须显式转。
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

	const FVector End = WorldOrigin + WorldDirection * MaxEditDistanceCm;

	// 两条通道都打：体素碰撞的 presets 由关卡决定，可能只在其中一条上响应。
	// 只接受命中 Actor 就是当前行星的结果——飞船、角色、其它 Actor 都能挡住编辑。
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PlanetEditBrush), /*bTraceComplex=*/true);
	Params.AddIgnoredActor(GetOwner());

	FHitResult VisibilityHit;
	const bool bVisibilityHit = World->LineTraceSingleByChannel(
		VisibilityHit, WorldOrigin, End, ECC_Visibility, Params);

	FHitResult StaticHit;
	const bool bStaticHit = World->LineTraceSingleByChannel(
		StaticHit, WorldOrigin, End, ECC_WorldStatic, Params);

	const FHitResult* Chosen = nullptr;
	if (bVisibilityHit && VisibilityHit.GetActor() == Planet)
	{
		Chosen = &VisibilityHit;
	}
	if (bStaticHit && StaticHit.GetActor() == Planet &&
		(!Chosen || StaticHit.Distance < Chosen->Distance))
	{
		Chosen = &StaticHit;
	}
	if (!Chosen)
	{
		return false;
	}

	const FHitResult& Hit = *Chosen;
	const float VoxelSize = FMath::Max(Planet->VoxelSize, 1.f);
	const FVector Center = PlanetEditBrush::GetEditCenterWorld(
		Hit.Location, Hit.ImpactNormal, BrushSizeVoxels, VoxelSize, bPlace);

	if (BrushShape == EPlanetBrushShape::Sphere)
	{
		const float Radius = PlanetEditBrush::GetHalfExtentCm(BrushSizeVoxels, VoxelSize);
		if (bPlace)
		{
			UVoxelSphereTools::AddSphere(Planet, Center, Radius,
				/*OutModifiedValues=*/nullptr, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bConvertToVoxelSpace=*/true,
				/*bUpdateRender=*/true);
		}
		else
		{
			UVoxelSphereTools::RemoveSphere(Planet, Center, Radius,
				/*OutModifiedValues=*/nullptr, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bConvertToVoxelSpace=*/true,
				/*bUpdateRender=*/true);
		}
	}
	else
	{
		const FVoxelVector LocalCenter = Planet->GlobalToLocalFloat(Center);
		const FPlanetEditBox Box = PlanetEditBrush::MakeBox(
			FVector(LocalCenter.X, LocalCenter.Y, LocalCenter.Z), BrushSizeVoxels);
		const FVoxelIntBox Bounds(Box.Min, Box.Max);

		if (bPlace)
		{
			UVoxelBoxTools::AddBox(Planet, Bounds,
				/*OutModifiedValues=*/nullptr, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bUpdateRender=*/true);
		}
		else
		{
			UVoxelBoxTools::RemoveBox(Planet, Bounds,
				/*OutModifiedValues=*/nullptr, /*OutEditedBounds=*/nullptr,
				/*bMultiThreaded=*/false, /*bUpdateRender=*/true);
		}
	}

	Planet->NotifyVoxelEdited();
	return true;
}
```

- [ ] **Step 4: 挂到角色上**

在 `PlanetCharacter.h` 前向声明区加：

```cpp
class UPlanetEditComponent;
```

在 `public:` 段（`PlanetActor` 属性之后）加：

```cpp
/** 编辑模式组件。角色天生带一个，不需要蓝图配置。 */
UPlanetEditComponent* GetPlanetEditComponent() const { return PlanetEdit; }
```

在 `protected:` 段的 `VoxelInvoker` 之后加：

```cpp
/** 行星编辑模式的状态与执行。 */
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
TObjectPtr<UPlanetEditComponent> PlanetEdit;
```

在 `PlanetCharacter.cpp` 的 include 区加：

```cpp
#include "PlanetEditComponent.h"
```

在构造函数 `VoxelInvoker->bUseForPriorities = false;` 之后加：

```cpp
PlanetEdit = CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"));
```

把 `ReevaluatePlanet()` 改成：

```cpp
void APlanetCharacter::ReevaluatePlanet()
{
	AVoxelPlanetActor* const OldPlanet = PlanetActor;
	AVoxelPlanetActor* Nearest = FindNearestPlanet();
	if (Nearest == OldPlanet)
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

	// 编辑模式跟着角色走：网格是行星上的组件，换星时把可见性从旧星搬到新星。
	// 不搬的话，站到另一颗星上按左键，网格还留在上一颗星上。
	if (PlanetEdit && PlanetEdit->IsEditMode())
	{
		if (OldPlanet)
		{
			OldPlanet->SetEditGridVisible(false);
		}
		if (PlanetActor)
		{
			PlanetActor->SetEditGridVisible(true);
		}
	}
}
```

- [ ] **Step 5: 加 BOM**

```bash
for f in Source/StellarEraVehicle/Public/PlanetEditComponent.h \
         Source/StellarEraVehicle/Private/PlanetEditComponent.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xef\xbb\xbf' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
```

- [ ] **Step 6: 构建并运行测试**

构建命令同 Task 1 Step 5，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditComponent" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditComponentTests.log"
```

Expected: `Result={Success}`。若 `FPlanetEditBox` 转 `FVoxelIntBox` 报错，确认用的是 `FVoxelIntBox(Box.Min, Box.Max)`（两个参数都是 `FIntVector`）。

- [ ] **Step 7: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEraVehicle/Public/PlanetEditComponent.h \
        Source/StellarEraVehicle/Private/PlanetEditComponent.cpp \
        Source/StellarEraVehicle/Public/PlanetCharacter.h \
        Source/StellarEraVehicle/Private/PlanetCharacter.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 角色编辑组件与射线执行" -- \
  Source/StellarEraVehicle/Public/PlanetEditComponent.h \
  Source/StellarEraVehicle/Private/PlanetEditComponent.cpp \
  Source/StellarEraVehicle/Public/PlanetCharacter.h \
  Source/StellarEraVehicle/Private/PlanetCharacter.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 6: 保存到行星的 SaveObject

**Files:**
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`
- Modify: `Source/StellarEra/Private/PlanetEditTests.cpp`（断言保存防抖默认值）
- Editor data: UniverseMap 里每颗 `AVoxelPlanetActor` 的 `SaveObject`

**Interfaces:**
- Consumes: `UVoxelDataTools::GetSave`、`UVoxelSaveUtilities::CompressVoxelSave`、`UVoxelWorldSaveObject::CopyDepthFromSave`。
- Produces: `AVoxelPlanetActor::SaveEditsToSaveObject()`；`NotifyVoxelEdited()` 追加 2 秒防抖保存；`EndPlay` 立即写一次。

**为什么不能依赖基类的自动保存：** `AVoxelWorld::SaveData()` 是编辑器专用（`WITH_EDITOR`）；运行时编辑必须走 `GetSave → CompressVoxelSave → SaveObject->Save`。插件在 `CreateWorldInternal` 里自动 `LoadFromSaveObject`，所以这份数据会在下次创建世界时读回。

- [ ] **Step 1: 追加失败测试**

在 `StellarEra.Planet.EditGrid.Component` 测试里、`TestFalse(TEXT("不可见时不 Tick")...)` 之后加：

```cpp
	TestEqual(TEXT("默认保存防抖 2 秒"), PlanetCDO->SaveDebounceSeconds, 2.f, 0.01f);
```

- [ ] **Step 2: 构建，确认失败**

构建命令同 Task 1 Step 5。Expected: FAIL，报 `SaveDebounceSeconds` 不是 `AVoxelPlanetActor` 的成员。

- [ ] **Step 3: 实现保存**

在 `VoxelPlanetActor.h` 的 `#include "VoxelWorld.h"` 之后加：

```cpp
#include "Engine/TimerHandle.h"
```

在 `public:` 段（`NotifyVoxelEdited` 之后）加：

```cpp
/**
 * 把当前体素数据写进 SaveObject（压缩后）。没有 SaveObject 时只记一条
 * Warning（每颗行星一次），不崩溃、也不阻止继续编辑。
 *
 * 由 NotifyVoxelEdited 的 2 秒防抖调用，EndPlay 再无条件兜一次。
 */
void SaveEditsToSaveObject();
```

在 `//~ Begin AActor Interface` 区加：

```cpp
virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
```

在 `EditGrid` 属性之后加：

```cpp
/** 编辑后多久写一次 SaveObject（秒）。 */
UPROPERTY(EditAnywhere, Category = "Planet|Edit", meta = (ClampMin = "0.0"))
float SaveDebounceSeconds = 2.f;
```

在类末尾的私有区（`ComputeParamsHash` 之后）加：

```cpp
	FTimerHandle SaveTimerHandle;
	bool bHasUnsavedEdits = false;
	bool bLoggedMissingSaveObject = false;
```

在 `VoxelPlanetActor.cpp` 的 include 区加：

```cpp
#include "TimerManager.h"
#include "VoxelData/VoxelSave.h"
#include "VoxelData/VoxelSaveUtilities.h"
#include "VoxelTools/VoxelDataTools.h"
```

把 `NotifyVoxelEdited()` 改成：

```cpp
void AVoxelPlanetActor::NotifyVoxelEdited()
{
	bHasUnsavedEdits = true;

	if (EditGrid)
	{
		EditGrid->NotifyVoxelEdited();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(SaveTimerHandle,
			FTimerDelegate::CreateUObject(this, &AVoxelPlanetActor::SaveEditsToSaveObject),
			FMath::Max(SaveDebounceSeconds, 0.f), false);
	}
}
```

在 `NotifyVoxelEdited()` 之后加：

```cpp
void AVoxelPlanetActor::SaveEditsToSaveObject()
{
	if (!bHasUnsavedEdits)
	{
		return;
	}

	// 先清标志：即使这次失败（没有 SaveObject / 世界没建），也不要在每次
	// EndPlay 时重复刷同一条警告。下一次编辑会重新置位。
	bHasUnsavedEdits = false;

	if (!SaveObject)
	{
		if (!bLoggedMissingSaveObject)
		{
			bLoggedMissingSaveObject = true;
			UE_LOG(LogTemp, Warning,
				TEXT("AVoxelPlanetActor %s 没有 SaveObject，编辑只存在于本次运行的内存里。"),
				*GetName());
		}
		return;
	}

	if (!IsCreated())
	{
		return;
	}

	FVoxelUncompressedWorldSave UncompressedSave;
	UVoxelDataTools::GetSave(this, UncompressedSave);
	UVoxelSaveUtilities::CompressVoxelSave(UncompressedSave, SaveObject->Save);

	// Depth 不参与压缩，必须从 Save 补上；否则下次加载时世界尺寸对不上。
	SaveObject->CopyDepthFromSave();
	SaveObject->MarkPackageDirty();
}

void AVoxelPlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SaveTimerHandle);
	}

	// 防抖窗口内退出 PIE 也不能丢最后一次编辑。
	SaveEditsToSaveObject();

	Super::EndPlay(EndPlayReason);
}
```

- [ ] **Step 4: 构建并运行测试**

构建命令同 Task 1 Step 5，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditGrid.Component" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/PlanetEditSaveTests.log"
```

Expected: `Result={Success}`。

- [ ] **Step 5: 在 UniverseMap 里创建并分配 SaveObject**

确认编辑器已打开、`UniverseMap` 是当前关卡、MCP 可用（`http://127.0.0.1:8000/mcp`）。用 `execute_python_code` 运行：

```python
import unreal

ASSET_DIR = "/Game/PlanetSaves"
if not unreal.EditorAssetLibrary.does_directory_exist(ASSET_DIR):
    unreal.EditorAssetLibrary.make_directory(ASSET_DIR)

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
factory = unreal.VoxelWorldSaveObjectFactory()

assigned = []
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if not isinstance(actor, unreal.VoxelPlanetActor):
        continue
    label = actor.get_actor_label()
    asset_name = "Save_" + label.replace(" ", "_")
    asset_path = ASSET_DIR + "/" + asset_name
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        save_obj = unreal.EditorAssetLibrary.load_asset(asset_path)
    else:
        save_obj = asset_tools.create_asset(
            asset_name, ASSET_DIR, unreal.VoxelWorldSaveObject, factory)
    actor.set_editor_property("SaveObject", save_obj)
    readback = actor.get_editor_property("SaveObject")
    assigned.append((label, asset_path, readback.get_path_name() if readback else None))

for entry in assigned:
    unreal.log("PlanetSave: %s -> %s (readback=%s)" % entry)
if not assigned:
    unreal.log_warning("PlanetSave: UniverseMap 里没找到 AVoxelPlanetActor")
```

Expected: 每颗行星一行 `PlanetSave: ... -> /Game/PlanetSaves/Save_xxx (readback=/Game/PlanetSaves/Save_xxx)`，读回不为 `None`。**必须看读回值**：本仓库出现过属性写入后读回旧值的情况（见 `python-level-edit-initialvelocity-revert` 记忆）。

若 `unreal.VoxelWorldSaveObjectFactory` 抛 `AttributeError`：改用编辑器 UI 创建——Content Browser 右键 → Miscellaneous → Voxel World Save Object，放在 `Content/PlanetSaves/`，然后在每颗行星 Details 面板的 `Voxel - Save → Save Object` 槽位里指定。命名统一 `Save_<行星名>`。

- [ ] **Step 6: 保存关卡**

编辑器里 `Ctrl+S` 保存 `UniverseMap`（MCP 的 OFPA 保存有已知限制，这一步由用户手动做）。然后重新打开关卡，确认每颗行星的 `Save Object` 仍然指向 `/Game/PlanetSaves/Save_xxx`。

- [ ] **Step 7: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
        Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 编辑结果写入行星 SaveObject" -- \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp \
  Source/StellarEra/Private/PlanetEditTests.cpp
```

`Save_*.uasset` 与 `UniverseMap.umap` 是否提交由用户决定；资产走 LFS。

---

### Task 7: 输入分派与 HUD 面板

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Modify: `Source/StellarEra/Public/StellarEraHUD.h`
- Modify: `Source/StellarEra/Private/StellarEraHUD.cpp`

**Interfaces:**
- Consumes: `UPlanetEditComponent`（Task 5）、`PlanetEditUI`（Task 2）。
- Produces: G 键进入/退出；左键挖（按钮优先）；右键点击放、拖拽转视角；滚轮改笔刷尺寸；HUD 两个按钮。

- [ ] **Step 1: 控制器头文件加声明**

在 `StellarEraPlayerController.h` 的 `// --- 角色输入回调 ---` 区加：

```cpp
	/** G 键：切换编辑模式。只有当前 Pawn 是角色时生效。 */
	void OnEditModeToggle(const FInputActionValue& Value);
```

在 `IA_StarMapSelect` 属性之后加：

```cpp
	/** 编辑模式开关（G）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_EditModeToggle;
```

在 `StarMapPressPosition` 之后加：

```cpp
	/** 编辑模式右键按下的屏幕位置。松开时用它区分「点击放置」与「拖拽转视角」。 */
	FVector2D EditRightPressPosition = FVector2D::ZeroVector;

	/** 右键按下时是否记下了编辑位置（不是编辑模式 / 没拿到鼠标时为 false）。 */
	bool bEditRightPressValid = false;
```

- [ ] **Step 2: 控制器 .cpp 写实现**

include 区加：

```cpp
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.h"
#include "PlanetEditUI.h"
```

`SetupInputComponent()` 里、`IA_StarMapSelect` 绑定之后加：

```cpp
	EnhancedInput->BindAction(IA_EditModeToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnEditModeToggle);
```

`BuildInputAssets()` 里创建动作：

```cpp
	IA_EditModeToggle = MakeAction(this, TEXT("IA_EditModeToggle"), EInputActionValueType::Boolean);
```

`MapKey(MappingContext, IA_StarMapSelect, EKeys::LeftMouseButton);` 之后加：

```cpp
	MapKey(MappingContext, IA_EditModeToggle, EKeys::G);
```

`ApplyMouseModeForPawn()` 里清零标志：

```cpp
	bLookHeld = false;
	bStarMapSelectHeld = false;
	bEditRightPressValid = false;
```

把 `OnLookActivateStarted` / `OnLookActivateCompleted` 改成：

```cpp
void AStellarEraPlayerController::OnLookActivateStarted(const FInputActionValue& Value)
{
	bLookHeld = true;

	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	const UPlanetEditComponent* Edit = PawnCharacter
		? PawnCharacter->GetPlanetEditComponent()
		: nullptr;
	if (!Edit || !Edit->IsEditMode())
	{
		return;
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	bEditRightPressValid = GetMousePosition(MouseX, MouseY);
	if (bEditRightPressValid)
	{
		EditRightPressPosition = FVector2D(MouseX, MouseY);
	}
}

void AStellarEraPlayerController::OnLookActivateCompleted(const FInputActionValue& Value)
{
	bLookHeld = false;

	if (!bEditRightPressValid)
	{
		return;
	}
	bEditRightPressValid = false;

	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	UPlanetEditComponent* Edit = PawnCharacter
		? PawnCharacter->GetPlanetEditComponent()
		: nullptr;
	if (!Edit || !Edit->IsEditMode())
	{
		return;
	}

	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return;
	}

	// 与星图的左键点击共用同一个阈值：按住右键拖拽是转视角，松开时若几乎
	// 没动才算「点放」。拖动中相机转了，光标回到原点附近也不会误解成点击，
	// 因为玩家在拖动时不会刻意回到起点。
	const double MovedSq = FVector2D::DistSquared(
		EditRightPressPosition, FVector2D(MouseX, MouseY));
	if (MovedSq > ClickMoveThresholdPx * ClickMoveThresholdPx)
	{
		return;
	}

	Edit->PlaceAtScreenPosition(FVector2D(MouseX, MouseY));
}
```

把 `OnStarMapSelectStarted` 改成：

```cpp
void AStellarEraPlayerController::OnStarMapSelectStarted(const FInputActionValue& Value)
{
	// 角色 + 编辑模式：按钮优先，其次挖一次。按钮命中不能顺带挖到脚下的地形。
	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		UPlanetEditComponent* Edit = PawnCharacter->GetPlanetEditComponent();
		if (!Edit || !Edit->IsEditMode())
		{
			return;
		}

		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!GetMousePosition(MouseX, MouseY))
		{
			return;
		}
		const FVector2D MousePosition(MouseX, MouseY);

		const AStellarEraHUD* StellarHUD = Cast<AStellarEraHUD>(GetHUD());
		const FVector2D ViewportSize = StellarHUD
			? StellarHUD->GetViewportSize()
			: FVector2D::ZeroVector;

		int32 ButtonIndex = INDEX_NONE;
		if (ViewportSize.X > 0.f && ViewportSize.Y > 0.f &&
			PlanetEditUI::HitTestBrushButton(ViewportSize, MousePosition, ButtonIndex))
		{
			const bool bSphere = ButtonIndex == 0;
			Edit->SetBrushShape(bSphere ? EPlanetBrushShape::Sphere : EPlanetBrushShape::Cube);
			if (AStellarEraHUD* MutableHUD = Cast<AStellarEraHUD>(GetHUD()))
			{
				MutableHUD->ShowNotice(bSphere ? TEXT("笔刷：球体") : TEXT("笔刷：正方体"));
			}
			return;
		}

		Edit->DigAtScreenPosition(MousePosition);
		return;
	}

	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	// 用 double 重载，是为了后面直接喂给 FVector2D（UE5 里是双精度）时少一次转换。
	// **但精度上并不占便宜**：引擎那个重载（PlayerController.cpp:6186）内部就是
	// `float X, Y; GetMousePosition(X, Y);` —— 先过 float 再展宽，真正决定精度的
	// 是底层的 UGameViewportClient::GetMousePosition，两个重载拿到的是同一个值。
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (GetMousePosition(MouseX, MouseY))
	{
		StarMapPressPosition = FVector2D(MouseX, MouseY);
		bStarMapSelectHeld = true;
	}
}
```

把 `OnZoom` 改成：

```cpp
void AStellarEraPlayerController::OnZoom(const FInputActionValue& Value)
{
	const float Scroll = Value.Get<float>();

	if (APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		UPlanetEditComponent* Edit = PawnCharacter->GetPlanetEditComponent();
		if (Edit && Edit->IsEditMode())
		{
			// 编辑模式下滚轮改笔刷尺寸，相机臂长保持不动 —— 挖洞时要盯着脚下，
			// 顺手把镜头拉远/拉近会打断瞄准。
			if (!FMath::IsNearlyZero(Scroll))
			{
				Edit->AdjustBrushSize(Scroll > 0.f ? 1 : -1);
			}
			return;
		}

		PawnCharacter->AddZoom(Scroll);
	}
	else if (ASpaceshipPawn* Ship = Cast<ASpaceshipPawn>(GetPawn()))
	{
		Ship->AddZoom(Scroll);
	}
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->AddZoom(Scroll);
	}
}
```

新增回调（放在 `OnInteract` 之前）：

```cpp
void AStellarEraPlayerController::OnEditModeToggle(const FInputActionValue& Value)
{
	APlanetCharacter* PawnCharacter = Cast<APlanetCharacter>(GetPawn());
	if (!PawnCharacter)
	{
		return;
	}

	if (UPlanetEditComponent* Edit = PawnCharacter->GetPlanetEditComponent())
	{
		Edit->ToggleEditMode();
	}
}
```

`BoardShip()` 里、`PawnCharacter->AttachToActor(...)` 之前加：

```cpp
	// 上船强制退出编辑模式：网格是行星上的组件，不退出会一直留在外面。
	if (UPlanetEditComponent* Edit = PawnCharacter->GetPlanetEditComponent())
	{
		Edit->SetEditMode(false);
	}
```

`ToggleStarMap()` 进入分支里、`PreviousPawn = GetPawn();` 之后加：

```cpp
	// 进星图前退出编辑模式：星图里左键是拾取，不能同时保留笔刷状态与网格。
	if (APlanetCharacter* WalkingCharacter = Cast<APlanetCharacter>(PreviousPawn.Get()))
	{
		if (UPlanetEditComponent* Edit = WalkingCharacter->GetPlanetEditComponent())
		{
			Edit->SetEditMode(false);
		}
	}
```

`DebugPossessShip()` 的最后 `Possess(NearestShip);` 之前加：

```cpp
	if (APlanetCharacter* WalkingCharacter = Cast<APlanetCharacter>(GetPawn()))
	{
		if (UPlanetEditComponent* Edit = WalkingCharacter->GetPlanetEditComponent())
		{
			Edit->SetEditMode(false);
		}
	}
```

- [ ] **Step 3: HUD 头文件加声明**

在 `StellarEraHUD.h` 的前向声明区加：

```cpp
class APlanetCharacter;
class UPlanetEditComponent;
```

在 `private:` 区、`GetViewedShip` 之后加：

```cpp
	/** 当前 Possess 的 Pawn 是角色就返回它，否则返回空。 */
	APlanetCharacter* GetViewedCharacter() const;

	/** 画底部居中的两个笔刷按钮与提示。只在角色的编辑模式下调用。 */
	void DrawPlanetEditPanel(const UPlanetEditComponent& Edit);
```

- [ ] **Step 4: HUD .cpp 写绘制**

include 区加：

```cpp
#include "PlanetCharacter.h"
#include "PlanetEditBrush.h"
#include "PlanetEditComponent.h"
#include "PlanetEditUI.h"
```

`DrawHUD()` 里，星图面板 block 之后、`DrawFrameRatePanel();` 之前加：

```cpp
	// 编辑模式面板：只有当前 Pawn 是角色且模式打开时画。
	if (const APlanetCharacter* Character = GetViewedCharacter())
	{
		if (const UPlanetEditComponent* Edit = Character->GetPlanetEditComponent())
		{
			if (Edit->IsEditMode())
			{
				DrawPlanetEditPanel(*Edit);
			}
		}
	}
```

在 `GetViewedShip()` 之后加：

```cpp
APlanetCharacter* AStellarEraHUD::GetViewedCharacter() const
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	return PlayerController
		? Cast<APlanetCharacter>(PlayerController->GetPawn())
		: nullptr;
}

void AStellarEraHUD::DrawPlanetEditPanel(const UPlanetEditComponent& Edit)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font || !Canvas)
	{
		return;
	}

	const FVector2D ViewportSize = GetViewportSize();
	FVector2D SpherePosition = FVector2D::ZeroVector;
	FVector2D CubePosition = FVector2D::ZeroVector;
	PlanetEditUI::GetBrushButtonRects(ViewportSize, SpherePosition, CubePosition);

	const bool bSphereSelected = Edit.GetBrushShape() == EPlanetBrushShape::Sphere;
	const bool bCubeSelected = !bSphereSelected;

	const FLinearColor SelectedColor(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor UnselectedColor(0.85f, 0.85f, 0.85f, 1.f);
	const FLinearColor Background(0.f, 0.f, 0.f, 0.55f);
	const float Size = PlanetEditUI::ButtonSize;

	// AHUD 的绘制参数都是 float，而 FVector2D 的分量是 double。
	// 本项目警告即错误，这里先转成 float 再用。
	const float SphereX = static_cast<float>(SpherePosition.X);
	const float SphereY = static_cast<float>(SpherePosition.Y);
	const float CubeX = static_cast<float>(CubePosition.X);
	const float CubeY = static_cast<float>(CubePosition.Y);

	// ── 球体按钮 ──
	DrawRect(Background, SphereX, SphereY, Size, Size);
	DrawBorder(this, SphereX, SphereY, Size, Size,
		bSphereSelected ? SelectedColor : UnselectedColor);

	// DrawLine 的参数也是 float：分量全部先落到 float 局部变量。
	const float SphereCenterX = SphereX + Size * 0.5f;
	const float SphereCenterY = SphereY + Size * 0.5f;
	const FLinearColor SphereColor = bSphereSelected ? SelectedColor : UnselectedColor;
	for (int32 Index = 0; Index < 24; ++Index)
	{
		const float Angle0 = 2.f * PI * Index / 24.f;
		const float Angle1 = 2.f * PI * (Index + 1) / 24.f;
		DrawLine(
			SphereCenterX + FMath::Cos(Angle0) * 14.f,
			SphereCenterY + FMath::Sin(Angle0) * 14.f,
			SphereCenterX + FMath::Cos(Angle1) * 14.f,
			SphereCenterY + FMath::Sin(Angle1) * 14.f,
			SphereColor, 2.f);
	}

	// ── 正方体按钮：等轴测六边形 + 顶面两条棱 + 中竖线 ──
	DrawRect(Background, CubeX, CubeY, Size, Size);
	DrawBorder(this, CubeX, CubeY, Size, Size,
		bCubeSelected ? SelectedColor : UnselectedColor);

	const float CubeCenterX = CubeX + Size * 0.5f;
	const float CubeCenterY = CubeY + Size * 0.5f;
	const FLinearColor CubeColor = bCubeSelected ? SelectedColor : UnselectedColor;

	// 六边形顶点用一对 float 数组：避免 FVector2D 的 double 分量喂进 DrawLine。
	const float HexX[6] =
	{
		CubeCenterX, CubeCenterX + 12.f, CubeCenterX + 12.f,
		CubeCenterX, CubeCenterX - 12.f, CubeCenterX - 12.f
	};
	const float HexY[6] =
	{
		CubeCenterY - 14.f, CubeCenterY - 7.f, CubeCenterY + 7.f,
		CubeCenterY + 14.f, CubeCenterY + 7.f, CubeCenterY - 7.f
	};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const int32 Next = (Index + 1) % 6;
		DrawLine(HexX[Index], HexY[Index], HexX[Next], HexY[Next], CubeColor, 2.f);
	}

	const float TopCenterY = CubeCenterY - 7.f;
	DrawLine(CubeCenterX, TopCenterY,
		HexX[4], HexY[4], CubeColor, 1.5f);
	DrawLine(CubeCenterX, TopCenterY,
		HexX[1], HexY[1], CubeColor, 1.5f);
	DrawLine(CubeCenterX, TopCenterY,
		CubeCenterX, CubeCenterY + 7.f, CubeColor, 1.5f);

	// ── 提示行 ──
	const FString Hint = FString::Printf(
		TEXT("笔刷 %d 体素 · 左键挖 / 右键点放 · G 退出"),
		Edit.GetBrushSizeVoxels());

	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Hint, TextWidth, TextHeight, Font, TextScale);

	const float HintY = SphereY + Size + PlanetEditUI::HintGap;
	DrawText(Hint, FLinearColor::White,
		static_cast<float>((ViewportSize.X - TextWidth) * 0.5), HintY, Font, TextScale);
}
```

- [ ] **Step 5: 构建**

构建命令同 Task 1 Step 5。Expected: 成功。

- [ ] **Step 6: 无头跑整套自动化**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllAfterEditMode.log"
```

Expected: 所有 `Result={Success}`，没有 `Result={Fail}`。

- [ ] **Step 7: 提交（仅在用户明确要求时）**

```bash
git add Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp \
        Source/StellarEra/Public/StellarEraHUD.h \
        Source/StellarEra/Private/StellarEraHUD.cpp
git commit -m "feat(行星编辑): G 键输入分派与 HUD 笔刷面板" -- \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp \
  Source/StellarEra/Public/StellarEraHUD.h \
  Source/StellarEra/Private/StellarEraHUD.cpp
```

---

### Task 8: 整体验证

**Files:** 无新增；只验证前七个任务。

- [ ] **Step 1: 关闭编辑器，全量构建**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: `Result: Succeeded`（或 `Result: Failed` 但仅仅 DLL 链接失败且编译无错——见 `ubt-direct-dotnet-invoke` 记忆；先看错误行）。

- [ ] **Step 2: 全量自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllFinal.log"
```

```bash
grep -E "Test Completed|Result=\{(Success|Fail|Failed)\}" \
  F:/workspace/ue/StellarEra/Saved/Logs/StellarEraAllFinal.log | tail -60
```

Expected: 本计划新增的 6 条测试全部 `Result={Success}`；既有 `StellarEra.*` 套件无回归。

- [ ] **Step 3: 确认没有改第三方插件**

```bash
git status -- Plugins/VoxelFree Plugins/VibeUE
```

Expected: 空输出。

- [ ] **Step 4: 确认新文件 BOM 与换行**

```bash
for f in $(git status --porcelain | awk '$1=="??" {print $2}' | grep -E 'PlanetEdit|PlanetSurfaceGrid'); do
  printf '%s ' "$f"
  head -c 3 "$f" | od -An -tx1 | tr -d ' \n'
  printf '\n'
done
```

Expected: 每个文件都是 `efbbbf`。已存在文件不要用工具统一换行。

- [ ] **Step 5: PIE 手动验证（UniverseMap）**

编辑器打开 `UniverseMap`，PIE。逐条确认（这是 spec §10.2）：

1. 下船站在行星上，按 G：底部出现两个按钮，星球表面出现网格。
2. 左键点地形：出现球形坑；网格在坑里下凹。
3. 右键轻点：球体凸起；右键按住拖动：视角旋转且不放置。
4. 点正方体按钮：该按钮边框变金色、球体变灰；左键挖方块坑，右键放方块。
5. 滚轮：HUD 提示里的「笔刷 N 体素」跟着变；相机臂长不变。
6. 按 G 退出：按钮与网格消失。
7. 上船（E）/ 按 M 进星图：编辑模式自动退出，按钮与网格消失。
8. 挖完等 2 s，退出 PIE 再进：地形保持（编辑器 `Ctrl+S` 后确认；未保存时本次进程内仍可见）。
9. 飞到行星背面：背面网格不显示；网格在方块堆上起伏。

若第 2/3 步没有反应，按顺序排查：
- 控制台 `stat unit` 确认不是编辑器后台限 3 FPS（见 `ue-editor-background-throttle-3fps` 记忆）；
- 日志搜 `没有 SaveObject`，确认 SaveObject 已分配；
- 在编辑模式下用 `ShowDebug` 或临时日志确认 `LineTraceSingleByChannel` 命中的 Actor 是否为行星。

- [ ] **Step 6: 汇报**

说明实际使用过的技能/Agent（本计划由 `superpowers:writing-plans` 产出；实现阶段使用 `superpowers:executing-plans` 或 `superpowers:subagent-driven-development`，UE C++ 部分遵循 `unreal-engine-cpp-pro`）。不要声称完成除非构建与测试命令的真实输出都通过。

---

## 自检记录

- **Spec 覆盖**：§1 目标 1–4 → Task 1/2/4/7；§5 输入 → Task 7；§6 笔刷几何 → Task 1/5；§7 HUD → Task 2/7；§8 网格 → Task 3/4；§9 持久化 → Task 6；§10 测试 → 各任务 + Task 8；§11 风险 → 已体现在实现选择中（双通道射线、SaveObject 为空警告、Vehicle 公开头不引 Voxel）。
- **类型一致性**：`GetHalfExtentCm` / `GetEditCenterWorld` / `MakeBox` / `ClampSize` 在 Task 1 定义，Task 5 使用；`PlanetEditUI::HitTestBrushButton(..., int32&)` 在 Task 2 定义，Task 7 使用；`SetEditGridVisible` / `NotifyVoxelEdited` 在 Task 4 定义，Task 5 使用；`SaveDebounceSeconds` 在 Task 6 定义并被 Task 6 的测试断言。
- **已知不在自动化范围**：密度采样、LineBatch 绘制、射线命中、SaveObject 读回都需要真实世界；这些只在 Task 8 的 PIE 清单里验证。
