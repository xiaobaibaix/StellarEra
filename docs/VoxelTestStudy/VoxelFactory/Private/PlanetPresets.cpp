// 星球预设 —— 实现。

#include "PlanetPresets.h"

///////////////////////////////////////////////////////////////////////////////
// 主入口

void FPlanetPresets::Apply(
	EPlanetType Type,
	int32 Seed,
	double Radius,
	FVoxelPlanetContinentLayer& OutContinents,
	FVoxelPlanetMountainLayer& OutMountains,
	FVoxelPlanetDetailLayer& OutDetail,
	FVoxelPlanetRavineLayer& OutRavine,
	FVoxelPlanetSeaSettings& OutSea,
	FVoxelPlanetLandPalette& OutLand,
	FOrePlanetConfig& OutOre)
{
	// 先重置成默认值，避免沿用上一次调参的残留
	OutContinents = FVoxelPlanetContinentLayer();
	OutMountains = FVoxelPlanetMountainLayer();
	OutDetail = FVoxelPlanetDetailLayer();
	OutRavine = FVoxelPlanetRavineLayer();
	OutSea = FVoxelPlanetSeaSettings();
	OutLand = FVoxelPlanetLandPalette();

	TuneTerrain(Type, OutContinents, OutMountains, OutDetail, OutRavine, OutSea, OutLand);
	OutOre = MakeBaseOreConfig(Type, Seed, Radius);
}

void FPlanetPresets::ApplyOreOnly(EPlanetType Type, int32 Seed, double Radius, FOrePlanetConfig& OutOre)
{
	OutOre = MakeBaseOreConfig(Type, Seed, Radius);
}

///////////////////////////////////////////////////////////////////////////////
// 矿脉预设

FOrePlanetConfig FPlanetPresets::MakeBaseOreConfig(EPlanetType Type, int32 Seed, double Radius)
{
	FOrePlanetConfig Config;
	Config.PlanetSeed = Seed;
	Config.PlanetRadius = Radius;

	// 所有矿的共同默认值：不在场
	const auto Reset = [](FOreDistribution& D)
	{
		D = FOreDistribution();
	};

	Reset(Config.Iron);
	Reset(Config.Copper);
	Reset(Config.Silicon);
	Reset(Config.Titanium);
	Reset(Config.Uranium);
	Reset(Config.Water);

	// 深度按星球半径缩放，这样小星球上矿层不会薄到挖不到
	const double Scale = FMath::Clamp(Radius / 1000.0, 0.2, 3.0);
	const double S = Scale;

	TuneOreAbundance(Type, Config);

	// 水：所有有大气或冰的星球都有，Depth 很浅
	Config.Water.PeakDepth = -10.0 * S;
	Config.Water.LayerThickness = 150.0 * S;
	Config.Water.SpatialFrequency = 3.0;
	Config.Water.SpatialVariance = 0.4f;
	Config.Water.Threshold = 0.30f;
	Config.Water.Hardness = 0.6f;
	Config.Water.BaseYield = 2.0f;

	// 按类型调各矿的深度位置
	switch (Type)
	{
	case EPlanetType::Rocky:
		Config.Iron.PeakDepth = -30.0 * S;
		Config.Copper.PeakDepth = -60.0 * S;
		Config.Silicon.PeakDepth = -45.0 * S;
		break;

	case EPlanetType::Metallic:
		Config.Iron.PeakDepth = -50.0 * S;
		Config.Titanium.PeakDepth = -100.0 * S;
		break;

	case EPlanetType::Frozen:
		Config.Iron.PeakDepth = -80.0 * S;
		break;

	case EPlanetType::Volcanic:
		Config.Titanium.PeakDepth = -70.0 * S;
		Config.Uranium.PeakDepth = -180.0 * S;
		break;

	case EPlanetType::Silica:
		Config.Silicon.PeakDepth = -40.0 * S;
		break;

	case EPlanetType::Barren:
		Config.Iron.PeakDepth = -120.0 * S;
		break;

	case EPlanetType::Rich:
		// 丰富星球：所有矿都在浅层，方便测试
		Config.Iron.PeakDepth = -25.0 * S;
		Config.Copper.PeakDepth = -25.0 * S;
		Config.Silicon.PeakDepth = -25.0 * S;
		Config.Titanium.PeakDepth = -25.0 * S;
		Config.Uranium.PeakDepth = -25.0 * S;
		break;
	}

	// 把负深度转成正的厚度（PeakDepth 是位置不是厚度，这里统一给个合理宽度）
	const auto SetThickness = [S](FOreDistribution& D, double Base)
	{
		D.LayerThickness = Base * S;
	};

	SetThickness(Config.Iron, 180.0);
	SetThickness(Config.Copper, 160.0);
	SetThickness(Config.Silicon, 150.0);
	SetThickness(Config.Titanium, 120.0);
	SetThickness(Config.Uranium, 100.0);

	return Config;
}

