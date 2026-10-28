# 星图连续挖掘 + 笔刷范围可调 — 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 星图编辑模式下 `Shift`+左键按住连续挖、`Ctrl`+左键填，笔刷范围与连续挖间隔在 `BP_StarMapCameraPawn` 细节面板可调。

**Architecture:** 连续挖的状态与节流放进通用的 `UPlanetEditComponent`（编辑模式才 Tick），节流判定抽成可无头测试的纯函数；控制器只负责按键分派与松开清理；星图相机改为优先生成蓝图子类，给细节面板一个落点。

**Tech Stack:** Unreal Engine 5.8.3（C++）、Automation Test（`StellarEra.*`）、Enhanced Input（运行期构造）、无头 Python 建资产。

**Spec:** `docs/superpowers/specs/2026-10-28-starmap-continuous-dig-design.md`

## Global Constraints

- 中文 `.h` / `.cpp` 必须存 **UTF-8 with BOM**，否则 MSVC 按 GBK 读。
- 项目**警告即错误**：`double → float` 必须显式转，别依赖隐式窄化。
- 构建前**必须关闭编辑器**（Live Coding 锁 DLL）。
- 构建命令固定为捆绑 dotnet 直调 UBT，cwd 在项目外：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- 自动化测试命令（编辑器开着也能跑）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_starmap_dig.log"
```

- 提交必须带 pathspec（索引里有常驻暂存文件）：`git commit -m "..." -- <paths>`。
- 不改 `Plugins/VoxelFree/`、`Plugins/VibeUE/`，不改任何 `Build.cs`。
- 新测试名一律以 `StellarEra.` 开头。

---

### Task 1: 连续挖节流纯函数 `ShouldDigNow`

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h`（`namespace PlanetEditBrush` 内，`ClampSize` 两个重载之后）
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp`（文件末尾）
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`（`#endif // WITH_DEV_AUTOMATION_TESTS` 之前）

**Interfaces:**
- Produces:
  ```cpp
  namespace PlanetEditBrush
  {
      STELLARERAPLANETEDIT_API bool ShouldDigNow(
          float& AccumulatorSeconds, float DeltaSeconds, float IntervalSeconds);
  }
  ```
  `IntervalSeconds <= 0` 时按 `0.01` 处理；触发时把 `AccumulatorSeconds` 清零；一次调用最多返回一次 `true`。

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEra/Private/PlanetEditTests.cpp` 的 `#endif // WITH_DEV_AUTOMATION_TESTS` **之前**追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraEditBrushShouldDigNowTest,
	"StellarEra.Planet.EditBrush.ShouldDigNow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraEditBrushShouldDigNowTest::RunTest(const FString& Parameters)
{
	float Accumulator = 0.f;

	// 未到间隔：不挖，累加器保留。
	TestFalse(TEXT("0.05 s 未到 0.12 s 间隔"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.05f, 0.12f));
	TestTrue(TEXT("未到间隔时累加器保留"),
		FMath::IsNearlyEqual(Accumulator, 0.05f, 1.e-4f));

	// 跨过间隔：挖一次并清零。
	TestTrue(TEXT("再走 0.08 s 跨过 0.12 s"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.08f, 0.12f));
	TestTrue(TEXT("触发后累加器清零"),
		FMath::IsNearlyEqual(Accumulator, 0.f, 1.e-4f));

	// 一帧巨大 DeltaSeconds 只触发一次，不做 while 补偿。
	TestTrue(TEXT("10 s 的单帧只触发一次"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 10.f, 0.12f));
	TestTrue(TEXT("大帧之后累加器仍是 0"),
		FMath::IsNearlyEqual(Accumulator, 0.f, 1.e-4f));
	TestFalse(TEXT("同一累加器不会连续返回 true"),
		PlanetEditBrush::ShouldDigNow(Accumulator, 0.f, 0.12f));

	// Interval <= 0 按 0.01 处理：不会每帧都挖。
	float ZeroInterval = 0.f;
	TestFalse(TEXT("间隔 0 时 0.001 s 不触发"),
		PlanetEditBrush::ShouldDigNow(ZeroInterval, 0.001f, 0.f));
	TestTrue(TEXT("间隔 0 时累计到 0.021 s 触发"),
		PlanetEditBrush::ShouldDigNow(ZeroInterval, 0.02f, 0.f));

	return true;
}
```

- [ ] **Step 2: 确认测试先失败**

先构建（编辑器关掉）：

```bash
cd /d/ && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期：**编译失败**，报 `ShouldDigNow` 不是 `PlanetEditBrush` 的成员。这一步失败是正确的红灯。

