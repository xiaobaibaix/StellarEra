#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "MaterialSceneTextureId.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const AtmosphereAssetPath = TEXT("/Game/Code/Planet/Materials/M_PlanetAtmosphere");

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
#define ATMO_STEPS 32
#define ATMO_PI 3.14159265358979323846
#define CLOUD_SHADOW_STEPS 3
#define CLOUD_WEATHER_OCTAVES 3
#define CLOUD_DETAIL_OCTAVES 2

float3 ro = CameraPos;
float3 rd = normalize(WorldPos - CameraPos);

// 双面材质下，同一屏幕位置的正反面各出一个片元。不剔掉多余的一面，
// 大气会被混合两遍（颜色约翻倍、透射率平方），ray march 也白跑一遍。
// 符号约定：正面 +1、背面 -1（MaterialTemplate.ush 的 GetFloatFacingSign，D3D 分支）。
// 相机在壳内就要背面，在壳外就要正面。
float sideWanted = (length(ro - PlanetCenter) < ShellRadius) ? -1.0 : 1.0;
if (TwoSidedSign * sideWanted < 0.0)
{
    return float4(0.0, 0.0, 0.0, 0.0);
}

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

// 关掉深度测试后，比壳更近的物体（飞船、水面）也会被这片片元覆盖。
// SceneDepthCm 是视空间 Z（沿相机前向的距离），除以射线与相机前向的夹角余弦
// 才是射线上的距离 —— 引擎自己也是这么换算的（MaterialTemplate.ush 里
// PixelDepthOffset 那段就是 dot(CameraPixelForwardVector, ResolvedView.ViewForward)）。
// 兜底：天空像素的深度是远平面，tScene 会大于 tExit，此时不截断。
float tScene = SceneDepthCm.r / max(dot(rd, ResolvedView.ViewForward), 0.01);
if (tScene > 0.0 && tScene < tExit)
{
    tExit = tScene;
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

// ---- 云 ----
// 3D value noise。按单位方向采样（见 CloudDensity 的 q），球面没有接缝、极点不挤。
float CloudHash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float CloudValueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = CloudHash13(i);
    float n100 = CloudHash13(i + float3(1.0, 0.0, 0.0));
    float n010 = CloudHash13(i + float3(0.0, 1.0, 0.0));
    float n110 = CloudHash13(i + float3(1.0, 1.0, 0.0));
    float n001 = CloudHash13(i + float3(0.0, 0.0, 1.0));
    float n101 = CloudHash13(i + float3(1.0, 0.0, 1.0));
    float n011 = CloudHash13(i + float3(0.0, 1.0, 1.0));
    float n111 = CloudHash13(i + float3(1.0, 1.0, 1.0));
    return lerp(
        lerp(lerp(n000, n100, f.x), lerp(n010, n110, f.x), f.y),
        lerp(lerp(n001, n101, f.x), lerp(n011, n111, f.x), f.y),
        f.z);
}

// 归一化到 [0,1]：每层振幅减半，最后除以振幅总和。
float CloudFbm(float3 p, int Octaves)
{
    float Sum = 0.0;
    float Amp = 0.5;
    float AmpSum = 0.0;
    for (int i = 0; i < Octaves; ++i)
    {
        Sum += Amp * CloudValueNoise(p);
        AmpSum += Amp;
        p *= 2.03;
        Amp *= 0.5;
    }
    return Sum / max(AmpSum, 1e-5);
}

// 云的消光系数（1/cm）。ops 是相对行星中心的向量，rLen 是它的长度（调用方已算）。
float CloudDensity(float3 ops, float rLen)
{
    float Thickness = CloudTopRadius - CloudBaseRadius;
    if (Thickness <= 0.0 || CloudExtinction <= 0.0)
    {
        return 0.0;
    }
    float h = (rLen - CloudBaseRadius) / Thickness;
    if (h <= 0.0 || h >= 1.0)
    {
        return 0.0;   // 早退：云层之外的步不付噪声代价
    }
    float profile = smoothstep(0.0, 0.15, h) * (1.0 - smoothstep(0.72, 1.0, h));

    float3 q = normalize(ops) * CloudNoiseScale;
    float w = CloudFbm(q + CloudDrift, CLOUD_WEATHER_OCTAVES);
    float d = saturate((w - (1.0 - CloudCoverage)) / max(CloudCoverage, 1e-3));
    d *= profile;
    d -= CloudDetailStrength
        * CloudFbm(q * (CloudDetailScale / max(CloudNoiseScale, 1e-3)), CLOUD_DETAIL_OCTAVES)
        * (1.0 - d);
    return saturate(d) * CloudExtinction;
}

