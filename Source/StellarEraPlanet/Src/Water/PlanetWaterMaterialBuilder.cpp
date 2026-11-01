// 水材质 M_PlanetWater 的构建器（只在编辑器里存在）。

// 匹配头必须是第一个 include（UBT 的 IWYU 检查）；它带了 CoreMinimal.h。
#include "PlanetWaterMaterialBuilder.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionPixelDepth.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionScreenPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const PlanetWaterMaterialAssetPath = TEXT("/Game/Code/Planet/Materials/M_PlanetWater");
const TCHAR* const PlanetWaterMaterialAssetName = TEXT("M_PlanetWater");

// 名字统一带 PlanetWater 前缀：UBT 默认开 unity build，本模块的多个 .cpp 会被拼进
// 同一个翻译单元，匿名 namespace 也跟着合并。PlanetMineralMaterialBuilder.cpp 与
// PlanetAtmosphereMaterialBuilder.cpp 里的 `AddScalarParam` / `AddVectorParam` 已经
// 占用了通用名，这里再叫同名就是 C2084 重定义。
// （测试文件 PlanetWaterMaterialTests.cpp 里还有一个 `WaterMaterialPath`，也不同名。）

// ---------------------------------------------------------------------------
// 水色与不透明度。自包含，不用 IncludeFilePaths（那需要 shader 目录映射）。
//
// 坐标空间全部是**绝对世界坐标（cm）**：
//   WorldPos  —— WorldPosition 节点 WPT_Default 的标题字面就是 "Absolute World Position"
//   CameraPos —— CameraPositionWS 是视图 uniform
//   NormalDir —— 网格顶点法线，我们写的就是径向单位向量（见 PlanetWaterSurfaceBuilder）
//
// 深度渐变用「深度缓冲里的场景深度 − 本像素深度」：正数表示水后面还有东西，
// 数值就是水层厚度。负数（水面前面还有更近的几何）夹到 0。
//
// DepthBuffer 是 SceneTexture 节点的输出：该节点恒为 float4，SceneDepth 走
// CalcSceneDepth（标量）隐式转成四个分量相同，所以取 .x 才是深度（cm）。
//
// Custom 节点里有 return，UE 不会再自动包一层，末尾必须自己 return。
// ---------------------------------------------------------------------------
const TCHAR* const PlanetWaterHLSL = TEXT(R"(
float3 toCamera = CameraPos - WorldPos;
float viewDistance = length(toCamera);
float3 viewDir = viewDistance > 0.0 ? toCamera / viewDistance : float3(0.0, 0.0, 1.0);

// 菲涅尔：掠射角更不透明、更亮。法线是径向的，垂直看下去几乎全透。
float facing = saturate(abs(dot(normalize(NormalDir), viewDir)));
float fresnel = pow(1.0 - facing, FresnelPower);

// 水层厚度（cm）。深度缓冲里没有东西时（天空）给一个大值，让远处的水接近 DeepColor。
float thickness = max(DepthBuffer.x - PixelDepth, 0.0);
float depthFactor = saturate(thickness / max(DepthFadeDistance, 1.0));

float3 waterColor = lerp(ShallowColor.rgb, DeepColor.rgb, depthFactor);

// 泡沫：水层很薄的地方（岸边）泛白。
float foam = saturate(1.0 - thickness / max(FoamDepth, 1.0)) * FoamStrength;
waterColor = lerp(waterColor, FoamColor.rgb, saturate(foam));

// 不透明度：浅处透、深处实，再叠菲涅尔与泡沫。
float opacity = lerp(ShallowOpacity, MaxOpacity, depthFactor);
opacity = saturate(opacity + fresnel * 0.35 + foam * 0.5);
opacity = max(opacity, foam);

return float4(waterColor, opacity);
)");

