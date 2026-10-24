# 行星地质系统实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给体素行星加一层由 JSON 定义的地质场：各元素独立的噪声密度场合并成物质含量分布，既轻微改变地形几何，又按混合后的元素颜色叠到地表材质上。

**Architecture:** 按设计文档方案 B，新增一个与地形并列的 `FPlanetGeologyField`（只依赖 Core / Json / 插件 FastNoise），通过 `FVoxelPlanetTerrainContext` 注入噪声地形实例。密度场、区间查询、地表查询、材质四个接入点都走同一个只读场；JSON 是唯一编辑入口，失败一律退化为空场。

**Tech Stack:** UE 5.8.3、C++、VoxelFree 插件（`FVoxelFastNoise`、`FVoxelGeneratorInstance`、`FVoxelMaterialBuilder`）、UE `Json` 模块、无头自动化测试。

**Spec:** `docs/superpowers/specs/2026-10-24-planet-geology-design.md`

## Global Constraints

- 工作树：`F:/workspace/ue/StellarEra/.claude/worktrees/planet-geology`（分支 `feat/planet-geology`）。所有命令都在这里跑，**不要 `cd` 回主仓库**。
- 不得修改 `Plugins/VoxelFree` 与 `Plugins/VibeUE`。自定义代码只写进 `Source/{StellarEra,StellarEraCore,StellarEraPlanet,StellarEraVehicle}`。
- `StellarEraVehicle` 不得依赖 `StellarEra`；本计划只在 `StellarEraPlanet` 内加代码。
- 含中文的 `.h`/`.cpp` 必须存成 **UTF-8 with BOM**。
- 不要统一换行符；仓库本来就是 CRLF/LF 混合。
- 构建前关闭编辑器（Live Coding 锁 DLL）。用捆绑 dotnet 直调 UBT，**cwd 在项目外**，不要用 `Build.bat`：

```bash
cd /f/workspace && \
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DOTNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/.claude/worktrees/planet-geology/StellarEra.uproject" \
  -WaitMutex -FromMsBuild
```

成功判据：输出里 `Result: Succeeded`。若 `Result: Failed`，先看是编译错误还是只有 DLL 链接失败。

- 跑测试（编辑器开着也能跑）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/planet-geology/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geology" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/.claude/worktrees/planet-geology/Saved/Automation/geology.log"
```

读日志末尾：应出现 `Automation Test Queue Empty`，且没有 `Result={Fail}`。`-unattended` 下的 Warning 不影响判定。

- 提交必须带 pathspec，否则索引里的常驻暂存文件会被一起吞掉：

```bash
git add -- <paths>
git commit -m "<message>" -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- <paths>
```

- 单位：深度用**体素**，以基准球面为 0、向行星内部为正；`Radius`/`Amplitude` 是体素；颜色是 0~1 的 `FLinearColor`。

## 文件结构

新增：

| 文件 | 职责 |
|---|---|
| `Source/StellarEraPlanet/Public/PlanetGeologyTypes.h` | 元素定义、采样结果、外观参数、加载诊断 |
| `Source/StellarEraPlanet/Public/PlanetGeologyField.h` | `FPlanetGeologyField`：加载 / 采样 / 几何增量 / 预设列表 |
| `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp` | JSON 解析 + 数学实现 |
| `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp` | 全部自动化测试 |
| `Content/Geology/Elements.json` | 默认 6 种元素 |
| `Content/Geology/Planets/earth.json` | 默认行星预设 |
| `Content/Geology/README.md` | schema 与改法 |

修改：

| 文件 | 改动 |
|---|---|
| `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` | 加 `Json` 私有依赖 |
| `Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h` | 上下文加 `Geology` 字段 |
| `Source/StellarEraPlanet/Public/VoxelPlanetNoiseTerrain.h` | 实例保存 `Geology` |
| `Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp` | 密度 / 区间 / 地表 / 材质四处接入 |
| `Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h` | `GeologyPreset`、`MakeTerrainContext()`、`GetGeology()`、`InvalidateGeology()` |
| `Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp` | 上下文单一入口 + 按预设懒加载缓存 |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` | `GeologyPreset`、`GetGeologyPresetOptions()`、`ReloadGeology()` |
| `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` | 推参数、混哈希、地表查询走 `MakeTerrainContext()`、控制台命令 |
| `Config/DefaultGame.ini` | `Geology` 目录按 UFS 打包 |

---

### Task 1: 地质类型与 JSON 解析

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetGeologyTypes.h`
- Create: `Source/StellarEraPlanet/Public/PlanetGeologyField.h`
- Create: `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`
- Create: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs:16`

**Interfaces:**
- Consumes: 无。
- Produces:
  - `FPlanetGeologyElement`、`FPlanetGeologySample`、`FPlanetGeologyAppearance`、`FPlanetGeologyLoadReport`
  - `FPlanetGeologyField::LoadFromString(const FString&, const FString&, FPlanetGeologyLoadReport&) -> TSharedPtr<const FPlanetGeologyField>`
  - `FPlanetGeologyField::NumElements() / GetTotalAbundance() / GetMaxReachDepth() / GetMaxGeometryDelta() / IsEmpty()`（Task 2/3/4 会用）

- [ ] **Step 1: 写解析测试（先红）**

创建 `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`，文件存 UTF-8 BOM：

```cpp
// 地质场的自动化测试：解析、确定性、边界、区间覆盖、材质颜色。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "VoxelItemStack.h"
#include "VoxelMaterial.h"
#include "PlanetGeologyField.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyTest
{
	// 两个已知元素 + 一条未知元素 + 一条非法层厚。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    {
      "id": "iron",
      "displayName": "铁",
      "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 }
    },
    {
      "id": "copper",
      "displayName": "铜",
      "color": [0.9, 0.4, 0.1],
      "noise": { "frequency": 4.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 202, "depthScale": 0.3 }
    }
  ]
}
)JSON");

	const TCHAR* PlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": 0.22, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {
    "iron":   { "abundance": 0.75, "peakDepth": -30, "layerThickness": 20 },
    "copper": { "abundance": 0.45, "peakDepth": -65, "layerThickness": 42 },
    "gold":   { "abundance": 0.9,  "peakDepth": -10, "layerThickness": 10 },
    "zinc":   { "abundance": 0.5,  "peakDepth": -10, "layerThickness": 0 }
  }
}
)JSON");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyParseTest,
	"StellarEra.Planet.Geology.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyParseTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(ElementsJson, PlanetJson, Report);

	TestNotNull(TEXT("加载返回非空"), Field.Get());
	if (!Field.IsValid())
	{
		return false;
	}

	TestFalse(TEXT("合法输入不应产生 error"), Report.HasErrors());
	TestEqual(TEXT("只接受两个已知元素"), Field->NumElements(), 2);
	// 用容差比较而不是 TestEqual：0.75f + 0.45f 在二进制里不是精确的 1.2f。
	TestTrue(TEXT("丰度和 = 0.75 + 0.45"),
		FMath::IsNearlyEqual(Field->GetTotalAbundance(), 1.2f, 1e-4f));
	TestTrue(TEXT("最大触达深度 = max(30+60, 65+126)"),
		FMath::IsNearlyEqual(Field->GetMaxReachDepth(), 191.f, 1e-4f));
	TestTrue(TEXT("几何振幅"),
		FMath::IsNearlyEqual(Field->GetMaxGeometryDelta(), 1.5f, 1e-4f));

	// 未知 id 与非法层厚都必须留下可定位的警告，而不是静默消失。
	bool bSawUnknown = false;
	bool bSawBadThickness = false;
	for (const FString& Warning : Report.Warnings)
	{
		bSawUnknown |= Warning.Contains(TEXT("gold"));
		bSawBadThickness |= Warning.Contains(TEXT("zinc"));
	}
	TestTrue(TEXT("未知元素有警告"), bSawUnknown);
	TestTrue(TEXT("layerThickness <= 0 有警告"), bSawBadThickness);

	// 空元素表是合法的「没有矿」。
	FPlanetGeologyLoadReport EmptyReport;
	const TSharedPtr<const FPlanetGeologyField> EmptyField = FPlanetGeologyField::LoadFromString(
		TEXT(R"JSON({"version":1,"elements":[]})JSON"), PlanetJson, EmptyReport);
	TestFalse(TEXT("空元素表不是错误"), EmptyReport.HasErrors());
	TestTrue(TEXT("空元素表产出空场"), EmptyField.IsValid() && EmptyField->IsEmpty());

	// 语法错误退化成空场，而不是崩溃或让行星消失。
	FPlanetGeologyLoadReport BadReport;
	const TSharedPtr<const FPlanetGeologyField> BadField =
		FPlanetGeologyField::LoadFromString(TEXT("{"), PlanetJson, BadReport);
	TestTrue(TEXT("语法错误有 error"), BadReport.HasErrors());
	TestTrue(TEXT("坏数据退化为空场"), BadField.IsValid() && BadField->IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 加 `Json` 依赖并构建，确认红**

修改 `Source/StellarEraPlanet/StellarEraPlanet.Build.cs:16`，在 `PublicDependencyModuleNames` 之后加：

```csharp
		// JSON 只在 PlanetGeologyField.cpp 里用，不需要向下游传递。
		PrivateDependencyModuleNames.AddRange(new string[] { "Json" });
