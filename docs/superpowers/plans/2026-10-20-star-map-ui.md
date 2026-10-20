# 星图 UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在星图模式下为选中天体与鼠标悬停天体显示完整属性面板，并封装为 `UStarMapUISystem`。

**Architecture:** 新建纯 C++ UObject `UStarMapUISystem`，由 `AStellarEraHUD` 持有；每帧在 `DrawHUD` 内现拉选中/悬停天体、现算布局、用 Canvas 绘制。数据/格式化/布局全部是纯函数，可无头测试；`AStellarEraPlayerController` 的拾取投影抽成 `MakeScreenCandidate` 并公开两个入口。

**Tech Stack:** Unreal Engine 5.8.3，C++，AHUD + Canvas，引擎默认字体；不引入 UMG/Slate。

**Spec:** `docs/superpowers/specs/2026-10-20-star-map-ui-design.md`

## Global Constraints

- 所有新代码进 `Source/StellarEra/`；不修改 `Plugins/VoxelFree`、`Plugins/VibeUE`。
- `StellarEra.Build.cs` 不动，不新增 UMG/Slate 依赖。
- `StellarEraVehicle` 不能依赖 `StellarEra`（本计划不碰 Vehicle）。
- 含中文的 `.h` / `.cpp` 必须存为 **UTF-8 with BOM**（首三字节 `ef bb bf`）。
- 不要统一换行符；仓库本来就是 CRLF/LF 混合。
- 构建前必须关闭编辑器。构建一律用捆绑 dotnet 直调 UBT，禁止 `Build.bat`，cwd 在项目外。
- 自动化测试用 `UnrealEditor-Cmd.exe` 无头跑；**退出码不算数**，日志里 `Result={Fail}` 必须为 0。
- 提交必须带 pathspec：`git commit -m "..." -- <paths>`；绝不裸 `git commit`。
- 本计划在 worktree `F:/workspace/ue/StellarEra/.claude/worktrees/star-map-ui`（分支 `star-map-ui`，从 `08cec9d` 拉出）内执行。所有命令从该目录运行，不 `cd` 到主仓库。
- 星图 UI 只在当前 Pawn 是 `AStarMapCameraPawn` 时绘制。

## 构建与测试脚本（一次性准备）

worktree 内 `cd` 到项目外会被会话守卫拒绝，因此把命令写进 `%TEMP%` 脚本再执行。

**Create: `C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh`**

```sh
#!/bin/sh
cd "C:/Users/23080/AppData/Local/Temp" || exit 1
exec "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/.claude/worktrees/star-map-ui/StellarEra.uproject" \
  -WaitMutex -FromMsBuild
```

**Create: `C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh`**

```sh
#!/bin/sh
cd "C:/Users/23080/AppData/Local/Temp" || exit 1
LOG="C:/Users/23080/AppData/Local/Temp/se_starmap_automation.log"
FILTER="${1:-StellarEra}"
rm -f "$LOG"
exec "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/.claude/worktrees/star-map-ui/StellarEra.uproject" \
  -ExecCmds="Automation RunTests $FILTER" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$LOG"
```

调用方式：`sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"`、
`sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra.StarMap.UI"`。

---

### Task 1: UI 系统数据层（引力 + 格式化）

**Files:**
- Create: `Source/StellarEra/Public/StarMapUISystem.h`
- Create: `Source/StellarEra/Private/StarMapUISystem.cpp`
- Test: `Source/StellarEra/Private/StarMapUISystemTests.cpp`

**Interfaces:**
- Consumes: `UCosmosOrbitComponent`（MassEarth / PositionAu / SolverVelocityKmPerSecond / SimulatorIndex / bSimulated / bExertsGravity）、`UCosmosSimulationSubsystem::GetOrbitalPeriodYears(int32)`、`AVoxelPlanetActor::GetSurfaceRadiusCm()`、`CosmosUnits::*`。
- Produces: `struct FStarMapBodyInfo`；`UStarMapUISystem::ComputeSurfaceGravityMps2(double, double) -> double`；`UStarMapUISystem::FormatBodyInfoLines(const FStarMapBodyInfo&) -> TArray<FString>`（第一行是名字）；`UStarMapUISystem::BuildBodyInfo(AActor*, FStarMapBodyInfo&) -> bool`。

- [ ] **Step 1: 先写失败测试**

