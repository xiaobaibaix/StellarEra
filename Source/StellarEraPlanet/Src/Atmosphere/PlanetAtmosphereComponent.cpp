#include "PlanetAtmosphereComponent.h"

#include "Components/LightComponent.h"
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

	const TCHAR* const PlanetShadowMaterialPath = TEXT("/Game/Code/Planet/Materials/M_PlanetShadowLightFunction");

	// 行星阴影光函数的参数名，必须与 PlanetShadowLightFunctionBuilder.cpp 里的一致。
	const FName ParamShadowSunDirection(TEXT("SunDirection"));
	const FName ParamShadowPlanetCenter(TEXT("PlanetCenter"));
	const FName ParamShadowPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShadowPenumbra(TEXT("Penumbra"));

	// 材质参数名。必须与 PlanetAtmosphereMaterialBuilder.cpp 里的一致。
	// 这里没有 CameraPos 与 PlanetCenter：两者都由材质节点从渲染侧取
	// （CameraPositionWS / ObjectPositionWS），不再经过 C++ / MID —— 否则慢一帧，
	// 原因见 PlanetAtmosphereMaterialBuilder.cpp 与 TickComponent 末尾的注释。
	const FName ParamPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShellRadius(TEXT("ShellRadius"));
	const FName ParamRayleighScaleHeight(TEXT("RayleighScaleHeight"));
	const FName ParamMieScaleHeight(TEXT("MieScaleHeight"));
	const FName ParamRayleighBeta(TEXT("RayleighBeta"));
	const FName ParamMieBeta(TEXT("MieBeta"));
	// 太阳槽位。数组下标即槽位号，必须与 PlanetClouds::MaxSuns 一致。
	const FName ParamSunDirections[PlanetClouds::MaxSuns] = {
		FName(TEXT("SunDirection0")), FName(TEXT("SunDirection1")),
		FName(TEXT("SunDirection2")), FName(TEXT("SunDirection3")) };
	const FName ParamSunRadiances[PlanetClouds::MaxSuns] = {
		FName(TEXT("SunRadiance0")), FName(TEXT("SunRadiance1")),
		FName(TEXT("SunRadiance2")), FName(TEXT("SunRadiance3")) };
	const FName ParamSunCount(TEXT("SunCount"));
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

	// 编辑器视图也要 Tick：PlanetCenter / 太阳参数与行星半径变化全靠它更新。
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

UMaterialInterface* UPlanetAtmosphereComponent::EnsurePlanetShadowMaterialBase()
{
	if (PlanetShadowBaseMaterial)
	{
		return PlanetShadowBaseMaterial;
	}
	// 生成光函数材质的命令在编辑器里跑，PIE 中途不会出现 —— 缺一次就别每帧再试。
	// LoadObject 失败会走包名解析，逐帧重试的代价远大于「重启 PIE 才有阴影」。
	if (bLoggedMissingPlanetShadowMaterial)
	{
		return nullptr;
	}

	PlanetShadowBaseMaterial = LoadObject<UMaterialInterface>(nullptr, PlanetShadowMaterialPath);
	if (!PlanetShadowBaseMaterial)
	{
		bLoggedMissingPlanetShadowMaterial = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[行星阴影] 找不到 %s —— 在编辑器里跑一次 StellarEra.BuildPlanetShadowLightFunction。")
			TEXT("夜面物体将被直射阳光照亮。"),
			PlanetShadowMaterialPath);
	}

	return PlanetShadowBaseMaterial;
}