```

运行构建命令。预期：编译失败，错误指向 `PlanetGeologyField.h` 不存在（测试先于实现）。

- [ ] **Step 3: 写类型头**

创建 `Source/StellarEraPlanet/Public/PlanetGeologyTypes.h`（UTF-8 BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质系统的值类型：元素定义、采样结果、外观参数、加载诊断。

#pragma once

#include "CoreMinimal.h"

/**
 * 元素表的单条定义。
 *
 * 刻意不是 USTRUCT：它只在地质系统内部按值传递，不进反射、不进蓝图。
 * JSON 才是唯一的编辑入口。
 */
struct FPlanetGeologyElement
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;

	// --- 行星级参数（解析行星 JSON 时填入） ---
	float Abundance = 0.f;
	float PeakDepth = 0.f;
	float LayerThickness = 16.f;
	/** |peakDepth| + 3 * layerThickness，解析时算好，供采样早退。 */
	float ReachDepth = 0.f;

	// --- 噪声参数（来自元素表） ---
	float Frequency = 3.f;
	int32 Octaves = 2;
	float Lacunarity = 2.f;
	float Gain = 0.5f;
	int32 Seed = 0;
	float DepthScale = 0.3f;
};

/** 一次地质采样的结果。TotalNorm / Purity 均已归一化到 [0,1]。 */
struct FPlanetGeologySample
{
	float TotalNorm = 0.f;
	float Purity = 0.f;
	FLinearColor MineralColor = FLinearColor::Black;
};

/** 行星级的外观参数（来自 Planets/<preset>.json 的 appearance 段）。 */
struct FPlanetGeologyAppearance
{
	float ColorThreshold = 0.22f;
	float FadeRange = 0.15f;
	float PurityForFullColor = 0.7f;
};

/** 解析诊断。Errors 非空时整份配置按「没有矿」处理。 */
struct FPlanetGeologyLoadReport
{
	TArray<FString> Warnings;
	TArray<FString> Errors;

	bool HasErrors() const { return Errors.Num() > 0; }
};
```

- [ ] **Step 4: 写地质场头**

创建 `Source/StellarEraPlanet/Public/PlanetGeologyField.h`（UTF-8 BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星地质场：元素表 + 行星参数合成的只读采样场。

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "PlanetGeologyTypes.h"

/**
 * 一颗行星的地质场。
 *
 * 由 Content/Geology/Elements.json（元素表）与 Planets/<preset>.json（行星参数）
 * 合成，构造后完全不可变；生成器实例用 TSharedPtr<const ...> 在后台线程共享。
 *
 * 设计见 docs/superpowers/specs/2026-10-24-planet-geology-design.md。
 */
class FPlanetGeologyField
{
public:
	FPlanetGeologyField() = default;

	/**
	 * 从磁盘加载。PresetName 只能是 Planets 下的文件名（不含扩展名）。
	 * 任何失败都返回一个**有效的空场**，错误写进 OutReport。
	 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromFiles(
		const FString& PresetName, FPlanetGeologyLoadReport& OutReport);

	/** 从内存加载，给测试和热重载用。 */
	static TSharedPtr<const FPlanetGeologyField> LoadFromString(
		const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport);

	/** Content/Geology/Planets 下所有预设名（排序后），给编辑器下拉框用。 */
	static TArray<FString> ListPresetNames();

	/** 指定方向与深度（体素，基准球面为 0，向内为正）的成分。 */
	FPlanetGeologySample Sample(const FVector& UnitDir, double Depth) const;

	/**
	 * 矿脉造成的地表外凸量（体素，∈ [0, GetMaxGeometryDelta()]）。
	 * 密度场和地表查询必须都走它，否则脚下与眼睛看到的不是同一个面。
	 */
	float SampleGeometryDelta(const FVector& UnitDir, double Depth) const;

	float GetMaxGeometryDelta() const { return GeometryAmplitude; }
	float GetMaxReachDepth() const { return MaxReachDepth; }
	const FPlanetGeologyAppearance& GetAppearance() const { return Appearance; }
	bool IsEmpty() const { return Elements.Num() == 0; }
	int32 NumElements() const { return Elements.Num(); }
	float GetTotalAbundance() const { return TotalAbundance; }

private:
	struct FElementRuntime
	{
		FPlanetGeologyElement Definition;
		FVoxelFastNoise Noise;
	};

	struct FAccumulation
	{
		double Total = 0.0;
		double MaxRaw = 0.0;
	};