创建 `Source/StellarEra/Private/StarMapUISystemTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "CosmosTypes.h"
#include "StarMapUISystem.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapUISurfaceGravityTest,
	"StellarEra.StarMap.UI.SurfaceGravity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapUISurfaceGravityTest::RunTest(const FString& Parameters)
{
	const double EarthGravity = UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 6371.0);
	TestTrue(TEXT("地球表面重力约 9.823 m/s²"),
		FMath::IsNearlyEqual(EarthGravity, 9.823, 0.05));

	const double DoubleMass = UStarMapUISystem::ComputeSurfaceGravityMps2(2.0, 6371.0);
	TestTrue(TEXT("质量翻倍，重力翻倍"),
		FMath::IsNearlyEqual(DoubleMass, EarthGravity * 2.0, 1.0e-9));

	TestEqual(TEXT("质量为 0 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(0.0, 6371.0), 0.0);
	TestEqual(TEXT("半径为 0 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 0.0), 0.0);
	TestEqual(TEXT("质量为 NaN 返回 0"),
		UStarMapUISystem::ComputeSurfaceGravityMps2(
			std::numeric_limits<double>::quiet_NaN(), 6371.0), 0.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapUIFormattingTest,
	"StellarEra.StarMap.UI.Formatting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapUIFormattingTest::RunTest(const FString& Parameters)
{
	FStarMapBodyInfo Info;
	Info.Name = TEXT("地球");
	Info.MassEarth = 1.0;
	Info.MassSolar = CosmosUnits::EarthMassToSolarMass(1.0);
	Info.PositionAu = FVector(0.8732, 0.0, 0.0);
	Info.VelocityKmPerSecond = FVector(29.790, 0.0, 0.0);
	Info.OrbitalPeriodYears = 1.0;
	Info.RadiusKm = 6371.0;
	Info.SurfaceGravityMps2 = UStarMapUISystem::ComputeSurfaceGravityMps2(1.0, 6371.0);
	Info.SimulatorIndex = 0;
	Info.bSimulated = true;
	Info.bExertsGravity = true;

	const TArray<FString> Lines = UStarMapUISystem::FormatBodyInfoLines(Info);
	const FString Joined = FString::Join(Lines, TEXT("\n"));

	TestTrue(TEXT("第一行是名字"), Lines.Num() > 0 && Lines[0] == TEXT("地球"));
	TestTrue(TEXT("含质量 M⊕"), Joined.Contains(TEXT("1 M⊕")));
	TestTrue(TEXT("含质量 M☉"), Joined.Contains(TEXT("3.003e-06 M☉")));
	TestTrue(TEXT("含位置 AU"), Joined.Contains(TEXT("(0.8732, 0.0000, 0.0000) AU")));
	TestTrue(TEXT("含速度"), Joined.Contains(TEXT("29.790 km/s")));
	TestTrue(TEXT("含轨道周期"), Joined.Contains(TEXT("1.000 年")));
	TestTrue(TEXT("含半径"), Joined.Contains(TEXT("6371.0 km")));
	// 不写死 9.822/9.823：用同一个值生成期望串，只锁「格式与数值来源一致」。
	TestTrue(TEXT("含表面引力"), Joined.Contains(FString::Printf(
		TEXT("%.3f m/s²"), Info.SurfaceGravityMps2)));
	TestTrue(TEXT("含 g 倍数"), Joined.Contains(FString::Printf(
		TEXT("%.3f g"), Info.SurfaceGravityMps2 / 9.80665)));
	TestTrue(TEXT("含 Simulator 下标"), Joined.Contains(TEXT("#0")));
	TestTrue(TEXT("含模拟与引力源"), Joined.Contains(TEXT("模拟")) &&
		Joined.Contains(TEXT("引力源")));

	FStarMapBodyInfo Unknown = Info;
	Unknown.OrbitalPeriodYears = 0.0;
	Unknown.RadiusKm = 0.0;
	Unknown.SurfaceGravityMps2 = 0.0;
	Unknown.SimulatorIndex = INDEX_NONE;
	Unknown.bSimulated = false;
	Unknown.bExertsGravity = false;

	const FString UnknownJoined =
		FString::Join(UStarMapUISystem::FormatBodyInfoLines(Unknown), TEXT("\n"));
	TestTrue(TEXT("周期无效显示未知"), UnknownJoined.Contains(TEXT("未知")));
	TestTrue(TEXT("半径无效显示破折号"), UnknownJoined.Contains(TEXT("半径        —")));
	TestTrue(TEXT("引力无效显示破折号"), UnknownJoined.Contains(TEXT("表面引力    —")));
	TestTrue(TEXT("未注册显示提示"), UnknownJoined.Contains(TEXT("#-1（未注册）")));
	TestTrue(TEXT("模拟关闭显示否"), UnknownJoined.Contains(TEXT("模拟        否")));
	TestTrue(TEXT("引力源关闭显示否"), UnknownJoined.Contains(TEXT("引力源    否")));

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认测试文件先失败**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
```

预期：编译失败，`StarMapUISystem.h` 不存在（fatal error: cannot open include file）。

- [ ] **Step 3: 创建头文件**