UMaterialInstanceDynamic* UPlanetAtmosphereComponent::GetPlanetShadowMID(ADirectionalLight* Light)
{
	if (!PlanetShadowBaseMaterial || !IsValid(Light))
	{
		return nullptr;
	}

	const TObjectPtr<ADirectionalLight> Key(Light);
	if (const TObjectPtr<UMaterialInstanceDynamic>* Existing = PlanetShadowMIDs.Find(Key))
	{
		return *Existing;
	}

	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(PlanetShadowBaseMaterial, this);
	if (!MID)
	{
		return nullptr;
	}
	PlanetShadowMIDs.Add(Key, MID);

	return MID;
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
		// 厚度比之类的参数改了要重算缩放与整批材质参数（几何 / 太阳槽位 / 云）；
		// bGeometryApplied 先清假，让 Refresh 的边沿语义保持一致
		// （这次是显式调用，清不清都会算）。
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

	// 行星中心**不在这里写**：材质用 ObjectPositionWS 节点直接取壳自己的变换
	// （原因见 TickComponent 末尾的注释）。这里剩下的是太阳列表 —— 它不能只靠
	// TickComponent：编辑器视口不保证 tick 这个组件，只靠那里会让天空停在初值。
	const FVector PlanetCenter = GetComponentLocation();
	CollectSunsFor(GetWorld(), PlanetCenter, CachedSuns);
	ApplySuns(CachedSuns);

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

UStarBodyComponent* UPlanetAtmosphereComponent::SelectPrimaryStar(const TArray<FPlanetSun>& Suns)
{
	for (const FPlanetSun& Sun : Suns)
	{
		if (Sun.Star && Sun.Star->bDrivesDirectionalLight)
		{
			return Sun.Star;
		}
	}
	return nullptr;
}

bool UPlanetAtmosphereComponent::MakeFallbackSun(const ADirectionalLight* Light,
	const FVector& FromLocation, FPlanetSun& OutSun)
{
	OutSun = FPlanetSun();
	if (!Light)
	{
		return false;
	}

	const FVector Direction = ComputeSunDirection(nullptr, Light, FromLocation);
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	OutSun.Direction = Direction;
	OutSun.Radiance = FLinearColor::White;
	OutSun.Star = nullptr;
	return true;
}

void UPlanetAtmosphereComponent::CollectSunsFor(UWorld* World, const FVector& PlanetCenter,
	TArray<FPlanetSun>& OutSuns)
{
	OutSuns.Reset();

	TArray<UStarBodyComponent*> Stars;
	UStarBodyComponent::FindAll(World, Stars);

	// 距离升序。槽位 0 必须是最近的恒星：shader 里只有 s == 0 做云自遮挡。
	// TArray 的指针排序会把元素解引用后再交给谓词，所以谓词收引用。
	Stars.Sort([&PlanetCenter](const UStarBodyComponent& A, const UStarBodyComponent& B)
	{
		const AActor* OwnerA = A.GetOwner();
		const AActor* OwnerB = B.GetOwner();
		const FVector LocationA = OwnerA ? OwnerA->GetActorLocation() : FVector::ZeroVector;
		const FVector LocationB = OwnerB ? OwnerB->GetActorLocation() : FVector::ZeroVector;
		return FVector::DistSquared(LocationA, PlanetCenter) < FVector::DistSquared(LocationB, PlanetCenter);
	});

	static bool bLoggedTooManySuns = false;
	if (Stars.Num() > PlanetClouds::MaxSuns && !bLoggedTooManySuns)
	{
		bLoggedTooManySuns = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 关卡里有 %d 颗恒星，材质只支持 %d 颗，只取最近的 %d 颗。"),
			Stars.Num(), PlanetClouds::MaxSuns, PlanetClouds::MaxSuns);
	}

	const int32 Count = FMath::Min(Stars.Num(), PlanetClouds::MaxSuns);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UStarBodyComponent* Star = Stars[Index];
		const AActor* StarActor = Star ? Star->GetOwner() : nullptr;
		if (!StarActor)
		{
			continue;
		}

		FPlanetSun Sun;
		Sun.Direction = (StarActor->GetActorLocation() - PlanetCenter).GetSafeNormal();
		if (Sun.Direction.IsNearlyZero())
		{
			// 恒星与行星重合：没有方向可言，跳过而不是塞一个零方向。
			continue;
		}
		Sun.Radiance = Star->GetSunColor() * Star->GetRelativeBrightness();
		Sun.Star = Star;
		OutSuns.Add(Sun);
	}

	if (OutSuns.Num() > 0)
	{
		return;
	}

	// 回退：没有恒星的世界（NewMap / TestMap）沿用改动前的单一光源行为。
	if (World)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			FPlanetSun Sun;
			if (MakeFallbackSun(*It, PlanetCenter, Sun))
			{
				OutSuns.Add(Sun);
				return;
			}
		}
	}

	static bool bLoggedMissingSun = false;
	if (!bLoggedMissingSun)
	{
		bLoggedMissingSun = true;
		UE_LOG(LogTemp, Warning,
			TEXT("[大气] 关卡里既没有 UStarBodyComponent 也没有 ADirectionalLight，"
				"大气与云不会发光。"));
	}
}