	/** 单遍累加；bComputeColor 为假时跳过加权颜色，供几何查询用。 */
	void Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
		FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor) const;

	FPlanetGeologyAppearance Appearance;
	float GeometryAmplitude = 1.5f;
	float TotalAbundance = 0.f;
	float MaxReachDepth = 0.f;
	TArray<FElementRuntime> Elements;
};
```

- [ ] **Step 5: 写解析实现（采样方法先留桩）**

创建 `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`（UTF-8 BOM）：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// JSON 解析与地质场采样。

#include "PlanetGeologyField.h"

#include "Dom/JsonObject.h"
#include "FastNoise/VoxelFastNoise.inl"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	constexpr double ThreeSigma = 3.0;

	bool ParseJsonObject(const FString& Json, const TCHAR* SourceName,
		TSharedPtr<FJsonObject>& OutRoot, FPlanetGeologyLoadReport& Report)
	{
		if (Json.IsEmpty())
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: 内容为空"), SourceName));
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
		{
			Report.Errors.Add(FString::Printf(TEXT("%s: JSON 语法错误"), SourceName));
			return false;
		}
		return true;
	}

	bool TryReadObject(const TSharedPtr<FJsonValue>& Value, const TSharedPtr<FJsonObject>*& OutObject)
	{
		OutObject = nullptr;
		return Value.IsValid()
			&& Value->TryGetObject(OutObject)
			&& OutObject
			&& OutObject->IsValid();
	}

	void ReadVersion(const TSharedPtr<FJsonObject>& Root, const TCHAR* SourceName,
		FPlanetGeologyLoadReport& Report)
	{
		double Version = 1.0;
		if (!Root->TryGetNumberField(TEXT("version"), Version))
		{
			return;
		}
		if (!FMath::IsNearlyEqual(Version, 1.0))
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s: version %g != 1，仍按当前 schema 解析"), SourceName, Version));
		}
	}

	float ReadClampedFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName,
		float Default, float Min, float Max, const FString& Context,
		FPlanetGeologyLoadReport& Report)
	{
		double Raw = 0.0;
		if (!Object->TryGetNumberField(FieldName, Raw))
		{
			return Default;
		}

		const float Value = float(Raw);
		const float Clamped = FMath::Clamp(Value, Min, Max);
		if (!FMath::IsNearlyEqual(Value, Clamped))
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s.%s = %f 越界，夹到 [%f, %f]"), *Context, FieldName, Value, Min, Max));
		}
		return Clamped;
	}

	int32 ReadClampedInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName,
		int32 Default, int32 Min, int32 Max, const FString& Context,
		FPlanetGeologyLoadReport& Report)
	{
		double Raw = 0.0;
		if (!Object->TryGetNumberField(FieldName, Raw))
		{
			return Default;
		}

		const int32 Value = FMath::RoundToInt(Raw);
		const int32 Clamped = FMath::Clamp(Value, Min, Max);
		if (Value != Clamped)
		{
			Report.Warnings.Add(FString::Printf(
				TEXT("%s.%s = %d 越界，夹到 [%d, %d]"), *Context, FieldName, Value, Min, Max));
		}
		return Clamped;
	}

	bool ParseElementTable(const TSharedPtr<FJsonObject>& Root,
		TMap<FString, FPlanetGeologyElement>& OutTable, FPlanetGeologyLoadReport& Report)
	{
		ReadVersion(Root, TEXT("Elements.json"), Report);

		const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
		if (!Root->TryGetArrayField(TEXT("elements"), Elements) || !Elements)
		{
			Report.Errors.Add(TEXT("Elements.json: 缺少 elements 数组"));
			return false;
		}

		for (int32 Index = 0; Index < Elements->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
			if (!TryReadObject((*Elements)[Index], ObjectPtr))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json[%d]: 不是对象，跳过"), Index));
				continue;
			}
			const TSharedPtr<FJsonObject>& Object = *ObjectPtr;

			FString Id;
			if (!Object->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty())
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json[%d]: 缺少 id，跳过"), Index));
				continue;
			}
			if (OutTable.Contains(Id))
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("Elements.json: 重复 id '%s'，保留第一条"), *Id));
				continue;
			}

			FPlanetGeologyElement Element;
			Element.Id = Id;
			Object->TryGetStringField(TEXT("displayName"), Element.DisplayName);

			const TArray<TSharedPtr<FJsonValue>>* ColorValues = nullptr;
			if (Object->TryGetArrayField(TEXT("color"), ColorValues) && ColorValues && ColorValues->Num() >= 3)
			{
				double R = 1.0;
				double G = 1.0;
				double B = 1.0;
				if ((*ColorValues)[0]->TryGetNumber(R)
					&& (*ColorValues)[1]->TryGetNumber(G)
					&& (*ColorValues)[2]->TryGetNumber(B))
				{
					Element.Color = FLinearColor(
						FMath::Clamp(float(R), 0.f, 1.f),
						FMath::Clamp(float(G), 0.f, 1.f),
						FMath::Clamp(float(B), 0.f, 1.f),
						1.f);
				}
			}

			const TSharedPtr<FJsonObject>* NoisePtr = nullptr;
			if (Object->TryGetObjectField(TEXT("noise"), NoisePtr) && NoisePtr && NoisePtr->IsValid())
			{
				const TSharedPtr<FJsonObject>& Noise = *NoisePtr;
				const FString Context = FString::Printf(TEXT("Elements.json[%s].noise"), *Id);
				Element.Frequency = ReadClampedFloat(Noise, TEXT("frequency"),
					Element.Frequency, 0.01f, 64.f, Context, Report);
				Element.Lacunarity = ReadClampedFloat(Noise, TEXT("lacunarity"),
					Element.Lacunarity, 1.f, 8.f, Context, Report);
				Element.Gain = ReadClampedFloat(Noise, TEXT("gain"),
					Element.Gain, 0.f, 1.f, Context, Report);
				Element.DepthScale = ReadClampedFloat(Noise, TEXT("depthScale"),
					Element.DepthScale, 0.01f, 1.f, Context, Report);
				Element.Octaves = ReadClampedInt(Noise, TEXT("octaves"),
					Element.Octaves, 1, 6, Context, Report);
				Element.Seed = ReadClampedInt(Noise, TEXT("seed"),
					Element.Seed, MIN_int32, MAX_int32, Context, Report);
			}

			OutTable.Add(Id, Element);
		}
		return true;
	}

	bool ParsePlanet(const TSharedPtr<FJsonObject>& Root,
		const TMap<FString, FPlanetGeologyElement>& Table,
		FPlanetGeologyAppearance& OutAppearance, float& OutGeometryAmplitude,
		TArray<FPlanetGeologyElement>& OutElements, FPlanetGeologyLoadReport& Report)
	{
		ReadVersion(Root, TEXT("Planets/<preset>.json"), Report);

		double GeometryAmplitude = 1.5;
		if (Root->TryGetNumberField(TEXT("geometryAmplitude"), GeometryAmplitude))
		{
			if (GeometryAmplitude < 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("geometryAmplitude %f < 0，按 0 处理"), GeometryAmplitude));
				GeometryAmplitude = 0.0;
			}
		}
		OutGeometryAmplitude = float(GeometryAmplitude);

		const TSharedPtr<FJsonObject>* AppearancePtr = nullptr;
		if (Root->TryGetObjectField(TEXT("appearance"), AppearancePtr) && AppearancePtr && AppearancePtr->IsValid())
		{
			const TSharedPtr<FJsonObject>& Appearance = *AppearancePtr;
			OutAppearance.ColorThreshold = ReadClampedFloat(Appearance, TEXT("colorThreshold"),
				OutAppearance.ColorThreshold, 0.f, 1.f, TEXT("appearance"), Report);
			OutAppearance.FadeRange = ReadClampedFloat(Appearance, TEXT("fadeRange"),
				OutAppearance.FadeRange, 0.001f, 1.f, TEXT("appearance"), Report);
			OutAppearance.PurityForFullColor = ReadClampedFloat(Appearance, TEXT("purityForFullColor"),
				OutAppearance.PurityForFullColor, 0.01f, 1.f, TEXT("appearance"), Report);
		}

		const TSharedPtr<FJsonObject>* ElementsPtr = nullptr;
		if (!Root->TryGetObjectField(TEXT("elements"), ElementsPtr) || !ElementsPtr || !ElementsPtr->IsValid())
		{
			Report.Errors.Add(TEXT("行星 JSON: 缺少 elements 对象"));
			return false;
		}

		// UE 5.8 的 FJsonObject 键是 UE::FSharedString（不是 FString），
		// 取值时显式转成 FString。
		for (const auto& Pair : (*ElementsPtr)->Values)
		{
			const FString ElementId(*Pair.Key);

			const FPlanetGeologyElement* Definition = Table.Find(ElementId);
			if (!Definition)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: 未知元素 '%s'，跳过"), *ElementId));
				continue;
			}

			const TSharedPtr<FJsonObject>* ElementPtr = nullptr;
			if (!Pair.Value.IsValid()
				|| !Pair.Value->TryGetObject(ElementPtr)
				|| !ElementPtr
				|| !ElementPtr->IsValid())
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: 元素 '%s' 的值不是对象，跳过"), *ElementId));
				continue;
			}

			const TSharedPtr<FJsonObject>& ElementObject = *ElementPtr;
			const float Abundance = ReadClampedFloat(ElementObject, TEXT("abundance"),
				0.f, 0.f, 1.f, ElementId, Report);
			if (Abundance <= 0.f)
			{
				continue;
			}

			double PeakDepth = 0.0;
			ElementObject->TryGetNumberField(TEXT("peakDepth"), PeakDepth);

			double LayerThickness = 0.0;
			if (!ElementObject->TryGetNumberField(TEXT("layerThickness"), LayerThickness)
				|| LayerThickness <= 0.0)
			{
				Report.Warnings.Add(FString::Printf(
					TEXT("行星 JSON: '%s' 的 layerThickness 必须 > 0，按不存在处理"), *ElementId));
				continue;
			}

			FPlanetGeologyElement Element = *Definition;
			Element.Abundance = Abundance;
			Element.PeakDepth = float(PeakDepth);
			Element.LayerThickness = float(LayerThickness);
			Element.ReachDepth = float(FMath::Abs(PeakDepth) + ThreeSigma * LayerThickness);
			OutElements.Add(Element);
		}
		return true;
	}

	bool LoadTextFile(const FString& RelativePath, FString& OutText)
	{
		const FString FullPath = FPaths::Combine(FPaths::ProjectContentDir(), RelativePath);
		return FFileHelper::LoadFileToString(OutText, *FullPath);
	}

	bool IsValidPresetName(const FString& PresetName)
	{
		return !PresetName.IsEmpty()
			&& !PresetName.Contains(TEXT("/"))
			&& !PresetName.Contains(TEXT("\\"))
			&& !PresetName.Contains(TEXT(".."));
	}
}

TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::LoadFromString(
	const FString& ElementsJson, const FString& PlanetJson, FPlanetGeologyLoadReport& OutReport)
{
	TSharedPtr<FPlanetGeologyField> Field = MakeShared<FPlanetGeologyField>();

	TSharedPtr<FJsonObject> ElementsRoot;
	if (!ParseJsonObject(ElementsJson, TEXT("Elements.json"), ElementsRoot, OutReport))
	{
		return Field;
	}

	TMap<FString, FPlanetGeologyElement> Table;
	if (!ParseElementTable(ElementsRoot, Table, OutReport))
	{
		return Field;
	}

	TSharedPtr<FJsonObject> PlanetRoot;
	if (!ParseJsonObject(PlanetJson, TEXT("Planets/<preset>.json"), PlanetRoot, OutReport))
	{
		return Field;
	}

	TArray<FPlanetGeologyElement> Definitions;
	FPlanetGeologyAppearance Appearance;
	float GeometryAmplitude = 1.5f;
	if (!ParsePlanet(PlanetRoot, Table, Appearance, GeometryAmplitude, Definitions, OutReport))
	{
		return Field;
	}

	Field->Appearance = Appearance;
	Field->GeometryAmplitude = FMath::Max(0.f, GeometryAmplitude);

	for (const FPlanetGeologyElement& Definition : Definitions)
	{
		FElementRuntime& Runtime = Field->Elements.AddDefaulted_GetRef();
		Runtime.Definition = Definition;
		Runtime.Noise.SetSeed(Definition.Seed);
		Runtime.Noise.SetFractalLacunarity(Definition.Lacunarity);
		// FractalBounding 只在这里算一次；octaves / gain 必须与采样时传进
		// GetPerlinFractal_3D 的值完全一致，否则归一化系数会错位。
		Runtime.Noise.SetFractalOctavesAndGain(Definition.Octaves, Definition.Gain);

		Field->TotalAbundance += Definition.Abundance;
		Field->MaxReachDepth = FMath::Max(Field->MaxReachDepth, Definition.ReachDepth);
	}

	return Field;
}

TSharedPtr<const FPlanetGeologyField> FPlanetGeologyField::LoadFromFiles(
	const FString& PresetName, FPlanetGeologyLoadReport& OutReport)
{
	if (!IsValidPresetName(PresetName))
	{
		OutReport.Errors.Add(FString::Printf(TEXT("地质预设名非法: '%s'"), *PresetName));
		return MakeShared<FPlanetGeologyField>();
	}

	FString ElementsJson;
	if (!LoadTextFile(TEXT("Geology/Elements.json"), ElementsJson))
	{
		OutReport.Errors.Add(TEXT("读取失败: Content/Geology/Elements.json"));
		return MakeShared<FPlanetGeologyField>();
	}

	const FString PlanetRelativePath = FString::Printf(
		TEXT("Geology/Planets/%s.json"), *PresetName);
	FString PlanetJson;
	if (!LoadTextFile(PlanetRelativePath, PlanetJson))
	{
		OutReport.Errors.Add(FString::Printf(
			TEXT("读取失败: Content/%s"), *PlanetRelativePath));
		return MakeShared<FPlanetGeologyField>();
	}

	return LoadFromString(ElementsJson, PlanetJson, OutReport);
}

TArray<FString> FPlanetGeologyField::ListPresetNames()
{
	TArray<FString> PresetNames;

	const FString Directory = FPaths::Combine(
		FPaths::ProjectContentDir(), TEXT("Geology/Planets"));
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(
		FileNames, *FPaths::Combine(Directory, TEXT("*.json")), true, false);

	for (const FString& FileName : FileNames)
	{
		PresetNames.Add(FPaths::GetBaseFilename(FileName));
	}
	PresetNames.Sort();
	return PresetNames;
}

// 采样数学在 Task 2 实现；先留桩让解析测试能单独跑。
FPlanetGeologySample FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth) const
{
	return FPlanetGeologySample();
}

float FPlanetGeologyField::SampleGeometryDelta(const FVector& UnitDir, double Depth) const
{
	return 0.f;
}
```