`Source/StellarEra/Public/StarMapUISystem.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图模式的信息 UI 控制系统：选中天体面板与悬停天体面板。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StarMapUISystem.generated.h"

class AActor;
class AStellarEraHUD;

/** 一颗天体的显示数据。纯数据，不含 UObject 引用。 */
struct FStarMapBodyInfo
{
	FString Name;
	double MassEarth = 0.0;
	double MassSolar = 0.0;
	FVector PositionAu = FVector::ZeroVector;
	FVector VelocityKmPerSecond = FVector::ZeroVector;
	double OrbitalPeriodYears = 0.0;
	double RadiusKm = 0.0;
	double SurfaceGravityMps2 = 0.0;
	int32 SimulatorIndex = INDEX_NONE;
	bool bSimulated = false;
	bool bExertsGravity = false;
};

/** 面板矩形（Canvas 像素）。 */
struct FStarMapPanelRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
};

/**
 * 星图模式的信息 UI 控制系统。
 *
 * 不 Tick、不保存跨帧状态：每帧由 AStellarEraHUD::DrawHUD 调用 Draw，
 * 现拉选中/悬停天体、现算布局、现画。纯函数单独暴露，便于无头测试。
 */
UCLASS()
class UStarMapUISystem : public UObject
{
	GENERATED_BODY()

public:
	/** 画一帧星图 UI。只在星图模式下由 HUD 调用。 */
	void Draw(AStellarEraHUD& Hud);

	/** 从已注册天体采集显示数据。没有 UCosmosOrbitComponent 时返回 false。 */
	static bool BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo);

	/** 格式化成面板行；**第一行是名字**，其余是属性。选中与悬停共用。 */
	static TArray<FString> FormatBodyInfoLines(const FStarMapBodyInfo& Info);

	/** 天体表面重力，单位 m/s²。质量或半径无效时返回 0。 */
	static double ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm);

	/**
	 * 把面板放在锚点右侧；右侧放不下翻到左侧，最后整体夹进视口。
	 * 鼠标锚点传 AnchorRadiusPx = 0，天体锚点传屏幕轮廓半径。
	 */
	static FStarMapPanelRect PlacePanelBesideAnchor(
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& PanelSizePx,
		const FVector2D& ViewportSizePx,
		float GapPx,
		float MarginPx);

private:
	void DrawBodyPanel(
		AStellarEraHUD& Hud,
		const FStarMapBodyInfo& Info,
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& ViewportSizePx);
};
```

- [ ] **Step 4: 创建实现（本任务只实现数据层三个函数）**

`Source/StellarEra/Private/StarMapUISystem.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapUISystem.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "StellarEraHUD.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UStarMapUISystem::BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo)
{
	OutInfo = FStarMapBodyInfo();
	if (!IsValid(Body))
	{
		return false;
	}

	const UCosmosOrbitComponent* Orbit =
		Body->FindComponentByClass<UCosmosOrbitComponent>();
	if (!Orbit)
	{
		return false;
	}

	OutInfo.Name = Body->GetActorNameOrLabel();
	OutInfo.MassEarth = Orbit->MassEarth;
	OutInfo.MassSolar = CosmosUnits::EarthMassToSolarMass(Orbit->MassEarth);
	OutInfo.PositionAu = Orbit->PositionAu;
	OutInfo.VelocityKmPerSecond = Orbit->SolverVelocityKmPerSecond;
	OutInfo.SimulatorIndex = Orbit->SimulatorIndex;
	OutInfo.bSimulated = Orbit->bSimulated;
	OutInfo.bExertsGravity = Orbit->bExertsGravity;

	double RadiusCm = 0.0;
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body))
	{
		RadiusCm = Planet->GetSurfaceRadiusCm();
	}
	else
	{
		RadiusCm = double(Body->GetSimpleCollisionRadius());
	}

	OutInfo.RadiusKm = RadiusCm / CosmosUnits::CentimetersPerKilometer;
	OutInfo.SurfaceGravityMps2 =
		ComputeSurfaceGravityMps2(OutInfo.MassEarth, OutInfo.RadiusKm);

	if (OutInfo.SimulatorIndex != INDEX_NONE)
	{
		if (const UWorld* World = Body->GetWorld())
		{
			if (const UCosmosSimulationSubsystem* Subsystem =
				World->GetSubsystem<UCosmosSimulationSubsystem>())
			{
				OutInfo.OrbitalPeriodYears =
					Subsystem->GetOrbitalPeriodYears(OutInfo.SimulatorIndex);
			}
		}
	}

	return true;
}

double UStarMapUISystem::ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm)
{
	if (!FMath::IsFinite(MassEarth) || !FMath::IsFinite(RadiusKm) ||
		MassEarth <= 0.0 || RadiusKm <= 0.0)
	{
		return 0.0;
	}

	const double MassKg =
		MassEarth * CosmosUnits::EarthMass * CosmosUnits::SolarMassToKilogram;
	const double RadiusCm = RadiusKm * CosmosUnits::CentimetersPerKilometer;

	// G_SI 的单位是 cm³/(kg·s²)；除以 100 换成 m/s²。
	return CosmosUnits::GravitationalConstant_SI * MassKg /
		(RadiusCm * RadiusCm) / 100.0;
}

TArray<FString> UStarMapUISystem::FormatBodyInfoLines(const FStarMapBodyInfo& Info)
{
	TArray<FString> Lines;
	Lines.Reserve(9);

	// 第一行是标题（名字），DrawBodyPanel 把它画成金色标题。
	Lines.Add(Info.Name);

	if (FMath::IsFinite(Info.MassEarth) && Info.MassEarth > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("质量        %.4g M⊕（%.4g M☉）"),
			Info.MassEarth, Info.MassSolar));
	}
	else
	{
		Lines.Add(TEXT("质量        —"));
	}

	Lines.Add(FString::Printf(TEXT("位置        (%.4f, %.4f, %.4f) AU"),
		Info.PositionAu.X, Info.PositionAu.Y, Info.PositionAu.Z));

	Lines.Add(FString::Printf(TEXT("速度        %.3f km/s"),
		Info.VelocityKmPerSecond.Size()));

	if (FMath::IsFinite(Info.OrbitalPeriodYears) && Info.OrbitalPeriodYears > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("轨道周期    %.3f 年"),
			Info.OrbitalPeriodYears));
	}
	else
	{
		Lines.Add(TEXT("轨道周期    未知"));
	}

	if (FMath::IsFinite(Info.RadiusKm) && Info.RadiusKm > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("半径        %.1f km"), Info.RadiusKm));
	}
	else
	{
		Lines.Add(TEXT("半径        —"));
	}

	if (FMath::IsFinite(Info.SurfaceGravityMps2) && Info.SurfaceGravityMps2 > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("表面引力    %.3f m/s²（%.3f g）"),
			Info.SurfaceGravityMps2, Info.SurfaceGravityMps2 / 9.80665));
	}
	else
	{
		Lines.Add(TEXT("表面引力    —"));
	}

	Lines.Add(Info.SimulatorIndex == INDEX_NONE
		? TEXT("Simulator   #-1（未注册）")
		: FString::Printf(TEXT("Simulator   #%d"), Info.SimulatorIndex));

	Lines.Add(FString::Printf(TEXT("模拟        %s    引力源    %s"),
		Info.bSimulated ? TEXT("是") : TEXT("否"),
		Info.bExertsGravity ? TEXT("是") : TEXT("否")));

	return Lines;
}
```