void UPlanetAtmosphereComponent::ApplySuns(const TArray<FPlanetSun>& Suns)
{
	if (!AtmosphereMID)
	{
		return;
	}

	for (int32 Index = 0; Index < PlanetClouds::MaxSuns; ++Index)
	{
		const FPlanetSun* Sun = Suns.IsValidIndex(Index) ? &Suns[Index] : nullptr;
		const FVector Direction = Sun ? Sun->Direction : FVector::ForwardVector;
		const FLinearColor Radiance = Sun ? Sun->Radiance : FLinearColor::Black;

		AtmosphereMID->SetVectorParameterValue(ParamSunDirections[Index],
			FLinearColor(Direction.X, Direction.Y, Direction.Z, 0.0f));
		AtmosphereMID->SetVectorParameterValue(ParamSunRadiances[Index],
			FLinearColor(Radiance.R, Radiance.G, Radiance.B, 1.0f));
	}
	AtmosphereMID->SetScalarParameterValue(ParamSunCount, static_cast<float>(Suns.Num()));
}

void UPlanetAtmosphereComponent::DriveSunLights(const TArray<FPlanetSun>& Suns)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld())
	{
		// 编辑器视口不转灯：每帧转灯会把关卡一直标脏。
		return;
	}

	UStarBodyComponent* PrimaryStar = SelectPrimaryStar(Suns);

	const FVector PlanetCenter = GetComponentLocation();
	for (const FPlanetSun& Sun : Suns)
	{
		if (!Sun.Star || !Sun.Star->bDrivesDirectionalLight)
		{
			continue;
		}
		Sun.Star->DriveSunLight(PlanetCenter, BaseLightIntensity, Sun.Star == PrimaryStar);
	}
}

void UPlanetAtmosphereComponent::ApplyPlanetShadow(UWorld* World)
{
	// 行星本体遮挡太阳。
	//
	// 定向光的阴影图只覆盖相机附近（本关卡 DynamicShadowDistanceMovableLight = 400 m），
	// 而行星半径 20 km —— 挡住太阳的那块球体离被照点 17~20 km 远，永远进不了阴影图，
	// 于是夜面（N·L < 0 的那半个球）上空飞行的飞船仍会被直射光照亮。
	// 这里给每盏定向光挂一个解析求交的光函数材质，把「射线到球心的垂距 < 半径」的区域乘成 0。
	// 详见 PlanetShadowLightFunctionBuilder.cpp 顶部注释。
	//
	// 为什么遍历「世界里所有定向光」而不是「被驱动的恒星的那盏灯」：
	// 关卡里三颗恒星的 bDrivesDirectionalLight 是 false（灯由蓝图自己对准行星），
	// 挂在驱动分支里就永远不会执行 —— 那正是「夜面飞船被照亮」的直接原因。
	// 方向取灯自己的 forward 反向，因此不依赖恒星 → 灯的归属，也不会被
	// ResolveSunLight() 的「自动认领」顺序影响。
	if (!World || !World->IsGameWorld())
	{
		// 同 DriveSunLights：编辑器视口不挂光函数，每帧写灯属性会把关卡一直标脏。
		// 编辑器里要看阴影就跑 PIE。
		return;
	}

	UMaterialInterface* ShadowMaterial = EnsurePlanetShadowMaterialBase();
	if (!ShadowMaterial)
	{
		return;
	}

	const AVoxelPlanetActor* PlanetActor = Cast<AVoxelPlanetActor>(GetOwner());
	const float PlanetRadiusCm = PlanetActor ? PlanetActor->GetSurfaceRadiusCm() : 0.0f;
	if (PlanetRadiusCm <= 0.0f)
	{
		return;
	}
	// 半影 = 半径的 1%（≈200 m / 0.57°）：球缘给一点软边，太窄会在地平线上出锯齿硬边。
	const float PenumbraCm = PlanetRadiusCm * 0.01f;
	const FVector PlanetCenter = GetComponentLocation();

	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		ADirectionalLight* Light = *It;
		if (!IsValid(Light))
		{
			continue;
		}
		ULightComponent* LightComponent = Light->GetLightComponent();
		if (!LightComponent)
		{
			continue;
		}
		UMaterialInstanceDynamic* ShadowMID = GetPlanetShadowMID(Light);
		if (!ShadowMID)
		{
			continue;
		}

		// 定向光的 forward 是光的**传播**方向（恒星 → 行星），太阳在它的反向。
		const FVector DirectionToSun = -Light->GetActorForwardVector();
		ShadowMID->SetVectorParameterValue(ParamShadowSunDirection,
			FLinearColor(DirectionToSun.X, DirectionToSun.Y, DirectionToSun.Z, 0.0f));
		ShadowMID->SetVectorParameterValue(ParamShadowPlanetCenter,
			FLinearColor(PlanetCenter.X, PlanetCenter.Y, PlanetCenter.Z, 0.0f));
		ShadowMID->SetScalarParameterValue(ParamShadowPlanetRadius, PlanetRadiusCm);
		ShadowMID->SetScalarParameterValue(ParamShadowPenumbra, PenumbraCm);

		LightComponent->SetLightFunctionMaterial(ShadowMID);
	}
}

void UPlanetAtmosphereComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 恒星灯与大气渲染解耦：关掉大气壳 ≠ 恒星不再照亮行星。
	// 每帧重算（恒星在 N 体里会动），成本是几次 Actor 遍历 + 至多 4 盏灯的
	// 旋转 / 颜色 / 强度写入（优先级与大气太阳光走边沿触发，不逐帧写）。
	{
		const FVector LightPlanetCenter = GetComponentLocation();
		CollectSunsFor(GetWorld(), LightPlanetCenter, CachedSuns);
		DriveSunLights(CachedSuns);
		// 阴影与「驱动灯」正交：灯不由 C++ 驱动时也必须挂上，否则夜面没有遮挡。
		ApplyPlanetShadow(GetWorld());
	}

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
	// 不能每帧无条件调 —— RefreshAtmosphereGeometry 会写世界缩放和整批材质参数
	// （几何 / 太阳槽位 / 云）；
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

	// 云漂移：每帧一个标量，成本可忽略；CloudDriftPerSecond = 0 时写进去的是常数。
	AtmosphereMID->SetScalarParameterValue(ParamCloudDrift,
		GetWorld()->GetTimeSeconds() * CloudDriftPerSecond);

	// 行星中心与相机位置**都不在这里**，材质各自从渲染侧取：
	//   · 相机位置 → CameraPositionWS 视图 uniform。早先每帧写 MID 参数，但组件 Tick
	//     早于视口 Tick，写进去的相机位置总慢一帧 —— 快速移动视口时壳的边缘会被
	//     算成过亮的暖白硬边。
	//   · 行星中心 → ObjectPositionWS（绝对世界坐标）。它是同一个毛病的重灾版本：
	//     UCosmosSimulationSubsystem 是 TickableWorldSubsystem，在 UWorld::Tick 末尾
	//     才把求解器结果写回 Actor，而组件 Tick 在这之前 —— 写进 MID 的中心永远等于
	//     **上一帧**的行星位置。行星在 N 体里约 7.8 km/s，2.5 FPS 时一帧就走 3.1 km，
	//     比大气层厚度（~1.8 km）还大，于是整层大气绕着行星来回甩 —— 肉眼即"大气在抖"。
	//     改成 ObjectPositionWS 之后，着色用的中心与壳几何用的是同一份变换，与帧率无关。

	ApplySuns(CachedSuns);
}
