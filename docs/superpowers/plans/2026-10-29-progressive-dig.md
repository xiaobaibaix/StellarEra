# 分块跨帧挖掘 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让星图/角色的挖掘落笔不再造成可感知的帧停顿 —— 一笔编辑切成块，每帧只花固定预算推进几块，洞从光标处逐帧向外长。

**Architecture:** 新增一个「有进度的编辑」对象 `FPlanetProgressiveEdit`，它保存整笔编辑的参数、预切好的块列表和进度；组件在 Tick 里按时间预算推进它。逐格写入语义从体素插件逐字照抄，只是把「一次调用写整块包围盒」换成「多次调用写各块」。

**Tech Stack:** UE 5.8.3 / C++、VoxelFree 插件公开 API（`FVoxelData::ParallelSet`、`FVoxelWriteScopeLock`、`FVoxelToolHelpers`、`FVoxelSphereToolsImpl::GetBounds`）、UE Automation 测试。

**Spec:** `docs/superpowers/specs/2026-10-29-progressive-dig-design.md`

## Global Constraints

- **绝不修改** `Plugins/VoxelFree/` 与 `Plugins/VibeUE/`。
- 相对路径一律相对于 `F:/workspace/ue/StellarEra`。
- 含中文的 `.h` / `.cpp` 存 **UTF-8 with BOM**；不要统一换行符（仓库本来就是 CRLF/LF 混合）。
- Epic 命名：`A`/`U`/`F`/`E`/`I`/`T` 前缀，布尔 `bXxx`，UObject 指针 `TObjectPtr<>`，每个 UObject 类写 `GENERATED_BODY()`，头文件 `#pragma once`。
- 构建前必须关编辑器；只能用捆绑 dotnet 直调 UBT（cwd 在项目外），**不要用 `Build.bat`**。
- 提交必须带 pathspec（`git commit -- <paths>`）；**绝不提交** `Content/Saves/PlanetVoxel/UniverseMap/BP_VoxelPlanet3.uasset`。
- 自动化测试前缀必须是 `StellarEra.`。
- 逐格语义**逐字照抄**插件，不许自己发明（见 Spec §3.6）。

---

## Task 1: 笔刷纯几何 —— 球包围盒与切块

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp`
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`（新建）

**Interfaces:**
- Produces:
  - `FPlanetEditVoxelBox PlanetEditBrush::MakeSphereEditVoxelBox(const FVector& CenterVoxels, float RadiusVoxels)`
  - `void PlanetEditBrush::SplitVoxelBoxIntoChunks(const FPlanetEditVoxelBox& Box, int32 ChunkSizeVoxels, TArray<FPlanetEditVoxelBox>& OutChunks)`

- [ ] **Step 1: 写失败测试**

新建 `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`（UTF-8 with BOM）：