注意：项目按警告即错误编译，未使用形参可能触发 C4100。**若构建报 C4100，在两个桩函数体开头加**：

```cpp
	(void)UnitDir;
	(void)Depth;
```

- [ ] **Step 6: 构建并跑解析测试**

先构建（关闭编辑器），再跑测试命令。

预期：构建 `Result: Succeeded`；`StellarEra.Planet.Geology.Parse` 通过。

若测试报 `TestEqual 丰度和` 失败，检查是否把未知元素 `gold` 也算进了丰度和（不应算）。

- [ ] **Step 7: 提交**

```bash
git add -- Source/StellarEraPlanet/Public/PlanetGeologyTypes.h \
  Source/StellarEraPlanet/Public/PlanetGeologyField.h \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs
git commit -m "feat(地质): 地质场类型与 JSON 解析" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetGeologyTypes.h \
  Source/StellarEraPlanet/Public/PlanetGeologyField.h \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs
```

---

### Task 2: 采样数学与确定性

**Files:**
- Modify: `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`（替换 Task 1 末尾两个桩）
- Test: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`（追加）

**Interfaces:**
- Consumes: Task 1 的 `FPlanetGeologyField`、`FPlanetGeologyElement`。
- Produces: `Sample(UnitDir, Depth)` 与 `SampleGeometryDelta(UnitDir, Depth)` 的真实实现；后续任务只调用、不再改签名。

- [ ] **Step 1: 追加确定性与边界测试**

在 `PlanetGeologyFieldTests.cpp` 的 `#endif` 之前插入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDeterminismTest,
	"StellarEra.Planet.Geology.Determinism",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDeterminismTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	const TCHAR* ElementsA = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* ElementsB = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 999, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "elements": {
    "iron": { "abundance": 0.8, "peakDepth": -30, "layerThickness": 20 }
  }
}
)JSON");

	FPlanetGeologyLoadReport ReportA;
	FPlanetGeologyLoadReport ReportB;
	const TSharedPtr<const FPlanetGeologyField> FieldA =
		FPlanetGeologyField::LoadFromString(ElementsA, Planet, ReportA);
	const TSharedPtr<const FPlanetGeologyField> FieldB =
		FPlanetGeologyField::LoadFromString(ElementsB, Planet, ReportB);

	const FVector Direction = FVector(0.3, -0.5, 0.8).GetSafeNormal();
	constexpr double Depth = -30.0;

	const FPlanetGeologySample First = FieldA->Sample(Direction, Depth);
	const FPlanetGeologySample Second = FieldA->Sample(Direction, Depth);
	TestEqual(TEXT("同坐标两次采样 TotalNorm 完全一致"), First.TotalNorm, Second.TotalNorm);
	TestEqual(TEXT("同坐标两次采样 Purity 完全一致"), First.Purity, Second.Purity);
	TestEqual(TEXT("同坐标两次采样颜色完全一致"), First.MineralColor, Second.MineralColor);

	const FPlanetGeologySample OtherSeed = FieldB->Sample(Direction, Depth);
	TestTrue(TEXT("不同 noise.seed 得到不同布局"), First.TotalNorm != OtherSeed.TotalNorm);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyBoundsTest,
	"StellarEra.Planet.Geology.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyTest;

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(ElementsJson, PlanetJson, Report);
	if (!Field.IsValid())
	{
		return false;
	}

	const float MaxDelta = Field->GetMaxGeometryDelta();
	FRandomStream Stream(20261024);

	bool bSampleBounds = true;
	bool bDeltaBounds = true;
	for (int32 Index = 0; Index < 2048; ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const double Depth = Stream.FRandRange(-400.0, 80.0);

		const FPlanetGeologySample Sample = Field->Sample(Direction, Depth);
		bSampleBounds &= Sample.TotalNorm >= 0.f && Sample.TotalNorm <= 1.f;
		bSampleBounds &= Sample.Purity >= 0.f && Sample.Purity <= 1.f;

		const float Delta = Field->SampleGeometryDelta(Direction, Depth);
		bDeltaBounds &= Delta >= 0.f && Delta <= MaxDelta;
	}

	TestTrue(TEXT("TotalNorm 与 Purity 都在 [0,1]"), bSampleBounds);
	TestTrue(TEXT("GeometryDelta 在 [0, geometryAmplitude]"), bDeltaBounds);

	return true;
}
```

- [ ] **Step 2: 构建并跑，确认红**

运行构建 + 测试命令。预期：构建成功；`Determinism` **失败**在「不同 noise.seed 得到不同布局」（两个桩都返回 0，所以相等）。`Bounds` 会因桩值恰好满足而通过——真正的判据是 Determinism 的种子断言。

- [ ] **Step 3: 用真实数学替换两个桩**

在 `PlanetGeologyField.cpp` 中，把 Task 1 末尾的两个桩函数整体替换为：

```cpp
FPlanetGeologySample FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth) const
{
	FPlanetGeologySample Result;
	if (Elements.Num() == 0 || TotalAbundance <= 0.f)
	{
		return Result;
	}

	FAccumulation Accumulation;
	FLinearColor WeightedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, true, Accumulation, WeightedColor);

	if (Accumulation.Total <= UE_DOUBLE_KINDA_SMALL_NUMBER)
	{
		return Result;
	}

	Result.TotalNorm = FMath::Clamp(
		float(Accumulation.Total / double(TotalAbundance)), 0.f, 1.f);
	Result.Purity = FMath::Clamp(
		float(Accumulation.MaxRaw / Accumulation.Total), 0.f, 1.f);
	Result.MineralColor = WeightedColor / float(Accumulation.Total);
	return Result;
}

float FPlanetGeologyField::SampleGeometryDelta(const FVector& UnitDir, double Depth) const
{
	if (Elements.Num() == 0 || TotalAbundance <= 0.f)
	{
		return 0.f;
	}
	// 高斯尾部之外没有贡献；绝大多数行星内部体积走这一条。
	if (Depth < -double(MaxReachDepth) || Depth > double(MaxReachDepth))
	{
		return 0.f;
	}

	FAccumulation Accumulation;
	FLinearColor UnusedColor = FLinearColor::Black;
	Accumulate(UnitDir, Depth, false, Accumulation, UnusedColor);

	if (Accumulation.Total <= 0.0)
	{
		return 0.f;
	}

	const float TotalNorm = FMath::Clamp(
		float(Accumulation.Total / double(TotalAbundance)), 0.f, 1.f);
	return GeometryAmplitude * TotalNorm;
}

void FPlanetGeologyField::Accumulate(const FVector& UnitDir, double Depth, bool bComputeColor,
	FAccumulation& OutAccumulation, FLinearColor& OutWeightedColor) const
{
	OutAccumulation = FAccumulation();
	OutWeightedColor = FLinearColor::Black;

	double WeightedR = 0.0;
	double WeightedG = 0.0;
	double WeightedB = 0.0;

	for (const FElementRuntime& Runtime : Elements)
	{
		const FPlanetGeologyElement& Definition = Runtime.Definition;
		const double LayerThickness = FMath::Max(double(Definition.LayerThickness), 1e-3);
		const double DepthRatio =
			(Depth - double(Definition.PeakDepth)) / LayerThickness;
		const double Gaussian = FMath::Exp(-DepthRatio * DepthRatio);

		// 单元素在任一深度上常常没有贡献；先算 exp 再决定是否花一次分形 Perlin。
		if (Gaussian < 0.01)
		{
			continue;
		}

		// SampleZ 用无量纲的深度比：GetPerlinFractal_3D 内部会把三个坐标都乘频率，
		// 直接加体素深度会让噪声随深度剧烈振荡。
		const double SampleZ =
			double(UnitDir.Z) + (Depth / LayerThickness) * double(Definition.DepthScale);

		const double RawNoise = double(Runtime.Noise.GetPerlinFractal_3D(
			v_flt(UnitDir.X), v_flt(UnitDir.Y), v_flt(SampleZ),
			v_flt(Definition.Frequency), Definition.Octaves));

		// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
		const double Noise01 = 0.5 * (RawNoise + 1.0);
		const double Raw = double(Definition.Abundance) * Gaussian * Noise01;

		OutAccumulation.Total += Raw;
		OutAccumulation.MaxRaw = FMath::Max(OutAccumulation.MaxRaw, Raw);

		if (bComputeColor && Raw > 0.0)
		{
			WeightedR += Raw * double(Definition.Color.R);
			WeightedG += Raw * double(Definition.Color.G);
			WeightedB += Raw * double(Definition.Color.B);
		}
	}

	OutWeightedColor = FLinearColor(
		float(WeightedR), float(WeightedG), float(WeightedB), 1.f);
}
```

- [ ] **Step 4: 构建并跑，确认绿**

预期：`StellarEra.Planet.Geology.Parse`、`.Determinism`、`.Bounds` 全部通过。

设计文档 §3.3 里的 `DominantIndex = argmax(raw_i)` 本期**不计算**：v1 没有消费者
（材质只用混合色，查询接口只返回含量与纯度）。等接真实矿物材质需要「主矿物」
时再加，避免为一个没人读的字段每次采样多存一个索引。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
git commit -m "feat(地质): 元素密度合并、纯度与几何增量" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
```

