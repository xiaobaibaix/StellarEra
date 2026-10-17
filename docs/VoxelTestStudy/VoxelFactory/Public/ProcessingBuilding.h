// 通用加工机 —— 工厂系统的核心类。
//
// 冶炼炉、零件厂、组装厂全部是这一个类，行为差异完全由配方决定。
// 这样做的收益：
//   · 代码量减半（11 种配方一个类覆盖）
//   · 存档只需存「配方 Id + 槽位内容」
//   · 加新配方 = 加数据，不改代码
//   · 改平衡数值不动任何代码
//
// 工作流程：
//   1. 每Tick 检查输入是否齐全 → 齐全则锁定配方、扣料、开始加工
//   2. 加工中累积进度（受电力供应率影响）
//   3. 完成时输出产物；若配方标记 bProducesActor 则生成建筑
//
// 关键约束：加工一旦开始就不能中断，除非输出槽腾出空间。
// 这是故意的——玩家不能靠反复启停刷进度。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "FactoryRecipe.h"
#include "ProcessingBuilding.generated.h"

class UMaterialInstanceDynamic;

/**
 * 通用加工机。
 *
 * 放置后指定一个配方资产，它就变成对应的建筑。
 */
UCLASS(Blueprintable)
class VOXELFACTORY_API AProcessingBuilding : public AActor
{
	GENERATED_BODY()

public:
	AProcessingBuilding();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 配方

	/** 这台机器当前执行的配方。空则不工作。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production")
	FFactoryRecipe Recipe;

	/** 只读访问器，给外部（传送带）用。 */
	const FFactoryRecipe& GetRecipe() const { return Recipe; }

	/**
	 * 配方索引。
	 * Recipe 是结构体（可内联编辑），但存档需要稳定的字符串标识，
	 * 所以同时存一份到 URecipeLibrary 里查到的索引。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Production")
	int32 RecipeIndex = INDEX_NONE;

	//////////////////////////////////////////////////////////////////////
	// 槽位

	/** 输入槽上限。每种配方种类占一格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "1", ClampMax = "8"))
	int32 InputSlotCapacity = 4;

	/** 每格能存多少个。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Production", meta = (ClampMin = "1"))
	int32 SlotCapacity = 100;

	/** 当前库存。BlueprintReadWrite 是为了让 UI 和调试面板能读。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Production")
	TMap<EItemType, int32> InputBuffer;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Production")
	TMap<EItemType, int32> OutputBuffer;

	//////////////////////////////////////////////////////////////////////
	// 能源

	/** 本建筑需要的功率。实际值取自配方。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	float PowerDraw = 0.f;

	/**
	 * 供电满足率，0~1。
	 * 由电网每帧写入。供电不足时加工变慢而不是停下——
	 * 这样玩家能看到「电力紧张」的信号，而不是「莫名卡住」。
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PowerSatisfaction = 1.0f;

	/** 是否已接入电网。未接入时按 bStandalonePowerRatio 工作。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Power")
	bool bConnectedToGrid = false;

	/**
	 * 未接入电网时的供电比例。
	 *
	 * 默认 0 —— 没电就不工作。这让「忘记接电」是一个明显的错误，
	 * 而不是默默满速运行骗玩家。
	 * 调试时可以直接调成 1.0 跳过电力系统。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Power", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StandalonePowerRatio = 0.0f;

	//////////////////////////////////////////////////////////////////////
	// 状态

	/** 是否正在加工（已扣料，进度未满）。 */
	UFUNCTION(BlueprintPure, Category = "Production")
	bool IsCrafting() const { return bCrafting; }

	/** 当前加工进度，0~1。 */
	UFUNCTION(BlueprintPure, Category = "Production")
	float GetProgress() const { return CraftTime > 0.f ? float(CraftProgress / CraftTime) : 0.f; }

	/** 库存里某物品的数量。 */
	UFUNCTION(BlueprintPure, Category = "Production")
	int32 GetInputCount(EItemType Item) const;

