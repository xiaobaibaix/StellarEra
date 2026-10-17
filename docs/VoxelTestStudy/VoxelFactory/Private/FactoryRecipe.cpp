// 配方库实现 + 默认配方表。

#include "FactoryRecipe.h"
#include "FactoryTypes.h"

///////////////////////////////////////////////////////////////////////////////
// 查找与校验

const FFactoryRecipe* URecipeLibrary::FindRecipe(FName Id) const
{
	for (const FFactoryRecipe& Recipe : Recipes)
	{
		if (Recipe.Id == Id)
		{
			return &Recipe;
		}
	}
	return nullptr;
}

bool URecipeLibrary::ValidateLibrary(FString& OutError) const
{
	// ── 1. Id 唯一性 ──
	TSet<FName> SeenIds;
	for (const FFactoryRecipe& Recipe : Recipes)
	{
		if (Recipe.Id.IsNone())
		{
			OutError = TEXT("存在没有 Id 的配方");
			return false;
		}
		if (SeenIds.Contains(Recipe.Id))
		{
			OutError = FString::Printf(TEXT("配方 Id 重复：%s"), *Recipe.Id.ToString());
			return false;
		}
		SeenIds.Add(Recipe.Id);
	}

	// ── 2. 单条配方合法性 ──
	for (const FFactoryRecipe& Recipe : Recipes)
	{
		FString Error;
		if (!Recipe.IsValid(Error))
		{
			OutError = FString::Printf(TEXT("配方 %s 非法：%s"), *Recipe.Id.ToString(), *Error);
			return false;
		}
	}

	// ── 3. 循环依赖检测 ──
	//
	// 建图：物品 → 哪些配方需要它作为输入
	// 然后从每个物品出发做深度优先，看能不能走回自己。
	//
	// 循环依赖是致命错误：A 的产物是 B 的原料，B 的产物又是 A 的原料，
	// 会让加工永远无法完成，而且玩家完全看不出原因。

	TMap<EItemType, TArray<int32>> Graph;
	for (int32 i = 0; i < Recipes.Num(); ++i)
	{
		const FFactoryRecipe& Recipe = Recipes[i];

		// 输出加入图
		for (const FRecipeIO& Out : Recipe.Outputs)
		{
			Graph.FindOrAdd(Out.Item).Add(i);
		}
	}

	// 三色DFS：0=未访问 1=在栈上 2=已完成
	TArray<int8> State;
	State.Init(0, Recipes.Num());

	TFunction<bool(int32)> HasCycle = [&](int32 RecipeIndex) -> bool
	{
		State[RecipeIndex] = 1;   // 标记为「在栈上」

		const FFactoryRecipe& Recipe = Recipes[RecipeIndex];

		// 递归检查「本配方需要的每种原料」
		for (const FRecipeIO& In : Recipe.Inputs)
		{
			const TArray<int32>* Producers = Graph.Find(In.Item);
			if (!Producers)
			{
				continue;   // 没有配方产出它 → 应该是挖机产出，不是循环
			}

			for (int32 ProducerIndex : *Producers)
			{
				if (State[ProducerIndex] == 1)
				{
					// 又回到栈上的节点 → 找到环
					OutError = FString::Printf(
						TEXT("配方存在循环依赖：%s 需要 %s，而 %s 又依赖回来"),
						*Recipe.Id.ToString(),
						*UEnum::GetValueAsString(In.Item),
						*Recipes[ProducerIndex].Id.ToString());
					return true;
				}

				if (State[ProducerIndex] == 0 && HasCycle(ProducerIndex))
				{
					return true;
				}
			}
		}

		State[RecipeIndex] = 2;   // 标记为「已完成」
		return false;
	};

	for (int32 i = 0; i < Recipes.Num(); ++i)
	{
		if (State[i] == 0 && HasCycle(i))
		{
			return false;
		}
	}

	OutError = TEXT("全部合法");
	return true;
}

///////////////////////////////////////////////////////////////////////////////
// 物品转换

EItemType URecipeLibrary::OreToItem(EOreType Ore)
{
	switch (Ore)
	{
	case EOreType::Iron:      return EItemType::IronOre;
	case EOreType::Copper:    return EItemType::CopperOre;
	case EOreType::Silicon:   return EItemType::SiliconOre;
	case EOreType::Titanium:  return EItemType::TitaniumOre;
	case EOreType::Uranium:   return EItemType::UraniumOre;
	case EOreType::Water:     return EItemType::Water;
	default:                  return EItemType::None;
	}
}

