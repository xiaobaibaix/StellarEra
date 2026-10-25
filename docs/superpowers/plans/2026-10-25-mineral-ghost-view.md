# 矿物透视观察（H 键玻璃球）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用 H 键把选中的体素行星变成半透明球壳、把地质场里的矿物显示成球内不透明方块，并支持按元素筛选；彻底删除旧的「切开半球」观察模式。

**Architecture:** 行星 Actor 上挂一个新的 `UPlanetMineralViewComponent`；它在激活时**单独 spawn** 一个 `APlanetMineralViewActor`（不能是子 Actor —— 隐藏行星会把子组件一起藏掉），由后者持有球壳网格 + 每元素一个 ISM。方块位置由纯函数 `PlanetMineralBlocks::Build` 按 `FPlanetGeologyField::Sample` 解析采样得到。控制器与星图 UI 从旧剖面组件切到新组件，最后删除全部旧文件。

**Tech Stack:** UE 5.8.3 / C++20 / Win64，模块 `StellarEraPlanet`（观察体与采样器）+ `StellarEra`（输入与图例 UI），材质用 C++ 直接构造 `UMaterial` 并 `SavePackage`（不依赖 UnrealEd）。

**Spec:** `docs/superpowers/specs/2026-10-25-mineral-ghost-view-design.md`

## Global Constraints

