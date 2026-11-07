// 水材质资产回归：不重建材质（那是编辑器命令的事），只检查「资产在、半透明双面、参数齐全」。
// 资产缺失时的失败信息会直接告诉你该跑哪条命令。

#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "UObject/ConstructorHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const TCHAR* const WaterMaterialPath = TEXT("/Game/Code/Planet/Materials/M_PlanetWater.M_PlanetWater");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStellarEraWaterMaterialTest,
	"StellarEra.Planet.Water.Material",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FStellarEraWaterMaterialTest::RunTest(const FString& Parameters)
{
	UMaterial* Material = LoadObject<UMaterial>(nullptr, WaterMaterialPath);
	if (!TestNotNull(
		TEXT("M_PlanetWater 不存在。在编辑器里执行 StellarEra.BuildPlanetWaterMaterial 生成它"), Material))
	{
		return false;
	}

	TestEqual(TEXT("必须是半透明"), int32(Material->GetBlendMode()), int32(BLEND_Translucent));
	TestTrue(TEXT("必须双面（水下要能看到水面）"), Material->IsTwoSided());

	// 参数齐全只说明「名字在」，说明不了「接到哪个插槽」。下面钉住实际接线。
	//
	// GetEditorOnlyData() 只在 WITH_EDITORONLY_DATA 下存在，而本文件只被
	// WITH_DEV_AUTOMATION_TESTS 包着 —— Development 的 Game 目标里两宏取值不同，不包编不过。
#if WITH_EDITORONLY_DATA
	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (TestNotNull(TEXT("必须拿到 EditorOnlyData"), EditorData))
	{
		TestNotNull(TEXT("BaseColor 必须接线（水色）"), EditorData->BaseColor.Expression);
		TestNotNull(TEXT("Opacity 必须接线（不透明度，否则水面不透明）"), EditorData->Opacity.Expression);
	}
#endif

	TestTrue(TEXT("着色模型必须是 DefaultLit（走场景光照）"),
		Material->GetShadingModels().HasOnlyShadingModel(MSM_DefaultLit));
	TestEqual(TEXT("半透明光照模式必须是 TLM_Surface（默认体积模式没有逐光高光）"),
		int32(Material->TranslucencyLightingMode.GetValue()), int32(TLM_Surface));

	// --- 参数齐全 ---
	TArray<FMaterialParameterInfo> ScalarInfos;
	TArray<FGuid> ScalarGuids;
	Material->GetAllScalarParameterInfo(ScalarInfos, ScalarGuids);

	TArray<FMaterialParameterInfo> VectorInfos;
	TArray<FGuid> VectorGuids;
	Material->GetAllVectorParameterInfo(VectorInfos, VectorGuids);

	const auto HasScalar = [&ScalarInfos](const TCHAR* Name)
	{
		return ScalarInfos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	};
	const auto HasVector = [&VectorInfos](const TCHAR* Name)
	{
		return VectorInfos.ContainsByPredicate(
			[Name](const FMaterialParameterInfo& Info) { return Info.Name == Name; });
	};

	for (const TCHAR* Name : { TEXT("DepthFadeDistance"), TEXT("FoamDepth"),
		TEXT("MaxOpacity"), TEXT("ShallowOpacity"), TEXT("FresnelPower"), TEXT("FoamStrength") })
	{
		TestTrue(FString::Printf(TEXT("缺少标量参数 %s"), Name), HasScalar(Name));
	}
	for (const TCHAR* Name : { TEXT("ShallowColor"), TEXT("DeepColor"), TEXT("FoamColor") })
	{
		TestTrue(FString::Printf(TEXT("缺少向量参数 %s"), Name), HasVector(Name));
	}

	return true;
}

#endif