```cpp
// 分块跨帧挖掘的纯几何回归测试。
//
// 这里钉的是「切块」这件事的正确性：一块一块地写，与一次写完整个包围盒，
// 逐格语义必须完全相同。前提是块**不重叠**且**并集恰好等于包围盒** ——
// 满足这两条，同一个逐格谓词在块上应用一次、在整盒上应用一次，结果按定义
// 相同（谓词只读格子自己的坐标，不依赖邻格）。所以这两个性质就是等价性的
// 全部依据，必须机械化检查。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetEditBrush.h"
#include "VoxelIntBox.h"
#include "VoxelTools/Impl/VoxelSphereToolsImpl.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraProgressiveChunkTest,
	"StellarEra.Planet.Edit.ProgressiveChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraProgressiveChunkTest::RunTest(const FString& Parameters)
{
	// --- 1. 球包围盒必须与插件逐分量相同 ---
	// 直接调插件的 GetBounds 比对，不写死期望数字：我们抄的就是它，
	// 它改了我们必须跟着改（否则球的边缘会被削掉一圈或多挖一圈）。
	{
		const FVoxelVector Centers[] = {
			FVoxelVector(0.f, 0.f, 0.f),
			FVoxelVector(1234.75f, -987.5f, 20000.25f),
			FVoxelVector(-0.4f, 0.6f, 1.5f),
		};
		const float Radii[] = {0.5f, 1.f, 7.f, 64.f, 640.f};

		for (const FVoxelVector& Center : Centers)
		{
			for (const float Radius : Radii)
			{
				const FVoxelIntBox Expected = FVoxelSphereToolsImpl::GetBounds(Center, Radius);
				const FPlanetEditVoxelBox Actual = PlanetEditBrush::MakeSphereEditVoxelBox(
					FVector(Center.X, Center.Y, Center.Z), Radius);

				TestTrue(FString::Printf(
					TEXT("球包围盒 R=%.1f 中心=(%.1f,%.1f,%.1f)：Min 期望 %s，实际 %s"),
					Radius, Center.X, Center.Y, Center.Z,
					*Expected.Min.ToString(), *Actual.Min.ToString()),
					Expected.Min == Actual.Min);
				TestTrue(FString::Printf(
					TEXT("球包围盒 R=%.1f 中心=(%.1f,%.1f,%.1f)：Max 期望 %s，实际 %s"),
					Radius, Center.X, Center.Y, Center.Z,
					*Expected.Max.ToString(), *Actual.MaxExclusive.ToString()),
					Expected.Max == Actual.MaxExclusive);
			}
		}
	}

	// --- 2. 切块：不重叠、并集 == 原盒、尺寸不超预算 ---
	// 用非对齐的盒子起点，专门覆盖「块边界不落在 32 网格上」的情况 ——
	// ParallelSet 按叶子重叠迭代，边界叶子被两块各处理一部分，并集仍正确。
	const auto CheckTiling = [&](const FPlanetEditVoxelBox& Box, int32 ChunkSize, const TCHAR* Label)
	{
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Box, ChunkSize, Chunks);

		TestTrue(FString::Printf(TEXT("%s：必须切出至少一块"), Label), Chunks.Num() > 0);
		if (Chunks.Num() == 0)
		{
			return;
		}

		// 体积守恒 + 无重叠：块体积累加必须**恰好**等于原盒体积。
		// 重叠会让和偏大，漏掉一块会让和偏小，两种错都能抓到。
		int64 SumCells = 0;
		FIntVector UnionMin = Chunks[0].Min;
		FIntVector UnionMax = Chunks[0].MaxExclusive;
		bool bSizesOk = true;
		for (const FPlanetEditVoxelBox& Chunk : Chunks)
		{
			const FIntVector Size = Chunk.GetSize();
			SumCells += int64(Size.X) * Size.Y * Size.Z;
			bSizesOk &= Size.X <= ChunkSize && Size.Y <= ChunkSize && Size.Z <= ChunkSize;
			UnionMin = FIntVector(
				FMath::Min(UnionMin.X, Chunk.Min.X),
				FMath::Min(UnionMin.Y, Chunk.Min.Y),
				FMath::Min(UnionMin.Z, Chunk.Min.Z));
			UnionMax = FIntVector(
				FMath::Max(UnionMax.X, Chunk.MaxExclusive.X),
				FMath::Max(UnionMax.Y, Chunk.MaxExclusive.Y),
				FMath::Max(UnionMax.Z, Chunk.MaxExclusive.Z));
		}

		const FIntVector BoxSize = Box.GetSize();
		const int64 BoxCells = int64(BoxSize.X) * BoxSize.Y * BoxSize.Z;
		TestEqual(FString::Printf(TEXT("%s：块体积累加必须等于原盒（重叠/漏块都会破坏它）"), Label),
			SumCells, BoxCells);
		TestTrue(FString::Printf(TEXT("%s：并集 Min 必须等于原盒 Min"), Label), UnionMin == Box.Min);
		TestTrue(FString::Printf(TEXT("%s：并集 Max 必须等于原盒 Max"), Label),
			UnionMax == Box.MaxExclusive);
		TestTrue(FString::Printf(TEXT("%s：单块边长不得超预算"), Label), bSizesOk);

		// 体积累加相等 + 并集相等 还不足以保证无重叠（可能既有重叠又有漏）。
		// 逐格计数兜底：每格必须**恰好**被块覆盖一次。盒子较大时抽样，
		// 避免测试本身太慢。
		const int32 StepX = FMath::Max(BoxSize.X / 12, 1);
		const int32 StepY = FMath::Max(BoxSize.Y / 12, 1);
		const int32 StepZ = FMath::Max(BoxSize.Z / 12, 1);
		bool bCoveredOnce = true;
		for (int32 X = Box.Min.X; X < Box.MaxExclusive.X; X += StepX)
		for (int32 Y = Box.Min.Y; Y < Box.MaxExclusive.Y; Y += StepY)
		for (int32 Z = Box.Min.Z; Z < Box.MaxExclusive.Z; Z += StepZ)
		{
			int32 Hits = 0;
			for (const FPlanetEditVoxelBox& Chunk : Chunks)
			{
				if (X >= Chunk.Min.X && X < Chunk.MaxExclusive.X &&
					Y >= Chunk.Min.Y && Y < Chunk.MaxExclusive.Y &&
					Z >= Chunk.Min.Z && Z < Chunk.MaxExclusive.Z)
				{
					++Hits;
				}
			}
			if (Hits != 1)
			{
				bCoveredOnce = false;
			}
		}
		TestTrue(FString::Printf(TEXT("%s：抽样格子必须恰好被一块覆盖"), Label), bCoveredOnce);
	};

	CheckTiling(FPlanetEditVoxelBox{FIntVector(0, 0, 0), FIntVector(10, 10, 10)}, 128, TEXT("小盒"));
	CheckTiling(FPlanetEditVoxelBox{FIntVector(-1007, 33, 501), FIntVector(255, 300, 129)}, 128,
		TEXT("非对齐中盒"));
	CheckTiling(FPlanetEditVoxelBox{FIntVector(-640, -640, -640), FIntVector(640, 640, 640)}, 128,
		TEXT("1280 立方"));

	// --- 3. 小块笔刷必须退化成单块 ---
	// 半径 8 的球包围盒只有 22³，切 128 的块必须恰好得到 1 块 ——
	// 这条保证小笔刷的路径与改动前完全一致。
	{
		const FPlanetEditVoxelBox Small = PlanetEditBrush::MakeSphereEditVoxelBox(
			FVector(100.0, 200.0, 300.0), 8.f);
		TArray<FPlanetEditVoxelBox> Chunks;
		PlanetEditBrush::SplitVoxelBoxIntoChunks(Small, 128, Chunks);
		TestEqual(TEXT("半径 8 的球必须只切出 1 块"), Chunks.Num(), 1);
	}

	return true;
}

#endif
```

