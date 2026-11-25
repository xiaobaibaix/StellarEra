#include "PlanetSkyLightComponent.h"

#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Math/Float16Color.h"
#include "PixelFormat.h"
#include "PlanetAtmosphereComponent.h"
// FTexture2DMipMap 的完整定义在 TextureResource.h；TextureCube.h 只前向声明它。
#include "TextureResource.h"
#include "VoxelPlanetActor.h"
// OnObjectPreSave 的声明在 UObjectGlobals.h，FObjectPreSaveContext 在 ObjectSaveContext.h。
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#endif

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
	// 编辑器世界也要 Tick：不标的话编辑预览永远停在关卡里那张静态立方图，与 PIE 亮度对不上。
	bTickInEditor = true;
}

#if WITH_EDITOR
void UPlanetSkyLightComponent::OnRegister()
{
	Super::OnRegister();

	const UWorld* World = GetWorld();
	if (bUpdateInEditorWorld && World && !World->IsGameWorld())
	{
		// 编辑器世界不跑 BeginPlay，钩子只能挂在这里。
		ObjectPreSaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddUObject(
			this, &UPlanetSkyLightComponent::HandleObjectPreSave);
	}
}

void UPlanetSkyLightComponent::OnUnregister()
{
	FCoreUObjectDelegates::OnObjectPreSave.Remove(ObjectPreSaveHandle);
	Super::OnUnregister();
}

void UPlanetSkyLightComponent::HandleObjectPreSave(UObject* Object, FObjectPreSaveContext)
{
	// 正在存这盏天光所在的包：先把作者立方图换回去。瞬态纹理存不进包，会写成空引用。
	if (bReplacedAuthoredCubemap && SkyLight.IsValid()
		&& Object && Object->GetPackage() == SkyLight->GetPackage())
	{
		SkyLight->SetCubemap(AuthoredCubemap);
		bReplacedAuthoredCubemap = false;
		// 存完下一 Tick 重新推一次计算图。
		bHasUpdated = false;
	}
}
#endif

void UPlanetSkyLightComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 编辑器世界里本组件改的是关卡那盏天光，退出时把作者立方图还回去，别让瞬态纹理留到下次存盘。
	if (bReplacedAuthoredCubemap && SkyLight.IsValid())
	{
		SkyLight->SetCubemap(AuthoredCubemap);
		bReplacedAuthoredCubemap = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UPlanetSkyLightComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 编辑器世界也驱动，观察点在 GetObserverLocation 里换成视口相机。
	if (!World->IsGameWorld() && !bUpdateInEditorWorld)
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

	if (!UpdateCube(Observer, Key))
	{
		return;
	}
	LastKey = MoveTemp(Key);
	bHasUpdated = true;
	LastUpdateTime = World->GetTimeSeconds();
}