- **模块分层不可破坏**：`StellarEra → StellarEraVehicle → StellarEraPlanet`，`StellarEra → StellarEraCore`；`StellarEraVehicle` **不能**依赖 `StellarEra`。本计划只往 `StellarEraPlanet` 与 `StellarEra` 里加文件。
- **不碰第三方插件**：`Plugins/VoxelFree/`、`Plugins/VibeUE/` 一行都不改；新代码全部进 `Source/` 下的项目模块，并在对应 `Build.cs` 里声明依赖。
- **构建必须关编辑器**（Live Coding 锁 DLL），且必须用捆绑 dotnet 直调 UBT，cwd 在项目外：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
    "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
    StellarEraEditor Win64 Development \
    -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
  ```
  不要用 `Build.bat`（锁机制会让 UBT 报「找不到 .uproject」）。
- **自动化测试命令**（编辑器开着也能跑；`-nullrhi` 必须带）：
  ```bash
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
    "F:/workspace/ue/StellarEra/StellarEra.uproject" \
    -ExecCmds="Automation RunTests StellarEra" \
    -TestExit="Automation Test Queue Empty" \
    -unattended -nopause -nullrhi -nosplash -stdout \
    -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralTests.log"
  ```
  通过与否**只认日志计数**：`grep -c "Result={Success}"` 与 `grep -c "Result={Fail}"`。
  （UE 5.8 的标记就是这两个词：`AutomationControllerManager.cpp:43` 的
  `Test Completed. Result={%s}` 填入 `EAutomationState` 的显示名；
  写成 `Passed` / `Failed` 的 grep 恒为 0。「Automation Test Queue Empty」同理。）
  editor world 里 `BeginPlay` 不执行 —— 本计划的自动化测试都不依赖 BeginPlay。
- **含中文的 `.h`/`.cpp` 必须是 UTF-8 with BOM**（MSVC 按 GBK 读会报假错）。Write 工具写出来**没有 BOM**，每个任务里都有一道补 BOM 的步骤，别跳过。
- **不要统一换行符**：仓库本来就是 CRLF/LF 混合，`autocrlf=true` 下 CRLF 是稳定态。
- **提交必须带 pathspec**（索引里有常驻暂存文件，裸 `git commit` 会吞掉全部）：
  ```bash
  git commit -m "<type>(矿区): <一句话>" -- <paths>
  ```
  提交信息末尾固定一行：
  ```
  Co-Authored-By: Claude Code <noreply@anthropic.com>
  ```
- **二进制资产走 LFS**：`Content/Materials/*.uasset` 已配 `filter=lfs`，新材质直接提交即可（提交前 `git check-attr filter -- Content/Materials/M_PlanetGhost.uasset`，输出必须含 `lfs`）。
- **性能判断必须让编辑器在前台**：后台整进程被限到 3 FPS，体素生成会从 0.16 s 变成 179 s。
- **单位约定**：地质深度单位是**体素**，`FPlanetGeologyField::Sample` 的 `Depth` 就是它；UL 坐标一律厘米。本计划假设 Actor 等比缩放 S = 1（`VoxelPlanetActor` 里已有「S≠1 不一致」的注释，本次不修）。

---

## 文件结构

| 文件 | 责任 |
|---|---|
| `Source/StellarEraPlanet/Public/PlanetGeologyField.h` / `Private/PlanetGeologyField.cpp` | 新增 `GetSampledElementDefinitions()`：暴露**运行期采样顺序**的元素定义 |
| `Source/StellarEraPlanet/Public/PlanetMineralBlocks.h` / `Private/PlanetMineralBlocks.cpp` | 纯函数采样器：地质场 + 参数 → 每元素的方块位置 |
| `Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp` | 采样器与运行期元素顺序的 6 个自动化测试 |
| `Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h` / `Private/PlanetMineralMaterialBuilder.cpp` | 生成 `M_PlanetGhost` / `M_MineralBlock` 两张材质并落盘（`WITH_EDITOR`） |
| `Source/StellarEraPlanet/Public/PlanetMineralViewActor.h` / `Private/PlanetMineralViewActor.cpp` | 观察体 Actor：球壳 + 每元素 ISM + 元素显隐 |
| `Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h` / `Private/PlanetMineralViewComponent.cpp` | 行星侧组件：进出模式、隐藏/恢复行星与大气、跟随、筛选 API |
| `Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp` | 观察体冒烟测试（分组数、显隐互不影响、空表） |
| `Source/StellarEraPlanet/Public/VoxelPlanetActor.h` / `Private/VoxelPlanetActor.cpp` | 挂上新组件（旧组件在 Task 6 删除） |
| `Source/StellarEra/Public/StarMapUISystem.h` / `Private/StarMapUISystem.cpp` | 图例纯函数（格式、固定布局、命中）与绘制 |
| `Source/StellarEra/Private/MineralLegendTests.cpp` | 图例布局 / 命中 / 文本 / 空表的 4 个自动化测试 |
| `Source/StellarEra/Public/StellarEraPlayerController.h` / `Private/StellarEraPlayerController.cpp` | H 开关、数字键筛选、图例点击、生命周期收口 |
| `Source/StellarEra/StellarEra.Build.cs` / `Source/StellarEraPlanet/StellarEraPlanet.Build.cs` | 删掉已经失效的依赖理由注释；去掉不再使用的 RHI 依赖 |

**删除清单**（Task 6 一次做完）：`PlanetCutawayComponent.*`、`PlanetCutawayMath.*`、`PlanetSectionImage.*`、`PlanetCutawayTests.cpp`、`CutawayLegendTests.cpp`。

## 与 spec 的偏差

1. **新增 `GetSampledElementDefinitions()`（运行期顺序），图例也改用它。**
   spec §5.4 说图例沿用 `GetElementDefinitions()`（`Elements.json` 数组顺序），但方块的 `DominantIndex` 是 `Elements` 的**运行期顺序**（行星 JSON 的 `elements` 是对象，解析后按哈希顺序落进数组）。两者不一定相同 —— 图例若用数组顺序，行号就会和方块组下标对不上，数字键与点击都会筛错元素。因此图例、数字键、方块组统一用运行期顺序。旧的 `StellarEra.StarMap.Cutaway.Legend.ElementOrder` 测试随文件删除，其覆盖由 `StellarEra.Planet.MineralBlocks.SampledDefinitions` 取代。
2. **图例面板固定宽度 460 px。** `ComputeMineralLegendLayout` 在输入回调里跑，那里量不到字体（`AHUD::Canvas` 只在 `PostRender` 期间有效），所以宽度必须与字体无关。高度仍按行数算；绘制与命中各自调用同一个纯函数，几何完全一致。`HitTestMineralLegendRow` 收 `const TArray<FStarMapPanelRect>&`（比 `TArrayView` 少一次显式转换，调用点更短）。
3. **`SpacingRatio` / `BlockSizeRatio` / `Seed` 暴露成组件上的 `EditAnywhere`。** spec 只把它们写在 `FBuildParams` 里；做成可调属性，PIE 里发现太密（spec §9）可以直接在 Details 面板改，不用重编译。
4. **材质缺失时在编辑器里自建一次。** `APlanetMineralViewActor::Build` 加载不到材质时，`#if WITH_EDITOR` 下调一次 `PlanetMineralMaterials::Build` 再重试加载；打包版仍然直接失败并打 Error。
5. **`OnMineralViewToggle` 不调用 `OrientToDirection`。** spec §3 明确「进入后保持玩家按 H 之前的角度」；旧剖面代码里的那句朝向重设必须删掉。
6. **球壳颜色不做 0.35 倍压暗。** spec §4.1 写「`ShellColor` 取 `SurfaceColor` 调暗到约 0.35 倍」。本计划直接把 `SurfaceColor` 传给球壳：亮度已经由 `Opacity = 0.12`（中心）与 `EdgeOpacity = 0.35`（边缘）给出，若再把颜色乘 0.35，边缘的 0.35 不透明度会被二次压暗，玻璃质感消失。PIE 里若觉得太亮，改的是材质默认 `ShellColor` 或 MID，不是调用点。

---

### Task 1: 采样器 + 运行期元素顺序

**Files:**
- Modify: `Source/StellarEraPlanet/Public/PlanetGeologyField.h`（在 `GetElementDefinitions()` 声明之后）
- Modify: `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp`（在 `GetElementDefinitions()` 实现之后）
- Create: `Source/StellarEraPlanet/Public/PlanetMineralBlocks.h`
- Create: `Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp`
- Test: `Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp`

**Interfaces:**
- Consumes: `FPlanetGeologyField::Sample(const FVector& UnitDir, double Depth)`、`NumElements()`、`IsEmpty()`、`GetAppearance().ColorThreshold`
- Produces:
  - `TArray<FPlanetGeologyElement> FPlanetGeologyField::GetSampledElementDefinitions() const`
  - `struct FPlanetMineralBlock { FVector LocationCm; int32 ElementIndex; }`
  - `namespace PlanetMineralBlocks { struct FBuildParams; struct FBuildResult; void Build(const FPlanetGeologyField&, const FBuildParams&, FBuildResult&); }`

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp`：

```cpp
// 矿物块采样的自动化测试：球内约束、元素归属、运行期顺序、确定性、阈值、空场。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralBlocks.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetMineralBlocksTest
{
	// 三个元素：a/b/c。半径 1000 体素，矿带峰值 150 / 450 / 750，厚度 50。
	// 3σ = 150 体素，三条带分别覆盖 0..300 / 300..600 / 600..900 —— 互不重叠。
	const TCHAR* ElementsJson = TEXT(R"JSON(
{
  "version": 1,
  "elements": [
    {
      "id": "a",
      "displayName": "A",
      "color": [1.0, 0.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 11, "depthScale": 0.3 }
    },
    {
      "id": "b",
      "displayName": "B",
      "color": [0.0, 1.0, 0.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 22, "depthScale": 0.3 }
    },
    {
      "id": "c",
      "displayName": "C",
      "color": [0.0, 0.0, 1.0],
      "noise": { "frequency": 3.0, "octaves": 2, "lacunarity": 2.0, "gain": 0.5, "seed": 33, "depthScale": 0.3 }
    }
  ]
}
)JSON");

	/**
	 * 行星 JSON。故意按 c / a / b 写，让「写下来的顺序」与 Elements.json 的
	 * a / b / c 不同 —— 运行期顺序由哈希决定，但至少不是数组顺序的简单复制。
	 */
	FString MakePlanetJson(float ColorThreshold)
	{
		return FString::Printf(TEXT(R"JSON(
{
  "version": 1,
  "geometryAmplitude": 1.5,
  "appearance": { "colorThreshold": %f, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {
    "c": { "abundance": 1.0, "peakDepth": 750, "layerThickness": 50 },
    "a": { "abundance": 1.0, "peakDepth": 150, "layerThickness": 50 },
    "b": { "abundance": 1.0, "peakDepth": 450, "layerThickness": 50 }
  }
}
)JSON"), ColorThreshold);
	}

	const TCHAR* EmptyPlanetJson = TEXT(R"JSON(
{
  "version": 1,
  "appearance": { "colorThreshold": 0.22, "fadeRange": 0.15, "purityForFullColor": 0.7 },
  "elements": {}
}
)JSON");

	TSharedPtr<const FPlanetGeologyField> MakeField(float ColorThreshold = 0.22f)
	{
		FPlanetGeologyLoadReport Report;
		TSharedPtr<const FPlanetGeologyField> Field =
			FPlanetGeologyField::LoadFromString(ElementsJson, MakePlanetJson(ColorThreshold),
				Report, /*PlanetRadiusVoxels=*/1000.0);
		check(Field.IsValid());
		return Field;
	}

	PlanetMineralBlocks::FBuildParams MakeParams()
	{
		PlanetMineralBlocks::FBuildParams Params;
		Params.SurfaceRadiusCm = 100000.f; // 1000 体素 × 100 cm
		Params.VoxelSizeCm = 100.f;
		Params.SpacingRatio = 1.f / 16.f;
		Params.Seed = 2026;
		return Params;
	}

	int32 CountBlocks(const PlanetMineralBlocks::FBuildResult& Result)
	{
		int32 Total = 0;
		for (const TArray<FPlanetMineralBlock>& Group : Result.BlocksByElement)
		{
			Total += Group.Num();
		}
		return Total;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksInsideSphereTest,
	"StellarEra.Planet.MineralBlocks.InsideSphere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksInsideSphereTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();
	TestFalse(TEXT("测试地质场非空"), Field->IsEmpty());

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	TestTrue(TEXT("有采样点"), Result.SampleCount > 0);
	TestEqual(TEXT("每个元素一组"), Result.BlocksByElement.Num(), Field->NumElements());
	TestTrue(TEXT("生成了矿物块"), PlanetMineralBlocksTest::CountBlocks(Result) > 0);

	const double RadiusCm = 100000.0;
	for (const TArray<FPlanetMineralBlock>& Group : Result.BlocksByElement)
	{
		for (const FPlanetMineralBlock& Block : Group)
		{
			// 抖动最多把格点推出半格，但「在球内」的判定在抖动之后才做，所以恒成立。
			TestTrue(TEXT("方块在球内"), Block.LocationCm.Size() <= RadiusCm + 1.0);
			TestTrue(TEXT("不在球心退化区"), Block.LocationCm.Size() >= 0.05 * RadiusCm - 1.0);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksElementMappingTest,
	"StellarEra.Planet.MineralBlocks.ElementMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksElementMappingTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	const FPlanetGeologyAppearance& Appearance = Field->GetAppearance();
	const double RadiusCm = 100000.0;
	const double VoxelSizeCm = 100.0;

	int32 Checked = 0;
	for (int32 GroupIndex = 0; GroupIndex < Result.BlocksByElement.Num(); ++GroupIndex)
	{
		const TArray<FPlanetMineralBlock>& Group = Result.BlocksByElement[GroupIndex];
		// 每组最多查 64 个：完整核对要几万次 Perlin，测试没必要那么慢。
		const int32 Limit = FMath::Min(64, Group.Num());
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			const FPlanetMineralBlock& Block = Group[Index];
			TestEqual(TEXT("组下标就是元素下标"), Block.ElementIndex, GroupIndex);

			const double Distance = Block.LocationCm.Size();
			const FVector UnitDir = Block.LocationCm / Distance;
			const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Depth);

			TestEqual(TEXT("归属与重新采样一致"), Sample.DominantIndex, GroupIndex);
			TestTrue(TEXT("通过颜色的阈值"),
				Sample.DominantNorm > Appearance.ColorThreshold);
			++Checked;
		}
	}

	TestTrue(TEXT("至少核对了一个方块"), Checked > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksSampledDefinitionsTest,
	"StellarEra.Planet.MineralBlocks.SampledDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksSampledDefinitionsTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();

	const TArray<FPlanetGeologyElement> Sampled = Field->GetSampledElementDefinitions();
	const TArray<FPlanetGeologyElement> Ordered = Field->GetElementDefinitions();

	TestEqual(TEXT("采样顺序数量 = 运行期元素数"), Sampled.Num(), Field->NumElements());
	TestEqual(TEXT("数组顺序数量 = 运行期元素数"), Ordered.Num(), Field->NumElements());

	// 运行期顺序里每个 id 都必须在数组顺序里出现。
	for (const FPlanetGeologyElement& Element : Sampled)
	{
		TestTrue(FString::Printf(TEXT("'%s' 在元素表里"), *Element.Id),
			Ordered.ContainsByPredicate([&Element](const FPlanetGeologyElement& Candidate)
			{
				return Candidate.Id == Element.Id;
			}));
	}

	// 深度探针：在每条矿带的峰值深度上采样，最强元素必须是该带的元素。
	// Sampled 若错用了 ElementOrder，这里就会指到别的 id 上。
	struct FProbe
	{
		const TCHAR* Id;
		double Depth;
	};
	const FProbe Probes[] = {
		{ TEXT("a"), 150.0 },
		{ TEXT("b"), 450.0 },
		{ TEXT("c"), 750.0 },
	};

	FRandomStream Random(4242);
	int32 Mapped = 0;
	constexpr int32 Iterations = 64;
	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		const FVector UnitDir = Random.GetUnitVector();
		for (const FProbe& Probe : Probes)
		{
			const FPlanetGeologySample Sample = Field->Sample(UnitDir, Probe.Depth);
			if (!Sampled.IsValidIndex(Sample.DominantIndex))
			{
				// 极小概率的噪声退化（整条带含量为 0）：不算错，但要求几乎全部命中。
				continue;
			}
			TestEqual(
				*FString::Printf(TEXT("%s 在深度 %.0f 的归属"), Probe.Id, Probe.Depth),
				Sampled[Sample.DominantIndex].Id,
				FString(Probe.Id));
			++Mapped;
		}
	}

	TestTrue(TEXT("几乎全部探针都命中了元素"), Mapped >= Iterations * 3 - 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksDeterministicTest,
	"StellarEra.Planet.MineralBlocks.Deterministic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksDeterministicTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Field = PlanetMineralBlocksTest::MakeField();
	const PlanetMineralBlocks::FBuildParams Params = PlanetMineralBlocksTest::MakeParams();

	PlanetMineralBlocks::FBuildResult First;
	PlanetMineralBlocks::FBuildResult Second;
	PlanetMineralBlocks::Build(*Field, Params, First);
	PlanetMineralBlocks::Build(*Field, Params, Second);

	TestEqual(TEXT("采样点数一致"), Second.SampleCount, First.SampleCount);
	TestEqual(TEXT("组数一致"), Second.BlocksByElement.Num(), First.BlocksByElement.Num());

	for (int32 GroupIndex = 0; GroupIndex < First.BlocksByElement.Num(); ++GroupIndex)
	{
		const TArray<FPlanetMineralBlock>& A = First.BlocksByElement[GroupIndex];
		const TArray<FPlanetMineralBlock>& B = Second.BlocksByElement[GroupIndex];
		TestEqual(TEXT("组内数量一致"), B.Num(), A.Num());

		const int32 Limit = FMath::Min(A.Num(), B.Num());
		for (int32 Index = 0; Index < Limit; ++Index)
		{
			TestEqual(TEXT("位置一致"), B[Index].LocationCm, A[Index].LocationCm);
			TestEqual(TEXT("归属一致"), B[Index].ElementIndex, A[Index].ElementIndex);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksThresholdTest,
	"StellarEra.Planet.MineralBlocks.Threshold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksThresholdTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<const FPlanetGeologyField> Low = PlanetMineralBlocksTest::MakeField(0.22f);
	const TSharedPtr<const FPlanetGeologyField> High = PlanetMineralBlocksTest::MakeField(0.90f);

	PlanetMineralBlocks::FBuildResult LowResult;
	PlanetMineralBlocks::FBuildResult HighResult;
	PlanetMineralBlocks::Build(*Low, PlanetMineralBlocksTest::MakeParams(), LowResult);
	PlanetMineralBlocks::Build(*High, PlanetMineralBlocksTest::MakeParams(), HighResult);

	const int32 LowCount = PlanetMineralBlocksTest::CountBlocks(LowResult);
	const int32 HighCount = PlanetMineralBlocksTest::CountBlocks(HighResult);

	TestTrue(TEXT("低阈值确实有方块"), LowCount > 0);
	TestTrue(TEXT("高阈值不会更多"), HighCount <= LowCount);
	TestTrue(TEXT("高阈值确实更少"), HighCount < LowCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralBlocksEmptyTest,
	"StellarEra.Planet.MineralBlocks.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralBlocksEmptyTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyLoadReport Report;
	const TSharedPtr<const FPlanetGeologyField> Field = FPlanetGeologyField::LoadFromString(
		PlanetMineralBlocksTest::ElementsJson,
		PlanetMineralBlocksTest::EmptyPlanetJson, Report, 1000.0);

	TestFalse(TEXT("解析没有报错"), Report.HasErrors());
	TestTrue(TEXT("空地质场"), Field->IsEmpty());
	TestEqual(TEXT("没有采样顺序元素"), Field->GetSampledElementDefinitions().Num(), 0);

	PlanetMineralBlocks::FBuildResult Result;
	PlanetMineralBlocks::Build(*Field, PlanetMineralBlocksTest::MakeParams(), Result);

	TestEqual(TEXT("空场直接返回，不采样"), Result.SampleCount, 0);
	TestEqual(TEXT("没有元素组"), Result.BlocksByElement.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 运行测试，确认失败**

编辑器开着也可以跑测试，但此时**编译不过**（`GetSampledElementDefinitions` 与 `PlanetMineralBlocks.h` 都还不存在）。执行 Step 6 的构建命令，预期 `error C2039` / `fatal error C1083: 无法打开包括文件: "PlanetMineralBlocks.h"`。这就是本步要的失败证据。

- [ ] **Step 3: 加 `GetSampledElementDefinitions`**

在 `Source/StellarEraPlanet/Public/PlanetGeologyField.h` 里 `GetElementDefinitions()` 声明之后插入：

```cpp
	/**
	 * 元素定义的副本（**运行期采样顺序**），与该顺序下的 DominantIndex 对齐。
	 *
	 * 和 GetElementDefinitions 的区别：那个按 Elements.json 数组顺序（给人看的图例），
	 * 这个是采样器实际遍历的顺序（给矿物块归属用）。两者通常不同 —— 行星 JSON 的
	 * elements 是对象，解析后按哈希顺序落进 Elements。
	 */
	TArray<FPlanetGeologyElement> GetSampledElementDefinitions() const;
```

在 `Source/StellarEraPlanet/Private/PlanetGeologyField.cpp` 里 `GetElementDefinitions()` 的实现之后插入：

```cpp
TArray<FPlanetGeologyElement> FPlanetGeologyField::GetSampledElementDefinitions() const
{
	TArray<FPlanetGeologyElement> Out;
	Out.Reserve(Elements.Num());
	for (const FElementRuntime& Runtime : Elements)
	{
		Out.Add(Runtime.Definition);
	}
	return Out;
}
```

- [ ] **Step 4: 写采样器**

创建 `Source/StellarEraPlanet/Public/PlanetMineralBlocks.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星内部矿物块采样：纯函数，不碰世界、不建组件。

#pragma once

#include "CoreMinimal.h"

class FPlanetGeologyField;

/** 一个矿物方块：位置相对球心（厘米），归属哪个元素（运行期元素下标）。 */
struct FPlanetMineralBlock
{
	FVector LocationCm = FVector::ZeroVector;
	int32 ElementIndex = INDEX_NONE;
};

namespace PlanetMineralBlocks
{
	struct FBuildParams
	{
		/** 基准球面半径（厘米），与 AVoxelPlanetActor::GetSurfaceRadiusCm 一致。 */
		float SurfaceRadiusCm = 0.f;
		/** 一个体素多少厘米；地质深度用它从体素换算回厘米。 */
		float VoxelSizeCm = 0.f;
		/** 采样间距 / 表面半径。1/16 时格点约 33³。 */
		float SpacingRatio = 1.f / 16.f;
		/** 抖动与跳过判定的随机种子；同种子结果逐项相同。 */
		int32 Seed = 0;
	};

	struct FBuildResult
	{
		/** 每个元素一组方块；下标 = 运行期元素下标 = Sample.DominantIndex。 */
		TArray<TArray<FPlanetMineralBlock>> BlocksByElement;
		/** 球内的采样点总数，供日志与性能诊断。 */
		int32 SampleCount = 0;
	};

	/**
	 * 按地质场解析采样，生成球内矿物方块。
	 *
	 * 判据与地表着色完全一致：`DominantNorm > Appearance.ColorThreshold` 才留下，
	 * 元素归属取 `Sample.DominantIndex`。空地质场直接返回（不遍历格点）。
	 */
	void Build(const FPlanetGeologyField& Field, const FBuildParams& Params, FBuildResult& Out);
}
```

创建 `Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralBlocks.h"

#include "PlanetGeologyField.h"
#include "Math/RandomStream.h"

namespace
{
	/** 球心附近方向退化：半径 5% 以内不采样。 */
	constexpr double MinDirectionalRadiusRatio = 0.05;
}

void PlanetMineralBlocks::Build(const FPlanetGeologyField& Field, const FBuildParams& Params,
	FBuildResult& Out)
{
	Out.BlocksByElement.Reset();
	Out.SampleCount = 0;

	const double RadiusCm = double(Params.SurfaceRadiusCm);
	const double StepCm = RadiusCm * double(Params.SpacingRatio);
	if (RadiusCm <= 0.0 || Params.VoxelSizeCm <= 0.0 || StepCm <= 0.0)
	{
		return;
	}

	if (Field.IsEmpty())
	{
		return;
	}

	Out.BlocksByElement.SetNum(Field.NumElements());

	const double ColorThreshold = double(Field.GetAppearance().ColorThreshold);
	const double VoxelSizeCm = double(Params.VoxelSizeCm);
	const int32 Steps = FMath::CeilToInt(double(RadiusCm / StepCm));

	FRandomStream Random(Params.Seed);

	for (int32 Z = -Steps; Z <= Steps; ++Z)
	{
		for (int32 Y = -Steps; Y <= Steps; ++Y)
		{
			for (int32 X = -Steps; X <= Steps; ++X)
			{
				// 每个格点固定消耗三次随机数，无论该点是否被跳过：
				// 跳过点也抽签，同种子的两次 Build 才能逐项一致。
				const double JitterX = double(Random.FRandRange(-0.5f, 0.5f));
				const double JitterY = double(Random.FRandRange(-0.5f, 0.5f));
				const double JitterZ = double(Random.FRandRange(-0.5f, 0.5f));

				const FVector Point(
					(double(X) + JitterX) * StepCm,
					(double(Y) + JitterY) * StepCm,
					(double(Z) + JitterZ) * StepCm);

				const double Distance = Point.Size();
				if (Distance > RadiusCm || Distance < MinDirectionalRadiusRatio * RadiusCm)
				{
					continue;
				}

				++Out.SampleCount;

				const FVector UnitDir = Point / Distance;
				const double Depth = (RadiusCm - Distance) / VoxelSizeCm;
				const FPlanetGeologySample Sample = Field.Sample(UnitDir, Depth);

				if (Sample.DominantNorm <= ColorThreshold
					|| !Out.BlocksByElement.IsValidIndex(Sample.DominantIndex))
				{
					continue;
				}

				FPlanetMineralBlock& Block =
					Out.BlocksByElement[Sample.DominantIndex].AddDefaulted_GetRef();
				Block.LocationCm = Point;
				Block.ElementIndex = Sample.DominantIndex;
			}
		}
	}
}
```

- [ ] **Step 5: 补 BOM**

```bash
cd "F:/workspace/ue/StellarEra"
for f in Source/StellarEraPlanet/Public/PlanetMineralBlocks.h \
         Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp \
         Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
head -c 3 Source/StellarEraPlanet/Public/PlanetMineralBlocks.h | od -An -tx1
```

预期最后一行输出 ` ef bb bf`。`PlanetGeologyField.h/.cpp` 本来就有 BOM，不要重复加。

- [ ] **Step 6: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期 `Build succeeded`。

- [ ] **Step 7: 跑本任务的测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.MineralBlocks" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralTests.log"
grep -c "Result={Success}" F:/workspace/ue/StellarEra/Saved/Logs/MineralTests.log
grep -c "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralTests.log
```

预期：`Success` = 6，`Fail` = 0。任何一条 Fail 都先看日志里的 `Error:` 行，别继续往下做。

- [ ] **Step 8: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "feat(矿区): 矿物块采样器 + 运行期元素顺序

按地质场解析采样球内矿物块，元素归属取 Sample.DominantIndex；
新增 GetSampledElementDefinitions 暴露运行期顺序，供图例与数字键对齐。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetGeologyField.h \
  Source/StellarEraPlanet/Private/PlanetGeologyField.cpp \
  Source/StellarEraPlanet/Public/PlanetMineralBlocks.h \
  Source/StellarEraPlanet/Private/PlanetMineralBlocks.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralBlocksTests.cpp
```

（路径已在上一步 `git add` 过；`git commit -- <paths>` 只提交这些路径，索引里的常驻暂存文件不会被吞。）

---

### Task 2: 两张材质（`M_PlanetGhost` / `M_MineralBlock`）

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h`
- Create: `Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp`
- Generate: `Content/Materials/M_PlanetGhost.uasset`、`Content/Materials/M_MineralBlock.uasset`（由控制台命令生成，不进版本库的手写内容）

**Interfaces:**
- Consumes: 无
- Produces:
  - `const TCHAR* PlanetMineralMaterials::GetGhostMaterialPath()` → `"/Game/Materials/M_PlanetGhost"`
  - `const TCHAR* PlanetMineralMaterials::GetBlockMaterialPath()` → `"/Game/Materials/M_MineralBlock"`
  - `bool PlanetMineralMaterials::Build(FString& OutError)`（仅 `WITH_EDITOR`）
  - 材质参数：`M_PlanetGhost` → `ShellColor`(vector) / `Opacity`(scalar 0.12) / `EdgeOpacity`(scalar 0.35) / `EdgeExponent`(scalar 5)；`M_MineralBlock` → `BlockColor`(vector) / `Brightness`(scalar 1.4)
  - 控制台命令 `StellarEra.BuildMineralMaterials`

- [ ] **Step 1: 写材质构建器**

创建 `Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视模式的两张材质：半透明球壳、不透明方块。

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

/**
 * 用 C++ 直接构造 UMaterial 并落盘，不依赖 UnrealEd 的资产创建流程。
 *
 * 与 PlanetAtmosphereMaterialBuilder.cpp 同一套路：新建表达式 → 挂到
 * GetExpressionCollection() → 连 EditorOnlyData 的插槽 → PostEditChange → SavePackage。
 * 改完参数重跑 `StellarEra.BuildMineralMaterials` 即可。
 */
namespace PlanetMineralMaterials
{
	/** `/Game/Materials/M_PlanetGhost`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetGhostMaterialPath();

	/** `/Game/Materials/M_MineralBlock`（包路径，不含 `.AssetName`）。 */
	STELLARERAPLANET_API const TCHAR* GetBlockMaterialPath();

	/** 重建两张材质并落盘；已存在的同名对象会被复用并清空表达式。 */
	STELLARERAPLANET_API bool Build(FString& OutError);
}

#endif // WITH_EDITOR
```

创建 `Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "PlanetMineralMaterialBuilder.h"

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const GhostPath = TEXT("/Game/Materials/M_PlanetGhost");
	const TCHAR* const BlockPath = TEXT("/Game/Materials/M_MineralBlock");
	const TCHAR* const GhostAssetName = TEXT("M_PlanetGhost");
	const TCHAR* const BlockAssetName = TEXT("M_MineralBlock");

	/** 建一个标量参数节点并登记到材质。 */
	UMaterialExpressionScalarParameter* AddScalarParam(UMaterial* Material, const TCHAR* Name,
		float Default, int32 PosX, int32 PosY)
	{
		UMaterialExpressionScalarParameter* Expr =
			NewObject<UMaterialExpressionScalarParameter>(Material);
		Expr->Material = Material;
		Expr->ParameterName = Name;
		Expr->DefaultValue = Default;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}

	/** 建一个向量参数节点并登记到材质。 */
	UMaterialExpressionVectorParameter* AddVectorParam(UMaterial* Material, const TCHAR* Name,
		FLinearColor Default, int32 PosX, int32 PosY)
	{
		UMaterialExpressionVectorParameter* Expr =
			NewObject<UMaterialExpressionVectorParameter>(Material);
		Expr->Material = Material;
		Expr->ParameterName = Name;
		Expr->DefaultValue = Default;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}

	/** 取同名材质或新建一个，并把旧表达式清空。失败返回 nullptr。 */
	UMaterial* FindOrCreateMaterial(const TCHAR* AssetPath, const TCHAR* AssetName,
		FString& OutError)
	{
		UPackage* Package = CreatePackage(AssetPath);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), AssetPath);
			return nullptr;
		}

		UMaterial* Material = FindObject<UMaterial>(Package, AssetName);
		if (!Material)
		{
			Material = NewObject<UMaterial>(Package, AssetName, RF_Public | RF_Standalone);
		}
		if (!Material)
		{
			OutError = FString::Printf(TEXT("NewObject<UMaterial> 失败：%s"), AssetName);
			return nullptr;
		}

		Material->PreEditChange(nullptr);
		Material->GetExpressionCollection().Empty();
		return Material;
	}

	bool SaveMaterial(UMaterial* Material, const TCHAR* AssetPath, FString& OutError)
	{
		Material->PostEditChange();
		Material->GetOutermost()->MarkPackageDirty();

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			AssetPath, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Material->GetOutermost(), Material, *PackageFileName, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
			return false;
		}
		return true;
	}

	/**
	 * 玻璃球：Unlit / Translucent / TwoSided。
	 *
	 * 不透明度 = Lerp(Opacity, EdgeOpacity, Fresnel)。BaseReflectFraction 设 0，
	 * 于是正对相机的像素菲涅尔为 0（只剩 Opacity），掠射角接近 1（吃到 EdgeOpacity）——
	 * 球体轮廓清楚，正面又足够透，能看见里面的方块。
	 */
	bool BuildGhostMaterial(FString& OutError)
	{
		UMaterial* Material = FindOrCreateMaterial(GhostPath, GhostAssetName, OutError);
		if (!Material)
		{
			return false;
		}

		UMaterialExpressionVectorParameter* ShellColor = AddVectorParam(Material,
			TEXT("ShellColor"), FLinearColor(0.06f, 0.15f, 0.06f, 1.f), -900, -300);
		UMaterialExpressionScalarParameter* Opacity = AddScalarParam(Material,
			TEXT("Opacity"), 0.12f, -900, -200);
		UMaterialExpressionScalarParameter* EdgeOpacity = AddScalarParam(Material,
			TEXT("EdgeOpacity"), 0.35f, -900, -100);
		UMaterialExpressionScalarParameter* EdgeExponent = AddScalarParam(Material,
			TEXT("EdgeExponent"), 5.f, -900, 0);

		UMaterialExpressionFresnel* Fresnel = NewObject<UMaterialExpressionFresnel>(Material);
		Fresnel->Material = Material;
		// Exponent 走参数：ExponentIn 给了表达式就以输入为准，Exponent 字段只是兜底。
		Fresnel->ExponentIn.Expression = EdgeExponent;
		Fresnel->ExponentIn.OutputIndex = 0;
		Fresnel->BaseReflectFraction = 0.f;
		Fresnel->MaterialExpressionEditorX = -600;
		Fresnel->MaterialExpressionEditorY = 0;
		Material->GetExpressionCollection().AddExpression(Fresnel);

		UMaterialExpressionLinearInterpolate* Lerp =
			NewObject<UMaterialExpressionLinearInterpolate>(Material);
		Lerp->Material = Material;
		Lerp->A.Expression = Opacity;
		Lerp->A.OutputIndex = 0;
		Lerp->B.Expression = EdgeOpacity;
		Lerp->B.OutputIndex = 0;
		Lerp->Alpha.Expression = Fresnel;
		Lerp->Alpha.OutputIndex = 0;
		Lerp->MaterialExpressionEditorX = -300;
		Lerp->MaterialExpressionEditorY = 0;
		Material->GetExpressionCollection().AddExpression(Lerp);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_PlanetGhost）");
			return false;
		}
		EditorData->EmissiveColor.Expression = ShellColor;
		EditorData->EmissiveColor.OutputIndex = 0;
		EditorData->Opacity.Expression = Lerp;
		EditorData->Opacity.OutputIndex = 0;

		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		// 投影是组件属性（SetCastShadow），不是材质属性 —— 在壳组件上设。

		return SaveMaterial(Material, GhostPath, OutError);
	}

	/** 方块：Unlit / Opaque，BlockColor × Brightness → Emissive。 */
	bool BuildBlockMaterial(FString& OutError)
	{
		UMaterial* Material = FindOrCreateMaterial(BlockPath, BlockAssetName, OutError);
		if (!Material)
		{
			return false;
		}

		UMaterialExpressionVectorParameter* BlockColor = AddVectorParam(Material,
			TEXT("BlockColor"), FLinearColor::White, -700, -200);
		UMaterialExpressionScalarParameter* Brightness = AddScalarParam(Material,
			TEXT("Brightness"), 1.4f, -700, -100);

		UMaterialExpressionMultiply* Multiply =
			NewObject<UMaterialExpressionMultiply>(Material);
		Multiply->Material = Material;
		Multiply->A.Expression = BlockColor;
		Multiply->A.OutputIndex = 0;
		Multiply->B.Expression = Brightness;
		Multiply->B.OutputIndex = 0;
		Multiply->MaterialExpressionEditorX = -400;
		Multiply->MaterialExpressionEditorY = -100;
		Material->GetExpressionCollection().AddExpression(Multiply);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_MineralBlock）");
			return false;
		}
		EditorData->EmissiveColor.Expression = Multiply;
		EditorData->EmissiveColor.OutputIndex = 0;

		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		// 方块走 UInstancedStaticMeshComponent：不开这一位，ISM 会退回默认材质。
		Material->bUsedWithInstancedStaticMeshes = true;

		return SaveMaterial(Material, BlockPath, OutError);
	}
} // namespace

const TCHAR* PlanetMineralMaterials::GetGhostMaterialPath()
{
	return GhostPath;
}

const TCHAR* PlanetMineralMaterials::GetBlockMaterialPath()
{
	return BlockPath;
}

bool PlanetMineralMaterials::Build(FString& OutError)
{
	OutError.Reset();
	if (!BuildGhostMaterial(OutError))
	{
		return false;
	}
	return BuildBlockMaterial(OutError);
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GBuildMineralMaterialsCommand(
		TEXT("StellarEra.BuildMineralMaterials"),
		TEXT("重建 /Game/Materials/M_PlanetGhost 与 M_MineralBlock。改完材质参数后跑一次。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				FString Error;
				const bool bOk = PlanetMineralMaterials::Build(Error);
				if (bOk)
				{
					UE_LOG(LogTemp, Display, TEXT("[矿物] 材质已生成：%s / %s"),
						GhostPath, BlockPath);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[矿物] 材质生成失败：%s"), *Error);
				}
				// -TestExit 靠日志里出现这一行退出进程：成功、失败都打，
				// 否则失败时无头进程会一直挂着。
				UE_LOG(LogTemp, Display, TEXT("StellarEra.MineralMaterials.Done ok=%d"),
					bOk ? 1 : 0);
			}));
} // namespace