- [ ] **Step 2: 跑测试确认失败**

构建命令见 Task 5 Step 1，然后：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Edit.ProgressiveChunks" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/progressive-chunks.log"
```

Expected: 编译失败，`MakeSphereEditVoxelBox` / `SplitVoxelBoxIntoChunks` 未声明。

- [ ] **Step 3: 实现**

`PlanetEditBrush.h` 在 `IsValidVoxelBox` 声明之后加：

```cpp
	/**
	 * 球编辑的完整包围盒（体素空间，右端开区间）。
	 *
	 * 与插件的 FVoxelSphereToolsImpl::GetBounds 逐分量相同：
	 *   Min = FloorToInt(Center - Radius - 3)
	 *   Max = CeilToInt(Center + Radius + 3) + 1
	 *
	 * 那个 ±3 是插件留的余量（SphereEdit 的衰减带最外圈在 Radius + 2，
	 * 再垫一格）。抄错它的方向很危险：少一格会削掉球最外圈，多一格会多挖。
	 * 有单测直接与插件比对，不写死期望值。
	 */
	STELLARERAPLANETEDIT_API FPlanetEditVoxelBox MakeSphereEditVoxelBox(
		const FVector& CenterVoxels, float RadiusVoxels);

	/**
	 * 把体素盒切成边长不超过 ChunkSizeVoxels 的块，按「离盒子中心由近到远」排序。
	 *
	 * 保证：块两两不重叠，并集**恰好**等于输入盒（单测钉住）。这两条是
	 * 「分块写 == 一次写」的充分条件 —— 逐格谓词只看格子自己的坐标，不依赖
	 * 邻格状态，所以按任意划分分别应用再并起来，与一次性应用完全相同。
	 *
	 * 不要求块边界落在 32 的网格上：FVoxelData::ParallelSet 是按
	 * 「叶子与本块的 Overlap」迭代的，跨边界的叶子被两块各处理一部分，结果仍对。
	 *
	 * 排序是为了让挖掘时洞从光标处向外长，而不是从左下角开始逐行填 ——
	 * 逐帧刷新渲染时这个顺序就是用户看到的样子。
	 */
	STELLARERAPLANETEDIT_API void SplitVoxelBoxIntoChunks(
		const FPlanetEditVoxelBox& Box, int32 ChunkSizeVoxels,
		TArray<FPlanetEditVoxelBox>& OutChunks);
