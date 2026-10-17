// 配方库验证 —— 不依赖 UE，可独立运行。
//
// 用法：
//   clang++ -std=c++17 FactoryRecipeTest.cpp -o test && ./test
//
// 目的：在进 UE 编译之前，先验证配方表的设计是否正确。
// 配方表是整个工厂系统的骨架，一旦有循环依赖或物品缺失，
// 后面所有代码都会建在错误的地基上。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

// ── 最小 UE 类型替身 ────────────────────────────────────────
// 只实现配方逻辑需要的部分，不模拟 UE 的任何行为。

// UE 用的定长整数别名，纯 C++ 里需要自己定义
using uint8 = std::uint8_t;
using int32 = std::int32_t;
using int8 = std::int8_t;

enum class EItemType : uint8
{
	None = 0,
	IronOre, CopperOre, SiliconOre, TitaniumOre, UraniumOre, Water,
	IronIngot, CopperIngot, SiliconIngot,
	Gear, Circuit, Beam, Propellant,
	Count
};

enum class EBuildingCategory : uint8
{
	Smelter, Parts, Assembly, Power
};

struct FRecipeIO
{
	EItemType Item = EItemType::None;
	int32 Count = 1;
	FRecipeIO() = default;
	FRecipeIO(EItemType I, int32 C) : Item(I), Count(C) {}
};

struct FFactoryRecipe
{
	std::string Id;
	std::string DisplayName;
	std::vector<FRecipeIO> Inputs;
	std::vector<FRecipeIO> Outputs;
	float CraftTime = 1.0f;
	float PowerDraw = 5.0f;
	EBuildingCategory Category = EBuildingCategory::Parts;
	bool bProducesActor = false;

	int32 GetInputCount(EItemType Item) const
	{
		for (const auto& IO : Inputs) { if (IO.Item == Item) return IO.Count; }
		return 0;
	}
	int32 GetOutputCount(EItemType Item) const
	{
		for (const auto& IO : Outputs) { if (IO.Item == Item) return IO.Count; }
		return 0;
	}
	bool IsValid(std::string& OutError) const
	{
		if (Inputs.empty()) { OutError = "配方没有输入"; return false; }
		if (Outputs.empty() && !bProducesActor) { OutError = "配方没有输出"; return false; }
		if (CraftTime <= 0.f) { OutError = "CraftTime 必须大于 0"; return false; }
		for (const auto& IO : Inputs)
		{
			if (IO.Item == EItemType::None || IO.Count <= 0) { OutError = "输入项非法"; return false; }
		}
		return true;
	}
};

static const char* ItemName(EItemType I)
{
	switch (I)
	{
	case EItemType::IronOre:     return "铁矿石";
	case EItemType::CopperOre:   return "铜矿石";
	case EItemType::SiliconOre:  return "硅矿石";
	case EItemType::TitaniumOre: return "钛矿石";
	case EItemType::UraniumOre:  return "铀矿石";
	case EItemType::Water:       return "水";
	case EItemType::IronIngot:   return "铁锭";
	case EItemType::CopperIngot: return "铜锭";
	case EItemType::SiliconIngot:return "硅锭";
	case EItemType::Gear:        return "齿轮";
	case EItemType::Circuit:     return "电路板";
	case EItemType::Beam:        return "结构梁";
	case EItemType::Propellant:  return "推进剂";
	default:                     return "无";
	}
}

// ── 配方库 ──────────────────────────────────────────────────

struct FRecipeLibrary
{
	std::vector<FFactoryRecipe> Recipes;

	const FFactoryRecipe* Find(const std::string& Id) const
	{
		for (const auto& R : Recipes) { if (R.Id == Id) return &R; }
		return nullptr;
	}

	bool Validate(std::string& OutError) const
	{
		// 1. Id唯一
		std::set<std::string> Seen;
		for (const auto& R : Recipes)
		{
			if (R.Id.empty()) { OutError = "存在没有 Id 的配方"; return false; }
			if (Seen.count(R.Id)) { OutError = "配方 Id 重复：" + R.Id; return false; }
			Seen.insert(R.Id);
		}

		// 2. 单条合法
		for (const auto& R : Recipes)
		{
			std::string Err;
			if (!R.IsValid(Err)) { OutError = "配方 " + R.Id + " 非法：" + Err; return false; }
		}

		// 3. 循环依赖（三色DFS）
		std::map<EItemType, std::vector<int>> Graph;
		for (size_t i = 0; i < Recipes.size(); ++i)
		{
			for (const auto& Out : Recipes[i].Outputs)
			{
				Graph[Out.Item].push_back(int(i));
			}
		}

		std::vector<int8_t> State(Recipes.size(), 0);

		std::function<bool(int)> HasCycle = [&](int Idx) -> bool
		{
			State[Idx] = 1;
			for (const auto& In : Recipes[Idx].Inputs)
			{
				auto It = Graph.find(In.Item);
				if (It == Graph.end()) continue;
				for (int P : It->second)
				{
					if (State[P] == 1)
					{
						OutError = "循环依赖：" + Recipes[Idx].Id
							+ " 需要 " + ItemName(In.Item)
							+ "，而 " + Recipes[P].Id + " 又依赖回来";
						return true;
					}
					if (State[P] == 0 && HasCycle(P)) return true;
				}
			}
			State[Idx] = 2;
			return false;
		};

		for (size_t i = 0; i < Recipes.size(); ++i)
		{
			if (State[i] == 0 && HasCycle(int(i))) return false;
		}

		OutError = "全部合法";
		return true;
	}
};

