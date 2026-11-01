// 地形消费水文场的回归测试。
//
// 全部用手工构造的均匀水文场（FPlanetHydrologyField::Create）：均匀意味着双线性
// 采样在哪儿都是精确值，断言因此是确定的数，不需要跑烘焙。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "VoxelPlanetNoiseTerrain.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr double CarveTestRadius = 1000.0;
	constexpr int32 CarveTestGridResolution = 16;

	/** 整颗星一套常数的水文场：不淹（水位远低于地表）、下切 = Carve、河强 = River。 */
	TSharedPtr<FPlanetHydrologyField> MakeUniformField(float Carve, float River)
	{
		const int32 Resolution = CarveTestGridResolution;
		const int32 Padding = 1;
		const int32 PaddedSize = Resolution + 2 * Padding;
		const int32 CellCount = 6 * PaddedSize * PaddedSize;   // PlanetBiomeCubemap::NumFaces == 6

		TArray<float> Water;
		TArray<float> CarveDepth;
		TArray<float> RiverStrength;
		Water.Init(float(CarveTestRadius) - 100.f, CellCount);   // 远低于任何地表
		CarveDepth.Init(Carve, CellCount);
		RiverStrength.Init(River, CellCount);

		return FPlanetHydrologyField::Create(
			Resolution,
			float(CarveTestRadius) - 100.f,
			4.f,
			MoveTemp(Water),
			MoveTemp(CarveDepth),
			MoveTemp(RiverStrength),
			TArray<FPlanetLakeBasin>(),
			TArray<FPlanetRiverPolyline>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyCarveTest,
	"StellarEra.Planet.Terrain.HydrologyCarve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyCarveTest::RunTest(const FString& Parameters)
{
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	TestNotNull(TEXT("噪声地形模块必须能实例化"), Terrain);
	if (!Terrain)
	{
		return false;
	}
	Terrain->Seed = 1234;

	FVoxelPlanetTerrainContext Context;
	Context.Radius = float(CarveTestRadius);

	const FVector Direction = FVector(0.3, -0.6, 0.74).GetSafeNormal();

	// --- 基准：没有水文场 ---
	Context.Hydrology = nullptr;
	const float NoWater = Terrain->GetSurfaceRadiusVoxels(Context, Direction);

	// --- 下切 3 体素：地表必须正好低 3 体素 ---
	Context.Hydrology = MakeUniformField(/*Carve=*/3.f, /*River=*/0.f);
	const float Carved = Terrain->GetSurfaceRadiusVoxels(Context, Direction);

	TestTrue(FString::Printf(TEXT("下切 3 体素后地表 %.3f 必须低于无水文场时的 %.3f"),
		Carved, NoWater), Carved < NoWater);
	TestTrue(FString::Printf(TEXT("下切量必须是 3 体素（实测 %.4f）"), NoWater - Carved),
		FMath::Abs((NoWater - Carved) - 3.f) < 0.01f);

	// --- 下切 0：与没有水文场完全一致（采样不能引入偏移）---
	Context.Hydrology = MakeUniformField(/*Carve=*/0.f, /*River=*/0.f);
	TestTrue(FString::Printf(TEXT("下切 0 时地表必须与无水文场一致（%.4f vs %.4f）"),
		Terrain->GetSurfaceRadiusVoxels(Context, Direction), NoWater),
		FMath::Abs(Terrain->GetSurfaceRadiusVoxels(Context, Direction) - NoWater) < 1e-3f);

	// --- 密度场本身也下切 ---
	//
	// HydrologyCarveRange 只检查「区间覆盖取值」：把 GetValueImpl 里的
	// + CarveDepth 删掉、区间里的 + MaxCarveDepth 留着，区间只会更宽，
	// 那条用例照样通过。这里直接比较同一个采样点在有/无水文场时的密度，
	// 钉住下切项确实进入了密度，而不只是被区间包络吸收掉。
	//
	// 采样距离取 Radius，落在两条早退边界之间（带内走精确计算）。
	constexpr double ProbeDistance = CarveTestRadius;
	constexpr double ProbeDensityScale = 5.0;   // 与 VoxelPlanetNoiseTerrain.cpp 的 DensityScale 一致
	const FVoxelItemStack& EmptyItems = FVoxelItemStack::Empty;
	const FPlanetNoiseParams ProbeParams = FPlanetNoiseParams();

	FVoxelPlanetTerrainContext DryContext;
	DryContext.Radius = float(CarveTestRadius);
	DryContext.Hydrology = nullptr;
	const FVoxelPlanetNoiseTerrainInstance DryInstance(DryContext, ProbeParams);

	FVoxelPlanetTerrainContext CarvedContext;
	CarvedContext.Radius = float(CarveTestRadius);
	CarvedContext.Hydrology = MakeUniformField(/*Carve=*/3.f, /*River=*/0.f);
	const FVoxelPlanetNoiseTerrainInstance CarvedInstance(CarvedContext, ProbeParams);

	const FVector ProbePoint = Direction * ProbeDistance;
	const double DryValue = double(DryInstance.GetValueImpl(
		v_flt(ProbePoint.X), v_flt(ProbePoint.Y), v_flt(ProbePoint.Z), 0, EmptyItems));
	const double CarvedValue = double(CarvedInstance.GetValueImpl(
		v_flt(ProbePoint.X), v_flt(ProbePoint.Y), v_flt(ProbePoint.Z), 0, EmptyItems));

	// Value = (Distance − Surface) / DensityScale ⇒ Surface = Distance − Value × DensityScale。
	// 两条路径的 Distance 相同，所以地表半径的差就是 Value 差乘回 DensityScale。
	const double SurfaceDrop = (CarvedValue - DryValue) * ProbeDensityScale;
	TestTrue(FString::Printf(TEXT("同一采样点处密度场报出的地表半径必须正好低 3 体素（实测 %.4f）"),
		SurfaceDrop),
		FMath::Abs(SurfaceDrop - 3.0) < 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyConfigTest,
	"StellarEra.Planet.Terrain.HydrologyConfig",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyConfigTest::RunTest(const FString& Parameters)
{
	UVoxelPlanetNoiseTerrain* Terrain = NewObject<UVoxelPlanetNoiseTerrain>();
	TestNotNull(TEXT("噪声地形模块必须能实例化"), Terrain);
	if (!Terrain)
	{
		return false;
	}

	// --- 配置：与模块上的旋钮同源 ---
	Terrain->bEnableWater = true;
	Terrain->SeaLevel = -0.42f;
	Terrain->Amplitude = 11.f;
	Terrain->HydrologyResolution = 128;
	Terrain->MinRiverFlow = 40.f;
	Terrain->MaxCarveDepth = 6.f;

	FPlanetWaterConfig Config;
	TestTrue(TEXT("开了水的模块必须返回 true"), Terrain->GetWaterConfig(Config));
	TestEqual(TEXT("SeaLevel 要透传"), Config.Params.SeaLevel, -0.42f);
	TestEqual(TEXT("Amplitude 要透传"), Config.Params.Amplitude, 11.f);
	TestEqual(TEXT("Resolution 要透传"), Config.Params.Resolution, 128);
	TestEqual(TEXT("MinRiverFlow 要透传"), Config.Params.MinRiverFlow, 40.f);
	TestEqual(TEXT("MaxCarveDepth 要透传"), Config.Params.MaxCarveDepth, 6.f);

	// --- 关掉水：返回 false，但仍然给出合法配置 ---
	Terrain->bEnableWater = false;
	TestFalse(TEXT("关掉水之后必须返回 false"), Terrain->GetWaterConfig(Config));
	TestTrue(TEXT("返回 false 时配置仍必须合法（分辨率 > 0）"), Config.Params.Resolution > 0);

	// --- 原始位移：有界，且与海平面无关 ---
	Terrain->bEnableWater = true;
	const FVector Direction = FVector(-0.5, 0.2, 0.84).GetSafeNormal();
	const float OffsetAtDefaultSea = Terrain->GetRawOffset(Direction);
	Terrain->SeaLevel = 0.5f;
	const float OffsetAtHighSea = Terrain->GetRawOffset(Direction);
	TestTrue(FString::Printf(TEXT("原始位移 %.4f 必须落在 [-1, 1]"), OffsetAtDefaultSea),
		OffsetAtDefaultSea >= -1.f && OffsetAtDefaultSea <= 1.f);
	TestEqual(TEXT("原始位移不能受海平面影响"), OffsetAtHighSea, OffsetAtDefaultSea);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainSeafloorColorTest,
	"StellarEra.Planet.Terrain.SeafloorColor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainSeafloorColorTest::RunTest(const FString& Parameters)
{
	// 把海平面抬到 0.99：几乎所有方向都落到「海床」分支。
	// 水深范围压到 0.01：只要低于海平面 1% 的位移就已经是「深水」。
	FPlanetNoiseParams Params;
	Params.SeaLevel = 0.99f;
	Params.WaterDepthRange = 0.01f;
	Params.DeepWaterColor = FLinearColor(0.05f, 0.10f, 0.20f);
	Params.ShallowWaterColor = FLinearColor(0.90f, 0.10f, 0.10f);   // 诱饵：不该出现在深水
	Params.SandColor = FLinearColor(0.95f, 0.90f, 0.70f);
	Params.RockColor = FLinearColor(0.40f, 0.38f, 0.36f);
	Params.SnowColor = FLinearColor::White;

	const FVector Direction = FVector(0.11, 0.55, -0.83).GetSafeNormal();
	const float Offset = ComputeRawPlanetOffset(Params, Direction);
	if (Offset > 0.97f)
	{
		// 这个方向恰好高到不会落进海床分支（概率极低）。换个方向重试没有意义 ——
		// 直接用固定种子下的另一个方向会失去可复现性，所以这里显式记一条跳过。
		AddInfo(TEXT("该方向的位移高于 0.97，跳过海床断言"));
		return true;
	}

	const FLinearColor Color = ComputePlanetSurfaceColor(
		Params, /*Geology=*/nullptr, /*Hydrology=*/nullptr,
		CarveTestRadius, Direction, CarveTestRadius);

	TestTrue(FString::Printf(TEXT("深水海床必须是 DeepWaterColor 而不是旧的水面色（实测 %.3f %.3f %.3f）"),
		Color.R, Color.G, Color.B),
		FMath::Abs(Color.R - Params.DeepWaterColor.R) < 0.02f
			&& FMath::Abs(Color.G - Params.DeepWaterColor.G) < 0.02f
			&& FMath::Abs(Color.B - Params.DeepWaterColor.B) < 0.02f);

	// --- 河床变暗：把海平面压到 −0.9，同一方向就成了陆地 ---
	// 河床变暗只在陆地上做（海底已经被深度插值盖住），所以必须换一套参数。
	// 注意 ComputeRawPlanetOffset 不读 SeaLevel，Offset 与上面那一次相同。
	FPlanetNoiseParams LandParams = Params;
	LandParams.SeaLevel = -0.9f;
	if (Offset > LandParams.SeaLevel)
	{
		const FLinearColor DryLand = ComputePlanetSurfaceColor(
			LandParams, nullptr, nullptr, CarveTestRadius, Direction, CarveTestRadius);
		const FLinearColor Riverbed = ComputePlanetSurfaceColor(
			LandParams, nullptr, MakeUniformField(0.f, 1.f).Get(),
			CarveTestRadius, Direction, CarveTestRadius);
		TestTrue(FString::Printf(TEXT("河床 %.3f 必须比周围地面 %.3f 更暗"), Riverbed.R, DryLand.R),
			Riverbed.R < DryLand.R && Riverbed.G < DryLand.G && Riverbed.B < DryLand.B);
	}
	else
	{
		AddInfo(TEXT("该方向位移低于 −0.9，跳过河床断言"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainHydrologyRangeTest,
	"StellarEra.Planet.Terrain.HydrologyCarveRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainHydrologyRangeTest::RunTest(const FString& Parameters)
{
	// 区间覆盖的机械化检查：盒子里的**实际**取值必须落在它自己的区间里。
	//
	// 这条用例专钉「下切项加在哪一侧」。下切让地表变低、最实的点变得更空，
	// 所以 **MaxValue** 必须加 MaxCarveDepth。写在下界一侧就永远查不出来 ——
	// 区间只会变得更宽，而插件据此会把整块判成「全实体」，河床里冒出实体块。
	//
	// 越界的条件是位移 c < −0.5：
	//   真值（最深处） = (D − R − 8c + 4) / 5
	//   上界（漏项）   = (D − R + 8) / 5
	//   前者更大 ⟺ −8c + 4 > 8 ⟺ c < −0.5
	FPlanetNoiseParams Params;   // Amplitude 8
	// 这条用例要一个位移 < −0.6 的深谷，才可能让「漏掉下切项」的旧包络越界。
	// 实测四层 fbm 的归一化输出在整颗球上只到 −0.46 左右（各层极值很少对齐），
	// 两层能到 −0.64；采样偏移取非零值（与 PlanetTerrainRawOffsetTests 同一个），
	// 让搜索确定地落在噪声起伏明显的区域。
	Params.Octaves = 2;
	Params.SeedOffset = FVector(137.5, -42.25, 911.75);
	const float Amplitude = Params.Amplitude;

	FVector Direction = FVector::ZeroVector;
	float Offset = 0.f;
	{
		constexpr int32 NumSamples = 4096;
		const double GoldenAngle = PI * (3.0 - FMath::Sqrt(5.0));
		for (int32 Index = 0; Index < NumSamples; ++Index)
		{
			const double Z = 1.0 - 2.0 * (double(Index) + 0.5) / double(NumSamples);
			const double RingRadius = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
			const double Theta = GoldenAngle * double(Index);
			const FVector Candidate(
				RingRadius * FMath::Cos(Theta), RingRadius * FMath::Sin(Theta), Z);
			const float CandidateOffset = ComputeRawPlanetOffset(Params, Candidate);
			if (CandidateOffset < -0.6f)
			{
				Direction = Candidate;
				Offset = CandidateOffset;
				break;
			}
		}
	}
	if (!TestTrue(FString::Printf(
		TEXT("必须能找到位移 < −0.6 的方向（实测 %.4f；否则这条用例失去意义）"), Offset),
		!Direction.IsNearlyZero()))
	{
		return false;
	}

	FVoxelPlanetTerrainContext Context;
	Context.Radius = float(CarveTestRadius);
	Context.Hydrology = MakeUniformField(/*Carve=*/4.f, /*River=*/0.f);

	const FVoxelPlanetNoiseTerrainInstance Instance(Context, Params);
	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	// 盒子以「下切后的地表」为中心：最远的角落在更深的实体里，
	// 那里的密度正是上界的定义点。
	const double Surface =
		double(CarveTestRadius) + double(Amplitude) * double(Offset) - 4.0;
	const FVector BoxCenter = Direction * Surface;
	constexpr int32 HalfSize = 16;
	const FIntVector Center(
		FMath::FloorToInt(BoxCenter.X),
		FMath::FloorToInt(BoxCenter.Y),
		FMath::FloorToInt(BoxCenter.Z));
	const FIntVector Min = Center - HalfSize;
	const FIntVector Max = Center + HalfSize;

	const TVoxelRange<v_flt> Range = Instance.GetValueRangeImpl(FVoxelIntBox(Min, Max), 0, Items);

	// 步长 2 而不是 3：盒子边长是 32（偶数），步长 2 会**恰好踩到八个角**，
	// 包括区间查询定义 MaxValue 用的那个最远角。步长 3 会停在距角 2 体素处，
	// 下切给出的 4 体素余量刚好被这点距离吃掉 —— 实测把 MaxValue 里的
	// + MaxCarveDepth 删掉，步长 3 的版本照样通过，护栏就成了摆设。
	bool bAllCovered = true;
	double WorstValue = 0.0;
	for (int32 X = Min.X; X <= Max.X; X += 2)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; Y += 2)
		{
			for (int32 Z = Min.Z; Z <= Max.Z; Z += 2)
			{
				const double Value = double(
					Instance.GetValueImpl(v_flt(X), v_flt(Y), v_flt(Z), 0, Items));
				if (Value < double(Range.Min) || Value > double(Range.Max))
				{
					bAllCovered = false;
					WorstValue = Value;
				}
			}
		}
	}

	TestTrue(FString::Printf(
		TEXT("取值必须被区间 [%.4f, %.4f] 覆盖（越界值 %.4f）—— 下切项属于 MaxValue 一侧"),
		double(Range.Min), double(Range.Max), WorstValue),
		bAllCovered);

	return true;
}

#endif