- [ ] **Step 5: 给三个新文件加 UTF-8 BOM**

```bash
for f in Source/StellarEra/Public/StarMapUISystem.h \
         Source/StellarEra/Private/StarMapUISystem.cpp \
         Source/StellarEra/Private/StarMapUISystemTests.cpp; do
  head -c 3 "$f" | od -An -tx1 | grep -q "ef bb bf" || {
    printf '\xef\xbb\xbf' > "$f.bom" && cat "$f" >> "$f.bom" && mv "$f.bom" "$f"
  }
done
head -c 3 Source/StellarEra/Public/StarMapUISystem.h | od -An -tx1
```

预期输出：` ef bb bf`。

- [ ] **Step 6: 构建**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
```

预期：`Result: Succeeded`。

- [ ] **Step 7: 跑测试**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra.StarMap.UI"
```

预期：两个测试 `Result={Success}`，日志中 `Result={Fail}` 为 0。

- [ ] **Step 8: 提交**

```bash
git add Source/StellarEra/Public/StarMapUISystem.h \
        Source/StellarEra/Private/StarMapUISystem.cpp \
        Source/StellarEra/Private/StarMapUISystemTests.cpp
git commit -m "feat(星图): UI 系统数据层与格式化

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StarMapUISystem.h \
  Source/StellarEra/Private/StarMapUISystem.cpp \
  Source/StellarEra/Private/StarMapUISystemTests.cpp
```

---

### Task 2: 面板布局纯函数

**Files:**
- Modify: `Source/StellarEra/Private/StarMapUISystem.cpp`
- Test: `Source/StellarEra/Private/StarMapUISystemTests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `FStarMapPanelRect`、`UStarMapUISystem`。
- Produces: `UStarMapUISystem::PlacePanelBesideAnchor(const FVector2D&, float, const FVector2D&, const FVector2D&, float, float) -> FStarMapPanelRect`。

- [ ] **Step 1: 追加失败测试**

在 `StarMapUISystemTests.cpp` 的 `#endif` 之前追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraStarMapUIPlacementTest,
	"StellarEra.StarMap.UI.Placement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraStarMapUIPlacementTest::RunTest(const FString& Parameters)
{
	const FVector2D Viewport(1000.f, 800.f);
	const FVector2D Panel(200.f, 100.f);

	{
		const FStarMapPanelRect Rect = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(500.f, 400.f), 0.f, Panel, Viewport, 12.f, 8.f);
		TestEqual(TEXT("右侧放得下：X 贴锚点"), Rect.X, 512.f);
		TestEqual(TEXT("右侧放得下：Y 居中"), Rect.Y, 350.f);
	}

	{
		const FStarMapPanelRect Rect = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(900.f, 400.f), 0.f, Panel, Viewport, 12.f, 8.f);
		TestEqual(TEXT("右侧溢出：翻到左侧"), Rect.X, 688.f);
	}

	{
		const FStarMapPanelRect Rect = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(200.f, 400.f), 50.f, Panel, Viewport, 12.f, 8.f);
		TestEqual(TEXT("天体轮廓半径参与锚定"), Rect.X, 262.f);
	}

	{
		const FStarMapPanelRect Top = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(500.f, 20.f), 0.f, Panel, Viewport, 12.f, 8.f);
		TestEqual(TEXT("顶部夹到边距"), Top.Y, 8.f);

		const FStarMapPanelRect Bottom = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(500.f, 790.f), 0.f, Panel, Viewport, 12.f, 8.f);
		TestEqual(TEXT("底部夹到边距"), Bottom.Y, 692.f);
	}

	{
		const FStarMapPanelRect Rect = UStarMapUISystem::PlacePanelBesideAnchor(
			FVector2D(50.f, 25.f), 0.f, Panel, FVector2D(100.f, 50.f), 12.f, 8.f);
		TestEqual(TEXT("小视口：X 钉在左边距"), Rect.X, 8.f);
		TestEqual(TEXT("小视口：Y 钉在顶边距"), Rect.Y, 8.f);
	}

	return true;
}
```

- [ ] **Step 2: 构建，确认链接失败**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
```