	UFUNCTION(BlueprintPure, Category = "Production")
	int32 GetOutputCount(EItemType Item) const;

	/** 配方所需材料是否已齐。 */
	UFUNCTION(BlueprintPure, Category = "Production")
	bool HasAllInputs() const;

	/** 输出槽是否有空间容纳产物。 */
	UFUNCTION(BlueprintPure, Category = "Production")
	bool HasOutputSpace() const;

	//////////////////////////////////////////////////////////////////////
	// 外部操作

	/**
	 * 投入物品。返回实际投入的数量。
	 * 槽满或配方不需要这种物品都会导致投入失败。
	 */
	UFUNCTION(BlueprintCallable, Category = "Production")
	int32 TryAddInput(EItemType Item, int32 Count);

	/** 取出物品。 */
	UFUNCTION(BlueprintCallable, Category = "Production")
	int32 TakeOutput(EItemType Item, int32 Count);

	/** 清空两个槽位。 */
	UFUNCTION(BlueprintCallable, Category = "Production")
	void ClearBuffers();

	/** 手动启动一次加工。失败返回 false。 */
	UFUNCTION(BlueprintCallable, Category = "Production")
	bool TryStartCrafting();

	//////////////////////////////////////////////////////////////////////
	// 电网接入

	/**
	 * 接入一个发电站。由 APowerPlant 在扫描时调用。
	 * 接入后供电率由电网决定。
	 */
	UFUNCTION(BlueprintCallable, Category = "Power")
	void ConnectToGrid(AActor* Plant);

	/** 断开电网，回到 StandalonePowerRatio。 */
	UFUNCTION(BlueprintCallable, Category = "Power")
	void DisconnectFromGrid();

	/** 是否已通电（供电率大于 0）。 */
	UFUNCTION(BlueprintPure, Category = "Power")
	bool HasPower() const { return PowerSatisfaction > 0.001f; }

	//////////////////////////////////////////////////////////////////////
	// 外观

	/** 建筑网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	TObjectPtr<UStaticMesh> BuildingMesh;

	/** 工作指示灯颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor CraftingColor = FLinearColor(0.3f, 1.f, 0.4f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor IdleColor = FLinearColor(0.6f, 0.6f, 0.6f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	FLinearColor StarvedColor = FLinearColor(1.f, 0.7f, 0.2f);

	/** 加工进度条的最大世界尺寸（厘米）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visual")
	float ProgressBarSize = 200.f;

protected:
	/** 每帧更新供电状态。电网可以调它，也可以让它自己算。 */
	void UpdatePower(float DeltaSeconds);

	/** 检查是否能开工。 */
	void EvaluateCrafting(float DeltaSeconds);

	/** 完成加工，产出结果。 */
	void FinishCrafting();

	/** 组装类配方：生成建筑 Actor。 */
	void SpawnProducedActor();

	/** 更新指示灯颜色。 */
	void UpdateStatusLight();

	/**
	 * 按建筑分类切换占位网格。
	 *
	 * 全部用引擎自带的基础形状（Cube / Sphere / Cylinder），
	 * 不依赖任何美术资源。形状 + 缩放共同区分建筑类型：
	 *   冶炼=扁平方块　零件=立方体　组装=竖长方块　发电=圆柱
	 *
	 * 美术资源到位后，只要在 Details 里指定 BuildingMesh 就会被覆盖。
	 */
	void ApplyPlaceholderMesh();

private:
	/** 是否正在加工。 */
	bool bCrafting = false;

	/** 已累积的加工时间（秒）。 */
	float CraftProgress = 0.f;

	/** 单次加工需要的总时间，来自配方。 */
	float CraftTime = 1.f;

	/** 接入的发电站。用弱引用，电厂被拆不会悬空。 */
	TWeakObjectPtr<AActor> ConnectedPlant;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, Category = "Visual")
	TObjectPtr<UStaticMeshComponent> ProgressBarMesh;

	/** 缓存的动态材质实例。BeginPlay 创建，之后只改参数不重建。 */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> CachedMID;
};
