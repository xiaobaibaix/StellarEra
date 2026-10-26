# 行星矿物分布生成器 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把「所有元素共用一种矿囊」改成「每个元素可叠加多个分布生成器（囊状 / 片层 / 脉状 / 浸染），逐点取最大值」；旧资产自动迁移，旧行星形状逐位不变。

**Architecture:** 编辑器侧是多态 `UPlanetGeologyDistribution` 子类，运行期是各自独立的 `FPlanetGeologyDistributionRuntime`（自带求值、区域枚举、空间索引）。`FPlanetGeologyField` 只负责「元素内各生成器取 max」；观测视图从「矿囊列表」改成「有界区域列表」，用空间哈希分簇。

**Tech Stack:** Unreal Engine 5.8.3、C++20、VoxelFree（第三方，勿改）、UE Automation Test、Git LFS

**Spec:** `docs/superpowers/specs/2026-10-26-planet-geology-distributions-design.md`

## Global Constraints

- 模块分层不变：`StellarEraGeology` 仍只依赖 Core / CoreUObject / Engine / Voxel / AssetRegistry；`StellarEraMinerals → StellarEraGeology`。不得新增模块依赖。
- 新生成器类全部放在 `StellarEraGeology`；自定义代码不进 `Plugins/`。
- 每个生成器子类的 `UCLASS` 必须**各自**写 `EditInlineNew`：`CLASS_EditInlineNew` 只在 `CLASS_ScriptInherit` 里
  （`ObjectMacros.h:302`），原生 C++ 子类不继承基类的这个标志，而属性面板的 `+` 类选择器要求候选类自身带它
  （`SPropertyEditorEditInline.cpp:73`）。漏了就是「`+` 不弹选择器」。
- 新写的 `.h` / `.cpp` 若含中文，必须存为 **UTF-8 with BOM**；不要统一换行符（仓库 CRLF/LF 混合）。
- 元素运行期结构 `FPlanetGeologyElement` 不再有 `Pods / PodRadius / ShapeDetail / Octaves / Lacunarity / Gain / Seed / VeinThreshold`；`FPlanetGeologyPod` 保留给囊状生成器。
- 合成语义固定为 **max**，不是相加；`Abundance` 只影响颜色，不改几何。
- 所有归一化空间距离以行星基准半径 = 1 计；观测视图厘米换算仍只乘 `SurfaceRadiusCm`。
- `BuildFromPreset` 必须 `check(IsInGameThread())`；运行期对象不持有 UObject 引用。
- 观测视图两条硬上限不变：每轴 ≤ 512 格、单簇单元素 ≤ 8'000'000 角点；触发时放大格边、绝不跳过簇。
- 构建前关闭编辑器（Live Coding 锁 DLL）。只用捆绑 dotnet 直调 UBT，cwd 在项目外，禁止 `Build.bat`：

```bash
cd "C:/" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

- 自动化测试（编辑器开着也能跑）：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests <前缀>" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_Test.log"
```

- 提交时 pathspec 必须挂在 commit 上（索引里有常驻暂存文件）：

```bash
git add -- <文件...> && git commit -m "<消息>" -- <文件...>
```

- 每个新文件写完都执行一次 BOM 兜底（幂等）：

```bash
for f in <文件...>; do
  [ "$(head -c3 "$f" | od -An -tx1 | tr -d ' \n')" = "efbbbf" ] || {
    printf '\xEF\xBB\xBF' | cat - "$f" > "$f.tmp" && mv "$f.tmp" "$f";
  }
done
```

---

## 文件结构

**新增（`Source/StellarEraGeology/`）**

| 文件 | 职责 |
|---|---|
| `Public/PlanetGeologyDistribution.h` / `Private/PlanetGeologyDistribution.cpp` | 抽象 UObject 基类、`FPlanetGeologyRegion`、运行期接口、噪声、放置与索引工具 |
| `Public/PlanetGeologySpherePodsDistribution.h` / `.cpp` | 囊状生成器（迁移目标） |
| `Public/PlanetGeologyLensDistribution.h` / `.cpp` | 片层 / 透镜 |
| `Public/PlanetGeologyVeinDistribution.h` / `.cpp` | 脉状 / 折线骨架 |
| `Public/PlanetGeologyDisseminatedDistribution.h` / `.cpp` | 浸染 / 稀疏散布 |
| `Private/PlanetGeologyDistributionTests.cpp` | 新增测试（噪声、种子、合成、各形态） |

**修改**

| 文件 | 改动 |
|---|---|
| `Source/StellarEraGeology/Public/PlanetGeologyTypes.h` | 元素运行期字段替换；保留 `FPlanetGeologyPod` |
| `Source/StellarEraGeology/Public/PlanetGeologyPreset.h` / `Private/PlanetGeologyPreset.cpp` | `Distributions` 数组、`NotifyContentChanged`、`PostLoad` 迁移、`FillEarthPreset` |
| `Source/StellarEraGeology/Public/PlanetGeologyField.h` / `Private/PlanetGeologyField.cpp` | max 合成、`EnumerateRegions`、删全局 `PodIndex` |
| `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp` | 夹具切到显式囊状生成器、测试改名 |
| `Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp` | 默认值、迁移、地球初始数据 |
| `Source/StellarEraMinerals/Public/PlanetMineralSurface.h` / `Private/PlanetMineralSurface.cpp` | 区域化、`ClusterRegionBounds`、`CellsPerFeature` |
| `Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h` / `Private/PlanetMineralViewComponent.cpp` | 字段改名 |
| `Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp` / `PlanetMineralViewTests.cpp` | 改名 + 新测试 |
| `Config/DefaultEngine.ini` | `CellsPerPod → CellsPerFeature` 的 CoreRedirect |
| `Content/Geology/README.md`、`CLAUDE.md` | 文档 |

---

## Task 1: 分布框架（区域 / 运行期接口 / 噪声 / 种子）

**Files:**
- Create: `Source/StellarEraGeology/Public/PlanetGeologyDistribution.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyPreset.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp`

**Interfaces:**
- Consumes: `FVoxelFastNoise`（Voxel 模块）、`FPropertyChangedEvent`（WITH_EDITOR）。
- Produces:
  - `struct FPlanetGeologyRegion { FBox Bounds; float CharacteristicRadius; };`
  - `class FPlanetGeologyDistributionRuntime`（5 个纯虚 + 2 个诊断虚函数 + `GetSpherePods`）
  - `struct FPlanetGeologyDistributionNoise`（`Init` / `EvaluateAt` / `EvaluateVein`）
  - `FVector SamplePlanetGeologyPlacement(FRandomStream&, float, float, float)`
  - `FIntVector PlanetGeologyToCell(const FVector&, double)`
  - `void PlanetGeologyIndexBounds(const FBox&, double, TMap<FIntVector, TArray<int32>>&, int32, int32&)`
  - `UPlanetGeologyDistribution`（`Seed / ShapeDetail / Octaves / Lacunarity / Gain / VeinThreshold / bSeedExplicit`、`ResolveSeed`、纯虚 `BuildRuntime`、编辑器通知）
  - `UPlanetGeologyPreset::NotifyContentChanged()`（嵌套子对象编辑时通知外层）

- [ ] **Step 1: 写失败测试**

创建 `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 分布生成器框架：噪声范围与确定性。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyPreset.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyPreset* MakePreset()
	{
		return NewObject<UPlanetGeologyPreset>();
	}

	FPlanetGeologyElementParams& AddElement(UPlanetGeologyPreset& Preset, FName Id)
	{
		FPlanetGeologyElementParams& Element = Preset.Elements.AddDefaulted_GetRef();
		Element.Id = Id;
		Element.DisplayName = Id.ToString();
		// 显式生成器路径：不让旧 PodCount 触发运行期兜底。
		Element.PodCount = 0;
		return Element;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionNoiseTest,
	"StellarEra.Planet.Geology.Distribution.Noise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionNoiseTest::RunTest(const FString& Parameters)
{
	FPlanetGeologyDistributionNoise Noise;
	Noise.Init(1234, 0.05f, 3.f, 2, 2.f, 0.5f, 0.f);

	FRandomStream Rng(7);
	const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
		Rng.FRandRange(-1.f, 1.f));

	const float A = Noise.EvaluateVein(P);
	const float B = Noise.EvaluateVein(P);
	TestEqual(TEXT("同参数同点确定"), A, B);
	TestTrue(*FString::Printf(TEXT("范围 [0,1]（%.4f）"), A), A >= 0.f && A <= 1.f);

	// 片层拿原始噪声做形变，必须独立于阈值可读。入参是噪声空间坐标，
	// 所以这里自己乘一个任意缩放（7.5），与 EvaluateVein 内部的折频无关。
	const FVector NoiseSpacePoint = P * 7.5f;
	const float Raw = Noise.EvaluateAt(NoiseSpacePoint);
	TestTrue(*FString::Printf(TEXT("EvaluateAt 范围 [0,1]（%.4f）"), Raw),
		Raw >= 0.f && Raw <= 1.f);
	TestEqual(TEXT("EvaluateAt 确定"), Noise.EvaluateAt(NoiseSpacePoint), Raw);

	FPlanetGeologyDistributionNoise Thresholded;
	Thresholded.Init(1234, 0.05f, 3.f, 2, 2.f, 0.5f, 0.9f);
	TestEqual(TEXT("阈值不改 EvaluateAt"), Thresholded.EvaluateAt(NoiseSpacePoint), Raw);
	TestTrue(TEXT("阈值只压低 EvaluateVein"), Thresholded.EvaluateVein(P) <= A);

	FPlanetGeologyDistributionNoise Other;
	Other.Init(4321, 0.05f, 3.f, 2, 2.f, 0.5f, 0.f);
	int32 Differences = 0;
	for (int32 Index = 0; Index < 64; ++Index)
	{
		const FVector Sample(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (!FMath::IsNearlyEqual(Noise.EvaluateVein(Sample), Other.EvaluateVein(Sample), 1e-6f))
		{
			++Differences;
		}
	}
	TestTrue(*FString::Printf(TEXT("换种子至少一个采样点不同（%d/64）"), Differences),
		Differences > 0);

	FPlanetGeologyDistributionNoise Invalid;
	Invalid.Init(1, 0.f, 3.f, 2, 2.f, 0.5f, 0.f);
	TestEqual(TEXT("特征半径为 0 时恒 0"), Invalid.EvaluateVein(P), 0.f);

	return true;
}

#endif
```

- [ ] **Step 2: 构建，确认失败**

运行 Global Constraints 的构建命令。预期：`PlanetGeologyDistribution.h` 不存在，编译失败。

- [ ] **Step 3: 写最小实现**

创建 `Public/PlanetGeologyDistribution.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 分布生成器：可编辑的多态 UObject 基类 + 运行期不可变实现。

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/VoxelFastNoise.h"
#include "Math/RandomStream.h"
#include "PlanetGeologyDistribution.generated.h"

class UPlanetGeologyPreset;
struct FPlanetGeologyPod;

/** 观测视图用的有界区域（归一化空间，行星基准半径 = 1）。 */
struct FPlanetGeologyRegion
{
	FBox Bounds = FBox(ForceInit);
	float CharacteristicRadius = 0.f;
};

/** 运行期基类：不可变、不持有 UObject 引用，可跨线程共享。 */
class STELLARERAGEOLOGY_API FPlanetGeologyDistributionRuntime
{
public:
	virtual ~FPlanetGeologyDistributionRuntime() = default;

	/** 该元素在某归一化点的强度 ∈ [0,1]。 */
	virtual float EvaluateNormalized(const FVector& P) const = 0;

	/** 绕过加速结构遍历全部基元；测试 oracle。 */
	virtual float EvaluateBruteForce(const FVector& P) const = 0;

	virtual void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const = 0;
	virtual bool IsEmpty() const = 0;
	virtual int32 NumPrimitives() const = 0;

	/** 建场日志用。 */
	virtual const TCHAR* GetDebugName() const { return TEXT("Unknown"); }
	virtual FString GetDebugSummary() const { return FString(); }

	/** 诊断 / 兼容：囊状生成器的矿囊表；其他形态返回空。 */
	virtual const TArray<FPlanetGeologyPod>& GetSpherePods() const;
};

/** 每个生成器独立一套噪声；构造后只读。 */
struct STELLARERAGEOLOGY_API FPlanetGeologyDistributionNoise
{
	void Init(int32 InSeed, float InFeatureRadius, float InShapeDetail, int32 InOctaves,
		float InLacunarity, float InGain, float InVeinThreshold);

	/**
	 * 原始分形噪声，[0,1]，**不做阈值、也不再缩放**：入参已经是噪声空间坐标，
	 * 缩放由调用方按自己的特征尺度做（`EvaluateVein` 用 ShapeDetail 折频；
	 * 片层的域扭曲用 `Local / 厚度 × WarpScale`）。
	 */
	float EvaluateAt(const FVector& NoiseSpacePoint) const;

	/** 阈值稀疏后的 [0,1]；FeatureRadius < 1e-4 时恒 0（半径趋零会把坐标缩放炸掉）。 */
	float EvaluateVein(const FVector& P) const;

	float FeatureRadius = 0.f;
	float ShapeDetail = 3.f;
	/** Init 会把它烘进 FVoxelFastNoise 的 FractalBounding；改它必须重新 Init，否则分形不再归一化。 */
	int32 Octaves = 2;
	float VeinThreshold = 0.f;

private:
	FVoxelFastNoise Noise;
};

/**
 * 带权深度放置：返回归一化球内的位置（|P| ≤ 1）。
 *
 * 随机流顺序与旧 PlacePods 逐位一致（方向 → 初始半径 → 可选拒绝采样循环），
 * 迁移保真测试依赖这一点 —— 所以**调用方必须先抽半径抖动**（旧 PlacePods 的第一步），
 * 少抽一次就整体错位。
 */
STELLARERAGEOLOGY_API FVector SamplePlanetGeologyPlacement(FRandomStream& Rng,
	float DepthBias, float DepthCenter, float DepthSpread);

/** 归一化点 → 格坐标（负坐标用 floor，不能用截断）。 */
STELLARERAGEOLOGY_API FIntVector PlanetGeologyToCell(const FVector& Point, double CellSize);

/** 把包围盒覆盖的格子写入索引；InOutReferenceCount 累加引用数。 */
STELLARERAGEOLOGY_API void PlanetGeologyIndexBounds(const FBox& Bounds, double CellSize,
	TMap<FIntVector, TArray<int32>>& Index, int32 PrimitiveIndex, int32& InOutReferenceCount);

struct FPlanetGeologyDistributionBuildContext
{
	FName ElementId;
	int32 DistributionIndex = 0;
};

/**
 * 分布生成器基类。子类只实现 BuildRuntime；噪声参数归实例所有。
 *
 * 嵌套编辑通知：编辑 Instanced 子对象时引擎不会调用外层预设的
 * PostEditChangeProperty，所以基类主动找外层并广播（见 .cpp）。
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, BlueprintType)
class STELLARERAGEOLOGY_API UPlanetGeologyDistribution : public UObject
{
	GENERATED_BODY()

public:
	/** 0 = 按「元素 Id + 生成器下标 + 类名」派生；非 0 直接使用。 */
	UPROPERTY(EditAnywhere, Category = "Noise")
	int32 Seed = 0;

	/** 一个特征尺度内大约几个噪声周期。观测视图要求 ShapeDetail ≤ CellsPerFeature / 2。 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "1.0"))
	float ShapeDetail = 3.0f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "1", ClampMax = "6"))
	int32 Octaves = 2;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "1.0"))
	float Lacunarity = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Gain = 0.5f;

	/** 噪声稀疏阈值；越高越破碎。0 = 不稀疏。 */
	UPROPERTY(EditAnywhere, Category = "Noise", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float VeinThreshold = 0.5f;

	/**
	 * 迁移专用：Seed 即使为 0 也按字面值使用。
	 * 旧公式 `Hash(Id) & 0x7fffffff` 理论上可能得到 0，普通 Seed=0 又会触发派生，
	 * 所以迁移数据用这个位标记「不要再派生」。
	 */
	UPROPERTY()
	bool bSeedExplicit = false;

	/** 解析后的种子。新生成器按 spec §2.3 派生；迁移数据走 bSeedExplicit。 */
	int32 ResolveSeed(FName ElementId, int32 DistributionIndex) const;

	// 不能写 `= 0`：UHT 的 GENERATED_BODY() 会实例化 UCLASS，纯虚声明让 gen.cpp 编不过（C2259）。
	// PURE_VIRTUAL 是 UE 的等价写法：UHT 可实例化，漏实现则在调用点 fatal。
	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const
		PURE_VIRTUAL(UPlanetGeologyDistribution::BuildRuntime, return nullptr;);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
```

创建 `Private/PlanetGeologyDistribution.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyDistribution.h"

#include "FastNoise/VoxelFastNoise.inl"
#include "PlanetGeologyPreset.h"
#include "PlanetGeologyTypes.h"

const TArray<FPlanetGeologyPod>& FPlanetGeologyDistributionRuntime::GetSpherePods() const
{
	static const TArray<FPlanetGeologyPod> Empty;
	return Empty;
}

void FPlanetGeologyDistributionNoise::Init(const int32 InSeed, const float InFeatureRadius,
	const float InShapeDetail, const int32 InOctaves, const float InLacunarity,
	const float InGain, const float InVeinThreshold)
{
	// 不在这里抬到 1e-4：抬了会让「特征半径为 0 时恒 0」那条测试失效。
	// 极小半径的保护放在 EvaluateVein 里。
	FeatureRadius = FMath::Max(InFeatureRadius, 0.f);
	ShapeDetail = FMath::Max(InShapeDetail, 1.f);
	Octaves = FMath::Clamp(InOctaves, 1, 6);
	VeinThreshold = FMath::Clamp(InVeinThreshold, 0.f, 0.95f);

	Noise.SetSeed(InSeed);
	Noise.SetFractalLacunarity(FMath::Max(InLacunarity, 1.f));
	// FractalBounding 只算一次；采样时的 octaves 必须与此处完全一致。
	Noise.SetFractalOctavesAndGain(Octaves, FMath::Clamp(InGain, 0.f, 1.f));
}

float FPlanetGeologyDistributionNoise::EvaluateAt(const FVector& NoiseSpacePoint) const
{
	const double RawNoise = double(Noise.GetPerlinFractal_3D(
		v_flt(double(NoiseSpacePoint.X)),
		v_flt(double(NoiseSpacePoint.Y)),
		v_flt(double(NoiseSpacePoint.Z)),
		v_flt(1.f), Octaves));

	// Perlin 输出 [-1,1]，映射到 [0,1] 才是「含量」。
	return float(FMath::Clamp(0.5 * (RawNoise + 1.0), 0.0, 1.0));
}

float FPlanetGeologyDistributionNoise::EvaluateVein(const FVector& P) const
{
	// 1e-4 与旧实现的 Max(PodRadius, 1e-4) 对齐：半径趋近 0 时 Scale 会炸成天文数字，
	// 噪声坐标失去意义。小于它一律当 0（旧路径上这种囊本身也不产出）。
	if (FeatureRadius < 1e-4f)
	{
		return 0.f;
	}

	// 频率折叠进坐标：ShapeDetail 是「每特征直径的周期数」，直径 = 2r。
	// 用 double 乘（FVector 分量本来就是 double），不要先把 Scale 窄化成 float。
	const double Scale = double(ShapeDetail) / (2.0 * double(FeatureRadius));
	const double Noise01 = double(EvaluateAt(P * Scale));
	if (VeinThreshold <= 0.0)
	{
		return float(Noise01);
	}
	return float(FMath::Clamp(
		(Noise01 - double(VeinThreshold)) / (1.0 - double(VeinThreshold)), 0.0, 1.0));
}

FVector SamplePlanetGeologyPlacement(FRandomStream& Rng, const float InDepthBias,
	const float InDepthCenter, const float InDepthSpread)
{
	const double DepthBias = FMath::Clamp(double(InDepthBias), 0.0, 0.9);
	const double DepthCenter = FMath::Clamp(double(InDepthCenter), 0.0, 1.0);
	const double DepthSpread = FMath::Max(double(InDepthSpread), 0.01);

	// 球面均匀方向；径向距离另抽，两者独立。
	const FVector Direction = Rng.GetUnitVector();

	// ∛U 让**体积**均匀（近地表体积大，基元自然更多）。
	double RadiusRatio = FMath::Pow(double(Rng.FRand()), 1.0 / 3.0);

	// 作者参数里的 DepthCenter 是「深度」（0 = 地表，1 = 球心），与半径比例
	// 互为补角 —— 带权时必须先换算成 1 - r 再比。
	if (DepthBias > 0.0)
	{
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

	return Direction * float(RadiusRatio);
}

FIntVector PlanetGeologyToCell(const FVector& Point, const double CellSize)
{
	// CellSize ≤ 0 会让除法出 inf，FloorToInt 是 UB，后面的格子循环还会跑飞。
	check(CellSize > 0.0);
	return FIntVector(
		FMath::FloorToInt(Point.X / CellSize),
		FMath::FloorToInt(Point.Y / CellSize),
		FMath::FloorToInt(Point.Z / CellSize));
}

void PlanetGeologyIndexBounds(const FBox& Bounds, const double CellSize,
	TMap<FIntVector, TArray<int32>>& Index, const int32 PrimitiveIndex,
	int32& InOutReferenceCount)
{
	const FIntVector MinCell = PlanetGeologyToCell(Bounds.Min, CellSize);
	const FIntVector MaxCell = PlanetGeologyToCell(Bounds.Max, CellSize);
	for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
	{
		for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
		{
			for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
			{
				Index.FindOrAdd(FIntVector(X, Y, Z)).Add(PrimitiveIndex);
				++InOutReferenceCount;
			}
		}
	}
}

int32 UPlanetGeologyDistribution::ResolveSeed(const FName ElementId,
	const int32 DistributionIndex) const
{
	if (bSeedExplicit || Seed != 0)
	{
		return Seed;
	}
	// 必须哈希字符串：GetTypeHash(FName) 取的是名字池下标，跨进程不稳定，
	// 编辑器与打包版会长出不同的矿。GetTypeHash(FString) 走 FCrc::Strihash，是确定性的。
	const uint32 Mixed = GetTypeHash(ElementId.ToString())
		^ (uint32(DistributionIndex) * 0x9E3779B1u)
		^ GetTypeHash(GetClass()->GetFName().ToString());
	return int32(Mixed & 0x7fffffffu);
}

#if WITH_EDITOR
void UPlanetGeologyDistribution::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (UPlanetGeologyPreset* Preset = GetTypedOuter<UPlanetGeologyPreset>())
	{
		// 漏了这条会同时坏两件事：观测预览不自动重建、地形缓存键的 Revision 不变。
		Preset->NotifyContentChanged();
	}
}
#endif
```

