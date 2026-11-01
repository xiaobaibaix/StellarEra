#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionTransformPosition.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// ===========================================================================
// 恒星表面材质：一个球 + 一个 Unlit 材质，不依赖任何贴图与粒子。
//
// 构图分三层，全部在这一个材质里：
//   1. 光球湍流 —— 两组 3D 噪声（对流胞 + 慢速翻涌）叠加
//   2. 米粒组织 —— Voronoi 高频细胞，压出硬边
//   3. 临边 —— 边缘压暗（limb darkening）+ 日冕辉光（limb brightening）
//
// 为什么用引擎内置的 Noise 节点而不是全塞进 Custom 的 HLSL：
// Custom 节点的代码是被**插进像素着色函数体内部**的，里面不能定义函数，
// 想把 value noise / fbm 写成可复用的 helper 就得整个手工展开三遍，改一次动全身。
// 所以噪声交给引擎节点（GradientALU 无贴图依赖、Voronoi 出细胞状米粒），
// Custom 只留直线计算：色带映射 + 临边。
//
// 为什么坐标必须走物体空间（TransformPosition World→Local）：
// 一是恒星自转时纹理会跟着转，用世界坐标会“漂”；
// 二是精度 —— 这颗恒星的世界坐标在 5e7 量级，float 在这个量级上的有效位数
// 只剩个位，喂给噪声就是一片死平；物体空间的坐标只有半径量级，精度够。
// ===========================================================================