```

`PlanetEditBrush.cpp` 末尾加：

```cpp
FPlanetEditVoxelBox PlanetEditBrush::MakeSphereEditVoxelBox(
	const FVector& CenterVoxels, const float RadiusVoxels)
{
	// 全部按 float 算：插件的 FVoxelVector 是 float，GetBounds 里
	// `Position - Radius - 3` 也在 float 上做。用 double 会在边界上
	// 差半个 ULP 就取整歪一格。
	const float MinX = static_cast<float>(CenterVoxels.X) - RadiusVoxels - 3.f;
	const float MinY = static_cast<float>(CenterVoxels.Y) - RadiusVoxels - 3.f;
	const float MinZ = static_cast<float>(CenterVoxels.Z) - RadiusVoxels - 3.f;
	const float MaxX = static_cast<float>(CenterVoxels.X) + RadiusVoxels + 3.f;
	const float MaxY = static_cast<float>(CenterVoxels.Y) + RadiusVoxels + 3.f;
	const float MaxZ = static_cast<float>(CenterVoxels.Z) + RadiusVoxels + 3.f;

	FPlanetEditVoxelBox Box;
	// 右端是 CeilToInt(Max) + 1（FVoxelIntBox 从浮点构造时就是这么取的）。
	// 注意不能写成 FloorToInt(Max) + 1：Max 非整数时两者差一格。
	Box.Min = FIntVector(FMath::FloorToInt(MinX), FMath::FloorToInt(MinY), FMath::FloorToInt(MinZ));
	Box.MaxExclusive = FIntVector(
		FMath::CeilToInt(MaxX) + 1, FMath::CeilToInt(MaxY) + 1, FMath::CeilToInt(MaxZ) + 1);
	return Box;
}

