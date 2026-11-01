# 矿物视图改用体素世界 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把矿物透视视图的矿体几何从「进入时一次性 marching cubes」换成「每个元素一个独立体素世界」。

**Architecture:** 每个元素 spawn 一个 `AVoxelWorld` 子类，它的生成器按「行星实心 ∧ 该元素 Norm > 阈值」产出密度。行星体素数据是挖掘的唯一真值，矿物世界是它的只读派生 —— 无存档、进模式建、退模式销毁。

**Tech Stack:** Unreal Engine 5.8.3 / Win64 / C++；VoxelFree 插件（`Plugins/VoxelFree`，不改）；`FPlanetGeologyField` 解析地质场。

**Spec:** `docs/superpowers/specs/2026-11-01-mineral-voxel-view-design.md`

## Global Constraints

- **UE 5.8.3 / Win64 / Development**。仓库没有 `.sln` / `.vcxproj`，这是正常的。
- **不要改 `Plugins/VoxelFree` 与 `Plugins/VibeUE`**。所有自定义代码写进 `Source/StellarEraPlanet`。
- **含中文的 `.h` / `.cpp` 存 UTF-8 with BOM**，否则 MSVC 按 GBK 读会产生假 IntelliSense 错误。
- **不要统一换行符**。仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- **构建前必须关闭编辑器**（Live Coding 会锁 DLL）。构建命令：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
- **测试命令**（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra.Planet.MineralView" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout \
    -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTest.log"
  ```
- **测试名前缀 `StellarEra.Planet.*`**；`*Tests.cpp` 只能放 `Source/StellarEraPlanet/Src/Tests/`。
- **提交必须带 pathspec**：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- **`StellarEraPlanet` 不依赖任何其它项目模块**，也不依赖 `UnrealEd`。
- **体素值符号**：`FVoxelValue` 负 = 实心。`GetValue(...).IsEmpty()` 为真即空气。
- **`FVoxelData::Get` 不创建节点**（只沿 `HasChildren()` 下走），但读它必须持有读锁（`VoxelData/VoxelDataLock.h` 的 `FVoxelReadScopeLock`）。
- **`FVoxelGeneratorInstance` 的宿主模板参数必须是 `UVoxelGenerator` 子类**，填 `UDataAsset` 会当场 checkf。
- **`UVoxelGenerator` 子类上不能用 `EditAnywhere` + `FString`**（`GetParameters` 只认 9 种类型，会弹 ensure）。生成器参数一律普通成员，由调用方直接赋值。

## 已核对的插件 API（写代码时直接照用，不要再猜）

```cpp
// Plugins/VoxelFree/Source/Voxel/Public/VoxelWorld.h
bool   bCreateWorldAutomatically = false;   // :175
FVoxelGeneratorPicker Generator;            // :164
int32  RenderOctreeDepth = 10;              // :208
uint32 WorldSizeInVoxel;                    // :212  ← uint32，不是 int32
TArray<FVoxelLODMaterials> LODMaterials;    // :259
TObjectPtr<UMaterialInterface> VoxelMaterial = nullptr; // :251
bool   bEnableCollisions = true;            // :457
bool   bEnableNavmesh = false;              // :523
bool   bCreateGlobalPool = true;            // :537
void   CreateWorld(FVoxelWorldCreateInfo);  // :607
void   DestroyWorld();                      // :611
bool   IsCreated() const;                   // :652

// Plugins/VoxelFree/Source/Voxel/Public/VoxelGenerators/VoxelGeneratorInstance.h
v_flt GetValue(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack&) const;      // 公开
TVoxelRange<v_flt> GetValueRange(const FVoxelIntBox&, int32 LOD, const FVoxelItemStack&) const; // 公开

