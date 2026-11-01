#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// 用具名命名空间而不是匿名命名空间：UBT 默认开 unity build，本文件的 .cpp 会与
// Atmosphere/PlanetAtmosphereComponent.cpp 落进同一个 TU，两边匿名命名空间里的
// AtmosphereMaterialPath 撞名（C2086 / C2374）。与 Star/SunMaterialBuilder.cpp 的
// 做法一致：具名命名空间在任何 unity 分组下都不冲突。
namespace StellarEraAtmosphereMaterialTests
{
	/** 与 PlanetAtmosphereComponent.cpp 的 AtmosphereMaterialPath 是同一个资产。 */
	const TCHAR* const AtmosphereMaterialPath =
		TEXT("/Game/Code/Planet/Materials/M_PlanetAtmosphere");

	UMaterial* LoadAtmosphereMaterial()
	{
		return LoadObject<UMaterial>(nullptr, AtmosphereMaterialPath);
	}

	bool HasParameter(const TArray<FMaterialParameterInfo>& Infos, const FName Name)
	{
		return Infos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	}
} // namespace StellarEraAtmosphereMaterialTests

// 锁死"资产真的按 PlanetAtmosphereMaterialBuilder.cpp 当前的样子重建过"。
// 改生成器代码忘了重跑 StellarEra.BuildAtmosphereMaterial，就会红在这里，
// 而不是等到 PIE 里发现天空还是黑的。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereMaterialStructureTest,
	"StellarEra.Planet.AtmosphereMaterial.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereMaterialStructureTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = StellarEraAtmosphereMaterialTests::LoadAtmosphereMaterial();
	if (!TestNotNull(TEXT("材质资产"), Material))
	{
		AddError(TEXT("资产不存在。先在编辑器里跑一次 StellarEra.BuildAtmosphereMaterial。"));
		return false;
	}

	// 球内看得见要靠双面；不被地形挡掉要靠关深度测试。
	TestTrue(TEXT("TwoSided（球内要看得见背面）"), Material->IsTwoSided());
	TestTrue(TEXT("bDisableDepthTest（不被地形挡掉）"), Material->bDisableDepthTest);

	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarIds;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);

	const TCHAR* const ScalarNames[] = {
		TEXT("PlanetRadius"), TEXT("ShellRadius"),
		TEXT("RayleighScaleHeight"), TEXT("MieScaleHeight"),
		TEXT("CloudBaseRadius"), TEXT("CloudTopRadius"), TEXT("CloudCoverage"),
		TEXT("CloudExtinction"), TEXT("CloudNoiseScale"), TEXT("CloudDetailScale"),
		TEXT("CloudDetailStrength"), TEXT("CloudDrift"), TEXT("CloudBrightness")
	};
	for (const TCHAR* Name : ScalarNames)
	{
		TestTrue(FString::Printf(TEXT("标量参数 %s"), Name),
			StellarEraAtmosphereMaterialTests::HasParameter(ScalarInfos, FName(Name)));
	}

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorIds;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);

	const TCHAR* const VectorNames[] = {
		TEXT("RayleighBeta"), TEXT("MieBeta"), TEXT("PlanetCenter"),
		TEXT("SunDirection"), TEXT("SunIntensity")
	};
	for (const TCHAR* Name : VectorNames)
	{
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name),
			StellarEraAtmosphereMaterialTests::HasParameter(VectorInfos, FName(Name)));
	}

	return true;
}

// HLSL 里出现这两个名字，说明生成器真的加上了面剔除与深度截断，
// 而不只是把材质开关改了。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereMaterialHlslTest,
	"StellarEra.Planet.AtmosphereMaterial.Hlsl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereMaterialHlslTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = StellarEraAtmosphereMaterialTests::LoadAtmosphereMaterial();
	if (!TestNotNull(TEXT("材质资产"), Material))
	{
		return false;
	}

	UMaterialExpressionCustom* Custom = nullptr;
	for (UMaterialExpression* Expr : Material->GetExpressionCollection().Expressions)
	{
		UMaterialExpressionCustom* Candidate = Cast<UMaterialExpressionCustom>(Expr);
		if (Candidate && Candidate->Description == TEXT("PlanetAtmosphereSingleScatter"))
		{
			Custom = Candidate;
			break;
		}
	}

	if (!TestNotNull(TEXT("Custom 节点 PlanetAtmosphereSingleScatter"), Custom))
	{
		return false;
	}

	TestTrue(TEXT("HLSL 含面剔除"), Custom->Code.Contains(TEXT("TwoSidedSign")));
	TestTrue(TEXT("HLSL 含场景深度截断"), Custom->Code.Contains(TEXT("SceneDepthCm")));
	// 夜面不该有辉光：没有这一段，本影里照样散射，暗面会挂一圈蓝边。
	TestTrue(TEXT("HLSL 含太阳遮挡"), Custom->Code.Contains(TEXT("sunVis")));

	// 云：密度函数、噪声、自遮挡步数、云层半径参数。四个都在，才说明云真进了 shader。
	TestTrue(TEXT("HLSL 含云密度函数"), Custom->Code.Contains(TEXT("CloudDensity")));
	TestTrue(TEXT("HLSL 含噪声函数"), Custom->Code.Contains(TEXT("CloudFbm")));
	TestTrue(TEXT("HLSL 含云自遮挡"), Custom->Code.Contains(TEXT("CLOUD_SHADOW_STEPS")));
	TestTrue(TEXT("HLSL 含云层底半径"), Custom->Code.Contains(TEXT("CloudBaseRadius")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