// ── 默认配方表（与 FactoryRecipe.cpp 保持一致）───────────────

static void PopulateDefaults(FRecipeLibrary& Lib)
{
	auto Add = [&](const char* Id, const char* Name,
		std::vector<FRecipeIO> In, std::vector<FRecipeIO> Out,
		float Time, float Power, EBuildingCategory Cat, bool bActor = false) -> FFactoryRecipe&
	{
		FFactoryRecipe R;
		R.Id = Id; R.DisplayName = Name;
		R.Inputs = In; R.Outputs = Out;
		R.CraftTime = Time; R.PowerDraw = Power;
		R.Category = Cat; R.bProducesActor = bActor;
		Lib.Recipes.push_back(R);
		return Lib.Recipes.back();
	};

	// 冶炼
	Add("SmeltIron", "铁锭", {{EItemType::IronOre,1},{EItemType::Water,2}},
		{{EItemType::IronIngot,1}}, 2.0f, 10.f, EBuildingCategory::Smelter);
	Add("SmeltCopper", "铜锭", {{EItemType::CopperOre,1},{EItemType::Water,2}},
		{{EItemType::CopperIngot,1}}, 2.0f, 10.f, EBuildingCategory::Smelter);
	Add("SmeltSilicon", "硅锭", {{EItemType::SiliconOre,1},{EItemType::Water,2}},
		{{EItemType::SiliconIngot,1}}, 2.5f, 12.f, EBuildingCategory::Smelter);

	// 零件
	Add("MakeGear", "齿轮", {{EItemType::IronIngot,2}},
		{{EItemType::Gear,1}}, 1.5f, 8.f, EBuildingCategory::Parts);
	Add("MakeCircuit", "电路板", {{EItemType::SiliconIngot,1},{EItemType::CopperIngot,1}},
		{{EItemType::Circuit,1}}, 3.0f, 15.f, EBuildingCategory::Parts);
	Add("MakeBeam", "结构梁", {{EItemType::IronIngot,3},{EItemType::CopperIngot,1}},
		{{EItemType::Beam,1}}, 4.0f, 20.f, EBuildingCategory::Parts);
	Add("MakePropellant", "推进剂", {{EItemType::Water,3},{EItemType::TitaniumOre,1}},
		{{EItemType::Propellant,1}}, 3.5f, 18.f, EBuildingCategory::Parts);

	// 组装
	Add("AssembleEngine", "行星发动机",
		{{EItemType::Gear,8},{EItemType::Circuit,4},{EItemType::Beam,6},{EItemType::Propellant,12}},
		{}, 20.0f, 100.f, EBuildingCategory::Assembly, true);
	Add("AssembleGenerator", "发电机",
		{{EItemType::Gear,4},{EItemType::Circuit,2},{EItemType::CopperIngot,6}},
		{}, 12.0f, 60.f, EBuildingCategory::Assembly, true);
	Add("AssembleTransport", "轨道运输船",
		{{EItemType::Beam,10},{EItemType::Propellant,8},{EItemType::Circuit,2}},
		{}, 25.0f, 80.f, EBuildingCategory::Assembly, true);
}

// ── 测试 ────────────────────────────────────────────────────

static int FailCount = 0;

static void Check(bool bCondition, const char* What)
{
	if (bCondition)
	{
		printf("  [OK]   %s\n", What);
	}
	else
	{
		printf("  [FAIL] %s\n", What);
		++FailCount;
	}
}

