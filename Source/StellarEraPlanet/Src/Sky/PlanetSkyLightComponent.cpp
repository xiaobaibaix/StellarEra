#include "PlanetSkyLightComponent.h"

#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Math/Float16Color.h"
#include "PixelFormat.h"
#include "PlanetAtmosphereComponent.h"
// FTexture2DMipMap 的完整定义在 TextureResource.h；TextureCube.h 只前向声明它。
#include "TextureResource.h"
#include "VoxelPlanetActor.h"

namespace
{
	/** 方向变化阈值：1°。太阳方向与当地天顶共用。 */
	constexpr float DirectionThresholdDegrees = 1.0f;
}

UPlanetSkyLightComponent::UPlanetSkyLightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 放在帧末：相机位置这一帧已经定下来了，算出来的 L1 方向才是玩家真正看到的方向。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UPlanetSkyLightComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	// 编辑器世界也会 Tick 本组件，驱动它等于每帧把关卡标脏。
	if (!World || !World->IsGameWorld())
	{
		return;
	}

	if (!ResolveAtmosphere() || !ResolveSkyLight())
	{
		return;
	}

	FVector Observer = FVector::ZeroVector;
	if (!GetObserverLocation(Observer))
	{
		return;
	}

	FUpdateKey Key;
	if (!BuildKey(Observer, Key))
	{
		return;
	}

	if (!NeedsUpdate(Key))
	{
		return;
	}

	UpdateCube(Observer, Key);
	LastKey = MoveTemp(Key);
	bHasUpdated = true;
	LastUpdateTime = World->GetTimeSeconds();
}

void UPlanetSkyLightComponent::RefreshNow()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || !ResolveAtmosphere() || !ResolveSkyLight())
	{
		return;
	}

	FVector Observer = FVector::ZeroVector;
	if (!GetObserverLocation(Observer))
	{
		return;
	}

	FUpdateKey Key;
	if (!BuildKey(Observer, Key))
	{
		return;
	}

	UpdateCube(Observer, Key);
	LastKey = MoveTemp(Key);
	bHasUpdated = true;
	LastUpdateTime = World->GetTimeSeconds();
}

bool UPlanetSkyLightComponent::ResolveAtmosphere()
{
	if (Atmosphere.IsValid())
	{
		return true;
	}

	AActor* Owner = GetOwner();
	Atmosphere = Owner ? Owner->FindComponentByClass<UPlanetAtmosphereComponent>() : nullptr;
	if (!Atmosphere.IsValid())
	{
		// 没有大气壳就没有天空模型可积；这时候给黑图不如什么都不做，让关卡原有的立方图顶着。
		if (!bLoggedMissingAtmosphere)
		{
			bLoggedMissingAtmosphere = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[天光] %s 上找不到 UPlanetAtmosphereComponent，天光不工作。"),
				*GetNameSafe(Owner));
		}
		return false;
	}
	return true;
}

bool UPlanetSkyLightComponent::ResolveSkyLight()
{
	if (SkyLight.IsValid())
	{
		return true;
	}

	USkyLightComponent* Found = TargetSkyLight;
	if (!Found && GetWorld())
	{
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (!It->ActorHasTag(SkyLightActorTag))
			{
				continue;
			}
			Found = It->FindComponentByClass<USkyLightComponent>();
			if (Found)
			{
				break;
			}
		}
	}

	if (!Found)
	{
		if (!bLoggedMissingSkyLight)
		{
			bLoggedMissingSkyLight = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[天光] 关卡里没有打了 %s 标签的 SkyLight，天光不工作。"),
				*SkyLightActorTag.ToString());
		}
		return false;
	}

	// 三件事只在第一次做：改用指定立方图（否则 SetCubemap 进去也用不上）、
	// 关掉实时捕获（引擎会 ensure「scheduled capture 与 RealTimeCapture 互斥」）、
	// 改可动（静态组件不接受运行期立方图）。
	Found->bRealTimeCapture = false;
	Found->SourceType = SLS_SpecifiedCubemap;
	Found->SetMobility(EComponentMobility::Movable);

	SkyLight = Found;
	return true;
}

bool UPlanetSkyLightComponent::GetObserverLocation(FVector& OutLocation)
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	APlayerCameraManager* CameraManager = Controller ? Controller->PlayerCameraManager : nullptr;
	if (!CameraManager)
	{
		// 没有相机时拿行星中心当观察点算出来的方向是错的，宁可不工作。
		if (!bLoggedMissingCamera)
		{
			bLoggedMissingCamera = true;
			UE_LOG(LogTemp, Warning, TEXT("[天光] 没有玩家相机，天光不工作。"));
		}
		return false;
	}

	OutLocation = CameraManager->GetCameraLocation();
	return true;
}