// 用具名命名空间而不是匿名命名空间：UBT 默认开 unity build，会把本模块多个
// .cpp 合成一个 TU。匿名的 AddScalarParam / AddVectorParam / AddCustomInput 与
// Atmosphere/PlanetAtmosphereMaterialBuilder.cpp 里的同名函数撞名（C2084）。
// 名字前缀保证在任何 unity 分组下都不冲突。
namespace StellarEraSunMaterialBuilder
{

const TCHAR* const SunMaterialDir = TEXT("/Game/Code/Star/Materials");
const TCHAR* const SunMaterialPath = TEXT("/Game/Code/Star/Materials/M_Sun");
const TCHAR* const SunMaterialName = TEXT("M_Sun");

// ---------------------------------------------------------------------------
// 色带 + 临边。输入都是已归一化到 [0,1] 的标量，输出直接进 Emissive。
//
// 注意：这里的代码会被拼进像素着色器函数体，**不能定义子函数**，
// 也只能 return（末尾必须自己 return，UE 不会再补一层）。
// ---------------------------------------------------------------------------
const TCHAR* const SunSurfaceHLSL = TEXT(R"(
// 两组湍流按 55:55 混合再减 0.05：两个噪声的均值都在 0.5 附近，
// 直接相加会把整颗恒星推到色带顶端，变成一颗没有结构的白球。
float Surf = saturate(0.55 * N1 + 0.55 * N2 - 0.05);

// 米粒组织：Voronoi 高频值，四次方压掉低值、只留细胞核的亮边
Surf = saturate(Surf + GranuleContrast * (pow(saturate(G), 4.0) - 0.12));

// 温度色带：暗橙 → 橙黄 → 近白。用 pow 提对比，让亮斑和暗条拉开
float Ramp = pow(Surf, max(ColorPower, 0.01));
float3 Col = (Ramp < 0.5)
	? lerp(CoolColor.rgb, MidColor.rgb, Ramp * 2.0)
	: lerp(MidColor.rgb, HotColor.rgb, saturate((Ramp - 0.5) * 2.0));

// 临边：先按球心法线夹角压暗本体（真实太阳的 limb darkening，约 0.35），
// 再叠一层幂次辉光做日冕。两件事方向相反，同时存在才是“恒星”的观感而不是“塑料球”。
float3 V = normalize(CameraPos - WorldPos);
float NdotV = saturate(dot(normalize(NormalWS), V));
Col *= (1.0 - LimbDarkening * (1.0 - NdotV));
Col += HotColor.rgb * pow(1.0 - NdotV, max(CoronaPower, 0.01)) * CoronaIntensity;

// Brightness 是自发光强度，量级要大（万级）才有过曝发光的感觉；
// 球**外**那圈光晕交给 Bloom 与曝光，不在这里画 —— 靠几何外推做火焰尖
// 在低模球上会被顶点密度卡死，得不偿失。
return Col * Brightness;
)");

/** 建一个标量参数节点并登记到材质。 */
UMaterialExpressionScalarParameter* AddSunScalarParam(UMaterial* Material, const TCHAR* Name,
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
UMaterialExpressionVectorParameter* AddSunVectorParam(UMaterial* Material, const TCHAR* Name,
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

/** 建一个常量节点。 */
UMaterialExpressionConstant* AddConstant(UMaterial* Material, float Value, int32 PosX, int32 PosY)
{
	UMaterialExpressionConstant* Expr = NewObject<UMaterialExpressionConstant>(Material);
	Expr->Material = Material;
	Expr->R = Value;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 乘法节点的统一入口。 */
UMaterialExpressionMultiply* AddMultiply(UMaterial* Material, UMaterialExpression* A,
	UMaterialExpression* B, int32 PosX, int32 PosY)
{
	UMaterialExpressionMultiply* Expr = NewObject<UMaterialExpressionMultiply>(Material);
	Expr->Material = Material;
	Expr->A.Expression = A;
	Expr->A.OutputIndex = 0;
	Expr->B.Expression = B;
	Expr->B.OutputIndex = 0;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 除法节点：A / B。 */
UMaterialExpressionDivide* AddDivide(UMaterial* Material, UMaterialExpression* A,
	UMaterialExpression* B, int32 PosX, int32 PosY)
{
	UMaterialExpressionDivide* Expr = NewObject<UMaterialExpressionDivide>(Material);
	Expr->Material = Material;
	Expr->A.Expression = A;
	Expr->A.OutputIndex = 0;
	Expr->B.Expression = B;
	Expr->B.OutputIndex = 0;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 加法节点。 */
UMaterialExpressionAdd* AddAdd(UMaterial* Material, UMaterialExpression* A,
	UMaterialExpression* B, int32 PosX, int32 PosY)
{
	UMaterialExpressionAdd* Expr = NewObject<UMaterialExpressionAdd>(Material);
	Expr->Material = Material;
	Expr->A.Expression = A;
	Expr->A.OutputIndex = 0;
	Expr->B.Expression = B;
	Expr->B.OutputIndex = 0;
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;
	Material->GetExpressionCollection().AddExpression(Expr);
	return Expr;
}

/** 给 Custom 节点加一个命名输入。InputName 就是 HLSL 里的变量名。 */
void AddSunCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName, UMaterialExpression* Source)
{
	FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
	In.InputName = InputName;
	In.Input.Expression = Source;
	In.Input.OutputIndex = 0;
}

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
 * 建主材质 M_Sun。参数全部暴露，实例只覆盖值不动节点。
 */
UMaterial* BuildSunMaterial(FString& OutError)
{
	UPackage* Package = CreatePackage(SunMaterialPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), SunMaterialPath);
		return nullptr;
	}

	UMaterial* Material = FindObject<UMaterial>(Package, SunMaterialName);
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, SunMaterialName, RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return nullptr;
	}

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();

	// --- 物体空间坐标：噪声的锚点 ---
	UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
	WorldPos->Material = Material;
	WorldPos->WorldPositionShaderOffset = WPT_Default; // "Absolute World Position"
	WorldPos->MaterialExpressionEditorX = -1600;
	WorldPos->MaterialExpressionEditorY = -900;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	UMaterialExpressionTransformPosition* ObjPos = NewObject<UMaterialExpressionTransformPosition>(Material);
	ObjPos->Material = Material;
	ObjPos->Input.Expression = WorldPos;
	ObjPos->Input.OutputIndex = 0;
	ObjPos->TransformSourceType = TRANSFORMPOSSOURCE_World;
	ObjPos->TransformType = TRANSFORMPOSSOURCE_Local;
	ObjPos->MaterialExpressionEditorX = -1400;
	ObjPos->MaterialExpressionEditorY = -900;
	Material->GetExpressionCollection().AddExpression(ObjPos);

	// --- 尺度 ---
	// BodyRadius 必须等于恒星网格的实际半径（uu）。噪声坐标 = 物体空间 / 半径，
	// 于是 SurfaceScale 的含义就变成「每半径几团湍流」，换网格尺寸只要改一个数。
	UMaterialExpressionScalarParameter* BodyRadius = AddSunScalarParam(Material, TEXT("BodyRadius"), 100.0f, -1600, -700);
	UMaterialExpressionScalarParameter* SurfaceScale = AddSunScalarParam(Material, TEXT("SurfaceScale"), 8.0f, -1600, -600);

	UMaterialExpressionMultiply* ScaledXY = AddMultiply(Material, ObjPos, SurfaceScale, -1200, -850);
	UMaterialExpressionDivide* Scaled = AddDivide(Material, ScaledXY, BodyRadius, -1050, -850);

	// --- 流动：两层噪声反向平移，才有“沸腾”而不是“整体滑动” ---
	UMaterialExpressionTime* Time = NewObject<UMaterialExpressionTime>(Material);
	Time->Material = Material;
	Time->bIgnorePause = false;
	Time->MaterialExpressionEditorX = -1600;
	Time->MaterialExpressionEditorY = -500;
	Material->GetExpressionCollection().AddExpression(Time);

	UMaterialExpressionScalarParameter* FlowSpeed = AddSunScalarParam(Material, TEXT("FlowSpeed"), 0.35f, -1600, -400);
	UMaterialExpressionVectorParameter* FlowDirection = AddSunVectorParam(Material, TEXT("FlowDirection"),
		FLinearColor(0.31f, 0.17f, 0.93f, 1.0f), -1600, -300);

	UMaterialExpressionMultiply* FlowT = AddMultiply(Material, Time, FlowSpeed, -1300, -500);
	UMaterialExpressionMultiply* OffsetA = AddMultiply(Material, FlowT, FlowDirection, -1150, -500);

	UMaterialExpressionAdd* PosA = AddAdd(Material, Scaled, OffsetA, -900, -850);

	// 第二层：频率 1.9 倍、沿反方向走 0.73 倍速度。
	// 用乘负数代替 Subtract 节点，少一个头文件依赖。
	UMaterialExpressionConstant* FreqB = AddConstant(Material, 1.9f, -1200, -700);
	UMaterialExpressionMultiply* ScaledB = AddMultiply(Material, Scaled, FreqB, -1050, -700);
	UMaterialExpressionConstant* BackB = AddConstant(Material, -0.73f, -1150, -400);
	UMaterialExpressionMultiply* OffsetB = AddMultiply(Material, OffsetA, BackB, -1000, -400);
	UMaterialExpressionAdd* PosB = AddAdd(Material, ScaledB, OffsetB, -900, -700);

	// --- 噪声 1/2：对流胞本体 ---
	UMaterialExpressionNoise* NoiseA = NewObject<UMaterialExpressionNoise>(Material);
	NoiseA->Material = Material;
	NoiseA->Position.Expression = PosA;
	NoiseA->Position.OutputIndex = 0;
	NoiseA->NoiseFunction = NOISEFUNCTION_GradientALU; // 无贴图依赖
	NoiseA->bTurbulence = true;                        // 内部按 Levels 叠加八度
	NoiseA->Levels = 3;
	NoiseA->LevelScale = 2.0f;
	NoiseA->Quality = 1;
	NoiseA->OutputMin = 0.0f;
	NoiseA->OutputMax = 1.0f;
	NoiseA->WorldPositionOriginType = EPositionOrigin::Absolute;
	NoiseA->MaterialExpressionEditorX = -750;
	NoiseA->MaterialExpressionEditorY = -850;
	Material->GetExpressionCollection().AddExpression(NoiseA);

	UMaterialExpressionNoise* NoiseB = NewObject<UMaterialExpressionNoise>(Material);
	NoiseB->Material = Material;
	NoiseB->Position.Expression = PosB;
	NoiseB->Position.OutputIndex = 0;
	NoiseB->NoiseFunction = NOISEFUNCTION_GradientALU;
	NoiseB->bTurbulence = true;
	NoiseB->Levels = 2;
	NoiseB->LevelScale = 2.0f;
	NoiseB->Quality = 1;
	NoiseB->OutputMin = 0.0f;
	NoiseB->OutputMax = 1.0f;
	NoiseB->WorldPositionOriginType = EPositionOrigin::Absolute;
	NoiseB->MaterialExpressionEditorX = -750;
	NoiseB->MaterialExpressionEditorY = -650;
	Material->GetExpressionCollection().AddExpression(NoiseB);

	// --- 米粒组织：Voronoi 细胞，频率单独给参数 ---
	UMaterialExpressionScalarParameter* GranuleScale = AddSunScalarParam(Material, TEXT("GranuleScale"), 34.0f, -1600, -200);
	UMaterialExpressionMultiply* ScaledG = AddMultiply(Material, Scaled, GranuleScale, -1050, -250);

	UMaterialExpressionNoise* NoiseG = NewObject<UMaterialExpressionNoise>(Material);
	NoiseG->Material = Material;
	NoiseG->Position.Expression = ScaledG;
	NoiseG->Position.OutputIndex = 0;
	NoiseG->NoiseFunction = NOISEFUNCTION_VoronoiALU; // Worley / 细胞噪声
	NoiseG->bTurbulence = false;
	NoiseG->Levels = 1;
	NoiseG->Quality = 1;
	NoiseG->OutputMin = 0.0f;
	NoiseG->OutputMax = 1.0f;
	NoiseG->WorldPositionOriginType = EPositionOrigin::Absolute;
	NoiseG->MaterialExpressionEditorX = -750;
	NoiseG->MaterialExpressionEditorY = -450;
	Material->GetExpressionCollection().AddExpression(NoiseG);

	// --- 临边需要的两个几何量 ---
	UMaterialExpressionVertexNormalWS* NormalWS = NewObject<UMaterialExpressionVertexNormalWS>(Material);
	NormalWS->Material = Material;
	NormalWS->MaterialExpressionEditorX = -1600;
	NormalWS->MaterialExpressionEditorY = 100;
	Material->GetExpressionCollection().AddExpression(NormalWS);

	// 相机位置取视图 uniform，不由 C++ 每帧写参数（跟着大气材质踩过的坑，见
	// PlanetAtmosphereMaterialBuilder.cpp 的注释：编辑器里 Tick 早于视口，会慢一帧）。
	UMaterialExpressionCameraPositionWS* CameraPos = NewObject<UMaterialExpressionCameraPositionWS>(Material);
	CameraPos->Material = Material;
	CameraPos->MaterialExpressionEditorX = -1600;
	CameraPos->MaterialExpressionEditorY = 250;
	Material->GetExpressionCollection().AddExpression(CameraPos);

	// --- 颜色与强度参数 ---
	UMaterialExpressionVectorParameter* CoolColor = AddSunVectorParam(Material, TEXT("CoolColor"),
		FLinearColor(1.0f, 0.18f, 0.02f, 1.0f), -1600, 400);
	UMaterialExpressionVectorParameter* MidColor = AddSunVectorParam(Material, TEXT("MidColor"),
		FLinearColor(1.0f, 0.55f, 0.10f, 1.0f), -1600, 500);
	UMaterialExpressionVectorParameter* HotColor = AddSunVectorParam(Material, TEXT("HotColor"),
		FLinearColor(1.0f, 0.94f, 0.78f, 1.0f), -1600, 600);
	UMaterialExpressionScalarParameter* GranuleContrast = AddSunScalarParam(Material, TEXT("GranuleContrast"), 0.35f, -1600, 700);
	UMaterialExpressionScalarParameter* ColorPower = AddSunScalarParam(Material, TEXT("ColorPower"), 1.5f, -1600, 800);
	UMaterialExpressionScalarParameter* CoronaPower = AddSunScalarParam(Material, TEXT("CoronaPower"), 2.5f, -1600, 900);
	UMaterialExpressionScalarParameter* CoronaIntensity = AddSunScalarParam(Material, TEXT("CoronaIntensity"), 1.1f, -1600, 1000);
	UMaterialExpressionScalarParameter* LimbDarkening = AddSunScalarParam(Material, TEXT("LimbDarkening"), 0.35f, -1600, 1100);
	UMaterialExpressionScalarParameter* Brightness = AddSunScalarParam(Material, TEXT("Brightness"), 40000.0f, -1600, 1200);

	// --- Custom：色带 + 临边 ---
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("SunSurface");
	Custom->Code = SunSurfaceHLSL;
	Custom->OutputType = CMOT_Float3;
	Custom->MaterialExpressionEditorX = -300;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	// 名字必须与 HLSL 里的引用完全一致。
	AddSunCustomInput(Custom, TEXT("N1"), NoiseA);
	AddSunCustomInput(Custom, TEXT("N2"), NoiseB);
	AddSunCustomInput(Custom, TEXT("G"), NoiseG);
	AddSunCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddSunCustomInput(Custom, TEXT("NormalWS"), NormalWS);
	AddSunCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddSunCustomInput(Custom, TEXT("CoolColor"), CoolColor);
	AddSunCustomInput(Custom, TEXT("MidColor"), MidColor);
	AddSunCustomInput(Custom, TEXT("HotColor"), HotColor);
	AddSunCustomInput(Custom, TEXT("GranuleContrast"), GranuleContrast);
	AddSunCustomInput(Custom, TEXT("ColorPower"), ColorPower);
	AddSunCustomInput(Custom, TEXT("CoronaPower"), CoronaPower);
	AddSunCustomInput(Custom, TEXT("CoronaIntensity"), CoronaIntensity);
	AddSunCustomInput(Custom, TEXT("LimbDarkening"), LimbDarkening);
	AddSunCustomInput(Custom, TEXT("Brightness"), Brightness);

	Custom->RebuildOutputs();

	// --- 材质本体 ---
	Material->BlendMode = BLEND_Opaque;    // 恒星本体不透明，避免半透明排序问题
	Material->SetShadingModel(MSM_Unlit);  // 自发光天体不受光照影响
	Material->TwoSided = false;

	// 不开 bUseEmissiveForDynamicAreaLighting：
	// 恒星照亮行星由平行光（或后续的恒星光照组件）负责。让 Lumen 从一颗
	// 几万强度的超大自发光球上做表面缓存采样，代价极高，而且会和已有的
	// 太阳方向光重复照明。
	Material->bUseEmissiveForDynamicAreaLighting = false;

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return nullptr;
	}
	EditorData->EmissiveColor.Expression = Custom;
	EditorData->EmissiveColor.OutputIndex = 0;

	Material->PostEditChange();
	Package->MarkPackageDirty();

	FString SaveError;
	if (!SaveAsset(Package, Material, SunMaterialPath, SaveError))
	{
		OutError = SaveError;
		return nullptr;
	}

	return Material;
}

/**
 * 一颗预设恒星。参数按黑体色温排：色温越高，HotColor 越偏蓝白、亮度越高。
 * 数值是观感值不是物理值 —— 真按普朗克曲线算出来的色带在 UE 的色调映射下
 * 会全部挤到白，反而丢掉层次。
 */
struct FSunPreset
{
	const TCHAR* AssetName;
	const TCHAR* Label;
	FLinearColor CoolColor;
	FLinearColor MidColor;
	FLinearColor HotColor;
	float Brightness;
	float CoronaIntensity;
	float GranuleContrast;
};

const FSunPreset SunPresets[] =
{
	// 红矮星（M2V）：暗、偏红、对流胞大而少 → 米粒组织弱
	{
		TEXT("MI_Sun_M2V"), TEXT("红矮星 M2V"),
		FLinearColor(0.62f, 0.08f, 0.01f, 1.0f),
		FLinearColor(1.0f, 0.28f, 0.04f, 1.0f),
		FLinearColor(1.0f, 0.62f, 0.26f, 1.0f),
		14000.0f, 0.85f, 0.22f
	},
	// 类太阳（G2V）：默认配色
	{
		TEXT("MI_Sun_G2V"), TEXT("类太阳 G2V"),
		FLinearColor(1.0f, 0.18f, 0.02f, 1.0f),
		FLinearColor(1.0f, 0.55f, 0.10f, 1.0f),
		FLinearColor(1.0f, 0.94f, 0.78f, 1.0f),
		40000.0f, 1.10f, 0.35f
	},
	// 蓝巨星（B3V）：高色温，边缘辉光更强
	{
		TEXT("MI_Sun_B3V"), TEXT("蓝巨星 B3V"),
		FLinearColor(0.30f, 0.42f, 0.95f, 1.0f),
		FLinearColor(0.62f, 0.78f, 1.0f, 1.0f),
		FLinearColor(0.92f, 0.96f, 1.0f, 1.0f),
		140000.0f, 1.8f, 0.30f
	},
};

/** 建一个材质实例常量并写入预设参数。 */
bool BuildSunPreset(UMaterial* Parent, const FSunPreset& Preset, FString& OutError)
{
	const FString PackageName = FString::Printf(TEXT("%s/%s"), SunMaterialDir, Preset.AssetName);

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), *PackageName);
		return false;
	}

	UMaterialInstanceConstant* Instance = FindObject<UMaterialInstanceConstant>(Package, Preset.AssetName);
	if (!Instance)
	{
		Instance = NewObject<UMaterialInstanceConstant>(Package, Preset.AssetName, RF_Public | RF_Standalone);
	}
	if (!Instance)
	{
		OutError = FString::Printf(TEXT("NewObject<UMaterialInstanceConstant> 失败：%s"), Preset.AssetName);
		return false;
	}

	Instance->PreEditChange(nullptr);
	Instance->SetParentEditorOnly(Parent);

	Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("CoolColor"))), Preset.CoolColor);
	Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("MidColor"))), Preset.MidColor);
	Instance->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("HotColor"))), Preset.HotColor);
	Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("Brightness"))), Preset.Brightness);
	Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("CoronaIntensity"))), Preset.CoronaIntensity);
	Instance->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(FName(TEXT("GranuleContrast"))), Preset.GranuleContrast);

	Instance->PostEditChange();
	Package->MarkPackageDirty();

	return SaveAsset(Package, Instance, *PackageName, OutError);
}

void BuildAllSunAssets()
{
	FString Error;
	UMaterial* SunMaterial = BuildSunMaterial(Error);
	if (!SunMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[恒星] 主材质生成失败：%s"), *Error);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[恒星] 主材质已生成：%s"), SunMaterialPath);

	for (const FSunPreset& Preset : SunPresets)
	{
		if (BuildSunPreset(SunMaterial, Preset, Error))
		{
			UE_LOG(LogTemp, Display, TEXT("[恒星] 预设已生成：%s/%s（%s）"),
				SunMaterialDir, Preset.AssetName, Preset.Label);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[恒星] 预设生成失败：%s（%s）"), *Error, Preset.AssetName);
		}
	}
}

FAutoConsoleCommand GBuildSunMaterialCommand(
	TEXT("StellarEra.BuildSunMaterial"),
	TEXT("重建 /Game/Code/Star/Materials/M_Sun 与三个恒星预设（M2V / G2V / B3V）。改完 HLSL 或参数后跑一次。"),
	FConsoleCommandDelegate::CreateStatic(&BuildAllSunAssets));

} // namespace StellarEraSunMaterialBuilder

#endif // WITH_EDITOR
