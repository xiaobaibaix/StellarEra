// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetCutawayComponent.h"

#include "PlanetAtmosphereComponent.h"
#include "PlanetSectionImage.h"
#include "VoxelPlanetActor.h"

#include "Components/LineBatchComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "RHITypes.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	const TCHAR* SectionMaterialPath = TEXT("/Game/Materials/M_PlanetSection.M_PlanetSection");
	const TCHAR* CutawayOriginMpcPath = TEXT("/Game/Materials/MPC_CutawayOrigin.MPC_CutawayOrigin");
	const TCHAR* CutawayNormalMpcPath = TEXT("/Game/Materials/MPC_CutawayNormal.MPC_CutawayNormal");
	const TCHAR* SectionPlaneMeshPath = TEXT("/Engine/BasicShapes/Plane.Plane");

	const TCHAR* SectionTextureParameter = TEXT("SectionTexture");
	const TCHAR* SectionAxisUParameter = TEXT("SectionAxisU");
	const TCHAR* SectionAxisVParameter = TEXT("SectionAxisV");
	const TCHAR* SectionHalfExtentParameter = TEXT("SectionHalfExtentCm");
	const TCHAR* CutawayOriginParameter = TEXT("CutawayOrigin");
	const TCHAR* CutawayNormalParameter = TEXT("CutawayNormal");

	// 预览限频：转过至少 0.75° 且距上次 ≥ 33ms 才重渲染。
	constexpr float PreviewRenderIntervalSeconds = 0.033f;
	constexpr float PreviewRenderStepDegrees = 0.75f;

	const FLinearColor RingColors[3] = {
		FLinearColor(1.f, 0.15f, 0.15f, 1.f),
		FLinearColor(0.2f, 1.f, 0.2f, 1.f),
		FLinearColor(0.2f, 0.45f, 1.f, 1.f)
	};
}

UPlanetCutawayComponent::UPlanetCutawayComponent()
{
	// 只在激活期间 Tick，且排在后更新阶段（此时相机位置本帧已定）。
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

bool UPlanetCutawayComponent::Activate(
	AVoxelPlanetActor* InPlanet,
	const FVector& InitialPlaneNormal)
{
	if (bActive)
	{
		Deactivate();
	}

	if (!InPlanet)
	{
		return false;
	}

	const FVector Normal = InitialPlaneNormal.GetSafeNormal();
	if (Normal.IsNearlyZero())
	{
		return false;
	}

	TSharedPtr<const FPlanetGeologyField> Geology = InPlanet->GetGeologyField();
	if (!Geology.IsValid() || Geology->IsEmpty())
	{
		return false;
	}

	// 资产缺失时在 EnsureViewAssets 里报错返回：调用方据此提示「该行星没有矿物分布」。
	Planet = InPlanet;
	CachedGeology = Geology;
	if (!EnsureViewAssets())
	{
		Planet.Reset();
		CachedGeology.Reset();
		return false;
	}

	PlaneOrientation = FRotationMatrix::MakeFromZ(Normal).ToQuat();
	ClipNormal = FPlanetCutawayMath::ComputeClipNormal(
		Normal, GetCameraLocationOrDefault(), InPlanet->GetActorLocation());
	FPlanetCutawayMath::ComputePlaneBasis(Normal, AxisU, AxisV);
	PendingRotationDegrees = 0.f;
	SecondsSincePreviewRender = PreviewRenderIntervalSeconds;

	if (UPlanetAtmosphereComponent* Atmosphere = InPlanet->GetAtmosphereComponent())
	{
		bSavedAtmosphereEnabled = Atmosphere->bAtmosphereEnabled;
		bSavedAtmosphereVisible = Atmosphere->IsVisible();
		Atmosphere->bAtmosphereEnabled = false;
		Atmosphere->SetVisibility(false);
	}

	bActive = true;
	ApplyClipMask();
	RenderSection(FinalResolution);
	UpdateDiscTransform();
	UpdateRingGeometry();

	if (SectionMesh)
	{
		SectionMesh->SetVisibility(true);
	}
	if (SectionRings)
	{
		SectionRings->SetVisibility(true);
	}

	SetComponentTickEnabled(true);
	return true;
}

void UPlanetCutawayComponent::Deactivate()
{
	if (!bActive)
	{
		return;
	}
	bActive = false;
	SetComponentTickEnabled(false);

	if (AVoxelPlanetActor* RestorePlanet = Planet.Get())
	{
		if (UPlanetAtmosphereComponent* Atmosphere = RestorePlanet->GetAtmosphereComponent())
		{
			Atmosphere->bAtmosphereEnabled = bSavedAtmosphereEnabled;
			if (bSavedAtmosphereEnabled)
			{
				// Refresh 成功路径末尾会 SetVisibility(true)：必须先刷新、再恢复
				// 进入前的可见性，否则「enabled 但被手动隐藏」的壳会被强行显示。
				Atmosphere->RefreshAtmosphereGeometry();
			}
			Atmosphere->SetVisibility(bSavedAtmosphereVisible);
		}
	}

	if (SectionMesh)
	{
		SectionMesh->SetVisibility(false);
	}
	if (SectionRings)
	{
		SectionRings->Flush();
		SectionRings->SetVisibility(false);
	}

	// 退出即恢复的另一半：Active = 0 让全星球的 OpacityMask 立即回到 1。
	if (CutawayOriginMpc)
	{
		UKismetMaterialLibrary::SetVectorParameterValue(
			this, CutawayOriginMpc, CutawayOriginParameter,
			FLinearColor(0.f, 0.f, 0.f, 0.f));
	}

	Planet.Reset();
	CachedGeology.Reset();
}

void UPlanetCutawayComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Deactivate();
	ReleaseViewAssets();
	Super::EndPlay(EndPlayReason);
}

void UPlanetCutawayComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!bActive || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const FVector PlaneNormal = PlaneOrientation.GetAxisZ();

	// 相机转过去时自动换边：看得见的永远是远侧。
	ClipNormal = FPlanetCutawayMath::ComputeClipNormal(
		PlaneNormal, GetCameraLocationOrDefault(), Center);
	FPlanetCutawayMath::ComputePlaneBasis(PlaneNormal, AxisU, AxisV);

	ApplyClipMask();
	UpdateDiscTransform();
	UpdateRingGeometry();

	SecondsSincePreviewRender += DeltaTime;
	if (PendingRotationDegrees >= PreviewRenderStepDegrees
		&& SecondsSincePreviewRender >= PreviewRenderIntervalSeconds)
	{
		RenderSection(PreviewResolution);
	}
}

void UPlanetCutawayComponent::RotatePlane(ESectionRing Ring, float DeltaDegrees)
{
	if (!bActive || DeltaDegrees == 0.f)
	{
		return;
	}

	FVector Axis = FVector::UpVector;
	switch (Ring)
	{
	case ESectionRing::X: Axis = FVector::ForwardVector; break;
	case ESectionRing::Y: Axis = FVector::RightVector; break;
	default:              Axis = FVector::UpVector; break;
	}

	PlaneOrientation = FPlanetCutawayMath::RotatePlane(PlaneOrientation, Axis, DeltaDegrees);
	PendingRotationDegrees += FMath::Abs(DeltaDegrees);

	// 盘与环立刻跟随，纹理在 Tick 里限频重渲染。
	FPlanetCutawayMath::ComputePlaneBasis(PlaneOrientation.GetAxisZ(), AxisU, AxisV);
	UpdateDiscTransform();
	UpdateRingGeometry();
}

void UPlanetCutawayComponent::RequestSectionRender(int32 Resolution)
{
	if (!bActive)
	{
		return;
	}
	RenderSection(Resolution >= FinalResolution ? FinalResolution : PreviewResolution);
}

