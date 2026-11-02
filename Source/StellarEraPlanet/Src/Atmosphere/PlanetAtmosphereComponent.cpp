#include "PlanetAtmosphereComponent.h"

#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetAtmosphereParams.h"
#include "PlanetCloudParams.h"
#include "StarBodyComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelPlanetActor.h"

namespace
{
	/** /Engine/BasicShapes/Sphere 的半径是 50 cm。 */
	constexpr float BasicSphereRadiusCm = 50.0f;

	const TCHAR* const AtmosphereMaterialPath = TEXT("/Game/Code/Planet/Materials/M_PlanetAtmosphere");

	// 材质参数名。必须与 PlanetAtmosphereMaterialBuilder.cpp 里的一致。
	// 这里没有 CameraPos：相机位置由材质的 CameraPositionWS 节点从视图 uniform 取，
	// 不再经过 C++ / MID（原因见 PlanetAtmosphereMaterialBuilder.cpp 里的注释）。
	const FName ParamPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShellRadius(TEXT("ShellRadius"));
	const FName ParamRayleighScaleHeight(TEXT("RayleighScaleHeight"));
	const FName ParamMieScaleHeight(TEXT("MieScaleHeight"));
	const FName ParamRayleighBeta(TEXT("RayleighBeta"));
	const FName ParamMieBeta(TEXT("MieBeta"));
	const FName ParamPlanetCenter(TEXT("PlanetCenter"));
	const FName ParamSunDirection(TEXT("SunDirection"));
	const FName ParamSunIntensity(TEXT("SunIntensity"));
	const FName ParamCloudBaseRadius(TEXT("CloudBaseRadius"));
	const FName ParamCloudTopRadius(TEXT("CloudTopRadius"));
	const FName ParamCloudCoverage(TEXT("CloudCoverage"));
	const FName ParamCloudExtinction(TEXT("CloudExtinction"));
	const FName ParamCloudNoiseScale(TEXT("CloudNoiseScale"));
	const FName ParamCloudDetailScale(TEXT("CloudDetailScale"));
	const FName ParamCloudDetailStrength(TEXT("CloudDetailStrength"));
	const FName ParamCloudDrift(TEXT("CloudDrift"));
	const FName ParamCloudBrightness(TEXT("CloudBrightness"));
}

UPlanetAtmosphereComponent::UPlanetAtmosphereComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// 编辑器视图也要 Tick：PlanetCenter / SunDirection 与行星半径变化全靠它更新。
	// 引擎只在 LEVELTICK_ViewportsOnly 下 tick 带这个标记的组件
	// （Actor.h 的 FActorComponentTickFunction::ExecuteTickHelper）。
	bTickInEditor = true;

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
	// -10 而不是 -1：材质现在 bDisableDepthTest = true，深度测试不再替我们排序，
	// 绘制顺序完全由这里决定。壳是球内天空的"底"，必须排在水面、矿物等半透明之前
	// （即 priority 更低）。-1 与常见的 0 太接近，容易被别的半透明抢在前面。
	TranslucencySortPriority = -10;

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

void UPlanetAtmosphereComponent::OnRegister()
{
	Super::OnRegister();

	// 编辑器世界不执行 BeginPlay，而壳的材质与缩放全在 BeginPlay 里准备。
	// 不在这里补一次，编辑视图里就只有一个半径 50cm 的默认球，埋进行星中心。
	// 游戏世界同样走这条路径，因此 BeginPlay 的后续调用必须是幂等的。
	EnsureAtmosphereMaterial();

	if (bAtmosphereEnabled)
	{
		RefreshAtmosphereGeometry();
	}
}

