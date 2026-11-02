// 水文场栅格约定与双线性采样的回归测试。
//
// 钉两件事：
//   1) 格 ↔ 方向 的往返：CellDirection 得到的格必须还是自己（否则烘焙写到一个格、
//      采样读另一个格，水位整体错位）；
//   2) 跨面连续：同一个方向在相邻两个面上采样，结果必须一致 —— cubemap 接缝
//      出错时最先在这里显形，而不是在 PIE 里。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetHydrologyField.h"
#include "PlanetHydrologyGrid.h"
#include "PlanetBiomeCubemap.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** 造一个按方向解析求值的水文场：三格栅格都填同一个函数。 */
	TSharedRef<FPlanetHydrologyField> MakeAnalyticField(
		int32 Resolution, TFunctionRef<float(const FVector&)> WaterFn)
	{
		const int32 Count = PlanetHydrologyGrid::CellCount(Resolution);
		TArray<float> Water;
		TArray<float> Carve;
		TArray<float> Strength;
		Water.SetNumUninitialized(Count);
		Carve.Init(0.f, Count);
		Strength.Init(0.f, Count);

		for (int32 Cell = 0; Cell < Count; ++Cell)
		{
			int32 Face = 0, X = 0, Y = 0;
			PlanetHydrologyGrid::DecodeCell(Cell, Resolution, Face, X, Y);
			Water[Cell] = WaterFn(PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution));
		}

		return FPlanetHydrologyField::Create(
			Resolution, 1000.f, 4.f,
			MoveTemp(Water), MoveTemp(Carve), MoveTemp(Strength),
			TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyGridTest,
	"StellarEra.Planet.Hydrology.GridIndexing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyGridTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 32;

	// --- 往返：格 → 方向 → 格 ---
	// 幽灵格（Padding 区）的方向是外插的，允许落到邻面，因此只检查内部格。
	int32 NumMismatches = 0;
	for (int32 Face = 0; Face < PlanetHydrologyGrid::FaceCount; ++Face)
	{
		for (int32 Y = 1; Y <= Resolution; ++Y)
		{
			for (int32 X = 1; X <= Resolution; ++X)
			{
				const FVector Dir = PlanetHydrologyGrid::CellDirection(Face, X, Y, Resolution);
				if (PlanetHydrologyGrid::DirectionToCell(Dir, Resolution)
					!= PlanetHydrologyGrid::CellIndex(Face, X, Y, Resolution))
				{
					++NumMismatches;
				}
			}
		}
	}
	TestEqual(TEXT("内部格的往返误差数（必须为 0）"), NumMismatches, 0);

	// --- UToGrid / CellToU 互逆 ---
	double MaxRoundTripError = 0.0;
	for (int32 Index = 0; Index <= Resolution + 1; ++Index)
	{
		const float U = PlanetHydrologyGrid::CellToU(Index, Resolution);
		const float Grid = PlanetHydrologyGrid::UToGrid(U, Resolution);
		MaxRoundTripError = FMath::Max(MaxRoundTripError, FMath::Abs(double(Grid) - double(Index)));
	}
	TestTrue(FString::Printf(TEXT("格 ↔ UV 往返误差 %.6f 必须 < 1e-3"), MaxRoundTripError),
		MaxRoundTripError < 1e-3);

	// --- 双线性取 X0/X1 永远不越界 ---
	// U ∈ [0,1] ⇒ g ∈ [0.5, N+0.5] ⇒ X0 ∈ [0, N]、X1 ∈ [1, N+1]。
	// 这是「采样永远不需要分支」的全部依据。
	bool bInBounds = true;
	for (int32 Step = 0; Step <= 256; ++Step)
	{
		const float U = float(Step) / 256.f;
		const float Grid = PlanetHydrologyGrid::UToGrid(U, Resolution);
		const int32 X0 = FMath::FloorToInt(Grid);
		bInBounds &= (X0 >= 0) && (X0 <= Resolution) && (X0 + 1 <= Resolution + 1);
	}
	TestTrue(TEXT("双线性索引必须永远落在 [0, N+1] 内"), bInBounds);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyFieldSampleTest,
	"StellarEra.Planet.Hydrology.FieldSample",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyFieldSampleTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 64;

	// --- 常数场：采样必须处处等值（含格点与非格点） ---
	const TSharedRef<FPlanetHydrologyField> Constant =
		MakeAnalyticField(Resolution, [](const FVector&) { return 42.f; });

	double MaxConstantError = 0.0;
	for (int32 Step = 0; Step < 512; ++Step)
	{
		const FVector Dir = FVector(
			FMath::Sin(double(Step) * 0.017),
			FMath::Cos(double(Step) * 0.031),
			FMath::Sin(double(Step) * 0.007 + 0.3)).GetSafeNormal();
		MaxConstantError = FMath::Max(
			MaxConstantError, FMath::Abs(double(Constant->Sample(Dir).WaterLevelRadius) - 42.0));
	}
	TestTrue(FString::Printf(TEXT("常数场采样误差 %.6f 必须 < 1e-3"), MaxConstantError),
		MaxConstantError < 1e-3);

	// --- 线性场：跨面必须连续 ---
	// 场值 = 1000 + 100 × Direction.Z。跨面取相邻两个方向，值之差必须与几何
	// 间距同阶；接缝映射写错时会看到一整格的跳变（>= 100 × 一格的角距）。
	const TSharedRef<FPlanetHydrologyField> Linear = MakeAnalyticField(
		Resolution, [](const FVector& Dir) { return float(1000.0 + 100.0 * Dir.Z); });

	// 沿六个面的交界扫一圈：取面边界上的方向，向两侧各偏一点点。
	double MaxSeamJump = 0.0;
	for (int32 Face = 0; Face < PlanetBiomeCubemap::NumFaces; ++Face)
	{
		for (int32 Along = 0; Along <= 16; ++Along)
		{
			const float T = float(Along) / 16.f;
			const FVector EdgeDir = PlanetBiomeCubemap::FaceUVToDirection(Face, 1.f, T);
			const FVector Inside = PlanetBiomeCubemap::FaceUVToDirection(Face, 0.999f, T);
			const FVector Outside = (EdgeDir * 2.0 - Inside).GetSafeNormal(); // 越界镜像
			MaxSeamJump = FMath::Max(MaxSeamJump, FMath::Abs(
				double(Linear->Sample(Inside).WaterLevelRadius)
				- double(Linear->Sample(Outside).WaterLevelRadius)));
		}
	}
	// 两个方向夹角 ≈ 一格的角距（π/2 / N ≈ 0.025 rad），线性场值差 ≤ 100 × 0.025 × 2。
	TestTrue(FString::Printf(TEXT("跨面采样跳变 %.3f 必须 <= 5（一格量级）"), MaxSeamJump),
		MaxSeamJump <= 5.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraHydrologyInvalidFieldTest,
	"StellarEra.Planet.Hydrology.InvalidField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraHydrologyInvalidFieldTest::RunTest(const FString& Parameters)
{
	constexpr int32 Resolution = 8;
	const int32 Count = PlanetHydrologyGrid::CellCount(Resolution);

	// --- 三个栅格齐长时必须是有效场（防止上一条校验写成过严） ---
	{
		TArray<float> Water;
		TArray<float> Carve;
		TArray<float> Strength;
		Water.Init(1200.f, Count);
		Carve.Init(1.f, Count);
		Strength.Init(1.f, Count);
		const TSharedRef<FPlanetHydrologyField> Valid = FPlanetHydrologyField::Create(
			Resolution, 1000.f, 4.f,
			MoveTemp(Water), MoveTemp(Carve), MoveTemp(Strength),
			TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());
		TestTrue(TEXT("三栅格齐长的场必须有效"), Valid->IsValid());
	}

	// --- 任一副栅格短一格：IsValid 必须为 false，Sample 必须返回全零 ---
	// 只把副栅格截短：WaterLevel 长度仍然正确。旧实现只校验 WaterLevel，会判有效，
	// 然后 Sample 用同一组下标去读短数组 —— Development 下命中 TArray 范围检查，
	// Shipping 下越界。契约（PlanetHydrologyField.h：「场无效时返回全零」）要求这里返回零。
	for (int32 ShortGrid = 0; ShortGrid < 2; ++ShortGrid)
	{
		TArray<float> Water;
		TArray<float> Carve;
		TArray<float> Strength;
		Water.Init(1200.f, Count);
		Carve.Init(1.f, Count);
		Strength.Init(1.f, Count);
		(ShortGrid == 0 ? Carve : Strength).SetNum(Count - 1);

		const TSharedRef<FPlanetHydrologyField> Invalid = FPlanetHydrologyField::Create(
			Resolution, 1000.f, 4.f,
			MoveTemp(Water), MoveTemp(Carve), MoveTemp(Strength),
			TArray<FPlanetLakeBasin>(), TArray<FPlanetRiverPolyline>());

		TestFalse(
			FString::Printf(TEXT("%s 短一格时 IsValid 必须为 false"),
				ShortGrid == 0 ? TEXT("CarveDepth") : TEXT("RiverStrength")),
			Invalid->IsValid());

		const FPlanetHydrologyField::FSample Sample = Invalid->Sample(FVector::UpVector);
		TestEqual(TEXT("无效场的采样水位必须为 0"), Sample.WaterLevelRadius, 0.f);
		TestEqual(TEXT("无效场的采样下切必须为 0"), Sample.CarveDepth, 0.f);
		TestEqual(TEXT("无效场的采样河流强度必须为 0"), Sample.RiverStrength, 0.f);
	}

	return true;
}

#endif