#endif // WITH_EDITOR
```

- [ ] **Step 2: 补 BOM**

```bash
cd "F:/workspace/ue/StellarEra"
for f in Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h \
         Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
head -c 3 Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp | od -An -tx1
```

预期 ` ef bb bf`。

- [ ] **Step 3: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期 `Build succeeded`。

- [ ] **Step 4: 无头生成两张材质**

编辑器保持关闭（`UnrealEditor-Cmd` 是第二个编辑器进程，不要和 GUI 编辑器同时开同一工程）。首次会编译着色器，可能要 1–3 分钟：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="StellarEra.BuildMineralMaterials" \
  -TestExit="StellarEra.MineralMaterials.Done" \
  -unattended -nopause -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralMaterials.log"

grep -o "StellarEra.MineralMaterials.Done ok=[01]" \
  "F:/workspace/ue/StellarEra/Saved/Logs/MineralMaterials.log"
ls -l "F:/workspace/ue/StellarEra/Content/Materials/M_PlanetGhost.uasset" \
      "F:/workspace/ue/StellarEra/Content/Materials/M_MineralBlock.uasset"
```

预期：`grep` 输出 `StellarEra.MineralMaterials.Done ok=1`，两个 `.uasset` 都存在且**大于 1000 字节**（LFS 指针只有 129 字节，别把指针当成功）。

若 `ok=0`，看同一日志里的 `[矿物] 材质生成失败：` 行；常见原因是 `WITH_EDITOR` 下 `GetEditorOnlyData()` 为空（材质资产没被当作编辑器对象加载）。

- [ ] **Step 5: 确认 LFS 属性并提交**

```bash
cd "F:/workspace/ue/StellarEra"
git check-attr filter -- Content/Materials/M_PlanetGhost.uasset Content/Materials/M_MineralBlock.uasset
```

两行都必须含 `filter: lfs`。然后：

```bash
git commit -m "feat(矿区): 玻璃球与矿方块材质 + 构建命令

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetMineralMaterialBuilder.h \
  Source/StellarEraPlanet/Private/PlanetMineralMaterialBuilder.cpp \
  Content/Materials/M_PlanetGhost.uasset \
  Content/Materials/M_MineralBlock.uasset
```

---

### Task 3: 观察体 Actor + 行星侧组件

**Files:**
- Create: `Source/StellarEraPlanet/Public/PlanetMineralViewActor.h`
- Create: `Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp`
- Create: `Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h`
- Create: `Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`
- Test: `Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h`（前置声明 + 成员 + getter）
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`（include + 构造）

**Interfaces:**
- Consumes: `PlanetMineralBlocks::Build`（Task 1）、`PlanetMineralMaterials::*`（Task 2）、`FPlanetGeologyField::GetSampledElementDefinitions()`（Task 1）
- Produces:
  - `APlanetMineralViewActor::Build(const TArray<TArray<FPlanetMineralBlock>>&, const FLinearColor& ShellColor, float SurfaceRadiusCm, float BlockSizeCm, const TArray<FPlanetGeologyElement>& Elements)` → `bool`
  - `APlanetMineralViewActor::NumElements() / IsElementVisible(int32) / SetElementVisible(int32,bool) / SetAllElementsVisible(bool)`
  - `UPlanetMineralViewComponent::Activate(AVoxelPlanetActor*)` → `bool`、`Deactivate()`、`IsActive()`
  - `UPlanetMineralViewComponent::NumElements() / IsElementVisible / SetElementVisible / ToggleElement / SetAllElementsVisible`
  - `UPlanetMineralViewComponent::GetElements()` → `const TArray<FPlanetGeologyElement>&`
  - `AVoxelPlanetActor::GetMineralViewComponent()` → `UPlanetMineralViewComponent*`

> **注意：本任务只新增，不删除。** 旧的 `UPlanetCutawayComponent` 成员与 getter 原样保留，保证每步都能编译。删除在 Task 6。

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp`：