- [ ] **Step 3: 写最小实现**

`Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h`，在 `ClampSize` 两个重载的声明之后加：

```cpp
	/**
	 * 连续挖的节流判定：把 DeltaSeconds 累加进 AccumulatorSeconds，
	 * 跨过 IntervalSeconds 时返回 true 并把累加器清零。
	 *
	 * 一次调用最多返回一次 true（不做 while 补偿）：卡顿后不会一帧补挖多下。
	 * IntervalSeconds <= 0 时按 0.01 处理，避免「每帧都 true」。
	 */
	STELLARERAPLANETEDIT_API bool ShouldDigNow(
		float& AccumulatorSeconds, float DeltaSeconds, float IntervalSeconds);
```

`Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp` 末尾追加：

```cpp
bool PlanetEditBrush::ShouldDigNow(
	float& AccumulatorSeconds, const float DeltaSeconds, const float IntervalSeconds)
{
	const float Interval = FMath::Max(IntervalSeconds, 0.01f);
	AccumulatorSeconds += FMath::Max(DeltaSeconds, 0.f);
	if (AccumulatorSeconds < Interval)
	{
		return false;
	}

	AccumulatorSeconds = 0.f;
	return true;
}
```

- [ ] **Step 4: 重新构建并跑测试**

构建命令同 Step 2。成功后再跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditBrush.ShouldDigNow" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_should_dig.log"
```

预期：`1 test(s) passed`（或等价的一行成功输出）；日志里没有 `Test Failed`。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h \
        Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 连续挖节流纯函数 ShouldDigNow

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h \
        Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 2: 组件连续挖状态 + Tick 驱动

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp`
- Test: `Source/StellarEra/Private/PlanetEditTests.cpp`

**Interfaces:**
- Consumes: `PlanetEditBrush::ShouldDigNow(float&, float, float)`（Task 1）
- Produces:
  ```cpp
  void UPlanetEditComponent::SetContinuousDigging(bool bEnabled);
  bool UPlanetEditComponent::IsContinuousDigging() const;
  UPROPERTY(EditAnywhere) float UPlanetEditComponent::ContinuousDigIntervalSeconds; // 默认 0.12
  ```
  行为：`SetContinuousDigging` 置位/清位并清零累加器；`SetEditMode(false)` 自动关闭；开启期间 Tick 每跨过间隔在光标处 `DigAtScreenPosition` 一次。

- [ ] **Step 1: 写失败的测试**

在 `Source/StellarEra/Private/PlanetEditTests.cpp` 的 `#endif` 之前追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetEditContinuousDigTest,
	"StellarEra.Planet.EditComponent.ContinuousDig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraPlanetEditContinuousDigTest::RunTest(const FString& Parameters)
{
	// 纯状态接口：NewObject 一个不注册、不进世界的临时组件即可。
	TStrongObjectPtr<UPlanetEditComponent> Edit(
		NewObject<UPlanetEditComponent>(GetTransientPackage()));
	if (!TestNotNull(TEXT("临时编辑组件"), Edit.Get()))
	{
		return false;
	}

	TestFalse(TEXT("默认不连续挖"), Edit->IsContinuousDigging());

	Edit->SetContinuousDigging(true);
	TestTrue(TEXT("开启后可读回"), Edit->IsContinuousDigging());

	// 退出编辑模式必须顺手关掉连续挖，否则用户按 G 退出后组件还在后台挖。
	Edit->SetEditMode(true);
	TestTrue(TEXT("进入编辑模式不改变连续挖状态"), Edit->IsContinuousDigging());
	Edit->SetEditMode(false);
	TestFalse(TEXT("退出编辑模式自动关闭连续挖"), Edit->IsContinuousDigging());

	// 重复关闭是幂等的：松开左键会无条件调一次。
	Edit->SetContinuousDigging(false);
	TestFalse(TEXT("重复关闭幂等"), Edit->IsContinuousDigging());

	return true;
}
```

- [ ] **Step 2: 确认测试先失败**

构建（编辑器关掉）后跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditComponent.ContinuousDig" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_continuous_dig.log"
```

