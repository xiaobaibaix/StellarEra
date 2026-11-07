// 密度场内外早退的回归测试。体素工具每碰一个 32³ 叶子就整叶逐体素采样
// （FVoxelDataOctreeLeaf::InitForEdit → GetValues），该路径**没有**区间预判，
// 所以 GetValueImpl 的两支早退（低于最低可能地表 = 实体；高于最高可能地表 = 空气）
// 是挖掘耗时的主要杠杆。早退改返回值本身，故须钉死两件事：①返回值仍落在
// GetValueRangeImpl 的区间内，否则插件误判整块为空/为实、表面出洞；②早退只出现在
// 真值已饱和 ±1 处。另钉「不许夹到 ±1」—— LOD>0 的着色法线靠中心差分，夹断会退化。

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "VoxelPlanetNoiseTerrain.h"
#include "VoxelIntBox.h"
#include "VoxelItemStack.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraTerrainEarlyOutTest,
	"StellarEra.Planet.Terrain.EarlyOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraTerrainEarlyOutTest::RunTest(const FString& Parameters)
{
	// 与 UniverseMap 的原点行星同量级。
	constexpr float Radius = 20000.f;

	FVoxelPlanetTerrainContext Context;
	Context.Radius = Radius;
	// 不给地质场：早退边界与地质无关（几何外凸只抬高上界），去掉才能断言确定的数。
	const FVoxelPlanetNoiseTerrainInstance Instance(Context, FPlanetNoiseParams());

	const FVoxelItemStack& Items = FVoxelItemStack::Empty;

	const auto ValueAt = [&](const FVector& P)
	{
		return double(Instance.GetValueImpl(v_flt(P.X), v_flt(P.Y), v_flt(P.Z), 0, Items));
	};

	// 三个方向分量都不相等的轴：避免对称性掩盖方向相关的错误。
	const FVector UnitDir = FVector(1.0, 2.0, 3.0).GetSafeNormal();
	const FVector SurfacePoint = Context.Center + UnitDir * double(Radius);

	// 盒内采样必须被该盒子自己的区间查询覆盖 —— 唯一能挡住「早退值跑出区间」的护栏。
	const auto CheckRangeCoverage = [&](const FVector& BoxCenter, const int32 HalfSize, const TCHAR* Label)
	{
		const FIntVector Center(
			FMath::FloorToInt(BoxCenter.X),
			FMath::FloorToInt(BoxCenter.Y),
			FMath::FloorToInt(BoxCenter.Z));
		const FIntVector Min = Center - HalfSize;
		const FIntVector Max = Center + HalfSize;

		const TVoxelRange<v_flt> Range =
			Instance.GetValueRangeImpl(FVoxelIntBox(Min, Max), 0, Items);

		bool bAllCovered = true;
		double Lowest = TNumericLimits<double>::Max();
		double Highest = -TNumericLimits<double>::Max();
		for (int32 X = Min.X; X <= Max.X; X += 3)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; Y += 3)
			{
				for (int32 Z = Min.Z; Z <= Max.Z; Z += 3)
				{
					const double Value = double(
						Instance.GetValueImpl(v_flt(X), v_flt(Y), v_flt(Z), 0, Items));
					Lowest = FMath::Min(Lowest, Value);
					Highest = FMath::Max(Highest, Value);
					if (Value < double(Range.Min) || Value > double(Range.Max))
					{
						bAllCovered = false;
					}
				}
			}
		}
		TestTrue(FString::Printf(
			TEXT("%s：取值 [%.2f, %.2f] 必须落在区间查询报出的 [%.2f, %.2f] 内"),
			Label, Lowest, Highest, double(Range.Min), double(Range.Max)), bAllCovered);
	};

	// --- 实体早退：低于最低可能地表（Radius − Amplitude）5 体素以下 ---
	// 默认 Amplitude 8 ⇒ 最低地表 Radius−8，−100 处必然早退且真值 ≤ −1（FVoxelValue Full()）。
	const FVector DeepInside = SurfacePoint - UnitDir * 100.0;
	const double InsideValue = ValueAt(DeepInside);
	TestTrue(FString::Printf(TEXT("深处必须是实体（实测 %.3f）"), InsideValue), InsideValue < 0.0);
	TestTrue(FString::Printf(TEXT("深处必须已饱和到 -1 以下（实测 %.3f）"), InsideValue),
		InsideValue <= -1.0);
	CheckRangeCoverage(DeepInside, 16, TEXT("深处盒子"));

	// --- 空气早退：高于最高可能地表（Radius + Amplitude）5 体素以上 ---
	const FVector DeepOutside = SurfacePoint + UnitDir * 100.0;
	const double OutsideValue = ValueAt(DeepOutside);
	TestTrue(FString::Printf(TEXT("高空必须是空气（实测 %.3f）"), OutsideValue), OutsideValue > 0.0);
	TestTrue(FString::Printf(TEXT("高空必须已饱和到 +1 以上（实测 %.3f）"), OutsideValue),
		OutsideValue >= 1.0);
	CheckRangeCoverage(DeepOutside, 16, TEXT("高空盒子"));

	// --- 夹断回归：早退**不许**把远处密度夹成 ±1 ---
	// LOD 8 中心差分间距 256 体素，地表外 256 处密度约 (256−8)/5 ≈ 50；夹成 1 法线就退化。
	const double FarAbove = ValueAt(SurfacePoint + UnitDir * 256.0);
	TestTrue(FString::Printf(TEXT("高空 256 体素处的密度不能被夹断（实测 %.2f，期望 > 10）"), FarAbove),
		FarAbove > 10.0);

	// --- 穿越地表带：符号翻转只能出现一次 ---
	// 带内精确、带外早退；早退把符号弄反会看到「变空后又变回实体」＝洞壁漂移。
	bool bSeenEmpty = false;
	bool bFlipIsUnique = true;
	for (int32 Step = -40; Step <= 40; ++Step)
	{
		const double Value = ValueAt(SurfacePoint + UnitDir * double(Step));
		const bool bEmpty = Value > 0.0;
		if (bSeenEmpty && !bEmpty)
		{
			bFlipIsUnique = false;
		}
		bSeenEmpty |= bEmpty;
	}
	TestTrue(TEXT("沿径向离开地表后必须出现空气"), bSeenEmpty);
	TestTrue(TEXT("从实体穿到空气之后不能再变回实体（早退符号一致性）"), bFlipIsUnique);

	// --- 早退带必须真的窄 ---
	// 只有 [Radius ± (Amplitude + 2×DensityScale)] 这条带要算噪声与地质，其余全早退。
	const FPlanetNoiseParams Defaults;
	const double BandBottom = double(Radius) - double(Defaults.Amplitude);
	const double BandTop = double(Radius) + double(Defaults.Amplitude);
	TestTrue(FString::Printf(TEXT("带宽 %.1f 体素必须不超过 32"), BandTop - BandBottom),
		BandTop - BandBottom <= 32.0);

	return true;
}

#endif