---

### Task 3: 地形密度、区间与地表查询接入

**Files:**
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h:11-35`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetNoiseTerrain.h:168-192`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp:99-111, 145-149, 263-266`
- Test: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`（追加）

**Interfaces:**
- Consumes: `FPlanetGeologyField::SampleGeometryDelta`、`GetMaxGeometryDelta`、`GetMaxReachDepth`。
- Produces:
  - `FVoxelPlanetTerrainContext::Geology`（`TSharedPtr<const FPlanetGeologyField>`）
  - `FVoxelPlanetNoiseTerrainInstance` 的密度与区间包含矿脉几何
  - `GetSurfaceRadiusVoxels` 包含矿脉几何

- [ ] **Step 1: 追加区间覆盖与地表一致性测试**

在测试文件的 `#endif` 之前插入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyRangeCoverageTest,
	"StellarEra.Planet.Geology.RangeCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyRangeCoverageTest::RunTest(const FString& Parameters)
{
	// 故意用大几何振幅，让「区间漏掉 Geology 项」立刻暴露成真失败。
	const TCHAR* Elements = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 0.5, "octaves": 1, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 12.0,
  "appearance": { "colorThreshold": 0.0, "fadeRange": 0.001, "purityForFullColor": 0.01 },
  "elements": {
    "iron": { "abundance": 1.0, "peakDepth": 0, "layerThickness": 200 }
  }
}
)JSON");

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(Elements, Planet, Report);

	FVoxelPlanetTerrainContext Context;
	Context.Radius = 100.f;
	Context.Geology = Field;

	// Amplitude 设为 0：把「基础起伏振幅」带来的区间余量清零，让密度下界只剩
	// 地质增量一个未知量。否则基础振幅 8 体素的保守余量（最多 8/5=1.6 的密度
	// 余量）会盖住矿脉增量，区间测试即使漏项也可能碰巧通过。
	FPlanetNoiseParams Params;
	Params.Amplitude = 0.f;
	Params.SeaLevel = -0.35f;
	const FVoxelPlanetNoiseTerrainInstance Instance(Context, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FRandomStream Stream(424242);
	bool bRangeCovers = true;
	FString FailureDetail;

	// 仿照网格化器：只检查贴着地表的 32³ 区块，而不是包住球心的整块包围盒。
	for (int32 BoxIndex = 0; BoxIndex < 48 && bRangeCovers; ++BoxIndex)
	{
		const FVector Direction = Stream.GetUnitVector();
		const FVector Center =
			Direction * (100.0 + Stream.FRandRange(-8.0, 8.0));
		const FIntVector BoxMin(
			FMath::FloorToInt(Center.X) - 16,
			FMath::FloorToInt(Center.Y) - 16,
			FMath::FloorToInt(Center.Z) - 16);
		const FIntVector BoxMax(BoxMin.X + 31, BoxMin.Y + 31, BoxMin.Z + 31);
		const FVoxelIntBox Bounds(BoxMin, BoxMax);
		const TVoxelRange<v_flt> Range = Instance.GetValueRangeImpl(Bounds, 0, Items);

		for (int32 SampleIndex = 0; SampleIndex < 256; ++SampleIndex)
		{
			const FVector P(
				Stream.FRandRange(double(BoxMin.X), double(BoxMax.X + 1)),
				Stream.FRandRange(double(BoxMin.Y), double(BoxMax.Y + 1)),
				Stream.FRandRange(double(BoxMin.Z), double(BoxMax.Z + 1)));

			const v_flt Value = Instance.GetValueImpl(
				v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items);

			if (Value < Range.Min || Value > Range.Max)
			{
				bRangeCovers = false;
				FailureDetail = FString::Printf(
					TEXT("盒 %d: 值 %.4f 落在区间 [%.4f, %.4f] 之外"),
					BoxIndex, double(Value), double(Range.Min), double(Range.Max));
				break;
			}
		}
	}

	TestTrue(*FString::Printf(TEXT("GetValueRangeImpl 覆盖所有采样值 %s"), *FailureDetail), bRangeCovers);

	// 地表查询必须与密度场落在同一个面上：否则角色出生会陷进矿脉凸起里。
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	const FPlanetNoiseParams TerrainParams = Terrain->MakeParams();
	const FVoxelPlanetNoiseTerrainInstance SurfaceInstance(Context, TerrainParams);

	bool bSurfaceConsistent = true;
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const float SurfaceRadius = Terrain->GetSurfaceRadiusVoxels(Context, Direction);
		const FVector P = Direction * double(SurfaceRadius);
		const double Value = double(SurfaceInstance.GetValueImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items));

		// 密度 = (距离 - 地表)/5；允许 float 半径带来的一点误差。
		if (FMath::Abs(Value) > 0.1)
		{
			bSurfaceConsistent = false;
			break;
		}
	}
	TestTrue(TEXT("地表查询处的密度约为 0"), bSurfaceConsistent);

	return bRangeCovers && bSurfaceConsistent;
}
```

- [ ] **Step 2: 先做密度与地表接入（区间故意不改）**

修改 `Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h`：在 `class FVoxelGeneratorInstance;` 后加：

```cpp
class FPlanetGeologyField;
```

在 `FVoxelPlanetTerrainContext` 的 `BaseColor` 后加：

```cpp
	/**
	 * 这颗行星的地质场；为空 = 没有矿。
	 *
	 * 只读指针，构造后不可变，因此可以安全地在后台体素线程上共享。
	 */
	TSharedPtr<const FPlanetGeologyField> Geology;
```

修改 `Source/StellarEraPlanet/Public/VoxelPlanetNoiseTerrain.h`：在 `FVoxelPlanetNoiseTerrainInstance` 类之前加前向声明：

```cpp
class FPlanetGeologyField;
```

在私有成员 `Params` 后加：

```cpp
	/** 与 Context.Geology 同一份只读场；为空 = 没有矿。 */
	const TSharedPtr<const FPlanetGeologyField> Geology;
```

修改 `Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp`：

1. 顶部加 include：

```cpp
#include "PlanetGeologyField.h"
```

2. 构造函数初始化列表末尾加 `, Geology(Context.Geology)`（必须与成员声明顺序一致）：

```cpp
FVoxelPlanetNoiseTerrainInstance::FVoxelPlanetNoiseTerrainInstance(
	const FVoxelPlanetTerrainContext& Context, const FPlanetNoiseParams& InParams)
	// 宿主指针传空：它只是序列化用的软引用，而本实例从不被序列化。
	: Super(nullptr)
	, Radius(Context.Radius)
	, Center(Context.Center)
	, Params(InParams)
	, Geology(Context.Geology)
{
}
```

3. 在 `GetValueImpl` 中**保留**第 145 行 `const double Distance = Relative.Size();`，把它之后到 `return v_flt(Value);` 之前的第 146-149 行替换为：

```cpp
	const FVector UnitDir = GetUnitDirection(Relative);
	const double BaseSurface =
		double(Radius) + double(Params.Amplitude) * double(ComputePlanetOffset(Params, UnitDir));

	// 深度以基准球面为 0、向内为正。几何增量只往外推，不挖坑。
	const double GeometryDelta = Geology.IsValid()
		? double(Geology->SampleGeometryDelta(UnitDir, double(Radius) - Distance))
		: 0.0;

	const double Value = (Distance - BaseSurface - GeometryDelta) / DensityScale;
```

4. 把 `GetSurfaceRadiusVoxels` 第 107-110 行（从 `// 与密度场调用同一个 ComputePlanetOffset` 那句注释到 `return` 行）替换为：

```cpp
	// 与密度场调用同一个 ComputePlanetOffset 和同一个 SampleGeometryDelta。
	// 角色脚下的地面和眼睛看到的地面必须是同一个面。
	const FPlanetNoiseParams Params = MakeParams();
	const double BaseSurface =
		double(Context.Radius) + double(Params.Amplitude) * double(ComputePlanetOffset(Params, Direction));

	double GeometryDelta = 0.0;
	if (Context.Geology.IsValid())
	{
		// 真实地表满足 R_s = Base + delta(R - R_s)。用不动点迭代一次，
		// 把 delta 自身的 1~2 体素偏移也收进来。
		const double Depth0 = double(Context.Radius) - BaseSurface;
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(Direction, Depth0));
		GeometryDelta = double(Context.Geology->SampleGeometryDelta(
			Direction, Depth0 - GeometryDelta));
	}

	return float(BaseSurface + GeometryDelta);
```

- [ ] **Step 3: 构建并跑，确认区间测试红**

运行构建 + 测试命令。预期：构建成功；`RangeCoverage` **失败**，失败详情类似「值 -0.5 落在区间 [-0.2, ...] 之外」——密度已经包含矿脉，区间还没有。`地表查询处的密度约为 0` 应通过。

若区间测试没红，检查测试 JSON 的 `geometryAmplitude` 是否仍是 12、`abundance` 是否 1.0。

- [ ] **Step 4: 补区间下界**

在 `GetValueRangeImpl` 中，把第 263-266 行（`const double MinValue =` 到 `const double MaxValue = ...;`）替换为：

