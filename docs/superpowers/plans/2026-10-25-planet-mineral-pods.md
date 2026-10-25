# 行星矿物矿囊化 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把「一层一元素」的同心矿层换成 3D 随机矿囊，让观测视图与地形显示同一批矿，且矿物分布参数像地形模块一样在资产里可调。

**Architecture:** 地质预设从 `Content/Geology/*.json` 改成 `UPlanetGeologyPreset`（`UDataAsset`）；`FPlanetGeologyField` 建场时按元素撒 `N` 个球状矿囊、建一个格子索引，求值 = 矿囊径向衰减 × 噪声稀疏；地形（每体素）与观测视图（等值面）共用这套求值，于是两边必然看到同一批矿。观测视图从「整颗星的全局立方栅格」改成「按矿囊聚簇的局部栅格」，分辨率随矿囊大小而不是行星直径。

**Tech Stack:** UE 5.8.3 / C++ / VoxelFree（第三方，不改）/ `UE::Geometry::FMarchingCubes` / `FVoxelFastNoise` / Automation 简易测试。

**Spec:** `docs/superpowers/specs/2026-10-25-planet-mineral-pods-design.md`

## Global Constraints

- 模块依赖是一张 **无环有向图**（见 `CLAUDE.md`）。`StellarEraMinerals → StellarEraTerrain` 已存在，**Terrain 永远不得反向引用 Minerals**。`StellarEraGeology` 无项目依赖。
- 自定义代码只写进 `Source/StellarEra*` 十一个模块；`Plugins/VoxelFree`、`Plugins/VibeUE` 一个字都不改。
- 含中文的 `.h` / `.cpp` 必须存 **UTF-8 with BOM**。
- **不要统一换行符**：仓库本来就是 CRLF/LF 混合。
- `UVoxelGenerator` 子类上任何不能被 VoxelFree 参数系统识别的 `EditAnywhere` 属性都会在选中行星 Actor 时弹 ensure。预设指针属性必须 `VisibleAnywhere`（= Edit + EditConst）。
- 构建前**必须关闭编辑器**，且只能用捆绑 dotnet 直调 UBT（cwd 在项目外）：
  ```bash
  cd /f && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  不要用 `Build.bat`。
- 自动化测试（编辑器开着也能跑）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout -abslog="F:/tmp/automation.log"
  ```
  `Result: Failed` 也可能只是链接失败；先看日志里有没有 `error C`。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`。索引里有常驻暂存文件，裸 `git commit` 会吞掉全部。
- 提交信息末尾加 `Co-Authored-By: Claude Code <noreply@anthropic.com>`。
- 单位：矿囊一律用**归一化空间**（行星基准半径 = 1）；深度仍是体素、向内为正；观测视图顶点是相对球心的厘米。

---

## Task 1: 预设资产类型

新增 `UPlanetGeologyPreset` 与它的元素参数结构。**纯新增**，不改任何现有行为，编译后现有测试全绿。

**Files:**
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyTypes.h`
- Create: `Source/StellarEraGeology/Public/PlanetGeologyPreset.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp`
- Test: `Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp`

**Interfaces:**
- Consumes: 无。
- Produces:
  - `USTRUCT FPlanetGeologyAppearance { float ColorThreshold; float FadeRange; float PurityForFullColor; }`
  - `USTRUCT FPlanetGeologyElementParams`（字段见下）
  - `UCLASS UPlanetGeologyPreset : UDataAsset`，成员 `TArray<FPlanetGeologyElementParams> Elements`、`FPlanetGeologyAppearance Appearance`、`float GeometryAmplitude`、`int32 Revision`、`uint32 GetRevision() const`
  - `struct FPlanetGeologyPod { FVector Center; float Radius; }`
  - `struct FPlanetGeologyPodRef { int32 ElementIndex; int32 PodIndex; }`

- [ ] **Step 1: 写失败的测试**

