// 星球预设 —— 一键配置地形参数与矿脉。
//
// 动机：手动调十几个参数太痛苦。预设让「生成一颗铁质星球」变成一行代码。
//
// 预设同时配置地形和矿脉，因为它们在设计上应该匹配：
// 铁质星球的铁多、铀少；冰冻星球水多、钛少。
// 分开配置会出现「火山星球但矿脉是水的」这种矛盾。

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetTypes.h"
#include "OreSystem.h"
#include "PlanetPresets.generated.h"

/** 星球类型。 */
UENUM(BlueprintType)
enum class EPlanetType : uint8
{
	/** 岩石星球：铁铜丰富，适合基础工业。 */
	Rocky,
	/** 金属星球：铁钛极丰富，铜硅稀少。 */
	Metallic,
	/** 冰冻星球：水极丰富，其他都少。 */
	Frozen,
	/** 火山星球：钛铀丰富，地形起伏大。 */
	Volcanic,
	/** 硅质地表：硅丰富，其他很少。 */
	Silica,
	/** 贫瘠星球：只有水和一点点铁。 */
	Barren,
	/** 丰富星球：所有矿都多。适合测试。 */
	Rich
};

/**
 * 星球预设工具。
 *
 * 纯静态函数，不持有状态。
 */
class VOXELFACTORY_API FPlanetPresets
{
public:
	/**
	 * 应用预设。会同时改写地形层配置与矿脉配置。
	 *
	 * @param Type       星球类型
	 * @param Seed       星球种子。相同种子 + 相同类型 = 完全相同的星球
	 * @param Radius     星球半径（体素）
	 * @param OutLayers  输出：地形层配置
	 * @param OutOre     输出：矿脉配置
	 */
	static void Apply(
		EPlanetType Type,
		int32 Seed,
		double Radius,
		FVoxelPlanetContinentLayer& OutContinents,
		FVoxelPlanetMountainLayer& OutMountains,
		FVoxelPlanetDetailLayer& OutDetail,
		FVoxelPlanetRavineLayer& OutRavine,
		FVoxelPlanetSeaSettings& OutSea,
		FVoxelPlanetLandPalette& OutLand,
		FOrePlanetConfig& OutOre);

	/** 只应用矿脉预设，不动地形。 */
	static void ApplyOreOnly(EPlanetType Type, int32 Seed, double Radius, FOrePlanetConfig& OutOre);

	/** 该预设的显示名。 */
	static FText GetDisplayName(EPlanetType Type);

	/** 该预设的一句话描述。 */
	static FText GetDescription(EPlanetType Type);

private:
	/** 填充一个默认矿脉配置。 */
	static FOrePlanetConfig MakeBaseOreConfig(EPlanetType Type, int32 Seed, double Radius);

	/** 按类型设置各矿的丰富度。 */
	static void TuneOreAbundance(EPlanetType Type, FOrePlanetConfig& Config);

	/** 按类型设置地形。 */
	static void TuneTerrain(EPlanetType Type,
		FVoxelPlanetContinentLayer& Continents,
		FVoxelPlanetMountainLayer& Mountains,
		FVoxelPlanetDetailLayer& Detail,
		FVoxelPlanetRavineLayer& Ravine,
		FVoxelPlanetSeaSettings& Sea,
		FVoxelPlanetLandPalette& Land);
};