/** 建一个标量参数节点并登记到材质。 */
UMaterialExpressionScalarParameter* AddPlanetWaterScalarParam(UMaterial* Material, const TCHAR* Name,
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

/** 建一个向量参数节点并登记到材质。 */
UMaterialExpressionVectorParameter* AddPlanetWaterVectorParam(UMaterial* Material, const TCHAR* Name,
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

/** 给 Custom 节点加一个命名输入。InputName 就是 HLSL 里的变量名。 */
void AddPlanetWaterCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName,
	UMaterialExpression* Source)
{
	FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
	In.InputName = InputName;
	In.Input.Expression = Source;
	In.Input.OutputIndex = 0;
}

/** 取资产原本是否存在（用于日志与「重建 vs 新建」的判定）。 */
enum class EPlanetWaterMaterialLoad : uint8
{
	Created,
	Rebuilt,
};

/**
 * 拿到可写的 UMaterial。
 *
 * 关键在「已存在」这条路径：走 LoadPackage 而不是 CreatePackage。CreatePackage 对
 * 磁盘上已有的包只会造出一个 IsFullyLoaded() == false 的空壳，后面的 SavePackage
 * 必定失败（错误还会被 SAVE_NoError 吞掉），也就是「重跑生成器只会打 ok=0」。
 */
UMaterial* LoadOrCreatePlanetWaterMaterial(EPlanetWaterMaterialLoad& OutLoad, FString& OutError)
{
	const bool bExistsOnDisk = FPackageName::DoesPackageExist(PlanetWaterMaterialAssetPath);

	UPackage* Package = nullptr;
	if (bExistsOnDisk)
	{
		Package = LoadPackage(nullptr, PlanetWaterMaterialAssetPath, LOAD_None | LOAD_NoWarn);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("资产已存在但 LoadPackage 失败：%s"),
				PlanetWaterMaterialAssetPath);
			return nullptr;
		}
	}
	else
	{
		Package = CreatePackage(PlanetWaterMaterialAssetPath);
		if (!Package)
		{
			OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), PlanetWaterMaterialAssetPath);
			return nullptr;
		}
	}

	UMaterial* Material = FindObject<UMaterial>(Package, PlanetWaterMaterialAssetName);
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, PlanetWaterMaterialAssetName, RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return nullptr;
	}

	OutLoad = bExistsOnDisk ? EPlanetWaterMaterialLoad::Rebuilt : EPlanetWaterMaterialLoad::Created;

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();
	return Material;
}

bool SavePlanetWaterMaterial(UMaterial* Material, FString& OutError)
{
	Material->PostEditChange();

	UPackage* Package = Material->GetOutermost();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		PlanetWaterMaterialAssetPath, FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	// 刻意不用 SAVE_NoError：保存失败必须留在日志里，不能被吞掉。
	SaveArgs.SaveFlags = SAVE_None;
	if (!UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs))
	{
		OutError = FString::Printf(TEXT("SavePackage 失败：%s"), *PackageFileName);
		return false;
	}
	return true;
}