创建 `Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp`（**含中文，存 UTF-8 with BOM**）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 地质预设资产的默认值与修订号。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetDefaultsTest,
	"StellarEra.Planet.Geology.PresetDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetDefaultsTest::RunTest(const FString& Parameters)
{
	const UPlanetGeologyPreset* Preset = GetDefault<UPlanetGeologyPreset>();
	TestNotNull(TEXT("默认对象存在"), Preset);
	if (!Preset)
	{
		return false;
	}

	TestEqual(TEXT("默认没有元素"), Preset->Elements.Num(), 0);
	TestEqual(TEXT("默认几何幅度 1.5"), Preset->GeometryAmplitude, 1.5f);
	TestEqual(TEXT("默认阈值 0.22"), Preset->Appearance.ColorThreshold, 0.22f);
	TestEqual(TEXT("默认淡出 0.15"), Preset->Appearance.FadeRange, 0.15f);
	TestEqual(TEXT("默认纯度 0.7"), Preset->Appearance.PurityForFullColor, 0.7f);

	const FPlanetGeologyElementParams* Defaults =
		GetDefault<FPlanetGeologyElementParams>();
	TestNotNull(TEXT("元素参数默认值存在"), Defaults);
	if (Defaults)
	{
		TestEqual(TEXT("默认 PodCount 16"), Defaults->PodCount, 16);
		TestEqual(TEXT("默认 PodRadius 0.05"), Defaults->PodRadius, 0.05f);
		TestEqual(TEXT("默认 PodRadiusJitter 0.5"), Defaults->PodRadiusJitter, 0.5f);
		TestEqual(TEXT("默认 PodDepthBias 0"), Defaults->PodDepthBias, 0.0f);
		TestEqual(TEXT("默认 ShapeDetail 3"), Defaults->ShapeDetail, 3.0f);
		TestEqual(TEXT("默认 Octaves 2"), Defaults->Octaves, 2);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetRevisionTest,
	"StellarEra.Planet.Geology.PresetRevision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetRevisionTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	const int32 Before = Preset->GetRevision();

	FPropertyChangedEvent Event(nullptr);
	Preset->PostEditChangeProperty(Event);

	TestTrue(*FString::Printf(TEXT("改属性后修订号自增（%d → %d）"),
		Before, Preset->GetRevision()), Preset->GetRevision() > Before);
	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认失败**

Run: 上面 Global Constraints 里的 UBT 命令。
Expected: 编译错误 `PlanetGeologyPreset.h: No such file or directory`。

- [ ] **Step 3: 在 `PlanetGeologyTypes.h` 加上反射头与两个 POD 类型**

在文件顶部的 `#include "CoreMinimal.h"` 之后加：

```cpp
#include "PlanetGeologyTypes.generated.h"
```

把现有的 `/** 行星级的外观参数（来自 Planets/<preset>.json 的 appearance 段）。 */ struct FPlanetGeologyAppearance {...};` 整块替换为：

```cpp
/** 行星级的外观参数。改 USTRUCT 是为了能在 UDataAsset 里编辑。 */
USTRUCT(BlueprintType)
struct STELLARERAGEOLOGY_API FPlanetGeologyAppearance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ColorThreshold = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeRange = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Appearance",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PurityForFullColor = 0.7f;
};
```

在文件末尾追加：

```cpp
/** 一个矿囊：归一化空间的球（行星基准半径 = 1）。 */
struct FPlanetGeologyPod
{
	FVector Center = FVector::ZeroVector;
	float Radius = 0.f;
};

/** 空间索引里的一条引用：哪个元素的第几个矿囊。 */
struct FPlanetGeologyPodRef
{
	int32 ElementIndex = INDEX_NONE;
	int32 PodIndex = INDEX_NONE;
};
```

- [ ] **Step 4: 创建 `PlanetGeologyPreset.h`**

UTF-8 with BOM。

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质预设资产：一颗行星的元素表 + 外观 + 几何参数。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlanetGeologyTypes.h"
#include "PlanetGeologyPreset.generated.h"

/**
 * 一种矿的分布参数。
 *
 * 全部是「这颗行星上这种矿长什么样」的旋钮，与地形模块的地位相同：资产里调，
 * 改完重建行星即可看到。距离一律是归一化空间的半径比例，换行星大小不改变相对尺寸。
 */
USTRUCT(BlueprintType)
struct STELLARERAGEOLOGY_API FPlanetGeologyElementParams
{
	GENERATED_BODY()

	/** 稳定标识，只用于日志、随机种子派生。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FString DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element")
	FLinearColor Color = FLinearColor::White;

	/**
	 * 只影响颜色混合权重与图例占比，**不改几何**。
	 * 想改矿囊密度请调 PodCount / PodRadius。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Element",
		meta = (ClampMin = "0.0"))
	float Abundance = 1.0f;

	/** 撒多少个矿囊。0 = 这个元素在这颗行星上不存在。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0", ClampMax = "512"))
	int32 PodCount = 16;

	/**
	 * 矿囊名义半径（占行星基准半径的比例）。
	 *
	 * 重叠对的数量按 PodCount² × (2·r)³ 增长：半径砍半，重叠少 8 倍。
	 * 想看不同元素互相穿插，就把这个值调大，别只加 PodCount。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0.001", ClampMax = "0.25"))
	float PodRadius = 0.05f;

	/** 每囊半径的随机抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodRadiusJitter = 0.5f;

	/**
	 * 深度偏好强度。**0 = 全深度体积均匀**（默认，也是「不要分层」的语义）。
	 * 大于 0 时按 PodDepthCenter/Spread 做带权拒绝采样，越深越偏。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PodDepthBias = 0.0f;

	/** 偏好深度（占半径比例，0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodDepthCenter = 0.25f;

	/** 偏好深度的高斯宽度（占半径比例）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods|Depth",
		meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PodDepthSpread = 0.15f;

	/** 一个矿囊直径内大约几个噪声周期。越大概括破碎。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.5"))
	float ShapeDetail = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "1", ClampMax = "6"))
	int32 Octaves = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "1.0"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Gain = 0.5f;

	/** 0 时由 Hash(Id) 派生，保证不同元素默认不相关。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	int32 Seed = 0;

	/** 噪声稀疏阈值，越高矿囊越破碎。0 = 不稀疏。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape",
		meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float VeinThreshold = 0.5f;
};

/**
 * 一颗行星的地质预设。**唯一数据源**：JSON 预设已退役。
 *
 * 元素数组的顺序 = 运行期采样顺序 = 图例顺序 = 矿体组件下标，四者共用。
 */
UCLASS(BlueprintType)
class STELLARERAGEOLOGY_API UPlanetGeologyPreset : public UDataAsset
{
	GENERATED_BODY()

public:
	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface

	/** 每次编辑自增。缓存键靠它判断「同一份资产的内容变了」。 */
	uint32 GetRevision() const { return Revision; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology")
	TArray<FPlanetGeologyElementParams> Elements;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology")
	FPlanetGeologyAppearance Appearance;

	/** 矿囊把地表顶起的最大幅度（体素）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geology",
		meta = (ClampMin = "0.0"))
	float GeometryAmplitude = 1.5f;

	/**
	 * 内容修订号。非 0 起，每次 PostEditChangeProperty 自增。
	 *
	 * 只做缓存失效用，不是数据：场景里改资产而不保存，读到的也是新内容。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Geology")
	int32 Revision = 1;
};
```

- [ ] **Step 5: 创建 `PlanetGeologyPreset.cpp`**

UTF-8 with BOM。

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyPreset.h"

#if WITH_EDITOR
void UPlanetGeologyPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	++Revision;
}
#endif
```

- [ ] **Step 6: 构建**

Run: UBT 命令。
Expected: 成功（`Result: Succeeded`）。若报 `PlanetGeologyTypes.generated.h` 找不到，确认第一行 `#include "PlanetGeologyTypes.generated.h"` 是**最后一个** include。

- [ ] **Step 7: 跑测试**

Run: 自动化测试命令。
Expected: `StellarEra.Planet.Geology.PresetDefaults` 与 `PresetRevision` 通过，其余既有测试全绿。测试总数应为 `0 failed`。

- [ ] **Step 8: 提交**

```bash
git commit -m "feat(地质): 新增 UPlanetGeologyPreset 资产类型

矿囊化改造的第一步：只加类型，不改行为。
元素参数全部可在资产里调，Abundance 明确只影响颜色。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraGeology/Public/PlanetGeologyTypes.h \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp
```

---

## Task 2: 地质场支持矿囊（双路并存）

给 `FPlanetGeologyField` 加 `BuildFromPreset`、矿囊播撒、空间索引、新求值。**保留** JSON/矿层路径：`FPlanetGeologyElement::Pods` 为空的元素仍走旧的高斯矿层分支，所以现有测试必须继续全绿。这一步只做加法，风险最高的数学部分单独可审。

**Files:**
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyTypes.h`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyField.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyField.cpp`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp:41,68-71`
- Test: `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp`（新建）

**Interfaces:**
- Consumes: `UPlanetGeologyPreset`、`FPlanetGeologyElementParams`、`FPlanetGeologyPod`、`FPlanetGeologyPodRef`（Task 1）。
- Produces:
  - `static TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::BuildFromPreset(const UPlanetGeologyPreset& Preset, double PlanetRadiusVoxels)`
  - `float FPlanetGeologyField::SampleElementNormAtNormalized(int32 ElementIndex, const FVector& NormalizedPoint) const`
  - `float FPlanetGeologyField::SampleElementNormBruteForce(int32 ElementIndex, const FVector& NormalizedPoint) const`（测试专用）
  - `const TArray<FPlanetGeologyPod>& FPlanetGeologyField::GetPods(int32 ElementIndex) const`

- [ ] **Step 1: 写失败的测试**

创建 `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp`（UTF-8 with BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿囊模型的自动化测试：覆盖、重叠、索引一致性、丰度不变性、形状。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyPodTest
{
	constexpr double PlanetRadiusVoxels = 40000.0;

	FPlanetGeologyElementParams MakeElement(FName Id, int32 PodCount, float PodRadius,
		int32 Seed, float VeinThreshold = 0.0f)
	{
		FPlanetGeologyElementParams Params;
		Params.Id = Id;
		Params.DisplayName = Id.ToString();
		Params.PodCount = PodCount;
		Params.PodRadius = PodRadius;
		Params.Seed = Seed;
		Params.VeinThreshold = VeinThreshold;
		return Params;
	}

	UPlanetGeologyPreset* MakePreset(const TArray<FPlanetGeologyElementParams>& InElements)
	{
		UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
		Preset->Elements = InElements;
		return Preset;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(Preset, PlanetRadiusVoxels);
	}

	/** 球内均匀随机点（归一化）。 */
	FVector RandomPoint(FRandomStream& Rng)
	{
		for (int32 Attempt = 0; Attempt < 64; ++Attempt)
		{
			const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			if (P.SizeSquared() <= 1.f)
			{
				return P;
			}
		}
		return FVector::ZeroVector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodCoverageTest,
	"StellarEra.Planet.Geology.PodCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodCoverageTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	// 60 囊 / r=0.08：相交与露头都够多，固定种子下不会偶发为 0。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("iron"), 60, 0.08f, 101),
		MakeElement(TEXT("copper"), 60, 0.08f, 202)});
	const auto Field = MakeField(*Preset);
	if (!TestTrue(TEXT("场非空"), Field.IsValid() && !Field->IsEmpty()))
	{
		return false;
	}

	// 1) 矿囊中心半径必须铺满整个球体 —— 这是「不分层」的直接证据。
	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(ElementIndex);
		TestEqual(*FString::Printf(TEXT("元素 %d 矿囊数"), ElementIndex), Pods.Num(), 60);

		float MinRadius = 2.f;
		float MaxRadius = -1.f;
		bool bOutcrop = false;
		for (const FPlanetGeologyPod& Pod : Pods)
		{
			MinRadius = FMath::Min(MinRadius, Pod.Center.Size());
			MaxRadius = FMath::Max(MaxRadius, Pod.Center.Size());
			// 探出基准球面 = 地形上能被看到。没这一条，观测视图再漂亮地形也是纯色。
			bOutcrop |= Pod.Center.Size() + Pod.Radius > 1.f;
		}
		TestTrue(*FString::Printf(TEXT("元素 %d 有矿囊贴近地表（最近 %.3f）"),
			ElementIndex, MinRadius), MinRadius < 0.5f);
		TestTrue(*FString::Printf(TEXT("元素 %d 有矿囊接近球心（最远 %.3f）"),
			ElementIndex, MaxRadius), MaxRadius > 0.5f);
		TestTrue(*FString::Printf(TEXT("元素 %d 至少一个矿囊探出球面（露头）"), ElementIndex),
			bOutcrop);
	}

	// 2) 采样点的深度覆盖：正样本必须散落在过半深度区间里。
	const int32 DepthBinCount = 20;
	TArray<bool> BinOccupied;
	BinOccupied.Init(false, DepthBinCount * 2);

	FRandomStream Rng(12345);
	const int32 SampleCount = 100000;
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		const double RadiusRatio = double(P.Size());
		const int32 Bin = FMath::Clamp(
			int32((1.0 - RadiusRatio) * DepthBinCount), 0, DepthBinCount - 1);
		for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
		{
			if (Field->SampleElementNormAtNormalized(ElementIndex, P) > 0.05f)
			{
				BinOccupied[ElementIndex * DepthBinCount + Bin] = true;
			}
		}
	}

	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		int32 OccupiedBins = 0;
		for (int32 Bin = 0; Bin < DepthBinCount; ++Bin)
		{
			OccupiedBins += BinOccupied[ElementIndex * DepthBinCount + Bin] ? 1 : 0;
		}
		TestTrue(*FString::Printf(TEXT("元素 %d 覆盖 %d/%d 个深度区间（需 >= 12）"),
			ElementIndex, OccupiedBins, DepthBinCount), OccupiedBins >= 12);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodOverlapTest,
	"StellarEra.Planet.Geology.PodOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodOverlapTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	// 故意用更大的矿囊：相交概率 ∝ (r_a + r_b)³，默认参数下只有个位数的对。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 60, 0.09f, 11),
		MakeElement(TEXT("b"), 60, 0.09f, 22)});
	const auto Field = MakeField(*Preset);

	int32 OverlappingPairs = 0;
	for (const FPlanetGeologyPod& PodA : Field->GetPods(0))
	{
		for (const FPlanetGeologyPod& PodB : Field->GetPods(1))
		{
			if (FVector::Dist(PodA.Center, PodB.Center) < PodA.Radius + PodB.Radius)
			{
				++OverlappingPairs;
			}
		}
	}

	TestTrue(*FString::Printf(TEXT("至少一对不同元素的矿囊相交（实测 %d 对）"), OverlappingPairs),
		OverlappingPairs > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologySpatialIndexTest,
	"StellarEra.Planet.Geology.SpatialIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologySpatialIndexTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 30, 0.06f, 7, 0.3f),
		MakeElement(TEXT("b"), 30, 0.04f, 8, 0.3f),
		MakeElement(TEXT("c"), 10, 0.02f, 9, 0.3f)});
	const auto Field = MakeField(*Preset);

	FRandomStream Rng(98765);
	int32 Mismatches = 0;
	for (int32 SampleIndex = 0; SampleIndex < 2000; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		for (int32 ElementIndex = 0; ElementIndex < 3; ++ElementIndex)
		{
			const float Indexed = Field->SampleElementNormAtNormalized(ElementIndex, P);
			const float Brute = Field->SampleElementNormBruteForce(ElementIndex, P);
			if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-6f))
			{
				++Mismatches;
			}
		}
	}

	TestEqual(TEXT("索引查询与暴力遍历逐点一致"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyAbundanceInvarianceTest,
	"StellarEra.Planet.Geology.AbundanceGeometryInvariance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyAbundanceInvarianceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	FPlanetGeologyElementParams Low = MakeElement(TEXT("a"), 20, 0.05f, 31, 0.2f);
	Low.Abundance = 0.2f;
	FPlanetGeologyElementParams High = Low;
	High.Abundance = 1.0f;

	UPlanetGeologyPreset* PresetLow = MakePreset({Low});
	UPlanetGeologyPreset* PresetHigh = MakePreset({High});
	const auto FieldLow = MakeField(*PresetLow);
	const auto FieldHigh = MakeField(*PresetHigh);

	FRandomStream Rng(4242);
	int32 Mismatches = 0;
	for (int32 SampleIndex = 0; SampleIndex < 1000; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		if (!FMath::IsNearlyEqual(FieldLow->SampleElementNormAtNormalized(0, P),
			FieldHigh->SampleElementNormAtNormalized(0, P), 1e-6f))
		{
			++Mismatches;
		}
	}

	TestEqual(TEXT("只改 Abundance 不改逐点几何"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPodShapeTest,
	"StellarEra.Planet.Geology.PodShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPodShapeTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	FPlanetGeologyElementParams Params = MakeElement(TEXT("a"), 8, 0.06f, 55, 0.0f);
	Params.ShapeDetail = 3.0f;
	UPlanetGeologyPreset* Preset = MakePreset({Params});
	const auto Field = MakeField(*Preset);

	const TArray<FPlanetGeologyPod>& Pods = Field->GetPods(0);
	if (!TestEqual(TEXT("矿囊数"), Pods.Num(), 8))
	{
		return false;
	}

	int32 InsideSolid = 0;
	int32 InsideVeined = 0;
	FRandomStream Rng(777);
	for (const FPlanetGeologyPod& Pod : Pods)
	{
		// 球外 5% 处场值必须精确为 0：这是闭网格的前提。
		const FVector Outside = Pod.Center + FVector(Pod.Radius * 1.05f, 0.f, 0.f);
		TestEqual(TEXT("矿囊外场值为 0"), Field->SampleElementNormAtNormalized(0, Outside), 0.f);

		// 囊内既不是实心球，也不是全空：噪声必须把形状打碎。
		for (int32 SampleIndex = 0; SampleIndex < 200; ++SampleIndex)
		{
			const FVector Offset(
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius,
				Rng.FRandRange(-1.f, 1.f) * Pod.Radius);
			if (Offset.SizeSquared() > Pod.Radius * Pod.Radius)
			{
				continue;
			}
			++InsideSolid;
			if (Field->SampleElementNormAtNormalized(0, Pod.Center + Offset) > 0.3f)
			{
				++InsideVeined;
			}
		}
	}

	const double Ratio = InsideSolid > 0 ? double(InsideVeined) / double(InsideSolid) : 0.0;
	TestTrue(*FString::Printf(TEXT("囊内高值点占比 %.2f 应在 (0.05, 0.95)"), Ratio),
		Ratio > 0.05 && Ratio < 0.95);
	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认失败**

Run: UBT 命令。
Expected: 编译错误 `BuildFromPreset` / `SampleElementNormAtNormalized` / `GetPods` / `SampleElementNormBruteForce` 不是成员。

- [ ] **Step 3: `PlanetGeologyTypes.h`：元素结构加矿囊字段**

把 `struct FPlanetGeologyElement` 里 `// --- 噪声参数（来自元素表） ---` 这一段（`Frequency` / `Octaves` / `Lacunarity` / `Gain` / `Seed` / `DepthScale`）替换为：

```cpp
	// --- 矿囊（预设路径；JSON 路径下为空，走 Bands 分支） ---
	TArray<FPlanetGeologyPod> Pods;
	/** 名义半径（归一化）。噪声坐标的缩放基准，不随单囊抖动。 */
	float PodRadius = 0.05f;
	/** 一个矿囊直径内大约几个噪声周期。 */
	float ShapeDetail = 3.f;

	// --- 噪声参数 ---
	int32 Octaves = 2;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	int32 Seed = 0;
	/** 旧矿层路径专用：噪声频率与深度缩放。**Task 2 必须留着**，Task 3 与 Bands 一起删。 */
	float Frequency = 3.f;
	float DepthScale = 0.3f;
```

（`Bands` / `ReachDepth` / `Frequency` / `DepthScale` 全部保留到 Task 3 —— 旧矿层
`else` 分支还在用 `Frequency` / `DepthScale`，提前删 Task 2 就编译不过。）

- [ ] **Step 4: `PlanetGeologyField.h`：公开 API 与私有成员**

在 `#include "PlanetGeologyTypes.h"` 之后加 `class UPlanetGeologyPreset;`。

在 `public:` 段、`LoadFromString` 之前插入：

```cpp
	/**
	 * 从预设资产生成场。**必须在游戏线程调用**（要读 UObject），构造函数用
	 * check(IsInGameThread()) 兜住。建完即不可变，可跨线程共享。
	 *
	 * PlanetRadiusVoxels 只用来把外部签名 (UnitDir, Depth) 换算成归一化坐标；
	 * 矿囊本身存的是归一化空间的位置，与行星大小无关。
	 */
	static TSharedPtr<const FPlanetGeologyField> BuildFromPreset(
		const UPlanetGeologyPreset& Preset, double PlanetRadiusVoxels);
```

在 `SampleElementNorms` 声明之后插入：

```cpp
	/**
	 * 单元素在**归一化空间**某点的强度（∈ [0,1]）。球外恒 0。
	 *
	 * 这是矿囊模型的原生入口：观测视图的角点本来就在归一化/厘米空间里，
	 * 不必绕回 (方向, 深度)。
	 */
	float SampleElementNormAtNormalized(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/**
	 * 与 SampleElementNormAtNormalized 同一公式，但**绕开空间索引**遍历该元素全部矿囊。
	 * 只给测试用：索引漏囊不会报错，只表现为「矿看起来少了」。
	 */
	float SampleElementNormBruteForce(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 该元素的矿囊表（归一化空间）。越界返回空数组。 */
	const TArray<FPlanetGeologyPod>& GetPods(int32 ElementIndex) const;
```

在 `private:` 段的 `FElementRuntime` 之后插入：

```cpp
	/** 单元素求值：矿囊径向衰减 × 噪声稀疏。P 为归一化坐标。 */
	float EvaluateElementNorm(int32 ElementIndex, const FVector& NormalizedPoint) const;

	/** 建 PodIndex。BuildFromPreset 末尾调用一次。 */
	void BuildPodIndex();

	/**
	 * 矿囊格索引：格边 = 全局最大矿囊半径。
	 *
	 * 每囊插入其包围盒覆盖的全部格子（≤ 8 格）。查询只查 P 所在格就够：
	 * 任何包含 P 的矿囊，其包围盒必覆盖 P 所在格。
	 */
	TMap<FIntVector, TArray<FPlanetGeologyPodRef>> PodIndex;
	float PodIndexCellSize = 0.f;

	/** 归一化换算用的行星半径（体素）。矿层路径与外部签名换算用。 */
	double RadiusVoxels = 0.0;
```

- [ ] **Step 5: `PlanetGeologyField.cpp`：播撒、索引、求值、建场**

文件顶部加 include：

```cpp
#include "PlanetGeologyPreset.h"
#include "Math/RandomStream.h"
```

在匿名 `namespace {` 里（`IsValidPresetName` 之后）加：

```cpp
	FIntVector ToPodCell(const FVector& Point, float CellSize)
	{
		return FIntVector(
			FMath::FloorToInt(Point.X / CellSize),
			FMath::FloorToInt(Point.Y / CellSize),
			FMath::FloorToInt(Point.Z / CellSize));
	}

	/** 球内线性衰减：中心 1，表面 0。 */
	float PodInfluence(const FPlanetGeologyPod& Pod, const FVector& Point)
	{
		const float RadiusSquared = Pod.Radius * Pod.Radius;
		if (RadiusSquared <= 0.f)
		{
			return 0.f;
		}
		const float DistanceSquared = (Point - Pod.Center).SizeSquared();
		if (DistanceSquared >= RadiusSquared)
		{
			return 0.f;
		}
		return 1.f - DistanceSquared / RadiusSquared;
	}

	void PlacePods(FPlanetGeologyElement& Definition, const FPlanetGeologyElementParams& Params)
	{
		const int32 PodCount = FMath::Max(0, Params.PodCount);
		Definition.Pods.Reset();
		Definition.Pods.Reserve(PodCount);
		if (PodCount == 0)
		{
			return;
		}

		FRandomStream Rng(Definition.Seed);
		const double Jitter = FMath::Clamp(double(Params.PodRadiusJitter), 0.0, 1.0);
		const double DepthBias = FMath::Clamp(double(Params.PodDepthBias), 0.0, 0.9);
		const double DepthCenter = FMath::Clamp(double(Params.PodDepthCenter), 0.0, 1.0);
		const double DepthSpread = FMath::Max(double(Params.PodDepthSpread), 0.01);

		for (int32 PodIndex = 0; PodIndex < PodCount; ++PodIndex)
		{
			const double RadiusJitter = 1.0 + Jitter * (2.0 * double(Rng.FRand()) - 1.0);
			const double Radius = FMath::Clamp(
				double(Definition.PodRadius) * RadiusJitter, 1e-3, 0.25);

			// 球面均匀方向；径向距离另抽，两者独立。
			const FVector Direction = Rng.GetUnitVector();

			// ∛U 让**体积**均匀（近地表体积大，矿囊自然更多）。r ∈ [0,1] 是「球心距比例」。
			double RadiusRatio = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);

			// 作者参数里的 DepthCenter 是「深度」（0 = 地表，1 = 球心），与半径比例
			// 互为补角 —— 带权时必须先换算成 1 - r 再比，否则偏好会朝反方向偏。
			if (DepthBias > 0.0)
			{
				// 带权拒绝采样：权重在 DepthCenter 附近最高，最多试 64 次。
				for (int32 Attempt = 0; Attempt < 64; ++Attempt)
				{
					const double CandidateRadius = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);
					const double CandidateDepth = 1.0 - CandidateRadius;
					const double Offset = (CandidateDepth - DepthCenter) / DepthSpread;
					const double Weight =
						(1.0 - DepthBias) + DepthBias * FMath::Exp(-Offset * Offset);
					RadiusRatio = CandidateRadius;
					if (double(Rng.FRand()) < Weight)
					{
						break;
					}
				}
			}

			FPlanetGeologyPod& Pod = Definition.Pods.AddDefaulted_GetRef();
			Pod.Radius = float(Radius);
			// **不要乘 (1 - r)**：乘了矿囊就永远够不到半径 1，地形在深度 0 采样时一个矿
			// 都看不到（露头恒为 0，验收 3 必挂）。探出球面的部分场值恒 0，观测视图里
			// 顶点会按 |v| <= R 径向夹回，两边都闭合。
			Pod.Center = Direction * float(RadiusRatio);
		}
	}
```

在 `LoadFromString` 之前插入建场函数：

```cpp
TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::BuildFromPreset(
	const UPlanetGeologyPreset& Preset, const double PlanetRadiusVoxels)
{
	check(IsInGameThread());

	TSharedPtr<FPlanetGeologyField> Field = MakeShared<FPlanetGeologyField>();
	Field->RadiusVoxels = FMath::Max(PlanetRadiusVoxels, 0.0);
	Field->Appearance = Preset.Appearance;
	Field->GeometryAmplitude = FMath::Max(0.f, Preset.GeometryAmplitude);

	Field->Elements.Reserve(Preset.Elements.Num());
	for (const FPlanetGeologyElementParams& Params : Preset.Elements)
	{
		FPlanetGeologyElement Definition;
		Definition.Id = Params.Id.ToString();
		Definition.DisplayName = Params.DisplayName.IsEmpty()
			? Params.Id.ToString()
			: Params.DisplayName;
		Definition.Color = Params.Color;
		Definition.Abundance = FMath::Max(0.f, Params.Abundance);
		Definition.Octaves = FMath::Clamp(Params.Octaves, 1, 6);
		Definition.Lacunarity = FMath::Max(Params.Lacunarity, 1.f);
		Definition.Gain = FMath::Clamp(Params.Gain, 0.f, 1.f);
		// Seed 0 = 按 Id 派生：不同元素默认互不相关，且跨进程稳定。
		Definition.Seed = Params.Seed != 0
			? Params.Seed
			: int32(GetTypeHash(Params.Id) & 0x7fffffffu);
		Definition.PodRadius = FMath::Clamp(Params.PodRadius, 0.001f, 0.25f);
		Definition.ShapeDetail = FMath::Max(Params.ShapeDetail, 0.5f);
		Definition.VeinThreshold = FMath::Clamp(Params.VeinThreshold, 0.f, 0.95f);

		PlacePods(Definition, Params);

		FElementRuntime& Runtime = Field->Elements.AddDefaulted_GetRef();
		Runtime.Definition = Definition;
		Runtime.Noise.SetSeed(Definition.Seed);
		Runtime.Noise.SetFractalLacunarity(Definition.Lacunarity);
		// FractalBounding 只算一次；octaves / gain 必须与采样时传进
		// GetPerlinFractal_3D 的值完全一致，否则归一化系数会错位。
		Runtime.Noise.SetFractalOctavesAndGain(Definition.Octaves, Definition.Gain);

		Field->TotalAbundance += Definition.Abundance;
	}

	Field->BuildPodIndex();
	return Field;
}

void FPlanetGeologyField::BuildPodIndex()
{
	PodIndex.Reset();
	PodIndexCellSize = 0.f;

	float MaxRadius = 0.f;
	for (const FElementRuntime& Runtime : Elements)
	{
		for (const FPlanetGeologyPod& Pod : Runtime.Definition.Pods)
		{
			MaxRadius = FMath::Max(MaxRadius, Pod.Radius);
		}
	}
	if (MaxRadius <= 0.f)
	{
		return;
	}

	PodIndexCellSize = MaxRadius;
	for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
	{
		const TArray<FPlanetGeologyPod>& Pods = Elements[ElementIndex].Definition.Pods;
		for (int32 PodIndex = 0; PodIndex < Pods.Num(); ++PodIndex)
		{
			const FPlanetGeologyPod& Pod = Pods[PodIndex];
			const FIntVector MinCell = ToPodCell(Pod.Center - FVector(Pod.Radius), PodIndexCellSize);
			const FIntVector MaxCell = ToPodCell(Pod.Center + FVector(Pod.Radius), PodIndexCellSize);
			for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
			{
				for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
				{
					for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
					{
						FPlanetGeologyPodRef Ref;
						Ref.ElementIndex = ElementIndex;
						Ref.PodIndex = PodIndex;
						PodIndex.FindOrAdd(FIntVector(X, Y, Z)).Add(Ref);
					}
				}
			}
		}
	}
}

float FPlanetGeologyField::EvaluateElementNorm(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	const FElementRuntime& Runtime = Elements[ElementIndex];
	const FPlanetGeologyElement& Definition = Runtime.Definition;
	if (Definition.Pods.Num() == 0 || PodIndexCellSize <= 0.f)
	{
		return 0.f;
	}

	// 1) 空间索引取候选矿囊，多囊取 max。
	float PodFactor = 0.f;
	const FIntVector Cell = ToPodCell(NormalizedPoint, PodIndexCellSize);
	if (const TArray<FPlanetGeologyPodRef>* Refs = PodIndex.Find(Cell))
	{
		for (const FPlanetGeologyPodRef& Ref : *Refs)
		{
			if (Ref.ElementIndex != ElementIndex)
			{
				continue;
			}
			PodFactor = FMath::Max(PodFactor,
				PodInfluence(Definition.Pods[Ref.PodIndex], NormalizedPoint));
			if (PodFactor >= 1.f)
			{
				break;
			}
		}
	}
	if (PodFactor <= 0.f)
	{
		return 0.f; // 不在任何矿囊里：一次 Perlin 都不求
	}

	// 2) 形状噪声。频率折叠进坐标（ShapeDetail 是「每囊直径的周期数」，
	//    直径 = 2r），噪声 API 的频率参数固定传 1。
	const double NoiseScale =
		2.0 * double(Definition.ShapeDetail) / FMath::Max(double(Definition.PodRadius), 1e-4);
	const double SampleX = double(NormalizedPoint.X) * NoiseScale;
	const double SampleY = double(NormalizedPoint.Y) * NoiseScale;
	const double SampleZ = double(NormalizedPoint.Z) * NoiseScale;

	const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
		v_flt(SampleX), v_flt(SampleY), v_flt(SampleZ), v_flt(1.f), Definition.Octaves));

	// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
	const double Noise01 = 0.5 * (RawNoise + 1.0);

	// 矿脉门：阈值以下完全为 0，阈值以上线性升到 1。
	const double Threshold = FMath::Clamp(double(Definition.VeinThreshold), 0.0, 0.95);
	const double Vein = Threshold > 0.0
		? FMath::Clamp((Noise01 - Threshold) / (1.0 - Threshold), 0.0, 1.0)
		: Noise01;

	return float(double(PodFactor) * Vein);
}

float FPlanetGeologyField::SampleElementNormAtNormalized(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	if (NormalizedPoint.SizeSquared() > 1.f + 1e-4f)
	{
		return 0.f; // 球外
	}
	return EvaluateElementNorm(ElementIndex, NormalizedPoint);
}

float FPlanetGeologyField::SampleElementNormBruteForce(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return 0.f;
	}
	const FElementRuntime& Runtime = Elements[ElementIndex];
	const FPlanetGeologyElement& Definition = Runtime.Definition;

	float PodFactor = 0.f;
	for (const FPlanetGeologyPod& Pod : Definition.Pods)
	{
		PodFactor = FMath::Max(PodFactor, PodInfluence(Pod, NormalizedPoint));
	}
	if (PodFactor <= 0.f)
	{
		return 0.f;
	}

	const double NoiseScale =
		2.0 * double(Definition.ShapeDetail) / FMath::Max(double(Definition.PodRadius), 1e-4);
	const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
		v_flt(double(NormalizedPoint.X) * NoiseScale),
		v_flt(double(NormalizedPoint.Y) * NoiseScale),
		v_flt(double(NormalizedPoint.Z) * NoiseScale),
		v_flt(1.f), Definition.Octaves));
	const double Noise01 = 0.5 * (RawNoise + 1.0);
	const double Threshold = FMath::Clamp(double(Definition.VeinThreshold), 0.0, 0.95);
	const double Vein = Threshold > 0.0
		? FMath::Clamp((Noise01 - Threshold) / (1.0 - Threshold), 0.0, 1.0)
		: Noise01;

	return float(double(PodFactor) * Vein);
}

const TArray<FPlanetGeologyPod>& FPlanetGeologyField::GetPods(const int32 ElementIndex) const
{
	static const TArray<FPlanetGeologyPod> Empty;
	return Elements.IsValidIndex(ElementIndex)
		? Elements[ElementIndex].Definition.Pods
		: Empty;
}
```

- [ ] **Step 6: `Accumulate` 分叉：有矿囊走矿囊，没有才走旧矿层**

在 `Accumulate` 开头算出归一化点：

```cpp
	OutAccumulation = FAccumulation();
	OutWeightedColor = FLinearColor::Black;

	// 矿囊用归一化坐标；JSON 矿层路径不读它，半径 0 时退化成方向。
	const FVector NormalizedPoint = RadiusVoxels > 0.0
		? UnitDir * float(1.0 - Depth / RadiusVoxels)
		: UnitDir;
```

把 `for (int32 ElementIndex ...)` 循环体改成：

```cpp
		const FElementRuntime& Runtime = Elements[ElementIndex];
		const FPlanetGeologyElement& Definition = Runtime.Definition;

		double ElementRaw = 0.0;
		double ElementNorm = 0.0;

		if (Definition.Pods.Num() > 0)
		{
			// --- 矿囊路径 ---
			ElementNorm = double(EvaluateElementNorm(ElementIndex, NormalizedPoint));
			ElementRaw = double(Definition.Abundance) * ElementNorm;
		}
		else
		{
			// --- 旧矿层路径（JSON 预设；Task 3 删除）---
			for (const FPlanetGeologyBand& Band : Definition.Bands)
			{
				// ……原样保留现有循环体……
			}
		}
```

旧循环体逐字照抄现有实现，不要改任何数字。

- [ ] **Step 7: `LoadFromString` 记下半径**

在 `Field->Appearance = Appearance;` 之前加一行：

```cpp
	Field->RadiusVoxels = FMath::Max(PlanetRadiusVoxels, 0.0);
```

- [ ] **Step 8: `PlanetMineralSurface.cpp` 去掉深度早退**

矿囊可以贴到球心，`GetMaxReachDepth()` 在矿囊模型下恒等于行星半径，早退失去意义且会误导。删除第 41 行 `const double MaxDepth = double(Field.GetMaxReachDepth());`，并把

```cpp
		if (Depth < 0.0 || Depth > MaxDepth)
		{
			return; // 高斯尾部之外没有任何元素有贡献
		}
```

替换为

```cpp
		if (Depth < 0.0)
		{
			return; // 球外
		}
```

- [ ] **Step 9: 构建**

Run: UBT 命令。
Expected: 成功。

- [ ] **Step 10: 跑测试**

Run: 自动化测试命令。
Expected: 5 个新测试通过，`StellarEra.Planet.Geology.*` 里旧的 JSON/矿层测试也全部通过（双路并存，谁都没被打破）。

- [ ] **Step 11: 提交**

```bash
git commit -m "feat(地质): 地质场支持 3D 矿囊

BuildFromPreset 撒矿囊、建空间索引、按囊求值；JSON 矿层路径暂时保留，
Pod 为空的元素仍走旧分支，所以现有测试不受影响。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraGeology/Public/PlanetGeologyTypes.h \
  Source/StellarEraGeology/Public/PlanetGeologyField.h \
  Source/StellarEraGeology/Private/PlanetGeologyField.cpp \
  Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp
```

---

## Task 3: 退役 JSON 路径，切换到预设资产

删掉 JSON 装载器、矿层模型、旧测试；把行星 Actor 与生成器切到 `UPlanetGeologyPreset`；把保留的测试改写成预设夹具。

**Files:**
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyTypes.h`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyField.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyField.cpp`
- Modify: `Source/StellarEraGeology/StellarEraGeology.Build.cs`
- Modify: `Source/StellarEraTerrain/Public/VoxelPlanetGenerator.h`
- Modify: `Source/StellarEraTerrain/Private/VoxelPlanetGenerator.cpp`
- Modify: `Source/StellarEraTerrain/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEraTerrain/Private/VoxelPlanetActor.cpp`
- Delete: `Source/StellarEraTerrain/Private/PlanetGeologyFieldTests.cpp`
- Delete: `Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp`（Task 4 重写）
- Delete: `Content/Geology/Elements.json`、`Content/Geology/Planets/earth.json`
- Test: `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp`（追加保留测试）

**Interfaces:**
- Consumes: Task 2 的 `BuildFromPreset` / `GetPods` / `SampleElementNormAtNormalized`。
- Produces:
  - `UVoxelPlanetGenerator::GeologyPreset`：`TObjectPtr<UPlanetGeologyPreset>`（`VisibleAnywhere`）
  - `AVoxelPlanetActor::GeologyAsset`：`TObjectPtr<UPlanetGeologyPreset>`（`EditAnywhere`）
  - `AVoxelPlanetActor::GetGeologyPresetAsset() const`
  - `AVoxelPlanetActor::MakeLegacyGeologyAssetPath(const FString&)`

- [ ] **Step 1: 删掉旧测试**

```bash
rm Source/StellarEraTerrain/Private/PlanetGeologyFieldTests.cpp
rm Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp
```

- [ ] **Step 2: 构建，确认只剩「旧 API 被引用」的错**

Run: UBT 命令。
Expected: 报 `LoadFromFiles` / `ListPresetNames` / `GetMaxReachDepth` / `GetElementDefinitions` 未定义等。这就是待删清单。

- [ ] **Step 3: 删地质场的 JSON 与矿层 API**

`PlanetGeologyField.h` 删除：`LoadFromFiles`、`LoadFromString`、`ListPresetNames`、`GetMaxReachDepth`、`GetElementDefinitions`，以及私有成员 `ElementOrder`。`GetSampledElementDefinitions` 保留（注释改成「现在就是 Elements 顺序」）。

`PlanetGeologyField.cpp` 删除：`ParseJsonObject`、`TryReadObject`、`ReadVersion`、`ReadClampedFloat` 及其余 JSON 辅助函数、`ParseBand`、`ParseElementTable`、`ParsePlanet`、`LoadTextFile`、`IsValidPresetName`、`LoadFromString`、`LoadFromFiles`、`ListPresetNames`、`GetElementDefinitions`，以及 `Accumulate` 里的矿层 `else` 分支（有矿囊的分支保留，去掉 if/else 的外壳）。

`#include` 里删掉 `Dom/JsonObject.h`、`Misc/FileHelper.h`、`Misc/Paths.h`、`Serialization/JsonReader.h`、`Serialization/JsonSerializer.h`、`HAL/FileManager.h`。

`StellarEraGeology.Build.cs` 的 `PrivateDependencyModuleNames` 去掉 `"Json"`，并把文件头注释里的「JSON 预设」改成「UDataAsset 预设」。

- [ ] **Step 4: 删矿层类型**

`PlanetGeologyTypes.h`：删除 `struct FPlanetGeologyBand`；`FPlanetGeologyElement` 里删除 `Bands`、`ReachDepth`、`DepthScale`、`Frequency`；删除 `FPlanetGeologyLoadReport`。顶部注释里「JSON 才是唯一的编辑入口」改成「预设资产是唯一编辑入口」。

- [ ] **Step 5: 生成器改用资产**

`VoxelPlanetGenerator.h`：把

```cpp
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	FString GeologyPreset;
```

替换为

```cpp
	/**
	 * 地质预设资产。空 = 关闭地质。
	 *
	 * 必须是 **VisibleAnywhere**（= Edit + EditConst），不能是 EditAnywhere：
	 * VoxelFree 的 UVoxelGenerator::GetParameters 会枚举可编辑属性并构造
	 * FVoxelGeneratorParameterType。作者入口是 AVoxelPlanetActor::GeologyAsset，
	 * RebuildPlanet 每次把它推到这里。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyPreset;
```

文件顶部加 `class UPlanetGeologyPreset;`。

私有缓存字段整块替换为：

```cpp
	TSharedPtr<const FPlanetGeologyField> GeologyCache;
	TWeakObjectPtr<UPlanetGeologyPreset> GeologyCachePreset;
	double GeologyCacheRadius = 0.0;
	/** 建缓存时的预设修订号：资产内容变了就重建。 */
	int32 GeologyCacheRevision = 0;
```

`VoxelPlanetGenerator.cpp`：`GetGeology()` 整块替换为

```cpp
TSharedPtr<const FPlanetGeologyField> UVoxelPlanetGenerator::GetGeology()
{
	if (!GeologyPreset)
	{
		return nullptr;
	}

	const double CacheRadius = double(Radius);
	const int32 CacheRevision = GeologyPreset->GetRevision();
	if (GeologyCache.IsValid()
		&& GeologyCachePreset.Get() == GeologyPreset.Get()
		&& GeologyCacheRadius == CacheRadius
		&& GeologyCacheRevision == CacheRevision)
	{
		return GeologyCache;
	}

	GeologyCache = FPlanetGeologyField::BuildFromPreset(*GeologyPreset, CacheRadius);
	GeologyCachePreset = GeologyPreset;
	GeologyCacheRadius = CacheRadius;
	GeologyCacheRevision = CacheRevision;

	if (GeologyCache->IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("行星地质预设 %s 没有任何元素"), *GeologyPreset->GetPathName());
	}

	return GeologyCache;
}

void UVoxelPlanetGenerator::InvalidateGeology()
{
	GeologyCache.Reset();
	GeologyCachePreset.Reset();
	GeologyCacheRadius = 0.0;
	GeologyCacheRevision = 0;
}
```

加 `#include "PlanetGeologyPreset.h"`。

- [ ] **Step 6: 行星 Actor 改用资产 + 迁移旧字段**

`VoxelPlanetActor.h`：把现有的 `GeologyPreset` 属性块（含 `GetOptions` meta）与 `GetGeologyPresetOptions` 声明替换为：

```cpp
	/** 地质预设资产。空 = 这颗行星没有矿。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	TObjectPtr<UPlanetGeologyPreset> GeologyAsset;

	/**
	 * 旧字段，只用于加载时迁移（存的是 JSON 预设名）。
	 * PostLoad 会按 MakeLegacyGeologyAssetPath 找资产并写进 GeologyAsset。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Geology",
		meta = (DeprecatedProperty))
	FString GeologyPreset;

	/** 重新取地质场并重建这颗行星：改完预设资产后调用。 */
	UFUNCTION(BlueprintCallable, Category = "Planet|Geology")
	void ReloadGeology();

	/** 当前地质预设资产（编辑器预览订阅它）。 */
	UFUNCTION(BlueprintPure, Category = "Planet|Geology")
	UPlanetGeologyPreset* GetGeologyPresetAsset() const { return GeologyAsset; }

	/**
	 * 旧字段名 → 预设资产路径：`earth` → `/Game/Geology/Planets/DA_Geology_Earth`。
	 * 空名返回空路径。纯函数，便于测试。
	 */
	static FSoftObjectPath MakeLegacyGeologyAssetPath(const FString& LegacyName);
```

类顶部加 `class UPlanetGeologyPreset;`，接口段加 `virtual void PostLoad() override;`，删掉 `GetGeologyPresetOptions` 声明。

`VoxelPlanetActor.cpp`：
1. `#include "PlanetGeologyPreset.h"`。
2. 第 473 行改为 `PlanetGenerator->GeologyPreset = GeologyAsset;`。
3. 删掉 `GetGeologyPresetOptions()` 实现。
4. 在 `ReloadGeology()` 之前加：

```cpp
void AVoxelPlanetActor::PostLoad()
{
	Super::PostLoad();

	// 迁移：旧关卡存的是 JSON 预设名。找不到资产就原样保留字符串，
	// 不报错 —— 资产可能还没建（Task 6）。
	if (!GeologyAsset && !GeologyPreset.IsEmpty())
	{
		const FSoftObjectPath LegacyPath = MakeLegacyGeologyAssetPath(GeologyPreset);
		if (UPlanetGeologyPreset* Loaded = Cast<UPlanetGeologyPreset>(LegacyPath.TryLoad()))
		{
			GeologyAsset = Loaded;
			UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor::PostLoad: %s 的地质预设 '%s' → %s"),
				*GetName(), *GeologyPreset, *LegacyPath.ToString());
		}
	}
}

FSoftObjectPath AVoxelPlanetActor::MakeLegacyGeologyAssetPath(const FString& LegacyName)
{
	FString Name = LegacyName;
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty())
	{
		return FSoftObjectPath();
	}
	Name[0] = FChar::ToUpper(Name[0]);

	const FString AssetName = FString::Printf(TEXT("DA_Geology_%s"), *Name);
	return FSoftObjectPath(FString::Printf(
		TEXT("/Game/Geology/Planets/%s.%s"), *AssetName, *AssetName));
}
```

5. `ReloadGeology()` 注释改成「预设在资产里的修订号变了，丢缓存后重建即可」，逻辑不变。
6. `ComputeParamsHash()` 里

```cpp
	const FString GeologyPresetName = GeologyPreset;
	Mix(*GeologyPresetName, GeologyPresetName.Len() * sizeof(TCHAR));
```

替换为

```cpp
	// 按资产路径混合，不混指针：重载后地址变了、资产没变，混指针会破坏哈希。
	// 也不混 Revision —— 改预设内容不会重刷已烘焙的体素，那是显式的「重建行星」。
	const FString GeologyAssetPath = GeologyAsset ? GeologyAsset->GetPathName() : FString();
	Mix(*GeologyAssetPath, GeologyAssetPath.Len() * sizeof(TCHAR));
```

7. 删掉 `#if WITH_EDITOR` 里的 `ReloadGeologyCommand`（`StellarEra.ReloadGeology` 控制台命令）整个匿名 namespace 块。

- [ ] **Step 7: 补回被删测试里仍然有效的几个**

在 `PlanetGeologyPodTests.cpp` 的 `#endif` 之前追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDeterminismTest,
	"StellarEra.Planet.Geology.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 12, 0.05f, 3, 0.3f),
		MakeElement(TEXT("b"), 9, 0.04f, 4, 0.3f)});
	const auto First = MakeField(*Preset);
	const auto Second = MakeField(*Preset);

	FRandomStream Rng(2024);
	int32 Mismatches = 0;
	for (int32 SampleIndex = 0; SampleIndex < 500; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
		{
			if (First->SampleElementNormAtNormalized(ElementIndex, P)
				!= Second->SampleElementNormAtNormalized(ElementIndex, P))
			{
				++Mismatches;
			}
		}
	}

	TestEqual(TEXT("同一预设两次建场逐点完全相同"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyBoundsTest,
	"StellarEra.Planet.Geology.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 20, 0.06f, 11, 0.2f),
		MakeElement(TEXT("b"), 20, 0.06f, 12, 0.4f)});
	const auto Field = MakeField(*Preset);

	FRandomStream Rng(5150);
	for (int32 SampleIndex = 0; SampleIndex < 5000; ++SampleIndex)
	{
		const FVector P = RandomPoint(Rng);
		for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
		{
			const float Norm = Field->SampleElementNormAtNormalized(ElementIndex, P);
			if (!TestTrue(*FString::Printf(TEXT("Norm %.4f 在 [0,1]"), Norm),
				Norm >= 0.f && Norm <= 1.f))
			{
				return false;
			}
		}

		const FPlanetGeologySample Sample = Field->Sample(P.GetSafeNormal(),
			(1.0 - double(P.Size())) * PlanetRadiusVoxels);
		if (!TestTrue(*FString::Printf(TEXT("Total %.4f / Dominant %.4f / Purity %.4f 均在 [0,1]"),
			Sample.TotalNorm, Sample.DominantNorm, Sample.Purity),
			Sample.TotalNorm >= 0.f && Sample.TotalNorm <= 1.f
			&& Sample.DominantNorm >= 0.f && Sample.DominantNorm <= 1.f
			&& Sample.Purity >= 0.f && Sample.Purity <= 1.f))
		{
			return false;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyColorTest,
	"StellarEra.Planet.Geology.Color",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyColorTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyPodTest;

	FPlanetGeologyElementParams Red = MakeElement(TEXT("red"), 6, 0.10f, 21, 0.0f);
	Red.Color = FLinearColor::Red;
	FPlanetGeologyElementParams Blue = MakeElement(TEXT("blue"), 6, 0.10f, 22, 0.0f);
	Blue.Color = FLinearColor::Blue;

	UPlanetGeologyPreset* Preset = MakePreset({Red, Blue});
	const auto Field = MakeField(*Preset);

	// 在红矿囊中心附近找一个红占优的点，颜色必须偏红。
	for (const FPlanetGeologyPod& Pod : Field->GetPods(0))
	{
		const FPlanetGeologySample Sample = Field->Sample(
			Pod.Center.GetSafeNormal(),
			(1.0 - double(Pod.Center.Size())) * PlanetRadiusVoxels);
		if (Sample.DominantIndex == 0 && Sample.MineralColor.R > Sample.MineralColor.B)
		{
			return true;
		}
	}

	TestTrue(TEXT("至少有一个点由红色元素主导且颜色偏红"), false);
	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyGeneratorParametersTest,
	"StellarEra.Planet.Geology.GeneratorParametersSupported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyGeneratorParametersTest::RunTest(const FString& Parameters)
{
	const FProperty* Property = FindFProperty<FProperty>(
		UVoxelPlanetGenerator::StaticClass(), TEXT("GeologyPreset"));
	if (!TestNotNull(TEXT("生成器上有 GeologyPreset 属性"), Property))
	{
		return false;
	}

	TestTrue(TEXT("必须是对象指针"), CastField<FObjectProperty>(Property) != nullptr);
	TestTrue(TEXT("必须带 CPF_Edit"), Property->HasAnyPropertyFlags(CPF_Edit));
	TestTrue(TEXT("必须同时带 CPF_EditConst（否则 VoxelFree 参数枚举会 ensure）"),
		Property->HasAnyPropertyFlags(CPF_EditConst));
	return true;
}
```

文件顶部补 `#include "VoxelPlanetGenerator.h"`。

- [ ] **Step 8: 删 JSON 内容**

```bash
rm Content/Geology/Elements.json Content/Geology/Planets/earth.json
```

- [ ] **Step 9: 构建 + 跑测试**

Run: UBT；然后自动化测试。
Expected: 构建成功；`StellarEra.Planet.Geology.*` 共 11 个测试通过（PresetDefaults、PresetRevision、PodCoverage、PodOverlap、SpatialIndex、AbundanceGeometryInvariance、PodShape、Determinism、Bounds、Color、GeneratorParametersSupported）。

- [ ] **Step 10: 提交**

```bash
git commit -m "refactor(地质): 退役 JSON 预设，改用 UPlanetGeologyPreset 资产

删除 JSON 装载器与同心矿层模型；行星 Actor 与生成器改持资产指针，
旧关卡的预设名在 PostLoad 里迁移。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraGeology Source/StellarEraTerrain Source/StellarEraMinerals \
  Content/Geology/Elements.json Content/Geology/Planets/earth.json
```

---

## Task 4: 观测视图局部栅格

把「整颗星一个 40³ 立方栅格」换成「按矿囊聚簇的局部栅格」，参数从 `GridResolution` 换成 `CellsPerPod`。

**Files:**
- Modify: `Source/StellarEraMinerals/Public/PlanetMineralSurface.h`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp`（整文件重写）
- Modify: `Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyField.h`（删 `SampleElementNorms`）
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyField.cpp`（删 `SampleElementNorms`）
- Test: `Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp`（新建）

**Interfaces:**
- Consumes: `FPlanetGeologyField::GetPods` / `SampleElementNormAtNormalized` / `NumElements`。
- Produces:
  - `FBuildParams { float SurfaceRadiusCm; float VoxelSizeCm; int32 CellsPerPod; float IsoValue; }`
  - `FCacheKey { TWeakObjectPtr<const UObject> Planet; float SurfaceRadiusCm; FVector Scale; int32 CellsPerPod; }`
  - `UPlanetMineralViewComponent::CellsPerPod`（4..32，默认 12）

- [ ] **Step 1: 写失败的测试**

创建 `Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp`（UTF-8 with BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物等值面构建：闭网格、局部栅格范围、分辨率语义、非法参数。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetMineralSurface.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralSurfaceTest
{
	constexpr float SurfaceRadiusCm = 100000.f; // 1 km 半径
	constexpr float VoxelSizeCm = 100.f;        // 1 体素 = 1 m

	FPlanetGeologyElementParams MakeElement(FName Id, float PodRadius, int32 Seed)
	{
		FPlanetGeologyElementParams Params;
		Params.Id = Id;
		Params.DisplayName = Id.ToString();
		Params.PodCount = 1;
		Params.PodRadius = PodRadius;
		Params.PodRadiusJitter = 0.f;
		Params.PodDepthBias = 0.f;
		Params.ShapeDetail = 2.0f;
		Params.VeinThreshold = 0.f;
		Params.Seed = Seed;
		return Params;
	}

	/**
	 * 矿囊位置由预设的随机流决定，不能指定。所以本文件所有几何断言
	 * 都先读 Field.GetPods() 拿到实际位置，再拿它当基准。
	 */
	UPlanetGeologyPreset* MakePreset(const TArray<FPlanetGeologyElementParams>& Elements)
	{
		UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
		Preset->Elements = Elements;
		Preset->Appearance.ColorThreshold = 0.05f;
		Preset->Appearance.FadeRange = 0.12f;
		Preset->Appearance.PurityForFullColor = 0.45f;
		return Preset;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(
			Preset, double(SurfaceRadiusCm / VoxelSizeCm));
	}

	PlanetMineralSurface::FBuildParams MakeParams(const FPlanetGeologyField& Field,
		int32 CellsPerPod)
	{
		PlanetMineralSurface::FBuildParams Params;
		Params.SurfaceRadiusCm = SurfaceRadiusCm;
		Params.VoxelSizeCm = VoxelSizeCm;
		Params.CellsPerPod = CellsPerPod;
		Params.IsoValue = Field.GetAppearance().ColorThreshold;
		return Params;
	}

	int32 CountNonEmptyMeshes(const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Count = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Count += Mesh.IsEmpty() ? 0 : 1;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceBuildTest,
	"StellarEra.Planet.MineralSurface.Build",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceBuildTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22),
		MakeElement(TEXT("c"), 0.04f, 33)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	TestEqual(TEXT("每个元素一块网格"), Result.MeshesByElement.Num(), 3);
	TestTrue(TEXT("至少两个元素出面"), CountNonEmptyMeshes(Result) >= 2);
	TestTrue(TEXT("角点数非 0"), Result.GridPoints > 0);

	bool bTrianglesMultipleOfThree = true;
	for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
	{
		bTrianglesMultipleOfThree &= Mesh.Triangles.Num() % 3 == 0;
	}
	TestTrue(TEXT("每块网格的三角形数都被 3 整除"), bTrianglesMultipleOfThree);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClosedMeshTest,
	"StellarEra.Planet.MineralSurface.ClosedMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClosedMeshTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.07f, 101),
		MakeElement(TEXT("b"), 0.03f, 202)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		if (Mesh.IsEmpty())
		{
			continue;
		}

		// 每条无向边恰好两个三角形 —— 面上开洞会破坏它。
		TMap<uint64, int32> EdgeUseCount;
		for (int32 TriangleIndex = 0; TriangleIndex < Mesh.Triangles.Num(); TriangleIndex += 3)
		{
			for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
			{
				const int32 A = Mesh.Triangles[TriangleIndex + EdgeIndex];
				const int32 B = Mesh.Triangles[TriangleIndex + (EdgeIndex + 1) % 3];
				const uint64 Key = (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B));
				++EdgeUseCount.FindOrAdd(Key);
			}
		}

		int32 OpenEdges = 0;
		for (const TPair<uint64, int32>& Pair : EdgeUseCount)
		{
			OpenEdges += Pair.Value == 2 ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 网格封闭（开放边 %d）"), ElementIndex, OpenEdges),
			OpenEdges, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfacePodLocalTest,
	"StellarEra.Planet.MineralSurface.PodLocal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfacePodLocalTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 7),
		MakeElement(TEXT("b"), 0.05f, 8)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		if (Mesh.IsEmpty())
		{
			continue;
		}

		// 该元素所有矿囊包围盒的并集，外扩得多一点（插值 + 夹回球面会略微出界）。
		FBox PodBounds(ForceInit);
		for (const FPlanetGeologyPod& Pod : Field->GetPods(ElementIndex))
		{
			const FVector CenterCm = Pod.Center * SurfaceRadiusCm;
			const float RadiusCm = Pod.Radius * SurfaceRadiusCm;
			PodBounds += FBox(CenterCm - FVector(RadiusCm), CenterCm + FVector(RadiusCm));
		}
		PodBounds = PodBounds.ExpandBy(PodBounds.GetExtent().GetMax() * 0.5 + FVector::OneVector);

		int32 Outside = 0;
		for (const FVector3f& Vertex : Mesh.VerticesCm)
		{
			if (!PodBounds.IsInside(FVector(Vertex)))
			{
				++Outside;
			}
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 的顶点全在矿囊包围盒内（越界 %d）"),
			ElementIndex, Outside), Outside, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCellsPerPodTest,
	"StellarEra.Planet.MineralSurface.CellsPerPod",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCellsPerPodTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({MakeElement(TEXT("a"), 0.08f, 5)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Coarse;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 4), Coarse);

	PlanetMineralSurface::FBuildResult Fine;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 24), Fine);

	const int32 CoarseTriangles = Coarse.MeshesByElement[0].Triangles.Num();
	const int32 FineTriangles = Fine.MeshesByElement[0].Triangles.Num();

	TestTrue(*FString::Printf(TEXT("粗网格 %d 个三角形索引，非 0"), CoarseTriangles),
		CoarseTriangles > 0);
	TestTrue(*FString::Printf(TEXT("细网格 %d 远多于粗网格 %d"), FineTriangles, CoarseTriangles),
		FineTriangles > CoarseTriangles * 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyTest,
	"StellarEra.Planet.MineralSurface.InvalidParams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({MakeElement(TEXT("a"), 0.06f, 11)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;

	PlanetMineralSurface::FBuildParams BadParams = MakeParams(*Field, 12);
	BadParams.SurfaceRadiusCm = 0.f;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("半径 0 → 空结果"), Result.MeshesByElement.Num(), 0);

	BadParams = MakeParams(*Field, 12);
	BadParams.CellsPerPod = 0;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("CellsPerPod 0 → 空结果"), Result.MeshesByElement.Num(), 0);

	BadParams = MakeParams(*Field, 12);
	BadParams.VoxelSizeCm = 0.f;
	PlanetMineralSurface::Build(*Field, BadParams, Result);
	TestEqual(TEXT("体素尺寸 0 → 空结果"), Result.MeshesByElement.Num(), 0);
	return true;
}

//////////////////////////////////////////////////////////////////////////
// spec §7.1「保留」表：语义与矿层模型无关，只换夹具。缺一个都算覆盖回退。

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceIsoAccuracyTest,
	"StellarEra.Planet.MineralSurface.IsoAccuracy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceIsoAccuracyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 16);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double Iso = double(Params.IsoValue);
	const float Tolerance = 0.15f;

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 OffSurface = 0;
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 13)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double LengthCm = Position.Size();
			// 被夹回球面的顶点不再落在等值线上 —— 那一条由 SurfaceOutcrop 覆盖。
			if (LengthCm <= UE_DOUBLE_KINDA_SMALL_NUMBER || LengthCm >= RadiusCm - 1.0)
			{
				continue;
			}

			++Probes;
			const float Norm = Field->SampleElementNormAtNormalized(
				ElementIndex, Position / RadiusCm);
			OffSurface += FMath::Abs(double(Norm) - Iso) > Tolerance ? 1 : 0;
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestEqual(*FString::Printf(TEXT("元素 #%d 的顶点都在等值线附近（%d/%d 越界）"),
			ElementIndex, OffSurface, Probes), OffSurface, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceElementOrderTest,
	"StellarEra.Planet.MineralSurface.ElementOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceElementOrderTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 小半径、少矿囊：保证两元素互不重叠，DominantIndex 不会来回跳。
	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.02f, 11),
		MakeElement(TEXT("b"), 0.02f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 16);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
		int32 Probes = 0;
		int32 Mismatches = 0;
		for (int32 Index = 0; Index < Mesh.VerticesCm.Num(); Index += 17)
		{
			const FVector3f& Vertex = Mesh.VerticesCm[Index];
			const FVector Position(double(Vertex.X), double(Vertex.Y), double(Vertex.Z));
			const double LengthCm = Position.Size();
			if (LengthCm <= UE_DOUBLE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			++Probes;
			const double Depth = (RadiusCm - LengthCm) / VoxelSizeCm;
			const FPlanetGeologySample Sample =
				Field->Sample(Position / LengthCm, Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				++Mismatches;
				continue;
			}
			Mismatches += Sample.DominantIndex == ElementIndex ? 0 : 1;
		}

		TestTrue(*FString::Printf(TEXT("元素 #%d 采到了顶点"), ElementIndex), Probes > 0);
		TestEqual(*FString::Printf(TEXT("元素 #%d 的顶点归它自己（%d/%d 错配）"),
			ElementIndex, Mismatches, Probes), Mismatches, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceThresholdTest,
	"StellarEra.Planet.MineralSurface.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceThresholdTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	auto CountTriangles = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Total += Mesh.Triangles.Num();
		}
		return Total;
	};
	auto CountOccupied = [](const PlanetMineralSurface::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const FPlanetMineralMesh& Mesh : Result.MeshesByElement)
		{
			Total += Mesh.OccupiedCells;
		}
		return Total;
	};

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.07f, 11),
		MakeElement(TEXT("b"), 0.06f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildParams Low = MakeParams(*Field, 16);
	Low.IsoValue = Field->GetAppearance().ColorThreshold;
	PlanetMineralSurface::FBuildParams High = Low;
	High.IsoValue = 0.6f;

	PlanetMineralSurface::FBuildResult LowResult;
	PlanetMineralSurface::Build(*Field, Low, LowResult);
	PlanetMineralSurface::FBuildResult HighResult;
	PlanetMineralSurface::Build(*Field, High, HighResult);

	TestTrue(TEXT("阈值升高，占据格数严格变少"), CountOccupied(HighResult) < CountOccupied(LowResult));
	TestTrue(TEXT("高阈值仍有几何"), CountTriangles(HighResult) > 0);
	TestTrue(TEXT("阈值升高，三角形不增加"), CountTriangles(HighResult) <= CountTriangles(LowResult));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptyFieldTest,
	"StellarEra.Planet.MineralSurface.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptyFieldTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 所有元素 PodCount = 0：场的 IsEmpty() 为真（spec §3.6），Build 干净拒绝。
	FPlanetGeologyElementParams Zero = MakeElement(TEXT("a"), 0.06f, 11);
	Zero.PodCount = 0;
	UPlanetGeologyPreset* Preset = MakePreset({Zero});
	const auto Field = MakeField(*Preset);

	TestTrue(TEXT("全零矿囊的场是空场"), Field->IsEmpty());

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);
	TestEqual(TEXT("空场 → 空结果"), Result.MeshesByElement.Num(), 0);
	TestEqual(TEXT("空场不花时间求角点"), Result.GridPoints, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceDeterministicTest,
	"StellarEra.Planet.MineralSurface.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceDeterministicTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		MakeElement(TEXT("b"), 0.05f, 22)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult First;
	PlanetMineralSurface::FBuildResult Second;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 12);
	PlanetMineralSurface::Build(*Field, Params, First);
	PlanetMineralSurface::Build(*Field, Params, Second);

	if (!TestEqual(TEXT("元素数一致"), First.MeshesByElement.Num(), Second.MeshesByElement.Num()))
	{
		return false;
	}

	for (int32 ElementIndex = 0; ElementIndex < First.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& A = First.MeshesByElement[ElementIndex];
		const FPlanetMineralMesh& B = Second.MeshesByElement[ElementIndex];
		TestEqual(*FString::Printf(TEXT("元素 %d 顶点数一致"), ElementIndex),
			A.VerticesCm.Num(), B.VerticesCm.Num());
		TestEqual(*FString::Printf(TEXT("元素 %d 三角索引一致"), ElementIndex),
			A.Triangles.Num(), B.Triangles.Num());

		// 逐顶点、逐索引相同：分簇的并行顺序若泄漏进结果，这里会抓到。
		int32 VertexDiff = 0;
		for (int32 Index = 0; Index < FMath::Min(A.VerticesCm.Num(), B.VerticesCm.Num()); ++Index)
		{
			VertexDiff += A.VerticesCm[Index] == B.VerticesCm[Index] ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 顶点裂变数"), ElementIndex), VertexDiff, 0);

		int32 IndexDiff = 0;
		for (int32 Index = 0; Index < FMath::Min(A.Triangles.Num(), B.Triangles.Num()); ++Index)
		{
			IndexDiff += A.Triangles[Index] == B.Triangles[Index] ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("元素 %d 索引裂变数"), ElementIndex), IndexDiff, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceEmptySlotsTest,
	"StellarEra.Planet.MineralSurface.EmptySlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceEmptySlotsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	FPlanetGeologyElementParams Middle = MakeElement(TEXT("b"), 0.06f, 22);
	Middle.PodCount = 0;

	UPlanetGeologyPreset* Preset = MakePreset({
		MakeElement(TEXT("a"), 0.06f, 11),
		Middle,
		MakeElement(TEXT("c"), 0.06f, 33)});
	const auto Field = MakeField(*Preset);

	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	// 网格数 == 元素数：观察体按下标建组件，少一个就会错位。
	TestEqual(TEXT("网格数等于元素数"), Result.MeshesByElement.Num(), 3);
	bool bMiddleEmpty = Result.MeshesByElement.IsValidIndex(1)
		&& Result.MeshesByElement[1].IsEmpty();
	TestTrue(TEXT("中间那个零矿囊元素留空网格"), bMiddleEmpty);
	TestTrue(TEXT("两侧元素仍有几何"),
		Result.MeshesByElement.IsValidIndex(0) && !Result.MeshesByElement[0].IsEmpty()
		&& Result.MeshesByElement.IsValidIndex(2) && !Result.MeshesByElement[2].IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceCacheKeyTest,
	"StellarEra.Planet.MineralSurface.CacheKey",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceCacheKeyTest::RunTest(const FString& Parameters)
{
	PlanetMineralSurface::FCacheKey Base;
	Base.SurfaceRadiusCm = 100000.f;
	Base.Scale = FVector::OneVector;
	Base.CellsPerPod = 12;

	PlanetMineralSurface::FCacheKey Same = Base;
	TestTrue(TEXT("同值键相等"), Same == Base);

	PlanetMineralSurface::FCacheKey Resolution = Base;
	Resolution.CellsPerPod = 24;
	TestTrue(TEXT("改分辨率 → 键不等"), Resolution != Base);

	PlanetMineralSurface::FCacheKey Radius = Base;
	Radius.SurfaceRadiusCm = 200000.f;
	TestTrue(TEXT("改半径 → 键不等"), Radius != Base);

	PlanetMineralSurface::FCacheKey Scale = Base;
	Scale.Scale = FVector(2.0, 2.0, 2.0);
	TestTrue(TEXT("改缩放 → 键不等"), Scale != Base);

	// 半径容差 0.01 cm（0.1 mm）：亚微米级抖动不该触发重建。
	PlanetMineralSurface::FCacheKey TinyRadius = Base;
	TinyRadius.SurfaceRadiusCm = Base.SurfaceRadiusCm + 0.001f;
	TestTrue(TEXT("半径抖动在容差内 → 键仍相等"), TinyRadius == Base);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceOutcropTest,
	"StellarEra.Planet.MineralSurface.SurfaceOutcrop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceOutcropTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	// 48 囊 / r=0.08：约 22% 的囊满足 rn + r > 1，固定种子下必然出现露头。
	FPlanetGeologyElementParams Element = MakeElement(TEXT("a"), 0.08f, 5);
	Element.PodCount = 48;
	Element.PodRadiusJitter = 0.2f;
	UPlanetGeologyPreset* Preset = MakePreset({Element});
	const auto Field = MakeField(*Preset);

	bool bAnyPodCrossesSurface = false;
	for (const FPlanetGeologyPod& Pod : Field->GetPods(0))
	{
		bAnyPodCrossesSurface |= Pod.Center.Size() + Pod.Radius > 1.f;
	}
	if (!TestTrue(TEXT("夹具里确有矿囊探出球面"), bAnyPodCrossesSurface))
	{
		return false;
	}

	PlanetMineralSurface::FBuildResult Result;
	const PlanetMineralSurface::FBuildParams Params = MakeParams(*Field, 12);
	PlanetMineralSurface::Build(*Field, Params, Result);

	const FPlanetMineralMesh& Mesh = Result.MeshesByElement[0];
	if (!TestTrue(TEXT("出面了"), !Mesh.IsEmpty()))
	{
		return false;
	}

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	int32 Outside = 0;
	int32 Clamped = 0;
	for (const FVector3f& Vertex : Mesh.VerticesCm)
	{
		const double LengthCm =
			FVector(double(Vertex.X), double(Vertex.Y), double(Vertex.Z)).Size();
		Outside += LengthCm > RadiusCm + 1.0 ? 1 : 0;
		Clamped += LengthCm >= RadiusCm - 1.0 ? 1 : 0;
	}

	TestEqual(*FString::Printf(TEXT("顶点全部夹在球内或球面上（越界 %d）"), Outside), Outside, 0);
	TestTrue(*FString::Printf(TEXT("露头顶点被夹到球面（%d 个）"), Clamped), Clamped > 0);
	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认失败**

Expected: `FBuildParams` 没有 `CellsPerPod` 等编译错误。

- [ ] **Step 3: 改 `PlanetMineralSurface.h`**

`FBuildParams` 整块替换：

```cpp
	/** 一次构建的输入。半径 ≤ 0、体素 ≤ 0 或 CellsPerPod < 4 时 Build 直接给空结果。 */
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 一个体素多少厘米：地质深度以体素计，换回厘米要用它。 */
		float VoxelSizeCm = 0.f;
		/**
		 * 一个矿囊直径横向切多少格（4..32）。
		 *
		 * 这是跟随矿囊的旋钮，与行星直径无关 —— 换一颗大行星，矿囊的相对大小不变，
		 * 观测视图的清晰度也不变。
		 */
		int32 CellsPerPod = 12;
		/** 等值线，调用方传 Field.GetAppearance().ColorThreshold。 */
		float IsoValue = 0.f;
	};
```

`FCacheKey` 里 `int32 GridResolution = 0;` → `int32 CellsPerPod = 0;`，`operator==` 同步。

`Build` 的文档注释重写：

```cpp
	/**
	 * 逐元素抽等值面。Out 先被清空；参数非法或场为空时返回空结果，不报错。
	 *
	 * **局部栅格**：按矿囊的空间邻近关系分簇（用未外扩的 AABB 判相交、并查集合并），
	 * 每簇一套独立栅格，格边 = 2 × 簇内最小矿囊半径 / CellsPerPod，盒子 = 簇包围盒
	 * 外扩一格。这样分辨率跟着矿囊走，而不是跟着行星直径走。
	 *
	 * 外扩一格保证盒子边界上所有角点场值为 0（矿囊表面之外场值恒 0），
	 * Marching Cubes 因此在盒子内封闭 —— 漏了外扩就会在面上开洞。
	 *
	 * 顶点仍会径向夹到球内（`|v| <= R`）：观察体的半透明壳就画在 R 上。
	 * 各元素场独立取面：两种元素同时高于门限的地方，两块矿体会互相穿插（共生矿囊），
	 * 不做 winner-take-all —— 布尔场做 MC 会退回方块状边缘。
	 */
```

- [ ] **Step 4: 重写 `PlanetMineralSurface.cpp`**

整文件替换为：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralSurface.h"

#include "PlanetGeologyField.h"

#include "Async/ParallelFor.h"
#include "Generators/MarchingCubes.h"
#include "HAL/PlatformTime.h"

namespace
{
	/** 一个待处理的矿囊（厘米空间）。 */
	struct FPodInstance
	{
		int32 ElementIndex = INDEX_NONE;
		FVector CenterCm = FVector::ZeroVector;
		float RadiusCm = 0.f;
	};

	/** 并查集：把邻近的矿囊合并成同一个局部栅格。 */
	struct FDisjointSet
	{
		TArray<int32> Parent;

		void Init(int32 Count)
		{
			Parent.SetNum(Count);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				Parent[Index] = Index;
			}
		}

		int32 Find(int32 Index)
		{
			while (Parent[Index] != Index)
			{
				Parent[Index] = Parent[Parent[Index]];
				Index = Parent[Index];
			}
			return Index;
		}

		void Union(int32 A, int32 B)
		{
			const int32 RootA = Find(A);
			const int32 RootB = Find(B);
			if (RootA != RootB)
			{
				Parent[RootB] = RootA;
			}
		}
	};

	bool AreAabbsOverlapping(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X
			&& A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y
			&& A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
}

void PlanetMineralSurface::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.MeshesByElement.Reset();
	Out.GridPoints = 0;
	Out.BuildSeconds = 0.0;

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	if (Field.IsEmpty() || RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.f
		|| Params.CellsPerPod < 4)
	{
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const int32 ElementCount = Field.NumElements();
	Out.MeshesByElement.SetNum(ElementCount);

	// --- 1. 收集所有矿囊（厘米空间） ---
	TArray<FPodInstance> Pods;
	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		for (const FPlanetGeologyPod& Pod : Field.GetPods(ElementIndex))
		{
			if (Pod.Radius <= 0.f)
			{
				continue;
			}
			FPodInstance& Instance = Pods.AddDefaulted_GetRef();
			Instance.ElementIndex = ElementIndex;
			Instance.CenterCm = Pod.Center * RadiusCm;
			Instance.RadiusCm = Pod.Radius * float(RadiusCm);
		}
	}
	if (Pods.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	TArray<FBox> PodBounds;
	PodBounds.Reserve(Pods.Num());
	for (const FPodInstance& Pod : Pods)
	{
		PodBounds.Add(FBox(Pod.CenterCm - FVector(Pod.RadiusCm),
			Pod.CenterCm + FVector(Pod.RadiusCm)));
	}

	// --- 2. 分簇：未外扩的 AABB 相交即同簇（并查集） ---
	FDisjointSet Clusters;
	Clusters.Init(Pods.Num());
	for (int32 A = 0; A < Pods.Num(); ++A)
	{
		for (int32 B = A + 1; B < Pods.Num(); ++B)
		{
			if (AreAabbsOverlapping(PodBounds[A], PodBounds[B]))
			{
				Clusters.Union(A, B);
			}
		}
	}

	TMap<int32, TArray<int32>> MembersByRoot;
	for (int32 Index = 0; Index < Pods.Num(); ++Index)
	{
		MembersByRoot.FindOrAdd(Clusters.Find(Index)).Add(Index);
	}

	TArray<TArray<int32>> ClusterMembers;
	ClusterMembers.Reserve(MembersByRoot.Num());
	for (TPair<int32, TArray<int32>>& Pair : MembersByRoot)
	{
		ClusterMembers.Add(MoveTemp(Pair.Value));
	}

	// --- 3. 每簇一套局部栅格，并行抽面 ---
	struct FClusterMesh
	{
		int32 ElementIndex = INDEX_NONE;
		FPlanetMineralMesh Mesh;
		int32 GridPoints = 0;
	};

	TArray<TArray<FClusterMesh>> ClusterResults;
	ClusterResults.SetNum(ClusterMembers.Num());

	ParallelFor(ClusterMembers.Num(), [&](int32 ClusterIndex)
	{
		const TArray<int32>& Members = ClusterMembers[ClusterIndex];

		FBox Box(ForceInit);
		float MinRadiusCm = TNumericLimits<float>::Max();
		for (const int32 PodIndex : Members)
		{
			Box += PodBounds[PodIndex];
			MinRadiusCm = FMath::Min(MinRadiusCm, Pods[PodIndex].RadiusCm);
		}
		if (MinRadiusCm <= 0.f || !Box.IsValid)
		{
			return;
		}

		const double CellSize = 2.0 * double(MinRadiusCm) / double(Params.CellsPerPod);

		// 外扩一格，并把盒子拉到整数格：FMarchingCubes 按 Width/CubeSize + 1 定角点数。
		const FVector PaddedMin = Box.Min - FVector(CellSize);
		const FVector PaddedMax = Box.Max + FVector(CellSize);
		const FIntVector CellCount(
			FMath::Max(1, FMath::CeilToInt((PaddedMax.X - PaddedMin.X) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Y - PaddedMin.Y) / CellSize)),
			FMath::Max(1, FMath::CeilToInt((PaddedMax.Z - PaddedMin.Z) / CellSize)));

		const FVector BoxMin = PaddedMin;
		const FVector BoxMax = BoxMin + FVector(
			double(CellCount.X) * CellSize,
			double(CellCount.Y) * CellSize,
			double(CellCount.Z) * CellSize);
		const FIntVector CornerCount = CellCount + FIntVector(1);

		// 本簇涉及哪些元素
		TSet<int32> ElementSet;
		for (const int32 PodIndex : Members)
		{
			ElementSet.Add(Pods[PodIndex].ElementIndex);
		}

		TArray<FClusterMesh>& Results = ClusterResults[ClusterIndex];
		Results.SetNum(ElementSet.Num());

		int32 ResultIndex = 0;
		for (const int32 ElementIndex : ElementSet)
		{
			FClusterMesh& Target = Results[ResultIndex];
			Target.ElementIndex = ElementIndex;

			const int32 TotalCorners = CornerCount.X * CornerCount.Y * CornerCount.Z;
			TArray<float> Norms;
			Norms.SetNumZeroed(TotalCorners);

			ParallelFor(TotalCorners, [&](int32 LinearIndex)
			{
				const int32 X = LinearIndex % CornerCount.X;
				const int32 Y = (LinearIndex / CornerCount.X) % CornerCount.Y;
				const int32 Z = LinearIndex / (CornerCount.X * CornerCount.Y);

				const FVector P(
					BoxMin.X + double(X) * CellSize,
					BoxMin.Y + double(Y) * CellSize,
					BoxMin.Z + double(Z) * CellSize);

				Norms[LinearIndex] = Field.SampleElementNormAtNormalized(ElementIndex, P / RadiusCm);
			});

			UE::Geometry::FMarchingCubes Marching;
			Marching.Bounds.Min = UE::Math::TVector<double>(BoxMin.X, BoxMin.Y, BoxMin.Z);
			Marching.Bounds.Max = UE::Math::TVector<double>(BoxMax.X, BoxMax.Y, BoxMax.Z);
			Marching.CubeSize = CellSize;
			Marching.IsoValue = double(Params.IsoValue);
			Marching.bEnableValueCaching = false;
			Marching.bParallelCompute = true;
			Marching.Implicit = [&Norms, CornerCount, BoxMin, CellSize](
				UE::Math::TVector<double> P) -> double
			{
				const double GX = FMath::Clamp((P.X - BoxMin.X) / CellSize, 0.0, double(CornerCount.X - 1));
				const double GY = FMath::Clamp((P.Y - BoxMin.Y) / CellSize, 0.0, double(CornerCount.Y - 1));
				const double GZ = FMath::Clamp((P.Z - BoxMin.Z) / CellSize, 0.0, double(CornerCount.Z - 1));

				const int32 X0 = FMath::Min(int32(GX), CornerCount.X - 2);
				const int32 Y0 = FMath::Min(int32(GY), CornerCount.Y - 2);
				const int32 Z0 = FMath::Min(int32(GZ), CornerCount.Z - 2);
				const double FX = GX - double(X0);
				const double FY = GY - double(Y0);
				const double FZ = GZ - double(Z0);

				auto At = [&Norms, CornerCount](int32 InX, int32 InY, int32 InZ) -> double
				{
					return double(Norms[(InZ * CornerCount.Y + InY) * CornerCount.X + InX]);
				};

				const double C00 = FMath::Lerp(At(X0, Y0, Z0), At(X0 + 1, Y0, Z0), FX);
				const double C10 = FMath::Lerp(At(X0, Y0 + 1, Z0), At(X0 + 1, Y0 + 1, Z0), FX);
				const double C01 = FMath::Lerp(At(X0, Y0, Z0 + 1), At(X0 + 1, Y0, Z0 + 1), FX);
				const double C11 = FMath::Lerp(At(X0, Y0 + 1, Z0 + 1), At(X0 + 1, Y0 + 1, Z0 + 1), FX);
				const double C0 = FMath::Lerp(C00, C10, FY);
				const double C1 = FMath::Lerp(C01, C11, FY);
				return FMath::Lerp(C0, C1, FZ);
			};

			Marching.Generate();

			Target.GridPoints = TotalCorners;
			Target.Mesh.VerticesCm.Reserve(Marching.Vertices.Num());
			for (const UE::Math::TVector<double>& Vertex : Marching.Vertices)
			{
				// 球外角点恒为 0、内侧角点为正：插值会把顶点落到球外，
				// 而观察体的半透明壳画在 R 上，逐顶点径向夹回。
				const double LengthCm = Vertex.Size();
				const double ScaleFactor = LengthCm > RadiusCm ? RadiusCm / LengthCm : 1.0;
				Target.Mesh.VerticesCm.Add(FVector3f(
					float(Vertex.X * ScaleFactor),
					float(Vertex.Y * ScaleFactor),
					float(Vertex.Z * ScaleFactor)));
			}

			Target.Mesh.Triangles.Reserve(Marching.Triangles.Num() * 3);
			for (const UE::Geometry::FIndex3i& Triangle : Marching.Triangles)
			{
				Target.Mesh.Triangles.Add(Triangle.A);
				Target.Mesh.Triangles.Add(Triangle.B);
				Target.Mesh.Triangles.Add(Triangle.C);
			}

			int32 Occupied = 0;
			for (const float Norm : Norms)
			{
				Occupied += Norm > Params.IsoValue ? 1 : 0;
			}
			Target.Mesh.OccupiedCells = Occupied;

			++ResultIndex;
		}
	});

	// --- 4. 合并各簇结果：顶点下标要按已合并的数量偏移 ---
	for (const TArray<FClusterMesh>& Results : ClusterResults)
	{
		for (const FClusterMesh& Result : Results)
		{
			if (!Out.MeshesByElement.IsValidIndex(Result.ElementIndex))
			{
				continue;
			}

			FPlanetMineralMesh& Mesh = Out.MeshesByElement[Result.ElementIndex];
			const int32 VertexOffset = Mesh.VerticesCm.Num();
			Mesh.VerticesCm.Append(Result.Mesh.VerticesCm);
			Mesh.Triangles.Reserve(Mesh.Triangles.Num() + Result.Mesh.Triangles.Num());
			for (const int32 Index : Result.Mesh.Triangles)
			{
				Mesh.Triangles.Add(Index + VertexOffset);
			}
			Mesh.OccupiedCells += Result.Mesh.OccupiedCells;
			Out.GridPoints += Result.GridPoints;
		}
	}

	// 顶点顺序里没有用到的元素保持空网格（观察体按元素数校验）。
	Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
}
```

- [ ] **Step 5: 组件换成 `CellsPerPod`**

`PlanetMineralViewComponent.h`：`GridResolution` 属性块替换为

```cpp
	/**
	 * 观测视图密度：一个矿囊直径横向切多少格。
	 *
	 * 12 首进约 0.1–0.3 秒；24 边缘更圆，约 2–4 倍耗时。改动会让缓存失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView",
		meta = (ClampMin = "4", ClampMax = "32"))
	int32 CellsPerPod = 12;
```

`PlanetMineralViewComponent.cpp`：
- `Params.GridResolution = GridResolution;` → `Params.CellsPerPod = CellsPerPod;`
- `Key.GridResolution = GridResolution;` → `Key.CellsPerPod = CellsPerPod;`
- 日志行里的 `网格 %d³` + `GridResolution` 参数改成 `每囊 %d 格` + `CellsPerPod`。

- [ ] **Step 6: 删掉 `SampleElementNorms`**

`PlanetGeologyField.h` / `.cpp` 删除 `SampleElementNorms` 声明与实现（`Build` 是唯一生产调用点，已改走 `SampleElementNormAtNormalized`）。`Accumulate` 的 `OutElementNorms` 参数一并删除。

- [ ] **Step 7: 构建 + 跑测试**

Run: UBT；然后自动化测试。
Expected: `StellarEra.Planet.MineralSurface.*` 五个测试通过，其余全绿。

- [ ] **Step 8: 提交**

```bash
git commit -m "feat(矿物): 观测视图改成按矿囊聚簇的局部栅格

网格分辨率从行星直径解耦，参数从 GridResolution 换成 CellsPerPod。
补 ClosedMesh / PodLocal / CellsPerPod 测试 —— 这三个 bug 都不报错，
只表现为矿少了或有破面。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraMinerals Source/StellarEraGeology
```

---

## Task 5: 编辑器预览

让矿囊在**编辑器视口**里就能看，不用进 PIE；改预设参数时自动重建。

**Files:**
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyPreset.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp`
- Modify: `Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp`

**Interfaces:**
- Consumes: `AVoxelPlanetActor::GetGeologyPresetAsset()`（Task 3）、`UPlanetGeologyPreset::Revision`（Task 1）。
- Produces:
  - `#if WITH_EDITOR DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlanetGeologyPresetChanged, UPlanetGeologyPreset*)` 与成员 `OnPresetChanged`
  - `UPlanetMineralViewComponent::EnterMineralPreviewInEditor()` / `ExitMineralPreviewInEditor()` / `IsPreviewing()`

- [ ] **Step 1: 预设增加变更广播**

`PlanetGeologyPreset.h` 顶部（`#include` 之后、`USTRUCT` 之前）加：

```cpp
#if WITH_EDITOR
/** 预设内容被改动（编辑器）。观察视图用它自动重建。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPlanetGeologyPresetChanged, UPlanetGeologyPreset*);
#endif
```

`UPlanetGeologyPreset` 的 `public:` 段末尾加：

```cpp
#if WITH_EDITOR
	/**
	 * 内容变更广播。只在编辑器存在；无人订阅时零成本。
	 * 用静态多播而不是 UPROPERTY 委托：订阅者是 C++ 组件，不需要蓝图序列化。
	 */
	FOnPlanetGeologyPresetChanged OnPresetChanged;
#endif
```

`PlanetGeologyPreset.cpp` 的 `PostEditChangeProperty` 里 `++Revision;` 之后加：

```cpp
	OnPresetChanged.Broadcast(this);
```

- [ ] **Step 2: 组件加预览入口**

`PlanetMineralViewComponent.h` 的 `public:` 段（`IsActive()` 之后）加：

```cpp
#if WITH_EDITOR
	/**
	 * 在编辑器视口里进入矿物预览（不进 PIE）。
	 *
	 * 与 PIE 的差别只有「怎么让行星消失」：这里用非序列化的临时隐藏，
	 * 不翻任何 UPROPERTY，所以退出后关卡不会留下脏包。
	 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void EnterMineralPreviewInEditor();

	/** 退出编辑器预览，撤销临时隐藏。重复调用安全。 */
	UFUNCTION(CallInEditor, Category = "Planet|MineralView")
	void ExitMineralPreviewInEditor();
#endif

	/** 当前是否处于预览（含 PIE 激活）。 */
	UFUNCTION(BlueprintPure, Category = "Planet|MineralView")
	bool IsPreviewing() const { return bActive; }
```

`private:` 段加：

```cpp
	/** 本次激活是编辑器预览而不是 PIE/运行时。 */
	bool bEditorPreview = false;
	/** 进入前行星是否已被临时隐藏（编辑器分支的恢复值）。 */
	bool bSavedPlanetHiddenInEditor = false;
#if WITH_EDITOR
	/** 预设变更订阅。 */
	FDelegateHandle PresetChangedHandle;
	void OnPreviewPresetChanged(UPlanetGeologyPreset* Preset);
#endif
```

- [ ] **Step 3: 实现分支**

`PlanetMineralViewComponent.cpp` 在 `Activate` 里：
1. 取 `World` 之后立刻判定：

```cpp
	// 编辑器世界与 PIE 是两套世界；用 WorldType 判，不用 GEditor->IsPlaySessionInProgress。
	const bool bInEditorWorld = World->WorldType == EWorldType::Editor;
```

2. `SpawnParams` 处加：

```cpp
	if (bInEditorWorld)
	{
		// 永远不写进 umap。
		SpawnParams.ObjectFlags |= RF_Transient;
	}
```

3. 把「先存后改」那一段替换为：

```cpp
	bEditorPreview = bInEditorWorld;

	if (bEditorPreview)
	{
		// 编辑器分支**不改任何 UPROPERTY**：翻 bAtmosphereEnabled / bActorEnableCollision
		// 会把关卡标脏，退出后留下待提交的脏包。临时隐藏是非序列化状态。
		bSavedPlanetHiddenInEditor = InPlanet->IsTemporarilyHiddenInEditor();
		InPlanet->SetIsTemporarilyHiddenInEditor(true);
	}
	else
	{
		bSavedPlanetHidden = InPlanet->IsHidden();
		bSavedCollisionEnabled = InPlanet->GetActorEnableCollision();
		InPlanet->SetActorEnableCollision(false);
		InPlanet->SetActorHiddenInGame(true);

		if (UPlanetAtmosphereComponent* Atmosphere =
			InPlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
		{
			bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
			bSavedAtmosphereVisible = Atmosphere->IsVisible();
			Atmosphere->bAtmosphereEnabled = false;
			Atmosphere->SetVisibility(false);
		}
	}
```

4. `bActive = true;` 之前加：

```cpp
#if WITH_EDITOR
	if (bEditorPreview)
	{
		if (UPlanetGeologyPreset* Preset = InPlanet->GetGeologyPresetAsset())
		{
			PresetChangedHandle = Preset->OnPresetChanged.AddUObject(
				this, &UPlanetMineralViewComponent::OnPreviewPresetChanged);
		}
	}
#endif
```

5. `SetComponentTickEnabled(true);` 改为 `SetComponentTickEnabled(!bEditorPreview);`（编辑器里不 Tick；位置在 SpawnActor 时已经给对）。

6. `Deactivate()` 恢复段替换为：

```cpp
#if WITH_EDITOR
	if (PresetChangedHandle.IsValid())
	{
		if (AVoxelPlanetActor* SubscribedPlanet = Planet.Get())
		{
			if (UPlanetGeologyPreset* Preset = SubscribedPlanet->GetGeologyPresetAsset())
			{
				Preset->OnPresetChanged.Remove(PresetChangedHandle);
			}
		}
		PresetChangedHandle.Reset();
	}
#endif

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		if (bEditorPreview)
		{
			RestorePlanet->SetIsTemporarilyHiddenInEditor(bSavedPlanetHiddenInEditor);
		}
		else
		{
			RestorePlanet->SetActorHiddenInGame(bSavedPlanetHidden);
			RestorePlanet->SetActorEnableCollision(bSavedCollisionEnabled);

			if (UPlanetAtmosphereComponent* Atmosphere =
				RestorePlanet->FindComponentByClass<UPlanetAtmosphereComponent>())
			{
				Atmosphere->bAtmosphereEnabled = bSavedAtmosphereEnabled;
				if (bSavedAtmosphereEnabled)
				{
					// Refresh 末尾会 SetVisibility(true)：必须先刷新、再恢复进入前的
					// 可见性，否则「enabled 但被手动隐藏」的壳会被强行显示。
					Atmosphere->RefreshAtmosphereGeometry();
				}
				Atmosphere->SetVisibility(bSavedAtmosphereVisible);
			}
		}
	}

	bEditorPreview = false;
```

7. 文件末尾（`#if WITH_EDITOR` 内）加三个函数：

```cpp
#if WITH_EDITOR
void UPlanetMineralViewComponent::EnterMineralPreviewInEditor()
{
	// 宿主就是行星：组件挂在 AVoxelPlanetActor 上。
	if (!Activate(Cast<AVoxelPlanetActor>(GetOwner())))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[矿物] 预览失败：宿主没有地质预设，或采样参数非法（%s）"),
			*GetNameSafe(GetOwner()));
	}
}

void UPlanetMineralViewComponent::ExitMineralPreviewInEditor()
{
	Deactivate();
}

void UPlanetMineralViewComponent::OnPreviewPresetChanged(UPlanetGeologyPreset* Preset)
{
	// 重新 Activate：BuildFromPreset 会造新场，缓存键里的地质对象随之改变 → 重建网格。
	if (AVoxelPlanetActor* InPlanet = Planet.Get())
	{
		Activate(InPlanet);
	}
}
#endif
```

8. 实现文件加 `#include "PlanetGeologyPreset.h"`。

- [ ] **Step 4: 构建**

Run: UBT。
Expected: 成功。

- [ ] **Step 5: 跑测试**

Run: 自动化测试。
Expected: 全绿（本任务无新增自动化测试；编辑器预览靠 Task 6 的手工验收）。

- [ ] **Step 6: 提交**

```bash
git commit -m "feat(矿物): 编辑器视口内预览矿囊

按钮挂在矿物组件上（Terrain 不能反向依赖 Minerals）。
编辑器分支只做非序列化的临时隐藏，退出后关卡不留脏包；
预设内容改动通过多播委托自动重建。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraGeology Source/StellarEraMinerals
```

---

## Task 6: 内容资产、文档与端到端验收

建 `DA_Geology_Earth`，重写旧说明，跑完整验收。

**Files:**
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp`
- Overwrite: `Content/Geology/README.md`
- Modify: `CLAUDE.md`

**Interfaces:**
- Consumes: `UPlanetGeologyPreset`（Task 1–3）。
- Produces: 控制台命令 `StellarEra.CreateEarthGeologyPreset`；资产 `/Game/Geology/Planets/DA_Geology_Earth`。

- [ ] **Step 1: 用控制台命令建资产**

`PlanetGeologyPreset.cpp` 顶部 include 区加（放在 `#if WITH_EDITOR` 里）：

```cpp
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif
```

文件末尾追加：

```cpp
#if WITH_EDITOR
namespace
{
	void FillEarthPreset(UPlanetGeologyPreset& Preset)
	{
		auto AddElement = [&Preset](const TCHAR* Id, const TCHAR* DisplayName,
			const FLinearColor& Color, float Abundance, int32 PodCount, float PodRadius,
			int32 Seed)
		{
			FPlanetGeologyElementParams& Params = Preset.Elements.AddDefaulted_GetRef();
			Params.Id = Id;
			Params.DisplayName = DisplayName;
			Params.Color = Color;
			Params.Abundance = Abundance;
			Params.PodCount = PodCount;
			Params.PodRadius = PodRadius;
			Params.Seed = Seed;
		};

		Preset.Elements.Reset();
		AddElement(TEXT("silicon"), TEXT("硅"), FLinearColor(0.62f, 0.58f, 0.50f), 0.60f, 20, 0.050f, 303);
		AddElement(TEXT("iron"), TEXT("铁"), FLinearColor(0.72f, 0.42f, 0.36f), 0.75f, 20, 0.050f, 101);
		AddElement(TEXT("water"), TEXT("水"), FLinearColor(0.20f, 0.45f, 0.90f), 0.20f, 16, 0.048f, 606);
		AddElement(TEXT("copper"), TEXT("铜"), FLinearColor(0.85f, 0.55f, 0.25f), 0.45f, 16, 0.045f, 202);
		AddElement(TEXT("titanium"), TEXT("钛"), FLinearColor(0.70f, 0.72f, 0.78f), 0.15f, 12, 0.042f, 404);
		AddElement(TEXT("uranium"), TEXT("铀"), FLinearColor(0.45f, 0.85f, 0.35f), 0.10f, 10, 0.038f, 505);

		Preset.Appearance.ColorThreshold = 0.05f;
		Preset.Appearance.FadeRange = 0.12f;
		Preset.Appearance.PurityForFullColor = 0.45f;
		Preset.GeometryAmplitude = 1.5f;
	}

	void CreateEarthGeologyPreset()
	{
		const FString PackageName = TEXT("/Game/Geology/Planets/DA_Geology_Earth");
		const FString AssetName = TEXT("DA_Geology_Earth");

		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			UE_LOG(LogTemp, Error, TEXT("建包失败: %s"), *PackageName);
			return;
		}

		UPlanetGeologyPreset* Preset = FindObject<UPlanetGeologyPreset>(Package, *AssetName);
		const bool bCreated = Preset == nullptr;
		if (bCreated)
		{
			Preset = NewObject<UPlanetGeologyPreset>(
				Package, *AssetName, RF_Public | RF_Standalone);
		}

		FillEarthPreset(*Preset);
		++Preset->Revision;

		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Preset);
		}
		Package->MarkPackageDirty();

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		const FString FileName = FPackageName::LongPackageNameToFilename(
			PackageName, FPackageName::GetAssetPackageExtension());
		const bool bSaved = UPackage::SavePackage(Package, Preset, *FileName, SaveArgs);

		UE_LOG(LogTemp, Log, TEXT("StellarEra.CreateEarthGeologyPreset: %s（%s，%d 个元素）"),
			bSaved ? TEXT("已保存") : TEXT("保存失败"),
			bCreated ? TEXT("新建") : TEXT("覆盖"),
			Preset->Elements.Num());
	}
}

static FAutoConsoleCommand CreateEarthGeologyPresetCommand(
	TEXT("StellarEra.CreateEarthGeologyPreset"),
	TEXT("创建或覆盖 /Game/Geology/Planets/DA_Geology_Earth。"),
	FConsoleCommandDelegate::CreateStatic(&CreateEarthGeologyPreset));
#endif
```

- [ ] **Step 2: 构建并执行命令**

Run: UBT；然后

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="StellarEra.CreateEarthGeologyPreset" -unattended -nopause -nullrhi -nosplash \
  -stdout -abslog="F:/tmp/create-geology.log"
```

Expected: 日志出现 `StellarEra.CreateEarthGeologyPreset: 已保存（新建，6 个元素）`；`Content/Geology/Planets/DA_Geology_Earth.uasset` 存在且不是 LFS 指针（`ls -l` 应 > 1 KB）。

- [ ] **Step 3: 重写 `Content/Geology/README.md`**

```markdown
# 行星地质预设

一颗行星的矿物分布定义在 **UDataAsset** 里：`Content/Geology/Planets/DA_Geology_<名字>`。
JSON 预设已退役（2026-10-25）。

## 怎么改

1. 选中 `DA_Geology_Earth`，在 Details 里调 `Elements`。
2. 选中行星 Actor，`Planet|Geology → Geology Asset` 指向这份资产。
3. 点 `Reload Geology` **重建行星** —— 体素是烘焙的，改预设不会自动重刷地表。

想看矿囊长什么样：选中行星上的矿物组件 → Details 里点 `Enter Mineral Preview`。
退出后关卡不会变脏（编辑器预览只做临时隐藏）。

## 参数语义

| 参数 | 含义 |
|---|---|
| `Abundance` | **只影响颜色混合与图例占比**，不改几何 |
| `PodCount` / `PodRadius` | 撒多少矿囊、每个多大（占行星半径比例） |
| `PodRadiusJitter` | 每囊半径的随机抖动 |
| `PodDepthBias` | 0 = 全深度体积均匀；>0 向 `PodDepthCenter` 聚集 |
| `ShapeDetail` | 一个矿囊直径内几个噪声周期，越大越破碎 |
| `VeinThreshold` | 噪声稀疏阈值，越高矿囊越破碎 |
| `Seed` | 0 = 按元素 Id 派生，不同元素默认不相关 |

**想看不同元素互相穿插**：重叠对数量 ≈ `PodCount² × (2·r)³`。半径砍半，重叠少 8 倍——
所以别靠加 `PodCount` 硬堆，先把 `PodRadius` 调大。
```

- [ ] **Step 4: 更新 `CLAUDE.md`**

把模块表那行

```
├── StellarEraGeology/     元素表、行星地质场、噪声采样、JSON 预设
```

改成

```
├── StellarEraGeology/     元素表、行星地质场、噪声采样、UDataAsset 预设
```

在 `Content/` 段落加一行（`Code/` 之前）：

```
├── Geology/Planets/    DA_Geology_<名字>（行星矿物预设；JSON 已退役）
```

- [ ] **Step 5: 完整验收（spec §9）**

1. `Automation RunTests StellarEra` 全绿，把总数记进实施记录。
2. 打开编辑器，`UniverseMap` 里选中行星 → 确认 Details 的 `Geology Asset` 已由 `PostLoad` 迁移成 `DA_Geology_Earth`（若为空说明资产路径不对，检查 `MakeLegacyGeologyAssetPath`）。
3. 点矿物组件的 `Enter Mineral Preview`：视口出现矿囊；用图例关掉一个元素，看到别的元素的矿囊嵌在里面。点 `Exit Mineral Preview`，然后 `git status -- Content/` **必须干净**。
4. PIE 按 H：矿囊横向约 12 格；地形上只有露头处的斑块染色，颜色与观测视图同元素同色。
5. 记录 `UniverseMap` 重建行星耗时（日志里重建行星那一行），与改造前对比。

Failure 处置：若 3 里 `git status` 出现脏包，说明编辑器分支动了 UPROPERTY，回到 Task 5 的 Step 3 检查是否漏了 `bEditorPreview` 判断。

- [ ] **Step 6: 提交**

```bash
git commit -m "chore(地质): 建 DA_Geology_Earth，重写预设说明

矿囊参数全部可在资产里调；README 说明「改预设需重建行星」与
「重叠数随 PodCount² × (2r)³ 增长」。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp \
  Content/Geology CLAUDE.md
```

---

## 自审

**Spec 覆盖**

| spec 章节 | 落在哪个任务 |
|---|---|
| §2 数据模型（资产、字段、运行期类型、接入点） | Task 1、Task 3 |
| §3.1 建场 `BuildFromPreset` | Task 2 Step 5 |
| §3.2 撒矿囊 | Task 2 Step 5 `PlacePods` |
| §3.3 求值 | Task 2 Step 5 `EvaluateElementNorm` |
| §3.4 空间索引 | Task 2 Step 5 `BuildPodIndex` |
| §3.5 一致性不变量 | Task 2 Step 6（同一 `EvaluateElementNorm` 供地形与观测视图） |
| §3.6 空场语义 | Task 3 `GetGeology` 空资产返回 nullptr；Task 4 `IsEmpty` 早退 |
| §4.1 局部栅格 | Task 4 Step 4 |
| §4.2 参数 `CellsPerPod` | Task 4 Step 5 |
| §4.3 成本 | Task 6 Step 5 记录耗时 |
| §4.4 接口改动 | Task 4 Step 3–6 |
| §4.5 看见重叠 | Task 6 Step 5 验收 3 |
| §5 编辑器预览 | Task 5 |
| §6.1 删除清单 | Task 3 Step 3–4、8；Task 4 Step 6 |
| §6.2 已知限制 | Task 6 Step 3 README |
| §6.3 顺序 | 任务顺序即此顺序 |
| §7 测试 | Task 2 Step 1、Task 3 Step 7、Task 4 Step 1 |
| §8 初始数据 | Task 6 Step 1 `FillEarthPreset` |
| §9 验收 | Task 6 Step 5 |
| §10 风险 | 见下 |

**风险对照**

| 风险 | 计划里的处置 |
|---|---|
| 地形每体素多一次哈希查询 | Task 2 的索引只有一次 `TMap::Find`，且不在囊内不调 Perlin；Task 6 Step 5 记录耗时 |
| `SetIsTemporarilyHiddenInEditor` 在 OFPA 下的行为 | Task 6 Step 5 验收 3 用 `git status` 直接验证 |
| 预设指针属性触发 VoxelFree ensure | Task 3 Step 7 的 `GeneratorParametersSupported` 测试 |
| `PodDepthBias > 0` 改变全部矿囊位置 | 默认 0；README 注明 |

**占位符扫描**：无 TBD / TODO /「类似上文」；每个代码步骤都有可粘贴的完整内容（Task 2 Step 6 的「旧循环体逐字照抄现有实现」指向的是**被删前就存在**的代码，不是待写代码）。

**类型一致性**：`BuildFromPreset(const UPlanetGeologyPreset&, double)`、`SampleElementNormAtNormalized(int32, const FVector&)`、`GetPods(int32)`、`CellsPerPod`、`GeologyAsset` 在产生它的任务与消费它的任务之间逐字一致。
