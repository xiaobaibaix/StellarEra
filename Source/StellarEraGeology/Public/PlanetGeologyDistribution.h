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

	/** 阈值稀疏后的 [0,1]；FeatureRadius ≤ 0 时恒 0。 */
	float EvaluateVein(const FVector& P) const;

	float FeatureRadius = 0.f;
	float ShapeDetail = 3.f;
	int32 Octaves = 2;
	float VeinThreshold = 0.f;

private:
	FVoxelFastNoise Noise;
};

/**
 * 带权深度放置：返回归一化球内的位置（|P| ≤ 1）。
 *
 * 随机流顺序与旧 PlacePods 逐位一致（方向 → 初始半径 → 可选拒绝采样循环），
 * 迁移保真测试依赖这一点。
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

	// UObject 不能有纯虚函数（UHT 生成的构造代码会实例化本类），所以用 PURE_VIRTUAL：
	// 语义相同（子类必须覆写），漏掉覆写时运行期致命报错而不是悄悄返回空。
	virtual TSharedPtr<const FPlanetGeologyDistributionRuntime> BuildRuntime(
		const FPlanetGeologyDistributionBuildContext& Context) const
		PURE_VIRTUAL(UPlanetGeologyDistribution::BuildRuntime, return nullptr;);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