预期：链接错误 `unresolved external symbol ...PlacePanelBesideAnchor`。

- [ ] **Step 3: 在 `StarMapUISystem.cpp` 末尾实现**

```cpp
FStarMapPanelRect UStarMapUISystem::PlacePanelBesideAnchor(
	const FVector2D& AnchorPx,
	float AnchorRadiusPx,
	const FVector2D& PanelSizePx,
	const FVector2D& ViewportSizePx,
	float GapPx,
	float MarginPx)
{
	FStarMapPanelRect Rect;
	Rect.W = float(PanelSizePx.X);
	Rect.H = float(PanelSizePx.Y);

	const float RightX = float(AnchorPx.X) + AnchorRadiusPx + GapPx;
	const float LeftX = float(AnchorPx.X) - AnchorRadiusPx - GapPx - Rect.W;
	const float MaxX = FMath::Max(MarginPx, float(ViewportSizePx.X) - MarginPx - Rect.W);
	const float MaxY = FMath::Max(MarginPx, float(ViewportSizePx.Y) - MarginPx - Rect.H);

	Rect.X = RightX;
	if (RightX + Rect.W > float(ViewportSizePx.X) - MarginPx && LeftX >= MarginPx)
	{
		Rect.X = LeftX;
	}

	Rect.X = FMath::Clamp(Rect.X, MarginPx, MaxX);
	Rect.Y = FMath::Clamp(float(AnchorPx.Y) - Rect.H * 0.5f, MarginPx, MaxY);
	return Rect;
}
```

- [ ] **Step 4: 构建并跑测试**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra.StarMap.UI"
```

预期：构建 `Result: Succeeded`；三个测试全 `Result={Success}`，`Result={Fail}` 为 0。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEra/Private/StarMapUISystem.cpp \
        Source/StellarEra/Private/StarMapUISystemTests.cpp
git commit -m "feat(星图): 面板右侧锚定与视口夹紧

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Private/StarMapUISystem.cpp \
  Source/StellarEra/Private/StarMapUISystemTests.cpp
```

---

### Task 3: 控制器投影复用

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h`
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp`

**Interfaces:**
- Consumes: `FStarMapScreenCandidate`、`AStarMapCameraPawn::SilhouetteRadiusScale`、`AStarMapCameraPawn::PickBodyAtScreenPosition`、`UCosmosSimulationSubsystem::GetRegisteredBodies`。
- Produces: `bool ProjectBodyToScreen(AActor*, FVector2D&, float&) const`；`AActor* PickBodyUnderScreenPosition(const FVector2D&) const`；私有 `bool MakeScreenCandidate(AActor*, FStarMapScreenCandidate&) const`。

- [ ] **Step 1: 修改头文件**

在 `StellarEraPlayerController.h` 顶部前置声明区加：

```cpp
struct FStarMapScreenCandidate;
```

在 `public:` 的 `EnterNearestShip()` 之后加：

```cpp
	/** 把一颗天体投到屏幕；成功时 OutCenter 为中心、OutRadiusPx 为轮廓半径（像素）。 */
	bool ProjectBodyToScreen(AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const;

	/** 任意屏幕点下的已注册天体。悬停与点击共用。 */
	AActor* PickBodyUnderScreenPosition(const FVector2D& ScreenPosition) const;
```

删除原来的私有声明：

```cpp
	/** 把已注册天体投到屏幕，交给纯函数挑出光标下的那颗。没有就是 nullptr。 */
	AActor* PickBodyUnderCursor() const;
```

并在 `private` 区（原 `PickBodyUnderCursor` 的位置）加：

```cpp
	/** 投影单颗天体。拾取与选中面板锚点共用这一条路径。 */
	bool MakeScreenCandidate(AActor* Body, FStarMapScreenCandidate& Out) const;
```