预期：**编译失败**——`IsContinuousDigging` / `SetContinuousDigging` 未声明。

- [ ] **Step 3: 头文件**

`Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h`：

公开接口区（挨着 `DigAtScreenPosition` 声明）加：

```cpp
	/**
	 * 按住连续挖：开 / 关。
	 *
	 * 开启后组件在 Tick 里按 ContinuousDigIntervalSeconds 节流，在当前光标处
	 * 重复调用 DigAtScreenPosition。按下那一下由控制器立即挖，这里不补刀。
	 * 退出编辑模式会自动关闭。
	 */
	void SetContinuousDigging(bool bEnabled);
	bool IsContinuousDigging() const { return bContinuousDigging; }
```

`UPROPERTY` 可调参数区（挨着 `bMultiThreadedEdits`）加：

```cpp
	/** 连续挖的间隔（秒）。太小会把体素工具刷爆，太大手感发涩。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.01"))
	float ContinuousDigIntervalSeconds = 0.12f;
```

私有状态区（挨着 `bPreviewPlacing`）加：

```cpp
	/** 连续挖是否开启。Tick 只在编辑模式下跑，退出编辑模式时由 SetEditMode 清掉。 */
	bool bContinuousDigging = false;

	/** 距上次连续挖累计的秒数；由 PlanetEditBrush::ShouldDigNow 推进并清零。 */
	float ContinuousDigAccumulatorSeconds = 0.f;
```

- [ ] **Step 4: 实现**

`Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp`：

`SetEditMode` 的 `if (!bEditMode)` 块里（`HideBrushPreview();` 之后）加一行：

```cpp
		// 退出编辑就停连挖：否则按 G 离开后组件还会在后台继续挖。
		SetContinuousDigging(false);
```

`AdjustBrushSize` 之后（`DigAtScreenPosition` 之前）加：

```cpp
void UPlanetEditComponent::SetContinuousDigging(const bool bEnabled)
{
	bContinuousDigging = bEnabled;
	// 无论开还是关都清零：开启时按下那一下已经由控制器挖过，不该立刻再挖；
	// 关闭时清掉余量，下次开启从干净状态起算。
	ContinuousDigAccumulatorSeconds = 0.f;
}
```

`TickComponent` 改为：

```cpp
void UPlanetEditComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateBrushPreview();

	if (!bContinuousDigging)
	{
		return;
	}

	if (!PlanetEditBrush::ShouldDigNow(
			ContinuousDigAccumulatorSeconds, DeltaTime, ContinuousDigIntervalSeconds))
	{
		return;
	}

	// 与预览同源：Canvas 只在 PostRender 有效，鼠标位置从控制器查。
	UWorld* World = GetWorld();
	APlayerController* PlayerController = World ? World->GetFirstPlayerController() : nullptr;
	float MouseX = 0.f;
	float MouseY = 0.f;
	if (PlayerController && PlayerController->GetMousePosition(MouseX, MouseY))
	{
		DigAtScreenPosition(FVector2D(MouseX, MouseY));
	}
}
```

- [ ] **Step 5: 重建并跑测试**

构建后跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.EditComponent" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_edit_component.log"
```

预期：`Defaults` / `State` / `ContinuousDig` 三组全过。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h \
        Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
git commit -m "feat(行星编辑): 组件支持按住连续挖

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h \
        Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp \
        Source/StellarEra/Private/PlanetEditTests.cpp
```

---

### Task 3: 星图默认笔刷 64 + HUD 文案

**Files:**
- Modify: `Source/StellarEraUI/Private/StarMapCameraPawn.cpp:62-79`
- Test: `Source/StellarEra/Private/StarMapTests.cpp:545-548`

**Interfaces:**
- Consumes: `UPlanetEditComponent::ContinuousDigIntervalSeconds`（Task 2，构造函数里不设，用类默认 0.12）
- Produces: 星图 Pawn CDO 上 `BrushSizeVoxels == 64`、`PlaceActionHint == "Shift+左键按住连续挖 · Ctrl+左键填"`。

