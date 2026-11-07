#include "PlanetAtmosphereComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/LightComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
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
	const FName ParamShadowPlanetCenterOffset(TEXT("PlanetCenterOffset"));
	const FName ParamShadowPlanetRadius(TEXT("PlanetRadius"));
	const FName ParamShadowPenumbra(TEXT("Penumbra"));

	// 材质参数名，必须与 PlanetAtmosphereMaterialBuilder.cpp 一致。CameraPos / PlanetCenter
	// 由材质从渲染侧取（CameraPositionWS / ObjectPositionWS），不经 C++ / MID，否则慢一帧。
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

	// 编辑器视图也要 Tick：行星半径与太阳参数靠它更新；引擎只在 ViewportsOnly 下
	// tick 带此标记的组件。
	bTickInEditor = true;

	// 壳纯装饰：不参与碰撞、不投影、不接收贴花。
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCastShadow(false);
	bReceivesDecals = false;

	// 负值让壳排在其他半透明后面。材质 bDisableDepthTest = true 后排序全靠这里：
	// 壳是球内天空的底，必须早于水面、矿物等半透明绘制；-1 太接近常见的 0，取 -10。
	// 直接赋属性（UPrimitiveComponent 没有 setter，只有公开成员），写成 setter 编译不过。
	TranslucencySortPriority = -10;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
		TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		SetStaticMesh(SphereMesh.Object);
	}
	else
	{
		// 必须在这里报：没有网格时下游只会报 "CreateDynamicMaterialInstance 失败"，把人引向材质。
		UE_LOG(LogTemp, Error,
			TEXT("[大气] 加载 /Engine/BasicShapes/Sphere.Sphere 失败 —— 壳没有网格，永远画不出来。"));
	}
}

void UPlanetAtmosphereComponent::OnRegister()
{
	Super::OnRegister();

	// 编辑器世界不执行 BeginPlay，而壳的材质与缩放全在 BeginPlay 里准备；不在这里补一次，
	// 编辑视图只有一个半径 50cm 的默认球。游戏世界同样走这里，故 BeginPlay 必须幂等。
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
	// 生成光函数材质的命令只在编辑器里跑；缺一次就别每帧重试（LoadObject 失败会走包名解析）。
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

	// OnRegister 已准备过一次，这里幂等；保留是为了「组件在运行时才注册」的情形。
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
		// 参数改了要重算缩放与整批材质参数；先清假让 Refresh 的边沿语义保持一致。
		bGeometryApplied = false;
		RefreshAtmosphereGeometry();
	}
}
#endif