```cpp
	// 地质增量只增大表面半径，因此只压缩最小密度那一侧。
	// 漏掉这一项会在矿脉处出洞，而且是静默的 —— RangeCoverage 测试兜底。
	const double MaxGeometryDelta =
		Geology.IsValid() ? double(Geology->GetMaxGeometryDelta()) : 0.0;

	const double MinValue =
		(MinDistance - double(Radius) - double(Params.Amplitude) * MaxOffset - MaxGeometryDelta)
		/ DensityScale;
	const double MaxValue =
		(MaxDistance - double(Radius) - double(Params.Amplitude) * MinOffset) / DensityScale;
```

- [ ] **Step 5: 构建并跑，确认绿**

预期：`Parse`、`Determinism`、`Bounds`、`RangeCoverage` 全部通过。

- [ ] **Step 6: 提交**

```bash
git add -- Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h \
  Source/StellarEraPlanet/Public/VoxelPlanetNoiseTerrain.h \
  Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
git commit -m "feat(地质): 矿脉接入密度场、区间与地表查询" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/VoxelPlanetTerrainModule.h \
  Source/StellarEraPlanet/Public/VoxelPlanetNoiseTerrain.h \
  Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
```

---

### Task 4: 材质颜色混合

**Files:**
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp:9-25, 160-218`
- Test: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`（追加）

**Interfaces:**
- Consumes: `FPlanetGeologyField::Sample`、`GetAppearance`。
- Produces: `MakeVoxelMaterial(const FPlanetGeologySample&, const FLinearColor&)`（文件内静态，未来 MultiIndex 的唯一出口）。

- [ ] **Step 1: 追加颜色测试**

在测试文件的 `#endif` 之前插入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyColorTest,
	"StellarEra.Planet.Geology.Color",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyColorTest::RunTest(const FString& Parameters)
{
	const TCHAR* Elements = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    { "id": "iron", "color": [0.8, 0.2, 0.2],
      "noise": { "frequency": 3.0, "octaves": 1, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 } }
  ]
}
)JSON");

	// 阈值 0、过渡极窄、纯度门槛极低 → 只要有一点含量就完全用元素色。
	const TCHAR* Planet = TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 0.0,
  "appearance": { "colorThreshold": 0.0, "fadeRange": 0.001, "purityForFullColor": 0.01 },
  "elements": {
    "iron": { "abundance": 1.0, "peakDepth": 0, "layerThickness": 200 }
  }
}
)JSON");

	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromString(Elements, Planet, Report);

	FVoxelPlanetTerrainContext Context;
	Context.Radius = 100.f;
	Context.Geology = Field;

	FVoxelPlanetTerrainContext NoGeoContext;
	NoGeoContext.Radius = 100.f;

	FVoxelPlanetTerrainContext EmptyGeoContext;
	EmptyGeoContext.Radius = 100.f;
	EmptyGeoContext.Geology = MakeShared<FPlanetGeologyField>();

	FPlanetNoiseParams Params;
	Params.Amplitude = 8.f;
	Params.SeaLevel = -0.35f;

	const FVoxelPlanetNoiseTerrainInstance WithGeo(Context, Params);
	const FVoxelPlanetNoiseTerrainInstance WithoutGeo(NoGeoContext, Params);
	const FVoxelPlanetNoiseTerrainInstance WithEmptyGeo(EmptyGeoContext, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	FRandomStream Stream(777);
	FVector LandDirection = FVector::UpVector;
	FVector WaterDirection = FVector::DownVector;
	bool bFoundLand = false;
	bool bFoundWater = false;

	for (int32 Index = 0; Index < 4096 && !(bFoundLand && bFoundWater); ++Index)
	{
		const FVector Direction = Stream.GetUnitVector();
		const double Offset = double(ComputePlanetOffset(Params, Direction));
		// 陆地点还要求矿物含量足够高：否则噪声恰好接近 0 时 TotalNorm 会低于
		// fadeRange，颜色只混了一部分，断言就变成概率性的。
		if (!bFoundLand
			&& Offset > double(Params.SeaLevel) + 0.1
			&& Field->Sample(Direction, 0.0).TotalNorm > 0.5)
		{
			LandDirection = Direction;
			bFoundLand = true;
		}
		if (!bFoundWater && Offset <= double(Params.SeaLevel) + UE_KINDA_SMALL_NUMBER)
		{
			WaterDirection = Direction;
			bFoundWater = true;
		}
	}

	TestTrue(TEXT("找到陆地采样点"), bFoundLand);
	TestTrue(TEXT("找到水面采样点"), bFoundWater);

	// 陆地：depth = 0 落在峰值层，且方向高于海平面 → 完全使用元素色。
	{
		const FVector P = LandDirection * 100.0;
		const FColor Color = WithGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();

		const auto Near = [](uint8 Actual, float Expected)
		{
			return FMath::Abs(int32(Actual) - FMath::RoundToInt(Expected * 255.f)) <= 2;
		};
		TestTrue(TEXT("纯矿地表 R 接近元素色"), Near(Color.R, 0.8f));
		TestTrue(TEXT("纯矿地表 G 接近元素色"), Near(Color.G, 0.2f));
		TestTrue(TEXT("纯矿地表 B 接近元素色"), Near(Color.B, 0.2f));
	}

	// 空场不能改变任何像素的颜色。
	{
		const FVector P = LandDirection * 100.0;
		const FColor Baseline = WithoutGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		const FColor EmptyGeo = WithEmptyGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		TestEqual(TEXT("空场不改变地形底色"), EmptyGeo, Baseline);
	}

	// 水面不叠矿色：与没有地质的实例逐通道一致。
	{
		const FVector P = WaterDirection * 100.0;
		const FColor Baseline = WithoutGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		const FColor WithGeology = WithGeo.GetMaterialImpl(
			v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items).GetColor();
		TestEqual(TEXT("水面不叠矿色"), WithGeology, Baseline);
	}

	return true;
}
```

- [ ] **Step 2: 构建并跑，确认红**

预期：构建成功；`Color` **失败**在「纯矿地表 R 接近元素色」（目前返回地形底色）。

- [ ] **Step 3: 加唯一出口与颜色混合**

在 `VoxelPlanetNoiseTerrain.cpp` 顶部已有的匿名 `namespace` 内（`MixColor` 之后）加：

```cpp
	/**
	 * 材质打包的唯一出口。本期 = RGB 顶点色；未来换 MultiIndex 只改这里，
	 * 地形与地质场的代码都不动。
	 */
	FVoxelMaterial MakeVoxelMaterial(const FPlanetGeologySample& Sample, const FLinearColor& FinalColor)
	{
		// 本期不用 Sample：元素颜色已经折算进 FinalColor。参数留着是为了
		// 将来接真实矿物材质时能直接拿到纯度与混合权重，不必再改调用点。
		(void)Sample;

		FVoxelMaterialBuilder Builder;
		Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);
		Builder.SetColor(FinalColor);
		return Builder.Build();
	}
```

把 `GetMaterialImpl` 第 214-217 行（`FVoxelMaterialBuilder Builder;` 到 `return Builder.Build();`）整段替换为：

```cpp
	FPlanetGeologySample GeologySample;
	if (Geology.IsValid() && Offset > Params.SeaLevel + UE_KINDA_SMALL_NUMBER)
	{
		const double Distance = Relative.Size();
		const double Depth = double(Radius) - Distance;
		GeologySample = Geology->Sample(UnitDir, Depth);

		const FPlanetGeologyAppearance& Appearance = Geology->GetAppearance();
		if (GeologySample.TotalNorm > Appearance.ColorThreshold)
		{
			const float Presence = FMath::Clamp(
				(GeologySample.TotalNorm - Appearance.ColorThreshold)
					/ FMath::Max(Appearance.FadeRange, 1e-4f),
				0.f, 1.f);
			const float PurityMix = FMath::Clamp(
				(GeologySample.Purity - (1.f - Appearance.PurityForFullColor))
					/ FMath::Max(Appearance.PurityForFullColor, 1e-4f),
				0.f, 1.f);
			Color = MixColor(Color, GeologySample.MineralColor, Presence * PurityMix);
		}
	}

	return MakeVoxelMaterial(GeologySample, Color);
```

- [ ] **Step 4: 构建并跑，确认绿**

预期：四个测试全部通过。

若「纯矿地表」三通道只有部分通过：检查测试点是否真的落在陆地（`Offset > SeaLevel`）；必要时把 `LandDirection` 的筛选阈值从 `+0.1` 提高到 `+0.3`，并确认 `depth = 100 - 100 = 0` 落在 `peakDepth = 0` 的高斯峰值。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
git commit -m "feat(地质): 地表材质按元素含量混色" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Private/VoxelPlanetNoiseTerrain.cpp \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
```

---

### Task 5: 生成器与 Actor 管线、重载命令

**Files:**
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h:4-66`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`

**Interfaces:**
- Consumes: `FPlanetGeologyField::LoadFromFiles`、`ListPresetNames`。
- Produces:
  - `UVoxelPlanetGenerator::GeologyPreset`、`MakeTerrainContext()`、`GetGeology()`、`InvalidateGeology()`
  - `AVoxelPlanetActor::GeologyPreset`、`GetGeologyPresetOptions()`、`ReloadGeology()`
  - 控制台命令 `StellarEra.ReloadGeology`

- [ ] **Step 1: 生成器加预设与上下文入口**

在 `Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h` 的 include 区加：

```cpp
// FVoxelPlanetTerrainContext（MakeTerrainContext 的返回类型）需要完整定义。
#include "VoxelPlanetTerrainModule.h"
```

