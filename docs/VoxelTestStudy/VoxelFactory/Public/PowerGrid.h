// 电网 —— 能源系统。
//
// 为什么必须有这个：没有能源系统，工厂游戏就是「堆数字」。
// 放1 台冶炼炉和放 100 台效果一样，只是数量变了。
// 有了电网，「集中布局 vs 分散布局」才成为真正的取舍。
//
// 设计取舍（刻意做简单）：
//   · 一个电厂就是一个供电范围，不需要玩家铺电线
//   · 范围内所有设备共享这个电厂的功率，按需求比例分配
//   · 供电不足时设备降速而不是停机 —— 玩家能看见信号
//
// 后续要扩展成真正电网（变压器、分区）时，
// 只要把 GetSatisfactionAt 换掉，加工机那边一行代码都不用改。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "FactoryRecipe.h"
#include "PowerGrid.generated.h"

class UMaterialInstanceDynamic;

class AProcessingBuilding;
class AMiningRig;
class AConveyorBelt;

/**
 * 发电站。
 *
 * 放置后自动为供电半径内的设备供电。
 */
UCLASS(Blueprintable)
class VOXELFACTORY_API APowerPlant : public AActor
{
	GENERATED_BODY()

public:
	APowerPlant();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 发电

	/** 额定输出功率。配方里「组装发电机」那一项就是产这个。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	float MaxOutput = 100.f;

	/** 供电半径，单位厘米。半径外的设备得不到电。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "100.0"))
	float SupplyRadius = 3000.f;

	/** 是否开机。关机后不供电但保留建筑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bOnline = true;

	/** 需要燃料吗？核电站需要铀，普通太阳能不需要。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power")
	bool bRequiresFuel = false;

	/** 燃料库存。bRequiresFuel 为 true 时消耗。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Power")
	TMap<EItemType, int32> FuelBuffer;

	/** 每秒消耗多少燃料。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0"))
	float FuelPerSecond = 0.f;

	/** 燃料种类。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (EditCondition = "bRequiresFuel"))
	EItemType FuelItem = EItemType::UraniumOre;

	//////////////////////////////////////////////////////////////////////
	// 状态

	/** 当前实际输出（可能因燃料不足而低于额定）。 */
	UFUNCTION(BlueprintPure, Category = "Power")
	float GetActualOutput() const { return ActualOutput; }

	/** 接入的设备数量。诊断用。 */
	UFUNCTION(BlueprintPure, Category = "Power")
	int32 GetConnectedCount() const { return ConnectedBuildings.Num(); }

	/** 当前负载率，0~1。超过 1 说明超载。 */
	UFUNCTION(BlueprintPure, Category = "Power")
	float GetLoadFactor() const { return TotalDemand > 0.001f ? TotalOutput / TotalDemand : 0.f; }

	//////////////////////////////////////////////////////////////////////
	// 外观

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> PlantMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor OnlineColor = FLinearColor(0.4f, 0.9f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor OfflineColor = FLinearColor(0.4f, 0.4f, 0.4f);

protected:
	/** 扫描供电范围内的设备，更新需求总量。 */
	void RefreshConnections();

	/** 给所有接入设备分配供电。 */
	void DistributePower();

	/** 消耗燃料。 */
	void ConsumeFuel(float DeltaSeconds);

	/** 更新指示灯。 */
	void UpdateVisual();

private:
	/** 供电范围内的设备。 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AProcessingBuilding>> ConnectedBuildings;

	/** 需求与输出。 */
	float TotalDemand = 0.f;
	float ActualOutput = 0.f;

	/** 重新扫描的间隔。不用每帧都扫全场。 */
	float RescanTimer = 0.f;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UPointLightComponent> StatusLight;

	/** 缓存的动态材质实例。BeginPlay 创建，之后只改参数。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CachedMID;
};