FText URecipeLibrary::GetItemDisplayName(EItemType Item)
{
	switch (Item)
	{
	case EItemType::IronOre:       return FText::FromString(TEXT("铁矿石"));
	case EItemType::CopperOre:     return FText::FromString(TEXT("铜矿石"));
	case EItemType::SiliconOre:    return FText::FromString(TEXT("硅矿石"));
	case EItemType::TitaniumOre:   return FText::FromString(TEXT("钛矿石"));
	case EItemType::UraniumOre:    return FText::FromString(TEXT("铀矿石"));
	case EItemType::Water:         return FText::FromString(TEXT("水"));

	case EItemType::IronIngot:     return FText::FromString(TEXT("铁锭"));
	case EItemType::CopperIngot:   return FText::FromString(TEXT("铜锭"));
	case EItemType::SiliconIngot:  return FText::FromString(TEXT("硅锭"));

	case EItemType::Gear:          return FText::FromString(TEXT("齿轮"));
	case EItemType::Circuit:       return FText::FromString(TEXT("电路板"));
	case EItemType::Beam:          return FText::FromString(TEXT("结构梁"));
	case EItemType::Propellant:    return FText::FromString(TEXT("推进剂"));

	default:                       return FText::FromString(TEXT("未知"));
	}
}

///////////////////////////////////////////////////////////////////////////////
// 默认配方表
//
// 单独提供一个静态构造函数，方便在测试和控制台创建配方库。
// 正常流程是用 DataAsset 在编辑器里配，但这份保证代码能自测。