```cpp
// 观察体的冒烟测试：每个元素一个 ISM、显隐互不影响、空数据不崩。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "PlanetMineralBlocks.h"
#include "PlanetMineralViewActor.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorTest,
	"StellarEra.Planet.MineralView.ActorBuild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEngine ? GEngine->GetEditorWorldContext(false).World() : nullptr;
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	// 两个元素，各 3 个方块。位置随便给：只验分组与显隐，不验几何。
	TArray<TArray<FPlanetMineralBlock>> Blocks;
	Blocks.SetNum(2);
	for (int32 ElementIndex = 0; ElementIndex < 2; ++ElementIndex)
	{
		for (int32 BlockIndex = 0; BlockIndex < 3; ++BlockIndex)
		{
			FPlanetMineralBlock& Block = Blocks[ElementIndex].AddDefaulted_GetRef();
			Block.LocationCm = FVector(1000.0 * (ElementIndex + 1), 100.0 * BlockIndex, 0.0);
			Block.ElementIndex = ElementIndex;
		}
	}

	TArray<FPlanetGeologyElement> Elements;
	Elements.SetNum(2);
	Elements[0].Id = TEXT("a");
	Elements[0].DisplayName = TEXT("A");
	Elements[0].Color = FLinearColor::Red;
	Elements[1].Id = TEXT("b");
	Elements[1].DisplayName = TEXT("B");
	Elements[1].Color = FLinearColor::Blue;

	const bool bBuilt = View->Build(Blocks, FLinearColor(0.1f, 0.2f, 0.1f, 1.f),
		/*SurfaceRadiusCm=*/100000.f, /*BlockSizeCm=*/2800.f, Elements);

	TestTrue(TEXT("Build 成功（材质已生成、引擎网格可加载）"), bBuilt);
	if (!bBuilt)
	{
		View->Destroy();
		return false;
	}

	TestEqual(TEXT("每个元素一个 ISM"), View->NumElements(), 2);
	TestTrue(TEXT("默认全部可见 0"), View->IsElementVisible(0));
	TestTrue(TEXT("默认全部可见 1"), View->IsElementVisible(1));

	View->SetElementVisible(0, false);
	TestFalse(TEXT("关掉 0"), View->IsElementVisible(0));
	TestTrue(TEXT("关 0 不影响 1"), View->IsElementVisible(1));

	View->SetAllElementsVisible(true);
	TestTrue(TEXT("全开恢复 0"), View->IsElementVisible(0));

	// 越界下标必须安全：调用方是需求键与鼠标命中，不该让进程崩。
	View->SetElementVisible(99, false);
	TestFalse(TEXT("越界下标不生效"), View->IsElementVisible(99));

	View->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewActorEmptyTest,
	"StellarEra.Planet.MineralView.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewActorEmptyTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEngine ? GEngine->GetEditorWorldContext(false).World() : nullptr;
	if (!TestNotNull(TEXT("编辑器世界"), World))
	{
		return false;
	}

	APlanetMineralViewActor* View = World->SpawnActor<APlanetMineralViewActor>();
	if (!TestNotNull(TEXT("观察体 spawn 成功"), View))
	{
		return false;
	}

	const bool bBuilt = View->Build({}, FLinearColor::Gray, 100000.f, 2800.f, {});
	TestTrue(TEXT("空元素表也能建出球壳"), bBuilt);
	TestEqual(TEXT("没有元素组"), View->NumElements(), 0);

	View->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 运行测试，确认失败**

此时 `PlanetMineralViewActor.h` 还不存在，构建会报 `fatal error C1083: 无法打开包括文件: "PlanetMineralViewActor.h"`。跑 Step 8 的构建命令确认这一点即可。

- [ ] **Step 3: 写观察体 Actor**

创建 `Source/StellarEraPlanet/Public/PlanetMineralViewActor.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 矿物透视的观察体：半透明球壳 + 每个元素一组不透明方块。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralViewActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
struct FPlanetMineralBlock;

/**
 * 观察体。
 *
 * **必须独立 spawn，不能挂在行星 Actor 下面**：进入模式会
 * SetActorHiddenInGame(true) 隐藏行星，而 AActor::IsHidden 会让**所有**子组件的
 * USceneComponent::ShouldRender 返回 false（SceneComponent.cpp:3517），球壳与方块
 * 会跟着一起消失。
 *
 * 方块位置相对本 Actor 原点（= 球心），组件不随行星旋转缩放。
 */
UCLASS()
class STELLARERAPLANET_API APlanetMineralViewActor : public AActor
{
	GENERATED_BODY()

public:
	APlanetMineralViewActor();

	/**
	 * 建球壳、按元素建 ISM 并放方块。
	 *
	 * BlocksByElement 与 Elements 的下标必须同为**运行期元素下标**
	 * （即 FPlanetGeologyField::GetSampledElementDefinitions 的顺序）。
	 * 材质/网格加载失败或元素数与分组数不一致时返回 false 并打 Error。
	 */
	bool Build(const TArray<TArray<FPlanetMineralBlock>>& BlocksByElement,
		const FLinearColor& ShellColor, float SurfaceRadiusCm, float BlockSizeCm,
		const TArray<FPlanetGeologyElement>& Elements);

	int32 NumElements() const { return BlockMeshes.Num(); }
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void SetAllElementsVisible(bool bVisible);

	/** 球壳组件，仅供测试与调试。 */
	UStaticMeshComponent* GetShellMesh() const { return ShellMesh; }

private:
	UPROPERTY(VisibleAnywhere, Category = "MineralView")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> ShellMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShellMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> BlockMeshes;
};
```

创建 `Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewActor.h"

#include "PlanetMineralBlocks.h"
#include "PlanetMineralMaterialBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** 方块朝向的随机翻转幅度（度）。 */
	constexpr float BlockTiltDegrees = 25.f;

	UMaterialInterface* LoadMaterial(const TCHAR* PackagePath, const TCHAR* AssetName)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, AssetName);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
#if WITH_EDITOR
		if (!Material)
		{
			// 首次运行、或材质资产还没生成：就地建一次再加载。打包版没有这条路。
			FString Error;
			if (PlanetMineralMaterials::Build(Error))
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[矿物] 材质生成失败：%s"), *Error);
			}
		}
#endif
		return Material;
	}
}

APlanetMineralViewActor::APlanetMineralViewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 必须有根组件：没有根，SetActorLocation 不生效（新 spawn 的裸 Actor 位置恒为 0）。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