- [ ] **Step 4: 预设加嵌套编辑通知**

`UPlanetGeologyDistribution::PostEditChangeProperty` 调用 `Preset->NotifyContentChanged()`，
而这个函数现在还不存在 —— 本步补上，否则 Task 1 编译不过。

`Public/PlanetGeologyPreset.h`：在 `GetRevision()` 之后插入：

```cpp
	/**
	 * 内容变了：修订号自增，编辑器里再广播一次。
	 *
	 * 嵌套子对象（分布生成器）的 PostEditChangeProperty 也走这里 —— 引擎不会替
	 * 子对象通知外层预设。少这一次通知，观测预览不重建、地形缓存键也不变。
	 */
	void NotifyContentChanged();
```

`Private/PlanetGeologyPreset.cpp`：把现有 `PostEditChangeProperty` 的自增+广播抽出来，
两处共用一份：

```cpp
void UPlanetGeologyPreset::NotifyContentChanged()
{
	++Revision;
#if WITH_EDITOR
	OnPresetChanged.Broadcast(this);
#endif
}

#if WITH_EDITOR
void UPlanetGeologyPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	NotifyContentChanged();
}
#endif
```

- [ ] **Step 5: 加 BOM**

运行 Global Constraints 的 BOM 循环，文件列表：

```bash
Source/StellarEraGeology/Public/PlanetGeologyDistribution.h
Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp
Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
Source/StellarEraGeology/Public/PlanetGeologyPreset.h
Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp
```

- [ ] **Step 6: 构建并跑测试**

构建后运行：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geology.Distribution.Noise" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_Noise.log"
```

预期：`1` 个测试通过。

- [ ] **Step 7: 提交**

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp
git commit -m "feat(地质): 分布生成器基类、区域契约与噪声" -- \
  Source/StellarEraGeology/Public/PlanetGeologyDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp
```

---

## Task 2: 囊状生成器 + 元素运行期形态 + 场的 max 合成

**Files:**
- Create: `Source/StellarEraGeology/Public/PlanetGeologySpherePodsDistribution.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologySpherePodsDistribution.cpp`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyTypes.h`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyField.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyField.cpp`
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyPreset.h`
- Modify: `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp:198-200`（仅改名）
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `UPlanetGeologyDistribution`、`FPlanetGeologyDistributionRuntime`、`FPlanetGeologyDistributionNoise`、`SamplePlanetGeologyPlacement`、`PlanetGeologyIndexBounds`。
- Produces:
  - `UPlanetGeologySpherePodsDistribution`（`PodCount / PodRadius / PodRadiusJitter / PodDepthBias / PodDepthCenter / PodDepthSpread`、`BuildRuntime`、`MakeRuntimeFromLegacyParams`）
  - `FPlanetGeologyElement::Distributions`（`TArray<TSharedPtr<const FPlanetGeologyDistributionRuntime>>`）
  - `FPlanetGeologyField::EnumerateRegions(int32, TArray<FPlanetGeologyRegion>&)`
  - `FPlanetGeologyElementParams::Distributions`（`TArray<TObjectPtr<UPlanetGeologyDistribution>>`）

- [ ] **Step 0（人工门）: 验 `Instanced` 数组的子类选择器**

**执行顺序：先做完 Step 1–8，再做这一步，过了这一步才允许开工 Task 3。**
spec §13 把它列为「失败则换方案」的第一风险，越早验越便宜。

1. 编辑器打开 `DA_Geology_Earth`（此时资产还是旧数据，六个元素都没有生成器）。
2. 展开 `Elements[0]` 的 `Distributions`，点 `+`。

预期：弹出子类选择列表，能看到 `PlanetGeologySpherePodsDistribution`（本任务只有囊状
一个可选项，其余三个随 Task 3–5 出现）。

**失败的表现**：`+` 直接加了一个空对象、或者根本不弹选择器。那就停下来改方案
（每个元素一个 `UObject` 容器子对象），**不要**继续往下写 Task 3–5。这一步需要人操作
（本会话的编辑器 MCP 端点连不上），把结论回报给任务执行者再继续。

- [ ] **Step 1: 写失败测试**

在 `PlanetGeologyDistributionTests.cpp` 的 `#endif` 之前追加：

```cpp
namespace PlanetGeologyDistributionTest
{
	UPlanetGeologySpherePodsDistribution* AddSpherePods(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element, int32 PodCount, float PodRadius, int32 Seed)
	{
		UPlanetGeologySpherePodsDistribution* Distribution =
			NewObject<UPlanetGeologySpherePodsDistribution>(&Preset);
		Distribution->PodCount = PodCount;
		Distribution->PodRadius = PodRadius;
		Distribution->Seed = Seed;
		// 刻意不设 bSeedExplicit：Seed=0 必须走派生，否则 SeedDerivation 的前两段
		// 会两条都锁死成种子 0，囊表相同，测试变成永真。
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	TSharedPtr<const FPlanetGeologyField> MakeField(const UPlanetGeologyPreset& Preset)
	{
		return FPlanetGeologyField::BuildFromPreset(Preset, 40000.0);
	}

	UPlanetGeologyPreset* MakeSingleElementPreset(const TCHAR* Id, bool bAddA, bool bAddB)
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, Id);
		if (bAddA)
		{
			AddSpherePods(*Preset, Element, 8, 0.06f, 111);
		}
		if (bAddB)
		{
			AddSpherePods(*Preset, Element, 5, 0.09f, 222);
		}
		return Preset;
	}

	bool AreRegionsEqual(const TArray<FPlanetGeologyRegion>& A, const TArray<FPlanetGeologyRegion>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (!A[Index].Bounds.Equals(B[Index].Bounds)
				|| A[Index].CharacteristicRadius != B[Index].CharacteristicRadius)
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionMaxCompositionTest,
	"StellarEra.Planet.Geology.Distribution.MaxComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionMaxCompositionTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* OnlyA = MakeSingleElementPreset(TEXT("iron"), true, false);
	UPlanetGeologyPreset* OnlyB = MakeSingleElementPreset(TEXT("iron"), false, true);
	UPlanetGeologyPreset* Both = MakeSingleElementPreset(TEXT("iron"), true, true);

	const auto FieldA = MakeField(*OnlyA);
	const auto FieldB = MakeField(*OnlyB);
	const auto FieldBoth = MakeField(*Both);
	if (!TestTrue(TEXT("三个场都建成功"),
		FieldA.IsValid() && FieldB.IsValid() && FieldBoth.IsValid()))
	{
		return false;
	}

	FRandomStream Rng(4321);
	int32 Mismatches = 0;
	int32 NonZeroSamples = 0;
	for (int32 SampleIndex = 0; SampleIndex < 3000; ++SampleIndex)
	{
		const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (P.SizeSquared() > 1.f)
		{
			continue;
		}
		const float A = FieldA->SampleElementNormAtNormalized(0, P);
		const float B = FieldB->SampleElementNormAtNormalized(0, P);
		const float BothNorm = FieldBoth->SampleElementNormAtNormalized(0, P);

		if (BothNorm > 0.f)
		{
			++NonZeroSamples;
		}
		if (!FMath::IsNearlyEqual(BothNorm, FMath::Max(A, B), 1e-6f))
		{
			++Mismatches;
		}
		// 两个生成器都为 0 的地方合成后必须还是 0（防止实现写成「加一点底噪」）。
		if (A == 0.f && B == 0.f && BothNorm != 0.f)
		{
			++Mismatches;
		}
	}

	TestEqual(TEXT("合成结果逐点等于两生成器的 max"), Mismatches, 0);
	TestTrue(*FString::Printf(TEXT("夹具确实采到过非零点（%d）"), NonZeroSamples),
		NonZeroSamples > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionSeedTest,
	"StellarEra.Planet.Geology.Distribution.SeedDerivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionSeedTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	// 1) Seed=0：不同元素派生出不同的囊表。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Alpha = AddElement(*Preset, TEXT("alpha"));
		FPlanetGeologyElementParams& Beta = AddElement(*Preset, TEXT("beta"));
		AddSpherePods(*Preset, Alpha, 6, 0.05f, 0);
		AddSpherePods(*Preset, Beta, 6, 0.05f, 0);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> RegionsAlpha;
		TArray<FPlanetGeologyRegion> RegionsBeta;
		Field->EnumerateRegions(0, RegionsAlpha);
		Field->EnumerateRegions(1, RegionsBeta);
		TestEqual(TEXT("两个元素各 6 个区域"), RegionsAlpha.Num(), 6);
		TestTrue(TEXT("不同元素 Seed=0 派生出的囊表不同"),
			!AreRegionsEqual(RegionsAlpha, RegionsBeta));
	}

	// 2) Seed=0：同一元素的两个生成器互不相同（下标参与派生）。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("alpha"));
		AddSpherePods(*Preset, Element, 6, 0.05f, 0);
		AddSpherePods(*Preset, Element, 6, 0.05f, 0);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> Regions;
		Field->EnumerateRegions(0, Regions);
		if (!TestEqual(TEXT("两个生成器共 12 个区域"), Regions.Num(), 12))
		{
			return false;
		}
		const TArray<FPlanetGeologyRegion> First(Regions.GetData(), 6);
		const TArray<FPlanetGeologyRegion> Second(Regions.GetData() + 6, 6);
		TestTrue(TEXT("同元素两个生成器 Seed=0 派生出的囊表不同"),
			!AreRegionsEqual(First, Second));
	}

	// 3) 非 0 Seed 被尊重：两个生成器同种子 + bSeedExplicit 必须逐位相同。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("alpha"));
		AddSpherePods(*Preset, Element, 6, 0.05f, 999);
		AddSpherePods(*Preset, Element, 6, 0.05f, 999);

		const auto Field = MakeField(*Preset);
		TArray<FPlanetGeologyRegion> Regions;
		Field->EnumerateRegions(0, Regions);
		if (!TestEqual(TEXT("两个生成器共 12 个区域"), Regions.Num(), 12))
		{
			return false;
		}
		const TArray<FPlanetGeologyRegion> First(Regions.GetData(), 6);
		const TArray<FPlanetGeologyRegion> Second(Regions.GetData() + 6, 6);
		TestTrue(TEXT("同种子两次生成逐位相同"), AreRegionsEqual(First, Second));
	}

	// 4) 跨进程稳定：把派生结果钉成字面量。
	// GetTypeHash(FName) 取的是名字池下标，换个进程就可能变；字符串哈希（FCrc::Strihash）才是确定的。
	// 这条字面量让「悄悄改回 FName 哈希」立刻变成失败。首次实现时先写成 0，
	// 从失败信息读出实际值再填回来 —— 不要反过来先算出值再写测试。
	{
		UPlanetGeologyPreset* Preset = MakePreset();
		UPlanetGeologySpherePodsDistribution* Distribution =
			NewObject<UPlanetGeologySpherePodsDistribution>(Preset);
		Distribution->Seed = 0;
		TestEqual(TEXT("ResolveSeed 对 iron/生成器 3 的派生值是钉住的字面量"),
			Distribution->ResolveSeed(TEXT("iron"), 3), 0 /* ← 首跑后换成实际值 */);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDistributionEmptyTest,
	"StellarEra.Planet.Geology.Distribution.EmptySemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDistributionEmptyTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	TestTrue(TEXT("没有元素 → 空场"), MakeField(*MakePreset())->IsEmpty());

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		AddElement(*Preset, TEXT("iron")); // AddElement 已把遗留 PodCount 置 0
		TestTrue(TEXT("既无生成器也无遗留矿囊 → 空场"), MakeField(*Preset)->IsEmpty());
	}

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
		AddSpherePods(*Preset, Element, 0, 0.05f, 1);
		TestTrue(TEXT("唯一生成器基元数为 0 → 空场"), MakeField(*Preset)->IsEmpty());
	}

	{
		UPlanetGeologyPreset* Preset = MakePreset();
		FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
		AddSpherePods(*Preset, Element, 4, 0.05f, 1);
		TestFalse(TEXT("有基元 → 非空场"), MakeField(*Preset)->IsEmpty());
	}

	return true;
}
```

`AddSpherePods` 依赖 `PlanetGeologySpherePodsDistribution.h` 与 `PlanetGeologyField.h`，在文件顶部 include 区补两行：

```cpp
#include "PlanetGeologyField.h"
#include "PlanetGeologySpherePodsDistribution.h"
```

- [ ] **Step 2: 构建，确认失败**

预期：`PlanetGeologySpherePodsDistribution.h` 不存在、`EnumerateRegions` / `Distributions` 不存在，编译失败。

- [ ] **Step 3: 写囊状生成器**

创建 `Public/PlanetGeologySpherePodsDistribution.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 囊状分布：最朴素的球团，旧矿囊模型的直接搬迁，也是旧资产的迁移目标。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologySpherePodsDistribution.generated.h"

/**
 * 球团分布。
 *
 * 参数名保留 Pod 前缀（spec §4.4「参数与语义完全沿用」）：迁移时逐字段照抄旧值，
 * 名字一改就又多一个漂移点。
 */
UCLASS(DisplayName = "囊状（球团）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologySpherePodsDistribution
	: public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	/** 撒多少个矿囊。0 = 这个生成器不贡献任何矿。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0", ClampMax = "512"))
	int32 PodCount = 16;

	/** 矿囊名义半径（占行星基准半径的比例）。重叠对数按 PodCount² × (2r)³ 增长。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0.001", ClampMax = "0.25"))
	float PodRadius = 0.05f;

	/** 每囊半径的随机抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, Category = "Pods", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodRadiusJitter = 0.5f;

	/** 深度偏好强度。0 = 全深度体积均匀。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PodDepthBias = 0.0f;

	/** 偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PodDepthCenter = 0.25f;

	/** 偏好深度的高斯宽度。 */
	UPROPERTY(EditAnywhere, Category = "Pods|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float PodDepthSpread = 0.15f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;

	/**
	 * 用旧元素字段直接造运行期，**不建 UObject**。
	 *
	 * 供 BuildFromPreset 的遗留兜底使用：种子由调用方按旧公式
	 * `Seed != 0 ? Seed : Hash(Id) & 0x7fffffff` 算好后传入。
	 * 这里是「旧行星形状逐位不变」的唯一实现，迁移与兜底都收敛到它。
	 */
	static TSharedPtr<const FPlanetGeologyDistributionRuntime> MakeRuntimeFromLegacyParams(
		int32 InPodCount, float InPodRadius, float InPodRadiusJitter,
		float InPodDepthBias, float InPodDepthCenter, float InPodDepthSpread,
		int32 InSeed, float InShapeDetail, int32 InOctaves, float InLacunarity,
		float InGain, float InVeinThreshold);
};
```