void UPlanetCutawayComponent::GetRingWorldPoints(
	TArray<FSectionRingWorldPolyline>& OutRings) const
{
	OutRings.Reset();
	if (!bActive || !Planet.IsValid())
	{
		return;
	}

	const FVector Center = Planet->GetActorLocation();
	const float RadiusCm = ComputeSectionRadiusCm() * RingRadiusScale;

	OutRings.Reserve(3);
	for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
	{
		FSectionRingWorldPolyline& Polyline = OutRings.AddDefaulted_GetRef();
		Polyline.Ring = static_cast<ESectionRing>(RingIndex);
		Polyline.Points.Reserve(RingSegments);
		for (int32 PointIndex = 0; PointIndex < RingSegments; ++PointIndex)
		{
			const float Angle = 2.f * PI * float(PointIndex) / float(RingSegments);
			Polyline.Points.Add(
				Center + FPlanetCutawayMath::RingPoint(Polyline.Ring, RadiusCm, Angle));
		}
	}
}

bool UPlanetCutawayComponent::EnsureViewAssets()
{
	if (SectionMesh && SectionRings && SectionMaterialInstance)
	{
		// 纹理尺寸在首次创建时就定死了；运行期改属性只警告一次，不逐帧重建。
		if (!bWarnedResolutionMismatch &&
			FinalTexture && PreviewTexture &&
			FinalTexture->GetSizeX() > 0 && PreviewTexture->GetSizeX() > 0 &&
			(FinalTexture->GetSizeX() != FinalResolution ||
				PreviewTexture->GetSizeX() != PreviewResolution))
		{
			bWarnedResolutionMismatch = true;
			UE_LOG(LogTemp, Warning,
				TEXT("UPlanetCutawayComponent: 运行期修改了分辨率属性，纹理不会重建；"
					 "继续使用 %dx%d Final / %dx%d Preview。"),
				FinalTexture->GetSizeX(), FinalTexture->GetSizeX(),
				PreviewTexture->GetSizeX(), PreviewTexture->GetSizeX());
		}
		return true;
	}

	// 部分创建（理论上只有 CreateTransient 失败才会发生）：先清掉半成品再重来。
	ReleaseViewAssets();

	UMaterialInterface* SectionMaterial =
		LoadObject<UMaterialInterface>(nullptr, SectionMaterialPath);
	CutawayOriginMpc =
		LoadObject<UMaterialParameterCollection>(nullptr, CutawayOriginMpcPath);
	CutawayNormalMpc =
		LoadObject<UMaterialParameterCollection>(nullptr, CutawayNormalMpcPath);
	SectionPlaneMesh = LoadObject<UStaticMesh>(nullptr, SectionPlaneMeshPath);

	if (!SectionMaterial || !CutawayOriginMpc || !CutawayNormalMpc || !SectionPlaneMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UPlanetCutawayComponent: 剖面资产缺失"
				 "（M_PlanetSection / MPC_CutawayOrigin / MPC_CutawayNormal / Plane）"));
		return false;
	}

	FinalTexture = UTexture2D::CreateTransient(FinalResolution, FinalResolution, PF_B8G8R8A8);
	PreviewTexture = UTexture2D::CreateTransient(
		PreviewResolution, PreviewResolution, PF_B8G8R8A8);
	if (!FinalTexture || !PreviewTexture)
	{
		UE_LOG(LogTemp, Error, TEXT("UPlanetCutawayComponent: CreateTransient 失败"));
		ReleaseViewAssets();
		return false;
	}

	for (UTexture2D* Texture : { FinalTexture.Get(), PreviewTexture.Get() })
	{
		Texture->SRGB = true;
		Texture->Filter = TF_Bilinear;
		Texture->AddressX = TA_Clamp;
		Texture->AddressY = TA_Clamp;
		Texture->NeverStream = true;
		Texture->UpdateResource();
	}

	SectionMaterialInstance = UMaterialInstanceDynamic::Create(SectionMaterial, this);
	if (!SectionMaterialInstance)
	{
		ReleaseViewAssets();
		return false;
	}
	SectionMaterialInstance->SetTextureParameterValue(SectionTextureParameter, FinalTexture);

	SectionMesh = NewObject<UStaticMeshComponent>(this, TEXT("PlanetSectionMesh"), RF_Transient);
	SectionMesh->SetupAttachment(this);
	SectionMesh->SetStaticMesh(SectionPlaneMesh);
	SectionMesh->SetMobility(EComponentMobility::Movable);
	SectionMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SectionMesh->SetCastShadow(false);
	SectionMesh->bReceivesDecals = false;
	SectionMesh->RegisterComponent();
	SectionMesh->SetMaterial(0, SectionMaterialInstance);
	SectionMesh->SetVisibility(false);

	SectionRings = NewObject<ULineBatchComponent>(this, TEXT("PlanetSectionRings"), RF_Transient);
	SectionRings->SetupAttachment(this);
	SectionRings->SetMobility(EComponentMobility::Movable);
	SectionRings->RegisterComponent();
	SectionRings->SetVisibility(false);

	return true;
}

