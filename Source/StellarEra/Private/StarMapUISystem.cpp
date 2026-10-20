// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapUISystem.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "StellarEraHUD.h"
#include "VoxelPlanetActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UStarMapUISystem::BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo)
{
	OutInfo = FStarMapBodyInfo();
	if (!IsValid(Body))
	{
		return false;
	}

	const UCosmosOrbitComponent* Orbit =
		Body->FindComponentByClass<UCosmosOrbitComponent>();
	if (!Orbit)
	{
		return false;
	}

	OutInfo.Name = Body->GetActorNameOrLabel();
	OutInfo.MassEarth = Orbit->MassEarth;
	OutInfo.MassSolar = CosmosUnits::EarthMassToSolarMass(Orbit->MassEarth);
	OutInfo.PositionAu = Orbit->PositionAu;
	OutInfo.VelocityKmPerSecond = Orbit->SolverVelocityKmPerSecond;
	OutInfo.SimulatorIndex = Orbit->SimulatorIndex;
	OutInfo.bSimulated = Orbit->bSimulated;
	OutInfo.bExertsGravity = Orbit->bExertsGravity;

	double RadiusCm = 0.0;
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body))
	{
		RadiusCm = Planet->GetSurfaceRadiusCm();
	}
	else
	{
		RadiusCm = double(Body->GetSimpleCollisionRadius());
	}

	OutInfo.RadiusKm = RadiusCm / CosmosUnits::CentimetersPerKilometer;
	OutInfo.SurfaceGravityMps2 =
		ComputeSurfaceGravityMps2(OutInfo.MassEarth, OutInfo.RadiusKm);

	if (OutInfo.SimulatorIndex != INDEX_NONE)
	{
		if (const UWorld* World = Body->GetWorld())
		{
			if (const UCosmosSimulationSubsystem* Subsystem =
				World->GetSubsystem<UCosmosSimulationSubsystem>())
			{
				OutInfo.OrbitalPeriodYears =
					Subsystem->GetOrbitalPeriodYears(OutInfo.SimulatorIndex);
			}
		}
	}

	return true;
}

double UStarMapUISystem::ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm)
{
	if (!FMath::IsFinite(MassEarth) || !FMath::IsFinite(RadiusKm) ||
		MassEarth <= 0.0 || RadiusKm <= 0.0)
	{
		return 0.0;
	}

	const double MassKg =
		MassEarth * CosmosUnits::EarthMass * CosmosUnits::SolarMassToKilogram;
	const double RadiusCm = RadiusKm * CosmosUnits::CentimetersPerKilometer;

	// G_SI 的单位是 cm³/(kg·s²)；除以 100 换成 m/s²。
	return CosmosUnits::GravitationalConstant_SI * MassKg /
		(RadiusCm * RadiusCm) / 100.0;
}

TArray<FString> UStarMapUISystem::FormatBodyInfoLines(const FStarMapBodyInfo& Info)
{
	TArray<FString> Lines;
	Lines.Reserve(9);

	// 第一行是标题（名字），DrawBodyPanel 把它画成金色标题。
	Lines.Add(Info.Name);

	if (FMath::IsFinite(Info.MassEarth) && Info.MassEarth > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("质量        %.4g M⊕（%.4g M☉）"),
			Info.MassEarth, Info.MassSolar));
	}
	else
	{
		Lines.Add(TEXT("质量        —"));
	}

	Lines.Add(FString::Printf(TEXT("位置        (%.4f, %.4f, %.4f) AU"),
		Info.PositionAu.X, Info.PositionAu.Y, Info.PositionAu.Z));

	Lines.Add(FString::Printf(TEXT("速度        %.3f km/s"),
		Info.VelocityKmPerSecond.Size()));

	if (FMath::IsFinite(Info.OrbitalPeriodYears) && Info.OrbitalPeriodYears > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("轨道周期    %.3f 年"),
			Info.OrbitalPeriodYears));
	}
	else
	{
		Lines.Add(TEXT("轨道周期    未知"));
	}

	if (FMath::IsFinite(Info.RadiusKm) && Info.RadiusKm > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("半径        %.1f km"), Info.RadiusKm));
	}
	else
	{
		Lines.Add(TEXT("半径        —"));
	}

	if (FMath::IsFinite(Info.SurfaceGravityMps2) && Info.SurfaceGravityMps2 > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("表面引力    %.3f m/s²（%.3f g）"),
			Info.SurfaceGravityMps2, Info.SurfaceGravityMps2 / 9.80665));
	}
	else
	{
		Lines.Add(TEXT("表面引力    —"));
	}

	Lines.Add(Info.SimulatorIndex == INDEX_NONE
		? TEXT("Simulator   #-1（未注册）")
		: FString::Printf(TEXT("Simulator   #%d"), Info.SimulatorIndex));

	Lines.Add(FString::Printf(TEXT("模拟        %s    引力源    %s"),
		Info.bSimulated ? TEXT("是") : TEXT("否"),
		Info.bExertsGravity ? TEXT("是") : TEXT("否")));

	return Lines;
}