UMaterial* BuildPlanetWaterMaterialInternal(EPlanetWaterMaterialLoad& OutLoad, FString& OutError)
{
	UMaterial* Material = LoadOrCreatePlanetWaterMaterial(OutLoad, OutError);
	if (!Material)
	{
		return nullptr;
	}

	// --- 几何输入 ---
	UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
	WorldPos->Material = Material;
	WorldPos->WorldPositionShaderOffset = WPT_Default;   // 绝对世界位置
	WorldPos->MaterialExpressionEditorX = -1100;
	WorldPos->MaterialExpressionEditorY = -700;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	// 顶点法线：水面网格写的是径向单位向量，所以它天然是「水面的朝向」。
	UMaterialExpressionVertexNormalWS* NormalWs = NewObject<UMaterialExpressionVertexNormalWS>(Material);
	NormalWs->Material = Material;
	NormalWs->MaterialExpressionEditorX = -1100;
	NormalWs->MaterialExpressionEditorY = -600;
	Material->GetExpressionCollection().AddExpression(NormalWs);

	// 深度缓冲与像素深度。
	//
	// ScreenPosition 节点在 UE 5.8 有两个输出：0 = ViewportUV、1 = PixelPosition。
	// SceneTexture 要的是 **0..1 的视口 UV**，所以取输出 0（默认）；喂 PixelPosition
	// 会采到贴图外，整片水全黑。5.8 已经删掉了旧的 SEM_ViewportUV 属性，不要再设。
	UMaterialExpressionScreenPosition* ScreenPos = NewObject<UMaterialExpressionScreenPosition>(Material);
	ScreenPos->Material = Material;
	ScreenPos->MaterialExpressionEditorX = -1100;
	ScreenPos->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(ScreenPos);

	UMaterialExpressionSceneTexture* SceneDepth = NewObject<UMaterialExpressionSceneTexture>(Material);
	SceneDepth->Material = Material;
	SceneDepth->SceneTextureId = PPI_SceneDepth;
	SceneDepth->Coordinates.Expression = ScreenPos;   // 不接就是全 0，采出一片黑
	SceneDepth->Coordinates.OutputIndex = 0;
	SceneDepth->MaterialExpressionEditorX = -800;
	SceneDepth->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(SceneDepth);

	UMaterialExpressionPixelDepth* PixelDepth = NewObject<UMaterialExpressionPixelDepth>(Material);
	PixelDepth->Material = Material;
	PixelDepth->MaterialExpressionEditorX = -800;
	PixelDepth->MaterialExpressionEditorY = -400;
	Material->GetExpressionCollection().AddExpression(PixelDepth);

	UMaterialExpressionCameraPositionWS* CameraPos = NewObject<UMaterialExpressionCameraPositionWS>(Material);
	CameraPos->Material = Material;
	CameraPos->MaterialExpressionEditorX = -1100;
	CameraPos->MaterialExpressionEditorY = -300;
	Material->GetExpressionCollection().AddExpression(CameraPos);

	// --- 参数 ---
	UMaterialExpressionScalarParameter* DepthFadeDistance = AddPlanetWaterScalarParam(Material, TEXT("DepthFadeDistance"), 400.0f, -1100, -200);
	UMaterialExpressionScalarParameter* FoamDepth = AddPlanetWaterScalarParam(Material, TEXT("FoamDepth"), 60.0f, -1100, -100);
	UMaterialExpressionScalarParameter* MaxOpacity = AddPlanetWaterScalarParam(Material, TEXT("MaxOpacity"), 0.85f, -1100, 0);
	UMaterialExpressionScalarParameter* ShallowOpacity = AddPlanetWaterScalarParam(Material, TEXT("ShallowOpacity"), 0.35f, -1100, 100);
	UMaterialExpressionScalarParameter* FresnelPower = AddPlanetWaterScalarParam(Material, TEXT("FresnelPower"), 4.0f, -1100, 200);
	UMaterialExpressionScalarParameter* FoamStrength = AddPlanetWaterScalarParam(Material, TEXT("FoamStrength"), 0.8f, -1100, 300);

	UMaterialExpressionVectorParameter* ShallowColor = AddPlanetWaterVectorParam(Material, TEXT("ShallowColor"),
		FLinearColor(0.055f, 0.22f, 0.28f, 1.0f), -1100, 400);
	UMaterialExpressionVectorParameter* DeepColor = AddPlanetWaterVectorParam(Material, TEXT("DeepColor"),
		FLinearColor(0.012f, 0.055f, 0.115f, 1.0f), -1100, 500);
	UMaterialExpressionVectorParameter* FoamColor = AddPlanetWaterVectorParam(Material, TEXT("FoamColor"),
		FLinearColor(0.75f, 0.80f, 0.85f, 1.0f), -1100, 600);

	// --- Custom 节点 ---
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("PlanetWaterSurface");
	Custom->Code = PlanetWaterHLSL;
	Custom->OutputType = CMOT_Float4;
	Custom->MaterialExpressionEditorX = -400;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	AddPlanetWaterCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddPlanetWaterCustomInput(Custom, TEXT("NormalDir"), NormalWs);
	AddPlanetWaterCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddPlanetWaterCustomInput(Custom, TEXT("DepthBuffer"), SceneDepth);
	AddPlanetWaterCustomInput(Custom, TEXT("PixelDepth"), PixelDepth);
	AddPlanetWaterCustomInput(Custom, TEXT("DepthFadeDistance"), DepthFadeDistance);
	AddPlanetWaterCustomInput(Custom, TEXT("FoamDepth"), FoamDepth);
	AddPlanetWaterCustomInput(Custom, TEXT("MaxOpacity"), MaxOpacity);
	AddPlanetWaterCustomInput(Custom, TEXT("ShallowOpacity"), ShallowOpacity);
	AddPlanetWaterCustomInput(Custom, TEXT("FresnelPower"), FresnelPower);
	AddPlanetWaterCustomInput(Custom, TEXT("FoamStrength"), FoamStrength);
	AddPlanetWaterCustomInput(Custom, TEXT("ShallowColor"), ShallowColor);
	AddPlanetWaterCustomInput(Custom, TEXT("DeepColor"), DeepColor);
	AddPlanetWaterCustomInput(Custom, TEXT("FoamColor"), FoamColor);

	Custom->RebuildOutputs();

	// --- 拆 float4：RGB → BaseColor，A → Opacity ---
	UMaterialExpressionComponentMask* MaskRGB = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskRGB->Material = Material;
	MaskRGB->Input.Expression = Custom;
	MaskRGB->Input.OutputIndex = 0;
	MaskRGB->R = 1; MaskRGB->G = 1; MaskRGB->B = 1; MaskRGB->A = 0;
	MaskRGB->MaterialExpressionEditorX = -100;
	MaskRGB->MaterialExpressionEditorY = -200;
	Material->GetExpressionCollection().AddExpression(MaskRGB);

	UMaterialExpressionComponentMask* MaskA = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskA->Material = Material;
	MaskA->Input.Expression = Custom;
	MaskA->Input.OutputIndex = 0;
	MaskA->R = 0; MaskA->G = 0; MaskA->B = 0; MaskA->A = 1;
	MaskA->MaterialExpressionEditorX = -100;
	MaskA->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(MaskA);

	// --- 材质本体 ---
	Material->BlendMode = BLEND_Translucent;
	Material->TwoSided = true;   // 水下抬头要看得见水面
	// 着色模型保持默认的 DefaultLit：光照走场景，昼夜与天体光照自动生效。

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return nullptr;
	}
	EditorData->BaseColor.Expression = MaskRGB;
	EditorData->BaseColor.OutputIndex = 0;
	EditorData->Opacity.Expression = MaskA;
	EditorData->Opacity.OutputIndex = 0;

	if (!SavePlanetWaterMaterial(Material, OutError))
	{
		return nullptr;
	}

	return Material;
}

} // namespace