- [ ] **Step 2: 替换实现**

`StellarEraPlayerController.cpp` 中，把 `PickBodyUnderCursor()` 整个函数替换为：

```cpp
bool AStellarEraPlayerController::MakeScreenCandidate(
	AActor* Body, FStarMapScreenCandidate& Out) const
{
	Out = FStarMapScreenCandidate();
	if (!IsValid(Body))
	{
		return false;
	}

	const FVector Center = Body->GetActorLocation();
	if (!ProjectWorldLocationToScreen(Center, Out.ScreenPosition))
	{
		return false;
	}

	// 视线右向：算「与视线垂直」的半径必须用它，不能用固定世界轴。
	FVector ViewLocation;
	FRotator ViewRotation;
	GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

	// 盘面半径 = |投影(中心) − 投影(中心 + 右向 × 世界半径)|。
	// 边缘点投不出来（越过后平面等）就留 0，退回固定像素容差。
	const double WorldRadiusCm = ResolveBodyRadiusCm(Body);
	if (WorldRadiusCm > 0.0)
	{
		FVector2D EdgePosition;
		if (ProjectWorldLocationToScreen(Center + ViewRight * WorldRadiusCm, EdgePosition))
		{
			// 直接投影「中心 + 右向 × R」量到的是 f·R/d，比切线决定的真实轮廓半径
			// 小一个 cos α（见 SilhouetteRadiusScale）。系数为 0 表示相机在球内
			// 或贴着球面，此时留 0、退回固定像素容差。
			const double CameraDistanceCm = FVector::Dist(ViewLocation, Center);
			Out.ScreenRadiusPx = float(
				FVector2D::Distance(Out.ScreenPosition, EdgePosition)
				* AStarMapCameraPawn::SilhouetteRadiusScale(CameraDistanceCm, WorldRadiusCm));
		}
	}

	Out.Actor = Body;
	return true;
}

bool AStellarEraPlayerController::ProjectBodyToScreen(
	AActor* Body, FVector2D& OutCenter, float& OutRadiusPx) const
{
	FStarMapScreenCandidate Candidate;
	if (!MakeScreenCandidate(Body, Candidate))
	{
		return false;
	}

	OutCenter = Candidate.ScreenPosition;
	OutRadiusPx = Candidate.ScreenRadiusPx;
	return true;
}

AActor* AStellarEraPlayerController::PickBodyUnderScreenPosition(
	const FVector2D& ScreenPosition) const
{
	UCosmosSimulationSubsystem* Subsystem =
		GetWorld() ? GetWorld()->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return nullptr;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 投影留在调用方，拾取函数才能是纯函数、才能无头测。
	TArray<FStarMapScreenCandidate> Candidates;
	Candidates.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		FStarMapScreenCandidate Candidate;
		if (MakeScreenCandidate(Body, Candidate))
		{
			Candidates.Add(Candidate);
		}
	}

	return AStarMapCameraPawn::PickBodyAtScreenPosition(
		Candidates, ScreenPosition, SelectionPixelRadius);
}
```

- [ ] **Step 3: 更新点击选择调用点**

`OnStarMapSelectCompleted` 中：

```cpp
	AActor* Picked = PickBodyUnderCursor();
```

改为：

```cpp
	AActor* Picked = PickBodyUnderScreenPosition(FVector2D(MouseX, MouseY));
```

- [ ] **Step 4: 构建**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
```

预期：`Result: Succeeded`。

- [ ] **Step 5: 跑星图拾取回归**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra.StarMap"
```

预期：全部 `Result={Success}`，`Result={Fail}` 为 0（尤其是 `PickingScreenRadius` 与 `SilhouetteRadiusScale`）。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp
git commit -m "refactor(星图): 投影候选抽成 MakeScreenCandidate 并公开拾取入口

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StellarEraPlayerController.h \
  Source/StellarEra/Private/StellarEraPlayerController.cpp
```

---

### Task 4: HUD 集成与 Canvas 绘制

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraHUD.h`
- Modify: `Source/StellarEra/Private/StellarEraHUD.cpp`
- Modify: `Source/StellarEra/Private/StarMapUISystem.cpp`

**Interfaces:**
- Consumes: Task 1 的 `BuildBodyInfo` / `FormatBodyInfoLines`、Task 2 的 `PlacePanelBesideAnchor`、Task 3 的 `ProjectBodyToScreen` / `PickBodyUnderScreenPosition`。
- Produces: `AStellarEraHUD::GetViewportSize() const -> FVector2D`；`UStarMapUISystem::Draw(AStellarEraHUD&)`；私有 `DrawBodyPanel(...)`。

- [ ] **Step 1: 修改 HUD 头文件**

在 `StellarEraHUD.h` 的前置声明区加：

```cpp
class UStarMapUISystem;
```

在 `public:` 的 `ShowNotice` 之后加：

