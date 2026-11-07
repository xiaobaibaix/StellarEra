#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Engine/TextureCube.h"
#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// ===========================================================================
// 太空背景：一张立方体贴图当成"无穷远的天空"。
// 采样方向必须是「相机 → 像素」：天球半径 1e9 cm 而相机在距原点 1e8 的行星上，
// 用物体空间位置当方向等于假设相机在球心，边缘会畸变约 6° 且天球一挪画面就滑。
// 不用后处理材质：大气壳不写深度，后处理会把它当天空一起盖掉；几何天球走不透明
// 通道，半透明大气在它之后绘制，遮挡自然正确。
// ===========================================================================

// 用具名命名空间：UBT 的 unity build 会把本模块多个 .cpp 合成一个 TU，
// 匿名命名空间的 SaveAsset 会与 Star/SunMaterialBuilder.cpp 撞名（C2084）。
namespace StellarEraSpaceSkyMaterialBuilder
{

const TCHAR* const SkyMaterialPath = TEXT("/Game/Code/Res/Sky/M_SpaceSkybox");
const TCHAR* const SkyMaterialName = TEXT("M_SpaceSkybox");

/** 93 MB 的 HDR 源图导入后得到的立方体贴图。换背景只要换这张图。 */
const TCHAR* const GalaxyCubePath = TEXT("/Game/Code/Res/GalaxyBackground.GalaxyBackground");

bool SaveAsset(UPackage* Package, UObject* Asset, const TCHAR* LongPackageName, FString& OutError)
{
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		LongPackageName, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
		return false;
	}
	return true;
}

/**
 * 建主材质 M_SpaceSkybox。不建预设实例：只有一个亮度参数，改默认值即可。
 */
UMaterial* BuildSpaceSkyMaterial(FString& OutError)
{
	UTextureCube* GalaxyCube = LoadObject<UTextureCube>(nullptr, GalaxyCubePath);
	if (!GalaxyCube)
	{
		// 这是硬引用，丢了材质就没有天空。报出来，别让天球变成黑球。
		OutError = FString::Printf(
			TEXT("找不到 %s —— 先确认 Content/Code/Res/GalaxyBackground.uasset 在，"
				"且 LFS 指针已 smudge。"), GalaxyCubePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(SkyMaterialPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), SkyMaterialPath);
		return nullptr;
	}

	UMaterial* Material = FindObject<UMaterial>(Package, SkyMaterialName);
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, SkyMaterialName, RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return nullptr;
	}

	Material->PreEditChange(nullptr);
	// 重建时先清空：否则每跑一次命令就多叠一套节点。
	Material->GetExpressionCollection().Empty();

	auto AddExpression = [Material](UMaterialExpression* Expr, int32 PosX, int32 PosY) -> void
	{
		Expr->Material = Material;
		Expr->MaterialExpressionEditorX = PosX;
		Expr->MaterialExpressionEditorY = PosY;
		Material->GetExpressionCollection().AddExpression(Expr);
	};

	// --- 视线方向：相机 → 像素 ---
	UMaterialExpressionWorldPosition* WorldPos =
		NewObject<UMaterialExpressionWorldPosition>(Material);
	// 默认就是 Absolute World Position。**不要**改：Default 才是眼睛看到的那个点。
	WorldPos->WorldPositionShaderOffset = WPT_Default;
	AddExpression(WorldPos, -900, 0);

	UMaterialExpressionCameraPositionWS* CameraPos =
		NewObject<UMaterialExpressionCameraPositionWS>(Material);
	AddExpression(CameraPos, -900, 200);

	UMaterialExpressionSubtract* ViewDirection =
		NewObject<UMaterialExpressionSubtract>(Material);
	ViewDirection->A.Expression = WorldPos;
	ViewDirection->A.OutputIndex = 0;
	ViewDirection->B.Expression = CameraPos;
	ViewDirection->B.OutputIndex = 0;
	AddExpression(ViewDirection, -700, 0);

	// --- 立方图采样 ---
	// 必须是 LinearColor：源图关 sRGB、压缩为 TC_HDR_COMPRESSED，用 Color 会报类型不匹配。
	UMaterialExpressionTextureSampleParameterCube* Cube =
		NewObject<UMaterialExpressionTextureSampleParameterCube>(Material);
	Cube->ParameterName = TEXT("GalaxyCube");
	Cube->Texture = GalaxyCube;
	Cube->SamplerType = SAMPLERTYPE_LinearColor;
	// 立方图采样内部会归一化方向向量，这里不必再接 Normalize 节点。
	Cube->Coordinates.Expression = ViewDirection;
	Cube->Coordinates.OutputIndex = 0;
	AddExpression(Cube, -450, 0);

	// --- 亮度 ---
	// 曝光由关卡 PostProcessVolume 决定（手动 +10 EV）。1.0 是实测值，再高会发白。
	UMaterialExpressionScalarParameter* Brightness =
		NewObject<UMaterialExpressionScalarParameter>(Material);
	Brightness->ParameterName = TEXT("Brightness");
	Brightness->DefaultValue = 1.0f;
	AddExpression(Brightness, -700, 300);

	UMaterialExpressionMultiply* Emissive =
		NewObject<UMaterialExpressionMultiply>(Material);
	Emissive->A.Expression = Cube;
	Emissive->A.OutputIndex = 0;
	Emissive->B.Expression = Brightness;
	Emissive->B.OutputIndex = 0;
	AddExpression(Emissive, -200, 0);

	Material->GetEditorOnlyData()->EmissiveColor.Expression = Emissive;
	Material->GetEditorOnlyData()->EmissiveColor.OutputIndex = 0;

	// --- 材质属性 ---
	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Opaque;    // 不透明：走几何通道，大气壳才能盖在它上面
	Material->SetShadingModel(MSM_Unlit);  // 天空不参与光照，只是一个会发光的背景
	// 相机在球内，只有背面可见；不双面的话整颗天球会被背面剔除掉，天空全黑。
	Material->TwoSided = true;

	Material->PostEditChange();
	Material->MarkPackageDirty();

	if (!SaveAsset(Package, Material, SkyMaterialPath, OutError))
	{
		return nullptr;
	}
	return Material;
}

void BuildSpaceSkyAssets()
{
	FString Error;
	if (UMaterial* Material = BuildSpaceSkyMaterial(Error))
	{
		UE_LOG(LogTemp, Display, TEXT("[太空背景] 主材质已生成：%s"), SkyMaterialPath);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[太空背景] 主材质生成失败：%s"), *Error);
	}
}

FAutoConsoleCommand GBuildSpaceSkyMaterialCommand(
	TEXT("StellarEra.BuildSpaceSkyMaterial"),
	TEXT("重建 /Game/Code/Res/Sky/M_SpaceSkybox（采样 GalaxyBackground 的太空背景材质）。"),
	FConsoleCommandDelegate::CreateStatic(&BuildSpaceSkyAssets));

} // namespace StellarEraSpaceSkyMaterialBuilder

#endif // WITH_EDITOR