void FPlanetPresets::TuneOreAbundance(EPlanetType Type, FOrePlanetConfig& Config)
{
	switch (Type)
	{
	case EPlanetType::Rocky:
		// 典型的类地行星
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.75f;
		Config.Copper.bPresent = true;    Config.Copper.GlobalAbundance = 0.45f;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.55f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.12f;
		Config.Uranium.bPresent = true;   Config.Uranium.GlobalAbundance = 0.05f;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.45f;
		break;

	case EPlanetType::Metallic:
		// 铁钛丰富，其他稀少
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.95f;
		Config.Copper.bPresent = true;    Config.Copper.GlobalAbundance = 0.15f;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.10f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.60f;
		Config.Uranium.bPresent = false;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.15f;
		break;

	case EPlanetType::Frozen:
		// 水多金属少
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.25f;
		Config.Copper.bPresent = false;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.20f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.08f;
		Config.Uranium.bPresent = false;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.95f;
		break;

	case EPlanetType::Volcanic:
		// 钛铀丰富，地形剧烈
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.50f;
		Config.Copper.bPresent = true;    Config.Copper.GlobalAbundance = 0.35f;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.25f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.55f;
		Config.Uranium.bPresent = true;   Config.Uranium.GlobalAbundance = 0.40f;
		Config.Water.bPresent = false;
		break;

	case EPlanetType::Silica:
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.10f;
		Config.Copper.bPresent = false;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.90f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.15f;
		Config.Uranium.bPresent = false;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.20f;
		break;

	case EPlanetType::Barren:
		// 贫瘠：只有水和一点点铁
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.08f;
		Config.Copper.bPresent = false;
		Config.Silicon.bPresent = false;
		Config.Titanium.bPresent = false;
		Config.Uranium.bPresent = false;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.10f;
		break;

	case EPlanetType::Rich:
		// 全都多，方便测试与平衡
		Config.Iron.bPresent = true;      Config.Iron.GlobalAbundance = 0.85f;
		Config.Copper.bPresent = true;    Config.Copper.GlobalAbundance = 0.80f;
		Config.Silicon.bPresent = true;   Config.Silicon.GlobalAbundance = 0.80f;
		Config.Titanium.bPresent = true;  Config.Titanium.GlobalAbundance = 0.75f;
		Config.Uranium.bPresent = true;   Config.Uranium.GlobalAbundance = 0.70f;
		Config.Water.bPresent = true;     Config.Water.GlobalAbundance = 0.80f;
		break;
	}

	// 硬度：铀最难挖
	Config.Uranium.Hardness = 2.5f;
	Config.Titanium.Hardness = 1.8f;
	Config.Copper.Hardness = 1.2f;
	Config.Iron.Hardness = 1.0f;
	Config.Silicon.Hardness = 1.3f;
}

///////////////////////////////////////////////////////////////////////////////
// 地形预设