// 双瓣 Henyey-Greenstein：前向给银边，后向让云的暗面不至于死黑。
float CloudPhase(float cosThetaCloud)
{
    float gF = 0.6;
    float gB = -0.15;
    float pF = (1.0 - gF * gF) / (4.0 * ATMO_PI * pow(max(1.0 + gF * gF - 2.0 * gF * cosThetaCloud, 1e-4), 1.5));
    float pB = (1.0 - gB * gB) / (4.0 * ATMO_PI * pow(max(1.0 + gB * gB - 2.0 * gB * cosThetaCloud, 1e-4), 1.5));
    return 0.7 * pF + 0.3 * pB;
}

float3 toSunDir = SunDirection.rgb;
float phaseC = CloudPhase(cosTheta);

// 单次散射积分：固定步长中点法
float ds = (tExit - tEnter) / float(ATMO_STEPS);
float3 transmittance = float3(1.0, 1.0, 1.0);
float3 accumulated = float3(0.0, 0.0, 0.0);
for (int i = 0; i < ATMO_STEPS; ++i)
{
    float t = tEnter + (float(i) + 0.5) * ds;
    float3 p = ro + rd * t;
    // 太阳被行星本体挡住时，这一段不产生单次散射：求 P 到"指向太阳的射线"的垂距，
    // 小于行星半径即落在本影里。必须 bS < 0 —— 否则最近点落在射线的反向延长线上。
    // 0.99~1.01 的平滑带给出晨昏线附近的过渡，避免一条硬边。
    float3 ops = p - PlanetCenter;
    float rLen = length(ops);
    float bS = dot(ops, toSunDir);
    float sunVis = 1.0;
    if (bS < 0.0)
    {
        float dPerp = length(ops - bS * toSunDir);
        sunVis = smoothstep(PlanetRadius * 0.99, PlanetRadius * 1.01, dPerp);
    }
    float h = max(rLen - PlanetRadius, 0.0);
    float dR = exp(-h / RayleighScaleHeight);
    float dM = exp(-h / MieScaleHeight);
    float dC = CloudDensity(ops, rLen);
    // 云的消光是灰度，直接加进 extinction。
    float3 extinction = RayleighBeta.rgb * dR + MieBeta.rgb * dM + float3(dC, dC, dC);
    transmittance *= exp(-extinction * ds);
    accumulated += (RayleighBeta.rgb * dR * phaseR + MieBeta.rgb * dM * phaseM) * transmittance * sunVis * ds;

    if (dC > 0.0)
    {
        // 云自遮挡：朝太阳方向再采几步，越往云顶透射越多 → 云顶亮、云底暗。
        float cloudLight = sunVis;
        float shadowStepCm = (CloudTopRadius - CloudBaseRadius) * 0.33;
        for (int j = 1; j <= CLOUD_SHADOW_STEPS; ++j)
        {
            float3 sp = p + toSunDir * (float(j) * shadowStepCm);
            float3 sops = sp - PlanetCenter;
            cloudLight *= exp(-CloudDensity(sops, length(sops)) * shadowStepCm);
        }
        accumulated += float3(CloudBrightness, CloudBrightness, CloudBrightness)
            * dC * phaseC * cloudLight * transmittance * ds;
    }
}

float coverage = saturate(1.0 - dot(transmittance, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0)));
return float4(accumulated * SunIntensity.rgb, coverage);
)");

// 下面三个 helper 带 Atmo 前缀，是为避开 unity 合并单元里的重定义。
// Star/SunMaterialBuilder.cpp 与本源文件会落进同一个 Module.StellarEraPlanet.cpp，
// 两边都在匿名命名空间里定义了同名同签名的 helper，编译报 C2084。
// 匿名命名空间在 unity blob 里其实是同一个作用域，改名是唯一不牵动对方的办法。