void UPlanetAtmosphereComponent::RefreshAtmosphereGeometry()
{
	if (!bAtmosphereEnabled)
	{
		// 关掉是显式选择不是失败，不报。清掉"已应用"：重新勾上时必须重算。
		bGeometryApplied = false;
		return;
	}

	// 从这里往下都算"处理过了"，包括三条失败分支（MID 空 / Owner 不是行星 / 半径非正）；
	// 否则 TickComponent 的边沿触发会每帧重试、每帧刷同一条日志。
	bGeometryApplied = true;

	if (!AtmosphereMID)
	{
		// 本函数是 BlueprintCallable，材质未就绪时调它会到这里；静默返回调用方拿不到反馈。
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
	// 先记下来再判断：半径非正时也记，否则编辑器 Tick 每帧发现"没算过"而重入刷日志。
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

	// 网格是半径 50 cm 的球。用 SetWorldScale3D 而非相对缩放：行星可能自带缩放，会叠加。
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

	// 行星中心不在这里写：材质用 ObjectPositionWS 直接取壳自己的变换（原因见 TickComponent 末尾）。
	// 太阳列表必须在这里也算一遍 —— 编辑器视口不保证 tick 组件，只靠 Tick 会让天空停在初值。
	const FVector PlanetCenter = GetComponentLocation();
	CollectSunsFor(GetWorld(), PlanetCenter, CachedSuns);
	ApplySuns(CachedSuns);

	// ---- 云 ----
	// 先钳比例：云必须留在壳内，否则射线求交会把整层裁掉且不报错。
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
		// DirectionalLight 的 forward 是光传播方向（天上→地上），指向太阳是它的反向。
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

	// 距离升序：槽位 0 必须是最近恒星，shader 里只有 s == 0 做云自遮挡。
	// TArray 的指针排序会把元素解引用后交给谓词，所以谓词收引用。
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
	// 定向光阴影图只覆盖相机附近（本关 400 m），而挡住太阳的球体离被照点 17~20 km，
	// 永远进不了阴影图，夜面的飞船仍被直射照亮。这里给每盏定向光挂解析求交的光函数；
	// 遍历所有灯而非被驱动的恒星灯（本关恒星的 bDrivesDirectionalLight 是 false）。
	if (!World || !World->IsGameWorld())
	{
		// 同 DriveSunLights：编辑器不挂光函数，否则每帧写灯属性会一直标脏；要看阴影跑 PIE。
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

	// 光函数要的是「渲染时」的球心，这里读到的却永远是上一帧：Cosmos 子系统在
	// UWorld::Tick 末尾才写回 Actor。行星 15 km/s，实测球心偏 1.6 km 且逐帧来回跳，
	// 而半影只有 200 m —— 晨昏线于是整片翻面，就是山体阴影一闪一闪。
	// 相机与行星刚性相连，改传两者之差（同一帧相减，慢的那一帧抵消），着色器里再加回
	// 渲染侧的 CameraPositionWS。
	const APlayerController* PlayerController = World->GetFirstPlayerController();
	const APlayerCameraManager* CameraManager =
		PlayerController ? PlayerController->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		// 没有视口就没有光函数可挂，不必纠正。
		return;
	}
	const FVector PlanetCenterOffset = PlanetCenter - CameraManager->GetCameraLocation();

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
		ShadowMID->SetVectorParameterValue(ParamShadowPlanetCenterOffset,
			FLinearColor(PlanetCenterOffset.X, PlanetCenterOffset.Y, PlanetCenterOffset.Z, 0.0f));
		ShadowMID->SetScalarParameterValue(ParamShadowPlanetRadius, PlanetRadiusCm);
		ShadowMID->SetScalarParameterValue(ParamShadowPenumbra, PenumbraCm);

		LightComponent->SetLightFunctionMaterial(ShadowMID);
	}
}

void UPlanetAtmosphereComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 恒星灯与大气渲染解耦：关掉大气壳 ≠ 恒星不再照亮行星。每帧重算（恒星在 N 体里会动），
	// 优先级与大气太阳光走边沿触发、不逐帧写。
	{
		const FVector LightPlanetCenter = GetComponentLocation();
		CollectSunsFor(GetWorld(), LightPlanetCenter, CachedSuns);
		DriveSunLights(CachedSuns);
		// 阴影与「驱动灯」正交：灯不由 C++ 驱动时也必须挂上，否则夜面没有遮挡。
		ApplyPlanetShadow(GetWorld());
	}

	if (!bAtmosphereEnabled)
	{
		// 取消勾选必须当场生效：只早退不够，壳会带着上次的参数继续画。
		// 用 IsVisible() 守卫，免得每帧写一次可见性。
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

	// 重新开启的边沿：只在这里重算一次。不能每帧无条件调（Refresh 会写世界缩放与整批
	// 材质参数），也不能拿 IsVisible() 当判据（失败分支本就不可见），只能看"算过没有"。
	if (!bGeometryApplied)
	{
		RefreshAtmosphereGeometry();
	}
#if WITH_EDITOR
	else if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		// 编辑器预览：gizmo 缩放 / 改 Radius 不重跑 BeginPlay，按表面半径变化重算。
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

	// 行星中心与相机位置都不在这里写，材质各自从渲染侧取：相机 → CameraPositionWS；
	// 行星中心 → ObjectPositionWS。后者尤其关键：Cosmos 子系统在 UWorld::Tick 末尾才写回
	// Actor，组件 Tick 早于它，写进 MID 的中心永远是上一帧位置 —— 行星 7.8 km/s、2.5 FPS
	// 时一帧走 3.1 km，超过大气厚度（~1.8 km），整层大气因而每帧被甩动。

	ApplySuns(CachedSuns);
}