bool APlanetMineralViewActor::Build(
	const TArray<TArray<FPlanetMineralBlock>>& BlocksByElement,
	const FLinearColor& ShellColor, float SurfaceRadiusCm, float BlockSizeCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	if (SurfaceRadiusCm <= 0.f || BlockSizeCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm，方块 %.1f cm"),
			SurfaceRadiusCm, BlockSizeCm);
		return false;
	}

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* GhostMaterial = LoadMaterial(
		PlanetMineralMaterials::GetGhostMaterialPath(), TEXT("M_PlanetGhost"));
	UMaterialInterface* BlockMaterial = LoadMaterial(
		PlanetMineralMaterials::GetBlockMaterialPath(), TEXT("M_MineralBlock"));

	if (!SphereMesh || !CubeMesh || !GhostMaterial || !BlockMaterial)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[矿物] 网格或材质加载失败：球=%s 方块=%s 球壳材质=%s 方块材质=%s"),
			SphereMesh ? TEXT("ok") : TEXT("缺"),
			CubeMesh ? TEXT("ok") : TEXT("缺"),
			GhostMaterial ? TEXT("ok") : TEXT("缺"),
			BlockMaterial ? TEXT("ok") : TEXT("缺"));
		return false;
	}

	// --- 球壳：引擎球是 100 cm 直径，缩放到行星直径 ---
	// 运行期建的组件必须在 RegisterComponent 之前设好 Mobility：注册之后再改会走
	// 重新注册的路径，而这块网格从第一帧起就要跟着行星移动。
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

	// --- 每个元素一个 ISM ---
	if (BlocksByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[矿物] 分组数与元素数不一致：%d 组 / %d 个元素"),
			BlocksByElement.Num(), Elements.Num());
		return false;
	}

	const float CubeScale = BlockSizeCm / 100.f;
	BlockMeshes.Reset();
	BlockMeshes.Reserve(BlocksByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < BlocksByElement.Num(); ++ElementIndex)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(
			this, *FString::Printf(TEXT("MineralBlocks%d"), ElementIndex));
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(CubeMesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BlockMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BlockColor"), Elements[ElementIndex].Color);
			Component->SetMaterial(0, MID);
		}

		// 每个元素一个独立随机流：元素增删不会打乱别的元素已有的方块朝向。
		FRandomStream OrientationRandom(ElementIndex + 1);
		const TArray<FPlanetMineralBlock>& Group = BlocksByElement[ElementIndex];
		for (const FPlanetMineralBlock& Block : Group)
		{
			// 绕随机轴 ±25° 翻身，避免整片方块像规则网格。
			const FQuat Rotation(
				OrientationRandom.GetUnitVector(),
				FMath::DegreesToRadians(
					OrientationRandom.FRandRange(-BlockTiltDegrees, BlockTiltDegrees)));
			Component->AddInstance(FTransform(Rotation, Block.LocationCm, FVector(CubeScale)));
		}

		Component->SetVisibility(true);
		BlockMeshes.Add(Component);
	}

	return true;
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	return BlockMeshes.IsValidIndex(Index) && BlockMeshes[Index]
		&& BlockMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (!BlockMeshes.IsValidIndex(Index) || !BlockMeshes[Index])
	{
		return;
	}
	// bPropagateToChildren = true：ISM 没有子组件，但保持一致语义。
	BlockMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (UInstancedStaticMeshComponent* Component : BlockMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
```

- [ ] **Step 4: 写行星侧组件**

创建 `Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 行星的矿物透视开关：进出模式、隐藏/恢复行星与大气、跟随、按元素筛选。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetGeologyTypes.h"
#include "PlanetMineralViewComponent.generated.h"

class APlanetMineralViewActor;
class AVoxelPlanetActor;
class FPlanetGeologyField;

/**
 * 行星上的矿物透视组件。观察性质：不改体素、不碰存档、不影响 N 体。
 *
 * 生命周期：Activate 时解析采样并 spawn 一个独立的 APlanetMineralViewActor，
 * 同时隐藏行星、关碰撞、关大气；Deactivate 全部恢复，重复调用安全。
 * 只在激活期间 Tick，每帧把观察体钉到行星球心。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetMineralViewComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetMineralViewComponent();

	//~ Begin UActorComponent Interface
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	/** 基类的 Activate(bool) 仍然可用：本类的同名重载只是多了一种入口。 */
	using USceneComponent::Activate;

	/**
	 * 进入矿物透视。
	 * 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败（打 Error）。
	 */
	bool Activate(AVoxelPlanetActor* InPlanet);

	/** 退出并恢复行星网格、碰撞、大气。重复调用安全。 */
	void Deactivate();

	bool IsActive() const { return bActive; }

	int32 NumElements() const;
	bool IsElementVisible(int32 Index) const;
	void SetElementVisible(int32 Index, bool bVisible);
	void ToggleElement(int32 Index);
	void SetAllElementsVisible(bool bVisible);

	AVoxelPlanetActor* GetViewPlanet() const { return Planet.Get(); }
	TSharedPtr<const FPlanetGeologyField> GetGeologyField() const { return CachedGeology; }

	/** 运行期元素定义（采样顺序）；图例、数字键、方块组下标共用它。 */
	const TArray<FPlanetGeologyElement>& GetElements() const { return Elements; }

	/** 采样间距 / 表面半径。太大没细节，太小方块数与进入耗时爆炸。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView", meta = (ClampMin = "0.01", ClampMax = "0.25"))
	float SpacingRatio = 1.f / 16.f;

	/** 方块边长 / 采样间距。1.0 会让相邻方块互相顶住。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float BlockSizeRatio = 0.45f;

	/** 抖动种子；同种子生成同一套方块。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	int32 Seed = 1337;

private:
	bool bActive = false;
	TWeakObjectPtr<AVoxelPlanetActor> Planet;
	TSharedPtr<const FPlanetGeologyField> CachedGeology;
	TArray<FPlanetGeologyElement> Elements;

	UPROPERTY(Transient)
	TObjectPtr<APlanetMineralViewActor> ViewActor;

	bool bSavedAtmosphereEnabled = true;
	bool bSavedAtmosphereVisible = true;
	bool bSavedPlanetHidden = false;
	bool bSavedCollisionEnabled = true;
};
```

创建 `Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralBlocks.h"
#include "PlanetMineralViewActor.h"
#include "VoxelPlanetActor.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

UPlanetMineralViewComponent::UPlanetMineralViewComponent()
{
	// 只在激活期间 Tick，且排在后更新阶段（此时 N 体已把行星位置写回）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UPlanetMineralViewComponent::Activate(AVoxelPlanetActor* InPlanet)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float SurfaceRadiusCm = InPlanet->GetSurfaceRadiusCm();
	// 地质深度以体素计，换回厘米要用单个体素的厘米尺寸（基类属性，与 Radius 同级）。
	const float VoxelSizeCm = InPlanet->VoxelSize;
	if (SurfaceRadiusCm <= 0.f || VoxelSizeCm <= 0.f)
	{
		return false;
	}

	PlanetMineralBlocks::FBuildParams Params;
	Params.SurfaceRadiusCm = SurfaceRadiusCm;
	Params.VoxelSizeCm = VoxelSizeCm;
	Params.SpacingRatio = SpacingRatio;
	Params.Seed = Seed;

	PlanetMineralBlocks::FBuildResult Blocks;
	const double BuildStartSeconds = FPlatformTime::Seconds();
	PlanetMineralBlocks::Build(*Geology, Params, Blocks);

	int32 BlockCount = 0;
	for (const TArray<FPlanetMineralBlock>& Group : Blocks.BlocksByElement)
	{
		BlockCount += Group.Num();
	}
	UE_LOG(LogTemp, Display, TEXT("[矿物] %s：采样 %d 点，生成 %d 个方块，用时 %.1f ms"),
		*InPlanet->GetName(), Blocks.SampleCount, BlockCount,
		(FPlatformTime::Seconds() - BuildStartSeconds) * 1000.0);

	const TArray<FPlanetGeologyElement> SampledElements =
		Geology->GetSampledElementDefinitions();
	const float BlockSizeCm = SurfaceRadiusCm * SpacingRatio * BlockSizeRatio;

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ViewActor = World->SpawnActor<APlanetMineralViewActor>(
		APlanetMineralViewActor::StaticClass(),
		InPlanet->GetActorLocation(), FRotator::ZeroRotator, SpawnParams);

	if (!ViewActor
		|| !ViewActor->Build(Blocks.BlocksByElement, InPlanet->SurfaceColor,
			SurfaceRadiusCm, BlockSizeCm, SampledElements))
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 观察体创建失败：%s"), *InPlanet->GetName());
		if (ViewActor)
		{
			ViewActor->Destroy();
			ViewActor = nullptr;
		}
		return false;
	}

	Planet = InPlanet;
	CachedGeology = Geology;
	Elements = SampledElements;

	// 先存后改：恢复时原样写回，别假设进入前一定是「可见 + 有碰撞」。
	bSavedPlanetHidden = InPlanet->IsHidden();
	bSavedCollisionEnabled = InPlanet->GetActorEnableCollision();
	InPlanet->SetActorEnableCollision(false);
	InPlanet->SetActorHiddenInGame(true);

	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
	{
		bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bSavedAtmosphereVisible = Atmosphere->IsVisible();
		Atmosphere->bAtmosphereEnabled = false;
		Atmosphere->SetVisibility(false);
	}

	bActive = true;
	SetComponentTickEnabled(true);
	return true;
}

void UPlanetMineralViewComponent::Deactivate()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetComponentTickEnabled(false);

	if (APlanetMineralViewActor* View = ViewActor)
	{
		View->Destroy();
	}
	ViewActor = nullptr;

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		RestorePlanet->SetActorHiddenInGame(bSavedPlanetHidden);
		RestorePlanet->SetActorEnableCollision(bSavedCollisionEnabled);

		if (UPlanetAtmosphereComponent* Atmosphere = RestorePlanet->GetAtmosphereComponent())
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

	Planet.Reset();
	CachedGeology.Reset();
	Elements.Reset();
}

void UPlanetMineralViewComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	Super::EndPlay(EndPlayReason);
}

void UPlanetMineralViewComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bActive)
	{
		return;
	}

	// 只跟位置：N 体只平移，行星不转向也不缩放；跟着转会把方块拧歪。
	if (const AVoxelPlanetActor* PlanetActor = Planet.Get())
	{
		if (ViewActor)
		{
			ViewActor->SetActorLocation(PlanetActor->GetActorLocation());
		}
	}
}

int32 UPlanetMineralViewComponent::NumElements() const
{
	return Elements.Num();
}

bool UPlanetMineralViewComponent::IsElementVisible(int32 Index) const
{
	return ViewActor && ViewActor->IsElementVisible(Index);
}

void UPlanetMineralViewComponent::SetElementVisible(int32 Index, bool bVisible)
{
	if (ViewActor)
	{
		ViewActor->SetElementVisible(Index, bVisible);
	}
}

void UPlanetMineralViewComponent::ToggleElement(int32 Index)
{
	if (!Elements.IsValidIndex(Index))
	{
		return;
	}
	SetElementVisible(Index, !IsElementVisible(Index));
}

void UPlanetMineralViewComponent::SetAllElementsVisible(bool bVisible)
{
	if (ViewActor)
	{
		ViewActor->SetAllElementsVisible(bVisible);
	}
}
```

- [ ] **Step 5: 挂到行星球**

`Source/StellarEraPlanet/Public/VoxelPlanetActor.h`：在前置声明区（`class UPlanetCutawayComponent;` 下一行）加：

```cpp
class UPlanetMineralViewComponent;
```

在 `CutawayComponent` 成员块之后加：

```cpp
	/** 矿物透视观察组件。构造时自动创建；未激活时不 Tick、不建任何资产。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|MineralView")
	TObjectPtr<UPlanetMineralViewComponent> MineralViewComponent;

	UFUNCTION(BlueprintPure, Category = "Planet|MineralView")
	UPlanetMineralViewComponent* GetMineralViewComponent() const { return MineralViewComponent; }
```

`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`：include 区（`#include "PlanetCutawayComponent.h"` 下一行）加：

```cpp
#include "PlanetMineralViewComponent.h"
```

构造函数里（`CutawayComponent->SetupAttachment(RootComponent);` 之后）加：

```cpp
	// 矿物透视组件：默认不 Tick，Activate 时才采样并 spawn 观察体。
	MineralViewComponent = CreateDefaultSubobject<UPlanetMineralViewComponent>(TEXT("PlanetMineralView"));
	MineralViewComponent->SetupAttachment(RootComponent);
```

- [ ] **Step 6: 补 BOM**

```bash
cd "F:/workspace/ue/StellarEra"
for f in Source/StellarEraPlanet/Public/PlanetMineralViewActor.h \
         Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp \
         Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h \
         Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp \
         Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp; do
  if [ "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
    printf '\xEF\xBB\xBF' > "$f.tmp" && cat "$f" >> "$f.tmp" && mv "$f.tmp" "$f"
  fi
done
head -c 3 Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h | od -An -tx1
```

`VoxelPlanetActor.h/.cpp` 本来就有 BOM。

- [ ] **Step 7: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期 `Build succeeded`。把完整的 UBT 输出贴进汇报。

- [ ] **Step 8: 运行本任务的测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.MineralView" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTests.log"
grep -c "Result={Success}" F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTests.log
grep -c "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralViewTests.log
```

预期 `Success` = 2，`Fail` = 0。

若 `ActorBuild` 失败在 `Build 成功` 这一条上，先看日志里 `[矿物] 材质生成失败` / `网格或材质加载失败` 行 —— 多半是 Task 2 的 `.uasset` 没提交或没生成。

- [ ] **Step 9: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "feat(矿区): 矿物透视观察体与行星侧组件

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetMineralViewActor.h \
  Source/StellarEraPlanet/Private/PlanetMineralViewActor.cpp \
  Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h \
  Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp \
  Source/StellarEraPlanet/Private/PlanetMineralViewTests.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp
```

---

### Task 4: 图例纯函数与绘制（只加不删）

**Files:**
- Modify: `Source/StellarEra/Public/StarMapUISystem.h`
- Modify: `Source/StellarEra/Private/StarMapUISystem.cpp`
- Test: `Source/StellarEra/Private/MineralLegendTests.cpp`

**Interfaces:**
- Consumes: `FStarMapPanelRect`、`PlacePanelBesideAnchor`（已有）
- Produces:
  - `struct FMineralLegendEntry { FString DisplayName; FLinearColor Color; float Abundance; bool bVisible; }`
  - `namespace MineralLegendLayout { Padding, SeparatorSpacing, SeparatorThickness, RowSlotHeight, TitleLineHeight, HintLineHeight, PanelWidthPx, MarginPx, HitInsetY, float PanelHeightPx(int32) }`
  - `static TArray<FString> UStarMapUISystem::FormatMineralLegendLines(const FString&, TArrayView<const FMineralLegendEntry>)`
  - `static void UStarMapUISystem::ComputeMineralLegendLayout(const FVector2D& ViewportSizePx, int32 EntryCount, FStarMapPanelRect& OutPanel, TArray<FStarMapPanelRect>& OutRowRects)`
  - `static int32 UStarMapUISystem::HitTestMineralLegendRow(const TArray<FStarMapPanelRect>&, const FVector2D&)`
  - `void UStarMapUISystem::DrawMineralLegend(AStellarEraHUD&, const FString&, const TArray<FMineralLegendEntry>&, const FVector2D&)`

> **本任务只新增。** 旧的 `FCutawayLegendEntry` / `FormatCutawayLegendLines` / `DrawCutawayLegend` 原样保留，保证每步都能编译；替换在 Task 5。

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEra/Private/MineralLegendTests.cpp`：

```cpp
// 矿物观察图例的纯函数测试：布局、命中、文本、空表。
// 只断言关系（行在面板内、行与行相邻不重叠），不写死像素数。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "StarMapUISystem.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace MineralLegendTest
{
	constexpr int32 EntryCount = 3;

	void BuildLayout(FStarMapPanelRect& OutPanel, TArray<FStarMapPanelRect>& OutRows,
		int32 Entries = EntryCount)
	{
		UStarMapUISystem::ComputeMineralLegendLayout(
			FVector2D(1920.f, 1080.f), Entries, OutPanel, OutRows);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendLayoutTest,
	"StellarEra.StarMap.MineralLegend.Layout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendLayoutTest::RunTest(const FString& Parameters)
{
	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> Rows;
	MineralLegendTest::BuildLayout(Panel, Rows);

	TestEqual(TEXT("行矩形数量 = 元素数"), Rows.Num(), MineralLegendTest::EntryCount);
	TestEqual(TEXT("面板高度与公式一致"), Panel.H,
		MineralLegendLayout::PanelHeightPx(MineralLegendTest::EntryCount));
	TestEqual(TEXT("面板宽度与常量一致"), Panel.W, MineralLegendLayout::PanelWidthPx);
	TestEqual(TEXT("贴左边距"), Panel.X, MineralLegendLayout::MarginPx);
	TestEqual(TEXT("贴上边距"), Panel.Y, MineralLegendLayout::MarginPx);

	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const FStarMapPanelRect& Row = Rows[Index];
		TestTrue(*FString::Printf(TEXT("第 %d 行在面板内（横向）"), Index),
			Row.X >= Panel.X && Row.X + Row.W <= Panel.X + Panel.W + 0.01f);
		TestTrue(*FString::Printf(TEXT("第 %d 行在面板内（纵向）"), Index),
			Row.Y >= Panel.Y && Row.Y + Row.H <= Panel.Y + Panel.H + 0.01f);
		TestEqual(*FString::Printf(TEXT("第 %d 行高度"), Index),
			Row.H, MineralLegendLayout::RowSlotHeight);
	}

	TestTrue(TEXT("第一行不与第二行重叠"), Rows[0].Y + Rows[0].H <= Rows[1].Y + 0.01f);
	TestTrue(TEXT("第一行与第二行之间没有空隙"),
		FMath::IsNearlyEqual(Rows[0].Y + Rows[0].H, Rows[1].Y, 0.01f));

	// 空元素表也要有一行占位，面板高度按 N = 0 算。
	FStarMapPanelRect EmptyPanel;
	TArray<FStarMapPanelRect> EmptyRows;
	MineralLegendTest::BuildLayout(EmptyPanel, EmptyRows, 0);
	TestEqual(TEXT("空表仍有一行占位"), EmptyRows.Num(), 1);
	TestEqual(TEXT("空表面板高度"), EmptyPanel.H, MineralLegendLayout::PanelHeightPx(0));
	TestTrue(TEXT("空表比 3 行矮"), EmptyPanel.H < Panel.H);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendHitTestTest,
	"StellarEra.StarMap.MineralLegend.HitTest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendHitTestTest::RunTest(const FString& Parameters)
{
	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> Rows;
	MineralLegendTest::BuildLayout(Panel, Rows);

	const FVector2D RowCenter(Rows[1].X + Rows[1].W * 0.5f, Rows[1].Y + Rows[1].H * 0.5f);
	TestEqual(TEXT("行中心命中该行"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, RowCenter), 1);

	const FVector2D FirstRowCenter(Rows[0].X + Rows[0].W * 0.5f, Rows[0].Y + Rows[0].H * 0.5f);
	TestEqual(TEXT("第一行中心命中第一行"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, FirstRowCenter), 0);

	// 面板右侧之外：不能命中任何行。
	TestEqual(TEXT("面板外不命中"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows,
			FVector2D(Panel.X + Panel.W + 5.f, RowCenter.Y)), INDEX_NONE);

	// 两行的接缝（上行的下沿 / 下行的上沿）：留了 HitInsetY 缩进，哪一行都不算。
	const FVector2D Seam(RowCenter.X, Rows[1].Y);
	TestEqual(TEXT("行接缝不命中"),
		UStarMapUISystem::HitTestMineralLegendRow(Rows, Seam), INDEX_NONE);

	TestEqual(TEXT("空矩形表返回 INDEX_NONE"),
		UStarMapUISystem::HitTestMineralLegendRow(
			TArray<FStarMapPanelRect>(), RowCenter), INDEX_NONE);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendFormatTest,
	"StellarEra.StarMap.MineralLegend.Format",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendFormatTest::RunTest(const FString& Parameters)
{
	TArray<FMineralLegendEntry> Entries;
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("铁");
		Entry.Color = FLinearColor(0.6f, 0.5f, 0.4f);
		Entry.Abundance = 0.42f;
		Entry.bVisible = true;
	}
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("铜");
		Entry.Color = FLinearColor(0.7f, 0.4f, 0.2f);
		Entry.Abundance = 0.25f;
		Entry.bVisible = false;
	}
	{
		FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.DisplayName = TEXT("金");
		Entry.Color = FLinearColor(1.f, 0.8f, 0.1f);
		Entry.Abundance = 0.05f;
		Entry.bVisible = true;
	}

	const TArray<FString> Lines =
		UStarMapUISystem::FormatMineralLegendLines(TEXT("地球"), Entries);

	TestEqual(TEXT("行数 = 标题 + 元素 + 提示"), Lines.Num(), Entries.Num() + 2);
	TestTrue(TEXT("标题含行星名"), Lines[0].Contains(TEXT("地球")));
	TestTrue(TEXT("可见行标「开」"), Lines[1].Contains(TEXT("开")));
	TestTrue(TEXT("隐藏行标「关」"), Lines[2].Contains(TEXT("关")));
	TestTrue(TEXT("百分比按 0..1 换算"), Lines[1].Contains(TEXT("42%")));
	TestTrue(TEXT("提示含 H 退出"), Lines.Last().Contains(TEXT("H 退出")));
	TestTrue(TEXT("提示含数字键"),
		Lines.Last().Contains(TEXT("数字键")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMineralLegendEmptyTest,
	"StellarEra.StarMap.MineralLegend.Empty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMineralLegendEmptyTest::RunTest(const FString& Parameters)
{
	const TArray<FString> Lines =
		UStarMapUISystem::FormatMineralLegendLines(TEXT("空星"), TArray<FMineralLegendEntry>());

	TestEqual(TEXT("只有标题 / 占位 / 提示"), Lines.Num(), 3);
	TestTrue(TEXT("占位行"), Lines[1].Contains(TEXT("无矿物元素")));
	TestTrue(TEXT("提示行还在"), Lines.Last().Contains(TEXT("H 退出")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
```

- [ ] **Step 2: 运行测试，确认失败**

构建（同 Step 7 的命令）会报 `error C2039: "ComputeMineralLegendLayout": 不是 "UStarMapUISystem" 的成员`。这就是失败证据。

- [ ] **Step 3: 加头文件声明**

`Source/StellarEra/Public/StarMapUISystem.h`：在 `FCutawayLegendEntry` 定义之后插入：

```cpp
/** 矿物观察图例的一行。纯数据，不含 UObject 引用。 */
struct FMineralLegendEntry
{
	FString DisplayName;

	/** 色块颜色 —— 与球内该元素方块的显示色一致。 */
	FLinearColor Color = FLinearColor::White;

	/** 该元素在行星总含量里的份额（0..1，各行相加约为 1）。 */
	float Abundance = 0.f;

	/** 当前是否显示。关掉的行整行变暗。 */
	bool bVisible = true;
};

/**
 * 矿物观察图例的固定布局。
 *
 * 宽度与字体无关：输入回调（鼠标命中）里拿不到 AHUD::Canvas，量不了字，
 * 所以宽度写死、高度只随行数变。绘制与命中各自调一次，几何完全一致。
 */
namespace MineralLegendLayout
{
	constexpr float Padding = 10.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float SeparatorThickness = 1.f;
	/** 每行占的高度。行与行之间没有缝，命中判定直接用这个矩形。 */
	constexpr float RowSlotHeight = 24.f;
	/** 标题 / 提示行高：固定值，不量字体。 */
	constexpr float TitleLineHeight = 22.f;
	constexpr float HintLineHeight = 22.f;
	/** 面板宽度：按最长的一行提示（约 34 个汉字宽）留出的固定值。 */
	constexpr float PanelWidthPx = 460.f;
	constexpr float MarginPx = 8.f;
	/** 命中判定的上下缩进：点在两行接缝上时不误命中相邻行。 */
	constexpr float HitInsetY = 2.f;

	/** 面板高度（夹进视口之前）。EntryCount = 0 时仍留一行占位。 */
	FORCEINLINE float PanelHeightPx(int32 EntryCount)
	{
		const int32 Slots = EntryCount > 0 ? EntryCount : 1;
		return Padding * 2.f + TitleLineHeight
			+ (SeparatorSpacing + SeparatorThickness) * 2.f
			+ RowSlotHeight * Slots + HintLineHeight;
	}
}
```

在 `FormatCutawayLegendLines` 声明之后插入：

```cpp
	/**
	 * 矿物观察图例的文本行：标题「矿物观察 — 行星名」，其后每元素一行
	 * 「名称  百分比  开/关」，最后一行是操作提示。
	 * 纯函数，便于无头测试。
	 */
	static TArray<FString> FormatMineralLegendLines(
		const FString& PlanetName,
		TArrayView<const FMineralLegendEntry> Entries);

	/** 算面板与每一行的矩形。EntryCount = 0 时 OutRowRects 里仍有一行（占位行）。 */
	static void ComputeMineralLegendLayout(
		const FVector2D& ViewportSizePx,
		int32 EntryCount,
		FStarMapPanelRect& OutPanel,
		TArray<FStarMapPanelRect>& OutRowRects);

	/** 屏幕坐标命中哪一行；没命中返回 INDEX_NONE。 */
	static int32 HitTestMineralLegendRow(
		const TArray<FStarMapPanelRect>& RowRects,
		const FVector2D& ScreenPosPx);
```

在私有区 `DrawCutawayLegend` 声明之后插入：

```cpp
	void DrawMineralLegend(
		AStellarEraHUD& Hud,
		const FString& PlanetName,
		const TArray<FMineralLegendEntry>& Entries,
		const FVector2D& ViewportSizePx);
```

- [ ] **Step 4: 加实现**

`Source/StellarEra/Private/StarMapUISystem.cpp`：在 `FormatCutawayLegendLines` 实现之后插入：

```cpp
TArray<FString> UStarMapUISystem::FormatMineralLegendLines(
	const FString& PlanetName,
	TArrayView<const FMineralLegendEntry> Entries)
{
	TArray<FString> Lines;
	Lines.Reserve(Entries.Num() + 2);

	Lines.Add(FString::Printf(TEXT("矿物观察 — %s"), *PlanetName));

	if (Entries.Num() == 0)
	{
		Lines.Add(TEXT("（无矿物元素）"));
	}
	else
	{
		for (const FMineralLegendEntry& Entry : Entries)
		{
			const FString Name = Entry.DisplayName.IsEmpty()
				? FString(TEXT("?"))
				: Entry.DisplayName;
			const int32 Percent = FMath::RoundToInt(
				FMath::Clamp(Entry.Abundance, 0.f, 1.f) * 100.f);
			Lines.Add(FString::Printf(TEXT("%s  %d%%  %s"), *Name, Percent,
				Entry.bVisible ? TEXT("开") : TEXT("关")));
		}
	}

	Lines.Add(TEXT("H 退出   点击图例/数字键筛选   右键拖转视角   滚轮缩放"));
	return Lines;
}

void UStarMapUISystem::ComputeMineralLegendLayout(
	const FVector2D& ViewportSizePx,
	int32 EntryCount,
	FStarMapPanelRect& OutPanel,
	TArray<FStarMapPanelRect>& OutRowRects)
{
	OutPanel = PlacePanelBesideAnchor(
		FVector2D(MineralLegendLayout::MarginPx, MineralLegendLayout::MarginPx),
		0.f,
		FVector2D(MineralLegendLayout::PanelWidthPx,
			MineralLegendLayout::PanelHeightPx(EntryCount)),
		ViewportSizePx, 0.f, MineralLegendLayout::MarginPx);

	const int32 SlotCount = FMath::Max(EntryCount, 1);
	OutRowRects.Reset();
	OutRowRects.Reserve(SlotCount);

	// 行区起点 = 内边距 + 标题 + 上分隔线（与 DrawMineralLegend 的排版同源）。
	const float RowsTop = OutPanel.Y + MineralLegendLayout::Padding
		+ MineralLegendLayout::TitleLineHeight
		+ MineralLegendLayout::SeparatorSpacing + MineralLegendLayout::SeparatorThickness
		+ MineralLegendLayout::SeparatorSpacing;

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		FStarMapPanelRect& Row = OutRowRects.AddDefaulted_GetRef();
		Row.X = OutPanel.X + MineralLegendLayout::Padding;
		Row.Y = RowsTop + SlotIndex * MineralLegendLayout::RowSlotHeight;
		Row.W = OutPanel.W - MineralLegendLayout::Padding * 2.f;
		Row.H = MineralLegendLayout::RowSlotHeight;
	}
}