在 `class UVoxelPlanetTerrainModule;` 后加：

```cpp
class FPlanetGeologyField;
```

在 `SurfaceColor` 属性后加：

```cpp
	/** 地质预设名：Content/Geology/Planets/<name>.json。空 = 关闭地质。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology")
	FString GeologyPreset;
```

在 `GetInstance()` 之后、`Terrain` 之前加：

```cpp
	/**
	 * 当前行星的完整地形上下文。**唯一构造点**：密度场与地表查询都从这里拿，
	 * 否则新增字段会出现第三份手工拷贝。
	 */
	FVoxelPlanetTerrainContext MakeTerrainContext();

	/** 按 GeologyPreset 懒加载并缓存；预设为空或加载失败时返回空指针。 */
	TSharedPtr<const FPlanetGeologyField> GetGeology();

	/** 丢弃缓存；下次 GetGeology 重新读盘。 */
	void InvalidateGeology();
```

在私有区 `FallbackTerrain` 之前加：

```cpp
	/** 地质场缓存。键是 GeologyPreset；失败也缓存，避免每个采样点都读盘。 */
	TSharedPtr<const FPlanetGeologyField> GeologyCache;
	FString GeologyCachePreset;
	bool bGeologyCacheValid = false;
```

- [ ] **Step 2: 生成器实现**

`Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp` 顶部加：

```cpp
#include "PlanetGeologyField.h"
```

用下面内容替换整个 `GetInstance()`：

```cpp
TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	UVoxelPlanetTerrainModule* EffectiveTerrain = Terrain;
	if (!EffectiveTerrain)
	{
		// 没挂地形 = 光滑球。这里造一个纯球模块来代理，而不是写一条独立分支：
		// 两条路走同一段代码，就不存在「只在缺模块时才执行、平时没人测」的逻辑。
		if (!FallbackTerrain)
		{
			FallbackTerrain = NewObject<UVoxelPlanetSphereTerrain>(
				this, TEXT("VoxelPlanetFallbackTerrain"));
		}
		EffectiveTerrain = FallbackTerrain;
	}

	// 行星的「在哪、多大」与地质场在这里、也只在这里与模块的「长什么样」交汇。
	return EffectiveTerrain->GetInstance(MakeTerrainContext());
}

FVoxelPlanetTerrainContext UVoxelPlanetGenerator::MakeTerrainContext()
{
	FVoxelPlanetTerrainContext Context;
	Context.Radius = Radius;
	Context.Center = Center;
	Context.BaseColor = SurfaceColor;
	Context.Geology = GetGeology();
	return Context;
}

TSharedPtr<const FPlanetGeologyField> UVoxelPlanetGenerator::GetGeology()
{
	if (GeologyPreset.IsEmpty())
	{
		return nullptr;
	}

	if (bGeologyCacheValid && GeologyCachePreset == GeologyPreset)
	{
		return GeologyCache;
	}

	FPlanetGeologyLoadReport Report;
	GeologyCache = FPlanetGeologyField::LoadFromFiles(GeologyPreset, Report);
	GeologyCachePreset = GeologyPreset;
	bGeologyCacheValid = true;

	for (const FString& Error : Report.Errors)
	{
		UE_LOG(LogTemp, Error, TEXT("行星地质 '%s': %s"), *GeologyPreset, *Error);
	}
	for (const FString& Warning : Report.Warnings)
	{
		UE_LOG(LogTemp, Warning, TEXT("行星地质 '%s': %s"), *GeologyPreset, *Warning);
	}

	return GeologyCache;
}

void UVoxelPlanetGenerator::InvalidateGeology()
{
	GeologyCache.Reset();
	GeologyCachePreset.Reset();
	bGeologyCacheValid = false;
}
```

- [ ] **Step 3: Actor 加属性与重载入口**

在 `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` 的 `Terrain` 属性（当前第 109-110 行）之后、`// 性能` 分节之前加：

```cpp
	/**
	 * 地质预设名：Content/Geology/Planets/<name>.json 的 <name>。
	 * 空 = 这颗行星没有矿。下拉框由 GetGeologyPresetOptions 提供。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Geology",
		meta = (GetOptions = "GetGeologyPresetOptions"))
	FString GeologyPreset = TEXT("earth");

	/** 重新读盘并重建这颗行星：改完 JSON 后调用。 */
	UFUNCTION(BlueprintCallable, Category = "Planet|Geology")
	void ReloadGeology();

	/** 编辑器下拉框的选项：Content/Geology/Planets 下所有预设名。 */
	UFUNCTION(BlueprintPure, Category = "Planet|Geology")
	TArray<FString> GetGeologyPresetOptions() const;
```

放在 `Terrain` 之后、性能分节之前。

- [ ] **Step 4: Actor 实现**

在 `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp` 的 include 区加：

```cpp
#include "PlanetGeologyField.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
```

在 `RebuildPlanet` 中 `PlanetGenerator->Terrain = Terrain;`（当前第 292 行）之后加：

```cpp
	// 地质预设与地形模块一样，是「这颗行星」的参数。
	PlanetGenerator->GeologyPreset = GeologyPreset;
```

用下面内容替换 `GetSurfaceRadiusCmAtDirection` 末尾的上下文构造与返回：

```cpp
	// 上下文由生成器统一构造：地质场必须与密度场那份是同一个，
	// 否则角色脚下的地面和眼睛看到的地面不是同一个面。
	const FVoxelPlanetTerrainContext Context = PlanetGenerator->MakeTerrainContext();

	// 体素空间与世界空间之间只差一个等比缩放，没有旋转，方向可以原样传递。
	return PlanetGenerator->Terrain->GetSurfaceRadiusVoxels(Context, Direction) * VoxelSize;
```

三点说明：

- 函数开头第 421-428 行的 `!PlanetGenerator || !PlanetGenerator->Terrain` 守卫**保留不动**：没有地形模块时密度场走的是纯球代理，本来就不含地质，返回基准球面是一致的。
- `MakeTerrainContext()` 是非 const 方法，而本函数是 const；能调用是因为 `TObjectPtr::operator->` 返回非 const 指针。
- 生成器的 `Radius` / `Center` / `SurfaceColor` 已在 `RebuildPlanet` 第 280-289 行与本 Actor 同步，所以换成 `MakeTerrainContext()` 不改变原有数值，只是多带上 `Geology`。

在 `ComputeParamsHash` 的 `TerrainPath` 混合之后加：

```cpp
	// 地质预设与地形模块同理：只混名字，不混地质场指针。
	const FString GeologyPresetName = GeologyPreset;
	Mix(*GeologyPresetName, GeologyPresetName.Len() * sizeof(TCHAR));
```

在 `InvalidateAndRebuild` 之后加：

```cpp
void AVoxelPlanetActor::ReloadGeology()
{
	// 生成器缓存必须在重建之前作废：重建会重新 GetInstance()，那时才读新 JSON。
	if (PlanetGenerator)
	{
		PlanetGenerator->InvalidateGeology();
	}
	InvalidateAndRebuild();
}

TArray<FString> AVoxelPlanetActor::GetGeologyPresetOptions() const
{
	return FPlanetGeologyField::ListPresetNames();
}
```

在文件末尾加：

```cpp
#if WITH_EDITOR
namespace
{
	// 改完 Content/Geology/*.json 的快捷入口，省掉重启编辑器。
	FAutoConsoleCommandWithWorld ReloadGeologyCommand(
		TEXT("StellarEra.ReloadGeology"),
		TEXT("重新读取 Content/Geology 下的 JSON，并重建场景中所有体素行星。"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World)
			{
				return;
			}

			int32 PlanetCount = 0;
			for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
			{
				It->ReloadGeology();
				++PlanetCount;
			}

			UE_LOG(LogTemp, Log, TEXT("StellarEra.ReloadGeology: 重建了 %d 颗行星"), PlanetCount);
		}));
}
#endif
```

- [ ] **Step 5: 构建**

运行构建命令。预期：`Result: Succeeded`。

常见错误：
- `C4458`（形参名遮蔽基类成员）：本项目按警告即错误编译，新增形参别叫 `Data`。
- `GetOptions` 找不到函数：确认 `GetGeologyPresetOptions` 是 `UFUNCTION()`，且不是 `static`。
- `MakeTerrainContext` 找不到 `FVoxelPlanetTerrainContext`：确认 `VoxelPlanetGenerator.h` 已 include `VoxelPlanetTerrainModule.h`。

- [ ] **Step 6: 跑全部地质测试**

预期：五个测试全部通过（颜色与区间测试不受管线改动影响）。

- [ ] **Step 7: 提交**

```bash
git add -- Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h \
  Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
git commit -m "feat(地质): 生成器与行星 Actor 接入地质预设" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/VoxelPlanetGenerator.h \
  Source/StellarEraPlanet/Private/VoxelPlanetGenerator.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
```

---

### Task 6: 默认内容、打包配置与默认内容测试

**Files:**
- Create: `Content/Geology/Elements.json`
- Create: `Content/Geology/Planets/earth.json`
- Create: `Content/Geology/README.md`
- Modify: `Config/DefaultGame.ini`
- Test: `Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp`（追加）

**Interfaces:**
- Consumes: `FPlanetGeologyField::LoadFromFiles`、`ListPresetNames`。
- Produces: 可被 `GeologyPreset="earth"` 直接使用的默认内容。

