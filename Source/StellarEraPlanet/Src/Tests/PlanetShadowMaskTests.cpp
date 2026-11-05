// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"
#include "PlanetShadowMask.h"

#if WITH_DEV_AUTOMATION_TESTS

// 具名命名空间：UBT 开 unity build，本文件与 Atmosphere/ 下的 .cpp 可能落进同一个 TU，
// 匿名命名空间在合并后的 blob 里是同一个作用域（同 PlanetAtmosphereMaterialTests.cpp 的注释）。
namespace StellarEraPlanetShadowTests
{
	const TCHAR* const ShadowMaterialPath =
		TEXT("/Game/Code/Planet/Materials/M_PlanetShadowLightFunction");

	/** 与关卡里那颗体素行星一致：表面半径 20 km。 */
	constexpr double RadiusCm = 2000000.0;
	/** 半影 = 半径的 1%，与 PlanetAtmosphereComponent.cpp 的写法一致。 */
	constexpr double PenumbraCm = RadiusCm * 0.01;

	bool HasParameter(const TArray<FMaterialParameterInfo>& Infos, const FName Name)
	{
		return Infos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	}
} // namespace StellarEraPlanetShadowTests

// 解析阴影的纯数学部分。锁死「白天是亮的、夜面是黑的、高空不会被误判成夜里」。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetShadowMaskTest,
	"StellarEra.Planet.PlanetShadow.Mask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetShadowMaskTest::RunTest(const FString& Parameters)
{
	using namespace StellarEraPlanetShadowTests;

	// 行星球心在原点，观测点在北极表面 (0,0,R)。
	const FVector NorthPole(0.0, 0.0, RadiusCm);
	const FVector Center = FVector::ZeroVector;

	// --- 地表：太阳在天顶 → 全亮 ---
	TestEqual(TEXT("太阳在天顶"),
		PlanetShadow::ComputeLightMask(NorthPole, FVector(0, 0, 1), Center, RadiusCm, PenumbraCm),
		1.0f);

	// --- 地表：太阳直射脚下 → 全黑（夜面） ---
	TestEqual(TEXT("太阳在正对面"),
		PlanetShadow::ComputeLightMask(NorthPole, FVector(0, 0, -1), Center, RadiusCm, PenumbraCm),
		0.0f);

	// --- 地表：太阳在地平线上 30° → 全亮 ---
	{
		const FVector Dir(0.5, 0.0, FMath::Sqrt(3.0) * 0.5);
		TestEqual(TEXT("太阳仰角 +30°"),
			PlanetShadow::ComputeLightMask(NorthPole, Dir, Center, RadiusCm, PenumbraCm), 1.0f);
	}

	// --- 地表：太阳在地平线下 30° → 全黑 ---
	{
		const FVector Dir(0.5, 0.0, -FMath::Sqrt(3.0) * 0.5);
		TestEqual(TEXT("太阳仰角 -30°"),
			PlanetShadow::ComputeLightMask(NorthPole, Dir, Center, RadiusCm, PenumbraCm), 0.0f);
	}

	// --- 高空：10 倍半径高、太阳只在本地地平线下 10° → 仍然照得到 ---
	// 这一条是「不要退化成 sin(仰角) < 0 就黑」的守门断言。
	{
		const FVector High(0.0, 0.0, RadiusCm * 11.0);
		const double TenDeg = FMath::DegreesToRadians(10.0);
		const FVector Dir(-FMath::Sin(TenDeg), 0.0, FMath::Cos(TenDeg));
		TestEqual(TEXT("10R 高空、太阳本地仰角 -10°"),
			PlanetShadow::ComputeLightMask(High, Dir, Center, RadiusCm, PenumbraCm), 1.0f);
	}

	// --- 正后方：观测点在行星背光侧、太阳在另一头 → 全黑 ---
	TestEqual(TEXT("行星正后方"),
		PlanetShadow::ComputeLightMask(FVector(0, 0, -RadiusCm * 1.5), FVector(0, 0, 1),
			Center, RadiusCm, PenumbraCm),
		0.0f);

	// --- 半影：垂距 = R + 半个半影宽 → 0.5 ---
	{
		const FVector Obs(-RadiusCm * 1.5, RadiusCm + PenumbraCm * 0.5, 0.0);
		const float Mask = PlanetShadow::ComputeLightMask(Obs, FVector(1, 0, 0),
			Center, RadiusCm, PenumbraCm);
		TestTrue(TEXT("半影中点 ≈ 0.5"), FMath::IsNearlyEqual(Mask, 0.5f, 0.01f));
	}

	// --- 单调：垂距从 R 走到 R + 半影，掩码单调不减 ---
	{
		float Previous = -1.0f;
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			const double Perp = RadiusCm + PenumbraCm * (Step / 10.0);
			const FVector Obs(-RadiusCm * 1.5, Perp, 0.0);
			const float Mask = PlanetShadow::ComputeLightMask(Obs, FVector(1, 0, 0),
				Center, RadiusCm, PenumbraCm);
			TestTrue(TEXT("掩码单调不减"), Mask >= Previous);
			Previous = Mask;
		}
	}

	// --- Penumbra <= 0 不产生除零：退化成 1 cm 的硬边 ---
	{
		const FVector Obs(-RadiusCm * 1.5, RadiusCm + 0.5, 0.0);
		const float Mask = PlanetShadow::ComputeLightMask(Obs, FVector(1, 0, 0),
			Center, RadiusCm, 0.0);
		TestTrue(TEXT("Penumbra=0 时不除零且给出 0.5"),
			FMath::IsNearlyEqual(Mask, 0.5f, 0.01f));
	}

	return true;
}

