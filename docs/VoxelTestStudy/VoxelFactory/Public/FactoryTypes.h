// 星球工厂系统 —— 类型与数据定义。
//
// 架构定位：工厂是普通 UE Actor，只借用体素星球的「地形外观」。
// 不把工厂塞进 AVoxelWorld，因为那会被 VoxelInvoker 的剔除范围影响，
// 坐标也受体素网格约束。
//
// 三层结构：
//   PlanetAnchor   星球局部坐标锚点（工厂「贴在」哪）
//   FactoryActor   工厂本体（挖掘机、冶炼厂、物流塔……）
//   MiningJob      一次挖矿作业（挖哪里、挖多久、产什么）

#pragma once

#include "CoreMinimal.h"
#include "CosmosTypes.h"
#include "FactoryTypes.generated.h"

class ACosmosBodyActor;
class AVoxelPlanetActor;

///////////////////////////////////////////////////////////////////////////////
// 星球锚点 —— 解决「星球在转，工厂怎么记住自己贴在哪」
//
// 不能存世界坐标：星球自转/公转会让世界坐标不断变化。
// 必须存星球局部坐标：
//   · LocalDirection 单位球面方向，标记贴在哪个方位
//   · LocalHeight    相对地表的高度（体素）
//
// 这样星球怎么转公转，工厂都贴在正确的地表点上。

USTRUCT(BlueprintType)
struct FPlanetAnchor
{
	GENERATED_BODY()

	/** 锚定在哪颗星球上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	TWeakObjectPtr<ACosmosBodyActor> Planet;

	/** 星球局部方向（单位球面向量）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FVector LocalDirection = FVector::UpVector;

	/** 相对该方向地表的高度，单位体素。0 = 贴地。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	double LocalHeight = 0.0;

	/** 工厂的朝向，会叠加在星球自转之上。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Anchor")
	FRotator LocalRotation = FRotator::ZeroRotator;

	/** 锚点是否有效。 */
	bool IsValid() const { return Planet.IsValid(); }

	/** 重算 LocalDirection（当星球移动后调用，保持锚定）。 */
	void RefreshDirection(const FVector& WorldLocation);

	/** 把锚点转成世界变换。 */
	void ToWorldTransform(FTransform& OutTransform) const;
};

///////////////////////////////////////////////////////////////////////////////
// 矿石类型

UENUM(BlueprintType)
enum class EOreType : uint8
{
	/** 无矿。 */
	None,
	/** 铁。通用基础金属。 */
	Iron,
	/** 铜。导电材料。 */
	Copper,
	/** 硅。电子元件与建筑。 */
	Silicon,
	/** 钛。轻质高强度，航天器用。 */
	Titanium,
	/** 铀。核燃料。 */
	Uranium,
	/** 水。生命维持与农业。 */
	Water
};

/** 矿石的静态配置。 */
USTRUCT(BlueprintType)
struct FOreDefinition
{
	GENERATED_BODY()

	/** 该星球上某种矿物的丰富度，0~1。0 = 没有。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Abundance = 0.f;

	/** 每次开采得到的单位数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.0"))
	float YieldPerDig = 1.f;

	/** 开采硬度。硬矿需要更长时间，影响挖掘机效率。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ore", meta = (ClampMin = "0.1"))
	float Hardness = 1.f;
};

///////////////////////////////////////////////////////////////////////////////
// 挖掘机参数

USTRUCT(BlueprintType)
struct FMiningRigConfig
{
	GENERATED_BODY()

	/**
	 * 挖掘半径，单位体素。
	 * 每次作业挖掉这个球形的体素。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "1.0"))
	float DigRadiusVoxels = 8.f;

	/** 挖掘深度。0 = 只挖地表，1 = 往深处挖。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DigDepthRatio = 0.5f;

	/** 每秒挖多少次。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "0.01"))
	float DigsPerSecond = 2.f;

	/** 单次作业消耗的能量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "0.0"))
	float EnergyPerDig = 10.f;

	/** 停止作业的矿物丰富度阈值。挖空了就停。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "0.0"))
	float DepletionThreshold = 0.05f;
};

///////////////////////////////////////////////////////////////////////////////
// 工厂类型

UENUM(BlueprintType)
enum class EFactoryType : uint8
{
	/** 采矿机。挖体素地形产出矿石。 */
	MiningRig,
	/** 冶炼厂。把矿石转成金属锭。 */
	Smelter,
	/** 精炼厂。高阶加工。 */
	Refinery,
	/** 物流塔。存储与转运。 */
	LogisticsTower,
	/** 发电站。为其他工厂供电。 */
	PowerPlant
};

USTRUCT(BlueprintType)
struct FFactoryDefinition
{
	GENERATED_BODY()

	/** 显示名。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Factory")
	FText DisplayName;

	/** 占地半径，单位厘米。用于放置检测。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Factory", meta = (ClampMin = "1.0"))
	float FootprintRadiusCm = 500.f;

	/** 建造消耗的资源。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Factory")
	TMap<EOreType, int32> BuildCost;

	/** 运行耗能（每秒）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Factory", meta = (ClampMin = "0.0"))
	float EnergyPerSecond = 5.f;
};