/** 建一个标量参数节点并登记到材质。 */
UMaterialExpressionScalarParameter* AddAtmoScalarParam(UMaterial* Material, const TCHAR* Name,
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
UMaterialExpressionVectorParameter* AddAtmoVectorParam(UMaterial* Material, const TCHAR* Name,
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
void AddAtmoCustomInput(UMaterialExpressionCustom* Custom, const TCHAR* InputName, UMaterialExpression* Source)
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
	UMaterialExpressionScalarParameter* PlanetRadius = AddAtmoScalarParam(Material, TEXT("PlanetRadius"), 75000.0f, -800, -400);
	UMaterialExpressionScalarParameter* ShellRadius = AddAtmoScalarParam(Material, TEXT("ShellRadius"), 78750.0f, -800, -300);
	UMaterialExpressionScalarParameter* RayleighScaleHeight = AddAtmoScalarParam(Material, TEXT("RayleighScaleHeight"), 1181.25f, -800, -200);
	UMaterialExpressionScalarParameter* MieScaleHeight = AddAtmoScalarParam(Material, TEXT("MieScaleHeight"), 236.25f, -800, -100);

	// 2.24169e-4 = 0.2648 / 1181.25（地球瑞利垂直光学厚度 / 壳标高）
	UMaterialExpressionVectorParameter* RayleighBeta = AddAtmoVectorParam(Material, TEXT("RayleighBeta"),
		FLinearColor(3.92938e-5f, 9.18247e-5f, 2.24169e-4f, 1.0f), -800, 0);
	// 2.25524e-5 = 0.005328 / 236.25
	UMaterialExpressionVectorParameter* MieBeta = AddAtmoVectorParam(Material, TEXT("MieBeta"),
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
	UMaterialExpressionVectorParameter* PlanetCenter = AddAtmoVectorParam(Material, TEXT("PlanetCenter"),
		FLinearColor::Black, -800, 300);
	UMaterialExpressionVectorParameter* SunDirection = AddAtmoVectorParam(Material, TEXT("SunDirection"),
		FLinearColor(1.0f, 0.0f, 0.0f, 1.0f), -800, 400);
	UMaterialExpressionVectorParameter* SunIntensity = AddAtmoVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 500);

	// ---- 云 ----
	// 初值按 21.8 km 行星的默认比例算：云底 0.02、云顶 0.07、光学厚度 6。
	// 组件会在 OnRegister / BeginPlay 里按实际行星半径覆盖。
	UMaterialExpressionScalarParameter* CloudBaseRadius =
		AddAtmoScalarParam(Material, TEXT("CloudBaseRadius"), 43600.0f, -800, 800);
	UMaterialExpressionScalarParameter* CloudTopRadius =
		AddAtmoScalarParam(Material, TEXT("CloudTopRadius"), 152600.0f, -800, 900);
	UMaterialExpressionScalarParameter* CloudCoverage =
		AddAtmoScalarParam(Material, TEXT("CloudCoverage"), 0.45f, -800, 1000);
	// 5.5e-5 /cm：云层厚 1.09e5 cm 时 τ = 6。材质要的是消光系数，不是光学厚度。
	UMaterialExpressionScalarParameter* CloudExtinction =
		AddAtmoScalarParam(Material, TEXT("CloudExtinction"), 5.5e-5f, -800, 1100);
	UMaterialExpressionScalarParameter* CloudNoiseScale =
		AddAtmoScalarParam(Material, TEXT("CloudNoiseScale"), 8.0f, -800, 1200);
	UMaterialExpressionScalarParameter* CloudDetailScale =
		AddAtmoScalarParam(Material, TEXT("CloudDetailScale"), 24.0f, -800, 1300);
	UMaterialExpressionScalarParameter* CloudDetailStrength =
		AddAtmoScalarParam(Material, TEXT("CloudDetailStrength"), 0.35f, -800, 1400);
	UMaterialExpressionScalarParameter* CloudDrift =
		AddAtmoScalarParam(Material, TEXT("CloudDrift"), 0.0f, -800, 1500);
	UMaterialExpressionScalarParameter* CloudBrightness =
		AddAtmoScalarParam(Material, TEXT("CloudBrightness"), 1.0f, -800, 1600);

	// 双面材质要靠它区分正反面（正面 +1 / 背面 -1）。
	UMaterialExpressionTwoSidedSign* TwoSidedSignNode =
		NewObject<UMaterialExpressionTwoSidedSign>(Material);
	TwoSidedSignNode->Material = Material;
	TwoSidedSignNode->MaterialExpressionEditorX = -800;
	TwoSidedSignNode->MaterialExpressionEditorY = 600;
	Material->GetExpressionCollection().AddExpression(TwoSidedSignNode);

	// 场景深度：视空间 Z，世界单位 cm。
	UMaterialExpressionSceneTexture* SceneDepthNode =
		NewObject<UMaterialExpressionSceneTexture>(Material);
	SceneDepthNode->Material = Material;
	SceneDepthNode->SceneTextureId = PPI_SceneDepth;
	SceneDepthNode->bFiltered = false;
	SceneDepthNode->MaterialExpressionEditorX = -800;
	SceneDepthNode->MaterialExpressionEditorY = 700;
	Material->GetExpressionCollection().AddExpression(SceneDepthNode);

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
	AddAtmoCustomInput(Custom, TEXT("WorldPos"), WorldPos);
	AddAtmoCustomInput(Custom, TEXT("CameraPos"), CameraPos);
	AddAtmoCustomInput(Custom, TEXT("PlanetRadius"), PlanetRadius);
	AddAtmoCustomInput(Custom, TEXT("ShellRadius"), ShellRadius);
	AddAtmoCustomInput(Custom, TEXT("PlanetCenter"), PlanetCenter);
	AddAtmoCustomInput(Custom, TEXT("SunDirection"), SunDirection);
	AddAtmoCustomInput(Custom, TEXT("RayleighScaleHeight"), RayleighScaleHeight);
	AddAtmoCustomInput(Custom, TEXT("MieScaleHeight"), MieScaleHeight);
	AddAtmoCustomInput(Custom, TEXT("RayleighBeta"), RayleighBeta);
	AddAtmoCustomInput(Custom, TEXT("MieBeta"), MieBeta);
	AddAtmoCustomInput(Custom, TEXT("SunIntensity"), SunIntensity);
	AddAtmoCustomInput(Custom, TEXT("TwoSidedSign"), TwoSidedSignNode);
	AddAtmoCustomInput(Custom, TEXT("SceneDepthCm"), SceneDepthNode);
	AddAtmoCustomInput(Custom, TEXT("CloudBaseRadius"), CloudBaseRadius);
	AddAtmoCustomInput(Custom, TEXT("CloudTopRadius"), CloudTopRadius);
	AddAtmoCustomInput(Custom, TEXT("CloudCoverage"), CloudCoverage);
	AddAtmoCustomInput(Custom, TEXT("CloudExtinction"), CloudExtinction);
	AddAtmoCustomInput(Custom, TEXT("CloudNoiseScale"), CloudNoiseScale);
	AddAtmoCustomInput(Custom, TEXT("CloudDetailScale"), CloudDetailScale);
	AddAtmoCustomInput(Custom, TEXT("CloudDetailStrength"), CloudDetailStrength);
	AddAtmoCustomInput(Custom, TEXT("CloudDrift"), CloudDrift);
	AddAtmoCustomInput(Custom, TEXT("CloudBrightness"), CloudBrightness);

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
	// 双面：球内看到的是背面，不画背面就等于天空是黑的。
	// 两面会在同一屏幕区域各出一个片元，由 HLSL 里的 TwoSidedSign 早退剔掉多余的一面
	// （见下一任务）—— 在此之前会有两次混合，这是有意的中间态。
	Material->TwoSided = true;

	// 壳在壳半径处，地形比它近。开深度测试的话，球内脚下往上看会被地形挡掉整片，
	// 只剩"透过地形的缝"。关掉之后由 HLSL 里的场景深度截断负责遮挡与空中透视。
	Material->bDisableDepthTest = true;
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
	TEXT("重建 /Game/Code/Planet/Materials/M_PlanetAtmosphere。改完 HLSL 字符串后跑一次。"),
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