bool UPlanetAtmosphereComponent::EnsureAtmosphereMaterial()
{
	if (AtmosphereMID)
	{
		return true;
	}

	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr, AtmosphereMaterialPath);
	if (!BaseMaterial)
	{
		// 材质是 Task 2 用控制台命令生成的资产。没生成过就在这里报一次，不崩。
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 找不到 %s —— 在编辑器里跑一次 StellarEra.BuildAtmosphereMaterial。壳将不可见。"),
			AtmosphereMaterialPath);
		SetVisibility(false);
		return false;
	}

	AtmosphereMID = CreateDynamicMaterialInstance(0, BaseMaterial);
	if (!AtmosphereMID)
	{
		UE_LOG(LogTemp, Warning, TEXT("[大气] CreateDynamicMaterialInstance 失败，壳将不可见。"));
		SetVisibility(false);
		return false;
	}

	return true;
}

void UPlanetAtmosphereComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bAtmosphereEnabled)
	{
		SetVisibility(false);
		return;
	}

	// OnRegister 已经准备过一次；这里幂等。保留这一步是为了「组件在运行时才
	// 注册」的情形，也保证 BeginPlay 之后一定按当前启用状态完整算过一遍。
	if (EnsureAtmosphereMaterial())
	{
		RefreshAtmosphereGeometry();
	}
}

#if WITH_EDITOR
void UPlanetAtmosphereComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (!GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}

	if (!bAtmosphereEnabled)
	{
		SetVisibility(false);
		bGeometryApplied = false;
		return;
	}

	if (EnsureAtmosphereMaterial())
	{
		// 厚度比之类的参数改了要重算缩放与 7 个材质参数；bGeometryApplied 先清假，
		// 让 Refresh 的边沿语义保持一致（这次是显式调用，清不清都会算）。
		bGeometryApplied = false;
		RefreshAtmosphereGeometry();
	}
}
#endif

void UPlanetAtmosphereComponent::RefreshAtmosphereGeometry()
{
	if (!bAtmosphereEnabled)
	{
		// 关掉是用户的显式选择，不是失败，不报。
		// 同时清掉"已应用"：重新勾上时必须重算一次，否则壳不会回来。
		bGeometryApplied = false;
		return;
	}

	// 从这里往下就算"处理过了"，包括下面三条失败分支（MID 空 / Owner 不是行星 /
	// 半径非正）。它们都合法地以"不可见"收场，若在这里不置真，TickComponent 的
	// 边沿触发会每帧重试，每帧刷同一条日志。
	bGeometryApplied = true;

	if (!AtmosphereMID)
	{
		// 本函数是 BlueprintCallable，材质尚未就绪时调它会走到这里
		// （OnRegister 里 EnsureAtmosphereMaterial 失败、还没到 BeginPlay 等）。
		// 静默返回的话调用方拿不到任何反馈，只能看着壳没反应。
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] RefreshAtmosphereGeometry 在材质就绪之前被调用（AtmosphereMID 还是空），本次不做任何事。"));
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
	// 先记下来再判断：半径非正时也要记，否则编辑器 Tick 会每帧发现"没算过"
	// 而反复重入，把同一条警告刷满日志。
	AppliedSurfaceRadiusCm = SurfaceRadiusCm;
	if (SurfaceRadiusCm <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 行星表面半径 %.3f cm 非正，壳没有可套的球面，跳过。"), SurfaceRadiusCm);
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

	// 行星中心与太阳方向必须在这里写一次，不能只靠 TickComponent。
	// 编辑器视口不保证 tick 这个组件（实测：组件只挂了个 bTickInEditor，
	// 行星摆在 y = 1e8 时 PlanetCenter 仍停在 (0,0,0)、CloudDrift 停在 0）。
	// 材质整条射线以 PlanetCenter 为原点，原点错到行星之外时球面求交全部落空，
	// 大气与云整层不画、**且不报任何错** —— 表现为"壳明明在、就是看不见"。
	// 几何重算是唯一必然发生的时机（OnRegister / BeginPlay / 半径变化都走这里）。
	const FVector PlanetCenter = GetComponentLocation();
	AtmosphereMID->SetVectorParameterValue(ParamPlanetCenter,
		FLinearColor(PlanetCenter.X, PlanetCenter.Y, PlanetCenter.Z, 1.0f));

	const FVector InitialSunDirection = ResolveSunDirection();
	if (!InitialSunDirection.IsNearlyZero())
	{
		LastSunDirection = InitialSunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(InitialSunDirection.X, InitialSunDirection.Y, InitialSunDirection.Z, 0.0f));
	}

	// ---- 云 ----
	// 先钳比例：云必须留在壳内，否则射线求交会把整层裁掉，而且不会有任何报错。
	const float ClampedTopRatio = FMath::Min(CloudTopRatio, AtmosphereHeightRatio);
	const float ClampedBaseRatio = FMath::Min(CloudBaseRatio, ClampedTopRatio);
	const PlanetClouds::FCloudLayerRadii CloudRadii =
		PlanetClouds::ComputeCloudLayerRadiiCm(SurfaceRadiusCm, ClampedBaseRatio, ClampedTopRatio);

	const float CloudExtinctionPerCm =
		(bCloudsEnabled && CloudOpticalDepth > 0.0f && CloudRadii.TopCm > CloudRadii.BaseCm)
			? CloudOpticalDepth / (CloudRadii.TopCm - CloudRadii.BaseCm)
			: 0.0f;

	AtmosphereMID->SetScalarParameterValue(ParamCloudBaseRadius, CloudRadii.BaseCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudTopRadius, CloudRadii.TopCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudCoverage, FMath::Clamp(CloudCoverage, 0.0f, 1.0f));
	AtmosphereMID->SetScalarParameterValue(ParamCloudExtinction, CloudExtinctionPerCm);
	AtmosphereMID->SetScalarParameterValue(ParamCloudNoiseScale, CloudNoiseScale);
	AtmosphereMID->SetScalarParameterValue(ParamCloudDetailScale, CloudDetailScale);
	AtmosphereMID->SetScalarParameterValue(ParamCloudDetailStrength, CloudDetailStrength);
	AtmosphereMID->SetScalarParameterValue(ParamCloudBrightness, CloudBrightness);

	SetVisibility(true);
}