void UPlanetSkyLightComponent::RefreshNow()
{
	UWorld* World = GetWorld();
	if (!World || (!World->IsGameWorld() && !bUpdateInEditorWorld))
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

	if (!UpdateCube(Observer, Key))
	{
		return;
	}
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

USkyLightComponent* UPlanetSkyLightComponent::FindOwnedSkyLight(const AActor* Owner)
{
	if (!Owner)
	{
		return nullptr;
	}

	// 先看本 Actor 自己：蓝图子类把 SkyLight 组件塞进行星身上时走这条。
	if (USkyLightComponent* OnOwner = Owner->FindComponentByClass<USkyLightComponent>())
	{
		return OnOwner;
	}

	// 再看挂在本 Actor 下的 SkyLight Actor —— 关卡里「把天光拖到行星上」就是这种挂法。
	TArray<AActor*> AttachedActors;
	Owner->GetAttachedActors(AttachedActors);
	for (AActor* Attached : AttachedActors)
	{
		if (USkyLightComponent* Found = Attached
			? Attached->FindComponentByClass<USkyLightComponent>() : nullptr)
		{
			return Found;
		}
	}
	return nullptr;
}

bool UPlanetSkyLightComponent::ResolveSkyLight()
{
	if (SkyLight.IsValid())
	{
		return true;
	}

	USkyLightComponent* Found = FindOwnedSkyLight(GetOwner());
	if (!Found)
	{
		if (!bLoggedMissingSkyLight)
		{
			bLoggedMissingSkyLight = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[天光] %s 自身与它挂的子 Actor 里都没有 SkyLight，天光不工作。"),
				*GetNameSafe(GetOwner()));
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
	if (CameraManager)
	{
		OutLocation = CameraManager->GetCameraLocation();
		return true;
	}

#if WITH_EDITOR
	// 编辑器世界没有玩家控制器，取第一台透视视口：预览要的就是作者在视口里看到的那个方向。
	if (World && !World->IsGameWorld() && GEditor)
	{
		for (FLevelEditorViewportClient* LevelClient : GEditor->GetLevelViewportClients())
		{
			if (LevelClient && LevelClient->IsPerspective())
			{
				OutLocation = LevelClient->GetViewLocation();
				return true;
			}
		}
	}
#endif

	// 没有相机时拿行星中心当观察点算出来的方向是错的，宁可不工作。
	if (!bLoggedMissingCamera)
	{
		bLoggedMissingCamera = true;
		UE_LOG(LogTemp, Warning, TEXT("[天光] 没有玩家相机，天光不工作。"));
	}
	return false;
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
	OutKey.RayleighScaleHeightRatio = Atmo->RayleighScaleHeightRatio;
	OutKey.MieScaleHeightRatio = Atmo->MieScaleHeightRatio;
	OutKey.SunIntensity = Atmo->SunIntensity;
	OutKey.CloudCoverage = Atmo->CloudCoverage;
	OutKey.bAtmosphereEnabled = Atmo->bAtmosphereEnabled;
	OutKey.bCloudsEnabled = Atmo->bCloudsEnabled;
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
	if (!Atmosphere.IsValid())
	{
		return true;
	}

	if (!bHasUpdated)
	{
		return true;
	}

	// 参数被改就必须下一次 Tick 重算，不等最小间隔：环境光要立刻跟上可见天空。
	if (Key.bAtmosphereEnabled != LastKey.bAtmosphereEnabled
		|| Key.bCloudsEnabled != LastKey.bCloudsEnabled
		|| !FMath::IsNearlyEqual(Key.RayleighScaleHeightRatio, LastKey.RayleighScaleHeightRatio,
			KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Key.MieScaleHeightRatio, LastKey.MieScaleHeightRatio,
			KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Key.CloudCoverage, LastKey.CloudCoverage, KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Key.SunIntensity.R, LastKey.SunIntensity.R, KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Key.SunIntensity.G, LastKey.SunIntensity.G, KINDA_SMALL_NUMBER)
		|| !FMath::IsNearlyEqual(Key.SunIntensity.B, LastKey.SunIntensity.B, KINDA_SMALL_NUMBER))
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
		Key.RayleighScaleHeightRatio * Key.ShellRadiusCm * 0.25f, 1.0f);
	return FMath::Abs(Key.AltitudeCm - LastKey.AltitudeCm) > AltitudeThresholdCm;
}

bool UPlanetSkyLightComponent::UpdateCube(const FVector& Observer, const FUpdateKey& Key)
{
	UPlanetAtmosphereComponent* Atmo = Atmosphere.Get();
	USkyLightComponent* Target = SkyLight.Get();
	if (!Atmo || !Target)
	{
		return false;
	}

	PlanetSky::FModelInput Input;
	Input.ObserverLocation = Observer;
	Input.PlanetCenter = Atmo->GetComponentLocation();
	Input.SurfaceRadiusCm = Key.SurfaceRadiusCm;
	Input.ShellRadiusCm = Key.ShellRadiusCm;
	Input.Scattering = PlanetAtmosphere::ComputeShellScatteringParams(
		Key.ShellRadiusCm, Key.RayleighScaleHeightRatio, Key.MieScaleHeightRatio);
	// 环境光必须和玩家看得见的天空一致：大气/云关掉时，可见天空里也没有它们。
	if (!Key.bAtmosphereEnabled)
	{
		Input.Scattering.RayleighBeta = FVector3f::ZeroVector;
		Input.Scattering.MieBeta = FVector3f::ZeroVector;
	}
	Input.SunIntensity = Key.SunIntensity;
	// 大气关掉时云壳也跟着隐藏，所以云的压暗也要一起停，不能只看 bCloudsEnabled。
	Input.CloudCoverage = (Key.bAtmosphereEnabled && Key.bCloudsEnabled) ? Key.CloudCoverage : 0.0f;
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

	const int32 Resolution = FMath::Clamp(CubeResolution, 4, 64);

	const double StartSeconds = FPlatformTime::Seconds();
	PlanetSky::RenderCube(Input, Resolution, Pixels);

	UTextureCube* Cube = UTextureCube::CreateTransient(Resolution, Resolution, PF_FloatRGBA);
	if (!Cube)
	{
		if (!bLoggedCubemapFailure)
		{
			bLoggedCubemapFailure = true;
			UE_LOG(LogTemp, Warning, TEXT("[天光] 立方图创建失败（边长 %d），跳过本次更新。"),
				Resolution);
		}
		return false;
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

#if WITH_EDITOR
	// 编辑器世界里改的是关卡里那盏真天光：首次覆盖前记住作者那张立方图，存盘 / 卸载时要换回去。
	if (const UWorld* World = GetWorld();
		World && !World->IsGameWorld() && !bReplacedAuthoredCubemap)
	{
		AuthoredCubemap = Target->Cubemap;
		bReplacedAuthoredCubemap = true;
	}
#endif

	// 每次新建纹理：SetCubemap 只在指针变化时才重新卷积，复用同一张会让环境光停在第一次。
	CurrentCubemap = Cube;
	Target->SetCubemap(Cube);

	const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	// 编辑器里视口一动就是 5 Hz 重算，别刷屏；游戏世界保持 Log 便于核对节流。
	const UWorld* World = GetWorld();
	if (World && World->IsGameWorld())
	{
		UE_LOG(LogTemp, Log, TEXT("[天光] 重算 边长=%d 太阳=%d 用时=%.3f ms"),
			Resolution, Input.Suns.Num(), ElapsedMs);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("[天光] 重算 边长=%d 太阳=%d 用时=%.3f ms"),
			Resolution, Input.Suns.Num(), ElapsedMs);
	}
	return true;
}