- [ ] **Step 1: 改测试（先失败）**

`Source/StellarEra/Private/StarMapTests.cpp` 的 `FStellarEraStarMapEditComponentDefaultsTest` 里：

```cpp
	TestEqual(TEXT("默认 64 体素"), Edit->GetBrushSizeVoxels(), 64);
	TestEqual(TEXT("下限 8 体素"), Edit->GetMinBrushSizeVoxels(), 8);
	TestEqual(TEXT("上限 128 体素"), Edit->GetMaxBrushSizeVoxels(), 128);
	TestEqual(TEXT("提示含连续挖与 Ctrl 填"), Edit->GetPlaceActionHint(),
		FString(TEXT("Shift+左键按住连续挖 · Ctrl+左键填")));
```

- [ ] **Step 2: 确认测试先失败**

构建后跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap.EditComponentDefaults" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_starmap_defaults.log"
```

预期：两条失败——默认 32 ≠ 64、提示文案不等。

- [ ] **Step 3: 改构造函数**

`Source/StellarEraUI/Private/StarMapCameraPawn.cpp` 里把注释与配置改成：

```cpp
	// --- 行星编辑：与角色共用组件，参数按星图尺度调大 ---
	//
	// 尺寸 8..128 体素：1 体素 = 100 cm（前两颗行星），128 = 128 米级的陨石坑。
	// 默认 64 体素：默认值偏大是因为「直径 32 的坑在这个距离上几乎看不见」，
	// 64 既看得出是一个坑，又不至于一点就挖穿。
	// `[` / `]` 每次 ±1（与角色一致，走 AdjustBrushSize）。
	PlanetEdit = CreateDefaultSubobject<UPlanetEditComponent>(TEXT("PlanetEdit"));
	PlanetEdit->ConfigureBrushRange(
		PlanetEditBrush::MinStarMapSizeVoxels, PlanetEditBrush::MaxStarMapSizeVoxels,
		/*InitialSize=*/64);
	// 128 是直径：半径 64 体素的球在体素空间里是百万格量级，填充时整球都要改，
	// 单线程会卡住整帧。
	PlanetEdit->bMultiThreadedEdits = true;
	// 右键在星图里已经归绕轨，所以「填」改用 Ctrl+左键；Shift+左键按住是连续挖。
	PlanetEdit->PlaceActionHint = TEXT("Shift+左键按住连续挖 · Ctrl+左键填");
	// 与 MaxOrbitDistanceCm 同一尺度：从最远 50 AU 处也该打得到行星。
	PlanetEdit->MaxEditDistanceCm = 5.0e7f;
```

- [ ] **Step 4: 重建并跑测试**

构建后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.StarMap" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_starmap_all.log"
```

预期：`StellarEra.StarMap.*` 全过。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEraUI/Private/StarMapCameraPawn.cpp Source/StellarEra/Private/StarMapTests.cpp
git commit -m "feat(星图): 默认笔刷 64 体素，提示改为连续挖 / Ctrl 填

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEraUI/Private/StarMapCameraPawn.cpp Source/StellarEra/Private/StarMapTests.cpp
```

---

### Task 4: 控制器左键分派（Shift 连挖 / Ctrl 填）+ 松开清理 + 视角抑制

**Files:**
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp:1019-1083`（`OnStarMapSelectStarted`）、`:1135-1141`（`OnStarMapSelectCompleted` 开头）、`:402-413`（`OnLook` 星图分支）

**Interfaces:**
- Consumes: `UPlanetEditComponent::SetContinuousDigging` / `IsContinuousDigging`（Task 2）
- Produces: 星图编辑模式下的左键语义——`Shift` 按住连挖、`Ctrl` 填一次、其它挖一次。

**说明：** 输入分派必须有真实 Pawn、真实按键状态才能测，无头 runner 跑不了；本任务的验收是「编译零警告 + 走查代码 + 最后 PIE 手验」。

- [ ] **Step 1: 改左键分派**

`OnStarMapSelectStarted` 里，`if (Edit->TryResolveTarget(MousePosition, Target))` 整个块替换为：

