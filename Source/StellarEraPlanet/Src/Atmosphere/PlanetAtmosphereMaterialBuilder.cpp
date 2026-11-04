#include "CoreMinimal.h"

#if WITH_EDITOR

#include "HAL/IConsoleManager.h"
#include "MaterialSceneTextureId.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCameraPositionWS.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionObjectPositionWS.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionSceneTexture.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Misc/PackageName.h"
#include "PlanetCloudParams.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{

const TCHAR* const AtmosphereAssetPath = TEXT("/Game/Code/Planet/Materials/M_PlanetAtmosphere");

// ---------------------------------------------------------------------------
// 壳的单次散射。
//
// **云必须靠 IncludeFilePaths 引 .ush，不能写在本字符串里**：本字符串会被注入到
// `CustomExpressionN(...)` 的**函数体**内（HLSLMaterialTranslator.cpp:14338），
// 而函数体内再定义函数是编译错误 —— "function definition is not allowed here"。
// 噪声 / 密度 / 相位函数，以及整个积分循环，定义在 /Project/PlanetClouds.ush，
// 由引擎在 CustomExpressionN 的函数定义**之前**插一行 #include（同文件 14327-14336），
// 因此它们展开在全局作用域。代价是它们看不见 Custom 节点的输入变量，
// 每个依赖量都得显式传参 —— 见下面的 CloudAtmosphereRaymarch 调用。
//
// 积分循环之所以也搬进 .ush，是因为它必须**按区间分段**跑：整条壳弦均匀
// 32 步时，云层那段常常只落进一两个采样点，一次 exp(-dC*ds) 就把透过率
// 按死、而发射项还没积分起来，画面上就是硬边的黑楔形。
// 虚拟目录 /Project 是引擎自带的：启动时映射到 <项目>/Shaders
// （LaunchEngineLoop.cpp:2553-2558，仅当该目录存在）。所以磁盘上必须是
// <项目>/Shaders/PlanetClouds.ush，而虚拟路径里**不再重复** "Shaders"。
//
// 不调用引擎的散射函数（那些要 LUT 或全局 Atmosphere uniform，材质里拿不到）。
//
// 坐标空间（已从引擎源码钉死，见计划「与规格的偏差」第 4 条）：
//   WorldPos / CameraPos / PlanetCenter 全是**绝对世界坐标（cm）**。
//   WorldPosition 节点 WPT_Default 的标题字面就是 "Absolute World Position"；
//   CameraPos 是视图 uniform（CameraPositionWS），同样在绝对世界坐标下；
//   PlanetCenter 是 ObjectPositionWS（OriginType = Absolute），按图元变换取值。
//   三者都从渲染侧取，所以同一帧内自洽，也与壳几何同源。
//
// 注意：因为函数体里有 return，UE 不会再自动包一层 return，末尾必须自己 return。
// ---------------------------------------------------------------------------
const TCHAR* const AtmosphereHLSL = TEXT(R"(
// 改 Shaders/PlanetClouds.ush 之后必须把这个数字 +1，否则画面不会有任何变化。
// 材质资产的 shader map id 只吃 Custom 节点的 Code 与 IncludeFilePaths 两项，
// .ush 的内容不进哈希（实测：改完 .ush 重跑 BuildAtmosphereMaterial、
// 再 RecompileMaterial，连亮度参数 1→4 都逐像素相同，说明拿到的还是旧编译产物）。
// 这个常量本身不参与运算，只用来改 Code 字符串 → 改哈希 → 强制重编译。
#define PLANET_CLOUDS_VERSION 9

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

// 相位与积分全部在 CloudAtmosphereRaymarch 里（/Project/PlanetClouds.ush）。
// 上面求出来的 tEnter / tExit 已经是"撞地或撞场景深度"之后的区间，
// 函数内部再按云层切成三段有序积分，所以这里只负责一件事：把输入摊平成参数表。
float4 march = CloudAtmosphereRaymarch(
    ro, rd, PlanetCenter,
    SunDirection0.rgb, SunDirection1.rgb, SunDirection2.rgb, SunDirection3.rgb,
    SunRadiance0.rgb, SunRadiance1.rgb, SunRadiance2.rgb, SunRadiance3.rgb,
    SunCount,
    tEnter, tExit, PlanetRadius,
    RayleighScaleHeight, MieScaleHeight,
    RayleighBeta.rgb, MieBeta.rgb,
    CloudBaseRadius, CloudTopRadius, CloudExtinction,
    CloudNoiseScale, CloudDetailScale, CloudCoverage,
    CloudDetailStrength, CloudDrift, CloudBrightness);

return float4(march.rgb * SunIntensity.rgb, march.a);
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
	// 行星中心取壳自己的变换（绝对世界坐标），**不是** MID 参数。
	//
	// 这里踩过和 CameraPos 一模一样的坑，但后果严重得多：C++ 每帧往 MID 写
	// GetComponentLocation()，而 UCosmosSimulationSubsystem 是 TickableWorldSubsystem，
	// 在 UWorld::Tick 末尾才把求解器结果写回 Actor —— 组件 Tick 在它之前，
	// 于是写进 MID 的中心永远等于**上一帧**的行星位置。行星在 N 体里约 7.8 km/s，
	// 2.5 FPS 时一帧就走 3.1 km，超过大气层厚度（~1.8 km），整层大气每帧绕着
	// 行星甩一次，肉眼就是"大气在抖"。
	// ObjectPositionWS 来自本次绘制用的图元变换，与壳几何完全同源，与帧率无关。
	// OriginType 显式钉成 Absolute（枚举零值，但别依赖默认值）：
	// HLSL 里 WorldPos / CameraPos 都是绝对世界坐标，三者必须同一空间。
	UMaterialExpressionObjectPositionWS* PlanetCenter = NewObject<UMaterialExpressionObjectPositionWS>(Material);
	PlanetCenter->Material = Material;
	PlanetCenter->OriginType = EPositionOrigin::Absolute;
	PlanetCenter->MaterialExpressionEditorX = -800;
	PlanetCenter->MaterialExpressionEditorY = 300;
	Material->GetExpressionCollection().AddExpression(PlanetCenter);
	// 太阳槽位 0..MaxSuns-1。0 号默认白（无恒星时的回退太阳就是这个值），其余默认黑。
	UMaterialExpressionVectorParameter* SunDirection[PlanetClouds::MaxSuns];
	UMaterialExpressionVectorParameter* SunRadiance[PlanetClouds::MaxSuns];
	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		const FString DirectionName = FString::Printf(TEXT("SunDirection%d"), Index);
		SunDirection[Index] = AddAtmoVectorParam(Material, *DirectionName,
			FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), -800, 400 + Index * 40);

		const FString RadianceName = FString::Printf(TEXT("SunRadiance%d"), Index);
		SunRadiance[Index] = AddAtmoVectorParam(Material, *RadianceName,
			Index == 0 ? FLinearColor::White : FLinearColor::Black, -800, 600 + Index * 40);
	}
	UMaterialExpressionScalarParameter* SunCount =
		AddAtmoScalarParam(Material, TEXT("SunCount"), 1.0f, -800, 800);
	UMaterialExpressionVectorParameter* SunIntensity = AddAtmoVectorParam(Material, TEXT("SunIntensity"),
		FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), -800, 900);

	// ---- 云 ----
	// 初值按 20 km 行星（表面半径 2e6 cm）的默认比例算：云底 0.02、云顶 0.07、
	// 光学厚度 6。半径以行星中心为原点，所以是 r × (1 + 比例)，不是 r × 比例。
	// 组件会在 OnRegister / BeginPlay 里按实际行星半径覆盖。
	UMaterialExpressionScalarParameter* CloudBaseRadius =
		AddAtmoScalarParam(Material, TEXT("CloudBaseRadius"), 2040000.0f, -800, 800);
	UMaterialExpressionScalarParameter* CloudTopRadius =
		AddAtmoScalarParam(Material, TEXT("CloudTopRadius"), 2140000.0f, -800, 900);
	UMaterialExpressionScalarParameter* CloudCoverage =
		AddAtmoScalarParam(Material, TEXT("CloudCoverage"), 0.45f, -800, 1000);
	// 6e-5 /cm：云层厚 1e5 cm 时 τ = 6。材质要的是消光系数，不是光学厚度。
	UMaterialExpressionScalarParameter* CloudExtinction =
		AddAtmoScalarParam(Material, TEXT("CloudExtinction"), 6e-5f, -800, 1100);
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
	// 引擎会把每项渲染成一行 #include，插在 CustomExpressionN 的函数定义**之前**
	// （HLSLMaterialTranslator.cpp:14327-14338），所以 .ush 里的函数在全局作用域，
	// Code 里的调用点能看见它们。路径必须是已映射的虚拟目录，否则
	// MaterialCachedData::Validate 会把这一项悄悄删掉（只留一条 Warning）。
	Custom->IncludeFilePaths.Add(TEXT("/Project/PlanetClouds.ush"));
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
	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		AddAtmoCustomInput(Custom, *FString::Printf(TEXT("SunDirection%d"), Index), SunDirection[Index]);
		AddAtmoCustomInput(Custom, *FString::Printf(TEXT("SunRadiance%d"), Index), SunRadiance[Index]);
	}
	AddAtmoCustomInput(Custom, TEXT("SunCount"), SunCount);
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