```cpp
	/** 当前画布尺寸（像素）；没有 Canvas 时返回零向量。 */
	FVector2D GetViewportSize() const;
```

在 `private:` 区末尾加：

```cpp
	/** 星图模式的信息面板。第一次进星图时惰性创建。 */
	UPROPERTY(Transient)
	TObjectPtr<UStarMapUISystem> StarMapUI;
```

- [ ] **Step 2: 修改 HUD 实现**

在 `StellarEraHUD.cpp` 的 include 区加：

```cpp
#include "StarMapCameraPawn.h"
#include "StarMapUISystem.h"
```

`DrawHUD` 中，在 `DrawNotice();` 之前加：

```cpp
	// 星图模式的信息面板：只在当前 Pawn 是星图相机时画。
	if (const APlayerController* PlayerController = GetOwningPlayerController())
	{
		if (Cast<AStarMapCameraPawn>(PlayerController->GetPawn()))
		{
			if (!StarMapUI)
			{
				StarMapUI = NewObject<UStarMapUISystem>(this);
			}
			StarMapUI->Draw(*this);
		}
	}
```

在 `ShowNotice` 之后加：

```cpp
FVector2D AStellarEraHUD::GetViewportSize() const
{
	return Canvas ? FVector2D(Canvas->SizeX, Canvas->SizeY) : FVector2D::ZeroVector;
}
```

- [ ] **Step 3: 在 `StarMapUISystem.cpp` 实现 Draw / DrawBodyPanel**

先补 include：

```cpp
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "StellarEraPlayerController.h"
```

投影与拾取是 `AStellarEraPlayerController` 的方法，`APlayerController` 上没有，
所以取到 PC 之后要 `Cast`。

在文件末尾加：

```cpp
void UStarMapUISystem::Draw(AStellarEraHUD& Hud)
{
	const FVector2D ViewportSize = Hud.GetViewportSize();
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return;
	}

	AStellarEraPlayerController* PlayerController =
		Cast<AStellarEraPlayerController>(Hud.GetOwningPlayerController());
	if (!PlayerController)
	{
		return;
	}

	const UWorld* World = Hud.GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return;
	}

	AActor* Selected = Subsystem->GetSelectedBody();

	double MouseX = 0.0;
	double MouseY = 0.0;
	const bool bHasMouse = PlayerController->GetMousePosition(MouseX, MouseY);
	const FVector2D MousePosition(MouseX, MouseY);
	const bool bMouseInside = bHasMouse &&
		MousePosition.X >= 0.0 && MousePosition.Y >= 0.0 &&
		MousePosition.X <= ViewportSize.X && MousePosition.Y <= ViewportSize.Y;

	AActor* Hovered = bMouseInside
		? PlayerController->PickBodyUnderScreenPosition(MousePosition)
		: nullptr;

	// 选中面板：贴天体投影中心右侧。中心不在视口内就不画。
	if (IsValid(Selected))
	{
		FVector2D Center = FVector2D::ZeroVector;
		float RadiusPx = 0.f;
		const bool bProjected =
			PlayerController->ProjectBodyToScreen(Selected, Center, RadiusPx);
		const bool bCenterInside =
			Center.X >= 0.0 && Center.Y >= 0.0 &&
			Center.X <= ViewportSize.X && Center.Y <= ViewportSize.Y;

		if (bProjected && bCenterInside)
		{
			FStarMapBodyInfo Info;
			if (BuildBodyInfo(Selected, Info))
			{
				DrawBodyPanel(Hud, Info, Center, RadiusPx, ViewportSize);
			}
		}
	}

	// 悬停面板：贴鼠标右侧；悬停的正是选中天体时不重复弹。
	if (bMouseInside && IsValid(Hovered) && Hovered != Selected)
	{
		FStarMapBodyInfo Info;
		if (BuildBodyInfo(Hovered, Info))
		{
			DrawBodyPanel(Hud, Info, MousePosition, 0.f, ViewportSize);
		}
	}
}

void UStarMapUISystem::DrawBodyPanel(
	AStellarEraHUD& Hud,
	const FStarMapBodyInfo& Info,
	const FVector2D& AnchorPx,
	float AnchorRadiusPx,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatBodyInfoLines(Info);
	if (Lines.Num() == 0)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float Padding = 10.f;
	constexpr float LineSpacing = 4.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float Gap = 12.f;
	constexpr float Margin = 8.f;

	const FString& Title = Lines[0];

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	Hud.GetTextSize(Title, TitleWidth, TitleHeight, Font, TextScale);

	float ContentWidth = TitleWidth;
	float LineHeight = 0.f;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		float LineWidth = 0.f;
		float ThisHeight = 0.f;
		Hud.GetTextSize(Lines[Index], LineWidth, ThisHeight, Font, TextScale);
		ContentWidth = FMath::Max(ContentWidth, LineWidth);
		LineHeight = FMath::Max(LineHeight, ThisHeight);
	}

	const int32 BodyLineCount = Lines.Num() - 1;
	const float PanelWidth = ContentWidth + Padding * 2.f;
	const float PanelHeight = Padding * 2.f + TitleHeight
		+ SeparatorSpacing * 2.f + 1.f
		+ BodyLineCount * (LineHeight + LineSpacing);

	const FStarMapPanelRect Rect = PlacePanelBesideAnchor(
		AnchorPx, AnchorRadiusPx,
		FVector2D(PanelWidth, PanelHeight), ViewportSizePx, Gap, Margin);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);

	Hud.DrawRect(Background, Rect.X, Rect.Y, Rect.W, Rect.H);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y + Rect.H - 1.f, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, 1.f, Rect.H);
	Hud.DrawRect(Gold, Rect.X + Rect.W - 1.f, Rect.Y, 1.f, Rect.H);

	float CursorY = Rect.Y + Padding;
	Hud.DrawText(Title, Gold, Rect.X + Padding, CursorY, Font, TextScale);
	CursorY += TitleHeight + SeparatorSpacing;

	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		Hud.DrawText(Lines[Index], White, Rect.X + Padding, CursorY, Font, TextScale);
		CursorY += LineHeight + LineSpacing;
	}
}
```

