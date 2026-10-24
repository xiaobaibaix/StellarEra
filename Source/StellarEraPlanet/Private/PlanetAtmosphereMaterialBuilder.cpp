#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const AtmosphereAssetPath = TEXT("/Game/Materials/M_PlanetAtmosphere");

// ---------------------------------------------------------------------------
// 壳的单次散射。自包含：不用 IncludeFilePaths（那需要一套 shader 目录映射），
// 不调用引擎的散射函数（那些要 LUT 或全局 Atmosphere uniform，材质里拿不到）。
//
// 坐标空间（已从引擎源码钉死，见计划「与规格的偏差」第 4 条）：
//   WorldPos / CameraPos / PlanetCenter 全是**绝对世界坐标（cm）**。
//   WorldPosition 节点 WPT_Default 的标题字面就是 "Absolute World Position"；
//   CameraPos 是视图 uniform（CameraPositionWS），同样在绝对世界坐标下。
//
// 注意：因为函数体里有 return，UE 不会再自动包一层 return，末尾必须自己 return。
// ---------------------------------------------------------------------------
const TCHAR* const AtmosphereHLSL = TEXT(R"(
#define ATMO_STEPS 16
#define ATMO_PI 3.14159265358979323846

float3 ro = CameraPos;
float3 rd = normalize(WorldPos - CameraPos);

// 与外球（壳）求交
float3 oc = ro - PlanetCenter;
float bOuter = dot(oc, rd);
float cOuter = dot(oc, oc) - ShellRadius * ShellRadius;
float discOuter = bOuter * bOuter - cOuter;
if (discOuter < 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}
float sqrtOuter = sqrt(discOuter);
float tEnter = max(-bOuter - sqrtOuter, 0.0);
float tExit = -bOuter + sqrtOuter;
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// 撞到行星地面就停在表面
float cInner = dot(oc, oc) - PlanetRadius * PlanetRadius;
float discInner = bOuter * bOuter - cInner;
if (discInner >= 0.0)
{
    float tGround = -bOuter - sqrt(discInner);
    if (tGround > tEnter)
    {
        tExit = min(tExit, tGround);
    }
}
if (tExit <= tEnter)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

// 相位函数：瑞利各向同性、米氏 Henyey-Greenstein。
// g = 0.8 与引擎 USkyAtmosphereComponent 的 MieAnisotropy 默认值一致，
// 这样壳与引擎大气的前向散射波瓣形状相同，穿过壳边界时观感不突变。
float cosTheta = dot(rd, SunDirection.rgb);
float phaseR = 3.0 / (16.0 * ATMO_PI) * (1.0 + cosTheta * cosTheta);
float g = 0.8;
float g2 = g * g;
float phaseM = (1.0 - g2) / (4.0 * ATMO_PI * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));

// 单次散射积分：固定步长中点法
float ds = (tExit - tEnter) / float(ATMO_STEPS);
float3 transmittance = float3(1.0, 1.0, 1.0);
float3 accumulated = float3(0.0, 0.0, 0.0);
for (int i = 0; i < ATMO_STEPS; ++i)
{
    float t = tEnter + (float(i) + 0.5) * ds;
    float3 p = ro + rd * t;
    float h = max(length(p - PlanetCenter) - PlanetRadius, 0.0);
    float dR = exp(-h / RayleighScaleHeight);
    float dM = exp(-h / MieScaleHeight);
    float3 extinction = RayleighBeta.rgb * dR + MieBeta.rgb * dM;
    transmittance *= exp(-extinction * ds);
    accumulated += (RayleighBeta.rgb * dR * phaseR + MieBeta.rgb * dM * phaseM) * transmittance * ds;
}

