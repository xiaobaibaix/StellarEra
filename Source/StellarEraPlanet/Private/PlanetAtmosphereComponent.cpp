#include "PlanetAtmosphereComponent.h"

// APlayerCameraManager 不在 GameFramework/ 下，在 Camera/ 下
// （Engine/Classes/Camera/PlayerCameraManager.h 是引擎里唯一一份）。
// 写成 GameFramework/PlayerCameraManager.h 编译不过（C1083）。
#include "Camera/PlayerCameraManager.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetAtmosphereParams.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelPlanetActor.h"

namespace
{
	/** /Engine/BasicShapes/Sphere 的半径是 50 cm。 */
	constexpr float BasicSphereRadiusCm = 50.0f;

	const TCHAR* const AtmosphereMaterialPath = TEXT("/Game/Materials/M_PlanetAtmosphere");

	// 材质参数名。必须与 PlanetAtmosphereMaterialBuilder.cpp 里的一致。
	const FName ParamPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShellRadius(TEXT("ShellRadius"));
	const FName ParamRayleighScaleHeight(TEXT("RayleighScaleHeight"));
	const FName ParamMieScaleHeight(TEXT("MieScaleHeight"));
	const FName ParamRayleighBeta(TEXT("RayleighBeta"));
	const FName ParamMieBeta(TEXT("MieBeta"));
	const FName ParamCameraPos(TEXT("CameraPos"));
	const FName ParamPlanetCenter(TEXT("PlanetCenter"));
	const FName ParamSunDirection(TEXT("SunDirection"));
	const FName ParamSunIntensity(TEXT("SunIntensity"));
}

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// 壳纯装饰：不参与碰撞、不投影、不接收贴花。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bReceivesDecals = false;

	// 负值让壳排在其他半透明的**后面**（深度上靠后）。
	// 引擎原话：lower sort priority draws behind objects with a higher priority
	// （PrimitiveComponent.h:883-884）。
	// 别用"之前画 / 之后画"来描述 —— 那说的是绘制先后，与"前后"正好相反，已经写反过一次。
	// 直接赋属性：UPrimitiveComponent 上**没有** SetTranslucencySortPriority 这个函数，
	// 只有公开成员 TranslucencySortPriority（PrimitiveComponent.h:892），写成 setter 编译不过。
	TranslucencySortPriority = -1;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		SetStaticMesh(SphereMesh.Object);
	}
	else
	{
		// 必须在这里报：没有网格的壳是空的，而下游唯一的症状是
		// "CreateDynamicMaterialInstance 失败"，那会把人引到材质上去。
		UE_LOG(LogTemp, Error,
			TEXT("[大气] 加载 /Engine/BasicShapes/Sphere.Sphere 失败 —— 壳没有网格，永远画不出来。"));
	}
}

void UPlanetAtmosphereComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bAtmosphereEnabled)
	{
		SetVisibility(false);
		return;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, AtmosphereMaterialPath);
	if (!BaseMaterial)
	{
		// 材质是 Task 2 用控制台命令生成的资产。没生成过就在这里报一次，不崩。
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 找不到 %s —— 在编辑器里跑一次 StellarEra.BuildAtmosphereMaterial。壳将不可见。"),
			AtmosphereMaterialPath);
		SetVisibility(false);
		return;
	}

	AtmosphereMID = CreateDynamicMaterialInstance(0, BaseMaterial);
	if (!AtmosphereMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[大气] CreateDynamicMaterialInstance 失败，壳将不可见。"));
		SetVisibility(false);
		return;
	}

	RefreshAtmosphereGeometry();
}