```cpp
		FPlanetEditTarget Target;
		if (Edit->TryResolveTarget(MousePosition, Target))
		{
			// 星图：Shift+左键按住 = 连续挖（移动光标沿路径挖）；Ctrl+左键 = 填一次；
			// 左键 = 挖一次。角色没有连续挖，也没把「填」挪到 Ctrl —— 仍是右键点放。
			const bool bStarMap = Cast<AStarMapCameraPawn>(GetPawn()) != nullptr;
			const bool bShift =
				IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
			const bool bCtrl =
				IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);

			if (bStarMap && bShift)
			{
				// 按下先挖一次，之后交给组件的 Tick 节流。
				Edit->SetContinuousDigging(true);
				Edit->DigAtScreenPosition(MousePosition);
			}
			else if (bStarMap && bCtrl)
			{
				Edit->PlaceAtScreenPosition(MousePosition);
			}
			else
			{
				Edit->DigAtScreenPosition(MousePosition);
			}
			return;
		}
```

- [ ] **Step 2: 松开时清理**

`OnStarMapSelectCompleted` 函数体**最前面**（`if (ActiveMineralView)` 之前）插入：

```cpp
	// 连续挖必须在所有早退之前关掉：按下时命中了行星走的是上面的「消费点击」
	// 分支，bStarMapSelectHeld 没被置位，下面的早退会把它跳过。
	if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
	{
		Edit->SetContinuousDigging(false);
	}
```

- [ ] **Step 3: 连续挖期间不转视角**

`OnLook` 的星图分支改为：

```cpp
	else if (AStarMapCameraPawn* StarMap = Cast<AStarMapCameraPawn>(GetPawn()))
	{
		// 连续挖期间拖鼠标 = 移动光标（挖掘点），不转视角；否则没法在球面上
		// 画挖掘路径。星图的鼠标位移只在按住鼠标（CaptureDuringMouseDown）
		// 时才有值，所以这条只影响「Shift+左键拖拽」这一种情况。
		if (UPlanetEditComponent* Edit = GetEditComponentInEditMode())
		{
			if (Edit->IsContinuousDigging())
			{
				return;
			}
		}

		// 星图里左键拖拽就是转视角，不设 bLookHeld 那道门 —— 那道门是给角色
		// 「不按右键就别动视角」用的；星图的相机除了转视角没有别的职责。
		//
		// 矿物透视是唯一例外：左键归图例点击，视角只认右键（与角色同一套门）。
		if (ActiveMineralView && !bLookHeld)
		{
			return;
		}
		StarMap->OrbitLook(DeltaYaw, DeltaPitch);
	}
```

- [ ] **Step 4: 构建确认零警告**

构建命令同全局约束。预期：`Result: Succeeded`，没有 warning。

- [ ] **Step 5: 提交**

```bash
git add Source/StellarEra/Private/StellarEraPlayerController.cpp
git commit -m "feat(星图): Shift+左键按住连续挖，Ctrl+左键填，连挖期间不转视角

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEra/Private/StellarEraPlayerController.cpp
```

---

### Task 5: 星图相机优先生成 `BP_StarMapCameraPawn`

**Files:**
- Modify: `Source/StellarEra/Public/StellarEraPlayerController.h:11`（前向声明附近）与 `:256-258`（`StarMapPawn` 成员附近）
- Modify: `Source/StellarEra/Private/StellarEraPlayerController.cpp:13-24`（include 区）、`:87-91`（构造函数）、`:971-980`（发 SpawnActor 处）

**Interfaces:**
- Produces: `TSubclassOf<AStarMapCameraPawn> AStellarEraPlayerController::StarMapPawnClass`，构造函数里解析 `/Game/Code/BP_StarMapCameraPawn.BP_StarMapCameraPawn_C`，失败退回 `AStarMapCameraPawn::StaticClass()`。

- [ ] **Step 1: 头文件**

`Source/StellarEra/Public/StellarEraPlayerController.h`，`#include "GameFramework/PlayerController.h"` 之后加：

```cpp
#include "Templates/SubclassOf.h"
```

`StarMapPawn` 成员之前加：