float coverage = saturate(1.0 - dot(transmittance, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
return float4(accumulated * SunIntensity.rgb, coverage);
)");

/** 建一个标量参数节点并登记到材质。 */
UMaterialExpressionScalarParameter* AddScalarParam(UMaterial* Material, const TCHAR* Name,
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
UMaterialExpressionVectorParameter* AddVectorParam(UMaterial* Material, const TCHAR* Name,
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
void AddCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName, UMaterialExpression* Source)
{
	FCustomInput& In = Custom->Inputs.AddDefaulted_GetRef();
	In.InputName = InputName;
	In.Input.Expression = Source;
	In.Input.OutputIndex = 0;
}

bool BuildPlanetAtmosphereMaterial(FString& OutError)
{
	UPackage* Package = CreatePackage(AtmosphereAssetPath);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("CreatePackage 失败：%s"), AtmosphereAssetPath);
		return false;
	}

	// 已存在就复用，避免同名对象冲突。
	UMaterial* Material = FindObject<UMaterial>(Package, TEXT("M_PlanetAtmosphere"));
	if (!Material)
	{
		Material = NewObject<UMaterial>(Package, TEXT("M_PlanetAtmosphere"), RF_Public | RF_Standalone);
	}
	if (!Material)
	{
		OutError = TEXT("NewObject<UMaterial> 失败");
		return false;
	}

	Material->PreEditChange(nullptr);
	Material->GetExpressionCollection().Empty();

	// --- 几何输入 ---
	UMaterialExpressionWorldPosition* WorldPos = NewObject<UMaterialExpressionWorldPosition>(Material);
	WorldPos->Material = Material;
	WorldPos->WorldPositionShaderOffset = WPT_Default; // "Absolute World Position"
	WorldPos->MaterialExpressionEditorX = -800;
	WorldPos->MaterialExpressionEditorY = -600;
	Material->GetExpressionCollection().AddExpression(WorldPos);

	// --- 参数节点。初值与 Task 1 的 ComputeShellScatteringParams 在 0.75km 行星上
	// --- 算出的量级一致；Task 3 会在 BeginPlay 里按实际行星半径覆盖掉。
	UMaterialExpressionScalarParameter* PlanetRadius = AddScalarParam(Material, TEXT("PlanetRadius"), 75000.0f, -800, -400);
	UMaterialExpressionScalarParameter* ShellRadius = AddScalarParam(Material, TEXT("ShellRadius"), 78750.0f, -800, -300);
	UMaterialExpressionScalarParameter* RayleighScaleHeight = AddScalarParam(Material, TEXT("RayleighScaleHeight"), 1181.25f, -800, -200);
	UMaterialExpressionScalarParameter* MieScaleHeight = AddScalarParam(Material, TEXT("MieScaleHeight"), 236.25f, -800, -100);

	// 2.24169e-4 = 0.2648 / 1181.25（地球瑞利垂直光学厚度 / 壳标高）
	UMaterialExpressionVectorParameter* RayleighBeta = AddVectorParam(Material, TEXT("RayleighBeta"),
		FLinearColor(3.92938e-5f, 9.18247e-5f, 2.24169e-4f, 1.0f), -800, 0);
	// 2.25524e-5 = 0.005328 / 236.25
	UMaterialExpressionVectorParameter* MieBeta = AddVectorParam(Material, TEXT("MieBeta"),
		FLinearColor(2.25524e-5f, 2.25524e-5f, 2.25524e-5f, 1.0f), -800, 100);

	// 相机位置取视图 uniform（CameraPositionWS 节点 → MEVP_WorldSpaceCameraPosition，
	// 渲染器按当前视口逐帧给出），**不再**由 C++ 每帧写 MID 参数。
	//
	// 曾经是向量参数：编辑器里组件的 Tick 跑在视口 Tick 之前（EditorEngine.cpp 里
	// World()->Tick 在视口循环之前），写进 MID 的相机位置永远慢一帧。快速移动视口时
	// 射线起点落后于真实相机，靠近行星边缘的像素会被算成一条长得多的壳内弦长，
	// 单次散射饱和成一条硬边暖白色带 —— 静止时相机不动，滞后无害，所以只在运动时出现。
	UMaterialExpressionCameraPositionWS* CameraPos = NewObject<UMaterialExpressionCameraPositionWS>(Material);
	CameraPos->Material = Material;
	CameraPos->MaterialExpressionEditorX = -800;
	CameraPos->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(CameraPos);
	UMaterialExpressionVectorParameter* PlanetCenter = AddVectorParam(Material, TEXT("PlanetCenter"),
		FLinearColor::Black, -800, 300);
	UMaterialExpressionVectorParameter* SunDirection = AddVectorParam(Material, TEXT("SunDirection"),
		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), -800, 400);
	UMaterialExpressionVectorParameter* SunIntensity = AddVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 500);

	// --- Custom 节点 ---
	UMaterialExpressionCustom* Custom = NewObject<UMaterialExpressionCustom>(Material);
	Custom->Material = Material;
	Custom->Description = TEXT("PlanetAtmosphereSingleScatter");
	Custom->Code = AtmosphereHLSL;
	Custom->OutputType = CMOT_Float4;
	Custom->MaterialExpressionEditorX = -300;
	Custom->MaterialExpressionEditorY = 0;
	Material->GetExpressionCollection().AddExpression(Custom);

	// 名字必须与 HLSL 里的引用一致；顺序只是可读性。
	AddCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddCustomInput(Custom, TEXT("PlanetRadius"), PlanetRadius);
	AddCustomInput(Custom, TEXT("ShellRadius"), ShellRadius);
	AddCustomInput(Custom, TEXT("PlanetCenter"), PlanetCenter);
	AddCustomInput(Custom, TEXT("SunDirection"), SunDirection);
	AddCustomInput(Custom, TEXT("RayleighScaleHeight"), RayleighScaleHeight);
	AddCustomInput(Custom, TEXT("MieScaleHeight"), MieScaleHeight);
	AddCustomInput(Custom, TEXT("RayleighBeta"), RayleighBeta);
	AddCustomInput(Custom, TEXT("MieBeta"), MieBeta);
	AddCustomInput(Custom, TEXT("SunIntensity"), SunIntensity);

	Custom->RebuildOutputs();

	// --- 拆 float4 输出：RGB → Emissive，A → Opacity ---
	UMaterialExpressionComponentMask* MaskRGB = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskRGB->Material = Material;
	MaskRGB->Input.Expression = Custom;
	MaskRGB->Input.OutputIndex = 0;
	MaskRGB->R = 1; MaskRGB->G = 1; MaskRGB->B = 1; MaskRGB->A = 0;
	MaskRGB->MaterialExpressionEditorX = 0;
	MaskRGB->MaterialExpressionEditorY = -200;
	Material->GetExpressionCollection().AddExpression(MaskRGB);

	UMaterialExpressionComponentMask* MaskA = NewObject<UMaterialExpressionComponentMask>(Material);
	MaskA->Material = Material;
	MaskA->Input.Expression = Custom;
	MaskA->Input.OutputIndex = 0;
	MaskA->R = 0; MaskA->G = 0; MaskA->B = 0; MaskA->A = 1;
	MaskA->MaterialExpressionEditorX = 0;
	MaskA->MaterialExpressionEditorY = 200;
	Material->GetExpressionCollection().AddExpression(MaskA);

	// --- 材质本体 ---
	Material->BlendMode = BLEND_Translucent;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = false;             // 球内看不见壳，交给引擎大气
	// 不投影**不在这里设**：UMaterial 上没有 bCastDynamicShadow / bCastStaticShadow，
	// 那两个是 UPrimitiveComponent 的属性（PrimitiveComponent.h:568/572），写上编译不过。
	// 由 Task 3 壳组件的 SetCastShadow(false) 负责 —— 投影本来就是组件的属性，不是材质的。

	UMaterialEditorOnlyData* EditorData = Material->GetEditorOnlyData();
	if (!EditorData)
	{
		OutError = TEXT("GetEditorOnlyData() 返回空");
		return false;
	}
	EditorData->EmissiveColor.Expression = MaskRGB;
	EditorData->EmissiveColor.OutputIndex = 0;
	EditorData->Opacity.Expression = MaskA;
	EditorData->Opacity.OutputIndex = 0;

	Material->PostEditChange();
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		AtmosphereAssetPath, FPackageName::GetAssetPackageExtension());

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

FAutoConsoleCommandWithWorldAndArgs GBuildAtmosphereMaterialCommand(
	TEXT("StellarEra.BuildAtmosphereMaterial"),
	TEXT("重建 /Game/Materials/M_PlanetAtmosphere。改完 HLSL 字符串后跑一次。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, UWorld* World)
		{
			FString Error;
			if (BuildPlanetAtmosphereMaterial(Error))
			{
				UE_LOG(LogTemp, Display, TEXT("[大气] 材质已生成：%s"), AtmosphereAssetPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[大气] 材质生成失败：%s"), *Error);
			}
		}));

} // namespace

#endif // WITH_EDITOR