void UPlanetCutawayComponent::ReleaseViewAssets()
{
	if (SectionRings)
	{
		SectionRings->Flush();
		SectionRings->DestroyComponent();
		SectionRings = nullptr;
	}
	if (SectionMesh)
	{
		SectionMesh->DestroyComponent();
		SectionMesh = nullptr;
	}

	SectionMaterialInstance = nullptr;
	FinalTexture = nullptr;
	PreviewTexture = nullptr;
	SectionPlaneMesh = nullptr;
	CutawayOriginMpc = nullptr;
	CutawayNormalMpc = nullptr;

	for (TArray<FColor>& Buffer : FinalPixels)
	{
		Buffer.Reset();
	}
	for (TArray<FColor>& Buffer : PreviewPixels)
	{
		Buffer.Reset();
	}
	FinalPixelsIndex = 0;
	PreviewPixelsIndex = 0;
}

void UPlanetCutawayComponent::ApplyClipMask()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!CutawayOriginMpc || !CutawayNormalMpc || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const float InsideRadiusCm =
		SectionRadiusScale * CutawayPlanet->GetSurfaceRadiusCm();

	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayOriginMpc, CutawayOriginParameter,
		FLinearColor(Center.X, Center.Y, Center.Z, bActive ? 1.f : 0.f));

	UKismetMaterialLibrary::SetVectorParameterValue(
		this, CutawayNormalMpc, CutawayNormalParameter,
		FLinearColor(ClipNormal.X, ClipNormal.Y, ClipNormal.Z, InsideRadiusCm));
}

void UPlanetCutawayComponent::UpdateDiscTransform()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!SectionMesh || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const FVector PlaneNormal = PlaneOrientation.GetAxisZ();
	const float SectionRadiusCm = ComputeSectionRadiusCm();

	// 基与朝向都用未翻转的 PlaneNormal：局部 X/Y = AxisU/AxisV，正面朝 +PlaneNormal。
	// 只有偏移用 ClipNormal。相机绕到背面时图案左右镜像，这是平面画从背面看的正确外观（裁定 2）。
	SectionMesh->SetWorldLocation(Center + ClipNormal * DiscOffsetCm);
	SectionMesh->SetWorldRotation(
		FRotationMatrix::MakeFromXZ(AxisU, PlaneNormal).Rotator());

	// 内置 Plane 是 100×100 cm，等比缩放到 2R 边长。
	const float Scale = 2.f * SectionRadiusCm / 100.f;
	SectionMesh->SetWorldScale3D(FVector(Scale, Scale, 1.f));

	if (SectionMaterialInstance)
	{
		SectionMaterialInstance->SetVectorParameterValue(
			SectionAxisUParameter, FLinearColor(AxisU.X, AxisU.Y, AxisU.Z, 0.f));
		SectionMaterialInstance->SetVectorParameterValue(
			SectionAxisVParameter, FLinearColor(AxisV.X, AxisV.Y, AxisV.Z, 0.f));
		SectionMaterialInstance->SetScalarParameterValue(
			SectionHalfExtentParameter, SectionRadiusCm);
	}
}