// Plugins/VoxelFree/Source/Voxel/Public/VoxelData/VoxelDataLock.h
FVoxelReadScopeLock(FVoxelData& Data, const FVoxelIntBox& Bounds, const FString& Name);
```

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `Src/Minerals/View/PlanetMineralVoxelSampling.h/.cpp` | **纯函数**：给定参数求一个体素的密度值。不碰世界、不碰 UObject。 |
| `Src/Minerals/View/PlanetMineralVoxelGenerator.h/.cpp` | `UVoxelGenerator` 子类 + 生成器实例。把采样纯函数接进 VoxelFree。 |
| `Src/Minerals/View/VoxelMineralFieldActor.h/.cpp` | `AVoxelWorld` 子类。一个实例 = 一个元素的体素世界。 |
| `Src/Minerals/View/PlanetMineralViewActor.h/.cpp`（改） | 宿主：管 N 个 field actor、球壳、显隐/染色转发。 |
| `Src/Minerals/View/PlanetMineralViewComponent.h/.cpp`（改） | 进出模式、元素表、统计 API、过渡期路径开关。 |
| `Src/Tests/PlanetMineralVoxelSamplingTests.cpp`（新） | 采样纯函数的单元测试。 |
| `Src/Tests/PlanetMineralVoxelGeneratorTests.cpp`（新） | 生成器实例的冒烟测试。 |
| `Src/Tests/PlanetMineralViewTests.cpp`（改） | 观察体测试跟着新接口走。 |
| `Src/Minerals/Surface/PlanetMineralSurface.*`（删） | Task 7 退役。 |

---

### Task 1: 采样纯函数

**Files:**
- Create: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.h`
- Create: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelSamplingTests.cpp`

**Interfaces:**
- Produces:
  - `struct FPlanetMineralVoxelParams` —— 字段见下，Task 2/3/5 都用它
  - `double PlanetMineralVoxel::EvaluateVoxel(const FPlanetMineralVoxelParams&, const FIntVector& VoxelIndex)`
  - `bool PlanetMineralVoxel::IsInsideAnyRegion(const TArray<FBox>&, const FVector& NormalizedPoint)`
  - `constexpr double PlanetMineralVoxel::AirValue`

- [ ] **Step 1: 写失败的测试**

Create `Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelSamplingTests.cpp`（UTF-8 with BOM）：

```cpp
// 采样纯函数的单元测试：早退顺序、密度门、阈值映射。
// 全部用注入的回调，不建体素世界、不读资产。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 一个恒定返回 Norm 的采样回调，并记录被调了几次。 */
	struct FConstantNormSampler
	{
		float Norm = 0.f;
		mutable int32 Calls = 0;

		FPlanetMineralVoxelParams MakeParams(double IsoValue = 0.22, double FadeRange = 0.15) const
		{
			FPlanetMineralVoxelParams Params;
			Params.RadiusVoxels = 100.0;
			Params.IsoValue = IsoValue;
			Params.FadeRange = FadeRange;
			Params.RegionBoxes.Add(FBox(FVector(-10.0), FVector(10.0)));
			Params.SampleElementNorm = [this](const FVector&) -> float
			{
				++Calls;
				return Norm;
			};
			Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };
			return Params;
		}
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelRegionEarlyOutTest,
	"StellarEra.Planet.MineralView.VoxelSample.RegionEarlyOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelRegionEarlyOutTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	Sampler.Norm = 1.0f; // 就算地质场说这里是纯矿
	const FPlanetMineralVoxelParams Params = Sampler.MakeParams();

	// 区域盒是归一化空间的 [-10, 10]，半径 100 体素 → 体素 [-1000, 1000]。
	TestEqual(TEXT("盒内：Norm 1 > 阈值 → 负值（实心）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0, true);
	TestTrue(TEXT("盒内确实调了地质场"), Sampler.Calls > 0);

	Sampler.Calls = 0;
	const double Outside = PlanetMineralVoxel::EvaluateVoxel(
		Params, FIntVector(5000, 0, 0)); // 归一化 50，盒外
	TestEqual(TEXT("盒外恒为空气"), Outside, PlanetMineralVoxel::AirValue);
	TestEqual(TEXT("盒外不调地质场"), Sampler.Calls, 0);

	// 空区域表 = 到处都不是
	FPlanetMineralVoxelParams NoRegions = Params;
	NoRegions.RegionBoxes.Reset();
	TestEqual(TEXT("空区域表恒为空气"),
		PlanetMineralVoxel::EvaluateVoxel(NoRegions, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelDensityGateTest,
	"StellarEra.Planet.MineralView.VoxelSample.DensityGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelDensityGateTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	Sampler.Norm = 1.0f;
	FPlanetMineralVoxelParams Params = Sampler.MakeParams();

	// 行星在那里是空气（被挖空）→ 矿不存在，且地质场根本不用调。
	Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return false; };
	Sampler.Calls = 0;
	TestEqual(TEXT("行星空气 → 矿也是空气"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);
	TestEqual(TEXT("密度门早退，不调地质场"), Sampler.Calls, 0);

	// 实心 → 矿存在
	Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };
	TestTrue(TEXT("行星实心 → 有矿"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0);

	// 回调未绑定 → 视作实心（不因为漏绑就整颗星没矿）
	Params.IsSolidAtPlanetVoxel = nullptr;
	TestTrue(TEXT("未绑密度回调时视作实心"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) < 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelThresholdTest,
	"StellarEra.Planet.MineralView.VoxelSample.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelThresholdTest::RunTest(const FString& Parameters)
{
	FConstantNormSampler Sampler;
	const FPlanetMineralVoxelParams Params = Sampler.MakeParams(/*IsoValue=*/0.22, /*FadeRange=*/0.15);

	Sampler.Norm = 0.22f;
	TestEqual(TEXT("Norm == 阈值 → 0（等值面）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)), 0.0, 1e-9);

	Sampler.Norm = 0.30f;
	const double Above = PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0));
	TestTrue(TEXT("Norm 高于阈值 → 负（实心）"), Above < 0.0);
	TestEqual(TEXT("高于阈值的量按 FadeRange 缩放"),
		Above, (0.22 - 0.30) / 0.15, 1e-9);

	Sampler.Norm = 0.10f;
	TestTrue(TEXT("Norm 低于阈值 → 正（空气）"),
		PlanetMineralVoxel::EvaluateVoxel(Params, FIntVector(0, 0, 0)) > 0.0);

	// FadeRange 是资产上可编辑的值，0 会让除法爆掉
	FPlanetMineralVoxelParams ZeroFade = Params;
	ZeroFade.FadeRange = 0.0;
	Sampler.Norm = 0.30f;
	const double Guarded = PlanetMineralVoxel::EvaluateVoxel(ZeroFade, FIntVector(0, 0, 0));
	TestTrue(TEXT("FadeRange 0 时结果仍然有限"), FMath::IsFinite(Guarded));
	TestTrue(TEXT("FadeRange 0 时符号仍然正确"), Guarded < 0.0);
	TestTrue(TEXT("结果不超过 FVoxelValue 的 int16 量程"),
		FMath::Abs(Guarded) <= double(MAX_VOXELVALUE));

	// 地质场为空 → 到处空气
	FPlanetMineralVoxelParams NoSampler = Params;
	NoSampler.SampleElementNorm = nullptr;
	TestEqual(TEXT("没有采样回调时恒为空气"),
		PlanetMineralVoxel::EvaluateVoxel(NoSampler, FIntVector(0, 0, 0)),
		PlanetMineralVoxel::AirValue);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelRegionBoxTest,
	"StellarEra.Planet.MineralView.VoxelSample.RegionBox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelRegionBoxTest::RunTest(const FString& Parameters)
{
	TArray<FBox> Boxes;
	Boxes.Add(FBox(FVector(-1.0, -0.5, -0.5), FVector(0.0, 0.5, 0.5)));
	Boxes.Add(FBox(FVector(0.5, -0.5, -0.5), FVector(1.0, 0.5, 0.5)));

	TestTrue(TEXT("第一个盒内"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(-0.5, 0.0, 0.0)));
	TestTrue(TEXT("第二个盒内"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(0.75, 0.0, 0.0)));
	TestFalse(TEXT("两个盒之间的空隙"),
		PlanetMineralVoxel::IsInsideAnyRegion(Boxes, FVector(0.25, 0.0, 0.0)));

	TArray<FBox> NoBoxes;
	TestFalse(TEXT("空盒数组恒为假"),
		PlanetMineralVoxel::IsInsideAnyRegion(NoBoxes, FVector(0.0, 0.0, 0.0)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

构建（会报头文件不存在，这是预期）：
```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: FAIL —— `Cannot open include file: 'Minerals/View/PlanetMineralVoxelSampling.h'`

- [ ] **Step 3: 写实现**

Create `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.h`（UTF-8 with BOM）：

```cpp
// 矿物等值面的采样核心：给定一组注入的回调，求一个体素的密度值。
//
// 刻意做成纯函数 + 注入回调，而不是直接持有 FPlanetGeologyField 与行星 Actor：
// 这样单元测试不需要建体素世界、也不需要地质资产，注入两个 lambda 就能覆盖
// 全部早退分支。

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

/**
 * 一次矿物求值需要的全部输入。纯值类型，可跨线程共享（回调必须线程安全）。
 *
 * 坐标约定：全部在**行星体素索引空间**。行星生成器的 Center 恒为原点
 * （VoxelPlanetActor.cpp 的 RebuildPlanet），所以归一化点 = 索引 / RadiusVoxels。
 */
struct FPlanetMineralVoxelParams
{
	/**
	 * 该元素所有区域的 AABB，**归一化空间**（半径 1 = 行星基准球面）。
	 * 来自 FPlanetGeologyField::EnumerateRegions。空数组 = 这个元素没有矿。
	 */
	TArray<FBox> RegionBoxes;

	/**
	 * 该元素在归一化点上的强度 ∈ [0,1]。为空时求值恒返回空气。
	 * 运行期绑到 FPlanetGeologyField::SampleElementNormAtNormalized。
	 */
	TFunction<float(const FVector& NormalizedPoint)> SampleElementNorm;

	/**
	 * 行星在该体素索引处是否实心。为空时**视作实心**（漏绑不该让整颗星没矿）。
	 * 运行期绑到行星的 FVoxelData 读取。
	 */
	TFunction<bool(const FIntVector& VoxelIndex)> IsSolidAtPlanetVoxel;

	/** 行星基准半径（体素数）。≤ 0 时求值恒返回空气。 */
	double RadiusVoxels = 0.0;

	/** 等值线，调用方传 GetAppearance().ColorThreshold。 */
	double IsoValue = 0.0;

	/** 软化带宽，调用方传 GetAppearance().FadeRange。≤ 0 时按 1e-4 处理。 */
	double FadeRange = 0.15;
};

namespace PlanetMineralVoxel
{
	/**
	 * 所有早退分支返回的空气值。
	 *
	 * 取正数而不是 0：等值面抽取只看符号，恒正保证「空气」不会因为浮点误差
	 * 被当成实心。
	 */
	constexpr double AirValue = 1.0;

	/** 归一化点是否落在任一区域盒内。空数组恒 false。 */
	STELLARERAPLANET_API bool IsInsideAnyRegion(
		const TArray<FBox>& RegionBoxes, const FVector& NormalizedPoint);

	/**
	 * 求一个体素的密度值。负 = 实心（是矿），正 = 空气。
	 *
	 * 早退顺序按代价排：
	 *   ① 区域 AABB   —— 纯算术
	 *   ② 行星密度    —— 一次八叉树走位
	 *   ③ 地质场求值  —— 最贵
	 * 绝大多数体素在 ① 就被判空，这是本方案比 marching cubes 便宜的主要来源。
	 *
	 * 返回值已夹到 FVoxelValue 的 int16 量程，调用方不必再夹。
	 */
	STELLARERAPLANET_API double EvaluateVoxel(
		const FPlanetMineralVoxelParams& Params, const FIntVector& VoxelIndex);
}
```

Create `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.cpp`（UTF-8 with BOM）：

```cpp
// 矿物等值面的采样核心。

#include "PlanetMineralVoxelSampling.h"

// MAX_VOXELVALUE：夹值用到，避免把 int16 的量程抄成一个字面数。
#include "VoxelMinimal.h"

bool PlanetMineralVoxel::IsInsideAnyRegion(
	const TArray<FBox>& RegionBoxes, const FVector& NormalizedPoint)
{
	for (const FBox& Box : RegionBoxes)
	{
		if (Box.IsValid && Box.IsInsideOrOn(NormalizedPoint))
		{
			return true;
		}
	}
	return false;
}

double PlanetMineralVoxel::EvaluateVoxel(
	const FPlanetMineralVoxelParams& Params, const FIntVector& VoxelIndex)
{
	if (!Params.SampleElementNorm || Params.RadiusVoxels <= 0.0)
	{
		return AirValue;
	}

	// ① 区域早退
	const FVector Normalized(
		double(VoxelIndex.X) / Params.RadiusVoxels,
		double(VoxelIndex.Y) / Params.RadiusVoxels,
		double(VoxelIndex.Z) / Params.RadiusVoxels);

	if (!IsInsideAnyRegion(Params.RegionBoxes, Normalized))
	{
		return AirValue;
	}

	// ② 密度门：行星被挖空了，矿就不在了
	if (Params.IsSolidAtPlanetVoxel && !Params.IsSolidAtPlanetVoxel(VoxelIndex))
	{
		return AirValue;
	}

	// ③ 地质场 + 屏阈
	const double Norm = double(Params.SampleElementNorm(Normalized));

	// FadeRange 是资产上可编辑的值，0 会让除法爆掉。钳到 1e-4 而不是跳过：
	// 跳过会把「阈值以上实心、以下空气」变成一个零宽的硬边。
	const double Fade = FMath::Max(Params.FadeRange, 1e-4);

	// 负 = 实心。Norm 越大越负 = 越是矿。
	//
	// 夹到 FVoxelValue 的 int16 量程：Fade 被钳到 1e-4 时这个除法能给出 1e4
	// 量级，而插件把 v_flt 写进 FVoxelValue 时溢出就是坏数据。
	const double Value = (Params.IsoValue - Norm) / Fade;
	return FMath::Clamp(Value, -double(MAX_VOXELVALUE), double(MAX_VOXELVALUE));
}
```

- [ ] **Step 4: 跑测试确认通过**

构建（命令同 Step 2），然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.MineralView.VoxelSample" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTest.log"
```

Expected: 4 个测试全部 PASS。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.h \
             Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.cpp \
             Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelSamplingTests.cpp
git commit -m "feat(矿物): 体素矿物的采样核心（纯函数 + 注入回调）

早退顺序按代价排：区域 AABB → 行星密度 → 地质场。
纯函数 + lambda 注入，单元测试不用体素世界也不用地质资产。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.h \
     Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelSampling.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelSamplingTests.cpp
```

---

### Task 2: VoxelFree 生成器适配器

**Files:**
- Create: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.h`
- Create: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelGeneratorTests.cpp`

**Interfaces:**
- Consumes: `FPlanetMineralVoxelParams`、`PlanetMineralVoxel::EvaluateVoxel`、`PlanetMineralVoxel::AirValue`（Task 1）
- Produces:
  - `UPlanetMineralVoxelGenerator`，公开方法 `void Configure(const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor)`
  - `virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override`
  - `GetValueRangeImpl` 对「不与任何区域相交」的块返回纯空气区间 —— Task 3 的世界性能全靠它

- [ ] **Step 1: 写失败的测试**

Create `Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelGeneratorTests.cpp`（UTF-8 with BOM）：

```cpp
// 生成器实例的冒烟测试：参数真的进了实例、区间查询会整块判空。
//
// 不建体素世界 —— 建世界要跑 BeginPlay，而 editor world 里不存在 BeginPlay。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Minerals/View/PlanetMineralVoxelGenerator.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	UPlanetMineralVoxelGenerator* MakeConfiguredGenerator(bool& bOutNormCalled)
	{
		UPlanetMineralVoxelGenerator* Generator =
			NewObject<UPlanetMineralVoxelGenerator>(GetTransientPackage());

		FPlanetMineralVoxelParams Params;
		Params.RadiusVoxels = 100.0;
		Params.IsoValue = 0.22;
		Params.FadeRange = 0.15;
		Params.RegionBoxes.Add(FBox(FVector(-0.2), FVector(0.2)));
		Params.SampleElementNorm = [&bOutNormCalled](const FVector&) -> float
		{
			bOutNormCalled = true;
			return 0.5f;
		};
		Params.IsSolidAtPlanetVoxel = [](const FIntVector&) { return true; };

		Generator->Configure(Params, FLinearColor::Red);
		return Generator;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelGeneratorInstanceTest,
	"StellarEra.Planet.MineralView.VoxelGenerator.Instance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelGeneratorInstanceTest::RunTest(const FString& Parameters)
{
	bool bNormCalled = false;
	UPlanetMineralVoxelGenerator* Generator = MakeConfiguredGenerator(bNormCalled);
	if (!TestNotNull(TEXT("生成器"), Generator))
	{
		return false;
	}

	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();
	if (!TestTrue(TEXT("实例有效"), Instance.IsValid()))
	{
		return false;
	}

	// 盒内（归一化 0.1 → 体素 10）：走完流程，Norm 0.5 > 0.22 → 负
	bNormCalled = false;
	const v_flt Inside = Instance->GetValue(10, 10, 10, /*LOD=*/0, FVoxelItemStack());
	TestTrue(TEXT("区域内的点是实心"), Inside < 0.0f);
	TestTrue(TEXT("区域内调了采样回调"), bNormCalled);

	// 盒外（归一化 0.9 → 体素 90）：恒空气
	bNormCalled = false;
	const v_flt Outside = Instance->GetValue(90, 0, 0, /*LOD=*/0, FVoxelItemStack());
	TestEqual(TEXT("区域外的点是空气"),
		float(Outside), float(PlanetMineralVoxel::AirValue));
	TestFalse(TEXT("区域外不调采样回调"), bNormCalled);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralVoxelGeneratorRangeTest,
	"StellarEra.Planet.MineralView.VoxelGenerator.Range",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralVoxelGeneratorRangeTest::RunTest(const FString& Parameters)
{
	bool bNormCalled = false;
	UPlanetMineralVoxelGenerator* Generator = MakeConfiguredGenerator(bNormCalled);
	if (!TestNotNull(TEXT("生成器"), Generator))
	{
		return false;
	}
	const TVoxelSharedRef<FVoxelGeneratorInstance> Instance = Generator->GetInstance();

	// 远离区域盒的块：区间必须整段在空气侧，插件才会整块跳过、一个体素都不求。
	// 这是矿物体素世界的性能杠杆 —— 返错区间会让每个块都被完整求值。
	const TVoxelRange<v_flt> FarRange = Instance->GetValueRange(
		FVoxelIntBox(FIntVector(500, 500, 500), FIntVector(540, 540, 540)),
		/*LOD=*/0, FVoxelItemStack());
	TestTrue(TEXT("远处块的区间下界 > 0"), FarRange.Min > 0.0f);

	// 与区域相交的块：区间必须跨过 0，否则插件会跳过整块、矿就没了。
	const TVoxelRange<v_flt> NearRange = Instance->GetValueRange(
		FVoxelIntBox(FIntVector(-15, -15, -15), FIntVector(15, 15, 15)),
		/*LOD=*/0, FVoxelItemStack());
	TestTrue(TEXT("区域块的区间跨过 0"),
		NearRange.Min < 0.0f && NearRange.Max > 0.0f);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 跑测试确认它失败**

构建命令同 Task 1 Step 2。
Expected: FAIL —— `Cannot open include file: 'Minerals/View/PlanetMineralVoxelGenerator.h'`

- [ ] **Step 3: 写实现**

Create `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.h`（UTF-8 with BOM）：

```cpp
// 把一个元素的矿物场接进 VoxelFree。
//
// 每元素一个本类实例。它自己**不产生任何几何定义** —— 判定全在
// PlanetMineralVoxel::EvaluateVoxel 里，这里只负责把参数搬进生成器实例、
// 并把区间查询翻译成「这整块是不是纯空气」。

#pragma once

#include "CoreMinimal.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "VoxelMinimal.h"
#include "PlanetMineralVoxelGenerator.generated.h"

/**
 * 生成器实例。
 *
 * 模板第二个参数是**宿主类型**，必须是 UVoxelGenerator 子类：
 * TVoxelGeneratorInstanceHelper 会把它作为 UStaticClass 交给基类，
 * 构造期 checkf 它确实派生自 UVoxelGenerator。
 */
class FPlanetMineralVoxelInstance
	: public TVoxelGeneratorInstanceHelper<FPlanetMineralVoxelInstance, UPlanetMineralVoxelGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<
		FPlanetMineralVoxelInstance, UPlanetMineralVoxelGenerator>;

	explicit FPlanetMineralVoxelInstance(
		const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor);

	//~ Begin FVoxelGeneratorInstance Interface
	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;
	//~ End FVoxelGeneratorInstance Interface

private:
	FPlanetMineralVoxelParams Params;
	FLinearColor Color;

	/**
	 * 区域盒换算到体素索引空间后的并集 AABB。GetValueRangeImpl 用它做整块判空。
	 *
	 * 单独存一份而不是每次重算：区间查询按块调用，重建 AABB 在块数量级上是纯浪费。
	 */
	FBox3d RegionBoundsVoxels;
	bool bHasRegionBounds = false;
};

/**
 * 一个元素的矿物生成器。
 *
 * 参数**不走** UVoxelGenerator 的属性系统：VoxelFree 的 GetParameters 会枚举
 * 可编辑属性并构造 FVoxelGeneratorParameterType，而它只认 9 种类型。全部参数
 * 由 UPlanetMineralViewComponent 在 CreateWorld 之前用 Configure 直接赋值。
 */
UCLASS()
class STELLARERAPLANET_API UPlanetMineralVoxelGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	//~ Begin UVoxelGenerator Interface
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;
	//~ End UVoxelGenerator Interface

	/** 一次性灌入全部参数。必须在 CreateWorld 之前调用。 */
	void Configure(const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor);

private:
	FPlanetMineralVoxelParams Params;
	FLinearColor Color = FLinearColor::White;
};
```

Create `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.cpp`（UTF-8 with BOM）：

```cpp
// 把一个元素的矿物场接进 VoxelFree。

#include "PlanetMineralVoxelGenerator.h"

#include "VoxelMaterialBuilder.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UPlanetMineralVoxelGenerator::GetInstance()
{
	return MakeVoxelShared<FPlanetMineralVoxelInstance>(Params, Color);
}

void UPlanetMineralVoxelGenerator::Configure(
	const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor)
{
	Params = InParams;
	Color = InColor;
}

///////////////////////////////////////////////////////////////////////////////

FPlanetMineralVoxelInstance::FPlanetMineralVoxelInstance(
	const FPlanetMineralVoxelParams& InParams, const FLinearColor& InColor)
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化
	// （FVoxelGeneratorPicker 每次建世界都重新调 GetInstance）。
	: Super(nullptr)
	, Params(InParams)
	, Color(InColor)
{
	// 区域盒从归一化空间换到体素索引空间，取并集。
	for (const FBox& Box : Params.RegionBoxes)
	{
		if (!Box.IsValid)
		{
			continue;
		}
		const FBox3d VoxelBox(
			FVector3d(Box.Min) * Params.RadiusVoxels,
			FVector3d(Box.Max) * Params.RadiusVoxels);
		if (!bHasRegionBounds)
		{
			RegionBoundsVoxels = VoxelBox;
			bHasRegionBounds = true;
		}
		else
		{
			RegionBoundsVoxels += VoxelBox;
		}
	}
}

v_flt FPlanetMineralVoxelInstance::GetValueImpl(
	v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 插件用浮点坐标调用，但我们的判据定义在整数体素索引上。
	// 取整而不是截断：负坐标下截断会往 0 靠，让球心附近错半格。
	const FIntVector Index(
		FMath::RoundToInt(double(X)),
		FMath::RoundToInt(double(Y)),
		FMath::RoundToInt(double(Z)));

	return v_flt(PlanetMineralVoxel::EvaluateVoxel(Params, Index));
}

FVoxelMaterial FPlanetMineralVoxelInstance::GetMaterialImpl(
	v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// 一个世界只有一种颜色。世界用 RGB 材质配置，颜色经顶点色传给材质。
	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
	Builder.SetColor(Color);
	return Builder.Build();
}

TVoxelRange<v_flt> FPlanetMineralVoxelInstance::GetValueRangeImpl(
	const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// 纯空气块：插件整块跳过，一个体素都不求。矿物体素世界的性能全靠这一条。
	if (!bHasRegionBounds
		|| !FBox3d(FVector3d(Bounds.Min), FVector3d(Bounds.Max)).Intersect(RegionBoundsVoxels))
	{
		return TVoxelRange<v_flt>(v_flt(PlanetMineralVoxel::AirValue));
	}

	// 与区域相交的块：给一个保守但紧的区间。契约要求它必须覆盖块内可能出现的
	// 所有取值，估窄了矿上会出洞。
	//
	// Norm ∈ [0, 1] → (Iso - Norm) / Fade ∈ [(Iso - 1) / Fade, Iso / Fade]。
	const double Fade = FMath::Max(Params.FadeRange, 1e-4);
	const double MaxValue = Params.IsoValue / Fade;
	const double MinValue = (Params.IsoValue - 1.0) / Fade;
	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(MaxValue));
}
```

- [ ] **Step 4: 跑测试确认通过**

构建 + 测试，过滤串换成 `StellarEra.Planet.MineralView.VoxelGenerator`。
Expected: 2 个测试 PASS。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.h \
             Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.cpp \
             Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelGeneratorTests.cpp
git commit -m "feat(矿物): 每元素一个 VoxelFree 生成器适配器

区间查询对不给区域相交的块返回纯空气，插件整块跳过 ——
这是矿物体素世界相对 marching cubes 的性能来源。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.h \
     Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralVoxelGenerator.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetMineralVoxelGeneratorTests.cpp
```

---

### Task 3: 矿物体素世界 Actor

**Files:**
- Create: `Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.h`
- Create: `Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.cpp`

**Interfaces:**
- Consumes: `UPlanetMineralVoxelGenerator::Configure`（Task 2）、`FPlanetMineralVoxelParams`（Task 1）
- Produces:
  - `bool AVoxelMineralFieldActor::CreateForElement(const FPlanetMineralVoxelParams& Params, const FLinearColor& Color, const AVoxelWorld& PlanetWorld, UMaterialInterface* BaseMaterial)`
  - `void AVoxelMineralFieldActor::DestroyField()`
  - `void AVoxelMineralFieldActor::SetShellColor(const FLinearColor& Color)`
  - `UMaterialInstanceDynamic* GetFieldMaterial() const`

本任务没有自动化测试：建体素世界需要 BeginPlay，而 editor world 不执行 BeginPlay。
验收靠构建通过 + Task 6 的手工 PIE 验证。

- [ ] **Step 1: 写头文件**

Create `Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.h`（UTF-8 with BOM）：

```cpp
// 一个元素一个体素世界。
//
// 它继承 AVoxelWorld（与 AVoxelPlanetActor 同一种做法），所以「世界就是本 Actor」，
// 没有子对象层。世界参数在 CreateForElement 里从行星整份复制，保证两边的体素
// 索引一一对应 —— 生成器因此不需要任何坐标换算。

#pragma once

#include "CoreMinimal.h"
#include "Minerals/View/PlanetMineralVoxelSampling.h"
#include "VoxelWorld.h"
#include "VoxelMineralFieldActor.generated.h"

class UMaterialInstanceDynamic;
class UPlanetMineralVoxelGenerator;

UCLASS()
class STELLARERAPLANET_API AVoxelMineralFieldActor : public AVoxelWorld
{
	GENERATED_BODY()

public:
	AVoxelMineralFieldActor();

	/**
	 * 复制行星的世界参数、挂上生成器、建世。
	 *
	 * 复制哪些：VoxelSize / RenderOctreeDepth / WorldSizeInVoxel /
	 * bUseCustomWorldBounds / CustomWorldBounds / MaxLOD / MinLOD / LODMaterials /
	 * Transform。不复制 SaveObject（矿物体素世界不存盘）。
	 *
	 * @param BaseMaterial 创建 MID 用的父材质（M_MineralBlock）。
	 * @return 失败时打 Error 并返回 false；调用方应当 Destroy 本 Actor。
	 */
	bool CreateForElement(
		const FPlanetMineralVoxelParams& Params,
		const FLinearColor& Color,
		const AVoxelWorld& PlanetWorld,
		UMaterialInterface* BaseMaterial);

	/** 销毁体素世界。重复调用安全。 */
	void DestroyField();

	/** 改颜色。世界已建时只换材质参数，不需要重建。 */
	void SetShellColor(const FLinearColor& Color);

	UMaterialInstanceDynamic* GetFieldMaterial() const { return FieldMaterial; }

	/** 体素世界是否已建。 */
	bool IsFieldCreated() const { return IsCreated(); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UPlanetMineralVoxelGenerator> MineralGenerator;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FieldMaterial;
};
```

- [ ] **Step 2: 写实现**

Create `Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.cpp`（UTF-8 with BOM）：

```cpp
// 一个元素一个体素世界。

#include "VoxelMineralFieldActor.h"

#include "Minerals/View/PlanetMineralVoxelGenerator.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AVoxelMineralFieldActor::AVoxelMineralFieldActor()
{
	// 基类构造函数已经把 WorldRoot 建好并设成 RootComponent。

	// 世界由 CreateForElement 显式创建。基类在 bCreateWorldAutomatically 为真时
	// 会排一个「下一帧 CreateWorld」的定时器，会和我们自己那次撞成建两次。
	bCreateWorldAutomatically = false;

	// 线程池由行星在建世时已经建好，这里只复用。基类默认 true 会让每个矿物世界
	// 各自抢着建池。
	bCreateGlobalPool = false;

	// 矿物体素世界不参与任何玩法碰撞与导航。
	bEnableCollisions = false;
	bEnableNavmesh = false;
}

bool AVoxelMineralFieldActor::CreateForElement(
	const FPlanetMineralVoxelParams& Params,
	const FLinearColor& Color,
	const AVoxelWorld& PlanetWorld,
	UMaterialInterface* BaseMaterial)
{
	// --- 世界参数整份复制：两边体素索引必须一一对应 ---
	//
	// 生成器在体素索引空间里直接读行星数据，不做任何坐标换算，前提就是这一组值
	// 与 Actor 变换完全一致。
	VoxelSize = PlanetWorld.VoxelSize;
	RenderOctreeDepth = PlanetWorld.RenderOctreeDepth;
	WorldSizeInVoxel = PlanetWorld.WorldSizeInVoxel;
	bUseCustomWorldBounds = PlanetWorld.bUseCustomWorldBounds;
	CustomWorldBounds = PlanetWorld.CustomWorldBounds;
	MaxLOD = PlanetWorld.MaxLOD;
	MinLOD = PlanetWorld.MinLOD;
	LODMaterials = PlanetWorld.LODMaterials;
	MaterialConfig = EVoxelMaterialConfig::RGB;

	SetActorTransform(PlanetWorld.GetActorTransform());

	// --- 材质 ---
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界缺少父材质"));
		return false;
	}

	FieldMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (!FieldMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界的动态材质创建失败"));
		return false;
	}
	FieldMaterial->SetVectorParameterValue(TEXT("BlockColor"), Color);
	VoxelMaterial = FieldMaterial;

	// --- 生成器 ---
	//
	// 必须在 CreateWorld 之前赋值：Generator 带 meta=(Recreate)，建世之后改它
	// 会报 "already created" 并保留旧生成器实例。
	MineralGenerator = NewObject<UPlanetMineralVoxelGenerator>(
		this, TEXT("MineralVoxelGenerator"));
	MineralGenerator->Configure(Params, Color);
	Generator = FVoxelGeneratorPicker(MineralGenerator.Get());

#if WITH_EDITOR
	// 编辑器世界里必须走插件自己的预览开关 Toggle()，不能直接调 CreateWorld()。
	//
	// AVoxelWorld::OnPreBeginPIE 在 PlayType == Preview 时无条件拆掉体素世界，
	// 而 OnEndPIE 只在 bIsToggled 为真时才建回来；bIsToggled 只有 Toggle() 会置真。
	// 直接 CreateWorld() 的话，编辑器里一进 PIE 矿体就消失且再也不会回来。
	// 与 AVoxelPlanetActor::RebuildPlanet 的同名分支是同一个理由。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		Toggle();
	}
	else
#endif
	{
		CreateWorld(FVoxelWorldCreateInfo());
	}

	if (!IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界创建失败"));
		return false;
	}

	return true;
}

void AVoxelMineralFieldActor::DestroyField()
{
	if (IsCreated())
	{
		DestroyWorld();
	}
	MineralGenerator = nullptr;
	FieldMaterial = nullptr;
}

void AVoxelMineralFieldActor::SetShellColor(const FLinearColor& Color)
{
	if (FieldMaterial)
	{
		FieldMaterial->SetVectorParameterValue(TEXT("BlockColor"), Color);
	}
}
```

- [ ] **Step 3: 构建**

构建命令见 Global Constraints。
Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.h \
             Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.cpp
git commit -m "feat(矿物): 每元素一个矿物体素世界 Actor

世界参数从行星整份复制（VoxelSize / 八叉树深度 / 世界边界 / LOD / 变换），
两边的体素索引因此一一对应，生成器不需要任何坐标换算。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.h \
     Source/StellarEraPlanet/Src/Minerals/View/VoxelMineralFieldActor.cpp
```

---

### Task 4: 观察体改为管理 N 个体素世界

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.h`
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.cpp`

**Interfaces:**
- Consumes: `AVoxelMineralFieldActor::CreateForElement` / `DestroyField` / `SetShellColor`（Task 3）
- Produces:
  - `bool BuildVoxelWorlds(const TArray<FPlanetMineralVoxelParams>& ParamsByElement, const AVoxelWorld& PlanetWorld, const FLinearColor& ShellColor, float SurfaceRadiusCm, const TArray<FPlanetGeologyElement>& Elements, UMaterialInterface* BlockMaterial)`
  - `void ReleaseAllFields()` / `void SyncToPlanetLocation(const FVector& PlanetCenterWorld)`
  - `AVoxelMineralFieldActor* GetElementField(int32) const` / `bool SetElementColor(int32, const FLinearColor&)`
  - 过渡期保留旧的 `Build(...)` / `GetElementMesh(...)`（Task 7 删除）

- [ ] **Step 1: 改头文件**

在 `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.h` 里，把
`#include "PlanetMineralSurface.h"` 之后的类前向声明区改为：

```cpp
class AVoxelMineralFieldActor;
class AVoxelWorld;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FPlanetMineralVoxelParams;
```

在 `public:` 区、现有 `Build(...)` 声明之前插入：

```cpp
	/**
	 * 按元素建 N 个体素世界。失败时把已建的逐个销毁，不留半成品。
	 *
	 * ParamsByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。空槽照样
	 * 建一个空世界 —— 下标才能与图例、数字键对齐。
	 */
	bool BuildVoxelWorlds(
		const TArray<FPlanetMineralVoxelParams>& ParamsByElement,
		const AVoxelWorld& PlanetWorld,
		const FLinearColor& ShellColor,
		float SurfaceRadiusCm,
		const TArray<FPlanetGeologyElement>& Elements);

	/** 销毁所有体素世界。重复调用安全。 */
	void ReleaseAllFields();

	/** 每帧把观察体钉到行星球心，并同步每个体素世界的位置。 */
	void SyncToPlanetLocation(const FVector& PlanetCenterWorld);

	/** 某个元素的体素世界，仅供测试与调试。越界返回 nullptr。 */
	AVoxelMineralFieldActor* GetElementField(int32 Index) const;

	/** 改某个元素的颜色。世界未建时返回 false。 */
	bool SetElementColor(int32 Index, const FLinearColor& Color);
```

`NumElements()` / `IsElementVisible` / `SetElementVisible` / `SetAllElementsVisible`
签名保持不变（实现改成两条路径都认）。

在 `private:` 区加入：

```cpp
	/** 下标 = 运行期元素下标。与 MineralMeshes 是两条互斥路径，同一时刻只有一条非空。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AVoxelMineralFieldActor>> ElementFields;

	/** 本观察体自己的根位置（= 行星球心），体素世界逐个同步到这里。 */
	FVector LastPlanetCenterWorld = FVector::ZeroVector;
```

- [ ] **Step 2: 改实现**

在 `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.cpp` 顶部
include 区加入：

```cpp
#include "VoxelMineralFieldActor.h"
#include "VoxelWorld.h"
```

（`Materials/MaterialInstanceDynamic.h` 与 `Engine/World.h` 已经有了。）

在文件末尾加入：

```cpp
///////////////////////////////////////////////////////////////////////////////
// 体素世界路径

bool APlanetMineralViewActor::BuildVoxelWorlds(
	const TArray<FPlanetMineralVoxelParams>& ParamsByElement,
	const AVoxelWorld& PlanetWorld,
	const FLinearColor& ShellColor,
	float SurfaceRadiusCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	ensureMsgf(!bHasBuilt, TEXT("[矿物] APlanetMineralViewActor 已建过，不可重入"));
	if (bHasBuilt)
	{
		return false;
	}

	if (SurfaceRadiusCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm"), SurfaceRadiusCm);
		return false;
	}

	if (ParamsByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 体素世界数不等于元素数：%d / %d"),
			ParamsByElement.Num(), Elements.Num());
		return false;
	}

	// 三个资产都在这里加载：LoadMineralViewMaterial 是本文件的匿名命名空间函数，
	// 组件那一侧看不见它，所以不能让调用方把材质传进来。
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, MineralViewSphereMeshPath);
	UMaterialInterface* GhostMaterial = LoadMineralViewMaterial(
		PlanetMineralMaterials::GetGhostMaterialPath(), TEXT("M_PlanetGhost"));
	UMaterialInterface* BlockMaterial = LoadMineralViewMaterial(
		PlanetMineralMaterials::GetBlockMaterialPath(), TEXT("M_MineralBlock"));

	if (!SphereMesh || !GhostMaterial || !BlockMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 网格或材质加载失败：球=%s 球壳材质=%s 矿体材质=%s"),
			SphereMesh ? TEXT("ok") : TEXT("缺"),
			GhostMaterial ? TEXT("ok") : TEXT("缺"),
			BlockMaterial ? TEXT("ok") : TEXT("缺"));
		return false;
	}

	// 到这里已经没有「什么都没建就失败」的退路了：后面要么全成、要么整体回滚。
	bHasBuilt = true;

	// --- 球壳：与 marching cubes 路径完全一样，不改 ---
	const float DiameterCm = SurfaceRadiusCm * 2.f;
	ShellMesh = NewObject<UStaticMeshComponent>(this, TEXT("MineralShell"));
	ShellMesh->SetupAttachment(SceneRoot);
	ShellMesh->SetMobility(EComponentMobility::Movable);
	ShellMesh->SetStaticMesh(SphereMesh);
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShellMesh->SetCastShadow(false);
	ShellMesh->RegisterComponent();
	ShellMesh->SetRelativeLocation(FVector::ZeroVector);
	ShellMesh->SetRelativeScale3D(FVector(DiameterCm / 100.f));

	ShellMaterial = UMaterialInstanceDynamic::Create(GhostMaterial, this);
	if (ShellMaterial)
	{
		ShellMaterial->SetVectorParameterValue(TEXT("ShellColor"), ShellColor);
		ShellMesh->SetMaterial(0, ShellMaterial);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[矿物] 球壳动态材质创建失败，球壳将留在默认材质"));
	}

	// --- 每元素一个体素世界 ---
	//
	// 失败就整体回滚：留一半的世界比彻底没建更糟 —— 图例下标会对上一半的矿。
	ElementFields.Reset();
	ElementFields.Reserve(ParamsByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < ParamsByElement.Num(); ++ElementIndex)
	{
		const FTransform PlanetTransform = PlanetWorld.GetActorTransform();
		AVoxelMineralFieldActor* Field = GetWorld()->SpawnActor<AVoxelMineralFieldActor>(
			AVoxelMineralFieldActor::StaticClass(),
			PlanetTransform.GetLocation(), PlanetTransform.Rotator());

		if (!Field
			|| !Field->CreateForElement(ParamsByElement[ElementIndex],
				Elements[ElementIndex].Color, PlanetWorld, BlockMaterial))
		{
			UE_LOG(LogTemp, Error, TEXT("[矿物] 元素 #%d 的体素世界创建失败"), ElementIndex);
			if (Field)
			{
				Field->Destroy();
			}
			ReleaseAllFields();
			return false;
		}

		ElementFields.Add(Field);
	}

	LastPlanetCenterWorld = PlanetWorld.GetActorLocation();
	return true;
}

void APlanetMineralViewActor::ReleaseAllFields()
{
	for (AVoxelMineralFieldActor* Field : ElementFields)
	{
		if (Field)
		{
			Field->DestroyField();
			Field->Destroy();
		}
	}
	ElementFields.Reset();
}

void APlanetMineralViewActor::SyncToPlanetLocation(const FVector& PlanetCenterWorld)
{
	// 观察体自己（球壳）跟着走。
	SetActorLocation(PlanetCenterWorld);
	LastPlanetCenterWorld = PlanetCenterWorld;

	// 体素世界不 attach 到本 Actor：AVoxelWorld 在构造时把 WorldRoot 立为根组件，
	// 再挂一个父级会让体素世界的局部/全局变换语义打架。改成直接写位置。
	for (AVoxelMineralFieldActor* Field : ElementFields)
	{
		if (Field)
		{
			Field->SetActorLocation(PlanetCenterWorld);
		}
	}
}

AVoxelMineralFieldActor* APlanetMineralViewActor::GetElementField(int32 Index) const
{
	return ElementFields.IsValidIndex(Index) ? ElementFields[Index].Get() : nullptr;
}

bool APlanetMineralViewActor::SetElementColor(int32 Index, const FLinearColor& Color)
{
	AVoxelMineralFieldActor* Field = GetElementField(Index);
	if (!Field)
	{
		return false;
	}
	Field->SetShellColor(Color);
	return true;
}
```

把现有的 `NumElements` / `IsElementVisible` / `SetElementVisible` /
`SetAllElementsVisible` 四个实现整体替换为**两条路径都认**的版本：

```cpp
int32 APlanetMineralViewActor::NumElements() const
{
	// 两条路径互斥，同一时刻只有一条非空。体素路径优先。
	return ElementFields.Num() > 0 ? ElementFields.Num() : MineralMeshes.Num();
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	if (ElementFields.IsValidIndex(Index) && ElementFields[Index])
	{
		return !ElementFields[Index]->IsHidden();
	}
	return MineralMeshes.IsValidIndex(Index) && MineralMeshes[Index]
		&& MineralMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (ElementFields.IsValidIndex(Index) && ElementFields[Index])
	{
		ElementFields[Index]->SetActorHiddenInGame(!bVisible);
		return;
	}
	if (!MineralMeshes.IsValidIndex(Index) || !MineralMeshes[Index])
	{
		return;
	}
	MineralMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (AVoxelMineralFieldActor* Field : ElementFields)
	{
		if (Field)
		{
			Field->SetActorHiddenInGame(!bVisible);
		}
	}
	for (UDynamicMeshComponent* Component : MineralMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
```

- [ ] **Step 3: 构建**

Expected: 编译通过。

- [ ] **Step 4: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.h \
             Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.cpp
git commit -m "feat(矿物): 观察体改为管理每元素一个体素世界

体素世界不 attach 到观察体（AVoxelWorld 的 WorldRoot 本身就是根组件），
由 SyncToPlanetLocation 逐帧同步位置。过渡期与 marching cubes 路径并存。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.h \
     Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.cpp
```

---

### Task 5: 组件切换到体素路径 + 统计

**Files:**
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.h`
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.cpp`
- Test: `Source/StellarEraPlanet/Src/Tests/PlanetMineralViewTests.cpp`

**Interfaces:**
- Consumes: `BuildVoxelWorlds` / `SyncToPlanetLocation` / `ReleaseAllFields` / `SetElementColor`（Task 4）；`AVoxelMineralFieldActor`（Task 3）
- Produces:
  - `bool bUseVoxelWorlds`（过渡期开关，默认 true，Task 7 删除）
  - `float GetElementVolumeCm3(int32 ElementIndex, float StepCm) const`
  - `TArray<float> GetAllElementVolumesCm3(float StepCm) const`
  - `bool SetElementColor(int32 ElementIndex, const FLinearColor& Color)`

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEraPlanet/Src/Tests/PlanetMineralViewTests.cpp` 末尾、`#endif`
之前加入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewStatisticsTest,
	"StellarEra.Planet.MineralView.Statistics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewStatisticsTest::RunTest(const FString& Parameters)
{
	// 统计的对外契约：没有地质场时必须安全失败（返回 0 / 空表），不能崩。
	//
	// 用 NewObject 而不是 SpawnActor 出组件：SpawnActor 会跑 OnConstruction，
	// 那里会建整个行星体素世界，无头进程里能耗上几分钟。
	AVoxelPlanetActor* Planet = NewObject<AVoxelPlanetActor>(GetTransientPackage());
	UPlanetMineralViewComponent* View = Planet
		? NewObject<UPlanetMineralViewComponent>(Planet)
		: nullptr;
	if (!TestNotNull(TEXT("组件"), View))
	{
		return false;
	}

	TestEqual(TEXT("没有地质场时体积为 0"),
		View->GetElementVolumeCm3(0, 100.f), 0.f, 1e-6f);
	TestEqual(TEXT("没有地质场时整表为空"),
		View->GetAllElementVolumesCm3(100.f).Num(), 0);

	// 非法的步长同样安全失败。
	TestEqual(TEXT("步长 <= 0 时体积为 0"),
		View->GetElementVolumeCm3(0, 0.f), 0.f, 1e-6f);

	return true;
}
```

- [ ] **Step 2: 跑测试确认它失败**

构建 + 测试，过滤串 `StellarEra.Planet.MineralView.Statistics`。
Expected: FAIL —— `GetElementVolumeCm3` 不是 `UPlanetMineralViewComponent` 的成员。

- [ ] **Step 3: 改头文件**

在 `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.h` 顶部
include 区加入：

```cpp
#include "VoxelData/VoxelDataLock.h"
```

在 `public:` 区加入：

```cpp
	/**
	 * 过渡期开关：true 走体素世界，false 走 marching cubes。
	 *
	 * 只在两条路径并存的那一轮有用，用于 A/B 对照矿体位置。Task 7 删除 MC
	 * 路径时本开关一并删除 —— 不许留在主干。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	bool bUseVoxelWorlds = true;

	/** 改某个元素的颜色。体素路径即时生效；没有视图时返回 false。 */
	bool SetElementColor(int32 ElementIndex, const FLinearColor& Color);

	/**
	 * 某个元素的矿量（立方厘米）。
	 *
	 * **走解析场，不走体素世界**：体素世界里的体素数量随 LOD 变（远处块边长
	 * 64 米、近处 1 米），数出来的值不是稳定量。这里按固定步长对区域 AABB 采样，
	 * 结果与相机位置无关。
	 *
	 * @param StepCm 采样步长（厘米）。≤ 0 或没有地质场时返回 0。
	 */
	float GetElementVolumeCm3(int32 ElementIndex, float StepCm) const;

	/** 所有元素的矿量，下标 = 运行期元素下标。 */
	TArray<float> GetAllElementVolumesCm3(float StepCm) const;
```

在 `private:` 区加入：

```cpp
	/**
	 * 视图期间持有的行星体素数据读锁。
	 *
	 * 为什么整段视图期间只锁一次而不是逐体素加锁：生成器在插件工作线程上按体素
	 * 调用密度查询，逐体素加锁的开销会吃掉全部收益。视图期间不做挖掘、且
	 * Activate 已经停掉了行星自己的 LOD 调用器，所以只会与激活瞬间仍在飞的
	 * 少量块生成任务竞争 —— 那把读锁的等待有界。
	 */
	TUniquePtr<FVoxelReadScopeLock> PlanetDataLock;
```

- [ ] **Step 4: 改实现**

在 `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.cpp` 的
include 区加入：

```cpp
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelMineralFieldActor.h"
#include "VoxelViewpointInvokerUtils.h"
```

在匿名命名空间里加入：

```cpp
	/**
	 * 把地质场翻成每个元素一份的采样参数。
	 *
	 * 两个回调都捕获**共享指针**而不是裸指针：地质场对象可能被 ReloadGeology
	 * 换掉，裸指针会悬空；持有共享指针同时保证旧对象不会在生成器还在用的时候
	 * 被释放。
	 */
	TArray<FPlanetMineralVoxelParams> MakeVoxelParams(
		AVoxelPlanetActor* Planet,
		const TSharedPtr<const FPlanetGeologyField>& Geology,
		const TArray<FPlanetGeologyElement>& Elements,
		float SurfaceRadiusCm)
	{
		TArray<FPlanetMineralVoxelParams> Out;
		Out.SetNum(Elements.Num());

		const double RadiusVoxels = double(SurfaceRadiusCm) / double(Planet->VoxelSize);
		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();

		for (int32 Index = 0; Index < Elements.Num(); ++Index)
		{
			FPlanetMineralVoxelParams& Params = Out[Index];
			Params.RadiusVoxels = RadiusVoxels;
			Params.IsoValue = Appearance.ColorThreshold;
			Params.FadeRange = Appearance.FadeRange;

			TArray<FPlanetGeologyRegion> Regions;
			Geology->EnumerateRegions(Index, Regions);
			Params.RegionBoxes.Reserve(Regions.Num());
			for (const FPlanetGeologyRegion& Region : Regions)
			{
				Params.RegionBoxes.Add(Region.Bounds);
			}

			const TSharedPtr<const FPlanetGeologyField> FieldForElement = Geology;
			Params.SampleElementNorm =
				[FieldForElement, Index](const FVector& NormalizedPoint) -> float
			{
				return FieldForElement->SampleElementNormAtNormalized(Index, NormalizedPoint);
			};

			TWeakObjectPtr<AVoxelPlanetActor> WeakPlanet = Planet;
			Params.IsSolidAtPlanetVoxel = [WeakPlanet](const FIntVector& VoxelIndex) -> bool
			{
				const AVoxelPlanetActor* Pinned = WeakPlanet.Get();
				if (!Pinned || !Pinned->IsCreated())
				{
					// 没建好时视作实心：宁可多画，不要整颗星没矿。
					return true;
				}
				return !Pinned->GetData().GetValue(VoxelIndex, /*LOD=*/0).IsEmpty();
			};
		}
		return Out;
	}
```

在 `Activate` 里，把「建观察体」那一段（从 `FActorSpawnParameters SpawnParams;`
到 `Elements = SampledElements;`）整体替换为：

```cpp
	FActorSpawnParameters SpawnParams;
	if (bInEditorWorld)
	{
		SpawnParams.ObjectFlags |= RF_Transient;
	}
	SpawnParams.Owner = GetOwner();
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlanetMineralViewActor* SpawnedView = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!SpawnedView)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体 spawn 失败：%s"), *InPlanet->GetName());
		return false;
	}

	bool bBuilt = false;
	if (bUseVoxelWorlds)
	{
		// 体素路径要求行星自己停下来：视图期间没有挖掘、也不需要行星继续生成块。
		// 停掉它同时让下面那把长读锁的等待有界 —— 只在飞的少量块会与它竞争。
		if (UVoxelSimpleInvokerComponent* PlanetInvoker =
			InPlanet->FindComponentByClass<UVoxelSimpleInvokerComponent>())
		{
			VoxelViewpointInvokers::SetEnabled(PlanetInvoker, false);
		}

		// 读锁必须在建世之前拿：生成器一 CreateWorld 就开始在自己的工作线程上
		// 读行星数据。
		PlanetDataLock = MakeUnique<FVoxelReadScopeLock>(
			InPlanet->GetData(),
			FVoxelIntBox(FIntVector::ZeroValue,
				FIntVector(int32(InPlanet->WorldSizeInVoxel))),
			TEXT("PlanetMineralVoxelView"));

		const TArray<FPlanetMineralVoxelParams> ParamsByElement =
			MakeVoxelParams(InPlanet, Geology, SampledElements, SurfaceRadiusCm);

		bBuilt = SpawnedView->BuildVoxelWorlds(
			ParamsByElement, *InPlanet, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements);
	}
	else
	{
		bBuilt = SpawnedView->Build(CachedSurface.MeshesByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, SampledElements);
	}

	ViewActor = SpawnedView;
	if (!bBuilt)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		ViewActor->Destroy();
		ViewActor = nullptr;
		PlanetDataLock.Reset();
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;
```

在 `TickComponent` 里把「钉到行星球心」那几行改成：

```cpp
	ViewActor->SyncToPlanetLocation(Planet->GetActorLocation());
```

在 `Deactivate` 里，把销毁观察体那一段换成：

```cpp
	if (APlanetMineralViewActor* View = ViewActor)
	{
		View->ReleaseAllFields();
		View->Destroy();
	}
	ViewActor = nullptr;

	// 先放锁再恢复行星调用器：反过来的话行星一恢复就开始写数据，
	// 而我们还持着读锁 —— 那才是真的死锁。
	PlanetDataLock.Reset();

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		if (!bEditorPreview)
		{
			if (UVoxelSimpleInvokerComponent* PlanetInvoker =
				RestorePlanet->FindComponentByClass<UVoxelSimpleInvokerComponent>())
			{
				VoxelViewpointInvokers::SetEnabled(PlanetInvoker, true);
			}
		}
	}
```

在文件末尾加入统计实现：

```cpp
namespace
{
	/**
	 * 逐元素统计矿量。走解析场：与相机、LOD、体素世界全部无关。
	 *
	 * 采样点在**归一化空间**，步长从厘米换算过去。区域 AABB 之外的采样点直接跳过
	 * —— 那里 Norm 恒为 0，换算成体素后只会白白跑一遍分布求值。
	 */
	float ComputeElementVolumeCm3(
		const FPlanetGeologyField& Geology,
		const AVoxelPlanetActor& Planet,
		int32 ElementIndex,
		float StepCm,
		float SurfaceRadiusCm)
	{
		if (StepCm <= 0.f || SurfaceRadiusCm <= 0.f || Planet.VoxelSize <= 0.f)
		{
			return 0.f;
		}

		TArray<FPlanetGeologyRegion> Regions;
		Geology.EnumerateRegions(ElementIndex, Regions);
		if (Regions.IsEmpty())
		{
			return 0.f;
		}

		const float Threshold = Geology.GetAppearance().ColorThreshold;
		const double RadiusVoxels = double(SurfaceRadiusCm) / double(Planet.VoxelSize);
		const double StepVoxels = double(StepCm) / double(Planet.VoxelSize);

		const bool bPlanetCreated = Planet.IsCreated();
		int64 SolidSamples = 0;

		for (const FPlanetGeologyRegion& Region : Regions)
		{
			if (!Region.Bounds.IsValid)
			{
				continue;
			}
			const FVector3d Min = FVector3d(Region.Bounds.Min) * RadiusVoxels;
			const FVector3d Max = FVector3d(Region.Bounds.Max) * RadiusVoxels;

			for (double Z = Min.Z; Z <= Max.Z; Z += StepVoxels)
			{
				for (double Y = Min.Y; Y <= Max.Y; Y += StepVoxels)
				{
					for (double X = Min.X; X <= Max.X; X += StepVoxels)
					{
						const FVector Normalized(
							X / RadiusVoxels, Y / RadiusVoxels, Z / RadiusVoxels);

						if (Geology.SampleElementNormAtNormalized(ElementIndex, Normalized) <= Threshold)
						{
							continue;
						}

						if (bPlanetCreated)
						{
							const FIntVector Index(
								FMath::RoundToInt(X), FMath::RoundToInt(Y), FMath::RoundToInt(Z));
							if (Planet.GetData().GetValue(Index, /*LOD=*/0).IsEmpty())
							{
								continue;
							}
						}

						++SolidSamples;
					}
				}
			}
		}

		const double VoxelSizeCm = double(Planet.VoxelSize);
		return float(double(SolidSamples) * StepVoxels * StepVoxels * StepVoxels
			* VoxelSizeCm * VoxelSizeCm * VoxelSizeCm);
	}
} // namespace

float UPlanetMineralViewComponent::GetElementVolumeCm3(
	int32 ElementIndex, float StepCm) const
{
	const AVoxelPlanetActor* PlanetPtr = Planet.Get();
	const TSharedPtr<const FPlanetGeologyField>& GeologyPtr = CachedGeology;
	if (!PlanetPtr || !GeologyPtr.IsValid() || !Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	return ComputeElementVolumeCm3(
		*GeologyPtr, *PlanetPtr, ElementIndex, StepCm, PlanetPtr->GetSurfaceRadiusCm());
}

TArray<float> UPlanetMineralViewComponent::GetAllElementVolumesCm3(float StepCm) const
{
	TArray<float> Out;
	Out.SetNum(Elements.Num());
	for (int32 Index = 0; Index < Elements.Num(); ++Index)
	{
		Out[Index] = GetElementVolumeCm3(Index, StepCm);
	}
	return Out;
}

bool UPlanetMineralViewComponent::SetElementColor(
	int32 ElementIndex, const FLinearColor& Color)
{
	return ViewActor ? ViewActor->SetElementColor(ElementIndex, Color) : false;
}
```

- [ ] **Step 5: 跑测试确认通过**

构建 + 测试，过滤串 `StellarEra.Planet.MineralView.Statistics`。
Expected: PASS。

- [ ] **Step 6: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.h \
             Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.cpp \
             Source/StellarEraPlanet/Src/Tests/PlanetMineralViewTests.cpp
git commit -m "feat(矿物): 组件切到体素世界路径，并加按解析场的矿量统计

统计不走体素世界：体素数量随 LOD 变，不是稳定量。
视图期间停掉行星的 LOD 调用器并持一把长读锁，避免逐体素加锁。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.h \
     Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.cpp \
     Source/StellarEraPlanet/Src/Tests/PlanetMineralViewTests.cpp
```

---

### Task 6: PIE 手工验收

本任务没有代码改动，只有验收。**必须在 `UniverseMap` 里做** —— 只有这个关卡有
体素行星、球面重力与 N 体（`NewMap` 没有 `AVoxelPlanetActor`）。

- [ ] **Step 1: 关编辑器、构建、重开**

构建命令见 Global Constraints。构建必须在编辑器关闭时做（Live Coding 锁 DLL）。

- [ ] **Step 2: 进 PIE，打开矿物视图**

在 `UniverseMap` 里 PIE → 星图 → 选中行星 → 按 H。

逐项检查并记下 `Saved/Logs/` 里的行：

- [ ] **进视图时游戏线程不卡死** —— 停在原地超过约 1 秒就记为失败。这是长读锁
      与行星在飞块生成竞争的表现；真卡住就回 Task 5 改成逐块加锁。
- [ ] `[矿物] ... 体素世界创建失败` 一条都没有
- [ ] 屏幕上出现 5 个体素矿体 + 半透明球壳
- [ ] 矿体的位置与**地表矿脉的着色位置**一致（同一条判据：`Norm > ColorThreshold`）
- [ ] 数字键逐个切换元素：只隐藏对应那一个，其它不受影响
- [ ] **行星移动时矿体跟着走** —— 等一分钟看矿体有没有飘离行星（N 体每帧都在动，
      这是 `SyncToPlanetLocation` 的验收）

- [ ] **Step 3: 性能对比**

同一个视角下，切 `bUseVoxelWorlds` 进两次视图（Details 面板能改），记录：

- MC 路径：日志里那条构建耗时
- 体素路径：进视图到矿体全部出现的秒数

**性能数据必须在编辑器前台的窗口里测** —— 编辑器不在前台会整进程限 3 FPS，
测出来的秒数没有意义。

- [ ] **Step 4: 挖掘联动验收**

退出视图 → 用编辑模式在某个矿上书挖一个洞 → 重新进视图。

- [ ] 洞的位置上**不再有矿**
- [ ] 洞以外的矿不受影响

- [ ] **Step 5: 记录结果**

把 Step 2~4 的结论追加到本文件末尾的「验收记录」小节：

- 5 个世界是否都建起来了、进视图是否卡顿
- 两条路径的耗时
- 挖掘联动是否符合预期
- 远处矿脉的实际观感（spec §8 风险 2 已确认「接受变糊」，记下糊到什么程度）

**如果任一验收项不通过，停下来回到对应的 Task 修，不要继续 Task 7。**

---

### Task 7: 退役 marching cubes 路径

**前置条件：Task 6 全部通过。**

**Files:**
- Delete: `Source/StellarEraPlanet/Src/Minerals/Surface/PlanetMineralSurface.h`
- Delete: `Source/StellarEraPlanet/Src/Minerals/Surface/PlanetMineralSurface.cpp`
- Delete: `Source/StellarEraPlanet/Src/Tests/PlanetMineralSurfaceTests.cpp`
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewActor.h` / `.cpp`
- Modify: `Source/StellarEraPlanet/Src/Minerals/View/PlanetMineralViewComponent.h` / `.cpp`
- Modify: `Source/StellarEraPlanet/Src/Tests/PlanetMineralViewTests.cpp`

- [ ] **Step 1: 先确认没有别的地方在用**

```bash
cd "F:/workspace/ue/StellarEra" && grep -rn "PlanetMineralSurface\|FPlanetMineralMesh\|CellsPerFeature" Source/ --include=*.h --include=*.cpp | grep -v "Surface/PlanetMineralSurface"
```

Expected: 只剩 `PlanetMineralViewActor.*`、`PlanetMineralViewComponent.*`、
`Tests/PlanetMineralViewTests.cpp`、`Tests/PlanetMineralSurfaceTests.cpp`。
**如果出现别的文件，停下来先处理它，不要直接删。**

- [ ] **Step 2: 删文件**

```bash
cd "F:/workspace/ue/StellarEra"
git rm -- Source/StellarEraPlanet/Src/Minerals/Surface/PlanetMineralSurface.h \
           Source/StellarEraPlanet/Src/Minerals/Surface/PlanetMineralSurface.cpp \
           Source/StellarEraPlanet/Src/Tests/PlanetMineralSurfaceTests.cpp
```

- [ ] **Step 3: 清掉观察体里的 MC 路径**

`PlanetMineralViewActor.h`：
- 删 `#include "PlanetMineralSurface.h"`
- 删 `class UDynamicMeshComponent;`
- 删 `bool Build(...)` 声明
- 删 `UDynamicMeshComponent* GetElementMesh(int32 Index) const;`
- 删 `TArray<TObjectPtr<UDynamicMeshComponent>> MineralMeshes;`

`PlanetMineralViewActor.cpp`：删 `Build(...)` 整个实现与 `GetElementMesh` 的实现；
把 `NumElements` / `IsElementVisible` / `SetElementVisible` / `SetAllElementsVisible`
里所有 `MineralMeshes` 分支删掉（体素路径变成唯一路径）；删掉
`Async/ParallelFor.h`、`Components/DynamicMeshComponent.h`、`DynamicMesh/DynamicMesh3.h`、
`DynamicMesh/MeshNormals.h` 四个 include。

- [ ] **Step 4: 清掉组件里的 MC 路径**

`PlanetMineralViewComponent.h`：
- 删 `bUseVoxelWorlds` 属性
- 删 `CellsPerFeature` 属性
- 删 `#include "PlanetMineralSurface.h"`
- 删 `CachedSurface` / `CachedKey` / `CachedSurfaceGeology` / `bHasCachedSurface` 四个成员

`PlanetMineralViewComponent.cpp`：
- 删 `PlanetMineralSurface::FBuildParams` / `FCacheKey` / `Build` 那一段（从
  `PlanetMineralSurface::FBuildParams Params;` 到 `bHasCachedSurface = true;`）
- 删 `Activate` 里 `if (bUseVoxelWorlds)` 的 `else` 分支，并去掉 `if` 外壳 ——
  体素路径变成唯一路径
- 删掉把三角形数、角点数打进日志的 `UE_LOG`（它们统计的是 MC 的产物）
- 前言区若有 `erased CellsPerFeature 缓存失效` 一类注释，一并删

- [ ] **Step 5: 清掉测试里的 MC 引用**

`PlanetMineralViewTests.cpp`：
- 删 `#include "PlanetMineralSurface.h"` 与 `#include "Components/DynamicMeshComponent.h"`
- 删辅助函数 `MakeMineralViewTriangleMesh`
- **删** `FPlanetMineralViewCellsPerFeatureTest`（`CellsPerFeature` 已删）
- **删** `FPlanetMineralViewActorBuildTest`、`FPlanetMineralViewEmptySlotTest`、
  `FPlanetMineralViewEmptyTest` —— 三条都建在 `Build(Meshes,...)` /
  `GetElementMesh` 上，那两个接口没了。用下面这**一条**替换它们：验的是
  「没建世界时接口安全」，因为建体素世界需要 BeginPlay、editor world 跑不出来
- `FPlanetMineralViewGeologyAssetMirrorTest`、`FPlanetMineralViewPresetEditMarksStaleTest`
  不动：它们只碰预设镜像与标脏，与几何路径无关

替换用的那一条：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorEmptyTest,
	"StellarEra.Planet.MineralView.ActorEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorEmptyTest::RunTest(const FString& Parameters)
{
	UWorld* World = FindMineralViewEditorWorld();
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	// 没建世界时接口必须安全：调用方是图例与数字键，不该让进程崩。
	TestEqual(TEXT("没建世界时元素数为 0"), View->NumElements(), 0);
	TestNull(TEXT("越界取世界返回 nullptr"), View->GetElementField(0));
	TestFalse(TEXT("越界显隐不生效"), View->IsElementVisible(0));
	View->SetElementVisible(0, false);
	View->SetAllElementsVisible(true);
	TestFalse(TEXT("越界改色返回 false"), View->SetElementColor(0, FLinearColor::Red));
	View->ReleaseAllFields(); // 重复调用安全

	View->Destroy();
	return true;
}
```

- [ ] **Step 6: 构建 + 跑全部矿物测试**

构建命令见 Global Constraints，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.MineralView" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTest.log"
```

Expected: 全绿。

- [ ] **Step 7: 再跑一次全量自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/AllTests.log"
```

Expected: 全绿。**任何一条红了都要修完再提交。**

- [ ] **Step 8: 提交**

```bash
git add -- Source/StellarEraPlanet/Src/Minerals/Surface \
             Source/StellarEraPlanet/Src/Minerals/View \
             Source/StellarEraPlanet/Src/Tests
git commit -m "refactor(矿物): 退役 marching cubes 路径

体素世界路径已在 UniverseMap 里验收通过，删掉 PlanetMineralSurface 与
观察体的动态网格构建，CellsPerFeature 与 bUseVoxelWorlds 一并删除。

Co-Authored-By: Claude Code <noreply@anthropic.com>" \
  -- Source/StellarEraPlanet/Src/Minerals/Surface \
     Source/StellarEraPlanet/Src/Minerals/View \
     Source/StellarEraPlanet/Src/Tests
```

---

## 验收记录

（Task 6 Step 5 填写，保持本小节在文件末尾。）