int32 UStarMapUISystem::HitTestMineralLegendRow(
	const TArray<FStarMapPanelRect>& RowRects,
	const FVector2D& ScreenPosPx)
{
	for (int32 RowIndex = 0; RowIndex < RowRects.Num(); ++RowIndex)
	{
		const FStarMapPanelRect& Row = RowRects[RowIndex];
		// 上下各缩 HitInsetY：两行之间没有缝，不缩边会让接缝算作命中。
		if (ScreenPosPx.X >= Row.X && ScreenPosPx.X <= Row.X + Row.W
			&& ScreenPosPx.Y >= Row.Y + MineralLegendLayout::HitInsetY
			&& ScreenPosPx.Y <= Row.Y + Row.H - MineralLegendLayout::HitInsetY)
		{
			return RowIndex;
		}
	}
	return INDEX_NONE;
}
```

在文件末尾（`DrawCutawayLegend` 实现之后）追加绘制：

```cpp
void UStarMapUISystem::DrawMineralLegend(
	AStellarEraHUD& Hud,
	const FString& PlanetName,
	const TArray<FMineralLegendEntry>& Entries,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatMineralLegendLines(PlanetName, Entries);
	if (Lines.Num() < 3)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float SwatchSize = 14.f;
	constexpr float SwatchGap = 8.f;

	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> RowRects;
	ComputeMineralLegendLayout(ViewportSizePx, Entries.Num(), Panel, RowRects);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(0.75f, 0.75f, 0.75f, 1.f);

	Hud.DrawRect(Background, Panel.X, Panel.Y, Panel.W, Panel.H);
	Hud.DrawRect(Gold, Panel.X, Panel.Y, Panel.W, MineralLegendLayout::SeparatorThickness);
	Hud.DrawRect(Gold, Panel.X, Panel.Y + Panel.H - MineralLegendLayout::SeparatorThickness,
		Panel.W, MineralLegendLayout::SeparatorThickness);
	Hud.DrawRect(Gold, Panel.X, Panel.Y, MineralLegendLayout::SeparatorThickness, Panel.H);
	Hud.DrawRect(Gold, Panel.X + Panel.W - MineralLegendLayout::SeparatorThickness, Panel.Y,
		MineralLegendLayout::SeparatorThickness, Panel.H);

	const float TitleY = Panel.Y + MineralLegendLayout::Padding;
	Hud.DrawText(Lines[0], Gold, Panel.X + MineralLegendLayout::Padding, TitleY,
		Font, TextScale);

	const float SeparatorY = TitleY + MineralLegendLayout::TitleLineHeight
		+ MineralLegendLayout::SeparatorSpacing;
	Hud.DrawRect(Gold, Panel.X + MineralLegendLayout::Padding, SeparatorY,
		Panel.W - MineralLegendLayout::Padding * 2.f,
		MineralLegendLayout::SeparatorThickness);

	if (Entries.Num() > 0)
	{
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FMineralLegendEntry& Entry = Entries[Index];
			const FStarMapPanelRect& Row = RowRects[Index];
			const float SwatchY = Row.Y + (Row.H - SwatchSize) * 0.5f;

			// 关掉的元素整行变暗：色块压到 35% 亮度，文字用 Dim。
			const FLinearColor SwatchColor = Entry.bVisible
				? Entry.Color
				: FLinearColor(Entry.Color.R * 0.35f, Entry.Color.G * 0.35f,
					Entry.Color.B * 0.35f, Entry.Color.A);
			Hud.DrawRect(SwatchColor, Panel.X + MineralLegendLayout::Padding, SwatchY,
				SwatchSize, SwatchSize);
			Hud.DrawText(Lines[Index + 1], Entry.bVisible ? White : Dim,
				Panel.X + MineralLegendLayout::Padding + SwatchSize + SwatchGap,
				Row.Y, Font, TextScale);
		}
	}
	else
	{
		Hud.DrawText(Lines[1], Dim, Panel.X + MineralLegendLayout::Padding,
			RowRects[0].Y, Font, TextScale);
	}

	// 提示行贴面板底：行数变化时提示不跟着上下跳。
	const float HintY = Panel.Y + Panel.H - MineralLegendLayout::Padding
		- MineralLegendLayout::HintLineHeight;
	Hud.DrawRect(Gold, Panel.X + MineralLegendLayout::Padding,
		HintY - MineralLegendLayout::SeparatorSpacing
			- MineralLegendLayout::SeparatorThickness,
		Panel.W - MineralLegendLayout::Padding * 2.f,
		MineralLegendLayout::SeparatorThickness);
	Hud.DrawText(Lines.Last(), Dim, Panel.X + MineralLegendLayout::Padding, HintY,
		Font, TextScale);
}
```

- [ ] **Step 5: 补 BOM**

```bash
cd "F:/workspace/ue/StellarEra"
if [ "$(head -c 3 Source/StellarEra/Private/MineralLegendTests.cpp | od -An -tx1 | tr -d ' \n')" != "efbbbf" ]; then
  printf '\xEF\xBB\xBF' > Source/StellarEra/Private/MineralLegendTests.cpp.tmp
  cat Source/StellarEra/Private/MineralLegendTests.cpp >> Source/StellarEra/Private/MineralLegendTests.cpp.tmp
  mv Source/StellarEra/Private/MineralLegendTests.cpp.tmp Source/StellarEra/Private/MineralLegendTests.cpp
fi
head -c 3 Source/StellarEra/Private/MineralLegendTests.cpp | od -An -tx1
```

`StarMapUISystem.h/.cpp` 本来就有 BOM。

- [ ] **Step 6: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- [ ] **Step 7: 跑本任务的测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.MineralLegend" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralLegendTests.log"
grep -c "Result={Success}" F:/workspace/ue/StellarEra/Saved/Logs/MineralLegendTests.log
grep -c "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralLegendTests.log
```

预期 `Success` = 4，`Fail` = 0。

- [ ] **Step 8: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "feat(矿区): 矿物观察图例的布局 / 命中 / 文本纯函数

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StarMapUISystem.h \
  Source/StellarEra/Private/StarMapUISystem.cpp \
  Source/StellarEra/Private/MineralLegendTests.cpp
```

---

### Task 5: 控制器与星图 UI 切到新组件

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`
- Modify: `Source/StellarEra/Public/StarMapUISystem.h`
- Modify: `Source/StellarEra/Private/StarMapUISystem.cpp`
- Delete: `Source/StellarEra/Private/CutawayLegendTests.cpp`

**Interfaces:**
- Consumes: `UPlanetMineralViewComponent`（Task 3）、`FMineralLegendEntry` / `MineralLegendLayout` / `ComputeMineralLegendLayout` / `HitTestMineralLegendRow` / `DrawMineralLegend`（Task 4）
- Produces: `AStellarEraPlayerController::GetActiveMineralView()`；`OnMineralViewToggle()` / `OnMineralFilter(const FInputActionValue&, int32)` / `OnMineralShowAll(const FInputActionValue&)` / `ExitMineralViewIfActive()`

> **本任务是原子替换。** 控制器的字段、绑定、回调与星图 UI 必须**同一次**改完：
> 中间态（控制器已指向新组件、UI 还在调 `GetActiveCutaway`）编译不过。

- [ ] **Step 1: 改控制器头文件**

`Source/StellarEra/Public/StellarEraPlayerController.h`：

1. 删掉第 8 行 `#include "PlanetCutawayMath.h"`。
2. `class UPlanetCutawayComponent;` → `class UPlanetMineralViewComponent;`。
3. `GetActiveCutaway()` 换成：

```cpp
	/** 当前激活的矿物透视组件；未激活时为空。星图 UI 用它画图例、喂方块显隐。 */
	UPlanetMineralViewComponent* GetActiveMineralView() const { return ActiveMineralView; }
```

4. `OnCutawayToggle` / `OnCutawayDrag` / `ExitCutawayIfActive` / `PickCutawayRing` 四个声明（含注释）整块替换为：

```cpp
	/**
	 * H 键：进入 / 退出矿物透视。只在星图里生效。
	 *
	 * 进入条件：当前选中的是体素行星、且它带地质场。失败时给 2 秒提示，不改变状态。
	 * 退出条件：再按一次 H、退出星图、换 Pawn、或选中天体变了（见 PlayerTick）。
	 */
	void OnMineralViewToggle();

	/** 数字键 1..9：切换第 ElementIndex 个元素的显示（下标从 0 起）。 */
	void OnMineralFilter(const FInputActionValue& Value, int32 ElementIndex);

	/** 数字键 0：全部打开。 */
	void OnMineralShowAll(const FInputActionValue& Value);

	/** 退出矿物透视（幂等）。退出星图 / 换 Pawn / 选中变化 / EndPlay 都走这里。 */
	void ExitMineralViewIfActive();
```

5. 属性区把 `IA_CutawayToggle` / `IA_CutawayDrag` / `ActiveCutaway` / `CutawayDragRing` / `bCutawayDragActive` / `bCutawayRotatedThisDrag` / `CutawayRingPickTolerancePx` 整块替换为：

```cpp
	/** 矿物透视开关（H）。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_MineralViewToggle;

	/** 数字键 1..9：按下标切换元素显示。下标 = 运行期元素顺序。 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInputAction>> IA_MineralFilter;

	/** 数字键 0：全部打开。 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_MineralShowAll;

	/** 当前激活的矿物透视组件。行星销毁时 GC 会把它置空。 */
	UPROPERTY(Transient)
	TObjectPtr<UPlanetMineralViewComponent> ActiveMineralView;
```

- [ ] **Step 2: 改控制器实现**

`Source/StellarEra/Private/StellarEraPlayerController.cpp`：

1. 第 6 行 `#include "PlanetCutawayComponent.h"` → `#include "PlanetMineralViewComponent.h"`；并在其下一行加：

```cpp
#include "StarMapUISystem.h"
```

（图例布局与命中是 `UStarMapUISystem` 的静态函数，控制器要用。）

2. `SetupInputComponent` 里的绑定（原 147–150 行）整块替换为：

```cpp
	EnhancedInput->BindAction(IA_MineralViewToggle, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnMineralViewToggle);

	// 数字键用变参 payload：同一个回调按下标分派，省掉九个一模一样的函数。
	for (int32 ElementIndex = 0; ElementIndex < IA_MineralFilter.Num(); ++ElementIndex)
	{
		EnhancedInput->BindAction(IA_MineralFilter[ElementIndex], ETriggerEvent::Started,
			this, &AStellarEraPlayerController::OnMineralFilter, ElementIndex);
	}
	EnhancedInput->BindAction(IA_MineralShowAll, ETriggerEvent::Started,
		this, &AStellarEraPlayerController::OnMineralShowAll);
```

3. `OnPossess` 里 `ExitCutawayIfActive();` → `ExitMineralViewIfActive();`（注释同步改成「一律退出矿物透视」）。

