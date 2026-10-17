// 工厂系统的物品与配方定义。
//
// 这是整个系统的骨架：所有生产行为都由配方驱动，没有第二套逻辑。
// 冶炼炉、零件厂、组装厂共用AProcessingBuilding，行为差异全靠配方。
//
// 物品种类上限 12：玩家能记住的大约是 12±3，超过就会去查 wiki，
// 而查 wiki 意味着游戏设计失败。

#pragma once

#include "CoreMinimal.h"
#include "CoreMinimal.h"
#include "FactoryRecipe.generated.h"

/**
 * 统一物品枚举。
 *
 * 原料 / 材料 / 零件共用同一个槽位，所以必须是统一枚举。
 * EOreType（FactoryTypes.h）只用于矿石丰富度分布，这里是实际物品。
 */
UENUM(BlueprintType)
enum class EItemType : uint8
{
	None = 0,

	// ── 原料（6 种）── 挖掘机直接产出
	IronOre,
	CopperOre,
	SiliconOre,
	TitaniumOre,
	UraniumOre,
	Water,

	// ── 材料（3 种）── 冶炼产出
	IronIngot,
	CopperIngot,
	SiliconIngot,

	// ── 零件（4 种）──
	Gear,
	Circuit,
	Beam,
	Propellant,

	Count
};

/** 建筑分类。决定外观与槽位数量，不影响配方逻辑。 */
UENUM(BlueprintType)
enum class EBuildingCategory : uint8
{
	/** 冶炼。1 进 1 出。 */
	Smelter,
	/** 零件。2 进 1 出。 */
	Parts,
	/** 组装。4 进 1 出。 */
	Assembly,
	/** 发电。 */
	Power
};

/**
 * 一条配方输入或输出。
 */
USTRUCT(BlueprintType)
struct FRecipeIO
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	EItemType Item = EItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "1"))
	int32 Count = 1;

	FRecipeIO() = default;
	FRecipeIO(EItemType InItem, int32 InCount)
		: Item(InItem), Count(InCount) {}
};

/**
 * 一条配方。
 *
 * 所有生产行为都从这里读。加新配方 = 加数据，不改代码。
 */
USTRUCT(BlueprintType)
struct FFactoryRecipe
{
	GENERATED_BODY()

	/** 唯一标识，存档用。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	FName Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	FText DisplayName;

	/** 输入材料。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	TArray<FRecipeIO> Inputs;

	/** 输出材料。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	TArray<FRecipeIO> Outputs;

	/** 加工一次耗时，单位秒。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "0.01"))
	float CraftTime = 1.0f;

	/** 运行时耗能，单位「每秒」。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (ClampMin = "0.0"))
	float PowerDraw = 5.0f;

	/** 建筑分类，决定外观与槽位数量。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	EBuildingCategory Category = EBuildingCategory::Parts;

	/**
	 * 这条配方完成后是否生成一个 Actor。
	 *
	 * 组装类配方（行星发动机、发电机、轨道船）不是「生产物品」，
	 * 而是「建造建筑」——完成后消耗配方、生成一个可放置的 Actor。
	 * 这类配方没有 Outputs，用这个标志区分。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe")
	bool bProducesActor = false;

	/** bProducesActor 为 true 时，生成哪种建筑。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recipe", meta = (EditCondition = "bProducesActor"))
	TSubclassOf<AActor> ProducedActor;

	//── 便捷查询 ─────────────────────────────────────────────

	/** 输入种类数。槽位数量按这个分配。 */
	int32 GetInputSlotCount() const { return Inputs.Num(); }

	/** 某物品在输入需求里的数量。不需要返回 0。 */
	int32 GetInputCount(EItemType Item) const
	{
		for (const FRecipeIO& IO : Inputs)
		{
			if (IO.Item == Item)
			{
				return IO.Count;
			}
		}
		return 0;
	}

	int32 GetOutputCount(EItemType Item) const
	{
		for (const FRecipeIO& IO : Outputs)
		{
			if (IO.Item == Item)
			{
				return IO.Count;
			}
		}
		return 0;
	}

	/** 配方是否合法。 */
	bool IsValid(FString& OutError) const
	{
		if (Inputs.Num() == 0)
		{
			OutError = TEXT("配方没有输入");
			return false;
		}
		// 组装类配方可以没有输出 —— 它完成后变成一个可放置的 Actor，
		// 不产生可堆叠物品。这种配方需要勾选 bProducesActor。
		if (Outputs.Num() == 0 && !bProducesActor)
		{
			OutError = TEXT("配方没有输出（若用于组装，请勾选 Produces Actor）");
			return false;
		}
		if (bProducesActor && !ProducedActor)
		{
			OutError = TEXT("勾选了 Produces Actor 但没有指定生成什么");
			return false;
		}
		if (CraftTime <= 0.f)
		{
			OutError = TEXT("CraftTime 必须大于 0");
			return false;
		}
		for (const FRecipeIO& IO : Inputs)
		{
			if (IO.Item == EItemType::None || IO.Count <= 0)
			{
				OutError = TEXT("输入项非法");
				return false;
			}
		}
		return true;
	}
};

/**
 * 配方库数据资产。
 *
 * 用 DataAsset 而不是硬编码常量，好处：
 *   · 策划能直接在编辑器里改数值，不用编译
 *   · 存档只需存 FRecipe::Id
 *   · 可以做多个配方库（不同星球难度、不同游戏模式）
 */
UCLASS(BlueprintType)
class VOXELFACTORY_API URecipeLibrary : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 全部配方。 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FFactoryRecipe> Recipes;

	/** 按 Id 查找配方。找不到返回 nullptr。 */
	UFUNCTION(BlueprintPure, Category = "Recipe")
	const FFactoryRecipe* FindRecipe(FName Id) const;

	/**
	 * 检查整个配方库是否合法。
	 * 检查项：重复 Id、空配方、配方循环依赖。
	 *
	 * 循环依赖是致命错误——A 的产物是 B 的原料，B 的产物又是 A 的原料，
	 * 会让加工进度永远无法完成。
	 *
	 * @param OutError 出错时填充具体原因
	 * @return 是否全部合法
	 */
	UFUNCTION(BlueprintCallable, Category = "Recipe")
	bool ValidateLibrary(FString& OutError) const;

	/**
	 * 把挖掘机的矿石类型转成物品类型。
	 * 挖掘机产出矿石物品，不直接产出锭。
	 */
	UFUNCTION(BlueprintPure, Category = "Recipe")
	static EItemType OreToItem(EOreType Ore);

	/** 物品的显示名。存档和 UI 共用。 */
	UFUNCTION(BlueprintPure, Category = "Recipe")
	static FText GetItemDisplayName(EItemType Item);
};
