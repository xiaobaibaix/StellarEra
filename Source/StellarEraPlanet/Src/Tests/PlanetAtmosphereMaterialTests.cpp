#include "CoreMinimal.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialParameters.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PlanetCloudParams.h"

#if WITH_DEV_AUTOMATION_TESTS

// 用具名命名空间而非匿名：unity build 会把本文件与 PlanetAtmosphereComponent.cpp 塞进
// 同一 TU，两边匿名命名空间的 AtmosphereMaterialPath 撞名（C2086/C2374）。具名命名空间在任何分组下都不冲突。
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

// 锁死「资产真的按 PlanetAtmosphereMaterialBuilder.cpp 重建过」：改了生成器忘了重跑
// StellarEra.BuildAtmosphereMaterial 会红在这里，而不是等到 PIE 里发现天空是黑的。
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
		TEXT("CloudDetailStrength"), TEXT("CloudDrift"), TEXT("CloudBrightness"),
		TEXT("SunCount")
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
		TEXT("RayleighBeta"), TEXT("MieBeta"), TEXT("PlanetCenter"), TEXT("SunIntensity"),
		TEXT("SunDirection0"), TEXT("SunDirection1"), TEXT("SunDirection2"), TEXT("SunDirection3"),
		TEXT("SunRadiance0"), TEXT("SunRadiance1"), TEXT("SunRadiance2"), TEXT("SunRadiance3")
	};
	for (const TCHAR* Name : VectorNames)
	{
		TestTrue(FString::Printf(TEXT("向量参数 %s"), Name),
			StellarEraAtmosphereMaterialTests::HasParameter(VectorInfos, FName(Name)));
	}

	// 单数形式必须已经消失：留着它等于两套太阳方向，HLSL 只认其中一套。
	TestFalse(TEXT("旧的单数 SunDirection 已删除"),
		StellarEraAtmosphereMaterialTests::HasParameter(VectorInfos, FName(TEXT("SunDirection"))));

	// 槽位数是两份字面量（耦合点见 PlanetCloudParams.h）：C++ MaxSuns 与 HLSL PLANET_MAX_SUNS 必须一致。
	TestEqual(TEXT("PlanetClouds::MaxSuns"), PlanetClouds::MaxSuns, 4);

	return true;
}

// HLSL 里出现这两个名字，说明生成器真的加了面剔除与深度截断，不只是改材质开关。
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

	// 云：Code 是 CustomExpressionN 的**函数体**，不能定义函数，函数本体必须在 .ush
	// 并靠 IncludeFilePaths 全局展开。虚拟目录没映射时 MaterialCachedData::Validate
	// 会把这一项删掉，所以这条就是「生成器真的挂上了 .ush」的证据。
	TestTrue(TEXT("Custom 节点引了 PlanetClouds.ush"),
		Custom->IncludeFilePaths.Contains(TEXT("/Project/PlanetClouds.ush")));

	TestTrue(TEXT("Code 调用云 march"), Custom->Code.Contains(TEXT("CloudAtmosphereRaymarch(")));
	TestTrue(TEXT("Code 含云层底半径"), Custom->Code.Contains(TEXT("CloudBaseRadius")));
	TestTrue(TEXT("Code 传第 4 个太阳方向"), Custom->Code.Contains(TEXT("SunDirection3")));
	TestTrue(TEXT("Code 传太阳数量"), Custom->Code.Contains(TEXT("SunCount")));

	// 磁盘上的 .ush 也要有内容：只挂了路径但文件是空的，编译期才会炸。
	const FString CloudUshPath =
		FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders/PlanetClouds.ush"));
	FString CloudUsh;
	TestTrue(TEXT("能读 Shaders/PlanetClouds.ush"), FFileHelper::LoadFileToString(CloudUsh, *CloudUshPath));
	TestTrue(TEXT("ush 含噪声函数"), CloudUsh.Contains(TEXT("CloudFbm")));
	TestTrue(TEXT("ush 含云密度函数"), CloudUsh.Contains(TEXT("float CloudDensity(")));
	TestTrue(TEXT("ush 含云相位函数"), CloudUsh.Contains(TEXT("float CloudPhase(")));
	TestTrue(TEXT("ush 含分段积分"), CloudUsh.Contains(TEXT("CloudAtmoSegment(")));
	TestTrue(TEXT("ush 含云段加密步数"), CloudUsh.Contains(TEXT("CLOUD_STEPS")));
	// 夜面不该有辉光：没有 SunVis，本影里照样散射、暗面挂一圈蓝边。
	TestTrue(TEXT("ush 含太阳遮挡"), CloudUsh.Contains(TEXT("SunVis")));
	TestTrue(TEXT("ush 含太阳集合结构"), CloudUsh.Contains(TEXT("FPlanetSunSet")));
	TestTrue(TEXT("ush 含固定槽位数"), CloudUsh.Contains(TEXT("PLANET_MAX_SUNS 4")));
	TestTrue(TEXT("ush 的太阳循环被展开"), CloudUsh.Contains(TEXT("[unroll]")));
	TestTrue(TEXT("ush 含太阳集合构造函数"), CloudUsh.Contains(TEXT("CloudBuildSunSet(")));
	TestTrue(TEXT("ush 含云自遮挡步数"), CloudUsh.Contains(TEXT("CLOUD_SHADOW_STEPS")));

	// 下面两条是踩过的坑，删掉任何一条画面都不会报错、只会变坏。
	// 1) 覆盖率必须映射到噪声实际取值区间：教科书式 Thresh = 1−Coverage 时，
	//    CloudFbm 分布（均值 0.497、σ 0.050）让 Coverage=0.45 落在 +1σ，只三分之一
	//    穿过阈值、平均密度 0.07，画出来是灰雾而不是云。
	TestTrue(TEXT("ush 覆盖率映射到噪声取值范围"),
		CloudUsh.Contains(TEXT("CLOUD_THRESHOLD_AT_ZERO")) &&
		CloudUsh.Contains(TEXT("CLOUD_THRESHOLD_AT_ONE")));

	// 2) 云细节只能乘到密度上，不能从密度里减。减会以 3 倍频率重新做一次阈值，
	//    而 12~48 步的 march 把这个硬边场重建成满屏的三角格。
	TestFalse(TEXT("ush 不用减法做云细节（会出三角格）"),
		CloudUsh.Contains(TEXT("d -= DetailStrength")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