bool UPlanetSkyLightComponent::BuildKey(const FVector& Observer, FUpdateKey& OutKey)
{
	UWorld* World = GetWorld();
	UPlanetAtmosphereComponent* Atmo = Atmosphere.Get();
	AVoxelPlanetActor* Planet = Atmo ? Cast<AVoxelPlanetActor>(Atmo->GetOwner()) : nullptr;
	if (!World || !Atmo || !Planet)
	{
		return false;
	}

	const float SurfaceRadiusCm = Planet->GetSurfaceRadiusCm();
	if (SurfaceRadiusCm <= 0.0f)
	{
		return false;
	}

	const FVector PlanetCenter = Atmo->GetComponentLocation();
	const FVector Offset = Observer - PlanetCenter;
	const float ShellRadiusCm =
		PlanetAtmosphere::ComputeShellRadiusCm(SurfaceRadiusCm, Atmo->AtmosphereHeightRatio);

	OutKey.Up = Offset.GetSafeNormal();
	OutKey.SurfaceRadiusCm = SurfaceRadiusCm;
	OutKey.ShellRadiusCm = ShellRadiusCm;
	// 钳到壳内：飞到深空以后「高度」每帧都在变，不钳的话节流形同虚设。
	OutKey.AltitudeCm = FMath::Clamp(Offset.Size() - SurfaceRadiusCm,
		0.0f, FMath::Max(ShellRadiusCm - SurfaceRadiusCm, 0.0f));

	UPlanetAtmosphereComponent::CollectSunsFor(World, PlanetCenter, Suns);
	OutKey.SunDirections.Reset(Suns.Num());
	for (const FPlanetSun& Sun : Suns)
	{
		OutKey.SunDirections.Add(Sun.Direction);
	}
	return true;
}

bool UPlanetSkyLightComponent::NeedsUpdate(const FUpdateKey& Key) const
{
	if (!bHasUpdated)
	{
		return true;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetTimeSeconds() - LastUpdateTime < UpdateIntervalSeconds)
	{
		return false;
	}

	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(DirectionThresholdDegrees));
	if (FVector::DotProduct(Key.Up, LastKey.Up) < CosThreshold)
	{
		return true;
	}

	if (Key.SunDirections.Num() != LastKey.SunDirections.Num())
	{
		return true;
	}
	for (int32 Index = 0; Index < Key.SunDirections.Num(); ++Index)
	{
		if (FVector::DotProduct(Key.SunDirections[Index], LastKey.SunDirections[Index])
			< CosThreshold)
		{
			return true;
		}
	}

	if (!FMath::IsNearlyEqual(Key.SurfaceRadiusCm, LastKey.SurfaceRadiusCm, 1.0f)
		|| !FMath::IsNearlyEqual(Key.ShellRadiusCm, LastKey.ShellRadiusCm, 1.0f))
	{
		return true;
	}

	// 高度阈值取瑞利标高的四分之一：比这更小的变化看不出来，却会让 5 Hz 的节流白跑。
	const float AltitudeThresholdCm = FMath::Max(
		Atmosphere->RayleighScaleHeightRatio * Key.ShellRadiusCm * 0.25f, 1.0f);
	return FMath::Abs(Key.AltitudeCm - LastKey.AltitudeCm) > AltitudeThresholdCm;
}

void UPlanetSkyLightComponent::UpdateCube(const FVector& Observer, const FUpdateKey& Key)
{
	UPlanetAtmosphereComponent* Atmo = Atmosphere.Get();
	USkyLightComponent* Target = SkyLight.Get();
	if (!Atmo || !Target)
	{
		return;
	}

	PlanetSky::FModelInput Input;
	Input.ObserverLocation = Observer;
	Input.PlanetCenter = Atmo->GetComponentLocation();
	Input.SurfaceRadiusCm = Key.SurfaceRadiusCm;
	Input.ShellRadiusCm = Key.ShellRadiusCm;
	Input.Scattering = PlanetAtmosphere::ComputeShellScatteringParams(
		Key.ShellRadiusCm, Atmo->RayleighScaleHeightRatio, Atmo->MieScaleHeightRatio);
	Input.SunIntensity = Atmo->SunIntensity;
	Input.CloudCoverage = Atmo->CloudCoverage;
	Input.SpaceSkyRadiance = SpaceSkyRadiance;
	Input.BrightnessScale = BrightnessScale;

	Input.Suns.Reset(Suns.Num());
	for (const FPlanetSun& Sun : Suns)
	{
		PlanetSky::FSun Converted;
		Converted.Direction = Sun.Direction;
		Converted.Radiance = Sun.Radiance;
		Input.Suns.Add(Converted);
	}

	const double StartSeconds = FPlatformTime::Seconds();
	PlanetSky::RenderCube(Input, CubeResolution, Pixels);

	const int32 Resolution = FMath::Max(CubeResolution, 1);
	UTextureCube* Cube = UTextureCube::CreateTransient(Resolution, Resolution, PF_FloatRGBA);
	if (!Cube)
	{
		if (!bLoggedCubemapFailure)
		{
			bLoggedCubemapFailure = true;
			UE_LOG(LogTemp, Warning, TEXT("[天光] 立方图创建失败（边长 %d），跳过本次更新。"),
				Resolution);
		}
		return;
	}

	// SRGB 必须关：存的是线性辐亮度，gamma 编码会把夜空压成 0。
	Cube->SRGB = false;

	FTexture2DMipMap& Mip = Cube->GetPlatformData()->Mips[0];
	FFloat16Color* MipData = static_cast<FFloat16Color*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	const int32 NumTexels = 6 * Resolution * Resolution;
	for (int32 Index = 0; Index < NumTexels; ++Index)
	{
		MipData[Index] = FFloat16Color(Pixels[Index]);
	}
	Mip.BulkData.Unlock();
	Cube->UpdateResource();

	// 每次新建纹理：SetCubemap 只在指针变化时才重新卷积，复用同一张会让环境光停在第一次。
	CurrentCubemap = Cube;
	Target->SetCubemap(Cube);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	UE_LOG(LogTemp, Log, TEXT("[天光] 重算 边长=%d 太阳=%d 用时=%.3f ms"),
		Resolution, Input.Suns.Num(), ElapsedMs);
}
