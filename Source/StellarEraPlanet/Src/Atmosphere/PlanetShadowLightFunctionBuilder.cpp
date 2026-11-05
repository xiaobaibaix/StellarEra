// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace StellarEraPlanetShadow
{

const TCHAR* const ShadowAssetPath = TEXT("/Game/Code/Planet/Materials/M_PlanetShadowLightFunction");

// ---------------------------------------------------------------------------
// 行星本体遮挡太阳 —— 光函数（Light Function）材质。
//
// 为什么需要它：定向光的阴影图只覆盖相机附近（本关卡 DynamicShadowDistanceMovableLight
// = 40000 cm = 400 m），而行星半径 20 km、挡住太阳的球体离被照点 17~20 km 远。
// 阴影图里根本没有这块遮挡物，于是夜面的飞船会被三颗太阳直射照亮。
// 地形看着是黑的，那是 N·L < 0 的锅，不是阴影工作正常 —— 同一个手电筒照一件
// 有朝向的物体时，物体总有几面迎着光。
//
// 做法：不解算阴影，直接对「指向太阳的射线 vs 行星球体」做解析求交，
// 结果当乘数乘到该灯的直射项上（LightFunctionCommon.ush:90-100：
// 光函数的输出就是 GetMaterialEmissive）。
//
// 每盏灯各自一份 MID（方向不同），SunDirection 由 C++ 按灯的朝向填，
// 不用材质里的 LightVector 节点 —— 那个在光函数里是「光空间」的向量
// （LightFunctionCommon.ush:23 写明），不是世界空间指向太阳的方向。
//
// 坐标空间：WorldPos 是绝对世界坐标（WorldPosition 节点 WPT_Default），
// PlanetCenter 是 MID 传进来的绝对世界坐标，两者同源，与大气材质一致。
// ---------------------------------------------------------------------------
const TCHAR* const ShadowHLSL = TEXT(R"(
// 改公式后必须 +1：材质 shader map id 只吃 Custom 节点的 Code 字符串，
// 不变就不重编译（同 PlanetAtmosphereMaterialBuilder 里那条注释）。
#define PLANET_SHADOW_VERSION 1

float3 ToCenter = PlanetCenter - WorldPos;
float TowardSun = dot(ToCenter, SunDirection);

// 球心在射线背后 → 行星挡不到这颗太阳。
// 这一支不能省：太阳在天顶时 ToCenter 与 SunDirection 反向，只算垂距会得 0
// （射线穿过球心），把大白天判成深夜。
if (TowardSun <= 0.0)
{
	return 1.0;
}

float3 PerpVec = ToCenter - TowardSun * SunDirection;
float Perp = length(PerpVec);
float Edge = max(Penumbra, 1.0);

// Perp < 半径 = 射线穿过行星本体 = 全黑；球缘外 Penumbra 宽度内线性渐亮。
return saturate((Perp - PlanetRadius) / Edge);
)");

// helper 带 PS 前缀：unity 构建会把同模块所有 .cpp 并进一个编译单元，
// 匿名命名空间在合并后的 blob 里其实是同一个作用域，重名就是 C2084。

UMaterialExpressionScalarParameter* AddPSShadowScalar(UMaterial* Material, const TCHAR* Name,
	float Default, int32 PosX, int32 PosY)
{
	UMaterialExpressionScalarParameter* Expr = NewObject<UMaterialExpressionScalarParameter>(Material);
	Expr->Material = Material;
	Expr->ParameterName = Name;
	Expr->DefaultValue = Default;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

UMaterialExpressionVectorParameter* AddPSShadowVector(UMaterial* Material, const TCHAR* Name,
	FLinearColor Default, int32 PosX, int32 PosY)
{
	UMaterialExpressionVectorParameter* Expr = NewObject<UMaterialExpressionVectorParameter>(Material);
	Expr->Material = Material;
	Expr->ParameterName = Name;
	Expr->DefaultValue = Default;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

bool BuildPlanetShadowLightFunction(FString& OutError)
{
	UPackage* Package = CreatePackage(ShadowAssetPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), ShadowAssetPath);
		return false;
	}

	UMaterial* Material = FindObject<UMaterial>(Package, TEXT("M_PlanetShadowLightFunction"));
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, TEXT("M_PlanetShadowLightFunction"),
			RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return false;
	}

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();

	UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
	WorldPos->Material = Material;
	WorldPos->MaterialExpressionEditorX = -600;
	WorldPos->MaterialExpressionEditorY = -200;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	// 默认值只是占位：C++ 每帧按灯的实际朝向覆盖。
	UMaterialExpressionVectorParameter* SunDirection = AddPSShadowVector(Material,
		TEXT("SunDirection"), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), -600, -60);
	UMaterialExpressionVectorParameter* PlanetCenter = AddPSShadowVector(Material,
		TEXT("PlanetCenter"), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), -600, 80);
	// 20 km：UniverseMap 里那颗体素行星的表面半径（AVoxelPlanetActor::GetSurfaceRadiusCm）。
	UMaterialExpressionScalarParameter* PlanetRadius = AddPSShadowScalar(Material,
		TEXT("PlanetRadius"), 2000000.0f, -600, 220);
	// 2 km 半影：球缘的软边。太窄会在地平线上出现锯齿状硬边。
	UMaterialExpressionScalarParameter* Penumbra = AddPSShadowScalar(Material,
		TEXT("Penumbra"), 20000.0f, -600, 300);

	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("PlanetShadowMask");
	Custom->Code = ShadowHLSL;
	Custom->OutputType = CMOT_Float1;
	Custom->MaterialExpressionEditorX = -300;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	// 名字必须与 HLSL 里的引用一致。
	auto AddInput = [Custom](const TCHAR* InputName, UMaterialExpression* Source)
	{
		FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
		In.InputName = InputName;
		In.Input.Expression = Source;
		In.Input.OutputIndex = 0;
	};
	AddInput(TEXT("WorldPos"), WorldPos);
	AddInput(TEXT("SunDirection"), SunDirection);
	AddInput(TEXT("PlanetCenter"), PlanetCenter);
	AddInput(TEXT("PlanetRadius"), PlanetRadius);
	AddInput(TEXT("Penumbra"), Penumbra);

	Custom->RebuildOutputs();

	// --- 材质本体 ---
	// 光函数域：输出（Emissive）当乘数乘到该灯的直射项上，1 = 全亮、0 = 全黑。
	Material->MaterialDomain = MD_LightFunction;
	Material->BlendMode = BLEND_Opaque;
	Material->SetShadingModel(MSM_Unlit);

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return false;
	}
	// 标量接 float3：材质图允许标量提升为向量，三个通道同值。
	EditorData->EmissiveColor.Expression = Custom;
	EditorData->EmissiveColor.OutputIndex = 0;

	Material->PostEditChange();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		ShadowAssetPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
		return false;
	}

	return true;
}

FAutoConsoleCommandWithWorldAndArgs GBuildPlanetShadowLightFunctionCommand(
	TEXT("StellarEra.BuildPlanetShadowLightFunction"),
	TEXT("重建 /Game/Code/Planet/Materials/M_PlanetShadowLightFunction。改完 HLSL 字符串后跑一次。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			FString Error;
			if (BuildPlanetShadowLightFunction(Error))
			{
				UE_LOG(LogTemp, Display, TEXT("[行星阴影] 光函数材质已生成：%s"), ShadowAssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[行星阴影] 光函数材质生成失败：%s"), *Error);
			}
		}));

} // namespace StellarEraPlanetShadow

#endif // WITH_EDITOR
