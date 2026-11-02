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
//
// 节点图只有四个：
//   (AbsoluteWorldPosition - CameraPositionWS) -> TextureSampleParameterCube
//   -> * Brightness -> Emissive
//
// 为什么采样方向必须算「相机 → 像素」而不是「物体空间位置」：
// 天球是一个半径 1e9 cm 的球，相机并不在球心（在行星上，离原点 1e8）。
// 用物体空间位置当方向，等于假设相机在球心，边缘会被拉出最大约
// arctan(1e8/1e9) ≈ 6° 的畸变；而且天球一挪，画面就整体滑。
// 用视线方向则与天球的位置和半径完全无关 —— 这正是"无穷远"的含义：
// 天球只是把天空光栅化出来的一个载体，你把它挪到哪、缩到多大都不影响构图。
//
// 为什么不用 Post Process 材质直接铺满屏幕：
// 大气壳是「关掉深度测试的半透明」，它不写深度。后处理拿不到它的存在，
// 会按"这里的深度是天空"把宇宙背景盖在大气层上面，球形大气就没了。
// 几何天球走不透明通道，半透明的大气壳在这之后绘制，遮挡关系自然正确。
// ===========================================================================

// 用具名命名空间：UBT 默认 unity build，会把本模块多个 .cpp 合成一个 TU，
// 匿名命名空间里的 SaveAsset 会和 Star/SunMaterialBuilder.cpp 撞名（C2084）。
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
 * 建主材质 M_SpaceSkybox。
 *
 * 不建预设实例：亮度只有一个参数，直接改 Brightness 默认值即可，
 * 没必要为每种观感多存一个资产。
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
	// 默认就是 Absolute World Position（含 WPO 偏移）。这里**不要**改：
	// 天球没有 WPO，两者等价，但 Default 才是"眼睛看到的那个点"。
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
	// 采样器类型必须是 LinearColor：源图 sRGB 是关掉的、压缩是 TC_HDR_COMPRESSED，
	// 用 Color 会报采样器类型不匹配。
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
	// 自发光值直接进 Emissive，曝光由关卡的 PostProcessVolume 决定（手动曝光、
	// +10 EV）。1.0 是实测：星点清晰、星云不糊，再高就变成一张发白的壁纸。
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