创建 `Private/PlanetGeologySpherePodsDistribution.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologySpherePodsDistribution.h"

#include "PlanetGeologyTypes.h"

namespace
{
	/** 球内二次衰减（1 - d²/r²）：中心 1，表面 0，囊外 0。与旧实现逐字相同。 */
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

	struct FSpherePodsRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FPlanetGeologyPod> Pods;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Pods.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float PodFactor = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 PodIndex : *Refs)
				{
					PodFactor = FMath::Max(PodFactor, PodInfluence(Pods[PodIndex], P));
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
			return PodFactor * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float PodFactor = 0.f;
			for (const FPlanetGeologyPod& Pod : Pods)
			{
				PodFactor = FMath::Max(PodFactor, PodInfluence(Pod, P));
			}
			if (PodFactor <= 0.f)
			{
				return 0.f;
			}
			return PodFactor * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FPlanetGeologyPod& Pod : Pods)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = FBox(Pod.Center - FVector(Pod.Radius),
					Pod.Center + FVector(Pod.Radius));
				Region.CharacteristicRadius = Pod.Radius;
			}
		}

		bool IsEmpty() const override { return Pods.Num() == 0; }
		int32 NumPrimitives() const override { return Pods.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("SpherePods"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}

		const TArray<FPlanetGeologyPod>& GetSpherePods() const override { return Pods; }
	};

	TSharedPtr<const FPlanetGeologyDistributionRuntime> MakeRuntime(
		const int32 InPodCount, const float InPodRadius, const float InPodRadiusJitter,
		const float InPodDepthBias, const float InPodDepthCenter, const float InPodDepthSpread,
		const int32 InSeed, const float InShapeDetail, const int32 InOctaves,
		const float InLacunarity, const float InGain, const float InVeinThreshold)
	{
		const int32 PodCount = FMath::Clamp(InPodCount, 0, 512);
		const float NominalRadius = FMath::Clamp(InPodRadius, 0.001f, 0.25f);

		TSharedPtr<FSpherePodsRuntime> Runtime = MakeShared<FSpherePodsRuntime>();
		Runtime->Noise.Init(InSeed, NominalRadius, InShapeDetail, InOctaves,
			InLacunarity, InGain, InVeinThreshold);
		if (PodCount == 0)
		{
			return Runtime;
		}

		FRandomStream Rng(InSeed);
		const double Jitter = FMath::Clamp(double(InPodRadiusJitter), 0.0, 1.0);
		Runtime->Pods.Reserve(PodCount);

		float MaxRadius = 0.f;
		for (int32 PodIndex = 0; PodIndex < PodCount; ++PodIndex)
		{
			// 随机流顺序必须与旧 PlacePods 逐位一致：先抖动半径，再方向，再径向距离。
			// 调换任何两步，旧行星的矿囊位置就全变了。
			const double RadiusJitter = 1.0 + Jitter * (2.0 * double(Rng.FRand()) - 1.0);
			const double Radius = FMath::Clamp(double(NominalRadius) * RadiusJitter, 1e-3, 0.25);

			FPlanetGeologyPod& Pod = Runtime->Pods.AddDefaulted_GetRef();
			Pod.Radius = float(Radius);
			Pod.Center = SamplePlanetGeologyPlacement(
				Rng, InPodDepthBias, InPodDepthCenter, InPodDepthSpread);

			MaxRadius = FMath::Max(MaxRadius, Pod.Radius);
		}

		Runtime->CellSize = double(MaxRadius);
		for (int32 PodIndex = 0; PodIndex < Runtime->Pods.Num(); ++PodIndex)
		{
			const FPlanetGeologyPod& Pod = Runtime->Pods[PodIndex];
			PlanetGeologyIndexBounds(
				FBox(Pod.Center - FVector(Pod.Radius), Pod.Center + FVector(Pod.Radius)),
				Runtime->CellSize, Runtime->Index, PodIndex, Runtime->IndexReferences);
		}

		return Runtime;
	}
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologySpherePodsDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	return MakeRuntime(PodCount, PodRadius, PodRadiusJitter, PodDepthBias, PodDepthCenter,
		PodDepthSpread, ResolveSeed(Context.ElementId, Context.DistributionIndex),
		ShapeDetail, Octaves, Lacunarity, Gain, VeinThreshold);
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologySpherePodsDistribution::MakeRuntimeFromLegacyParams(
	const int32 InPodCount, const float InPodRadius, const float InPodRadiusJitter,
	const float InPodDepthBias, const float InPodDepthCenter, const float InPodDepthSpread,
	const int32 InSeed, const float InShapeDetail, const int32 InOctaves,
	const float InLacunarity, const float InGain, const float InVeinThreshold)
{
	return MakeRuntime(InPodCount, InPodRadius, InPodRadiusJitter, InPodDepthBias,
		InPodDepthCenter, InPodDepthSpread, InSeed, InShapeDetail, InOctaves,
		InLacunarity, InGain, InVeinThreshold);
}
```

- [ ] **Step 4: 元素运行期形态**

`Public/PlanetGeologyTypes.h`：在 include 区加 `#include "PlanetGeologyDistribution.h"`；删除 `FPlanetGeologyPodRef`（整个结构体，文件第 16-21 行）；把 `FPlanetGeologyElement` 整块替换为：

```cpp
struct FPlanetGeologyElement
{
	FString Id;
	FString DisplayName;
	FLinearColor Color = FLinearColor::White;

	/**
	 * 只影响颜色混合权重与图例占比，不改几何。
	 * 几何外凸量取各元素 Norm 的最大值（见 SampleGeometryDelta），与 Abundance 无关。
	 */
	float Abundance = 0.f;

	/**
	 * 分布生成器，顺序 = 资产里的顺序。元素 Norm = 各生成器结果取 max。
	 * 不可变、可跨线程共享；空数组表示这个元素没有矿。
	 */
	TArray<TSharedPtr<const FPlanetGeologyDistributionRuntime>> Distributions;
};
```

顶部文件注释同步改成「元素定义、矿囊、分布运行期、采样结果、外观参数」。

- [ ] **Step 5: 场的 max 合成与区域枚举**

`Public/PlanetGeologyField.h`：

1. 删掉 `#include "FastNoise/VoxelFastNoise.h"`（噪声已进运行期）。
2. 在 `GetPods` 注释上方加：

```cpp
	/**
	 * 该元素的全部有界区域（归一化空间），观测视图用它分簇。
	 * 顺序 = 生成器顺序 × 各生成器的区域顺序。
	 */
	void EnumerateRegions(int32 ElementIndex, TArray<FPlanetGeologyRegion>& Out) const;
```

3. `GetPods` 注释改为：

```cpp
	/** 第一个囊状生成器的矿囊表（归一化空间）；没有囊状生成器时返回空数组。 */
```

4. `IsEmpty()` 改成声明，实现搬到 `.cpp`：

```cpp
	/** 空场 = 没有元素，**或**所有元素的生成器都为空。 */
	bool IsEmpty() const;
```

5. 私有区：删除 `struct FElementRuntime`、`BuildPodIndex`、`PodIndex`、`PodIndexCellSize`；`Elements` 类型改为 `TArray<FPlanetGeologyElement>`；`EvaluateElementNorm` 注释改为「各生成器取 max」。

`Private/PlanetGeologyField.cpp`：

1. 删除匿名 namespace 里的 `ToPodCell`、`PodInfluence`、`PlacePods` 三个函数（前者后两者已搬进囊状生成器，`ToPodCell` 被 `PlanetGeologyToCell` 取代）。
2. `BuildFromPreset` 的元素循环替换为：

```cpp
	Field->Elements.Reserve(Preset.Elements.Num());
	for (const FPlanetGeologyElementParams& Params : Preset.Elements)
	{
		FPlanetGeologyElement Definition;
		Definition.Id = Params.Id.ToString();
		Definition.DisplayName = Params.DisplayName.IsEmpty()
			? Params.Id.ToString()
			: Params.DisplayName;
		Definition.Color = Params.Color;
		Definition.Abundance = FMath::Clamp(Params.Abundance, 0.f, 1.f);

		for (int32 DistributionIndex = 0;
			DistributionIndex < Params.Distributions.Num(); ++DistributionIndex)
		{
			const UPlanetGeologyDistribution* Distribution =
				Params.Distributions[DistributionIndex];
			if (!Distribution)
			{
				continue;
			}

			FPlanetGeologyDistributionBuildContext Context;
			Context.ElementId = Params.Id;
			Context.DistributionIndex = DistributionIndex;

			TSharedPtr<const FPlanetGeologyDistributionRuntime> Runtime =
				Distribution->BuildRuntime(Context);
			if (!Runtime.IsValid() || Runtime->IsEmpty())
			{
				continue;
			}

			TArray<FPlanetGeologyRegion> Regions;
			Runtime->EnumerateRegions(Regions);
			UE_LOG(LogTemp, Log, TEXT("元素=%s 生成器=%s 基元=%d 区域=%d %s"),
				*Params.Id.ToString(), Runtime->GetDebugName(),
				Runtime->NumPrimitives(), Regions.Num(), *Runtime->GetDebugSummary());

			Definition.Distributions.Add(MoveTemp(Runtime));
		}

		// 遗留兜底：旧资产没有 Distributions 但有 PodCount。种子必须是旧公式，
		// 用新派生规则会让旧行星的矿囊全部换位置。
		if (Definition.Distributions.Num() == 0 && Params.PodCount > 0)
		{
			const int32 LegacySeed = Params.Seed != 0
				? Params.Seed
				: int32(GetTypeHash(Params.Id) & 0x7fffffffu);
			Definition.Distributions.Add(
				UPlanetGeologySpherePodsDistribution::MakeRuntimeFromLegacyParams(
					Params.PodCount, Params.PodRadius, Params.PodRadiusJitter,
					Params.PodDepthBias, Params.PodDepthCenter, Params.PodDepthSpread,
					LegacySeed, Params.ShapeDetail, Params.Octaves, Params.Lacunarity,
					Params.Gain, Params.VeinThreshold));
			UE_LOG(LogTemp, Log,
				TEXT("元素=%s 生成器=SpherePods(遗留兜底) 基元=%d 区域=%d"),
				*Params.Id.ToString(), Params.PodCount, Params.PodCount);
		}

		Field->TotalAbundance += Definition.Abundance;
		Field->Elements.Add(MoveTemp(Definition));
	}
```

同时删掉循环上方原来的 `Field->Elements.Reserve` 与循环末尾 `Field->BuildPodIndex();` 调用。include 区加 `#include "PlanetGeologySpherePodsDistribution.h"`。

3. `BuildPodIndex` 与 `EvaluateElementNorm` 整块替换为：

```cpp
float FPlanetGeologyField::EvaluateElementNorm(const int32 ElementIndex,
	const FVector& NormalizedPoint) const
{
	const FPlanetGeologyElement& Element = Elements[ElementIndex];
	float Norm = 0.f;
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Element.Distributions)
	{
		Norm = FMath::Max(Norm, Distribution->EvaluateNormalized(NormalizedPoint));
		if (Norm >= 1.f)
		{
			break;
		}
	}
	return Norm;
}
```

4. `SampleElementNormBruteForce` 的矿囊循环替换为：

```cpp
	float Norm = 0.f;
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		Norm = FMath::Max(Norm, Distribution->EvaluateBruteForce(NormalizedPoint));
	}
	if (Norm <= 0.f)
	{
		return 0.f;
	}
	return Norm;
```

5. `GetPods` 替换为：

```cpp
const TArray<FPlanetGeologyPod>& FPlanetGeologyField::GetPods(const int32 ElementIndex) const
{
	static const TArray<FPlanetGeologyPod> Empty;
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return Empty;
	}
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		const TArray<FPlanetGeologyPod>& Pods = Distribution->GetSpherePods();
		if (Pods.Num() > 0)
		{
			return Pods;
		}
	}
	return Empty;
}
```

6. 新增两个函数（放在 `GetPods` 之后）：

```cpp
void FPlanetGeologyField::EnumerateRegions(const int32 ElementIndex,
	TArray<FPlanetGeologyRegion>& Out) const
{
	Out.Reset();
	if (!Elements.IsValidIndex(ElementIndex))
	{
		return;
	}
	for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
		: Elements[ElementIndex].Distributions)
	{
		Distribution->EnumerateRegions(Out);
	}
}

bool FPlanetGeologyField::IsEmpty() const
{
	if (Elements.Num() == 0)
	{
		return true;
	}
	for (const FPlanetGeologyElement& Element : Elements)
	{
		for (const TSharedPtr<const FPlanetGeologyDistributionRuntime>& Distribution
			: Element.Distributions)
		{
			if (Distribution.IsValid() && !Distribution->IsEmpty())
			{
				return false;
			}
		}
	}
	return true;
}
```

7. `Accumulate` 与 `GetSampledElementDefinitions` 里 `Runtime.Definition` 的访问全部去掉一层：

```cpp
		const FPlanetGeologyElement& Definition = Elements[ElementIndex];
```
（`Accumulate` 内原本还有 `const FElementRuntime& Runtime = Elements[ElementIndex];` 一行，删掉。）
`GetSampledElementDefinitions` 改为 `Out.Add(Elements[Index]);`。

- [ ] **Step 6: 预设加生成器数组**

`Public/PlanetGeologyPreset.h`：顶部加 `class UPlanetGeologyDistribution;`；在 `FPlanetGeologyElementParams` 的 `Abundance` 之后插入：

```cpp
	/**
	 * 这个元素的分布生成器。多个生成器逐点取 max（不是相加）。
	 * 空数组且 PodCount > 0 时走遗留兜底，旧资产因此在运行期保持原样。
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "Distribution")
	TArray<TObjectPtr<UPlanetGeologyDistribution>> Distributions;
```

- [ ] **Step 7: 改名既有测试**

`Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp`：只改名字，不动实现：

- `"StellarEra.Planet.Geology.PodCoverage"` → `"StellarEra.Planet.Geology.ShapePods.Coverage"`
- `"StellarEra.Planet.Geology.PodOverlap"` → `"StellarEra.Planet.Geology.ShapePods.Overlap"`
- `"StellarEra.Planet.Geology.SpatialIndex"` → `"StellarEra.Planet.Geology.ShapePods.BruteForceAgrees"`
- `"StellarEra.Planet.Geology.PodShape"` → `"StellarEra.Planet.Geology.ShapePods.Shape"`

- [ ] **Step 8: 加 BOM**

```bash
Source/StellarEraGeology/Public/PlanetGeologySpherePodsDistribution.h
Source/StellarEraGeology/Private/PlanetGeologySpherePodsDistribution.cpp
Source/StellarEraGeology/Public/PlanetGeologyTypes.h
Source/StellarEraGeology/Public/PlanetGeologyField.h
Source/StellarEraGeology/Private/PlanetGeologyField.cpp
Source/StellarEraGeology/Public/PlanetGeologyPreset.h
Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

- [ ] **Step 9: 构建并跑地质测试**

先跑 Global Constraints 的构建命令，再跑：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Geology" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_Geology2.log"
```

预期：全绿。`EarthPresetValues` 此时仍应通过 —— 地球上还没有生成器，走的正是遗留兜底，囊表不变。

- [ ] **Step 10: 提交**

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp \
  Source/StellarEraGeology/Public/PlanetGeologySpherePodsDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologySpherePodsDistribution.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyTypes.h \
  Source/StellarEraGeology/Public/PlanetGeologyField.h \
  Source/StellarEraGeology/Private/PlanetGeologyField.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp
git commit -m "feat(地质): 囊状生成器 + 元素多生成器 max 合成" -- \
  Source/StellarEraGeology/Public/PlanetGeologyDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp \
  Source/StellarEraGeology/Public/PlanetGeologySpherePodsDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologySpherePodsDistribution.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyTypes.h \
  Source/StellarEraGeology/Public/PlanetGeologyField.h \
  Source/StellarEraGeology/Private/PlanetGeologyField.cpp \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp
```

---

## Task 3: 片层 / 透镜生成器

**Files:**
- Create: `Source/StellarEraGeology/Public/PlanetGeologyLensDistribution.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyLensDistribution.cpp`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`

**Interfaces:**
- Consumes: Task 1 的全部工具（含 `FPlanetGeologyDistributionNoise::EvaluateAt`，见 Task 1 的噪声实现）；Task 2 的 `EnumerateRegions`。
- Produces: `UPlanetGeologyLensDistribution`（`SeamCount / SeamLength / SeamWidth / SeamThickness / SizeJitter / DepthBias / DepthCenter / DepthSpread / TiltJitterDeg / WarpAmplitude / WarpScale`）。

- [ ] **Step 1: 写失败测试**

在 `PlanetGeologyDistributionTests.cpp` 追加（并在 include 区加 `#include "PlanetGeologyLensDistribution.h"`）：

```cpp
namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyLensDistribution* AddLens(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		UPlanetGeologyLensDistribution* Lens =
			NewObject<UPlanetGeologyLensDistribution>(&Preset);
		Lens->SeamCount = 24;
		Lens->SeamLength = 0.14f;
		Lens->SeamWidth = 0.07f;
		Lens->SeamThickness = 0.008f;
		Lens->SizeJitter = 0.35f;
		Lens->DepthBias = 0.9f;
		Lens->DepthCenter = 0.12f;
		Lens->DepthSpread = 0.04f;
		Lens->TiltJitterDeg = 5.f;
		Lens->Seed = 707;
		Lens->bSeedExplicit = true;
		Element.Distributions.Add(Lens);
		return Lens;
	}

	/** 加速路径与暴力路径的逐点一致性。
	 *
	 * 采样范围必须**超出**上报的包围盒：只在盒内采样时，包围盒算小了也发现不了 ——
	 * 盒内每一格索引都登记过，当然一致。所以区域点按 2× extent 采，再沿特征半径补壳层点。
	 * 失配要打出坐标：不说清是哪个点、差多少，两个 0.x 的浮点差根本 loc 不到。
	 */
	int32 CountBruteForceMismatches(FAutomationTestBase& Test, const FPlanetGeologyField& Field,
		int32 ElementIndex, const TArray<FPlanetGeologyRegion>& Regions, FRandomStream& Rng)
	{
		int32 Mismatches = 0;

		auto CheckPoint = [&Mismatches, &Test, &Field, ElementIndex](const FVector& P)
		{
			if (P.SizeSquared() > 1.f)
			{
				return;
			}
			const float Indexed = Field.SampleElementNormAtNormalized(ElementIndex, P);
			const float Brute = Field.SampleElementNormBruteForce(ElementIndex, P);
			if (!FMath::IsNearlyEqual(Indexed, Brute, 1e-6f))
			{
				Test.AddError(FString::Printf(
					TEXT("索引与暴力不一致：索引=%.6f 暴力=%.6f，点 %s"), Indexed, Brute,
					*P.ToString()));
				++Mismatches;
			}
		};

		for (int32 SampleIndex = 0; SampleIndex < 2000; ++SampleIndex)
		{
			CheckPoint(FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f)));
		}
		for (const FPlanetGeologyRegion& Region : Regions)
		{
			const FVector Center = Region.Bounds.GetCenter();
			const FVector Extent = Region.Bounds.GetExtent();
			for (int32 SampleIndex = 0; SampleIndex < 40; ++SampleIndex)
			{
				const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
					Rng.FRandRange(-1.f, 1.f));
				CheckPoint(Center + Offset * Extent * 2.0);
			}
			for (int32 SampleIndex = 0; SampleIndex < 16; ++SampleIndex)
			{
				CheckPoint(Center + Rng.GetUnitVector() * double(Region.CharacteristicRadius));
			}
		}
		return Mismatches;
	}

	/** 包围盒的保守性：暴力路径说有矿的点，必须落在某个区域的包围盒里。
	 *
	 * 这条**不依赖包围盒的算法**，所以公式写反（盒子比真实矿体小）时它立刻红。
	 * 上面那条「索引 == 暴力」在盒子偏小时可能结构性失明：索引只登记盒内格子，
	 * 盒外没有候选可查；只有主动去盒外采样的人才会发现那里本该有矿。
	 */
	int32 CountOreOutsideAllRegions(const FPlanetGeologyField& Field, int32 ElementIndex,
		const TArray<FPlanetGeologyRegion>& Regions, FRandomStream& Rng)
	{
		int32 Violations = 0;
		for (int32 SampleIndex = 0; SampleIndex < 4000; ++SampleIndex)
		{
			const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			if (P.SizeSquared() > 1.f)
			{
				continue;
			}
			if (Field.SampleElementNormBruteForce(ElementIndex, P) <= 0.f)
			{
				continue;
			}
			bool bCovered = false;
			for (const FPlanetGeologyRegion& Region : Regions)
			{
				if (Region.Bounds.IsInsideOrOn(P))
				{
					bCovered = true;
					break;
				}
			}
			if (!bCovered)
			{
				++Violations;
			}
		}
		// 区域包围盒内部（真实矿体所在）按 2× extent 补采：命中率比整个球体随机高得多，
		// 而且正是盒子偏小时矿体探出盒外的位置。
		for (const FPlanetGeologyRegion& Region : Regions)
		{
			const FVector Center = Region.Bounds.GetCenter();
			const FVector Extent = Region.Bounds.GetExtent();
			for (int32 SampleIndex = 0; SampleIndex < 40; ++SampleIndex)
			{
				const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
					Rng.FRandRange(-1.f, 1.f));
				const FVector P = Center + Offset * Extent * 2.0;
				if (Field.SampleElementNormBruteForce(ElementIndex, P) <= 0.f)
				{
					continue;
				}
				bool bCovered = false;
				for (const FPlanetGeologyRegion& Other : Regions)
				{
					if (Other.Bounds.IsInsideOrOn(P))
					{
						bCovered = true;
						break;
					}
				}
				if (!bCovered)
				{
					++Violations;
				}
			}
		}
		return Violations;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLensBoundsTest,
	"StellarEra.Planet.Geology.Lens.Bounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLensBoundsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("silicon"));
	AddLens(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);
	if (!TestEqual(TEXT("24 片各一个区域"), Regions.Num(), 24))
	{
		return false;
	}

	int32 InBand = 0;
	for (const FPlanetGeologyRegion& Region : Regions)
	{
		const FVector Center = Region.Bounds.GetCenter();
		TestTrue(TEXT("区域包围盒有效"), Region.Bounds.IsValid != 0);
		// 厚度带 ±35% 抖动，所以判据是「落在抖动区间内」，不是等于标称值。
		TestTrue(*FString::Printf(TEXT("特征半径 = 厚度且落在抖动区间（%.4f）"),
			Region.CharacteristicRadius),
			Region.CharacteristicRadius >= 0.008f * 0.64f
				&& Region.CharacteristicRadius <= 0.008f * 1.36f);
		TestTrue(*FString::Printf(TEXT("片心不越出球体（%.4f）"), Center.Size()),
			Center.Size() <= 1.0 + 1e-4);

		// 作者参数里的深度 0 = 地表、1 = 球心；这里按同一口径回算。
		const double Depth = 1.0 - double(Center.Size());
		if (FMath::Abs(Depth - 0.12) <= 4.0 * 0.04)
		{
			++InBand;
		}
	}
	TestTrue(*FString::Printf(TEXT("多数片心落在偏好深度带（%d/24）"), InBand), InBand >= 18);

	// 两次建场逐位一致（放置全走固定随机流）。
	TArray<FPlanetGeologyRegion> Rebuilt;
	MakeField(*Preset)->EnumerateRegions(0, Rebuilt);
	TestTrue(TEXT("两次建场区域逐位相同"), AreRegionsEqual(Regions, Rebuilt));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLensBruteForceTest,
	"StellarEra.Planet.Geology.Lens.BruteForceAgrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLensBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("silicon"));
	AddLens(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	FRandomStream Rng(31415);
	TestEqual(TEXT("索引路径 == 暴力路径"), CountBruteForceMismatches(*this, *Field, 0, Regions, Rng), 0);
	TestEqual(TEXT("包围盒盖住全部真实矿体"), CountOreOutsideAllRegions(*Field, 0, Regions, Rng), 0);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

预期：`PlanetGeologyLensDistribution.h` 不存在，编译失败。

- [ ] **Step 3: 写实现**

创建 `Public/PlanetGeologyLensDistribution.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 片层 / 透镜分布：沿球面切平面铺开的薄椭球，角度上成片、厚度上成层。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyLensDistribution.generated.h"