void UPlanetCutawayComponent::UpdateRingGeometry()
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!SectionRings || !CutawayPlanet)
	{
		return;
	}

	const FVector Center = CutawayPlanet->GetActorLocation();
	const float RadiusCm = ComputeSectionRadiusCm() * RingRadiusScale;

	TArray<FBatchedLine> Lines;
	Lines.Reserve(3 * RingSegments);

	for (int32 RingIndex = 0; RingIndex < 3; ++RingIndex)
	{
		const ESectionRing Ring = static_cast<ESectionRing>(RingIndex);
		for (int32 Segment = 0; Segment < RingSegments; ++Segment)
		{
			const float Angle0 = 2.f * PI * float(Segment) / float(RingSegments);
			const float Angle1 = 2.f * PI * float(Segment + 1) / float(RingSegments);
			Lines.Emplace(
				Center + FPlanetCutawayMath::RingPoint(Ring, RadiusCm, Angle0),
				Center + FPlanetCutawayMath::RingPoint(Ring, RadiusCm, Angle1),
				RingColors[RingIndex], 0.f, 3.f, SDPG_Foreground);
		}
	}

	SectionRings->Flush();
	SectionRings->DrawLines(Lines);
}

void UPlanetCutawayComponent::RenderSection(int32 Resolution)
{
	AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	if (!bActive || !CutawayPlanet || !CachedGeology.IsValid() || !SectionMaterialInstance)
	{
		return;
	}

	UTexture2D* FinalTarget = FinalTexture.Get();
	UTexture2D* PreviewTarget = PreviewTexture.Get();

	// 纹理只在 EnsureViewAssets 首次创建时按当时属性定尺，运行期改属性不会重建。
	// 渲染/上传尺寸必须取真实纹理：否则区域宽度与行跨距会超过纹理宽度，越界写 RHI。
	const int32 FinalRes = FinalTarget && FinalTarget->GetSizeX() > 0
		? FinalTarget->GetSizeX()
		: FMath::Max(FinalResolution, 1);
	const int32 PreviewRes = PreviewTarget && PreviewTarget->GetSizeX() > 0
		? PreviewTarget->GetSizeX()
		: FMath::Max(PreviewResolution, 1);

	const bool bFinal = Resolution >= FinalRes;
	const int32 Res = bFinal ? FinalRes : PreviewRes;
	UTexture2D* Target = bFinal ? FinalTarget : PreviewTarget;

	// 512² 与 96² 各有两份缓冲轮换：上一份上传可能还在渲染线程里被读。
	TArray<FColor>& Buffer = bFinal
		? FinalPixels[FinalPixelsIndex]
		: PreviewPixels[PreviewPixelsIndex];
	if (!Target)
	{
		return;
	}

	const float SurfaceRadiusCm = CutawayPlanet->GetSurfaceRadiusCm();

	FPlanetSectionImageParams Params;
	Params.Resolution = Res;
	Params.AxisU = AxisU;
	Params.AxisV = AxisV;
	Params.PlaneNormal = PlaneOrientation.GetAxisZ();
	Params.SectionRadiusCm = ComputeSectionRadiusCm();
	Params.VoxelSize = FMath::Max(CutawayPlanet->VoxelSize, 1e-3f);
	Params.BaseRadiusVoxels = SurfaceRadiusCm / Params.VoxelSize;
	Params.SurfaceColor = CutawayPlanet->SurfaceColor;
	Params.Appearance = CachedGeology->GetAppearance();

	// Task 2 留下的调用方约定：剖面半径必须恰好是 1.05 × 基准表面半径（spec §3.1）。
	// 光栅化的单测验不到调用方传了什么，所以把这条比例钉在唯一的构造点。
	const float ExpectedSectionRadiusCm = SectionRadiusScale * SurfaceRadiusCm;
	const float RadiusToleranceCm = 1e-3f * FMath::Max(ExpectedSectionRadiusCm, 1.f);
	checkf(FMath::IsNearlyEqual(
			Params.SectionRadiusCm, ExpectedSectionRadiusCm, RadiusToleranceCm),
		TEXT("剖面半径 %.3f cm 与 1.05 × 表面半径 %.3f cm 不符"),
		Params.SectionRadiusCm, ExpectedSectionRadiusCm);

	// 时间戳留在这里是有意的：每像素一次地表半径查询 + 一次地质采样，512² 是
	// 唯一可能卡顿的一帧（见 spec §9 的风险）。这行日志就是 Task 7 的量测证据。
	const double RenderStartSeconds = FPlatformTime::Seconds();

	FPlanetSectionImage::Render(Params,
		[CutawayPlanet](const FVector& UnitDir)
		{
			return CutawayPlanet->GetSurfaceRadiusCmAtDirection(UnitDir);
		},
		[this](const FVector& UnitDir, double DepthVoxels)
		{
			return CachedGeology->Sample(UnitDir, DepthVoxels);
		},
		Buffer);

	UploadSection(Target, Buffer, Res);
	SectionMaterialInstance->SetTextureParameterValue(SectionTextureParameter, Target);

	if (bFinal)
	{
		FinalPixelsIndex = 1 - FinalPixelsIndex;
	}
	else
	{
		PreviewPixelsIndex = 1 - PreviewPixelsIndex;
	}

	const double RenderMs = (FPlatformTime::Seconds() - RenderStartSeconds) * 1000.0;
	UE_LOG(LogTemp, Log, TEXT("Cutaway: 剖面 %dx%d 渲染+上传 %.1f ms"), Res, Res, RenderMs);

	PendingRotationDegrees = 0.f;
	SecondsSincePreviewRender = 0.f;
}

