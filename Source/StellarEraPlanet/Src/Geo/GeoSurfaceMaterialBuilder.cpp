// 地表材质：按「行星中心 → 该点」的方向采样高程立方图，直接输出颜色。
// 与地质查看器用的是同一张立方图和同一套配色，所以两边看起来一致。
#include "CoreMinimal.h"
#include "Engine/TextureCube.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const SurfaceMaterialPath = TEXT("/Game/Code/Planet/Materials/M_GeoSurface");

	void AddExpr(UMaterial* Material, UMaterialExpression* Expression, int32 X, int32 Y)
	{
		Expression->Material = Material;
		Expression->MaterialExpressionEditorX = X;
		Expression->MaterialExpressionEditorY = Y;
		Material->GetExpressionCollection().AddExpression(Expression);
	}

	UMaterial* BuildGeoSurfaceMaterial(FString& OutError)
	{
		UPackage* Package = CreatePackage(SurfaceMaterialPath);
		UMaterial* Material = NewObject<UMaterial>(Package, TEXT("M_GeoSurface"), RF_Public | RF_Standalone);
		if (!Material)
		{
			OutError = TEXT("创建材质失败");
			return nullptr;
		}

		UMaterialExpressionVectorParameter* PlanetCenter = NewObject<UMaterialExpressionVectorParameter>(Material);
		PlanetCenter->ParameterName = TEXT("PlanetCenter");
		PlanetCenter->DefaultValue = FLinearColor(0.f, 0.f, 0.f, 0.f);
		AddExpr(Material, PlanetCenter, -900, 120);

		UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
		AddExpr(Material, WorldPos, -900, 0);

		// 立方图采样内部会归一化方向，这里不必再接 Normalize 节点。
		UMaterialExpressionSubtract* Dir = NewObject<UMaterialExpressionSubtract>(Material);
		Dir->A.Expression = WorldPos;
		Dir->B.Expression = PlanetCenter;
		AddExpr(Material, Dir, -650, 0);

		UMaterialExpressionTextureSampleParameterCube* Cube =
			NewObject<UMaterialExpressionTextureSampleParameterCube>(Material);
		Cube->ParameterName = TEXT("SurfaceCube");
		Cube->SamplerType = SAMPLERTYPE_Color;
		Cube->Coordinates.Expression = Dir;
		AddExpr(Material, Cube, -400, 0);

		UMaterialExpressionScalarParameter* Brightness = NewObject<UMaterialExpressionScalarParameter>(Material);
		Brightness->ParameterName = TEXT("Brightness");
		Brightness->DefaultValue = 1.0f;
		AddExpr(Material, Brightness, -400, 260);

		UMaterialExpressionMultiply* Emissive = NewObject<UMaterialExpressionMultiply>(Material);
		Emissive->A.Expression = Cube;
		Emissive->B.Expression = Brightness;
		AddExpr(Material, Emissive, -150, 0);

		Material->GetEditorOnlyData()->EmissiveColor.Expression = Emissive;

		// 无光照自发光：地表颜色就是高程配色本身，不再受三颗恒星叠加影响。
		Material->MaterialDomain = MD_Surface;
		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = false;
		Material->PostEditChange();

		OutError.Reset();
		return Material;
	}

	void BuildGeoSurfaceAssets()
	{
		FString Error;
		UMaterial* Material = BuildGeoSurfaceMaterial(Error);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("[地表材质] 生成失败：%s"), *Error);
			return;
		}

		UPackage* Package = Material->GetOutermost();
		Package->MarkPackageDirty();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			SurfaceMaterialPath, FPackageName::GetAssetPackageExtension());
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Package, nullptr, *FileName, Args))
		{
			UE_LOG(LogTemp, Log, TEXT("[地表材质] 已重建 %s"), SurfaceMaterialPath);
		}
	}
}

FAutoConsoleCommand GBuildGeoSurfaceMaterialCommand(
	TEXT("StellarEra.BuildGeoSurfaceMaterial"),
	TEXT("重建 /Game/Code/Planet/Materials/M_GeoSurface（按方向采样高程立方图的无光照地表材质）。"),
	FConsoleCommandDelegate::CreateStatic(&BuildGeoSurfaceAssets));