```cpp
	/**
	 * 生成星图相机用的类：优先蓝图子类 BP_StarMapCameraPawn。
	 *
	 * 笔刷区间 / 默认尺寸 / 连续挖间隔都在它的 PlanetEdit 组件细节面板里，
	 * 不用重编译就能调。蓝图缺失时退回原生 AStarMapCameraPawn，功能不坏，
	 * 但参数只能改 C++（构造函数里有 Warning）。
	 */
	UPROPERTY()
	TSubclassOf<AStarMapCameraPawn> StarMapPawnClass;
```

- [ ] **Step 2: include**

`Source/StellarEra/Private/StellarEraPlayerController.cpp` 的 include 区加：

```cpp
#include "UObject/ConstructorHelpers.h"
```

- [ ] **Step 3: 构造函数解析蓝图类**

```cpp
AStellarEraPlayerController::AStellarEraPlayerController()
{
	// 输入组件由 DefaultInput.ini 指定为 EnhancedInput 版本；这里不额外设置。
	// 若 DefaultInput.ini 缺失，Cast<UEnhancedInputComponent> 会失败。

	// 星图相机优先用蓝图子类：笔刷区间、默认尺寸、连续挖间隔都在它的
	// PlanetEdit 组件细节面板里，不重编译就能调。找不到时退回原生类。
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
			TEXT("AStellarEraPlayerController: 找不到 BP_StarMapCameraPawn，"
				 "将使用原生 AStarMapCameraPawn（编辑参数只能在 C++ 里改）。"));
	}
}
```

- [ ] **Step 4: SpawnActor 用这个类**

把：

```cpp
		StarMapPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
			AStarMapCameraPawn::StaticClass(), FTransform::Identity, SpawnParams);
```

改为：

```cpp
		UClass* const PawnClass = StarMapPawnClass
			? StarMapPawnClass.Get()
			: AStarMapCameraPawn::StaticClass();
		StarMapPawn = GetWorld()->SpawnActor<AStarMapCameraPawn>(
			PawnClass, FTransform::Identity, SpawnParams);
```

- [ ] **Step 5: 构建**

构建命令同全局约束。预期：`Result: Succeeded`，零警告。此时蓝图还不存在，启动日志里会出现那条 Warning —— 这是预期的，下一步建资产。

- [ ] **Step 6: 提交**

```bash
git add Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp
git commit -m "feat(星图): 相机优先生成 BP_StarMapCameraPawn

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Source/StellarEra/Public/StellarEraPlayerController.h \
        Source/StellarEra/Private/StellarEraPlayerController.cpp
```

---

### Task 6: 全量构建 + 全量自动化测试

**Files:** 无改动。

- [ ] **Step 1: 关编辑器后全量构建**

构建命令同全局约束。预期：`Result: Succeeded`，零 warning。

- [ ] **Step 2: 跑全量测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="C:/Users/23080/AppData/Local/Temp/stellar_all_tests.log"
```

预期：没有 `Failed`；日志末尾有完整的 `X test(s) passed` 汇总。把失败项逐个修掉再继续，**不要带着红测试往下走**。

- [ ] **Step 3: 记录证据**

在实施记录里贴：构建输出最后 5 行、测试汇总行、DLL 的 mtime（`ls -l Binaries/Win64/UnrealEditor-StellarEra*.dll`），证明代码真的编译进去了。

---

### Task 7: 创建 `BP_StarMapCameraPawn` 资产

**Files:**
- Create: `Content/Code/BP_StarMapCameraPawn.uasset`
- Temp script（不进仓库）：`C:/Users/23080/AppData/Local/Temp/create_starmap_bp.py`

- [ ] **Step 1: 写创建脚本**

```python
import unreal

PACKAGE_PATH = "/Game/Code/BP_StarMapCameraPawn"
PARENT = "/Script/StellarEraUI.StarMapCameraPawn"

if unreal.EditorAssetLibrary.does_asset_exist(PACKAGE_PATH):
    unreal.log("BP_StarMapCameraPawn 已存在，跳过创建")
else:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.load_class(None, PARENT))
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    bp = tools.create_asset("BP_StarMapCameraPawn", "/Game/Code", unreal.Blueprint, factory)
    if not bp:
        unreal.log_error("创建 BP_StarMapCameraPawn 失败")
    else:
        unreal.EditorAssetLibrary.save_loaded_asset(bp)
        unreal.log("已创建 BP_StarMapCameraPawn")