FVector UPlanetAtmosphereComponent::ComputeSunDirection(const AActor* StarActor,
	const ADirectionalLight* FallbackLight, const FVector& FromLocation)
{
	if (StarActor)
	{
		// 恒星在 N 体里会动（位置每帧被写回），所以方向每帧现算，不缓存。
		return (StarActor->GetActorLocation() - FromLocation).GetSafeNormal();
	}

	if (FallbackLight)
	{
		// DirectionalLight 的 forward 是光**传播**的方向（天上→地上）。
		// 从行星指向太阳是它的反向。
		return -FallbackLight->GetActorForwardVector();
	}

	return FVector::ZeroVector;
}

FVector UPlanetAtmosphereComponent::ResolveSunDirection()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	// 恒星优先。每帧完整遍历：关卡里天体是个位数，成本可忽略，
	// 换来的是恒星移动、增删时不需要任何失效通知。
	UStarBodyComponent* Star = UStarBodyComponent::FindNearest(World, GetComponentLocation());

	// 缓存的灯没了（关卡重载、actor 被删）就重新找。
	if (!IsValid(CachedSunLight))
	{
		CachedSunLight = nullptr;
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			CachedSunLight = *It;
			break;
		}

		// 关卡里这盏灯是 Static（摆放时没人会想到它要动）。但它的朝向由恒星驱动、
		// 每帧都要转，Static 组件会拒绝 SetActorRotation 并逐帧刷
		// "Mobility must be 'Movable'"。这里在找到它的当口切一次。
		//
		// 只在游戏世界切：编辑器世界里改组件移动性会把关卡一直标脏。编辑器态本来
		// 也不转灯（见下方 IsGameWorld 守卫），所以不会退回到静态光。
		if (CachedSunLight && World->IsGameWorld())
		{
			if (USceneComponent* LightRoot = CachedSunLight->GetRootComponent())
			{
				if (LightRoot->Mobility != EComponentMobility::Movable)
				{
					LightRoot->SetMobility(EComponentMobility::Movable);
				}
			}
		}
	}

	const AActor* StarActor = Star ? Star->GetOwner() : nullptr;
	if (!StarActor && !CachedSunLight)
	{
		if (!bLoggedMissingSun)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[大气] 关卡里既没有 UStarBodyComponent 也没有 ADirectionalLight，"
					"壳的太阳方向无从确定，明暗分界会停在初值。"));
			bLoggedMissingSun = true;
		}
		return FVector::ZeroVector;
	}

	const FVector SunDirection = ComputeSunDirection(StarActor, CachedSunLight, GetComponentLocation());

	// 主光同步：只在游戏世界里转灯 —— 编辑器世界里每帧转灯会把关卡一直标脏。
	// 多恒星时只有最近那颗生效（见 UStarBodyComponent::bDrivesDirectionalLight 的说明）。
	if (Star && Star->bDrivesDirectionalLight && CachedSunLight && World->IsGameWorld())
	{
		CachedSunLight->SetActorRotation((-SunDirection).Rotation());
	}

	return SunDirection;
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
		// 清掉"已应用"：重新勾上时要再算一遍，否则壳会一直不可见。
		bGeometryApplied = false;
		return;
	}

	if (!AtmosphereMID)
	{
		return;
	}

	// 重新开启的边沿：只在这里重算一次。
	// 不能每帧无条件调 —— RefreshAtmosphereGeometry 会写世界缩放和 7 个材质参数；
	// 也不能拿 IsVisible() 当判据 —— 它的三条失败分支本来就以"不可见"收场，
	// 那样会每帧重试、每帧刷日志。判据只能是"这个启用状态算过没有"。
	if (!bGeometryApplied)
	{
		RefreshAtmosphereGeometry();
	}