4. `BuildInputAssets`：

   原 `IA_CutawayToggle` / `IA_CutawayDrag` 两行替换为：

```cpp
	IA_MineralViewToggle = MakeAction(this, TEXT("IA_MineralViewToggle"), EInputActionValueType::Boolean);
	IA_MineralShowAll = MakeAction(this, TEXT("IA_MineralShowAll"), EInputActionValueType::Boolean);
	IA_MineralFilter.Reset();
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		IA_MineralFilter.Add(MakeAction(this,
			*FString::Printf(TEXT("IA_MineralFilter%d"), Slot),
			EInputActionValueType::Boolean));
	}
```

   原「剖面：H 进出；左键位移转切面……」注释与两行 `MapKey`（314–318 行）替换为：

```cpp
	// 矿物透视：H 进出；数字键 1..9 切换前 9 个元素，0 全部打开。
	MapKey(MappingContext, IA_MineralViewToggle, EKeys::H);

	const FKey DigitKeys[] = {
		EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine
	};
	for (int32 Slot = 0; Slot < IA_MineralFilter.Num(); ++Slot)
	{
		MapKey(MappingContext, IA_MineralFilter[Slot], DigitKeys[Slot]);
	}
	MapKey(MappingContext, IA_MineralShowAll, EKeys::Zero);
```

5. `OnLook` 里星图分支的门（原 385–390 行）改成：

```cpp
		// 矿物透视是唯一例外：左键归图例点击，视角只认右键（与角色同一套门）。
		if (ActiveMineralView && !bLookHeld)
		{
			return;
		}
```

6. `PlayerTick` 的剖面守卫整块替换为：

```cpp
	// 矿物透视绑在「选中的那颗行星」上。焦点被改选、或 Pawn 已经不是星图相机，
	// 就立刻退出 —— 否则球壳与方块会跟着一颗已取消选中的行星。
	if (ActiveMineralView)
	{
		UCosmosSimulationSubsystem* ViewSubsystem = GetWorld()
			? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
			: nullptr;
		const bool bSamePlanet = ViewSubsystem &&
			ViewSubsystem->GetSelectedBody() == ActiveMineralView->GetViewPlanet();
		if (!Cast<AStarMapCameraPawn>(GetPawn()) || !bSamePlanet)
		{
			ExitMineralViewIfActive();
		}
	}
```

7. `OnEditModeToggle` 的门改成：

```cpp
	// 矿物透视下左键归图例：编辑模式会抢左键并改写体素，所以直接忽略切换，
	// 保持进入观察时强制退出的状态。
	if (ActiveMineralView)
	{
		return;
	}
```

8. `OnCutawayToggle` / `OnCutawayDrag` / `ExitCutawayIfActive` / `PickCutawayRing` 四个函数（原 809–941 行）整块替换为：

```cpp
void AStellarEraPlayerController::OnMineralViewToggle()
{
	if (!Cast<AStarMapCameraPawn>(GetPawn()))
	{
		return;
	}

	if (ActiveMineralView)
	{
		ExitMineralViewIfActive();
		return;
	}

	UCosmosSimulationSubsystem* Subsystem = GetWorld()
		? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	AVoxelPlanetActor* Planet = Subsystem
		? Cast<AVoxelPlanetActor>(Subsystem->GetSelectedBody())
		: nullptr;
	UPlanetMineralViewComponent* MineralView =
		Planet ? Planet->GetMineralViewComponent() : nullptr;
	if (!MineralView)
	{
		ShowPromptFor(TEXT("矿物观察只支持行星"), 2.f);
		return;
	}

	if (!MineralView->Activate(Planet))
	{
		// 失败只有三种：没有地质场、采样参数非法、观察体/材质创建失败
		// （后两种会先打 Error 日志）。
		ShowPromptFor(TEXT("该行星没有矿物分布"), 2.f);
		return;
	}

	// 观察里左键只点图例、右键转视角：若此前按过 G，先退出编辑模式。
	ForceExitEditMode(GetPawn());

	// 只拉远、不改朝向：按 H 前看哪儿，进入后还是那个角度（spec §3）。
	if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		StarMap->FocusOn(Planet, 3.0 * Planet->GetSurfaceRadiusCm());
	}

	ActiveMineralView = MineralView;
}

void AStellarEraPlayerController::OnMineralFilter(
	const FInputActionValue& Value, int32 ElementIndex)
{
	if (!ActiveMineralView)
	{
		return;
	}
	ActiveMineralView->ToggleElement(ElementIndex);
}

void AStellarEraPlayerController::OnMineralShowAll(const FInputActionValue& Value)
{
	if (!ActiveMineralView)
	{
		return;
	}
	ActiveMineralView->SetAllElementsVisible(true);
}

void AStellarEraPlayerController::ExitMineralViewIfActive()
{
	if (!ActiveMineralView)
	{
		return;
	}

	ActiveMineralView->Deactivate();
	ActiveMineralView = nullptr;
}
```

9. `OnStarMapSelectStarted`：

   第一行改成 `UPlanetEditComponent* Edit = ActiveMineralView ? nullptr : GetEditComponentInEditMode();`，注释同步（「矿物透视激活期间左键归图例」）。

   原 `if (ActiveCutaway) { ... 圆环拾取 ... return; }` 整块替换为：

```cpp
	if (ActiveMineralView)
	{
		double MouseX = 0.0;
		double MouseY = 0.0;
		if (!GetMousePosition(MouseX, MouseY))
		{
			return;
		}

		// 与绘制同一份纯函数：视口尺寸从控制器查（Canvas 只在 PostRender 有效）。
		int32 ViewportSizeX = 0;
		int32 ViewportSizeY = 0;
		GetViewportSize(ViewportSizeX, ViewportSizeY);
		const FVector2D ViewportSize(
			static_cast<float>(ViewportSizeX), static_cast<float>(ViewportSizeY));

		FStarMapPanelRect Panel;
		TArray<FStarMapPanelRect> RowRects;
		UStarMapUISystem::ComputeMineralLegendLayout(
			ViewportSize, ActiveMineralView->NumElements(), Panel, RowRects);

		const int32 RowIndex = UStarMapUISystem::HitTestMineralLegendRow(
			RowRects, FVector2D(MouseX, MouseY));
		if (RowRects.IsValidIndex(RowIndex))
		{
			ActiveMineralView->ToggleElement(RowIndex);
		}

		// 观察模式下左键不再拾取天体，也不转视角（转视角归右键）。
		return;
	}
```

10. `OnStarMapSelectCompleted` 的开头剖面块整块替换为：

```cpp
	if (ActiveMineralView)
	{
		// 观察模式下左键只用来点图例，松手不做任何事。
		return;
	}
```

11. `ToggleStarMap` 里的 `ExitCutawayIfActive();` → `ExitMineralViewIfActive();`（注释「进入星图 …」不需要改）。

- [ ] **Step 3: 改星图 UI**

`Source/StellarEra/Private/StarMapUISystem.cpp`：

1. 第 10 行 `#include "PlanetCutawayComponent.h"` → `#include "PlanetMineralViewComponent.h"`。
2. `Draw()` 里的图例块（原 307–335 行）整块替换为：

```cpp
	// 矿物观察图例：固定左上角，画在最后（压在所有天体面板之上）。
	if (UPlanetMineralViewComponent* MineralView = PlayerController->GetActiveMineralView())
	{
		AVoxelPlanetActor* ViewPlanet = MineralView->GetViewPlanet();
		if (ViewPlanet)
		{
			const TArray<FPlanetGeologyElement>& Elements = MineralView->GetElements();
			const TSharedPtr<const FPlanetGeologyField> Geology = MineralView->GetGeologyField();
			const float TotalAbundance = Geology.IsValid()
				? FMath::Max(Geology->GetTotalAbundance(), 1e-6f)
				: 1.f;

			TArray<FMineralLegendEntry> Entries;
			Entries.Reserve(Elements.Num());
			for (int32 ElementIndex = 0; ElementIndex < Elements.Num(); ++ElementIndex)
			{
				const FPlanetGeologyElement& Element = Elements[ElementIndex];
				FMineralLegendEntry& Entry = Entries.AddDefaulted_GetRef();
				Entry.DisplayName = Element.DisplayName.IsEmpty()
					? Element.Id
					: Element.DisplayName;
				Entry.Color = Element.Color;
				Entry.Abundance = Element.Abundance / TotalAbundance;
				Entry.bVisible = MineralView->IsElementVisible(ElementIndex);
			}

			DrawMineralLegend(
				Hud, ViewPlanet->GetActorNameOrLabel(), Entries, ViewportSize);
		}
	}
```

3. 删掉 `FormatCutawayLegendLines` 实现（原 180–208 行）与 `DrawCutawayLegend` 实现（原 414–520 行）。

`Source/StellarEra/Public/StarMapUISystem.h`：

4. 删掉 `FCutawayLegendEntry`（38–48 行）、`FormatCutawayLegendLines` 声明（71–80 行）、私有 `DrawCutawayLegend` 声明（105–109 行）。

- [ ] **Step 4: 删旧图例测试**

```bash
cd "F:/workspace/ue/StellarEra"
git rm Source/StellarEra/Private/CutawayLegendTests.cpp
```

（它的两条断言 —— 百分比格式与空表 —— 已由 Task 4 的 `Format` / `Empty` 覆盖；`ElementOrder` 那条由 Task 1 的 `SampledDefinitions` 覆盖。）

- [ ] **Step 5: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期 `Build succeeded`。若报 `ESectionRing` 未定义，说明还有残留引用没删干净 —— `grep -rn "ESectionRing" Source/StellarEra/` 应为空。

- [ ] **Step 6: 跑测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralSwitchTests.log"
grep -c "Result={Success}" F:/workspace/ue/StellarEra/Saved/Logs/MineralSwitchTests.log
grep -c "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralSwitchTests.log
grep "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralSwitchTests.log
```

预期 `Fail` = 0；`Success` 至少 12（Task 1 的 6 + Task 3 的 2 + Task 4 的 4）。把实际数字记进汇报。

- [ ] **Step 7: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "feat(矿区): 控制器与星图 UI 切到矿物透视组件

H 进玻璃球观察、数字键 1..9 与图例点击筛选、0 全开；
删除拖拽圆环的输入与图例旧接口。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp \
  Source/StellarEra/Public/StarMapUISystem.h \
  Source/StellarEra/Private/StarMapUISystem.cpp \
  Source/StellarEra/Private/CutawayLegendTests.cpp
```

（`git rm` 已经把它从工作区删掉；把它写进 pathspec 是为了让这次提交显式记录这次删除。）

---

### Task 6: 删除旧剖面全部文件与残留引用

**Files:**
- Delete: `Source/StellarEraPlanet/Public/PlanetCutawayComponent.h`
- Delete: `Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp`
- Delete: `Source/StellarEraPlanet/Public/PlanetCutawayMath.h`
- Delete: `Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp`
- Delete: `Source/StellarEraPlanet/Public/PlanetSectionImage.h`
- Delete: `Source/StellarEraPlanet/Private/PlanetSectionImage.cpp`
- Delete: `Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp`
- Modify: `Source/StellarEraPlanet/Public/VoxelPlanetActor.h:18,231-236`
- Modify: `Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp:18,120-122`
- Modify: `Source/StellarEraPlanet/StellarEraPlanet.Build.cs`（删 RHI）
- Modify: `Source/StellarEra/StellarEra.Build.cs:20-22`（假注释）

**Interfaces:**
- Consumes: Task 5 已让控制器与 UI 完全不再引用旧类型
- Produces: 仓库里 `Cutaway` / `PlanetSectionImage` / `ESectionRing` 零命中

> 本任务全程只删不加。删完的唯一验收标准是：
> `grep -rn "Cutaway\|PlanetSectionImage\|ESectionRing" Source/` **无输出**，且全量测试通过。

- [ ] **Step 1: 删文件**

```bash
cd "F:/workspace/ue/StellarEra"
git rm \
  Source/StellarEraPlanet/Public/PlanetCutawayComponent.h \
  Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp \
  Source/StellarEraPlanet/Public/PlanetCutawayMath.h \
  Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp \
  Source/StellarEraPlanet/Public/PlanetSectionImage.h \
  Source/StellarEraPlanet/Private/PlanetSectionImage.cpp \
  Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp
```

- [ ] **Step 2: 清掉行星 Actor 上的旧成员**

`Source/StellarEraPlanet/Public/VoxelPlanetActor.h`：

1. 删第 18 行 `class UPlanetCutawayComponent;`（`class UPlanetMineralViewComponent;` 保留）。
2. 删掉这一整块（原 231–236 行，含上一行注释）：

```cpp
	/** 矿物剖面视图组件。构造时自动创建；未激活时不 Tick、不建任何资产。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet|Cutaway")
	TObjectPtr<UPlanetCutawayComponent> CutawayComponent;

	UFUNCTION(BlueprintPure, Category = "Planet|Cutaway")
	UPlanetCutawayComponent* GetCutawayComponent() const { return CutawayComponent; }
```

删完后 `EditGrid` 与 `MineralViewComponent` 之间不应再有任何 `Cutaway` 字样。

`Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp`：

3. 删第 18 行 `#include "PlanetCutawayComponent.h"`（`#include "PlanetMineralViewComponent.h"` 保留）。
4. 删构造函数里的两行：

```cpp
	CutawayComponent = CreateDefaultSubobject<UPlanetCutawayComponent>(TEXT("PlanetCutaway"));
	CutawayComponent->SetupAttachment(RootComponent);
```

- [ ] **Step 3: 去掉只被旧组件用到的 RHI 依赖**

`Source/StellarEraPlanet/StellarEraPlanet.Build.cs`：删掉这两行（含注释）：

```cs
		// RHI：FUpdateTextureRegion2D（RHITypes.h）是 UTexture2D::UpdateTextureRegions 的参数类型。
		PrivateDependencyModuleNames.AddRange(new string[] { "RHI" });
```

依据（已核）：`grep -rn "RHITypes\|FUpdateTextureRegion2D\|UpdateTextureRegions" Source/`
在删除前只命中 `PlanetCutawayComponent.cpp`；删完后为零。

- [ ] **Step 4: 修掉 Build.cs 里已经不成立的理由**

`Source/StellarEra/StellarEra.Build.cs` 第 20–22 行现在写着「公开头
`StellarEraPlayerController.h` 直接包含 `PlanetCutawayMath.h`」—— 该 include 已在
Task 5 删除，这句是假话。整段替换为：

```cs
// StellarEraPlanet 降为 **Private**：本模块没有任何公开头 include 行星模块的头
// （StellarEraPlayerController.h 里的行星类型一律前置声明 + TObjectPtr）。
// 历史原因 —— 公开头曾以成员类型引用了剖面数学头里的枚举，所以它被写成 Public。
```

同时把 `PublicDependencyModuleNames` 里的 `"StellarEraPlanet"` 从 Public 数组移到
`PrivateDependencyModuleNames` 数组：

```cs
// 改前
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "StellarEraPlanet",
		});

// 改后
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine",
		});
```