void PlanetEditBrush::SplitVoxelBoxIntoChunks(
	const FPlanetEditVoxelBox& Box, const int32 ChunkSizeVoxels,
	TArray<FPlanetEditVoxelBox>& OutChunks)
{
	OutChunks.Reset();
	if (!IsValidVoxelBox(Box))
	{
		return;
	}

	const int32 ChunkSize = FMath::Max(ChunkSizeVoxels, 1);

	// 三轴分别按 ChunkSize 步进，笛卡尔积得到块；最后一块按盒子右端裁。
	for (int32 X = Box.Min.X; X < Box.MaxExclusive.X; X += ChunkSize)
	{
		for (int32 Y = Box.Min.Y; Y < Box.MaxExclusive.Y; Y += ChunkSize)
		{
			for (int32 Z = Box.Min.Z; Z < Box.MaxExclusive.Z; Z += ChunkSize)
			{
				FPlanetEditVoxelBox Chunk;
				Chunk.Min = FIntVector(X, Y, Z);
				Chunk.MaxExclusive = FIntVector(
					FMath::Min(X + ChunkSize, Box.MaxExclusive.X),
					FMath::Min(Y + ChunkSize, Box.MaxExclusive.Y),
					FMath::Min(Z + ChunkSize, Box.MaxExclusive.Z));
				OutChunks.Add(Chunk);
			}
		}
	}

	// 按块中心到盒子中心的距离升序。用整数平方距离：1280³ 的盒子里分量差
	// 能到 640，平方和只有几百万，int64 富余；小块中心用「两倍」表示，
	// 避免整除带来的半格误差。
	const FIntVector BoxCenterTimes2(
		Box.Min.X + Box.MaxExclusive.X,
		Box.Min.Y + Box.MaxExclusive.Y,
		Box.Min.Z + Box.MaxExclusive.Z);

	Algo::Sort(OutChunks, [&BoxCenterTimes2](const FPlanetEditVoxelBox& A, const FPlanetEditVoxelBox& B)
	{
		const auto SquaredDistance = [&BoxCenterTimes2](const FPlanetEditVoxelBox& Chunk)
		{
			const int64 DX = int64(Chunk.Min.X + Chunk.MaxExclusive.X) - BoxCenterTimes2.X;
			const int64 DY = int64(Chunk.Min.Y + Chunk.MaxExclusive.Y) - BoxCenterTimes2.Y;
			const int64 DZ = int64(Chunk.Min.Z + Chunk.MaxExclusive.Z) - BoxCenterTimes2.Z;
			return DX * DX + DY * DY + DZ * DZ;
		};
		return SquaredDistance(A) < SquaredDistance(B);
	});
}
```

需要 `#include "Algo/Sort.h"`。

- [ ] **Step 4: 构建 + 跑测试通过**

Expected: `Result: Succeeded`；测试 `Success`。

- [ ] **Step 5: 提交**

```bash
git commit -m "feat(编辑): 球编辑包围盒与体素盒切块的纯几何" \
  -- Source/StellarEraPlanetEdit/Public/PlanetEditBrush.h \
     Source/StellarEraPlanetEdit/Private/PlanetEditBrush.cpp \
     Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp
```

（pathspec 挂在 commit 上，不要先 `git add` 再裸 `git commit`。）

---

## Task 2: 执行单元 `FPlanetProgressiveEdit`

**Files:**
- Create: `Source/StellarEraPlanetEdit/Public/PlanetProgressiveEdit.h`
- Create: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEdit.cpp`
- Test: `Source/StellarEraPlanetEdit/Private/PlanetProgressiveEditTests.cpp`（追加）

**Interfaces:**
- Consumes: Task 1 的两个纯函数。
- Produces:
  - `bool FPlanetProgressiveEdit::Setup(AVoxelPlanetActor*, const FVector& CenterWorld, EPlanetBrushShape, float RadiusCm, bool bPlace, int32 ChunkSizeVoxels)`
  - `bool ApplyNextChunk()` / `bool ApplyAll()` / `bool IsFinished() const`
  - `bool DidChangeAnything() const` / `const FPlanetEditVoxelBox& GetAppliedBounds() const`
  - `TWeakObjectPtr<AVoxelPlanetActor> GetPlanet() const` / `int32 GetChunkCount() const`

- [ ] **Step 1: 写测试**

在 `PlanetProgressiveEditTests.cpp` 追加 `StellarEra.Planet.Edit.ProgressiveKernel`：
断言内核公式的性质（`D <= R-2 → Empty(+1)`、`D >= R+2 → 不改`、`D == R → 0`、
随 D 单调不增、填时取反）。谓词在 .cpp 里是局部的，测试通过
`FPlanetProgressiveEdit::EvaluateSphereValueForTest` 这个**仅测试可见**的静态入口调用
（实现里由同一条公式喂给 `Apply` 与测试，避免复制第二份）。

- [ ] **Step 2: 跑测试确认失败**
- [ ] **Step 3: 实现**

`PlanetProgressiveEdit.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 一笔有进度的编辑：整个包围盒切成块，每帧只推进几块。

#pragma once

#include "CoreMinimal.h"
#include "PlanetEditBrush.h"

class AVoxelPlanetActor;