int main()
{
	printf("===== 配方库验证 =====\n\n");

	// ── 测试 1：默认配方表合法性 ──
	printf("测试 1：默认配方表\n");
	{
		FRecipeLibrary Lib;
		PopulateDefaults(Lib);

		std::string Err;
		const bool bOk = Lib.Validate(Err);
		Check(bOk, Err.c_str());
		Check(Lib.Recipes.size() == 10, "配方数量为 10");
	}

	// ── 测试 2：循环依赖检测 ──
	printf("\n测试 2：循环依赖检测\n");
	{
		FRecipeLibrary Lib;
		// 故意构造一个环：A 产出齿轮，造齿轮要 A
		FFactoryRecipe R1; R1.Id = "MakeX"; R1.Inputs = {{EItemType::Gear, 1}};
		R1.Outputs = {{EItemType::IronIngot, 1}}; R1.CraftTime = 1.f;
		FFactoryRecipe R2; R2.Id = "MakeGear"; R2.Inputs = {{EItemType::IronIngot, 2}};
		R2.Outputs = {{EItemType::Gear, 1}}; R2.CraftTime = 1.f;
		Lib.Recipes = { R1, R2 };

		std::string Err;
		Check(!Lib.Validate(Err), "检出循环依赖");
		printf("         原因：%s\n", Err.c_str());
	}

	// ── 测试 3：物品数量 ──
	printf("\n测试 3：物品与配方设计\n");
	{
		FRecipeLibrary Lib;
		PopulateDefaults(Lib);

		// 统计被用到的不同物品
		std::set<EItemType> Used;
		for (const auto& R : Lib.Recipes)
		{
			for (const auto& IO : R.Inputs)  Used.insert(IO.Item);
			for (const auto& IO : R.Outputs) Used.insert(IO.Item);
		}

		printf("         用到的物品种类：%zu\n", Used.size());
		Check(Used.size() <= 15, "物品种类不超过 15");

		// 行星发动机需要全部 4 种零件（设计意图验证）
		const FFactoryRecipe* Engine = Lib.Find("AssembleEngine");
		Check(Engine != nullptr, "存在行星发动机配方");

		if (Engine)
		{
			Check(Engine->GetInputCount(EItemType::Gear) > 0,        "引擎需要齿轮");
			Check(Engine->GetInputCount(EItemType::Circuit) > 0,     "引擎需要电路板");
			Check(Engine->GetInputCount(EItemType::Beam) > 0,        "引擎需要结构梁");
			Check(Engine->GetInputCount(EItemType::Propellant) > 0,  "引擎需要推进剂");
			Check(Engine->bProducesActor, "引擎配方标记为生成 Actor");
		}

		// 推进剂直接吃钛矿石（逼迫探矿的设计意图）
		const FFactoryRecipe* Prop = Lib.Find("MakePropellant");
		Check(Prop && Prop->GetInputCount(EItemType::TitaniumOre) > 0, "推进剂需要钛矿石");
	}

	// ── 测试 4：溯源检查 ──
	printf("\n测试 4：成品溯源（每种物品都能追到挖机原料）\n");
	{
		FRecipeLibrary Lib;
		PopulateDefaults(Lib);

		// DFS + 递归栈判环。
		// 注意：Visited 必须是「当前递归栈」而不是「全局访问过」——
		// 否则多条配方路径共用同一种原料时会被误判成环。
		std::set<EItemType> OnStack;

		std::function<bool(EItemType)> CanTrace = [&](EItemType Item) -> bool
		{
			if (OnStack.count(Item))
			{
				return false;   // 回到栈上的节点 → 环
			}

			// 没配方产出它 → 必须是挖机原料
			bool bIsRawOre =
				Item == EItemType::IronOre || Item == EItemType::CopperOre ||
				Item == EItemType::SiliconOre || Item == EItemType::TitaniumOre ||
				Item == EItemType::UraniumOre || Item == EItemType::Water;

			bool bProduced = false;
			for (const auto& R : Lib.Recipes)
			{
				if (R.GetOutputCount(Item) > 0) { bProduced = true; break; }
			}

			if (!bProduced)
			{
				if (bIsRawOre) return true;
				printf("         %s 既无配方产出，也不是挖机原料 → 无法获得\n", ItemName(Item));
				return false;
			}

			OnStack.insert(Item);

			bool bAllOk = true;
			for (const auto& R : Lib.Recipes)
			{
				if (R.GetOutputCount(Item) <= 0) continue;
				for (const auto& In : R.Inputs)
				{
					if (!CanTrace(In.Item)) { bAllOk = false; break; }
				}
				if (!bAllOk) break;
			}

			OnStack.erase(Item);   // 回溯
			return bAllOk;
		};

		// 逐项检查所有可获得的物品
		const EItemType AllItems[] = {
			EItemType::IronIngot, EItemType::CopperIngot, EItemType::SiliconIngot,
			EItemType::Gear, EItemType::Circuit, EItemType::Beam, EItemType::Propellant
		};

		bool bAllTraceable = true;
		for (EItemType Item : AllItems)
		{
			OnStack.clear();
			if (!CanTrace(Item))
			{
				printf("         溯源失败：%s\n", ItemName(Item));
				bAllTraceable = false;
			}
		}
		Check(bAllTraceable, "所有中间品都能溯源到挖机原料");

		// 反向检查：铀矿石有配方产出吗？（它应该只能由挖机产出）
		bool bUraniumProduced = false;
		for (const auto& R : Lib.Recipes)
		{
			if (R.GetOutputCount(EItemType::UraniumOre) > 0) bUraniumProduced = true;
		}
		Check(!bUraniumProduced, "铀矿石只能由挖机产出（当前未做成配方，属预留）");
	}

	printf("\n===== 结果 =====\n");
	if (FailCount == 0)
	{
		printf("全部通过 ✓\n");
		return 0;
	}
	printf("%d 项失败\n", FailCount);
	return 1;
}
