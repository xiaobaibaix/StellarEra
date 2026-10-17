// 星球表面探测器 —— 工厂系统的基础设施。
//
// 回答三个问题：
//   1. 从太空某点往下打射线，落到星球表面的哪？
//   2. 那个方向的矿石丰富度是多少？
//   3. 挖掉这块体素需要多少工作量？
//
// 关键：探测在星球「局部坐标」下进行，因为体素世界固定在原点，
// 而星球 Actor 在轨道上移动。两者必须解耦。

#pragma once

#include "CoreMinimal.h"
#include "CoreMinimal.h"
#include "FactoryTypes.h"

/**
 * 一次地表探测的结果。
 */
USTRUCT(BlueprintType)
struct FSurfaceProbeResult
{
	GENERATED_BODY()

	/** 探测是否命中了星球表面。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	bool bHit = false;

	/** 命中点的世界坐标。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	FVector WorldLocation = FVector::ZeroVector;

	/** 命中点的体素局部坐标（相对体素世界原点）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	FVector VoxelLocation = FVector::ZeroVector;

	/** 该点相对星球中心的距离，单位体素。约等于当地海拔+半径。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	double SurfaceDistanceVoxels = 0.0;

	/** 该点的法线（外法线）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	FVector WorldNormal = FVector::UpVector;

	/** 该方向的矿物丰富度，0~1。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	float OreAbundance = 0.f;

	/** 挖到这里需要的难度（受硬度与深度影响）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Probe")
	float DigDifficulty = 1.f;
};

/**
 * 星球表面探测器。
 *
 * 是纯计算类，不持有 UObject 状态。
 */
class VOXELFACTORY_API FPlanetSurfaceProbe
{
public:
	/**
	 * 从某个方向探测星球表面。
	 *
	 * @param VoxelWorld 星球对应的体素世界
	 * @param SearchOriginVoxels 射线起点（体素坐标）
	 * @param DirectionVoxels 射线方向（体素坐标，会被归一化）
	 * @param StepVoxels 步进粒度。越小越精确，但越慢。
	 * @param OreField 矿物分布函数：输入方向 → 丰富度
	 *
	 * 步进探测而不是物理射线：体素世界不是标准碰撞体，
	 * 用步进更可控，也更容易在任意距离上工作。
	 */
	static FSurfaceProbeResult ProbeVoxelWorld(
		class AVoxelWorld* VoxelWorld,
		const FVector& SearchOriginVoxels,
		const FVector& DirectionVoxels,
		double StepVoxels = 4.0,
		double MaxDistanceVoxels = 100000.0);

	/**
	 * 解析式估算地表半径。
	 *
	 * 用生成器的公式直接算，不查体素数据——快得多。
	 * 适合「我只想知道大概表面在哪」的场景，比如放置预览。
	 *
	 * @param Radius 基准半径（体素）
	 * @param Direction 单位球面方向
	 * @param TerrainRadiusOffset 地形偏移函数（来自 Terrain 类）
	 */
	static double EstimateSurfaceRadius(
		double Radius,
		const FVector& Direction,
		TFunction<double(const FVector&)> TerrainRadiusOffset);

	/**
	 * 计算某点的挖掘难度。
	 *
	 * 越深越难挖：需要穿过上覆岩层。
	 *
	 * @param DepthVoxels 该点相对基准半径的深度（体素），负值表示在地表以上
	 * @param Hardness 矿物硬度
	 */
	static float ComputeDigDifficulty(double DepthVoxels, float Hardness);

	/**
	 * 采样矿物分布。
	 *
	 * 用一层廉价噪声模拟矿脉分布，保证同一星球不同位置矿物不同，
	 * 但整体丰富度由星球参数决定。
	 */
	static float SampleOreAbundance(
		const FVector& UnitDirection,
		double PlanetSeed,
		EOreType Ore,
		float GlobalAbundance);
};