/**
 * 一笔渐进式编辑。
 *
 * 存在理由：体素工具的代价是包围盒 Size³，一次 1280 体素的挖掘要扫 2×10⁹ 格，
 * 全在游戏线程上一口气做完就是几秒的冻结。本类把同一笔编辑切成块，
 * 由调用方每帧只推进几块，帧时间因此有上限。
 *
 * **逐格语义与插件完全相同**（见 .cpp 的 Apply 谓词）：分块只是改变了
 * 「谁在什么时候调 ParallelSet」，不改变任何一格写进去的值。
 *
 * 生命周期：Setup 建好后由调用方独占持有；非 UObject，用完即丢。
 */
class STELLARERAPLANETEDIT_API FPlanetProgressiveEdit
{
public:
	FPlanetProgressiveEdit() = default;

	/**
	 * 算包围盒、切块、记录中心格当前值（供「有没有真的改到」判断）。
	 * 行星无效、盒子无效时返回 false。
	 */
	bool Setup(
		AVoxelPlanetActor* InPlanet,
		const FVector& CenterWorld,
		EPlanetBrushShape InShape,
		float RadiusCm,
		bool bInPlace,
		int32 InChunkSizeVoxels);

	/**
	 * 应用下一块。返回 false = 没有剩余工作（已完成或行星已失效）。
	 * 每块内部开一个覆盖该块的写锁，锁的持有时间因此与块大小成正比，
	 * 而不是与整笔编辑成正比。
	 */
	bool ApplyNextChunk();

	/** 一次跑完剩余块，返回是否真的改到了体素。存档 / 退出前的安全点用。 */
	bool ApplyAll();

	bool IsFinished() const { return NextChunkIndex >= Chunks.Num(); }
	int32 GetChunkCount() const { return Chunks.Num(); }

	/** 唯一「有没有改到」判据：中心格编辑前后是否不同。 */
	bool DidChangeAnything() const { return bChangedAnything; }

	/** 已应用的块的并集（体素空间，右端开区间）。无效 = 一块都没做。 */
	const FPlanetEditVoxelBox& GetAppliedBounds() const { return AppliedBounds; }

	/** 整笔编辑的完整包围盒。 */
	const FPlanetEditVoxelBox& GetFullBounds() const { return FullBounds; }

	TWeakObjectPtr<AVoxelPlanetActor> GetPlanet() const { return Planet; }

	/**
	 * 球内核的逐格取值，**仅供测试**。运行时走 ApplyNextChunk 里的同一条公式
	 * （那里是内联 lambda，避免每格一次函数调用）。两处共用这一个函数体
	 * 是为了让测试与实现不可能写歪。
	 */
	static FVoxelValue EvaluateSphereValueForTest(
		float Radius, float SquaredRadiusPlus2, float SquaredRadiusMinus2,
		float SquaredDistance, FVoxelValue CurrentValue, bool bPlace);

private:
	void RecordAppliedChunk(const FPlanetEditVoxelBox& Chunk);
	void EvaluateChange();

	TWeakObjectPtr<AVoxelPlanetActor> Planet;

	EPlanetBrushShape Shape = EPlanetBrushShape::Sphere;
	bool bPlace = false;

	/** 体素空间的球心与半径（float，与插件同一个精度）。 */
	FVector CenterVoxels = FVector::ZeroVector;
	float RadiusVoxels = 0.f;

	FPlanetEditVoxelBox FullBounds;
	FPlanetEditVoxelBox AppliedBounds;
	TArray<FPlanetEditVoxelBox> Chunks;
	int32 NextChunkIndex = 0;

	/** 中心格的体素坐标与编辑前的值。 */
	FIntVector ProbeVoxel = FIntVector::ZeroValue;
	float ProbeValueBefore = 0.f;
	bool bChangedAnything = false;
	bool bEvaluatedChange = false;
};
```

`PlanetProgressiveEdit.cpp` 的核心：

```cpp
#include "PlanetProgressiveEdit.h"