void FPlanetPresets::TuneTerrain(EPlanetType Type,
	FVoxelPlanetContinentLayer& Continents,
	FVoxelPlanetMountainLayer& Mountains,
	FVoxelPlanetDetailLayer& Detail,
	FVoxelPlanetRavineLayer& Ravine,
	FVoxelPlanetSeaSettings& Sea,
	FVoxelPlanetLandPalette& Land)
{
	// 各类型的默认海平面深度（0 = 无海洋）
	float SeaDepth = 0.f;

	switch (Type)
	{
	case EPlanetType::Rocky:
		// 类地：温和的地形，有海
		Continents.Frequency = 2.0f;
		Continents.Height = 45.f;
		Continents.Contrast = 1.8f;
		Mountains.Frequency = 6.f;
		Mountains.Height = 30.f;
		Mountains.Sharpness = 2.2f;
		SeaDepth = 30.f;
		Sea.DeepColor = FLinearColor(0.02f, 0.08f, 0.30f);
		Sea.ShoreColor = FLinearColor(0.80f, 0.72f, 0.50f);
		Land.LowlandColor = FLinearColor(0.20f, 0.42f, 0.18f);
		Land.HighlandColor = FLinearColor(0.38f, 0.36f, 0.20f);
		Land.SnowLine = 35.f;
		break;

	case EPlanetType::Metallic:
		// 金属：地形平坦，矿丰富
		Continents.Frequency = 1.5f;
		Continents.Height = 25.f;
		Continents.Contrast = 1.0f;
		Mountains.Frequency = 4.f;
		Mountains.Height = 40.f;
		Mountains.Sharpness = 3.0f;   // 尖锐的山脊
		SeaDepth = 0.f;
		Land.LowlandColor = FLinearColor(0.45f, 0.42f, 0.40f);
		Land.HighlandColor = FLinearColor(0.60f, 0.55f, 0.48f);
		Land.SnowLine = 0.f;
		break;

	case EPlanetType::Frozen:
		// 冰冻：平坦，白茫茫
		Continents.Frequency = 2.5f;
		Continents.Height = 30.f;
		Continents.Contrast = 1.0f;
		Mountains.Frequency = 7.f;
		Mountains.Height = 18.f;
		Mountains.Sharpness = 1.5f;
		SeaDepth = 45.f;
		Sea.DeepColor = FLinearColor(0.55f, 0.75f, 0.90f);
		Sea.ShoreColor = FLinearColor(0.85f, 0.92f, 0.98f);
		Land.LowlandColor = FLinearColor(0.88f, 0.92f, 0.95f);
		Land.HighlandColor = FLinearColor(0.95f, 0.96f, 0.98f);
		Land.SnowLine = 5.f;   // 到处都是雪
		break;

	case EPlanetType::Volcanic:
		// 火山：剧烈起伏，没有海洋
		Continents.Frequency = 3.0f;
		Continents.Height = 70.f;
		Continents.Contrast = 2.5f;
		Mountains.Frequency = 5.f;
		Mountains.Height = 55.f;
		Mountains.Sharpness = 3.5f;
		Mountains.LandBias = 0.3f;   // 山脉不管在哪都长
		Ravine.bEnabled = true;
		Ravine.Frequency = 4.f;
		Ravine.Depth = 25.f;
		SeaDepth = 0.f;
		Land.LowlandColor = FLinearColor(0.16f, 0.10f, 0.08f);
		Land.HighlandColor = FLinearColor(0.35f, 0.15f, 0.08f);
		Land.SnowLine = 0.f;
		break;

	case EPlanetType::Silica:
		// 硅地：荒原
		Continents.Frequency = 1.8f;
		Continents.Height = 35.f;
		Continents.Contrast = 1.2f;
		Mountains.Frequency = 8.f;
		Mountains.Height = 22.f;
		SeaDepth = 0.f;
		Land.LowlandColor = FLinearColor(0.55f, 0.52f, 0.48f);
		Land.HighlandColor = FLinearColor(0.70f, 0.68f, 0.65f);
		Land.SnowLine = 0.f;
		break;

	case EPlanetType::Barren:
		// 贫瘠：几乎全平
		Continents.Frequency = 1.2f;
		Continents.Height = 12.f;
		Continents.Contrast = 0.6f;
		Mountains.bEnabled = false;
		Detail.Frequency = 30.f;
		Detail.Height = 3.f;
		SeaDepth = 0.f;
		Land.LowlandColor = FLinearColor(0.35f, 0.32f, 0.28f);
		Land.HighlandColor = FLinearColor(0.42f, 0.38f, 0.33f);
		Land.SnowLine = 0.f;
		break;

	case EPlanetType::Rich:
		// 丰富：地形中等但资源全，便于测试
		Continents.Frequency = 2.0f;
		Continents.Height = 40.f;
		Continents.Contrast = 1.5f;
		Mountains.Frequency = 6.f;
		Mountains.Height = 25.f;
		SeaDepth = 25.f;
		Sea.DeepColor = FLinearColor(0.05f, 0.15f, 0.40f);
		Sea.ShoreColor = FLinearColor(0.80f, 0.75f, 0.55f);
		Land.LowlandColor = FLinearColor(0.25f, 0.45f, 0.22f);
		Land.HighlandColor = FLinearColor(0.45f, 0.42f, 0.25f);
		Land.SnowLine = 30.f;
		break;
	}

	// 海平面：Depth > 0 才有海
	Sea.bEnabled = SeaDepth > 0.f;
	Sea.Depth = SeaDepth;

	// 细节层按地形强度微调
	Detail.Frequency = 20.f;
	Detail.Height = 6.f;
	if (Type == EPlanetType::Volcanic)
	{
		Detail.Height = 10.f;
	}

	Ravine.bEnabled = (Type == EPlanetType::Volcanic);
}