- [ ] **Step 1: 追加默认内容测试**

在测试文件的 `#endif` 之前插入：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDefaultContentTest,
	"StellarEra.Planet.Geology.DefaultContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDefaultContentTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field =
		FPlanetGeologyField::LoadFromFiles(TEXT("earth"), Report);

	for (const FString& Error : Report.Errors)
	{
		AddError(Error);
	}
	for (const FString& Warning : Report.Warnings)
	{
		AddWarning(Warning);
	}

	TestTrue(TEXT("earth 预设加载成功"), Field.IsValid() && !Field->IsEmpty());
	TestEqual(TEXT("默认 6 种元素"), Field->NumElements(), 6);
	TestTrue(TEXT("预设列表包含 earth"),
		FPlanetGeologyField::ListPresetNames().Contains(TEXT("earth")));

	return true;
}
```

- [ ] **Step 2: 构建并跑，确认红**

预期：`DefaultContent` **失败**（`Content/Geology` 还不存在，`LoadFromFiles` 返回空场并产生读取错误）。

- [ ] **Step 3: 写 Elements.json**

创建 `Content/Geology/Elements.json`：

```json
{
  "version": 1,
  "elements": [
    {
      "id": "iron",
      "displayName": "铁",
      "color": [0.55, 0.28, 0.16],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 101, "depthScale": 0.3 }
    },
    {
      "id": "copper",
      "displayName": "铜",
      "color": [0.85, 0.45, 0.15],
      "noise": { "frequency": 3.5, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 202, "depthScale": 0.3 }
    },
    {
      "id": "silicon",
      "displayName": "硅",
      "color": [0.55, 0.55, 0.50],
      "noise": { "frequency": 2.5, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 303, "depthScale": 0.3 }
    },
    {
      "id": "titanium",
      "displayName": "钛",
      "color": [0.35, 0.38, 0.45],
      "noise": { "frequency": 4.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 404, "depthScale": 0.3 }
    },
    {
      "id": "uranium",
      "displayName": "铀",
      "color": [0.30, 0.85, 0.20],
      "noise": { "frequency": 4.5, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 505, "depthScale": 0.3 }
    },
    {
      "id": "water",
      "displayName": "水",
      "color": [0.10, 0.35, 0.75],
      "noise": { "frequency": 2.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 606, "depthScale": 0.2 }
    }
  ]
}
```

- [ ] **Step 4: 写 earth.json**

创建 `Content/Geology/Planets/earth.json`：

```json
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": {
    "colorThreshold": 0.22,
    "fadeRange": 0.15,
    "purityForFullColor": 0.7
  },
  "elements": {
    "iron":     { "abundance": 0.75, "peakDepth": 30,  "layerThickness": 20 },
    "copper":   { "abundance": 0.45, "peakDepth": 65,  "layerThickness": 42 },
    "silicon":  { "abundance": 0.60, "peakDepth": 48,  "layerThickness": 31 },
    "titanium": { "abundance": 0.15, "peakDepth": 110, "layerThickness": 72 },
    "uranium":  { "abundance": 0.10, "peakDepth": 190, "layerThickness": 124 },
    "water":    { "abundance": 0.30, "peakDepth": 12,  "layerThickness": 12 }
  }
}
```

R9：深度以基准球面为 0、向内为正（代码与设计文档 §3.1 为准）。

- [ ] **Step 5: 写 README**

创建 `Content/Geology/README.md`：

```markdown
# 行星地质数据

纯文本 JSON，由 `FPlanetGeologyField` 在运行时读取（编辑器与打包后同一套路径：
`FPaths::ProjectContentDir()/Geology/...`）。不是资产，不走内容浏览器。

## 文件

- `Elements.json`：全局元素表。所有行星共享。
- `Planets/<preset>.json`：每颗行星的丰度、层位与外观。行星 Actor 的
  `Planet|Geology|GeologyPreset` 填文件名（不含 `.json`），空 = 没有矿。

## 元素（Elements.json）

| 字段 | 说明 |
|---|---|
| `id` | 唯一非空；行星 JSON 用它引用元素 |
| `displayName` | 只用于日志/未来 UI |
| `color` | `[r,g,b]`，0~1，高纯度时的本色 |
| `noise.frequency` | 单位方向上的噪声频率，越大矿脉越碎 |
| `noise.octaves` | 1~6，分形层数，性能旋钮 |
| `noise.lacunarity` / `noise.gain` | 每层频率倍数 / 振幅衰减 |
| `noise.seed` | 不同元素用不同种子，分布互不相关 |
| `noise.depthScale` | (0,1]，深度混入噪声的强度；太小矿会退化成均匀壳 |

## 行星（Planets/*.json）

| 字段 | 默认 | 说明 |
|---|---|---|
| `geometryAmplitude` | 1.5 | 矿脉让地表外凸的最大体素数；0 = 不影响几何 |
| `appearance.colorThreshold` | 0.22 | 归一化总含量的显现阈值 |
| `appearance.fadeRange` | 0.15 | 从阈值到完全显现的过渡宽度 |
| `appearance.purityForFullColor` | 0.7 | 纯度达到它时颜色完全用矿色 |
| `elements.<id>.abundance` | 0 | 全局丰度，0~1 |
| `elements.<id>.peakDepth` | — | 峰值深度，体素；正 = 向行星内部 |
| `elements.<id>.layerThickness` | — | 高斯层厚，体素，必须 > 0 |

未在行星 JSON 中出现的元素 = 这颗行星没有该元素。

## 改完怎么生效

编辑器控制台执行：

```
StellarEra.ReloadGeology
```

会重新读盘并重建场景中所有体素行星。注意：重建会让已有的手工挖掘编辑与新地形
不再对齐（与改地形参数的行为一致）。
```

- [ ] **Step 6: 打包配置**

在 `Config/DefaultGame.ini` 末尾追加：

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsUFS=(Path="Geology")
```

- [ ] **Step 7: 构建并跑，确认绿**

预期：`DefaultContent` 通过；六个测试全部通过。

- [ ] **Step 8: 提交**

```bash
git add -- Content/Geology/Elements.json Content/Geology/Planets/earth.json \
  Content/Geology/README.md Config/DefaultGame.ini \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
git commit -m "feat(地质): 默认元素表、地球预设与 UFS 打包配置" \
  -m "Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Content/Geology/Elements.json Content/Geology/Planets/earth.json \
  Content/Geology/README.md Config/DefaultGame.ini \
  Source/StellarEraPlanet/Private/PlanetGeologyFieldTests.cpp
```

---

### Task 7: 完整验证

**Files:**
- 无新增；验证 Task 1-6 的产物。

**Interfaces:**
- Consumes: 全部。
- Produces: 可交接的验证证据。

- [ ] **Step 1: 完整构建**

关闭编辑器，运行全局约束里的构建命令。预期：`Result: Succeeded`。记录 `Total execution time`。

- [ ] **Step 2: 全部自动化测试**

把命令里的 `StellarEra.Planet.Geology` 换成 `StellarEra`，跑全部测试。预期：0 个 `Result={Fail}`，日志末尾出现 `Automation Test Queue Empty`。记录通过数。

- [ ] **Step 3: 确认 LFS 资源已落地**

```bash
git lfs ls-files | head -20
```

每行前面的标记应为 `*`（已 smudge）。若出现 `-`，执行 `git lfs checkout` 后再进编辑器。否则 `UniverseMap` 会报 `Package is unloadable`。

- [ ] **Step 4: 编辑器里手工验证**

打开工作树里的 `StellarEra.uproject`，PIE `UniverseMap`：

1. 地表出现零星矿色（铁锈红、铜橙、铀绿等），矿脉处有轻微凸起；
2. 用体素编辑工具挖开一处，新切面出现矿色；
3. 停止 PIE，改 `Content/Geology/Planets/earth.json` 的 `iron.abundance`（如 0.75 → 0.1），控制台执行 `StellarEra.ReloadGeology`，地形与颜色随之变化；改回原值再执行一次。
4. 编辑器**保持前台**，记录改前/改后的 `stat unit`（Frame 时间）。预期：地表薄带内略增，整帧不显著——若出现明显下降，先确认编辑器在前台（后台会被硬限 3 FPS）。

- [ ] **Step 5: 失败时先查证据**

- 地质没生效：日志里搜 `行星地质`，看是否 `读取失败` / `语法错误`；
- 地表出洞：先看 `RangeCoverage` 是否绿，再查 `GetValueRangeImpl` 的 `MaxGeometryDelta` 项；
- 颜色没变：确认该点 `Offset > SeaLevel`，且 `TotalNorm > colorThreshold`；
- 角色悬空/陷地：确认 `GetSurfaceRadiusCmAtDirection` 走的是 `MakeTerrainContext()`。

- [ ] **Step 6: 汇报**

用实际命令输出汇报：构建结果、测试总数/失败数、手工验证观察到的现象。不要只说「应该没问题」。

---

## 附：实施顺序与依赖

```
Task 1 类型+解析
  └→ Task 2 数学
       ├→ Task 3 地形密度/区间/地表
       │    └→ Task 4 材质颜色
       └→ Task 6 默认内容（只依赖解析，可与 3/4 并行）
Task 5 管线（依赖 1、3、6 的文件存在才能手工验证）
Task 7 全量验证（依赖全部）
```

Task 5 只做编译级验证；真正的「预设能生效」在 Task 7 的 `UniverseMap` 手工步骤里验。