#include "VoxelPlanetActor.h"
#include "VoxelIntBox.h"
#include "VoxelValue.h"
#include "VoxelVector.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelTools/VoxelDataTools.h"
#include "VoxelTools/VoxelToolHelpers.h"
#include "VoxelUtilities/VoxelMathUtilities.h"

bool FPlanetProgressiveEdit::Setup(...)
{
	// 世界 → 体素用插件自己的换算函数（与 UVoxelSphereTools 内部一致），
	// 不自己除 VoxelSize —— 那会漏掉 VoxelWorld 的缩放。
	const FVoxelVector RealPosition = FVoxelToolHelpers::GetRealPosition(
		InPlanet, CenterWorld, /*bConvertToVoxelSpace=*/true);
	const float RealRadius = FVoxelToolHelpers::GetRealDistance(
		InPlanet, RadiusCm, /*bConvertToVoxelSpace=*/true);
	CenterVoxels = FVector(RealPosition.X, RealPosition.Y, RealPosition.Z);
	RadiusVoxels = RealRadius;

	FullBounds = (Shape == EPlanetBrushShape::Sphere)
		? PlanetEditBrush::MakeSphereEditVoxelBox(CenterVoxels, RealRadius)
		: PlanetEditBrush::MakeVoxelBox(PlanetEditBrush::MakeBox(
			FVector(RealPosition.X, RealPosition.Y, RealPosition.Z), SizeVoxels, ...));
	...
}

FVoxelValue FPlanetProgressiveEdit::EvaluateSphereValueForTest(
	const float Radius, const float SquaredRadiusPlus2, const float SquaredRadiusMinus2,
	const float SquaredDistance, FVoxelValue CurrentValue, const bool bPlace)
{
	if (SquaredDistance > SquaredRadiusPlus2) return CurrentValue;
	if (SquaredDistance <= SquaredRadiusMinus2)
		return bPlace ? FVoxelValue::Full() : FVoxelValue::Empty();
	const float Distance = FMath::Sqrt(SquaredDistance);
	const FVoxelValue NewValue{
		FMath::Clamp(Radius - Distance, -2.f, 2.f) / 2 * (bPlace ? -1.f : 1.f) };
	return FVoxelUtilities::MergeAsset(CurrentValue, NewValue, !bPlace);
}
```

`ApplyNextChunk` 的谓词直接调 `EvaluateSphereValueForTest`（内联展开成本可忽略，
换来「测试与运行时是同一条公式」）。

- [ ] **Step 4: 构建 + 跑测试通过**
- [ ] **Step 5: 提交**（pathspec 带这三个文件）

---

## Task 3: `PlanetBrushApplicator::Apply` 改走 job

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetBrushApplicator.cpp`

**Interfaces:**
- Consumes: Task 2。
- Produces: `PlanetBrushApplicator::Apply` 的**签名与语义不变**（公开头不动）。

- [ ] **Step 1: 替换函数体**

`Apply` 内部改成「建一个单块 job 并 `ApplyAll`」，这样同步路径与渐进路径走
**同一份**谓词实现：

```cpp
bool PlanetBrushApplicator::Apply(...)
{
	const double StartSeconds = FPlatformTime::Seconds();

	FPlanetProgressiveEdit Edit;
	const float RadiusCm = PlanetEditBrush::GetHalfExtentCm(
		SizeVoxels, VoxelSize, MinSizeVoxels, MaxSizeVoxels);
	if (!Edit.Setup(&Planet, CenterWorld, Shape, RadiusCm, bPlace, /*ChunkSizeVoxels=*/MAX_int32))
	{
		return false;
	}

	if (!Edit.ApplyAll())
	{
		// 空操作不调 NotifyVoxelEdited（表面网格刷新 + 未保存标记）。
		return false;
	}

	if (OutEditedBounds && PlanetEditBrush::IsValidVoxelBox(Edit.GetFullBounds()))
	{
		OutEditedBounds->Min = Edit.GetFullBounds().Min;
		OutEditedBounds->MaxExclusive = Edit.GetFullBounds().MaxExclusive;
	}
	Planet.NotifyVoxelEdited();

	// 慢日志保持原样（>= 20 ms 才说话）
	...
	return true;
}
```

