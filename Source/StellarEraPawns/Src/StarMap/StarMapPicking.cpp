// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapPicking.h"

#include "CosmosSimulationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "StarMapCameraPawn.h"
#include "VoxelPlanetActor.h"

double StarMapPicking::GetBodyRadiusCm(const AActor* Body)
{
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body))
	{
		return Planet->GetSurfaceRadiusCm();
	}
	return Body ? double(Body->GetSimpleCollisionRadius()) : 0.0;
}

bool StarMapPicking::MakeScreenCandidate(
	const APlayerController* PC, AActor* Body, FStarMapScreenCandidate& Out)
{
	Out = FStarMapScreenCandidate();
	if (!PC || !IsValid(Body))
	{
		return false;
	}

	const FVector Center = Body->GetActorLocation();
	if (!PC->ProjectWorldLocationToScreen(Center, Out.ScreenPosition))
	{
		return false;
	}

	// 视线右向：算「与视线垂直」的半径必须用它，不能用固定世界轴。
	FVector ViewLocation;
	FRotator ViewRotation;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

	// 盘面半径 = |投影(中心) − 投影(中心 + 右向 × 世界半径)|。
	// 边缘点投不出来（越过后平面等）就留 0，退回固定像素容差。
	const double WorldRadiusCm = GetBodyRadiusCm(Body);
	if (WorldRadiusCm > 0.0)
	{
		FVector2D EdgePosition;
		if (PC->ProjectWorldLocationToScreen(Center + ViewRight * WorldRadiusCm, EdgePosition))
		{
			// 直接投影「中心 + 右向 × R」量到的是 f·R/d，比切线决定的真实轮廓半径
			// 小一个 cos α（见 SilhouetteRadiusScale）。系数为 0 表示相机在球内
			// 或贴着球面，此时留 0、退回固定像素容差。
			const double CameraDistanceCm = FVector::Dist(ViewLocation, Center);
			Out.ScreenRadiusPx = float(
				FVector2D::Distance(Out.ScreenPosition, EdgePosition)
				* AStarMapCameraPawn::SilhouetteRadiusScale(CameraDistanceCm, WorldRadiusCm));
		}
	}

	Out.Actor = Body;
	return true;
}

AActor* StarMapPicking::PickBodyUnderScreenPosition(
	const UWorld* World,
	const APlayerController* PC,
	const FVector2D& ScreenPosition,
	const float PixelTolerance)
{
	if (!World || !PC)
	{
		return nullptr;
	}

	UCosmosSimulationSubsystem* Subsystem = World->GetSubsystem<UCosmosSimulationSubsystem>();
	if (!Subsystem)
	{
		return nullptr;
	}

	TArray<AActor*> Bodies;
	Subsystem->GetRegisteredBodies(Bodies);

	// 投影留在调用方，拾取函数才能是纯函数、才能无头测。
	TArray<FStarMapScreenCandidate> Candidates;
	Candidates.Reserve(Bodies.Num());
	for (AActor* Body : Bodies)
	{
		if (!AStarMapCameraPawn::HasStarMapCamera(Body))
		{
			continue;
		}

		FStarMapScreenCandidate Candidate;
		if (MakeScreenCandidate(PC, Body, Candidate))
		{
			Candidates.Add(Candidate);
		}
	}

	return AStarMapCameraPawn::PickBodyAtScreenPosition(
		Candidates, ScreenPosition, PixelTolerance);
}