#if WITH_EDITOR
	else if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		// 编辑器预览：gizmo 缩放 / 改 Radius 都不会重跑 BeginPlay，也没有可靠的
		// OnConstruction 时序。按"表面半径变没变"重算一次，不变就不动。
		if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(GetOwner()))
		{
			if (!FMath::IsNearlyEqual(Planet->GetSurfaceRadiusCm(), AppliedSurfaceRadiusCm))
			{
				RefreshAtmosphereGeometry();
			}
		}
	}
#endif

	// 行星中心就是组件自己的世界位置（相对位置是零）。
	const FVector PlanetCenter = GetComponentLocation();
	AtmosphereMID->SetVectorParameterValue(ParamPlanetCenter,
		FLinearColor(PlanetCenter.X, PlanetCenter.Y, PlanetCenter.Z, 1.0f));

	// 云漂移：每帧一个标量，成本可忽略；CloudDriftPerSecond = 0 时写进去的是常数。
	AtmosphereMID->SetScalarParameterValue(ParamCloudDrift,
		GetWorld()->GetTimeSeconds() * CloudDriftPerSecond);

	// 相机位置**不在这里**：材质用 CameraPositionWS 视图 uniform 自己取。
	// 早先这里每帧写 MID 参数，但编辑器里组件 Tick 早于视口 Tick，写进去的相机位置
	// 总是慢一帧 —— 快速移动视口时壳的边缘会被算成过亮的暖白硬边。

	const FVector SunDirection = ResolveSunDirection();
	if (!SunDirection.IsNearlyZero() && !SunDirection.Equals(LastSunDirection, 1e-4f))
	{
		LastSunDirection = SunDirection;
		AtmosphereMID->SetVectorParameterValue(ParamSunDirection,
			FLinearColor(SunDirection.X, SunDirection.Y, SunDirection.Z, 0.0f));
	}
}
