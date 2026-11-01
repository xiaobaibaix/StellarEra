#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
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
}

// 锁死"资产真的按 PlanetAtmosphereMaterialBuilder.cpp 当前的样子重建过"。
// 改生成器代码忘了重跑 StellarEra.BuildAtmosphereMaterial，就会红在这里，
// 而不是等到 PIE 里发现天空还是黑的。
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraAtmosphereMaterialStructureTest,
	"StellarEra.Planet.AtmosphereMaterial.Structure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FStellarEraAtmosphereMaterialStructureTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = LoadAtmosphereMaterial();
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
		TEXT("RayleighScaleHeight"), TEXT("MieScaleHeight")
	};
	for (const TCHAR* Name : ScalarNames)
	{
		TestTrue(FString::Printf(TEXT("标量参数 %s"), Name), HasParameter(ScalarInfos, FName(Name)));
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
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name), HasParameter(VectorInfos, FName(Name)));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