```

- [ ] **Step 2: 无头运行脚本**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -run=pythonscript -script="C:/Users/23080/AppData/Local/Temp/create_starmap_bp.py" \
  -unattended -nopause -nullrhi -nosplash -stdout \
  -abslog="C:/Users/23080/AppData/Local/Temp/stellar_create_bp.log"
```

预期：日志出现 `已创建 BP_StarMapCameraPawn`，进程自行退出。

- [ ] **Step 3: 机械确认资产落盘**

```bash
ls -l Content/Code/BP_StarMapCameraPawn.uasset
head -c 200 Content/Code/BP_StarMapCameraPawn.uasset | od -c | head -3
```

预期：文件存在、大小 > 1 KB。若前 200 字节是 `version https://git-lfs...` 的指针文本，说明 LFS 没 smudge：

```bash
git lfs checkout Content/Code/BP_StarMapCameraPawn.uasset
```

- [ ] **Step 4: 确认控制器用上了蓝图**

再跑一次 Task 6 Step 2 的测试命令；日志里应当**没有** `找不到 BP_StarMapCameraPawn` 这条 Warning（`FClassFinder` 在 CDO 构造期解析，`UnrealEditor-Cmd` 会加载控制器 CDO）。

- [ ] **Step 5: 提交**

```bash
git add Content/Code/BP_StarMapCameraPawn.uasset
git commit -m "feat(星图): 新增 BP_StarMapCameraPawn，编辑参数细节面板可调

Co-Authored-By: Claude Code <noreply@anthropic.com>" -- \
        Content/Code/BP_StarMapCameraPawn.uasset
```

---

### Task 8: PIE 手验（UniverseMap）

**Files:** 无改动。

- [ ] **Step 1: 进星图编辑**

编辑器中打开 `Content/Maps/UniverseMap.umap` → PIE → `M` 进星图 → `G` 进编辑 → 滚轮拉到离地最近。

- [ ] **Step 2: 连续挖**

`Shift`+左键按住，拖动鼠标在球面上画路径。

预期：
- 出现连续的坑道，不是一个孤立的坑；
- **相机不转**（画面保持同一视角，只有光标在动）；
- 松开左键后连挖立即停止；再单击一次只出一个坑。

- [ ] **Step 3: Ctrl 填**

`Ctrl`+左键单击。预期：填出一个堆，且只填一次（按住不放也只填一次）。

- [ ] **Step 4: 尺寸**

`[` / `]` 改尺寸；HUD 提示显示 `笔刷 N 体素 · 左键挖 / Shift+左键按住连续挖 · Ctrl+左键填 · G 退出`，N 初始为 64。

- [ ] **Step 5: 细节面板**

打开 `Content/Code/BP_StarMapCameraPawn` → 选中 `PlanetEdit` 组件 → 把 `BrushSizeVoxels` 改成 96、`ContinuousDigIntervalSeconds` 改成 0.3 → 编译并保存蓝图 → 重新 PIE。预期：初始笔刷 96，连挖节奏明显变慢，**不需要重新编译 C++**。

- [ ] **Step 6: 退出与回归**

- 连续挖中途按 `G` 退出编辑，再按 `G` 进来：不会残留连续挖掘。
- 连续挖中途按 `M` 退出星图：不会残留。
- 角色编辑模式：左键挖、右键点放、滚轮调尺寸，行为与改动前一致。

- [ ] **Step 7: 记录结果**

在实施记录里逐条写「通过 / 失败 + 现象」，失败项回到对应任务修。

---

## 自检

- [x] Spec 覆盖：连续挖（Task 1/2/4）、按键迁移（Task 4）、细节面板（Task 5/7）、默认 64（Task 3）、HUD（Task 3）、测试（Task 1/2/3/6）、手验（Task 8）。
- [x] 无占位符：所有代码块都是可直接粘贴的完整实现。
- [x] 类型一致：`SetContinuousDigging(bool)` / `IsContinuousDigging()` / `ContinuousDigIntervalSeconds` / `PlanetEditBrush::ShouldDigNow(float&, float, float)` 在各任务中签名一致。
- [x] 已知不可自动测的部分（Tick 实际挖掘、按键分派、蓝图细节面板）明确放在 Task 8 手验。