/**
 * 片层 / 透镜。
 *
 * 一片 = 一个有向椭球（三轴半长 A / B / C，C 是径向半厚）。宽而薄是刻意的：
 * A / C 可达 20 倍以上。基元尺度一致性（spec §3.2）约束的是**片与片之间**，
 * 不是单片的长宽比。
 */
UCLASS(DisplayName = "片层（透镜）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyLensDistribution : public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0", ClampMax = "256"))
	int32 SeamCount = 24;

	/** 长轴半径（占行星基准半径）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float SeamLength = 0.12f;

	/** 宽轴半径。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.01", ClampMax = "0.5"))
	float SeamWidth = 0.06f;

	/** 径向半厚。观测视图角点与它成平方反比：减半角点涨 4 倍。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0005", ClampMax = "0.05"))
	float SeamThickness = 0.008f;

	/** 三轴各自的抖动幅度（±比例）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SizeJitter = 0.35f;

	/** 深度偏好强度。0 = 全深度体积均匀。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.6f;

	/** 偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.12f;

	UPROPERTY(EditAnywhere, Category = "Seams|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.06f;

	/** 相对球面切平面的随机倾斜（度）。 */
	UPROPERTY(EditAnywhere, Category = "Seams", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float TiltJitterDeg = 5.0f;

	/** 径向域扭曲强度，单位 = 厚度。0 = 关闭（默认）。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Warp", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float WarpAmplitude = 0.0f;

	/** 扭曲噪声频率，单位 = 厚度的倒数。 */
	UPROPERTY(EditAnywhere, Category = "Seams|Warp", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float WarpScale = 2.0f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
```

创建 `Private/PlanetGeologyLensDistribution.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyLensDistribution.h"

namespace
{
	/** 一片透镜（归一化空间）：中心 + 局部→世界的旋转 + 三轴半长。 */
	struct FLensPrimitive
	{
		FVector Center = FVector::ZeroVector;
		FQuat Orientation = FQuat::Identity;
		FVector HalfExtent = FVector::ZeroVector; // A, B, C
		FBox Bounds = FBox(ForceInit);
		float BoundingRadius = 0.f;
	};

	struct FLensRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FLensPrimitive> Primitives;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		double WarpAmplitude = 0.0;
		double WarpScale = 2.0;
		FPlanetGeologyDistributionNoise Noise;

		/** 单片在 P 处的强度：非负的 1 − Σ(Local/h)²。 */
		float EvaluatePrimitive(const FLensPrimitive& Lens, const FVector& P) const
		{
			if (Lens.HalfExtent.X <= 0.f || Lens.HalfExtent.Y <= 0.f
				|| Lens.HalfExtent.Z <= 0.f)
			{
				return 0.f;
			}

			FVector Local = Lens.Orientation.UnrotateVector(P - Lens.Center);
			if (WarpAmplitude > 0.0)
			{
				// 只扭曲径向：薄的一轴动起来才像片层被揉皱，横向扭曲会把整片拽走。
				const double Thickness = double(Lens.HalfExtent.Z);
				const double Frequency = WarpScale / Thickness;
				const float Warp = Noise.EvaluateAt(Local * float(Frequency));
				Local.Z += float(WarpAmplitude * Thickness * (2.0 * double(Warp) - 1.0));
			}

			const double Q =
				FMath::Square(double(Local.X) / double(Lens.HalfExtent.X))
				+ FMath::Square(double(Local.Y) / double(Lens.HalfExtent.Y))
				+ FMath::Square(double(Local.Z) / double(Lens.HalfExtent.Z));
			return float(FMath::Max(0.0, 1.0 - Q));
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Primitives.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 PrimitiveIndex : *Refs)
				{
					Best = FMath::Max(Best, EvaluatePrimitive(Primitives[PrimitiveIndex], P));
					if (Best >= 1.f)
					{
						break;
					}
				}
			}
			if (Best <= 0.f)
			{
				return 0.f; // 不在任何片里：一次 Perlin 都不求
			}
			return Best * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float Best = 0.f;
			for (const FLensPrimitive& Lens : Primitives)
			{
				Best = FMath::Max(Best, EvaluatePrimitive(Lens, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FLensPrimitive& Lens : Primitives)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Lens.Bounds;
				Region.CharacteristicRadius = Lens.HalfExtent.Z;
			}
		}

		bool IsEmpty() const override { return Primitives.Num() == 0; }
		int32 NumPrimitives() const override { return Primitives.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Lens"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}
	};
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologyLensDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 InSeamCount = FMath::Clamp(SeamCount, 0, 256);
	const double InSizeJitter = FMath::Clamp(double(SizeJitter), 0.0, 1.0);
	const double TiltDegrees = FMath::Clamp(double(TiltJitterDeg), 0.0, 45.0);

	// 种子只解析一次：噪声与随机流必须是同一个种子，否则调参会得到「噪声和
	// 位置各自随机」的鬼影。
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	TSharedPtr<FLensRuntime> Runtime = MakeShared<FLensRuntime>();
	Runtime->WarpAmplitude = FMath::Clamp(double(WarpAmplitude), 0.0, 2.0);
	Runtime->WarpScale = FMath::Clamp(double(WarpScale), 0.5, 8.0);
	Runtime->Noise.Init(ResolvedSeed,
		FMath::Clamp(SeamThickness, 0.0005f, 0.05f),
		ShapeDetail, Octaves, Lacunarity, Gain, VeinThreshold);
	if (InSeamCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	Runtime->Primitives.Reserve(InSeamCount);
	float MaxBoundingRadius = 0.f;

	for (int32 SeamIndex = 0; SeamIndex < InSeamCount; ++SeamIndex)
	{
		// 随机流顺序固定：方向+半径 → 切向随机向量 → 倾斜角 → A/B/C 抖动。
		const FVector Center = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const FVector LocalZ = Center.IsNearlyZero() ? FVector::ZAxisVector
			: Center.GetSafeNormal();

		FVector LocalX = FVector::CrossProduct(LocalZ, Rng.GetUnitVector()).GetSafeNormal();
		if (LocalX.IsNearlyZero())
		{
			LocalX = FVector::CrossProduct(LocalZ, FVector::XAxisVector).GetSafeNormal();
		}
		const FQuat Base = FRotationMatrix::MakeFromXZ(LocalX, LocalZ).ToQuat();
		const FVector LocalY = FVector::CrossProduct(LocalZ, LocalX);

		const double TiltRadians =
			FMath::DegreesToRadians(TiltDegrees * (2.0 * double(Rng.FRand()) - 1.0));
		const FQuat Orientation = FQuat(LocalY, TiltRadians) * Base;

		auto Jittered = [&Rng, InSizeJitter](const double BaseValue)
		{
			return BaseValue * (1.0 + InSizeJitter * (2.0 * double(Rng.FRand()) - 1.0));
		};

		FLensPrimitive& Lens = Runtime->Primitives.AddDefaulted_GetRef();
		Lens.Center = Center;
		Lens.Orientation = Orientation;
		Lens.HalfExtent = FVector(
			float(FMath::Clamp(Jittered(double(SeamLength)), 0.01, 0.5)),
			float(FMath::Clamp(Jittered(double(SeamWidth)), 0.01, 0.5)),
			float(FMath::Clamp(Jittered(double(SeamThickness)), 0.0005, 0.05)));

		// 有向包围盒：把局部盒的 8 个角点转到世界再取 FBox。
		//
		// 不要手推 `Σ_j |M_ij| h_j` 那种下标：`FMatrix` 是行向量约定，行列含义极易写反，
		// 写反会得到一个**不保守**的盒子（某些朝向下比真实包围盒小十几倍），索引随之漏格，
		// 真实矿体被切掉 —— 而按包围盒采样的测试根本发现不了。转 8 个角点没有下标可写错。
		//
		// 厚度用**扭曲放大后**的值：径向域扭曲最多把点沿局部 Z 挪
		// WarpAmplitude × 厚度，按原始厚度算包围盒会让索引漏候选、区域也盖不住
		// 实际矿体（WarpAmplitude 非 0 时表现为矿体被切掉一角）。
		const double EffectiveHalfThickness =
			double(Lens.HalfExtent.Z) * (1.0 + Runtime->WarpAmplitude);
		FBox OrientedBounds(ForceInit);
		for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
		{
			const FVector LocalCorner(
				(CornerIndex & 1) ? double(Lens.HalfExtent.X) : -double(Lens.HalfExtent.X),
				(CornerIndex & 2) ? double(Lens.HalfExtent.Y) : -double(Lens.HalfExtent.Y),
				(CornerIndex & 4) ? EffectiveHalfThickness : -EffectiveHalfThickness);
			OrientedBounds += Lens.Center + Orientation.RotateVector(LocalCorner);
		}
		Lens.Bounds = OrientedBounds;
		const FVector Extent = OrientedBounds.GetExtent();
		Lens.BoundingRadius = float(Extent.GetMax());
		MaxBoundingRadius = FMath::Max(MaxBoundingRadius, Lens.BoundingRadius);
	}

	Runtime->CellSize = double(MaxBoundingRadius);
	for (int32 PrimitiveIndex = 0; PrimitiveIndex < Runtime->Primitives.Num(); ++PrimitiveIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Primitives[PrimitiveIndex].Bounds,
			Runtime->CellSize, Runtime->Index, PrimitiveIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
```

- [ ] **Step 4: 加 BOM 并构建**

```bash
Source/StellarEraGeology/Public/PlanetGeologyLensDistribution.h
Source/StellarEraGeology/Private/PlanetGeologyLensDistribution.cpp
Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

构建后运行 `Automation RunTests StellarEra.Planet.Geology.Lens`，预期 2 条通过。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyLensDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyLensDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
git commit -m "feat(地质): 片层/透镜分布生成器" -- \
  Source/StellarEraGeology/Public/PlanetGeologyLensDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyLensDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

---

## Task 4: 脉状生成器

**Files:**
- Create: `Source/StellarEraGeology/Public/PlanetGeologyVeinDistribution.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyVeinDistribution.cpp`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`

**Interfaces:**
- Consumes: Task 1 工具、Task 3 的 `CountBruteForceMismatches` / `CountOreOutsideAllRegions` / `AreRegionsEqual` 测试助手。
- Produces: `UPlanetGeologyVeinDistribution`（`VeinCount / VeinLength / VeinRadius / RadiusJitter / SegmentLengthRatio / DirectionJitterDeg / BranchCount / DepthBias / DepthCenter / DepthSpread`）。

**为什么拆段：** 一整条 0.35 长的脉若当成一个胶囊，包围半径约 0.18，是管半径的 22 倍，
违反 spec §3.2 的「同一生成器基元同量级」；而且观测视图会把整条脉并进一个大簇。
按 `SegmentLengthRatio × 半径` 拆成短段后，每段包围半径 ≈ 1.25r + r，量级一致。

- [ ] **Step 1: 写失败测试**

在 `PlanetGeologyDistributionTests.cpp` 追加（include 区加 `#include "PlanetGeologyVeinDistribution.h"`）：

```cpp
namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyVeinDistribution* AddVein(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		UPlanetGeologyVeinDistribution* Vein =
			NewObject<UPlanetGeologyVeinDistribution>(&Preset);
		Vein->VeinCount = 4;
		Vein->VeinLength = 0.35f;
		Vein->VeinRadius = 0.008f;
		Vein->RadiusJitter = 0.4f;
		Vein->SegmentLengthRatio = 2.5f;
		Vein->DirectionJitterDeg = 8.f;
		Vein->BranchCount = 1;
		Vein->DepthBias = 0.5f;
		Vein->DepthCenter = 0.4f;
		Vein->DepthSpread = 0.12f;
		Vein->VeinThreshold = 0.f;
		Vein->Seed = 909;
		Vein->bSeedExplicit = true;
		Element.Distributions.Add(Vein);
		return Vein;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyVeinContinuityTest,
	"StellarEra.Planet.Geology.Vein.Continuity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyVeinContinuityTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
	UPlanetGeologyVeinDistribution* Vein = AddVein(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	// 段数上限从夹具参数算，别手抄数字：改了夹具常量却忘了改这里，上限会静默失真。
	// 分支链只有主脉一半长 —— 按主脉算会多留一倍余量，「主链多走一段」这种 off-by-one 就溜过去。
	const double SegmentLength = double(Vein->VeinRadius) * double(Vein->SegmentLengthRatio);
	const int32 MainChainSegments = FMath::CeilToInt(double(Vein->VeinLength) / SegmentLength);
	const int32 BranchChainSegments =
		FMath::CeilToInt(0.5 * double(Vein->VeinLength) / SegmentLength);
	const int32 MaxSegments =
		Vein->VeinCount * (MainChainSegments + Vein->BranchCount * BranchChainSegments);
	TestTrue(*FString::Printf(TEXT("段数在构造上限内（%d ≤ %d）"), Regions.Num(), MaxSegments),
		Regions.Num() > 0 && Regions.Num() <= MaxSegments);

	TArray<FVector> Centers;
	Centers.Reserve(Regions.Num());
	for (const FPlanetGeologyRegion& Region : Regions)
	{
		Centers.Add(Region.Bounds.GetCenter());
	}

	// 段中点是骨架上的点：VeinThreshold = 0 时那里必须真的有矿。
	// 但这**证明不了「脉是连续的」**：包围盒是 min/max ± r，盒心恒等于段中点，
	// 所以任意一组互不相连的胶囊都能让「中点有矿」全绿。
	// 真正的判据是相邻段中点必须靠得足够近：每段恰好前进 SegmentLength，
	// 链上相邻中点间距恰好 = SegmentLength，分支首段离锚点 ≤ SegmentLength。
	// 骨架若断了（每段独立摆放），中点会散布在整个球面，几乎每个段都成孤点。
	int32 ZeroCenters = 0;
	int32 IsolatedSegments = 0;
	float MinRadius = TNumericLimits<float>::Max();
	float MaxRadius = 0.f;
	for (int32 RegionIndex = 0; RegionIndex < Regions.Num(); ++RegionIndex)
	{
		const FPlanetGeologyRegion& Region = Regions[RegionIndex];
		TestTrue(TEXT("段包围盒有效"), Region.Bounds.IsValid != 0);
		// 逐段半径带 ±40% 抖动，判据同样是「落在抖动区间内」。
		TestTrue(*FString::Printf(TEXT("特征半径 = 管半径且落在抖动区间（%.4f）"),
			Region.CharacteristicRadius),
			Region.CharacteristicRadius >= 0.008f * 0.59f
				&& Region.CharacteristicRadius <= 0.008f * 1.41f);
		MinRadius = FMath::Min(MinRadius, Region.CharacteristicRadius);
		MaxRadius = FMath::Max(MaxRadius, Region.CharacteristicRadius);
		if (Field->SampleElementNormAtNormalized(0, Centers[RegionIndex]) <= 0.f)
		{
			++ZeroCenters;
		}

		bool bHasNeighbour = false;
		for (int32 OtherIndex = 0; OtherIndex < Centers.Num(); ++OtherIndex)
		{
			if (OtherIndex != RegionIndex
				&& FVector::DistSquared(Centers[RegionIndex], Centers[OtherIndex])
					<= FMath::Square(1.5 * SegmentLength))
			{
				bHasNeighbour = true;
				break;
			}
		}
		if (!bHasNeighbour)
		{
			++IsolatedSegments;
		}
	}
	TestEqual(TEXT("所有段中点场值 > 0"), ZeroCenters, 0);
	TestEqual(TEXT("没有孤立段（脉是连着的，不是一串点）"), IsolatedSegments, 0);
	TestTrue(*FString::Printf(TEXT("半径抖动确实生效（%.4f..%.4f）"), MinRadius, MaxRadius),
		MaxRadius > MinRadius * 1.01f);

	// 两次建场逐位一致（种子只解析一次，噪声与随机流同源）。
	TArray<FPlanetGeologyRegion> Rebuilt;
	MakeField(*Preset)->EnumerateRegions(0, Rebuilt);
	TestTrue(TEXT("两次建场区域逐位相同"), AreRegionsEqual(Regions, Rebuilt));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyVeinBruteForceTest,
	"StellarEra.Planet.Geology.Vein.BruteForceAgrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyVeinBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("iron"));
	AddVein(*Preset, Element);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	FRandomStream Rng(27182);
	TestEqual(TEXT("索引路径 == 暴力路径"),
		CountBruteForceMismatches(*this, *Field, 0, Regions, Rng), 0);
	TestEqual(TEXT("包围盒盖住全部真实矿体"), CountOreOutsideAllRegions(*Field, 0, Regions, Rng), 0);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

预期：`PlanetGeologyVeinDistribution.h` 不存在，编译失败。

- [ ] **Step 3: 写实现**

创建 `Public/PlanetGeologyVeinDistribution.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 脉状分布：折线骨架 + 胶囊，穿切地层。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyVeinDistribution.generated.h"

/**
 * 脉状 / 折线骨架。
 *
 * 一条脉按 `SegmentLengthRatio × 半径` 拆成短段，每段一个胶囊。拆段是刻意的：
 * 单段包围半径因此稳定在 (0.5 × 段长 + r) 量级，观测视图的稀疏网格格边不会被
 * 一条长脉拉大（spec §3.2）。
 */
UCLASS(DisplayName = "脉状（穿切）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyVeinDistribution : public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0", ClampMax = "64"))
	int32 VeinCount = 4;

	/** 单条总长度（占行星基准半径）。这是**目标**：起点越浅、方向越朝外，实际越短。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float VeinLength = 0.35f;

	/** 管半径。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.002", ClampMax = "0.05"))
	float VeinRadius = 0.008f;

	/** 逐段半径抖动。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RadiusJitter = 0.4f;

	/** 段长 / 管半径。越大段越少、弯曲越粗放。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float SegmentLengthRatio = 2.5f;

	/** 每段方向偏折（度）。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0.0", ClampMax = "45.0"))
	float DirectionJitterDeg = 8.0f;

	/** 每条主脉的分叉数。分叉长度 = 主脉的一半。 */
	UPROPERTY(EditAnywhere, Category = "Veins", meta = (ClampMin = "0", ClampMax = "8"))
	int32 BranchCount = 1;

	/** 起点深度偏好强度。 */
	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.5f;

	/** 起点偏好深度（0 = 地表，1 = 球心）。 */
	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.4f;

	UPROPERTY(EditAnywhere, Category = "Veins|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.12f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
```

创建 `Private/PlanetGeologyVeinDistribution.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyVeinDistribution.h"

namespace
{
	/** 一段脉（归一化空间）：胶囊 A→B，半径 r。 */
	struct FVeinSegment
	{
		FVector A = FVector::ZeroVector;
		FVector B = FVector::ZeroVector;
		float Radius = 0.f;
		FBox Bounds = FBox(ForceInit);
		float BoundingRadius = 0.f;
	};

	/**
	 * 从 Point 出发沿 Direction 走 TargetLength，把每一段写进 Segments。
	 *
	 * 随机流顺序固定：扰动轴 → 扰动角 → （新点出球则停）→ 半径抖动。
	 * 停止条件是「新点离球心超过 1 − 0.5r」：露头靠胶囊自身探出，不额外外推。
	 */
	void GrowChain(TArray<FVeinSegment>& Segments, TArray<FVector>& OutPoints,
		FRandomStream& Rng, FVector Point, FVector Direction, const double TargetLength,
		const double InVeinRadius, const double InRadiusJitter, const double SegmentLength,
		const double DirectionJitterDeg)
	{
		OutPoints.Reset();
		OutPoints.Add(Point);

		double Traveled = 0.0;
		while (Traveled < TargetLength)
		{
			const FVector RandomVector = Rng.GetUnitVector();
			const double Angle = FMath::DegreesToRadians(
				DirectionJitterDeg * (2.0 * double(Rng.FRand()) - 1.0));
			FVector Axis = FVector::CrossProduct(Direction, RandomVector).GetSafeNormal();
			if (Axis.IsNearlyZero())
			{
				Axis = FVector::CrossProduct(Direction, FVector::ZAxisVector).GetSafeNormal();
			}

			FVector NewDirection = Axis.IsNearlyZero()
				? Direction
				: Direction.RotateAngleAxisRad(Angle, Axis);
			NewDirection = NewDirection.GetSafeNormal();
			if (NewDirection.IsNearlyZero())
			{
				NewDirection = Direction;
			}

			const FVector NewPoint = Point + NewDirection * float(SegmentLength);
			if (NewPoint.Size() > 1.0 - 0.5 * InVeinRadius)
			{
				break;
			}

			const double RadiusScale =
				1.0 + InRadiusJitter * (2.0 * double(Rng.FRand()) - 1.0);

			FVeinSegment& Segment = Segments.AddDefaulted_GetRef();
			Segment.A = Point;
			Segment.B = NewPoint;
			Segment.Radius = float(FMath::Clamp(InVeinRadius * RadiusScale, 0.002, 0.05));
			Segment.Bounds = FBox(
				Segment.A.ComponentMin(Segment.B) - FVector(Segment.Radius),
				Segment.A.ComponentMax(Segment.B) + FVector(Segment.Radius));
			Segment.BoundingRadius =
				float((NewPoint - Point).Size() * 0.5) + Segment.Radius;

			OutPoints.Add(NewPoint);
			Point = NewPoint;
			Direction = NewDirection;
			Traveled += SegmentLength;
		}
	}

	struct FVeinRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FVeinSegment> Segments;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		float EvaluateSegment(const FVeinSegment& Segment, const FVector& P) const
		{
			const FVector Delta = Segment.B - Segment.A;
			const double LengthSquared = Delta.SizeSquared();
			const double RadiusSquared =
				double(Segment.Radius) * double(Segment.Radius);
			if (LengthSquared <= 0.0 || RadiusSquared <= 0.0)
			{
				return 0.f;
			}

			const double T = FMath::Clamp(
				double(FVector::DotProduct(P - Segment.A, Delta)) / LengthSquared, 0.0, 1.0);
			const FVector Closest = Segment.A + Delta * float(T);
			const double DistanceSquared = (P - Closest).SizeSquared();
			if (DistanceSquared >= RadiusSquared)
			{
				return 0.f;
			}
			return float(1.0 - DistanceSquared / RadiusSquared);
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Segments.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 SegmentIndex : *Refs)
				{
					Best = FMath::Max(Best, EvaluateSegment(Segments[SegmentIndex], P));
					if (Best >= 1.f)
					{
						break;
					}
				}
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float Best = 0.f;
			for (const FVeinSegment& Segment : Segments)
			{
				Best = FMath::Max(Best, EvaluateSegment(Segment, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FVeinSegment& Segment : Segments)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Segment.Bounds;
				Region.CharacteristicRadius = Segment.Radius;
			}
		}

		bool IsEmpty() const override { return Segments.Num() == 0; }
		int32 NumPrimitives() const override { return Segments.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Vein"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}
	};
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologyVeinDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	const int32 InVeinCount = FMath::Clamp(VeinCount, 0, 64);
	const double InVeinRadius = FMath::Clamp(double(VeinRadius), 0.002, 0.05);
	const double SegmentLength =
		InVeinRadius * FMath::Clamp(double(SegmentLengthRatio), 1.0, 4.0);
	const double TargetLength = FMath::Clamp(double(VeinLength), 0.05, 1.0);
	const double InDirectionJitter = FMath::Clamp(double(DirectionJitterDeg), 0.0, 45.0);
	const int32 InBranchCount = FMath::Clamp(BranchCount, 0, 8);
	const double InRadiusJitter = FMath::Clamp(double(RadiusJitter), 0.0, 1.0);

	TSharedPtr<FVeinRuntime> Runtime = MakeShared<FVeinRuntime>();
	Runtime->Noise.Init(ResolvedSeed, float(InVeinRadius), ShapeDetail, Octaves,
		Lacunarity, Gain, VeinThreshold);
	if (InVeinCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	for (int32 VeinIndex = 0; VeinIndex < InVeinCount; ++VeinIndex)
	{
		const FVector Start = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const FVector InitialDirection = Rng.GetUnitVector();

		TArray<FVector> ChainPoints;
		GrowChain(Runtime->Segments, ChainPoints, Rng, Start, InitialDirection,
			TargetLength, InVeinRadius, InRadiusJitter, SegmentLength, InDirectionJitter);

		for (int32 BranchIndex = 0; BranchIndex < InBranchCount; ++BranchIndex)
		{
			if (ChainPoints.Num() < 2)
			{
				break;
			}
			const int32 StartIndex = Rng.RandRange(0, ChainPoints.Num() - 1);
			const FVector BranchDirection = Rng.GetUnitVector();

			TArray<FVector> BranchPoints;
			GrowChain(Runtime->Segments, BranchPoints, Rng, ChainPoints[StartIndex],
				BranchDirection, TargetLength * 0.5, InVeinRadius, InRadiusJitter,
				SegmentLength, InDirectionJitter);
		}
	}

	float MaxBoundingRadius = 0.f;
	for (const FVeinSegment& Segment : Runtime->Segments)
	{
		MaxBoundingRadius = FMath::Max(MaxBoundingRadius, Segment.BoundingRadius);
	}
	Runtime->CellSize = double(MaxBoundingRadius);
	for (int32 SegmentIndex = 0; SegmentIndex < Runtime->Segments.Num(); ++SegmentIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Segments[SegmentIndex].Bounds,
			Runtime->CellSize, Runtime->Index, SegmentIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
```

- [ ] **Step 4: 加 BOM 并构建**

```bash
Source/StellarEraGeology/Public/PlanetGeologyVeinDistribution.h
Source/StellarEraGeology/Private/PlanetGeologyVeinDistribution.cpp
Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

构建后运行 `Automation RunTests StellarEra.Planet.Geology.Vein`，预期 2 条通过。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyVeinDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyVeinDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
git commit -m "feat(地质): 脉状分布生成器（折线骨架 + 胶囊）" -- \
  Source/StellarEraGeology/Public/PlanetGeologyVeinDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyVeinDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

---

## Task 5: 浸染 / 稀疏散布生成器

**Files:**
- Create: `Source/StellarEraGeology/Public/PlanetGeologyDisseminatedDistribution.h`
- Create: `Source/StellarEraGeology/Private/PlanetGeologyDisseminatedDistribution.cpp`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp`

**Interfaces:**
- Consumes: Task 1 工具、Task 3 的 `CountBruteForceMismatches` / `CountOreOutsideAllRegions`。
- Produces: `UPlanetGeologyDisseminatedDistribution`（`GrainCount / GrainRadius / RadiusJitter / DepthBias / DepthCenter / DepthSpread`）。

- [ ] **Step 1: 写失败测试**

在 `PlanetGeologyDistributionTests.cpp` 追加（include 区加 `#include "PlanetGeologyDisseminatedDistribution.h"`）：

```cpp
namespace PlanetGeologyDistributionTest
{
	UPlanetGeologyDisseminatedDistribution* AddDisseminated(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element, int32 GrainCount, float GrainRadius)
	{
		UPlanetGeologyDisseminatedDistribution* Disseminated =
			NewObject<UPlanetGeologyDisseminatedDistribution>(&Preset);
		Disseminated->GrainCount = GrainCount;
		Disseminated->GrainRadius = GrainRadius;
		Disseminated->RadiusJitter = 0.5f;
		Disseminated->DepthBias = 0.5f;
		Disseminated->DepthCenter = 0.15f;
		Disseminated->DepthSpread = 0.08f;
		Disseminated->Seed = 606;
		Disseminated->bSeedExplicit = true;
		Element.Distributions.Add(Disseminated);
		return Disseminated;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDisseminatedCountsTest,
	"StellarEra.Planet.Geology.Disseminated.Counts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDisseminatedCountsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	constexpr int32 GrainCount = 320;
	constexpr float GrainRadius = 0.0025f;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("titanium"));
	AddDisseminated(*Preset, Element, GrainCount, GrainRadius);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);
	if (!TestEqual(TEXT("区域数 == GrainCount"), Regions.Num(), GrainCount))
	{
		return false;
	}

	const float MinRadius = GrainRadius * 0.5f;
	const float MaxRadius = GrainRadius * 1.5f;
	int32 OutOfRange = 0;
	int32 OutsideSphere = 0;
	for (const FPlanetGeologyRegion& Region : Regions)
	{
		if (Region.CharacteristicRadius < MinRadius || Region.CharacteristicRadius > MaxRadius)
		{
			++OutOfRange;
		}
		if (Region.Bounds.GetCenter().Size() > 1.0 + 1e-4)
		{
			++OutsideSphere;
		}
	}
	TestEqual(TEXT("颗粒半径都在抖动范围内"), OutOfRange, 0);
	TestEqual(TEXT("没有颗粒中心越出球体"), OutsideSphere, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyDisseminatedBruteForceTest,
	"StellarEra.Planet.Geology.Disseminated.BruteForceAgrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyDisseminatedBruteForceTest::RunTest(const FString& Parameters)
{
	using namespace PlanetGeologyDistributionTest;

	UPlanetGeologyPreset* Preset = MakePreset();
	FPlanetGeologyElementParams& Element = AddElement(*Preset, TEXT("titanium"));
	AddDisseminated(*Preset, Element, 320, 0.0025f);

	const auto Field = MakeField(*Preset);
	TArray<FPlanetGeologyRegion> Regions;
	Field->EnumerateRegions(0, Regions);

	FRandomStream Rng(16180);
	TestEqual(TEXT("索引路径 == 暴力路径"),
		CountBruteForceMismatches(*this, *Field, 0, Regions, Rng), 0);
	TestEqual(TEXT("包围盒盖住全部真实矿体"), CountOreOutsideAllRegions(*Field, 0, Regions, Rng), 0);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

预期：`PlanetGeologyDisseminatedDistribution.h` 不存在，编译失败。

- [ ] **Step 3: 写实现**

创建 `Public/PlanetGeologyDisseminatedDistribution.h`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.
// 浸染 / 稀疏散布：大量小颗粒，单颗不成片，整体是"星星点点"。

#pragma once

#include "CoreMinimal.h"
#include "PlanetGeologyDistribution.h"
#include "PlanetGeologyDisseminatedDistribution.generated.h"

/**
 * 浸染状分布。
 *
 * 成本随 GrainCount 线性涨：默认 512 粒约 110 万观测视图角点。
 * 想更密先加 GrainRadius（角点 ∝ 粒径²），再加数量。
 */
UCLASS(DisplayName = "浸染（散布）", EditInlineNew)
class STELLARERAGEOLOGY_API UPlanetGeologyDisseminatedDistribution
	: public UPlanetGeologyDistribution
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0", ClampMax = "8192"))
	int32 GrainCount = 512;

	/** 颗粒半径（占行星基准半径）。 */
	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0.0005", ClampMax = "0.02"))
	float GrainRadius = 0.002f;

	UPROPERTY(EditAnywhere, Category = "Grains", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RadiusJitter = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float DepthBias = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DepthCenter = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Grains|Depth", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float DepthSpread = 0.08f;

	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const override;
};
```

创建 `Private/PlanetGeologyDisseminatedDistribution.cpp`：

```cpp
// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetGeologyDisseminatedDistribution.h"

namespace
{
	struct FGrain
	{
		FVector Center = FVector::ZeroVector;
		float Radius = 0.f;
		FBox Bounds = FBox(ForceInit);
	};

	struct FDisseminatedRuntime final : public FPlanetGeologyDistributionRuntime
	{
		TArray<FGrain> Grains;
		TMap<FIntVector, TArray<int32>> Index;
		double CellSize = 0.0;
		int32 IndexReferences = 0;
		FPlanetGeologyDistributionNoise Noise;

		/** 与囊状同一个二次衰减：中心 1，表面 0。 */
		static float GrainInfluence(const FGrain& Grain, const FVector& P)
		{
			const float RadiusSquared = Grain.Radius * Grain.Radius;
			if (RadiusSquared <= 0.f)
			{
				return 0.f;
			}
			const float DistanceSquared = (P - Grain.Center).SizeSquared();
			if (DistanceSquared >= RadiusSquared)
			{
				return 0.f;
			}
			return 1.f - DistanceSquared / RadiusSquared;
		}

		float EvaluateNormalized(const FVector& P) const override
		{
			if (Grains.Num() == 0 || CellSize <= 0.0)
			{
				return 0.f;
			}

			float Best = 0.f;
			if (const TArray<int32>* Refs = Index.Find(PlanetGeologyToCell(P, CellSize)))
			{
				for (const int32 GrainIndex : *Refs)
				{
					Best = FMath::Max(Best, GrainInfluence(Grains[GrainIndex], P));
					if (Best >= 1.f)
					{
						break;
					}
				}
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		float EvaluateBruteForce(const FVector& P) const override
		{
			float Best = 0.f;
			for (const FGrain& Grain : Grains)
			{
				Best = FMath::Max(Best, GrainInfluence(Grain, P));
			}
			if (Best <= 0.f)
			{
				return 0.f;
			}
			return Best * Noise.EvaluateVein(P);
		}

		void EnumerateRegions(TArray<FPlanetGeologyRegion>& Out) const override
		{
			for (const FGrain& Grain : Grains)
			{
				FPlanetGeologyRegion& Region = Out.AddDefaulted_GetRef();
				Region.Bounds = Grain.Bounds;
				Region.CharacteristicRadius = Grain.Radius;
			}
		}

		bool IsEmpty() const override { return Grains.Num() == 0; }
		int32 NumPrimitives() const override { return Grains.Num(); }
		const TCHAR* GetDebugName() const override { return TEXT("Disseminated"); }

		FString GetDebugSummary() const override
		{
			const double AverageCandidates = Index.Num() > 0
				? double(IndexReferences) / double(Index.Num())
				: 0.0;
			return FString::Printf(TEXT("格边=%.4f 平均每格候选=%.2f"), CellSize, AverageCandidates);
		}
	};
}

TSharedPtr<const FPlanetGeologyDistributionRuntime>
UPlanetGeologyDisseminatedDistribution::BuildRuntime(
	const FPlanetGeologyDistributionBuildContext& Context) const
{
	const int32 ResolvedSeed = ResolveSeed(Context.ElementId, Context.DistributionIndex);

	const int32 InGrainCount = FMath::Clamp(GrainCount, 0, 8192);
	const double InGrainRadius = FMath::Clamp(double(GrainRadius), 0.0005, 0.02);
	const double InRadiusJitter = FMath::Clamp(double(RadiusJitter), 0.0, 1.0);

	TSharedPtr<FDisseminatedRuntime> Runtime = MakeShared<FDisseminatedRuntime>();
	Runtime->Noise.Init(ResolvedSeed, float(InGrainRadius), ShapeDetail, Octaves,
		Lacunarity, Gain, VeinThreshold);
	if (InGrainCount == 0)
	{
		return Runtime;
	}

	FRandomStream Rng(ResolvedSeed);
	Runtime->Grains.Reserve(InGrainCount);
	float MaxRadius = 0.f;

	for (int32 GrainIndex = 0; GrainIndex < InGrainCount; ++GrainIndex)
	{
		const FVector Center = SamplePlanetGeologyPlacement(
			Rng, DepthBias, DepthCenter, DepthSpread);
		const double RadiusScale = 1.0 + InRadiusJitter * (2.0 * double(Rng.FRand()) - 1.0);

		FGrain& Grain = Runtime->Grains.AddDefaulted_GetRef();
		Grain.Center = Center;
		Grain.Radius = float(FMath::Clamp(InGrainRadius * RadiusScale, 0.0005, 0.02));
		Grain.Bounds = FBox(Center - FVector(Grain.Radius), Center + FVector(Grain.Radius));
		MaxRadius = FMath::Max(MaxRadius, Grain.Radius);
	}

	Runtime->CellSize = double(MaxRadius);
	for (int32 GrainIndex = 0; GrainIndex < Runtime->Grains.Num(); ++GrainIndex)
	{
		PlanetGeologyIndexBounds(Runtime->Grains[GrainIndex].Bounds,
			Runtime->CellSize, Runtime->Index, GrainIndex, Runtime->IndexReferences);
	}
	return Runtime;
}
```

- [ ] **Step 4: 加 BOM 并构建**

```bash
Source/StellarEraGeology/Public/PlanetGeologyDisseminatedDistribution.h
Source/StellarEraGeology/Private/PlanetGeologyDisseminatedDistribution.cpp
Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

构建后运行 `Automation RunTests StellarEra.Planet.Geology.Disseminated`，预期 2 条通过。

- [ ] **Step 5: 提交**

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyDisseminatedDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDisseminatedDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
git commit -m "feat(地质): 浸染/稀疏散布分布生成器" -- \
  Source/StellarEraGeology/Public/PlanetGeologyDisseminatedDistribution.h \
  Source/StellarEraGeology/Private/PlanetGeologyDisseminatedDistribution.cpp \
  Source/StellarEraGeology/Private/PlanetGeologyDistributionTests.cpp
```

---

## Task 6: 观测视图接入区域化 + 空间哈希分簇 + 改名

**Files:**
- Modify: `Source/StellarEraMinerals/Public/PlanetMineralSurface.h`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp`
- Modify: `Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h:92-99`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp:72-130`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp`
- Modify: `Source/StellarEraMinerals/Private/PlanetMineralViewTests.cpp`
- Modify: `Config/DefaultEngine.ini:152`

**Interfaces:**
- Consumes: `FPlanetGeologyField::EnumerateRegions`、`FPlanetGeologyRegion`、`PlanetGeologyToCell`。
- Produces: `PlanetMineralSurface::ClusterRegionBounds(const TArray<FBox>&, TArray<int32>&)`；`FBuildParams::CellsPerFeature`、`FCacheKey::CellsPerFeature`、`UPlanetMineralViewComponent::CellsPerFeature`。

- [ ] **Step 1: 写失败测试**

`PlanetMineralSurfaceTests.cpp`：include 区加

```cpp
#include "PlanetGeologyDisseminatedDistribution.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyVeinDistribution.h"
```

在测试 namespace 里追加：

```cpp
	/** 给已有元素挂一个显式生成器；PodCount 置 0，避免遗留兜底再插一批矿囊。 */
	template <typename TDistribution>
	TDistribution* AddDistribution(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		Element.PodCount = 0;
		TDistribution* Distribution = NewObject<TDistribution>(&Preset);
		Distribution->Seed = 2468;
		Distribution->bSeedExplicit = true;
		Distribution->VeinThreshold = 0.f;
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	bool BoundsOverlap(const FBox& A, const FBox& B)
	{
		return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X
			&& A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y
			&& A.Min.Z <= B.Max.Z && A.Max.Z >= B.Min.Z;
	}
```

追加新测试：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceRegionClusteringTest,
	"StellarEra.Planet.MineralSurface.RegionClustering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceRegionClusteringTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	FRandomStream Rng(123456);
	TArray<FBox> Bounds;
	for (int32 Index = 0; Index < 400; ++Index)
	{
		const FVector Center(Rng.FRandRange(-1000.f, 1000.f), Rng.FRandRange(-1000.f, 1000.f),
			Rng.FRandRange(-1000.f, 1000.f));
		const FVector Extent(Rng.FRandRange(1.f, 60.f), Rng.FRandRange(1.f, 60.f),
			Rng.FRandRange(1.f, 60.f));
		Bounds.Add(FBox(Center - Extent, Center + Extent));
	}
	// 三连相交：保证夹具里真的有需要传递合并的簇，不全是 400 个单点簇。
	Bounds.Add(FBox(FVector(0, 0, 0), FVector(10, 10, 10)));
	Bounds.Add(FBox(FVector(5, 5, 5), FVector(15, 15, 15)));
	Bounds.Add(FBox(FVector(12, 12, 12), FVector(20, 20, 20)));

	TArray<int32> ActualRoots;
	PlanetMineralSurface::ClusterRegionBounds(Bounds, ActualRoots);
	if (!TestEqual(TEXT("根数组长度 == 区域数"), ActualRoots.Num(), Bounds.Num()))
	{
		return false;
	}

	// O(N²) 参照实现：判据与 Build 相同（AABB 相交，含边界）。
	TArray<int32> Parents;
	Parents.SetNum(Bounds.Num());
	for (int32 Index = 0; Index < Parents.Num(); ++Index)
	{
		Parents[Index] = Index;
	}
	auto Find = [&Parents](int32 Index)
	{
		while (Parents[Index] != Index)
		{
			Parents[Index] = Parents[Parents[Index]];
			Index = Parents[Index];
		}
		return Index;
	};
	for (int32 A = 0; A < Bounds.Num(); ++A)
	{
		for (int32 B = A + 1; B < Bounds.Num(); ++B)
		{
			if (BoundsOverlap(Bounds[A], Bounds[B]))
			{
				const int32 RootA = Find(A);
				const int32 RootB = Find(B);
				if (RootA != RootB)
				{
					Parents[RootB] = RootA;
				}
			}
		}
	}

	TMap<int32, int32> MinIndexByRoot;
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		const int32 Root = Find(Index);
		int32& MinIndex = MinIndexByRoot.FindOrAdd(Root, Index);
		MinIndex = FMath::Min(MinIndex, Index);
	}
	TArray<int32> ReferenceRoots;
	ReferenceRoots.SetNum(Bounds.Num());
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		ReferenceRoots[Index] = MinIndexByRoot.FindChecked(Find(Index));
	}

	int32 Mismatches = 0;
	for (int32 Index = 0; Index < Bounds.Num(); ++Index)
	{
		Mismatches += ReferenceRoots[Index] == ActualRoots[Index] ? 0 : 1;
	}
	TestEqual(TEXT("空间哈希分簇 == O(N²) 参照"), Mismatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceClosedMeshAllFormsTest,
	"StellarEra.Planet.MineralSurface.ClosedMesh.AllForms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceClosedMeshAllFormsTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	Preset->Appearance.ColorThreshold = 0.05f;
	Preset->Appearance.FadeRange = 0.12f;
	Preset->Appearance.PurityForFullColor = 0.45f;

	FPlanetGeologyElementParams& LensElement = Preset->Elements.AddDefaulted_GetRef();
	LensElement.Id = TEXT("lens");
	LensElement.DisplayName = TEXT("片层");
	UPlanetGeologyLensDistribution* Lens =
		AddDistribution<UPlanetGeologyLensDistribution>(*Preset, LensElement);
	Lens->SeamCount = 4;
	Lens->SeamLength = 0.12f;
	Lens->SeamWidth = 0.06f;
	Lens->SeamThickness = 0.01f;
	Lens->SizeJitter = 0.f;
	Lens->DepthBias = 0.f;

	FPlanetGeologyElementParams& VeinElement = Preset->Elements.AddDefaulted_GetRef();
	VeinElement.Id = TEXT("vein");
	VeinElement.DisplayName = TEXT("脉");
	UPlanetGeologyVeinDistribution* Vein =
		AddDistribution<UPlanetGeologyVeinDistribution>(*Preset, VeinElement);
	Vein->VeinCount = 2;
	Vein->VeinLength = 0.3f;
	Vein->VeinRadius = 0.01f;
	Vein->RadiusJitter = 0.f;
	Vein->BranchCount = 0;
	Vein->DepthBias = 0.f;

	FPlanetGeologyElementParams& GrainElement = Preset->Elements.AddDefaulted_GetRef();
	GrainElement.Id = TEXT("grain");
	GrainElement.DisplayName = TEXT("浸染");
	UPlanetGeologyDisseminatedDistribution* Disseminated =
		AddDistribution<UPlanetGeologyDisseminatedDistribution>(*Preset, GrainElement);
	Disseminated->GrainCount = 24;
	Disseminated->GrainRadius = 0.006f;
	Disseminated->RadiusJitter = 0.f;
	Disseminated->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);
	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	if (!TestEqual(TEXT("三个元素各一块网格"), Result.MeshesByElement.Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("三种形态都出了面"), CountNonEmptyMeshes(Result), 3);

	for (int32 ElementIndex = 0; ElementIndex < Result.MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Mesh = Result.MeshesByElement[ElementIndex];
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
		TestEqual(*FString::Printf(TEXT("元素 %d（形态夹具）网格封闭（开放边 %d）"),
			ElementIndex, OpenEdges), OpenEdges, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralSurfaceGridCapTest,
	"StellarEra.Planet.MineralSurface.GridCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralSurfaceGridCapTest::RunTest(const FString& Parameters)
{
	using namespace PlanetMineralSurfaceTest;

	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	Preset->Appearance.ColorThreshold = 0.05f;
	Preset->Appearance.FadeRange = 0.12f;
	Preset->Appearance.PurityForFullColor = 0.45f;

	FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
	Element.Id = TEXT("longvein");
	Element.DisplayName = TEXT("长脉");
	UPlanetGeologyVeinDistribution* Vein =
		AddDistribution<UPlanetGeologyVeinDistribution>(*Preset, Element);
	Vein->VeinCount = 1;
	Vein->VeinLength = 1.0f;      // 单簇可跨整个球：必然撞 512 格/轴上限
	Vein->VeinRadius = 0.008f;
	Vein->RadiusJitter = 0.f;
	Vein->BranchCount = 0;
	Vein->DepthBias = 0.f;

	const auto Field = MakeField(*Preset);
	PlanetMineralSurface::FBuildResult Result;
	PlanetMineralSurface::Build(*Field, MakeParams(*Field, 12), Result);

	if (!TestEqual(TEXT("一个元素一块网格"), Result.MeshesByElement.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("触发格数上限后仍然有网格（不跳簇）"),
		!Result.MeshesByElement[0].IsEmpty());
	TestTrue(TEXT("确实求过场"), Result.GridPoints > 0);
	return true;
}
```

`PlanetMineralViewTests.cpp`：include 区加 `#include "PlanetMineralViewComponent.h"`，追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetMineralViewCellsPerFeatureTest,
	"StellarEra.Planet.MineralView.CellsPerFeature",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetMineralViewCellsPerFeatureTest::RunTest(const FString& Parameters)
{
	const UPlanetMineralViewComponent* Defaults = GetDefault<UPlanetMineralViewComponent>();
	TestNotNull(TEXT("组件 CDO"), Defaults);
	if (!Defaults)
	{
		return false;
	}
	TestEqual(TEXT("默认每特征 12 格"), Defaults->CellsPerFeature, 12);

	PlanetMineralSurface::FCacheKey Key;
	Key.CellsPerFeature = 12;
	PlanetMineralSurface::FCacheKey Same = Key;
	TestTrue(TEXT("同分辨率键相等"), Key == Same);

	Same.CellsPerFeature = 24;
	TestTrue(TEXT("改分辨率使键不等"), Key != Same);
	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

预期：`ClusterRegionBounds`、`CellsPerFeature`、`CellsPerFeature` 组件字段都不存在，编译失败。

- [ ] **Step 3: 改 `PlanetMineralSurface.h`**

1. `FBuildParams::CellsPerPod` 整块替换为：

```cpp
		/**
		 * 一个**特征尺度**（片层=厚度、脉=管半径、浸染=粒径、囊=囊半径）横向切多少格
		 * （4..32）。
		 *
		 * 这个旋钮跟着生成器的特征尺度走，与行星直径无关 —— 换一颗大行星，
		 * 相对尺寸与观测视图清晰度都不变。
		 */
		int32 CellsPerFeature = 12;
```

2. `FCacheKey::CellsPerPod` → `int32 CellsPerFeature = 0;`，`operator==` 里同步改名。

3. `namespace PlanetMineralSurface` 里在 `Build` 声明之前插入：

```cpp
	/**
	 * 按 AABB 相交把区域分簇（空间哈希 + 每格 sweep-and-prune）。
	 *
	 * OutClusterRoots[i] = 区域 i 所属簇的代表，取**簇内最小区域下标**。
	 * 这样两次构建的簇顺序由数据决定，而不是 TMap 的哈希顺序。
	 *
	 * 不能把「同格」直接当相交：两个小区域可以同格却相距很远，直接 union 会把
	 * 不相交的矿体并成一个大簇，簇盒变大 → 角点数暴涨。
	 */
	STELLARERAMINERALS_API void ClusterRegionBounds(const TArray<FBox>& BoundsCm,
		TArray<int32>& OutClusterRoots);
```

4. `Build` 的文档注释把「按矿囊的空间邻近关系分簇（用未外扩的 AABB 判相交、并查集合并）」
   改成「按区域的空间邻近关系分簇（未外扩的 AABB 相交即同簇，空间哈希 + 并查集）」，
   并把「基准格边 = 2 × 簇内最小矿囊半径 / CellsPerPod」改成
   「基准格边 = 2 × 簇内最小特征半径 / CellsPerFeature」。其余（两条硬上限、绝不跳簇、
   径向夹回）原文保留。

- [ ] **Step 4: 改 `PlanetMineralSurface.cpp`**

1. `FPodInstance` 整块替换为：

```cpp
	/** 一个待处理的区域（厘米空间）。 */
	struct FRegionInstance
	{
		int32 ElementIndex = INDEX_NONE;
		FBox BoundsCm = FBox(ForceInit);
		float RadiusCm = 0.f;
	};
```

2. 在 `Build` 之前、匿名 namespace 之后插入 `ClusterRegionBounds`：

```cpp
void PlanetMineralSurface::ClusterRegionBounds(const TArray<FBox>& BoundsCm,
	TArray<int32>& OutClusterRoots)
{
	OutClusterRoots.Reset();
	const int32 RegionCount = BoundsCm.Num();
	if (RegionCount == 0)
	{
		return;
	}
	OutClusterRoots.SetNum(RegionCount);
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		OutClusterRoots[Index] = Index; // 尚未合并时每个区域自成一簇
	}

	// 格边 = 最大轴长。相交对至少共享一个点，该点落在一格里，两边的包围盒都覆盖
	// 那一格 —— 所以只要把「同格」再逐个判相交，就不可能漏 union。
	double CellSize = 0.0;
	for (const FBox& Bounds : BoundsCm)
	{
		CellSize = FMath::Max(CellSize, double(Bounds.GetSize().GetMax()));
	}
	if (CellSize <= 0.0)
	{
		return;
	}

	FDisjointSet Clusters;
	Clusters.Init(RegionCount);

	TMap<FIntVector, TArray<int32>> Grid;
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		const FIntVector MinCell = PlanetGeologyToCell(BoundsCm[Index].Min, CellSize);
		const FIntVector MaxCell = PlanetGeologyToCell(BoundsCm[Index].Max, CellSize);
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
		{
			for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
			{
				for (int32 Z = MinCell.Z; Z <= MaxCell.Z; ++Z)
				{
					Grid.FindOrAdd(FIntVector(X, Y, Z)).Add(Index);
				}
			}
		}
	}

	for (TPair<FIntVector, TArray<int32>>& Pair : Grid)
	{
		TArray<int32>& Members = Pair.Value;
		Members.Sort([&BoundsCm](const int32 A, const int32 B)
		{
			return BoundsCm[A].Min.X < BoundsCm[B].Min.X;
		});

		for (int32 A = 0; A < Members.Num(); ++A)
		{
			for (int32 B = A + 1; B < Members.Num(); ++B)
			{
				// 按 Min.X 升序：一旦越过 A 的 Max.X，后面的都不可能与 A 相交。
				if (BoundsCm[Members[B]].Min.X > BoundsCm[Members[A]].Max.X)
				{
					break;
				}
				if (AreAabbsOverlapping(BoundsCm[Members[A]], BoundsCm[Members[B]]))
				{
					Clusters.Union(Members[A], Members[B]);
				}
			}
		}
	}

	// 根归一成簇内最小下标：TMap 迭代顺序不是机制，不归一化的话两次构建的
	// 簇序（进而顶点顺序）会跟着哈希走。
	TMap<int32, int32> MinIndexByRoot;
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		const int32 Root = Clusters.Find(Index);
		int32& MinIndex = MinIndexByRoot.FindOrAdd(Root, Index);
		MinIndex = FMath::Min(MinIndex, Index);
	}
	for (int32 Index = 0; Index < RegionCount; ++Index)
	{
		OutClusterRoots[Index] = MinIndexByRoot.FindChecked(Clusters.Find(Index));
	}
}
```

3. `Build` 开头：`Params.CellsPerPod < 4` → `Params.CellsPerFeature < 4`。

4. 第 1-2 段（收集矿囊 + 并查集分簇）整块替换：

```cpp
	// --- 1. 收集所有区域（厘米空间） ---
	TArray<FRegionInstance> Regions;
	for (int32 ElementIndex = 0; ElementIndex < ElementCount; ++ElementIndex)
	{
		TArray<FPlanetGeologyRegion> ElementRegions;
		Field.EnumerateRegions(ElementIndex, ElementRegions);
		for (const FPlanetGeologyRegion& Region : ElementRegions)
		{
			if (Region.CharacteristicRadius <= 0.f || Region.Bounds.IsValid == 0)
			{
				continue;
			}
			FRegionInstance& Instance = Regions.AddDefaulted_GetRef();
			Instance.ElementIndex = ElementIndex;
			Instance.BoundsCm = FBox(
				Region.Bounds.Min * RadiusCm, Region.Bounds.Max * RadiusCm);
			Instance.RadiusCm = Region.CharacteristicRadius * float(RadiusCm);
		}
	}
	if (Regions.Num() == 0)
	{
		Out.BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
		return;
	}

	TArray<FBox> RegionBounds;
	RegionBounds.Reserve(Regions.Num());
	for (const FRegionInstance& Region : Regions)
	{
		RegionBounds.Add(Region.BoundsCm);
	}

	// --- 2. 分簇：未外扩的 AABB 相交即同簇（空间哈希 + sweep-and-prune） ---
	TArray<int32> ClusterRoots;
	PlanetMineralSurface::ClusterRegionBounds(RegionBounds, ClusterRoots);

	TMap<int32, TArray<int32>> MembersByRoot;
	for (int32 Index = 0; Index < Regions.Num(); ++Index)
	{
		MembersByRoot.FindOrAdd(ClusterRoots[Index]).Add(Index);
	}

	TArray<TArray<int32>> ClusterMembers;
	ClusterMembers.Reserve(MembersByRoot.Num());
	for (TPair<int32, TArray<int32>>& Pair : MembersByRoot)
	{
		ClusterMembers.Add(MoveTemp(Pair.Value));
	}
```

（后面的排序块原样保留，比较的仍是 `A[0] < B[0]`。）

5. 第 3 段里的引用全部换名：

- `Box += PodBounds[PodIndex];` → `Box += RegionBounds[RegionIndex];`
- `MinRadiusCm = FMath::Min(MinRadiusCm, Pods[PodIndex].RadiusCm);` → `... Regions[RegionIndex].RadiusCm);`
- `for (const int32 PodIndex : Members)` → `for (const int32 RegionIndex : Members)`
- `ElementSet.Add(Pods[PodIndex].ElementIndex);` → `ElementSet.Add(Regions[RegionIndex].ElementIndex);`
- `double(Params.CellsPerPod)` → `double(Params.CellsPerFeature)`
- 段首注释里的「矿囊」按语义替换成「区域」；两条硬上限与「绝不跳过」的说明原样保留。

`Build` 文档注释里提到 `PodInfluence` 的那段前提说明保留原文（囊状生成器的边界行为没变）。

- [ ] **Step 5: 改组件与缓存键**

`PlanetMineralViewComponent.h`：`CellsPerPod` 改为

```cpp
	/**
	 * 观测视图密度：一个**特征尺度**横向切多少格（片层=厚度、脉=管半径、
	 * 浸染=粒径、囊=囊半径）。
	 *
	 * 12 首进约 0.1–0.3 秒；24 边缘更圆，约 2–4 倍耗时。改动会让缓存失效。
	 */
	UPROPERTY(EditAnywhere, Category = "Planet|MineralView",
		meta = (ClampMin = "4", ClampMax = "32"))
	int32 CellsPerFeature = 12;
```

`PlanetMineralViewComponent.cpp`：

- `Params.CellsPerPod = CellsPerPod;` → `Params.CellsPerFeature = CellsPerFeature;`
- `Key.CellsPerPod = CellsPerPod;` → `Key.CellsPerFeature = CellsPerFeature;`
- 注释「CellsPerPod 是组件上可编辑的属性」→「CellsPerFeature 是组件上可编辑的属性」
- 日志 `"[矿物] %s：每囊 %d 格（%d 个角点）..."` → `"[矿物] %s：每特征 %d 格（%d 个角点）..."`，
  参数 `CellsPerPod` → `CellsPerFeature`。

- [ ] **Step 6: 全仓改名残留**

```bash
grep -rn "CellsPerPod" Source Config Content --include=*.h --include=*.cpp --include=*.ini
```

预期只剩 `Config/DefaultEngine.ini` 里即将新增的 CoreRedirect 那一条。其余全部改名。

- [ ] **Step 7: 加 CoreRedirect**

`Config/DefaultEngine.ini` 的 `[CoreRedirects]` 段末尾（`PlanetAtmosphereComponent` 那行之后）追加：

```ini
; 观测视图的「每囊 N 格」改名成「每特征 N 格」：BP_VoxelPlanet 上存着 32，
; 没有这条 redirect 会掉回默认 12。
+PropertyRedirects=(OldName="/Script/StellarEraMinerals.PlanetMineralViewComponent.CellsPerPod",NewName="/Script/StellarEraMinerals.PlanetMineralViewComponent.CellsPerFeature")
```

- [ ] **Step 8: 改既有测试的字段名与测试名**

`PlanetMineralSurfaceTests.cpp`：

- `MakeParams(..., int32 CellsPerPod)` 的形参改 `CellsPerFeature`，函数体 `Params.CellsPerFeature = CellsPerFeature;`
- `ExpectedCellSizeCm(..., int32 CellsPerPod)` 同上，公式里 `double(CellsPerFeature)`
- `MakeParams(*Field, 4)` 等调用不用改
- `BadParams.CellsPerPod = 0;` → `BadParams.CellsPerFeature = 0;`，断言文案同步
- `Params.CellsPerPod`（IsoAccuracy / PodLocal）→ `Params.CellsPerFeature`
- `Base.CellsPerPod = 12; Resolution.CellsPerPod = 24;` → `CellsPerFeature`
- `constexpr int32 CellsPerPod = 12;`（ClampSmoke）→ `CellsPerFeature`，后续引用同步
- 结构体 `FPlanetMineralSurfaceCellsPerPodTest` → `FPlanetMineralSurfaceCellsPerFeatureTest`，
  字符串 `"StellarEra.Planet.MineralSurface.CellsPerPod"` → `"...CellsPerFeature"`
- `FPlanetMineralSurfacePodLocalTest` 的字符串 `"...MineralSurface.PodLocal"` → `"...MineralSurface.RegionLocal"`

- [ ] **Step 9: 加 BOM 并构建**

```bash
Source/StellarEraMinerals/Public/PlanetMineralSurface.h
Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp
Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h
Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp
Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp
Source/StellarEraMinerals/Private/PlanetMineralViewTests.cpp
```

构建后运行：

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra.Planet.Mineral" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_Mineral.log"
```

预期全绿。`ClosedMesh`（矿囊版）与新的 `ClosedMesh.AllForms` 都必须过；后者失败通常意味着
某形态的包围盒算小了（区域没覆盖满基元）。

- [ ] **Step 10: 提交**

```bash
git add -- Source/StellarEraMinerals/Public/PlanetMineralSurface.h \
  Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp \
  Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h \
  Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp \
  Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp \
  Source/StellarEraMinerals/Private/PlanetMineralViewTests.cpp \
  Config/DefaultEngine.ini
git commit -m "feat(矿物): 观测视图改用分布区域 + 空间哈希分簇，CellsPerPod 改名" -- \
  Source/StellarEraMinerals/Public/PlanetMineralSurface.h \
  Source/StellarEraMinerals/Private/PlanetMineralSurface.cpp \
  Source/StellarEraMinerals/Public/PlanetMineralViewComponent.h \
  Source/StellarEraMinerals/Private/PlanetMineralViewComponent.cpp \
  Source/StellarEraMinerals/Private/PlanetMineralSurfaceTests.cpp \
  Source/StellarEraMinerals/Private/PlanetMineralViewTests.cpp \
  Config/DefaultEngine.ini
```

---

## Task 7: 资产侧迁移 + 地球初始数据 + 测试夹具切换

**Files:**
- Modify: `Source/StellarEraGeology/Public/PlanetGeologyPreset.h`
- Modify: `Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp`
- Modify: `Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp:24-35`
- Modify: `Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp`
- Modify: `Content/Geology/README.md`
- Modify: `CLAUDE.md`
- Regenerate: `Content/Geology/Planets/DA_Geology_Earth.uasset`

**Interfaces:**
- Consumes: Task 2 的 `UPlanetGeologySpherePodsDistribution`、Task 3–5 的三种生成器。
- Produces: `UPlanetGeologyPreset::MigrateLegacyElement(FPlanetGeologyElementParams&, UObject*)`、`UPlanetGeologyPreset::PostLoad()`。

- [ ] **Step 1: 写失败测试**

`PlanetGeologyPodTests.cpp`：把 `MakeElement` 整块替换为显式生成器夹具（spec §10.1「保留但换夹具」）：

```cpp
	FPlanetGeologyElementParams MakeElement(FName Id, int32 PodCount, float PodRadius,
		int32 Seed, float VeinThreshold = 0.0f)
	{
		FPlanetGeologyElementParams Params;
		Params.Id = Id;
		Params.DisplayName = Id.ToString();
		// 显式囊状生成器：遗留字段不再被生产路径消费，留着只会掩盖回归。
		Params.PodCount = 0;

		UPlanetGeologySpherePodsDistribution* Distribution =
			NewObject<UPlanetGeologySpherePodsDistribution>(GetTransientPackage());
		Distribution->PodCount = PodCount;
		Distribution->PodRadius = PodRadius;
		Distribution->PodRadiusJitter = 0.5f;
		Distribution->PodDepthBias = 0.f;
		Distribution->PodDepthCenter = 0.25f;
		Distribution->PodDepthSpread = 0.15f;
		Distribution->ShapeDetail = 3.f;
		Distribution->Octaves = 2;
		Distribution->Lacunarity = 2.f;
		Distribution->Gain = 0.5f;
		Distribution->VeinThreshold = VeinThreshold;
		// 夹具里的 Seed 都是非 0，bSeedExplicit 与「非 0 直接使用」等价。
		Distribution->Seed = Seed;
		Distribution->bSeedExplicit = true;

		Params.Distributions.Add(Distribution);
		return Params;
	}
```

include 区加 `#include "PlanetGeologySpherePodsDistribution.h"`。

`PlanetGeologyPresetTests.cpp`：

1. `PresetDefaults` 里补一条（其余断言不动，遗留字段仍保留默认值）：

```cpp
	TestEqual(TEXT("默认没有生成器"), Defaults.Distributions.Num(), 0);
```

2. 在文件末尾（`#endif` 之前）追加：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyLegacyMigrationFidelityTest,
	"StellarEra.Planet.Geology.LegacyMigrationFidelity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyLegacyMigrationFidelityTest::RunTest(const FString& Parameters)
{
	// 旧资产形态：只有遗留字段。
	FPlanetGeologyElementParams Legacy;
	Legacy.Id = TEXT("iron");
	Legacy.DisplayName = TEXT("铁");
	Legacy.Abundance = 0.75f;
	Legacy.PodCount = 20;
	Legacy.PodRadius = 0.05f;
	Legacy.Seed = 0; // 派生路径最能暴露「新旧种子公式不同」
	Legacy.VeinThreshold = 0.5f;

	UPlanetGeologyPreset* LegacyPreset = NewObject<UPlanetGeologyPreset>();
	LegacyPreset->Elements.Add(Legacy);
	const auto LegacyField = FPlanetGeologyField::BuildFromPreset(*LegacyPreset, 40000.0);

	// 迁移后的资产形态：显式囊状生成器。
	UPlanetGeologyPreset* MigratedPreset = NewObject<UPlanetGeologyPreset>();
	MigratedPreset->Elements.Add(Legacy);
	UPlanetGeologyPreset::MigrateLegacyElement(MigratedPreset->Elements[0], MigratedPreset);

	if (!TestEqual(TEXT("迁移后一个元素一个生成器"),
		MigratedPreset->Elements[0].Distributions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("PodCount 置 0 作已迁移标记"), MigratedPreset->Elements[0].PodCount, 0);

	const UPlanetGeologySpherePodsDistribution* Migrated =
		Cast<UPlanetGeologySpherePodsDistribution>(MigratedPreset->Elements[0].Distributions[0]);
	if (!TestNotNull(TEXT("迁移目标是囊状生成器"), Migrated))
	{
		return false;
	}
	TestEqual(TEXT("种子 = 旧公式"), Migrated->Seed,
		int32(GetTypeHash(Legacy.Id) & 0x7fffffffu));

	const auto MigratedField = FPlanetGeologyField::BuildFromPreset(*MigratedPreset, 40000.0);
	TestTrue(TEXT("迁移前后 IsEmpty 一致"),
		LegacyField->IsEmpty() == MigratedField->IsEmpty());

	int32 Mismatches = 0;
	int32 HitPoints = 0;
	auto ComparePoint = [&](const FVector& P)
	{
		const float A = LegacyField->SampleElementNormAtNormalized(0, P);
		const float B = MigratedField->SampleElementNormAtNormalized(0, P);
		Mismatches += FMath::IsNearlyEqual(A, B, 1e-6f) ? 0 : 1;
		HitPoints += A > 0.f ? 1 : 0;
	};

	FRandomStream Rng(5150);
	for (int32 SampleIndex = 0; SampleIndex < 4000; ++SampleIndex)
	{
		const FVector P(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
			Rng.FRandRange(-1.f, 1.f));
		if (P.SizeSquared() <= 1.f)
		{
			ComparePoint(P);
		}
	}
	// 随机点几乎全在矿囊外，补一圈囊内探针，避免 0 == 0 的空转。
	for (const FPlanetGeologyPod& Pod : LegacyField->GetPods(0))
	{
		for (int32 SampleIndex = 0; SampleIndex < 32; ++SampleIndex)
		{
			const FVector Offset(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f),
				Rng.FRandRange(-1.f, 1.f));
			const FVector P = Pod.Center + Offset * Pod.Radius * 1.2f;
			if (P.SizeSquared() <= 1.f)
			{
				ComparePoint(P);
			}
		}
	}

	TestEqual(TEXT("迁移前后逐点相等"), Mismatches, 0);
	TestTrue(*FString::Printf(TEXT("探针确实命中矿（%d 点）"), HitPoints), HitPoints > 0);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyPresetNestedEditTest,
	"StellarEra.Planet.Geology.PresetRevision.NestedEdit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyPresetNestedEditTest::RunTest(const FString& Parameters)
{
	UPlanetGeologyPreset* Preset = NewObject<UPlanetGeologyPreset>();
	FPlanetGeologyElementParams& Element = Preset->Elements.AddDefaulted_GetRef();
	Element.Id = TEXT("iron");

	UPlanetGeologyLensDistribution* Lens = NewObject<UPlanetGeologyLensDistribution>(Preset);
	Element.Distributions.Add(Lens);

	int32 Broadcasts = 0;
	Preset->OnPresetChanged.AddLambda(
		[&Broadcasts](UPlanetGeologyPreset*) { ++Broadcasts; });
	const int32 Before = Preset->GetRevision();

	FPropertyChangedEvent Event(UPlanetGeologyLensDistribution::StaticClass()
		->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UPlanetGeologyLensDistribution, SeamCount)));
	Lens->PostEditChangeProperty(Event);

	TestTrue(*FString::Printf(TEXT("嵌套编辑后修订号自增（%d → %d）"),
		Before, Preset->GetRevision()), Preset->GetRevision() > Before);
	TestEqual(TEXT("广播恰好一次"), Broadcasts, 1);
	return true;
}
#endif
```

include 区加：

```cpp
#include "PlanetGeologyField.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologySpherePodsDistribution.h"
```

3. `EarthPresetValues` 整块替换（新数据见 spec §8）：

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanetGeologyEarthValuesTest,
	"StellarEra.Planet.Geology.EarthPresetValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPlanetGeologyEarthValuesTest::RunTest(const FString& Parameters)
{
	const FString AssetPath = UPlanetGeologyPreset::MakeAssetPath(TEXT("earth")).ToString();
	UPlanetGeologyPreset* Preset = LoadObject<UPlanetGeologyPreset>(nullptr, *AssetPath);
	if (!TestNotNull(*FString::Printf(TEXT("能加载 %s"), *AssetPath), Preset))
	{
		return false;
	}

	const TCHAR* ExpectedIds[] = {
		TEXT("silicon"), TEXT("iron"), TEXT("water"),
		TEXT("copper"), TEXT("titanium"), TEXT("uranium"),
	};
	const float ExpectedAbundances[] = { 0.60f, 0.75f, 0.20f, 0.45f, 0.15f, 0.10f };
	const FLinearColor ExpectedColors[] = {
		FLinearColor(0.62f, 0.58f, 0.50f),
		FLinearColor(0.72f, 0.42f, 0.36f),
		FLinearColor(0.20f, 0.45f, 0.90f),
		FLinearColor(0.85f, 0.55f, 0.25f),
		FLinearColor(0.70f, 0.72f, 0.78f),
		FLinearColor(0.45f, 0.85f, 0.35f),
	};
	const int32 ElementCount = UE_ARRAY_COUNT(ExpectedIds);

	TestEqual(TEXT("6 个元素"), Preset->Elements.Num(), ElementCount);
	if (Preset->Elements.Num() != ElementCount)
	{
		return false;
	}

	for (int32 Index = 0; Index < ElementCount; ++Index)
	{
		const FPlanetGeologyElementParams& Element = Preset->Elements[Index];
		const FString Label = ExpectedIds[Index];

		TestEqual(*FString::Printf(TEXT("%s 的 Id 顺序"), *Label),
			Element.Id.ToString(), Label);
		TestEqual(*FString::Printf(TEXT("%s 的 Abundance"), *Label),
			Element.Abundance, ExpectedAbundances[Index]);
		TestEqual(*FString::Printf(TEXT("%s 的 Color"), *Label),
			Element.Color, ExpectedColors[Index]);
		// 遗留字段必须清零：留着就等于资产里有两套数据。
		TestEqual(*FString::Printf(TEXT("%s 的遗留 PodCount"), *Label), Element.PodCount, 0);
		TestEqual(*FString::Printf(TEXT("%s 一个生成器"), *Label),
			Element.Distributions.Num(), 1);
		if (Element.Distributions.Num() != 1)
		{
			continue;
		}
		TestNotNull(*FString::Printf(TEXT("%s 的生成器非空"), *Label),
			Element.Distributions[0].Get());
	}

	// 硅 / 水：片层
	{
		const auto* Lens = Cast<UPlanetGeologyLensDistribution>(
			Preset->Elements[0].Distributions[0]);
		if (TestNotNull(TEXT("硅是片层"), Lens))
		{
			TestEqual(TEXT("硅 16 片"), Lens->SeamCount, 16);
			TestEqual(TEXT("硅 A 0.14"), Lens->SeamLength, 0.14f);
			TestEqual(TEXT("硅 B 0.07"), Lens->SeamWidth, 0.07f);
			TestEqual(TEXT("硅 C 0.008"), Lens->SeamThickness, 0.008f);
			TestEqual(TEXT("硅深度中心 0.10"), Lens->DepthCenter, 0.10f);
			TestEqual(TEXT("硅深度偏置 0.5"), Lens->DepthBias, 0.5f);
			TestEqual(TEXT("硅 VeinThreshold 0.45"), Lens->VeinThreshold, 0.45f);
		}
		const auto* WaterLens = Cast<UPlanetGeologyLensDistribution>(
			Preset->Elements[2].Distributions[0]);
		if (TestNotNull(TEXT("水是片层"), WaterLens))
		{
			TestEqual(TEXT("水 14 片"), WaterLens->SeamCount, 14);
			TestEqual(TEXT("水 A 0.18"), WaterLens->SeamLength, 0.18f);
			TestEqual(TEXT("水 B 0.10"), WaterLens->SeamWidth, 0.10f);
			TestEqual(TEXT("水 C 0.010"), WaterLens->SeamThickness, 0.010f);
			TestEqual(TEXT("水深度中心 0.18"), WaterLens->DepthCenter, 0.18f);
			TestEqual(TEXT("水 VeinThreshold 0.40"), WaterLens->VeinThreshold, 0.40f);
		}
	}

	// 铁 / 铜：脉状
	{
		const auto* IronVein = Cast<UPlanetGeologyVeinDistribution>(
			Preset->Elements[1].Distributions[0]);
		if (TestNotNull(TEXT("铁是脉状"), IronVein))
		{
			TestEqual(TEXT("铁 5 条"), IronVein->VeinCount, 5);
			TestEqual(TEXT("铁长 0.40"), IronVein->VeinLength, 0.40f);
			TestEqual(TEXT("铁半径 0.009"), IronVein->VeinRadius, 0.009f);
			TestEqual(TEXT("铁 1 分叉"), IronVein->BranchCount, 1);
			TestEqual(TEXT("铁 VeinThreshold 0.50"), IronVein->VeinThreshold, 0.50f);
		}
		const auto* CopperVein = Cast<UPlanetGeologyVeinDistribution>(
			Preset->Elements[3].Distributions[0]);
		if (TestNotNull(TEXT("铜是脉状"), CopperVein))
		{
			TestEqual(TEXT("铜 3 条"), CopperVein->VeinCount, 3);
			TestEqual(TEXT("铜长 0.28"), CopperVein->VeinLength, 0.28f);
			TestEqual(TEXT("铜半径 0.007"), CopperVein->VeinRadius, 0.007f);
			TestEqual(TEXT("铜无分叉"), CopperVein->BranchCount, 0);
			TestEqual(TEXT("铜 VeinThreshold 0.55"), CopperVein->VeinThreshold, 0.55f);
		}
	}

	// 钛 / 铀：浸染
	{
		const auto* Titanium = Cast<UPlanetGeologyDisseminatedDistribution>(
			Preset->Elements[4].Distributions[0]);
		if (TestNotNull(TEXT("钛是浸染"), Titanium))
		{
			TestEqual(TEXT("钛 320 粒"), Titanium->GrainCount, 320);
			TestEqual(TEXT("钛粒径 0.0025"), Titanium->GrainRadius, 0.0025f);
			TestEqual(TEXT("钛 VeinThreshold 0.60"), Titanium->VeinThreshold, 0.60f);
		}
		const auto* Uranium = Cast<UPlanetGeologyDisseminatedDistribution>(
			Preset->Elements[5].Distributions[0]);
		if (TestNotNull(TEXT("铀是浸染"), Uranium))
		{
			TestEqual(TEXT("铀 140 粒"), Uranium->GrainCount, 140);
			TestEqual(TEXT("铀粒径 0.0015"), Uranium->GrainRadius, 0.0015f);
			TestEqual(TEXT("铀深度中心 0.35"), Uranium->DepthCenter, 0.35f);
			TestEqual(TEXT("铀深度偏置 0.6"), Uranium->DepthBias, 0.6f);
			TestEqual(TEXT("铀 VeinThreshold 0.65"), Uranium->VeinThreshold, 0.65f);
		}
	}

	TestEqual(TEXT("ColorThreshold"), Preset->Appearance.ColorThreshold, 0.05f);
	TestEqual(TEXT("FadeRange"), Preset->Appearance.FadeRange, 0.12f);
	TestEqual(TEXT("PurityForFullColor"), Preset->Appearance.PurityForFullColor, 0.45f);
	TestEqual(TEXT("GeometryAmplitude"), Preset->GeometryAmplitude, 1.5f);

	return true;
}
```

- [ ] **Step 2: 构建，确认失败**

预期：`MigrateLegacyElement` 不存在、`PlanetGeologyDisseminatedDistribution.h` 之类已存在但测试引用失败在 `MigrateLegacyElement` / 地球数据断言，编译或测试失败。

- [ ] **Step 3: 加迁移**

`PlanetGeologyPreset.h`：

1. UObject 接口区加：

```cpp
	//~ Begin UObject Interface
	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	//~ End UObject Interface
```

2. 在 `MakeAssetPath` 之后加：

```cpp
	/**
	 * 旧字段 → 显式囊状生成器。**只在 Distributions 为空且 PodCount > 0 时动作**。
	 *
	 * 种子必须是旧公式 `Seed != 0 ? Seed : Hash(Id) & 0x7fffffff`，不是生成器基类的
	 * 派生规则 —— 换了公式，旧行星的矿囊位置就全变了。
	 *
	 * PodCount 置 0 是「已迁移」标记：已保存的资产不会再迁，未保存的每次加载
	 * 都重建同一个生成器，结果一致。
	 */
	static void MigrateLegacyElement(FPlanetGeologyElementParams& Element, UObject* Outer);
```

3. 遗留字段的 `meta` 全部加 `DeprecatedProperty`（保留 `EditAnywhere` 与原有 Clamp）：

```cpp
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pods",
		meta = (ClampMin = "0", ClampMax = "512", DeprecatedProperty))
	int32 PodCount = 16;
```

同理：`PodRadius`、`PodRadiusJitter`、`PodDepthBias`、`PodDepthCenter`、`PodDepthSpread`、
`ShapeDetail`、`Octaves`、`Lacunarity`、`Gain`、`Seed`、`VeinThreshold`，各在自己的 meta 里追加一项。

`PlanetGeologyPreset.cpp`：

1. include 区加 `#include "PlanetGeologySpherePodsDistribution.h"`。
2. 在 `MakeAssetPath` 之后加：

```cpp
void UPlanetGeologyPreset::MigrateLegacyElement(FPlanetGeologyElementParams& Element,
	UObject* Outer)
{
	if (Element.Distributions.Num() > 0 || Element.PodCount <= 0)
	{
		return;
	}

	UPlanetGeologySpherePodsDistribution* Distribution =
		NewObject<UPlanetGeologySpherePodsDistribution>(Outer);
	Distribution->PodCount = Element.PodCount;
	Distribution->PodRadius = Element.PodRadius;
	Distribution->PodRadiusJitter = Element.PodRadiusJitter;
	Distribution->PodDepthBias = Element.PodDepthBias;
	Distribution->PodDepthCenter = Element.PodDepthCenter;
	Distribution->PodDepthSpread = Element.PodDepthSpread;
	Distribution->ShapeDetail = Element.ShapeDetail;
	Distribution->Octaves = Element.Octaves;
	Distribution->Lacunarity = Element.Lacunarity;
	Distribution->Gain = Element.Gain;
	Distribution->VeinThreshold = Element.VeinThreshold;
	// 旧公式。Seed=0 时也要锁定「派生后的值」，否则运行期会走新派生规则。
	Distribution->Seed = Element.Seed != 0
		? Element.Seed
		: int32(GetTypeHash(Element.Id) & 0x7fffffffu);
	Distribution->bSeedExplicit = true;

	Element.Distributions.Add(Distribution);
	Element.PodCount = 0;
}

void UPlanetGeologyPreset::PostLoad()
{
	Super::PostLoad();

	// 异步加载线程上不能建 UObject。跳过迁移不会改变行为：BuildFromPreset 的
	// 遗留兜底会按同一份旧字段建出同样的囊表；下次在编辑器里打开/保存时再迁移。
	if (!IsInGameThread())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[地质] %s 在非游戏线程加载，跳过旧字段迁移（运行期走遗留兜底）"),
			*GetPathName());
		return;
	}

	for (FPlanetGeologyElementParams& Element : Elements)
	{
		MigrateLegacyElement(Element, this);
	}
}
```

- [ ] **Step 4: 改地球初始数据**

`PlanetGeologyPreset.cpp` 的 `FillEarthPreset` 整块替换为：

```cpp
	template <typename TDistribution>
	TDistribution* AddEarthDistribution(UPlanetGeologyPreset& Preset,
		FPlanetGeologyElementParams& Element)
	{
		// 一次性夹具不要遗留字段：两套数据并存时读代码的人永远不知道哪套在生效。
		Element.PodCount = 0;
		TDistribution* Distribution = NewObject<TDistribution>(&Preset);
		Distribution->ShapeDetail = 3.0f;
		Distribution->Octaves = 2;
		Distribution->Lacunarity = 2.0f;
		Distribution->Gain = 0.5f;
		Element.Distributions.Add(Distribution);
		return Distribution;
	}

	void FillEarthPreset(UPlanetGeologyPreset& Preset)
	{
		auto AddElement = [&Preset](const TCHAR* Id, const TCHAR* DisplayName,
			const FLinearColor& Color, float Abundance)
		{
			FPlanetGeologyElementParams& Params = Preset.Elements.AddDefaulted_GetRef();
			Params.Id = Id;
			Params.DisplayName = DisplayName;
			Params.Color = Color;
			Params.Abundance = Abundance;
			return &Params;
		};

		Preset.Elements.Reset();

		// 硅：片层，近地表的薄板
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("silicon"), TEXT("硅"), FLinearColor(0.62f, 0.58f, 0.50f), 0.60f);
			UPlanetGeologyLensDistribution* Lens =
				AddEarthDistribution<UPlanetGeologyLensDistribution>(Preset, Element);
			Lens->SeamCount = 16;
			Lens->SeamLength = 0.14f;
			Lens->SeamWidth = 0.07f;
			Lens->SeamThickness = 0.008f;
			Lens->DepthCenter = 0.10f;
			Lens->DepthBias = 0.5f;
			Lens->VeinThreshold = 0.45f;
		}

		// 铁：脉状，穿切地层
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("iron"), TEXT("铁"), FLinearColor(0.72f, 0.42f, 0.36f), 0.75f);
			UPlanetGeologyVeinDistribution* Vein =
				AddEarthDistribution<UPlanetGeologyVeinDistribution>(Preset, Element);
			Vein->VeinCount = 5;
			Vein->VeinLength = 0.40f;
			Vein->VeinRadius = 0.009f;
			Vein->BranchCount = 1;
			Vein->VeinThreshold = 0.50f;
		}

		// 水：更深更宽的片层（含水层）
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("water"), TEXT("水"), FLinearColor(0.20f, 0.45f, 0.90f), 0.20f);
			UPlanetGeologyLensDistribution* Lens =
				AddEarthDistribution<UPlanetGeologyLensDistribution>(Preset, Element);
			Lens->SeamCount = 14;
			Lens->SeamLength = 0.18f;
			Lens->SeamWidth = 0.10f;
			Lens->SeamThickness = 0.010f;
			Lens->DepthCenter = 0.18f;
			Lens->DepthBias = 0.5f;
			Lens->VeinThreshold = 0.40f;
		}

		// 铜：少而短的脉
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("copper"), TEXT("铜"), FLinearColor(0.85f, 0.55f, 0.25f), 0.45f);
			UPlanetGeologyVeinDistribution* Vein =
				AddEarthDistribution<UPlanetGeologyVeinDistribution>(Preset, Element);
			Vein->VeinCount = 3;
			Vein->VeinLength = 0.28f;
			Vein->VeinRadius = 0.007f;
			Vein->BranchCount = 0;
			Vein->VeinThreshold = 0.55f;
		}

		// 钛：浸染，细颗粒铺开
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("titanium"), TEXT("钛"), FLinearColor(0.70f, 0.72f, 0.78f), 0.15f);
			UPlanetGeologyDisseminatedDistribution* Disseminated =
				AddEarthDistribution<UPlanetGeologyDisseminatedDistribution>(Preset, Element);
			Disseminated->GrainCount = 320;
			Disseminated->GrainRadius = 0.0025f;
			Disseminated->VeinThreshold = 0.60f;
		}

		// 铀：更深、更稀的浸染
		{
			FPlanetGeologyElementParams& Element = *AddElement(
				TEXT("uranium"), TEXT("铀"), FLinearColor(0.45f, 0.85f, 0.35f), 0.10f);
			UPlanetGeologyDisseminatedDistribution* Disseminated =
				AddEarthDistribution<UPlanetGeologyDisseminatedDistribution>(Preset, Element);
			Disseminated->GrainCount = 140;
			Disseminated->GrainRadius = 0.0015f;
			Disseminated->DepthCenter = 0.35f;
			Disseminated->DepthBias = 0.6f;
			Disseminated->VeinThreshold = 0.65f;
		}

		Preset.Appearance.ColorThreshold = 0.05f;
		Preset.Appearance.FadeRange = 0.12f;
		Preset.Appearance.PurityForFullColor = 0.45f;
		Preset.GeometryAmplitude = 1.5f;
	}
```

include 区加三个生成器头：

```cpp
#include "PlanetGeologyDisseminatedDistribution.h"
#include "PlanetGeologyLensDistribution.h"
#include "PlanetGeologyVeinDistribution.h"
```

- [ ] **Step 5: 重新生成地球资产**

构建通过后（编辑器必须关闭），跑一次控制台命令：

```bash
cd "C:/" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="StellarEra.CreateEarthGeologyPreset" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_CreateEarth.log"
```

验收：日志里出现

```
StellarEra.CreateEarthGeologyPreset: 已保存（覆盖，6 个元素）
```

若进程在命令执行前就退出（日志里没有这一行），改用编辑器控制台手动执行同一条命令，
再重跑下方测试。资产是 LFS 二进制，`git status` 会显示它被修改。

- [ ] **Step 6: 跑地质与矿物全量测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_All.log"
```

预期全绿，其中 `EarthPresetValues` 证明磁盘上的资产真的是新数据。

- [ ] **Step 7: 文档**

`Content/Geology/README.md`：把「## 参数语义」整节（现第 19–32 行，从标题到
「别靠加 `PodCount` 硬堆」那段）替换为：

```markdown
## 参数语义

每个元素有一组**分布生成器**（`Distributions`）。一个元素可以叠加多个生成器，
**逐点取 max**（不是相加）。四种形态：

| 生成器 | 形态 | 地球上的用法 |
|---|---|---|
| 囊状（球团） | 离散团块 | 旧资产的迁移目标；最朴素的形态 |
| 片层（透镜） | 沿切平面铺开的薄椭球 | 硅、水 |
| 脉状（穿切） | 折线骨架 + 胶囊 | 铁、铜 |
| 浸染（散布） | 大量小颗粒 | 钛、铀 |

公共参数（每个生成器各有一套）：

| 参数 | 含义 |
|---|---|
| `Seed` | 0 = 按「元素 Id + 生成器下标 + 类名」派生。**生成器重排会改变形状** |
| `ShapeDetail` | 一个特征尺度内几个噪声周期，越大越破碎 |
| `Octaves` / `Lacunarity` / `Gain` | 分形噪声的层数与衰减 |
| `VeinThreshold` | 噪声稀疏阈值，越高越破碎；0 = 不稀疏 |

`Abundance` 在元素上，**只影响颜色混合与图例占比**，不改几何。

**想看不同元素互相穿插**：给同一深度带上的两个元素各挂一个生成器，把它们的
`DepthCenter` / `DepthBias` 调到同一区间即可 —— 几何是否重叠由生成器决定，
`Abundance` 帮不上忙。

## 观测视图与生成器的关系

观测视图的 `CellsPerFeature` 跟的是生成器的**特征尺度**（片层=厚度、脉=管半径、
浸染=粒径、囊=囊半径），不是行星直径：换一颗大行星，相对清晰度不变。

- `ShapeDetail` 要能被观测视图解析，需 `ShapeDetail ≤ CellsPerFeature / 2`
  （默认 3 ≤ 6）。
- 片层宽而薄是刻意的：厚度减半，观测视图角点涨约 4 倍。建议厚度 ≥ 0.008、
  `CellsPerFeature` 取 12–16。
- 浸染想更密，先加粒径（角点 ∝ 粒径²），再加数量。

## 旧资产

只有 `PodCount` 等遗留字段、没有生成器的资产，在运行期按原样建囊（种子用旧公式，
形状逐位不变）；在编辑器里保存一次就会写成一个显式囊状生成器。遗留字段标了
`DeprecatedProperty`，新内容请一律用生成器。
```

同一个文件的「## 已知缺口：同元素相邻簇的盒边截断」一段里，
「观测视图按矿囊的空间邻近关系分簇」「对方的矿囊会落进」两处「矿囊」改为「区域」，
其余判据与结论不变（那条缺口仍在，只是现在对四种形态都成立）。

`CLAUDE.md` 的「关键类」表加一行（放在 `AVoxelPlanetActor` 之前）：

```markdown
| `UPlanetGeologyDistribution` | Geology | 分布生成器基类；子类=囊状/片层/脉状/浸染，元素内取 max |
```

- [ ] **Step 8: 加 BOM 并提交**

```bash
Source/StellarEraGeology/Public/PlanetGeologyPreset.h
Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp
Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp
Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp
```

```bash
git add -- Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp \
  Content/Geology/Planets/DA_Geology_Earth.uasset Content/Geology/README.md CLAUDE.md
git commit -m "feat(地质): 旧资产迁移 + 地球六元素各用专属分布，测试夹具切显式生成器" -- \
  Source/StellarEraGeology/Public/PlanetGeologyPreset.h \
  Source/StellarEraGeology/Private/PlanetGeologyPreset.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPodTests.cpp \
  Source/StellarEraTerrain/Private/PlanetGeologyPresetTests.cpp \
  Content/Geology/Planets/DA_Geology_Earth.uasset Content/Geology/README.md CLAUDE.md
```

---

## Task 8: 全量构建 + 全量测试 + 验收

**Files:**
- Modify: `docs/superpowers/plans/2026-10-26-planet-geology-distributions.md`（末尾「实施记录」）
- 无新增源码。

**Interfaces:**
- Consumes: Task 1–7 的全部产出。
- Produces: 可以交给用户验收的分支状态。

- [ ] **Step 1: 关编辑器，全量构建**

编辑器必须完全退出（Live Coding 锁 DLL）。cwd 在项目外：

```bash
cd "C:/" && "D:/Application/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe" \
  "D:/Application/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" \
  StellarEraEditor Win64 Development \
  -Project="F:/workspace/ue/StellarEra/StellarEra.uproject" -WaitMutex -FromMsBuild
```

预期末尾出现 `Result: Succeeded`。若出现 `Result: Failed`，先看完整日志里有没有
`error C` / `error LNK`：有时只是 DLL 链接被占用，编译本身是过的，关掉编辑器重跑即可。
**不要**改用 `Build.bat`。

- [ ] **Step 2: 全量测试**

```bash
"D:/Application/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" \
  "F:/workspace/ue/StellarEra/StellarEra.uproject" \
  -ExecCmds="Automation RunTests StellarEra" \
  -TestExit="Automation Test Queue Empty" \
  -unattended -nopause -nullrhi -nosplash -stdout -abslog="$TEMP/StellarEra_All.log"
```

预期：`**** TEST COMPLETE. EXIT CODE: 0`，且日志里 `Automation Test Failed` 出现 0 次。

逐一确认这些测试名都在跑（改名后）：

```
StellarEra.Planet.Geology.Distribution.Noise
StellarEra.Planet.Geology.Distribution.MaxComposition
StellarEra.Planet.Geology.Distribution.SeedDerivation
StellarEra.Planet.Geology.Distribution.EmptySemantics
StellarEra.Planet.Geology.ShapePods.Coverage / .Overlap / .Shape / .BruteForceAgrees
StellarEra.Planet.Geology.Lens.Bounds / .BruteForceAgrees
StellarEra.Planet.Geology.Vein.Continuity / .BruteForceAgrees
StellarEra.Planet.Geology.Disseminated.Counts / .BruteForceAgrees
StellarEra.Planet.Geology.PresetDefaults / .PresetRevision / .PresetRevision.NestedEdit
StellarEra.Planet.Geology.PresetRevision           （旧名，保留）
StellarEra.Planet.Geology.LegacyAssetPath / .LegacyMigration / .LegacyMigrationFidelity
StellarEra.Planet.Geology.EarthPresetValues
StellarEra.Planet.Geology.Determinism / .Bounds / .Color / .GeneratorParametersSupported
StellarEra.Planet.Geology.Pipeline / .AbundanceGeometryInvariance
StellarEra.Planet.Geology.ElementNorms.MatchesDominant / .RangeCoverage / .AssetClamps
StellarEra.Planet.MineralSurface.*（含 .RegionLocal / .CellsPerFeature / .RegionClustering / .ClosedMesh.AllForms / .GridCap）
StellarEra.Planet.MineralView.*（含 .CellsPerFeature）
StellarEra.Core.* / StellarEra.Planet.AtmosphereParams.* / StellarEra.StarMap.*（不该被本次改动影响）
```

- [ ] **Step 3: 静态自查**

BOM（含中文的源文件必须 `efbbbf` 开头）：

```bash
cd "f:/workspace/ue/StellarEra" && for f in $(grep -rl "分布\|生成器\|每特征\|迁移" Source --include=*.h --include=*.cpp); do \
  printf '%-88s %s\n' "$f" "$(head -c 3 "$f" | od -An -tx1 | tr -d ' \n')"; done
```

预期：每行末尾都是 `efbbbf`。任何一行不是，给它加 BOM 后**重新构建**。

测试名不重复：

```bash
grep -rho '"StellarEra\.[^"]*"' Source --include=*.cpp | sort | uniq -d
```

预期：无输出。

改名残留：

```bash
grep -rn "CellsPerPod" Source Config | grep -v "Config/DefaultEngine.ini"
grep -rn "FPlanetGeologyPodRef" Source
```

预期：两条都无输出（`CellsPerPod` 只允许存在于 ini 的 `PropertyRedirects`）。

遗留字段只许出现在三处：

```bash
grep -rn "PodCount\|PodRadiusJitter\|PodDepthBias\|PodDepthCenter\|PodDepthSpread" \
  Source --include=*.h --include=*.cpp | grep -v "PlanetGeologyPreset.h"
```

预期命中只允许出现在：`PlanetGeologySpherePodsDistribution.*`（自己的参数）、
`PlanetGeologyField.cpp` 的遗留兜底、`PlanetGeologyPreset.cpp` 的迁移与地球数据、
测试文件。出现在别的运行期文件里就是漏改。

- [ ] **Step 4: 人工验收（交给用户）**

本会话的编辑器 MCP 端点是 `ECONNREFUSED`，下面这些必须在编辑器里由人操作。
逐条回报结果，任何一条不符合都算没完成：

1. 打开 `DA_Geology_Earth`：六个元素各带一个生成器，类型分别是
   片层/脉状/片层/脉状/浸染/浸染；`PodCount` 等遗留字段全是 0。
2. 改硅的 `SeamCount`，正在预览的观测体自动重建（不用手动点）。
3. 预览里同屏能看到片层、穿切地层的脉、散点浸染；图例里关掉其他元素后，
   能看到片层与脉互相穿插。
4. `UniverseMap` 点「重建行星」后 PIE：地表只见露头矿，海面没有蓝色环。
5. 读日志里的两个数字并记进 Step 5：
   - `[矿物] ... 构造 %.1f ms`（编辑器要在前台，后台会被限 3 FPS）
   - `[矿物] ... 每特征 %d 格（%d 个角点）`
   基线取迁移前最后一次提交的实测值；若工作区有未提交改动、不便回退代码，
   就写「无基线」并只记新值 —— **不要编数字**。
6. 退出预览后 `git status` 没有新增脏包。
7. 打开 `BP_VoxelPlanet3`，`PlanetMineralView` 的 `CellsPerFeature` 仍是 32。
8. 复制 `DA_Geology_Earth` 成 `DA_Geology_Earth_Copy`，打开、保存、删除副本 ——
   内联生成器子对象的归属不出错。

- [ ] **Step 5: 写实施记录**

在本文件末尾追加：

```markdown
---

## 实施记录

- 构建：<Result 行原文>
- 全量测试：<通过数>/<总数>，`EXIT CODE: 0`
- 观测体重建：<新数字>（基线 <数字或无基线>）
- 角点：<数字>
- 人工验收 1–8：<逐条结果>
```

- [ ] **Step 6: 提交**

```bash
cd "f:/workspace/ue/StellarEra" && git add -- \
  docs/superpowers/plans/2026-10-26-planet-geology-distributions.md
git commit -m "docs(地质): 分布生成器实施记录与验收结果" -- \
  docs/superpowers/plans/2026-10-26-planet-geology-distributions.md
```

---

## 完成判据

- [ ] Task 1–7 每步的构建与测试都真实跑过（不是「应该能过」）。
- [ ] Task 8 Step 2 全绿，Step 3 四条静态自查都干净。
- [ ] Task 8 Step 4 的八条人工验收逐条有结论。
- [ ] 旧资产保真由 `Geology.LegacyMigrationFidelity` 逐点证明。
- [ ] `git log --oneline -8` 能看到每个任务一条提交。