void UPlanetAtmosphereComponent::RefreshAtmosphereGeometry()
{
	if (!bAtmosphereEnabled)
	{
		// 关掉是用户的显式选择，不是失败，不报。
		return;
	}

	if (!AtmosphereMID)
	{
		// 本函数是 BlueprintCallable，BeginPlay 之前调它会走到这里。
		// 静默返回的话调用方拿不到任何反馈，只能看着壳没反应。
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] RefreshAtmosphereGeometry 在 BeginPlay 之前被调用（AtmosphereMID 还是空），本次不做任何事。"));
		return;
	}

	const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(GetOwner());
	if (!Planet)
	{
		UE_LOG(LogTemp, Warning, TEXT("[大气] 壳组件的 Owner 不是 AVoxelPlanetActor，跳过。"));
		SetVisibility(false);
		return;
	}

	const float SurfaceRadiusCm = Planet->GetSurfaceRadiusCm();
	if (SurfaceRadiusCm <= 0.0f)
	{
		SetVisibility(false);
		return;
	}

	const float ShellRadiusCm = PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, AtmosphereHeightRatio);
	const PlanetAtmosphere::FShellScatteringParams Scattering =
		PlanetAtmosphere::ComputeShellScatteringParams(
			ShellRadiusCm, RayleighScaleHeightRatio, MieScaleHeightRatio);

	// 网格是半径 50 cm 的球。用 SetWorldScale3D 而不是相对缩放：
	// 行星 actor 自己可能带缩放，相对缩放会把它叠进来。
	SetWorldScale3D(FVector(ShellRadiusCm / BasicSphereRadiusCm));

	AtmosphereMID->SetScalarParameterValue(ParamPlanetRadius, SurfaceRadiusCm);
	AtmosphereMID->SetScalarParameterValue(ParamShellRadius, ShellRadiusCm);
	AtmosphereMID->SetScalarParameterValue(ParamRayleighScaleHeight, Scattering.RayleighScaleHeightCm);
	AtmosphereMID->SetScalarParameterValue(ParamMieScaleHeight, Scattering.MieScaleHeightCm);
	AtmosphereMID->SetVectorParameterValue(ParamRayleighBeta,
		FLinearColor(Scattering.RayleighBeta.X, Scattering.RayleighBeta.Y, Scattering.RayleighBeta.Z, 1.0f));
	AtmosphereMID->SetVectorParameterValue(ParamMieBeta,
		FLinearColor(Scattering.MieBeta.X, Scattering.MieBeta.Y, Scattering.MieBeta.Z, 1.0f));
	AtmosphereMID->SetVectorParameterValue(ParamSunIntensity, SunIntensity);

	SetVisibility(true);
}

FVector UPlanetAtmosphereComponent::ResolveSunDirection()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	// 缓存的灯没了（关卡重载、actor 被删）就重新找。
	if (!IsValid(CachedSunLight))
	{
		CachedSunLight = nullptr;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			CachedSunLight = *It;
			break;
		}
	}

	if (!CachedSunLight)
	{
		if (!bLoggedMissingSun)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里没有 ADirectionalLight，壳的太阳方向无从确定，明暗分界会停在初值。"));
			bLoggedMissingSun = true;
		}
		return FVector::ZeroVector;
	}

	// DirectionalLight 的 forward 是光**传播**的方向（天上→地上）。
	// 从行星指向太阳是它的反向。
	return -CachedSunLight->GetActorForwardVector();
}

void UPlanetAtmosphereComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bAtmosphereEnabled)
	{
		// 面板里取消勾选必须当场生效：只早退不够 —— 壳会带着上一次的参数继续画，
		// Task 5 就是靠勾掉这一项来对比 stat unit 的，那样量到的是"什么都没变"。
		// 用 IsVisible() 守卫，免得每帧都写一次可见性。
		if (IsVisible())
		{
			SetVisibility(false);
		}
		return;
	}

	if (!AtmosphereMID)
	{
		return;
	}

	// 行星中心就是组件自己的世界位置（相对位置是零）。
	const FVector PlanetCenter = GetComponentLocation();
	AtmosphereMID->SetVectorParameterValue(ParamPlanetCenter,
		FLinearColor(PlanetCenter.X, PlanetCenter.Y, PlanetCenter.Z, 1.0f));

	// 相机位置。星图模式下 Possess 的是 AStarMapCameraPawn，GetCameraLocation 一样能取到。
	FVector CameraPos = LastCameraPos;
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CamMgr = PC->PlayerCameraManager)
			{
				CameraPos = CamMgr->GetCameraLocation();
			}
		}
	}
	if (!CameraPos.Equals(LastCameraPos, 1.0f))
	{
		LastCameraPos = CameraPos;
		AtmosphereMID->SetVectorParameterValue(ParamCameraPos,
			FLinearColor(CameraPos.X, CameraPos.Y, CameraPos.Z, 1.0f));
	}

	const FVector SunDirection = ResolveSunDirection();
	if (!SunDirection.IsNearlyZero() && !SunDirection.Equals(LastSunDirection, 1e-4f))
	{
		LastSunDirection = SunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(SunDirection.X, SunDirection.Y, SunDirection.Z, 0.0f));
	}
}
