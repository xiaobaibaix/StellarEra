// Copyright Voxel Plugin SAS. All Rights Reserved.

// 匹配头必须是第一个 include（UBT 的 IWYU 检查）；它带了 CoreMinimal.h。
#include "PlanetMineralMaterialBuilder.h"

// 路径常量与两个 getter 必须在 WITH_EDITOR 之外：APlanetMineralViewActor::Build
// 在 Game 目标里也要按路径加载资产，只有材质构建本身是编辑器专属。
namespace
{
	const TCHAR* const GhostPath = TEXT("/Game/Materials/M_PlanetGhost");
	const TCHAR* const BlockPath = TEXT("/Game/Materials/M_MineralBlock");
} // namespace

const TCHAR* PlanetMineralMaterials::GetGhostMaterialPath()
{
	return GhostPath;
}

const TCHAR* PlanetMineralMaterials::GetBlockMaterialPath()
{
	return BlockPath;
}

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionFresnel.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* const GhostAssetName = TEXT("M_PlanetGhost");
	const TCHAR* const BlockAssetName = TEXT("M_MineralBlock");

	// 名字带 Mineral 前缀是必须的：本文件与 PlanetAtmosphereMaterialBuilder.cpp 会被
	// UBT 的 unity 构建拼进同一个翻译单元，两个匿名 namespace 会合并，同名的
	// AddScalarParam / AddVectorParam 直接 C2084 重定义。别把它们改回通用名。

	/** 建一个标量参数节点并登记到材质。 */
	UMaterialExpressionScalarParameter* AddMineralScalarParam(UMaterial* Material, const TCHAR* Name,
		float Default, int32 PosX, int32 PosY)
	{
		UMaterialExpressionScalarParameter* Expr =
			NewObject<UMaterialExpressionScalarParameter>(Material);
		Expr->Material = Material;
		Expr->ParameterName = Name;
		Expr->DefaultValue = Default;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}

	/** 建一个向量参数节点并登记到材质。 */
	UMaterialExpressionVectorParameter* AddMineralVectorParam(UMaterial* Material, const TCHAR* Name,
		FLinearColor Default, int32 PosX, int32 PosY)
	{
		UMaterialExpressionVectorParameter* Expr =
			NewObject<UMaterialExpressionVectorParameter>(Material);
		Expr->Material = Material;
		Expr->ParameterName = Name;
		Expr->DefaultValue = Default;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
		return Expr;
	}

	/** 取同名材质或新建一个，并把旧表达式清空。失败返回 nullptr。 */
	UMaterial* FindOrCreateMaterial(const TCHAR* AssetPath, const TCHAR* AssetName,
		FString& OutError)
	{
		UPackage* Package = CreatePackage(AssetPath);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), AssetPath);
			return nullptr;
		}

		UMaterial* Material = FindObject<UMaterial>(Package, AssetName);
		if (!Material)
		{
			Material = NewObject<UMaterial>(Package, AssetName, RF_Public | RF_Standalone);
		}
		if (!Material)
		{
			OutError = FString::Printf(TEXT("NewObject<UMaterial> 失败：%s"), AssetName);
			return nullptr;
		}

		Material->PreEditChange(nullptr);
		Material->GetExpressionCollection().Empty();
		return Material;
	}

	bool SaveMaterial(UMaterial* Material, const TCHAR* AssetPath, FString& OutError)
	{
		Material->PostEditChange();
		Material->GetOutermost()->MarkPackageDirty();

		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			AssetPath, FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Material->GetOutermost(), Material, *PackageFileName, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
			return false;
		}
		return true;
	}

	/**
	 * 玻璃球：Unlit / Translucent / TwoSided。
	 *
	 * 不透明度 = Lerp(Opacity, EdgeOpacity, Fresnel)。BaseReflectFraction 设 0，
	 * 于是正对相机的像素菲涅尔为 0（只剩 Opacity），掠射角接近 1（吃到 EdgeOpacity）——
	 * 球体轮廓清楚，正面又足够透，能看见里面的方块。
	 */
	bool BuildGhostMaterial(FString& OutError)
	{
		UMaterial* Material = FindOrCreateMaterial(GhostPath, GhostAssetName, OutError);
		if (!Material)
		{
			return false;
		}

		UMaterialExpressionVectorParameter* ShellColor = AddMineralVectorParam(Material,
			TEXT("ShellColor"), FLinearColor(0.06f, 0.15f, 0.06f, 1.f), -900, -300);
		UMaterialExpressionScalarParameter* Opacity = AddMineralScalarParam(Material,
			TEXT("Opacity"), 0.12f, -900, -200);
		UMaterialExpressionScalarParameter* EdgeOpacity = AddMineralScalarParam(Material,
			TEXT("EdgeOpacity"), 0.35f, -900, -100);
		UMaterialExpressionScalarParameter* EdgeExponent = AddMineralScalarParam(Material,
			TEXT("EdgeExponent"), 5.f, -900, 0);

		UMaterialExpressionFresnel* Fresnel = NewObject<UMaterialExpressionFresnel>(Material);
		Fresnel->Material = Material;
		// Exponent 走参数：ExponentIn 给了表达式就以输入为准，Exponent 字段只是兜底。
		Fresnel->ExponentIn.Expression = EdgeExponent;
		Fresnel->ExponentIn.OutputIndex = 0;
		Fresnel->BaseReflectFraction = 0.f;
		Fresnel->MaterialExpressionEditorX = -600;
		Fresnel->MaterialExpressionEditorY = 0;
		Material->GetExpressionCollection().AddExpression(Fresnel);

		UMaterialExpressionLinearInterpolate* Lerp =
			NewObject<UMaterialExpressionLinearInterpolate>(Material);
		Lerp->Material = Material;
		Lerp->A.Expression = Opacity;
		Lerp->A.OutputIndex = 0;
		Lerp->B.Expression = EdgeOpacity;
		Lerp->B.OutputIndex = 0;
		Lerp->Alpha.Expression = Fresnel;
		Lerp->Alpha.OutputIndex = 0;
		Lerp->MaterialExpressionEditorX = -300;
		Lerp->MaterialExpressionEditorY = 0;
		Material->GetExpressionCollection().AddExpression(Lerp);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_PlanetGhost）");
			return false;
		}
		EditorData->EmissiveColor.Expression = ShellColor;
		EditorData->EmissiveColor.OutputIndex = 0;
		EditorData->Opacity.Expression = Lerp;
		EditorData->Opacity.OutputIndex = 0;

		Material->BlendMode = BLEND_Translucent;
		Material->SetShadingModel(MSM_Unlit);
		Material->TwoSided = true;
		// 投影是组件属性（SetCastShadow），不是材质属性 —— 在壳组件上设。

		return SaveMaterial(Material, GhostPath, OutError);
	}

	/** 方块：Unlit / Opaque，BlockColor × Brightness → Emissive。 */
	bool BuildBlockMaterial(FString& OutError)
	{
		UMaterial* Material = FindOrCreateMaterial(BlockPath, BlockAssetName, OutError);
		if (!Material)
		{
			return false;
		}

		UMaterialExpressionVectorParameter* BlockColor = AddMineralVectorParam(Material,
			TEXT("BlockColor"), FLinearColor::White, -700, -200);
		UMaterialExpressionScalarParameter* Brightness = AddMineralScalarParam(Material,
			TEXT("Brightness"), 1.4f, -700, -100);

		UMaterialExpressionMultiply* Multiply =
			NewObject<UMaterialExpressionMultiply>(Material);
		Multiply->Material = Material;
		Multiply->A.Expression = BlockColor;
		Multiply->A.OutputIndex = 0;
		Multiply->B.Expression = Brightness;
		Multiply->B.OutputIndex = 0;
		Multiply->MaterialExpressionEditorX = -400;
		Multiply->MaterialExpressionEditorY = -100;
		Material->GetExpressionCollection().AddExpression(Multiply);

		UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			OutError = TEXT("GetEditorOnlyData() 返回空（M_MineralBlock）");
			return false;
		}
		EditorData->EmissiveColor.Expression = Multiply;
		EditorData->EmissiveColor.OutputIndex = 0;

		Material->BlendMode = BLEND_Opaque;
		Material->SetShadingModel(MSM_Unlit);
		// 方块走 UInstancedStaticMeshComponent：不开这一位，ISM 会退回默认材质。
		// 5.8 起成员直接赋值已废弃（C4996），走 SetUsageByFlag。
		Material->SetUsageByFlag(MATUSAGE_InstancedStaticMeshes, true);

		return SaveMaterial(Material, BlockPath, OutError);
	}
} // namespace

bool PlanetMineralMaterials::Build(FString& OutError)
{
	OutError.Reset();
	if (!BuildGhostMaterial(OutError))
	{
		return false;
	}
	return BuildBlockMaterial(OutError);
}

namespace
{
	FAutoConsoleCommandWithWorldAndArgs GBuildMineralMaterialsCommand(
		TEXT("StellarEra.BuildMineralMaterials"),
		TEXT("重建 /Game/Materials/M_PlanetGhost 与 M_MineralBlock。改完材质参数后跑一次。"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				FString Error;
				const bool bOk = PlanetMineralMaterials::Build(Error);
				if (bOk)
				{
					UE_LOG(LogTemp, Display, TEXT("[矿物] 材质已生成：%s / %s"),
						GhostPath, BlockPath);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[矿物] 材质生成失败：%s"), *Error);
				}
				// -TestExit 靠日志里出现这一行退出进程：成功、失败都打，
				// 否则失败时无头进程会一直挂着。
				UE_LOG(LogTemp, Display, TEXT("StellarEra.MineralMaterials.Done ok=%d"),
					bOk ? 1 : 0);
			}));
} // namespace

#endif // WITH_EDITOR