// 锁死「资产真的按 PlanetShadowLightFunctionBuilder.cpp 当前的样子重建过」。
// 改生成器忘了重跑 StellarEra.BuildPlanetShadowLightFunction 会红在这里。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraPlanetShadowMaterialTest,
	"StellarEra.Planet.PlanetShadow.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraPlanetShadowMaterialTest::RunTest(const FString& Parameters)
{
	using namespace StellarEraPlanetShadowTests;

	UMaterial* Material = LoadObject<UMaterial>(nullptr, ShadowMaterialPath);
	if (!TestNotNull(TEXT("光函数材质资产"), Material))
	{
		AddError(TEXT("资产不存在。先在编辑器里跑一次 StellarEra.BuildPlanetShadowLightFunction。"));
		return false;
	}

	TestEqual(TEXT("MaterialDomain = LightFunction"), Material->MaterialDomain, MD_LightFunction);
	TestEqual(TEXT("BlendMode = Opaque"), Material->BlendMode, BLEND_Opaque);

	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarIds;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
	for (const TCHAR* Name : { TEXT("PlanetRadius"), TEXT("Penumbra") })
	{
		TestTrue(FString::Printf(TEXT("标量参数 %s"), Name), HasParameter(ScalarInfos, FName(Name)));
	}

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorIds;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);
	for (const TCHAR* Name : { TEXT("SunDirection"), TEXT("PlanetCenter") })
	{
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name), HasParameter(VectorInfos, FName(Name)));
	}

	// Custom 节点的 Code 是 shader map id 的一部分；公式在这儿，锁一下关键词。
	bool bFoundCustom = false;
	for (UMaterialExpression* Expression : Material->GetExpressions())
	{
		if (const UMaterialExpressionCustom* Custom = Cast<UMaterialExpressionCustom>(Expression))
		{
			bFoundCustom = true;
			TestTrue(TEXT("Custom 节点里有 TowardSun"), Custom->Code.Contains(TEXT("TowardSun")));
			TestTrue(TEXT("Custom 节点里有 PlanetRadius"), Custom->Code.Contains(TEXT("PlanetRadius")));
			TestEqual(TEXT("Custom 输出是标量"), Custom->OutputType, CMOT_Float1);
		}
	}
	TestTrue(TEXT("存在 Custom 节点"), bFoundCustom);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