///////////////////////////////////////////////////////////////////////////////
// 显示信息

FText FPlanetPresets::GetDisplayName(EPlanetType Type)
{
	switch (Type)
	{
	case EPlanetType::Rocky:    return FText::FromString(TEXT("岩石星球"));
	case EPlanetType::Metallic: return FText::FromString(TEXT("金属星球"));
	case EPlanetType::Frozen:   return FText::FromString(TEXT("冰冻星球"));
	case EPlanetType::Volcanic: return FText::FromString(TEXT("火山星球"));
	case EPlanetType::Silica:   return FText::FromString(TEXT("硅地星球"));
	case EPlanetType::Barren:   return FText::FromString(TEXT("贫瘠星球"));
	case EPlanetType::Rich:     return FText::FromString(TEXT("富饶星球"));
	default:                    return FText::FromString(TEXT("未知"));
	}
}

FText FPlanetPresets::GetDescription(EPlanetType Type)
{
	switch (Type)
	{
	case EPlanetType::Rocky:
		return FText::FromString(TEXT("类地行星。铁铜硅均衡，适合基础工业。"));
	case EPlanetType::Metallic:
		return FText::FromString(TEXT("铁钛极丰富但缺水。适合重型制造。"));
	case EPlanetType::Frozen:
		return FText::FromString(TEXT("水极丰富，金属稀少。适合作为水与冰的来源。"));
	case EPlanetType::Volcanic:
		return FText::FromString(TEXT("地形剧烈，钛与铀丰富。核工业首选。"));
	case EPlanetType::Silica:
		return FText::FromString(TEXT("硅含量极高。电子工业与建筑材料。"));
	case EPlanetType::Barren:
		return FText::FromString(TEXT("几乎什么都没有。早期探索的挑战。"));
	case EPlanetType::Rich:
		return FText::FromString(TEXT("所有资源都丰富。适合测试与平衡调整。"));
	default:
		return FText::GetEmpty();
	}
}