- [ ] **Step 4: 检查 BOM**

```bash
for f in Source/StellarEra/Public/StarMapUISystem.h \
         Source/StellarEra/Private/StarMapUISystem.cpp \
         Source/StellarEra/Public/StellarEraHUD.h \
         Source/StellarEra/Private/StellarEraHUD.cpp; do
  printf '%s: ' "$f"; head -c 3 "$f" | od -An -tx1
done
```

预期每个文件都是 ` ef bb bf`。

- [ ] **Step 5: 构建**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
```

预期：`Result: Succeeded`。

- [ ] **Step 6: 跑全量测试**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra"
```

预期：日志中 `Result={Fail}` 为 0（含 `StellarEra.StarMap.UI.*` 三组与已有 `StellarEra.StarMap.*`）。

- [ ] **Step 7: 提交**

```bash
git add Source/StellarEra/Public/StellarEraHUD.h \
        Source/StellarEra/Private/StellarEraHUD.cpp \
        Source/StellarEra/Private/StarMapUISystem.cpp
git commit -m "feat(星图): HUD 每帧绘制选中与悬停属性面板

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
  Source/StellarEra/Public/StellarEraHUD.h \
  Source/StellarEra/Private/StellarEraHUD.cpp \
  Source/StellarEra/Private/StarMapUISystem.cpp
```

---

### Task 5: 全量验证与人工验收交接

**Files:** 无新改动（只验证）。

- [ ] **Step 1: 确认工作区只含本计划提交**

```bash
git status --porcelain
git log --oneline -5
```

预期：`git status` 为空；最近 4 个提交是本计划的 4 次提交。

- [ ] **Step 2: 全量构建与测试（最终证据）**

```bash
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_build.sh"
sh "C:/Users/23080/AppData/Local/Temp/se_starmap_tests.sh" "StellarEra"
```

然后统计失败数与三条新测试：

```bash
grep -c "Result={Fail}" "C:/Users/23080/AppData/Local/Temp/se_starmap_automation.log" || true
grep -E "StellarEra\.StarMap\.UI\.(SurfaceGravity|Formatting|Placement).*Result=\{(Success|Fail)\}" \
  "C:/Users/23080/AppData/Local/Temp/se_starmap_automation.log" || true
```

预期：`Fail` 计数为 0；上面 grep 恰好打出三行，且每行都是 `Result={Success}`。
（不写死总测试数：基线数目随分支变化，逐条点名比总数可靠。）

- [ ] **Step 3: 确认 BOM 与 pathspec 提交**

```bash
git show --stat --oneline HEAD~3..HEAD
for f in Source/StellarEra/Public/StarMapUISystem.h \
         Source/StellarEra/Private/StarMapUISystem.cpp \
         Source/StellarEra/Private/StarMapUISystemTests.cpp; do
  printf '%s: ' "$f"; head -c 3 "$f" | od -An -tx1
done
```

预期：4 个提交都只含计划内文件；三个新文件首三字节均为 `ef bb bf`。

- [ ] **Step 4: 人工 PIE 验收（交给用户）**

在关闭编辑器前**不要**声称已完成视觉验收。请在 `UniverseMap` 中验证：

1. 进 PIE，开飞船后按 M 进入星图。
2. 鼠标悬停任意行星：鼠标右侧出现属性窗口；移开后天体消失即窗口消失。
3. 点击行星选中：天体中心右侧出现完整属性面板，标题是天体名字；拖拽转视角时面板跟随。
4. 悬停选中的行星：只有天体右侧面板，不出现鼠标右侧的重复窗口。
5. 选中的行星转到相机背后：面板消失，不钉在屏幕边缘。
6. 按 M 退出星图：所有面板消失，飞船状态条恢复。

记录任何一条不符合的实际现象；未通过前不要合并。