并在 Private 数组里加一行：

```cs
			"StellarEraPlanet", // 行星 Actor / 地质场 / 纹理；只在 .cpp 里 include
```

> 若构建报 `Public header ... includes ... from a private dependency`，说明还有
> 公开头漏了行星模块的 include —— 先 `grep -rn "include \"" Source/StellarEra/Public/`
> 找出来，把它改成前置声明；**不要**用把 StellarEraPlanet 挪回 Public 来掩盖。

- [ ] **Step 5: 构建（先关编辑器）**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期 `Build succeeded`。

- [ ] **Step 6: 零残留 + 全量测试**

```bash
cd "F:/workspace/ue/StellarEra"
grep -rn "Cutaway\|PlanetSectionImage\|ESectionRing" Source/
echo "exit=$?"
```

预期：`grep` 无输出（`exit=1`）。

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="F:/workspace/ue/StellarEra/Saved/Logs/MineralCleanupTests.log"
grep -c "Result={Success}" F:/workspace/ue/StellarEra/Saved/Logs/MineralCleanupTests.log
grep "Result={Fail}" F:/workspace/ue/StellarEra/Saved/Logs/MineralCleanupTests.log
```

预期 `Fail` 无输出。**把 Success 数字和 Task 5 的对照**：本任务删掉的是
`PlanetCutawayTests.cpp` 里的 8 条 `StellarEra.Planet.Cutaway.*`（`Math.*` 5 条 +
`Image.*` 3 条），`CutawayLegendTests.cpp` 的 2 条已在 Task 5 删除。
所以 Success 应当**正好**比 Task 5 少 8。用下面这条机械核对，`0` 才是对的：

```bash
grep -cE "Result=\{(Success|Fail)\}.*StellarEra\.(Planet|StarMap)\.Cutaway" \
  F:/workspace/ue/StellarEra/Saved/Logs/MineralCleanupTests.log
```

（若日志里测试名与结果不在同一行，改成先 `grep "StellarEra.*Cutaway"` 看有没有输出，
无输出即可。若 Success 少的不止 8，说明误删了别的测试文件，回头查 `git status`。）

- [ ] **Step 7: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "refactor(矿区): 删除旧剖面组件、数学与剖面纹理

切半球模式被矿物透视取代：行星侧组件、ESectionRing、FPlanetSectionImage 与
旧测试全部移除；行星 Actor 不再创建剖面组件，RHI 依赖随之去掉。
StellarEraPlanet 降为 StellarEra 的私有依赖（公开头已无行星模块 include）。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetCutawayComponent.h \
  Source/StellarEraPlanet/Private/PlanetCutawayComponent.cpp \
  Source/StellarEraPlanet/Public/PlanetCutawayMath.h \
  Source/StellarEraPlanet/Private/PlanetCutawayMath.cpp \
  Source/StellarEraPlanet/Public/PlanetSectionImage.h \
  Source/StellarEraPlanet/Private/PlanetSectionImage.cpp \
  Source/StellarEraPlanet/Private/PlanetCutawayTests.cpp \
  Source/StellarEraPlanet/Public/VoxelPlanetActor.h \
  Source/StellarEraPlanet/Private/VoxelPlanetActor.cpp \
  Source/StellarEraPlanet/StellarEraPlanet.Build.cs \
  Source/StellarEra/StellarEra.Build.cs
```

---

### Task 7: `UniverseMap` 的 PIE 验收（人工）

**Files:** 无代码改动（只验证）

**Interfaces:**
- Consumes: Task 1–6 的全部产物
- Produces: 一份「能看 / 不能看」的结论。**不能看就进 Task 8 或按下面诊断。**

> **编辑器必须在前台。** 后台整进程限 3 FPS，体素与采样都会慢上百倍，
> 你会把「慢」误读成「卡死」。

- [ ] **Step 1: 起 PIE**

编辑器打开 `Content/Maps/UniverseMap.umap` → 点 Play（选 Play In Editor，不要 Standalone）。

- [ ] **Step 2: 进星图并选中行星**

按 `M` 进星图 → 左键点一颗行星（有体素的那颗，`BP_VoxelPlanet*`）→ 确认右侧天体面板出现。

- [ ] **Step 3: 按 H**

预期（逐条打勾，任何一条不符就记下来）：

1. 星图相机平滑拉远到约 3 倍半径；**视角朝向不变**（不是自动转到某个方向）。
2. 行星的体素表面**消失**。
3. 出现一颗**半透明玻璃球**，边缘比中心亮一圈（菲涅尔）。
4. 球内出现**成片分层**的不透明小方块，颜色与图例色块一致。
5. 左上角图例面板出现：标题 `矿物观察 — <行星名>`、每行 `名称  百分比%  开`、底部一行操作提示。
6. 帧率没有掉到个位数（`stat fps`，编辑器前台）。

- [ ] **Step 4: 筛选**

1. 用鼠标点第 1 行 → 该行变灰、文字变暗、球内该颜色方块消失；再点一次恢复。
2. 按 `2` / `3` → 对应行状态翻转（下标从 1 起对应第 1 行是 1、第 2 行是 2 …）。
3. 按 `0` → 所有行变回 `开`，所有方块恢复。
4. 只留一种元素时，能清楚看出该元素在球内的分层分布。

- [ ] **Step 5: 观察期间的跟随**

等 15–30 秒（三体在跑），确认球壳与方块**跟着行星一起移动**，不留在原地、不抖动。
若漂移：查 `PlanetMineralViewComponent.cpp` 的 Tick 是否只写 `SetActorLocation(Planet->GetActorLocation())`。

- [ ] **Step 6: 退出**

按 `H` → 行星体素表面、大气、碰撞全部恢复，图例消失，相机不动。
再用同样的步骤进一次、按 `M` 退出星图 → 观察模式应一并退出（行星已恢复）。
再进一次、按 `G` → 编辑模式**不应**进入（观察期间被挡住）。

- [ ] **Step 7: 版式检查**

图例底部提示行必须在一行内、不换行、不超出 460 px 面板（`H 退出   点击图例/数字键筛选   右键拖转视角   滚轮缩放`）。
若溢出，把 `StarMapUISystem.h` 里 `MineralLegendLayout::PanelWidthPx` 调到 500 再验一次。

- [ ] **Step 8: 记录结论**

在 `docs/superpowers/plans/2026-10-25-mineral-ghost-view.md` 末尾追加一节
「PIE 验收记录」，写：进入耗时（目测秒数）、方块总数（日志 `MineralView` 行）、
命中/未命中的条目、以及是否触发 Task 8。

**诊断对照：**

| 现象 | 先查 |
|---|---|
| 按 H 无反应 | 是否在星图（Pawn 是 `AStarMapCameraPawn`）、选中的是不是体素行星 |
| 提示「该行星没有矿物分布」 | 日志里 `FPlanetGeologyField` 的加载报告；`Content/Geology/Planets/` 预设是否为空 |
| 球壳在、没有方块 | `PlanetMineralBlocks` 的 `SampleCount` 与各方块数日志；`SpacingRatio` 是否被改小 |
| 行星没消失、球壳套在实体球外 | 进 Task 8（`SetActorHiddenInGame` 对体素无效） |
| 球壳全黑 / 不透 | `M_PlanetGhost` 的 BlendMode 是否为 Translucent、Opacity 参数是否被连上 |
| 方块全黑 | `M_MineralBlock` 的 `bUsedWithInstancedStaticMeshes` 是否为真、Emissive 是否连了 |

---

### Task 8: 兜底 —— 用全局 MPC 遮掉行星体素（**仅在 Task 7 判定必要时执行**）

**Files:**
- Modify: `Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h`
- Modify: `Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`

**Interfaces:**
- Consumes: Task 3 的组件；旧的 `MPC_CutawayOrigin` / `MPC_CutawayNormal` 资产（磁盘上仍在）
- Produces: `UPlanetMineralViewComponent::ApplyFullMask()` / `ClearFullMask()`；`bMaskVoxels` 开关

原理（沿用旧剖面实测过的遮罩语义，见已删除的 `PlanetCutawayComponent::ApplyClipMask`）：

- `MPC_CutawayOrigin.CutawayOrigin = (Center, Active)`；
- `MPC_CutawayNormal.CutawayNormal = (ClipNormal, InsideRadiusCm)`；
- 材质丢弃「距 Center 小于 InsideRadiusCm、且在 ClipNormal 正侧」的体素。

把 `Center` 换成「球心沿相机反方向 4R」，`InsideRadiusCm` 写 8R、`ClipNormal` 指向
相机，则被丢弃的就是整颗行星，而不会碰到更远的其它天体。

- [ ] **Step 1: 头文件加开关与 MPC 成员**

`Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h`：

1. `#include` 区加：

```cpp
#include "UObject/WeakObjectPtrTemplates.h"
```

（`TWeakObjectPtr<UMaterialParameterCollection>` 需要；若已有更宽的 include 可省。）

2. 前置声明区加：

```cpp
class UMaterialParameterCollection;
```

3. `public:` 里 `Deactivate()` 之后加：

```cpp
	/**
	 * 兜底遮罩：隐藏行星对体素网格无效时，改用全星球 OpacityMask = 0。
	 * 代价是全局 MPC —— 期间该球附近别的体素行星也会被遮掉，除非它们离得更远。
	 */
	bool IsMaskingVoxels() const { return bMaskVoxels; }
	void SetMaskVoxels(bool bEnable);
```

4. `private:` 区加：

```cpp
	/** 每帧按相机重算「全遮」参数；bMaskVoxels 为假时什么都不做。 */
	void ApplyFullMask();
	void ClearFullMask();

	/** 兜底遮罩开关。默认关；只在 SetActorHiddenInGame 被证明无效后打开。 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView")
	bool bMaskVoxels = false;

	TWeakObjectPtr<UMaterialParameterCollection> CutawayOriginMpc;
	TWeakObjectPtr<UMaterialParameterCollection> CutawayNormalMpc;
```

- [ ] **Step 2: 实现**

`Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp`：

1. include 区加：

```cpp
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
```

2. 匿名 namespace 里（文件顶部那个）加常量：

```cpp
	const TCHAR* CutawayOriginMpcPath = TEXT("/Game/Materials/MPC_CutawayOrigin.MPC_CutawayOrigin");
	const TCHAR* CutawayNormalMpcPath = TEXT("/Game/Materials/MPC_CutawayNormal.MPC_CutawayNormal");
	const TCHAR* CutawayOriginParameter = TEXT("CutawayOrigin");
	const TCHAR* CutawayNormalParameter = TEXT("CutawayNormal");
```

3. `SetAllElementsVisible` 之后加：

```cpp
void UPlanetMineralViewComponent::SetMaskVoxels(bool bEnable)
{
	if (bMaskVoxels == bEnable)
	{
		return;
	}
	bMaskVoxels = bEnable;

	if (!bEnable)
	{
		ClearFullMask();
	}
}

void UPlanetMineralViewComponent::ApplyFullMask()
{
	AVoxelPlanetActor* ViewPlanet = GetViewPlanet();
	if (!bMaskVoxels || !ViewPlanet)
	{
		return;
	}

	if (!CutawayOriginMpc.IsValid())
	{
		CutawayOriginMpc = LoadObject<UMaterialParameterCollection>(nullptr, CutawayOriginMpcPath);
	}
	if (!CutawayNormalMpc.IsValid())
	{
		CutawayNormalMpc = LoadObject<UMaterialParameterCollection>(nullptr, CutawayNormalMpcPath);
	}
	if (!CutawayOriginMpc.IsValid() || !CutawayNormalMpc.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MineralView] 兜底遮罩资产缺失：%s / %s"),
			CutawayOriginMpcPath, CutawayNormalMpcPath);
		bMaskVoxels = false;
		return;
	}

	// 相机位置：没有玩家相机就不动遮罩（宁可多显示一帧，也不要闪一下全空）。
	FVector CameraLocation = FVector::ZeroVector;
	if (const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			CameraLocation = PC->PlayerCameraManager->GetCameraLocation();
		}
		else
		{
			return;
		}
	}
	else
	{
		return;
	}

	const FVector Center = ViewPlanet->GetActorLocation();
	const float RadiusCm = ViewPlanet->GetSurfaceRadiusCm();
	const FVector AwayFromCamera = (Center - CameraLocation).GetSafeNormal();
	const FVector MaskOrigin = Center + AwayFromCamera * (4.0 * RadiusCm);

	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayOriginMpc.Get(), CutawayOriginParameter,
		FLinearColor(MaskOrigin.X, MaskOrigin.Y, MaskOrigin.Z, 1.f));

	// 平面法线指向相机：被丢掉的是「球心到相机这一侧」，正好整颗行星。
	const FVector ToCamera = -AwayFromCamera;
	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayNormalMpc.Get(), CutawayNormalParameter,
		FLinearColor(ToCamera.X, ToCamera.Y, ToCamera.Z, 8.0 * RadiusCm));
}

void UPlanetMineralViewComponent::ClearFullMask()
{
	if (CutawayOriginMpc.IsValid())
	{
		UKismetMaterialLibrary::SetVectorParameterValue(
			this, CutawayOriginMpc.Get(), CutawayOriginParameter,
			FLinearColor(0.f, 0.f, 0.f, 0.f));
	}
}
```

4. `TickComponent` 里，跟随观察 Actor 之后加：

```cpp
	ApplyFullMask();
```

5. `Deactivate` 里恢复大气之后加：

```cpp
	ClearFullMask();
```

> `Deactivate` 末尾本来就把 `Planet` 置空；`ClearFullMask` 必须在置空**之前**调用
> （它只用 MPC，不看行星，但保持「退出即清」的顺序更安全）。

- [ ] **Step 3: 构建 + 测试**

同 Task 6 Step 5 / Step 6 的两条命令（日志名换 `MineralMaskFallbackTests.log`）。

- [ ] **Step 4: 重新验 Task 7 的第 3 步与第 6 步**

验证路径：把 `bMaskVoxels` 的默认值**临时**改成 `true` → 构建 → 跑 Task 7 的
Step 3（行星体素应消失）与 Step 6（退出后应完全恢复，且附近别的天体不受影响）
→ 把默认值改回 `false` → 再构建一次。

理由：组件属性虽然在 Details 里可改，但观察期间选中/编辑行星的操作路径并不稳定，
直接用默认值验证最省事。**提交的是 `false` 版本** —— 默认开会让任何一次 H 都去写全局
MPC，那是兜底手段，不是常规路径。

- [ ] **Step 5: 提交**

```bash
cd "F:/workspace/ue/StellarEra"
git commit -m "fix(矿区): 体素隐藏失效时用全局遮罩兜底

bMaskVoxels 默认关闭；开启后每帧把 CutawayOrigin 放到球心背离相机 4R、
CutawayNormal 指向相机且半径 8R，使行星体素整体透明。

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEraPlanet/Public/PlanetMineralViewComponent.h \
  Source/StellarEraPlanet/Private/PlanetMineralViewComponent.cpp
```

---
