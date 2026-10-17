// 挖掘机 Actor —— 真正挖掉体素地形获得矿石。
//
// 这是整个工厂系统里唯一直接改动体素数据的地方。
// 其他工厂（冶炼、物流）都不碰体素。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "FactoryTypes.h"
#include "OreSystem.h"
#include "PlanetPresets.h"
#include "MiningRig.generated.h"

class FOreSystem;

class UMaterialInstanceDynamic;

class AVoxelWorld;
class AVoxelPlanetActor;

/**
 * 挖掘机。
 *
 * 工作流程：
 *   1. 每 DigsPerSecond 秒启动一次作业
 *   2. 探测作业点的矿物丰富度
 *   3. 丰富度够 → 挖掉一个球形区域的体素，产出矿石
 *   4. 丰富度太低 → 停机
 *
 * 关键设计：机器存的是 FPlanetAnchor（星球局部坐标），
 * 不是世界坐标。星球怎么自转公转，机器都贴在正确的地表点上。
 */
UCLASS(Blueprintable)
class VOXELFACTORY_API AMiningRig : public AActor
{
	GENERATED_BODY()

public:
	AMiningRig();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 锚定

	/**
	 * 把机器锚定到星球的某个方向。
	 *
	 * @param Planet    目标星球（通常是 ACosmosBodyActor）
	 * @param PlanetActor 该星球对应的体素星球 Actor
	 * @param WorldDirection 世界空间的方向（从星球中心指过来）
	 * @param HeightOffset 相对地表的高度，0 = 贴地
	 */
	UFUNCTION(BlueprintCallable, Category = "Mining")
	bool AnchorToPlanet(
		AActor* Planet,
		AActor* PlanetActor,
		const FVector& WorldDirection,
		float HeightOffset = 0.f);

	/** 解除锚定，机器停止工作。 */
	UFUNCTION(BlueprintCallable, Category = "Mining")
	void Unanchor();

	/** 当前锚点。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	const FPlanetAnchor& GetAnchor() const { return Anchor; }

	//////////////////////////////////////////////////////////////////////
	// 工作状态

	/** 是否正在工作。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	bool IsWorking() const { return bWorking; }

	/** 已产出的矿石总量（按类型）。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	int32 GetProducedAmount(EOreType Ore) const;

	/** 当前作业点的矿物丰富度。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	float GetCurrentAbundance() const { return CurrentAbundance; }

	/** 手动启动/停止。断开锚点时会自动停止。 */
	UFUNCTION(BlueprintCallable, Category = "Mining")
	void SetWorking(bool bInWorking);

	//////////////////////////////////////////////////////////////////////
	// 物流接口
	//
	// 传送带从这里取货。必须与 AProcessingBuilding 的接口保持一致，
	// 这样上层物流设备不需要区分上游是什么类型。

	/**
	 * 交出库存里的物品。返回实际交出的数量。
	 * 传送带每帧调用，所以要支持「只给一部分」。
	 */
	UFUNCTION(BlueprintCallable, Category = "Mining")
	int32 TakeOre(EItemType Item, int32 Count);

	/** 当前库存总量。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	int32 GetTotalStored() const;

	/** 库存是否已满。传送带据此决定要不要来取。 */
	UFUNCTION(BlueprintPure, Category = "Mining")
	bool IsStorageFull() const;

	//////////////////////////////////////////////////////////////////////
	// 参数

	/** 挖掘配置。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	FMiningRigConfig Config;

	/**
	 * 主要开采的矿物。
	 *
	 * bAutoSelectOre 打开时忽略这个字段，改为每次探测选当前点最富的矿。
	 * 这让一台挖掘机在矿脉丰富的星球上不会闲置。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	EOreType PrimaryOre = EOreType::Iron;

	/** 是否自动选择当前点最富的矿。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining")
	bool bAutoSelectOre = true;

	/** 该星球的矿脉配置。留空则用 PlanetType 预设自动生成。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining|Ore")
	FOrePlanetConfig OreConfig;

	/** 星球类型。用于一键生成预设。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining|Ore")
	EPlanetType PlanetType = EPlanetType::Rocky;

	/** 单次开采得到的矿石数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "0"))
	int32 YieldPerDig = 4;

	/** 库存上限。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mining", meta = (ClampMin = "1"))
	int32 StorageCapacity = 500;

	/** 机器网格。为空时用引擎自带立方体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> MachineMesh;

	/** 运行指示灯的颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor StatusColorOn = FLinearColor(0.2f, 1.f, 0.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor StatusColorOff = FLinearColor(1.f, 0.2f, 0.2f);

protected:
	/** 执行一次挖掘作业。 */
	void PerformDig();

	/** 更新锚点对应的世界变换。 */
	void UpdateTransformFromAnchor();

	/** 探测作业点的地表信息。 */
	FSurfaceProbeResult ProbeSurface() const;

	/** 拿到该星球的体素世界。 */
	AVoxelWorld* GetVoxelWorld() const;

	/** 更新指示灯。 */
	void UpdateStatusLight();

private:
	/** 当前锚点。 */
	FPlanetAnchor Anchor;

	/** 该星球对应的体素星球 Actor。 */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PlanetActorRef;

	/** 该星球的基准半径（体素）。 */
	double PlanetRadiusVoxels = 1000.0;

	/** 工作状态。 */
	bool bWorking = false;
	float CurrentAbundance = 0.f;
	float DigTimer = 0.f;

	/** 挖掉的体素总数。诊断用。 */
	int32 TotalDigs = 0;

	/** 各矿物库存。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mining")
	TMap<EOreType, int32> Storage;

	UPROPERTY(VisibleAnywhere, Category = "Mining")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Mining")
	TObjectPtr<UPointLightComponent> StatusLight;

	/** 缓存的动态材质实例。BeginPlay 创建，之后只改参数不重建。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CachedMID;

	/** 矿脉采样器。BeginPlay 用 OreConfig 初始化。 */
	TUniquePtr<FOreSystem> OreSampler;

	/** 本次作业选中的矿种。每次探测时更新。 */
	EOreType ActiveOre = EOreType::Iron;
};