bool BuildPlanetWaterMaterial(FString& OutError)
{
	OutError.Reset();

	EPlanetWaterMaterialLoad Load = EPlanetWaterMaterialLoad::Created;
	UMaterial* Material = nullptr;

	// 失败原因有两段：LoadOrCreate / Build 段由内部写 OutError，Save 段也是。
	Material = BuildPlanetWaterMaterialInternal(Load, OutError);
	if (!Material)
	{
		return false;
	}

	// 调用方（控制台命令）需要知道走的是哪条路径，所以这里把结果写进日志。
	UE_LOG(LogTemp, Display, TEXT("[水体] 材质%s：%s"),
		Load == EPlanetWaterMaterialLoad::Rebuilt ? TEXT("已重建（覆盖既有资产）") : TEXT("已生成（新建）"),
		PlanetWaterMaterialAssetPath);
	return true;
}

namespace
{

FAutoConsoleCommandWithWorldAndArgs GBuildPlanetWaterMaterialCommand(
	TEXT("StellarEra.BuildPlanetWaterMaterial"),
	TEXT("重建 /Game/Code/Planet/Materials/M_PlanetWater。改完 HLSL 字符串或参数后跑一次；已有资产会被覆盖。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			FString Error;
			const bool bOk = BuildPlanetWaterMaterial(Error);
			if (!bOk)
			{
				UE_LOG(LogTemp, Error, TEXT("[水体] 材质生成失败：%s"), *Error);
			}
			// -TestExit 靠日志里出现这一行退出进程：成功、失败都打，
			// 否则失败时无头进程会一直挂着（QUIT 本身不生效，见计划里的环境事实）。
			UE_LOG(LogTemp, Display, TEXT("StellarEra.PlanetWaterMaterial.Done ok=%d"),
				bOk ? 1 : 0);
		}));

} // namespace

#endif // WITH_EDITOR