namespace FactoryRecipeDefaults
{
	void PopulateDefaultRecipes(URecipeLibrary* Library)
	{
		if (!Library)
		{
			return;
		}

		Library->Recipes.Reset();
		Library->Recipes.Reserve(11);

		// ── ② 冶炼（一进一出）────────────────────────────
		// 水的消耗给冶炼一个持续性成本。
		// 想让玩家专注主线的话可以把它设成 0。

		FFactoryRecipe& SmeltIron = Library->Recipes.AddDefaulted_GetRef();
		SmeltIron.Id = TEXT("SmeltIron");
		SmeltIron.DisplayName = FText::FromString(TEXT("铁锭"));
		SmeltIron.Inputs = { FRecipeIO(EItemType::IronOre, 1), FRecipeIO(EItemType::Water, 2) };
		SmeltIron.Outputs = { FRecipeIO(EItemType::IronIngot, 1) };
		SmeltIron.CraftTime = 2.0f;
		SmeltIron.PowerDraw = 10.f;
		SmeltIron.Category = EBuildingCategory::Smelter;

		FFactoryRecipe& SmeltCopper = Library->Recipes.AddDefaulted_GetRef();
		SmeltCopper.Id = TEXT("SmeltCopper");
		SmeltCopper.DisplayName = FText::FromString(TEXT("铜锭"));
		SmeltCopper.Inputs = { FRecipeIO(EItemType::CopperOre, 1), FRecipeIO(EItemType::Water, 2) };
		SmeltCopper.Outputs = { FRecipeIO(EItemType::CopperIngot, 1) };
		SmeltCopper.CraftTime = 2.0f;
		SmeltCopper.PowerDraw = 10.f;
		SmeltCopper.Category = EBuildingCategory::Smelter;

		FFactoryRecipe& SmeltSilicon = Library->Recipes.AddDefaulted_GetRef();
		SmeltSilicon.Id = TEXT("SmeltSilicon");
		SmeltSilicon.DisplayName = FText::FromString(TEXT("硅锭"));
		SmeltSilicon.Inputs = { FRecipeIO(EItemType::SiliconOre, 1), FRecipeIO(EItemType::Water, 2) };
		SmeltSilicon.Outputs = { FRecipeIO(EItemType::SiliconIngot, 1) };
		SmeltSilicon.CraftTime = 2.5f;
		SmeltSilicon.PowerDraw = 12.f;
		SmeltSilicon.Category = EBuildingCategory::Smelter;

		// ── ③ 零件（一对多，复杂度关键层）─────────────────

		FFactoryRecipe& MakeGear = Library->Recipes.AddDefaulted_GetRef();
		MakeGear.Id = TEXT("MakeGear");
		MakeGear.DisplayName = FText::FromString(TEXT("齿轮"));
		MakeGear.Inputs = { FRecipeIO(EItemType::IronIngot, 2) };
		MakeGear.Outputs = { FRecipeIO(EItemType::Gear, 1) };
		MakeGear.CraftTime = 1.5f;
		MakeGear.PowerDraw = 8.f;
		MakeGear.Category = EBuildingCategory::Parts;

		FFactoryRecipe& MakeCircuit = Library->Recipes.AddDefaulted_GetRef();
		MakeCircuit.Id = TEXT("MakeCircuit");
		MakeCircuit.DisplayName = FText::FromString(TEXT("电路板"));
		MakeCircuit.Inputs = { FRecipeIO(EItemType::SiliconIngot, 1), FRecipeIO(EItemType::CopperIngot, 1) };
		MakeCircuit.Outputs = { FRecipeIO(EItemType::Circuit, 1) };
		MakeCircuit.CraftTime = 3.0f;
		MakeCircuit.PowerDraw = 15.f;
		MakeCircuit.Category = EBuildingCategory::Parts;

		FFactoryRecipe& MakeBeam = Library->Recipes.AddDefaulted_GetRef();
		MakeBeam.Id = TEXT("MakeBeam");
		MakeBeam.DisplayName = FText::FromString(TEXT("结构梁"));
		MakeBeam.Inputs = { FRecipeIO(EItemType::IronIngot, 3), FRecipeIO(EItemType::CopperIngot, 1) };
		MakeBeam.Outputs = { FRecipeIO(EItemType::Beam, 1) };
		MakeBeam.CraftTime = 4.0f;
		MakeBeam.PowerDraw = 20.f;
		MakeBeam.Category = EBuildingCategory::Parts;

		// 推进剂直接吃钛矿石 —— 钛是稀有种，逼迫玩家去探矿
		FFactoryRecipe& MakePropellant = Library->Recipes.AddDefaulted_GetRef();
		MakePropellant.Id = TEXT("MakePropellant");
		MakePropellant.DisplayName = FText::FromString(TEXT("推进剂"));
		MakePropellant.Inputs = { FRecipeIO(EItemType::Water, 3), FRecipeIO(EItemType::TitaniumOre, 1) };
		MakePropellant.Outputs = { FRecipeIO(EItemType::Propellant, 1) };
		MakePropellant.CraftTime = 3.5f;
		MakePropellant.PowerDraw = 18.f;
		MakePropellant.Category = EBuildingCategory::Parts;

		// ── ④ 组装（玩家目标）────────────────────────────

		// 引擎需要全部 4 种零件 → 强迫玩家把四条产线都建起来
		FFactoryRecipe& AssembleEngine = Library->Recipes.AddDefaulted_GetRef();
		AssembleEngine.Id = TEXT("AssemblePlanetaryEngine");
		AssembleEngine.DisplayName = FText::FromString(TEXT("行星发动机"));
		AssembleEngine.Inputs = {
			FRecipeIO(EItemType::Gear, 8),
			FRecipeIO(EItemType::Circuit, 4),
			FRecipeIO(EItemType::Beam, 6),
			FRecipeIO(EItemType::Propellant, 12)
		};
		// 组装不产出物品，改为生成一个可放置的 Actor
		AssembleEngine.bProducesActor = true;
		// ProducedActor 在编辑器里指定：
		// Engine 对应的建筑蓝图
		// AssembleEngine.ProducedActor = nullptr;
		AssembleEngine.CraftTime = 20.0f;
		AssembleEngine.PowerDraw = 100.f;
		AssembleEngine.Category = EBuildingCategory::Assembly;

		FFactoryRecipe& AssembleGenerator = Library->Recipes.AddDefaulted_GetRef();
		AssembleGenerator.Id = TEXT("AssembleGenerator");
		AssembleGenerator.DisplayName = FText::FromString(TEXT("发电机"));
		AssembleGenerator.Inputs = {
			FRecipeIO(EItemType::Gear, 4),
			FRecipeIO(EItemType::Circuit, 2),
			FRecipeIO(EItemType::CopperIngot, 6)
		};
		AssembleGenerator.bProducesActor = true;
		// ProducedActor 在编辑器里指定：
		// Generator 对应的建筑蓝图
		// AssembleGenerator.ProducedActor = nullptr;
		AssembleGenerator.CraftTime = 12.0f;
		AssembleGenerator.PowerDraw = 60.f;
		AssembleGenerator.Category = EBuildingCategory::Assembly;

		FFactoryRecipe& AssembleTransport = Library->Recipes.AddDefaulted_GetRef();
		AssembleTransport.Id = TEXT("AssembleOrbitalTransport");
		AssembleTransport.DisplayName = FText::FromString(TEXT("轨道运输船"));
		AssembleTransport.Inputs = {
			FRecipeIO(EItemType::Beam, 10),
			FRecipeIO(EItemType::Propellant, 8),
			FRecipeIO(EItemType::Circuit, 2)
		};
		AssembleTransport.bProducesActor = true;
		// ProducedActor 在编辑器里指定：
		// Transport 对应的建筑蓝图
		// AssembleTransport.ProducedActor = nullptr;
		AssembleTransport.CraftTime = 25.0f;
		AssembleTransport.PowerDraw = 80.f;
		AssembleTransport.Category = EBuildingCategory::Assembly;
	}
}
