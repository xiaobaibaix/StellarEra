// 传送带 —— 第一版唯一的物流方式。
//
// 为什么只做一种：卡车和飞机的复杂度在寻路与调度，
// 而这些在生产链没跑通前不值得投入。等传送带验证了闭环，
// 加卡车只是多一个运输载具，加工机一行代码都不用改。
//
// 三种物流必须接口一致，这样上层完全不关心货物怎么来的：
//   virtual void RequestPickup(FItemStack& OutStack)
//   virtual void Deliver(const FItemStack& Stack)
//
// 设计取舍：
//   · 用「端口」而不是「拖拽路径」——每段带子是一个独立 Actor，
//     端口记录连接对象。玩家摆好带子自然就连上了。
//   · 库存有上限（背压）——上游满了会停，不会无限堆积。
//   · 每帧按距离搬运，不是逐个物品模拟——性能考虑。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "FactoryRecipe.h"
#include "ConveyorBelt.generated.h"

class AProcessingBuilding;
class AMiningRig;

/**
 * 传送带的一段。
 *
 * 每段带子有自己的输入端和输出端。摆放好首尾相接就形成通路。
 */
UCLASS(Blueprintable)
class VOXELFACTORY_API AConveyorBelt : public AActor
{
	GENERATED_BODY()

public:
	AConveyorBelt();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 连接

	/** 上游：谁把货物送进来。空则从场景里自动找。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	TWeakObjectPtr<AActor> InputSource;

	/** 下游：货物送去哪。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	TWeakObjectPtr<AActor> OutputTarget;

	/** 自动连接搜索半径（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt", meta = (ClampMin = "100.0"))
	float AutoConnectRadius = 500.f;

	/** 扫描周围并自动连上最近的上下游。 */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	void AutoConnect();

	//////////////////////////////////////////////////////////////////////
	// 输送

	/** 当前携带的货物。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Belt")
	TMap<EItemType, int32> Cargo;

	/** 最大载货量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt", meta = (ClampMin = "1"))
	int32 Capacity = 20;

	/** 输送速度，单位「每 100 厘米每秒」。默认 200 = 2 米/秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt", meta = (ClampMin = "10.0"))
	float Speed = 200.f;

	/** 只输送这一种物品。None = 什么都送。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	EItemType FilterItem = EItemType::None;

	/** 本段带的载货量，UI 用。 */
	UFUNCTION(BlueprintPure, Category = "Belt")
	int32 GetTotalCargo() const;

	/** 带子是空的吗。 */
	UFUNCTION(BlueprintPure, Category = "Belt")
	bool IsEmpty() const;

	/** 尝试放一个物品进来。返回实际放入数量。 */
	UFUNCTION(BlueprintCallable, Category = "Belt")
	int32 Receive(EItemType Item, int32 Count);

	//////////////////////////////////////////////////////////////////////
	// 外观

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> BeltMesh;

protected:
	/** 从上游取货。 */
	void PullFromUpstream(float DeltaSeconds);

	/** 把货送到下游。 */
	void PushToDownstream(float DeltaSeconds);

	/** 尝试从 AProcessingBuilding 取货。 */
	void PullFromProcessing(AProcessingBuilding* Source, float DeltaSeconds);

	/** 尝试从 AMiningRig 取货。 */
	void PullFromMiningRig(AMiningRig* Source, float DeltaSeconds);

	/** 尝试送进 AProcessingBuilding。 */
	void PushToProcessing(AProcessingBuilding* Target, float DeltaSeconds);

private:
	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> MeshComponent;
};