- [ ] **Step 2: 构建，确认既有 `StellarEra.Planet.Edit.*` 测试不受影响**
- [ ] **Step 3: 提交**

---

## Task 4: `UPlanetEditComponent` 队列 / 预算 / 安全点

**Files:**
- Modify: `Source/StellarEraPlanetEdit/Public/PlanetEditComponent.h`
- Modify: `Source/StellarEraPlanetEdit/Private/PlanetEditComponent.cpp`

**Interfaces:**
- Consumes: Task 2/3。
- Produces: 三个新 `UPROPERTY`：`ProgressiveEditChunkSizeVoxels`（128）、
  `ProgressiveEditBudgetMs`（4.0）、`MaxQueuedEdits`（16）。

- [ ] **Step 1: 头文件加成员与函数**

```cpp
	/** 渐进编辑的块边长（体素）。128³ ≈ 2.1e6 格，16 线程下约 3 ms。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "8"))
	int32 ProgressiveEditChunkSizeVoxels = 128;

	/**
	 * 每帧最多花多少毫秒推进编辑。4 ms 在 60 fps 下约把一帧从 16.7 ms 抬到
	 * 20 ms，手感仍是流畅的；调大 = 更快看到结果、帧率更差。
	 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "0.1"))
	float ProgressiveEditBudgetMs = 4.f;

	/** 待处理编辑最高笔数。超了丢下标 1 的（队首正在做，不能丢）。 */
	UPROPERTY(EditAnywhere, Category = "PlanetEdit", meta = (ClampMin = "1"))
	int32 MaxQueuedEdits = 16;
```

私有成员与函数：

```cpp
	TArray<TSharedPtr<FPlanetProgressiveEdit>> EditQueue;
	void TickProgressiveEdits();
	void OnEditFinished(FPlanetProgressiveEdit& Edit);
	/** 把队列里所有编辑一次做完、刷渲染。退出编辑 / EndPlay / 换星前调用。 */
	void FinishAllEdits();
```

- [ ] **Step 2: 实现**

- `ApplyBrushAtScreenPosition`：解析目标 → 算 `Center` → `MakeShared` → `Setup` →
  入队（超限丢下标 1）→ 返回 true。返回值语义从「有没有改到体素」变成
  「有没有接受这一笔」—— 控制器本来就不消费它
  （`StellarEraPlayerController.cpp:1119` 的注释写明不用它判断归属）。
- `TickComponent`：`UpdateBrushPreview()` 之后调 `TickProgressiveEdits()`，
  再走原有的连挖节流。渐进编辑不能挂在 `bContinuousDigging` 下面。
- `TickProgressiveEdits`：每帧**至少推进一块**（否则预算再小也永远不动），
  累计超过预算就停；每块应用到就把 `GetAppliedBounds` 并进 `PendingRenderBounds`
  并**当帧** `FlushPendingRenderUpdate()`，洞因此逐帧向外长。
- `OnEditFinished`：`DidChangeAnything()` 为真时更新 `LastEditedPlanet` 并
  `NotifyVoxelEdited()`。
- 安全点：`SetEditMode(false)` 开头、`EndPlay` 开头、`SetGridPlanet` 换星前
  调 `FinishAllEdits()`。

- [ ] **Step 3: 构建 + 全量测试**
- [ ] **Step 4: 提交**

---

## Task 5: 构建与验证

- [ ] **Step 1: 构建**

```bash
cd "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64"
./dotnet.exe "../../../../DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

Expected: `Result: Succeeded`

- [ ] **Step 2: 全量自动化测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/progressive-all.log"
```

Expected: 除既有 `StellarEra.Planet.Geology.EarthPresetValues` 失败外全绿，
并新增两条 `StellarEra.Planet.Edit.Progressive*` 通过。

- [ ] **Step 3: PIE 手感**（`UniverseMap`，需用户在编辑器前台运行）
  观察 `[Dig]` 日志与帧率；大笔刷应看到洞逐帧向外长。