void UPlanetCutawayComponent::UploadSection(
	UTexture2D* Target,
	TArray<FColor>& Buffer,
	int32 Resolution)
{
	if (!Target || Resolution <= 0 || Buffer.Num() < Resolution * Resolution)
	{
		return;
	}

	// UpdateTextureRegions 把 Regions 与 SrcData 的指针原样挂到渲染命令上，
	// 两者都必须活到 RHI 线程消费完。像素缓冲由两份轮换保证（见 RenderSection），
	// Region 则在这里单独分配、交给 DataCleanupFunc 释放 —— 不能用栈变量。
	FUpdateTextureRegion2D* Region =
		new FUpdateTextureRegion2D(0, 0, 0, 0, uint32(Resolution), uint32(Resolution));

	Target->UpdateTextureRegions(
		0, 1, Region,
		uint32(Resolution * sizeof(FColor)),
		uint32(sizeof(FColor)),
		reinterpret_cast<uint8*>(Buffer.GetData()),
		[](uint8*, const FUpdateTextureRegion2D* Regions)
		{
			delete Regions;
		});
}

float UPlanetCutawayComponent::ComputeSectionRadiusCm() const
{
	const AVoxelPlanetActor* CutawayPlanet = Planet.Get();
	return CutawayPlanet
		? SectionRadiusScale * CutawayPlanet->GetSurfaceRadiusCm()
		: 0.f;
}

FVector UPlanetCutawayComponent::GetCameraLocationOrDefault() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			if (const APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager)
			{
				return CameraManager->GetCameraLocation();
			}
		}
	}

	return Planet.IsValid()
		? Planet->GetActorLocation() + FVector::UpVector * Planet->GetSurfaceRadiusCm()
		: FVector::ZeroVector;
}
