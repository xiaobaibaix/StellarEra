// 编辑器态映射预览：把天体按 FUniverseMapping 摆到“物理尺度下的初始布局”，不跑物理。
// 用途：PIE 之前先看清初始位置/尺寸；日志里的 AU 坐标可直接填进 InitialPositionAu。

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "CosmosBody.h"
#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "StarBodyActor.h"
#include "UniverseMapping.h"
#include "VoxelPlanetActor.h"

#if WITH_EDITOR

namespace
{
	void ApplyMappedLayout()
	{
		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::Editor && Context.World())
				{
					World = Context.World();
					break;
				}
			}
		}
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[映射预览] 没有编辑器世界"));
			return;
		}

		const UCosmosSimulationSubsystem* Defaults = GetDefault<UCosmosSimulationSubsystem>();
		if (!Defaults->bPhysicalScale)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[映射预览] bPhysicalScale 未开启：编辑器布局本来就是摆放位置，无需预览"));
			return;
		}

		FUniverseMapping Mapping;
		Mapping.RenderCmPerAu = Defaults->CentimetersPerAu;
		const double RenderCm = Defaults->CentimetersPerAu;
		const double PhysicalCm = FUniverseMapping::PhysicalCmPerAu;

		// 只遍历本世界的 Actor：TObjectIterator 会把 CDO / 其它世界上下文的组件也扫进来。
		TArray<UCosmosOrbitComponent*> Components;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UCosmosOrbitComponent* Component = It->FindComponentByClass<UCosmosOrbitComponent>();
			if (Component && Component->bRegistersWithSimulation)
			{
				Components.Add(Component);
			}
		}
		if (Components.Num() == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[映射预览] 编辑器世界里没有登记的天体"));
			return;
		}

		// 求解器位置的来源与注册一致：显式 AU 优先，否则摆放 ÷ 物理标定。
		TArray<FVector3d> Positions;
		TArray<double> Radii;
		int32 ShipIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			UCosmosOrbitComponent* Component = Components[Index];
			AActor* Owner = Component->GetOwner();
			const FVector3d PlacementAu = FVector3d(Owner->GetActorLocation()) / PhysicalCm;
			Positions.Add(ResolveInitialPosition(
				PlacementAu, Component->InitialPositionAu, Component->bUseInitialPositionAu));

			double RadiusAu = 0.0;
			if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Owner))
			{
				RadiusAu = double(Planet->GetPhysicalRadiusCm()) / PhysicalCm;
			}
			Radii.Add(RadiusAu);
			if (Cast<ICosmosBody>(Owner))
			{
				ShipIndex = Index;
			}
		}

		const int32 CurrentBody = ShipIndex == INDEX_NONE ? INDEX_NONE : PickCurrentBody(
			SurfaceDistancesCm(Positions, Radii, Positions[ShipIndex], PhysicalCm, ShipIndex),
			INDEX_NONE, Mapping);

		// 锚点钉在当前天体现有世界位置；远场以飞船为中心。
		const int32 AnchorIndex = CurrentBody != INDEX_NONE ? CurrentBody : ShipIndex;
		const FVector3d AnchorAu = AnchorIndex != INDEX_NONE ? Positions[AnchorIndex] : FVector3d::ZeroVector;
		const FVector AnchorWorld = AnchorIndex != INDEX_NONE
			? Components[AnchorIndex]->GetOwner()->GetActorLocation()
			: FVector::ZeroVector;
		const FVector ShipWorld = ShipIndex != INDEX_NONE
			? AnchorWorld + FVector((Positions[ShipIndex] - AnchorAu) * PhysicalCm)
			: AnchorWorld;

		for (int32 Index = 0; Index < Components.Num(); ++Index)
		{
			UCosmosOrbitComponent* Component = Components[Index];
			AActor* Owner = Component->GetOwner();
			const bool bNear = Index == AnchorIndex || Index == ShipIndex;
			const FVector RenderWorld = bNear
				? FVector(AnchorWorld + FVector3d(Positions[Index] - AnchorAu) * PhysicalCm)
				: FVector(ShipWorld + FVector3d(Positions[Index] - Positions[ShipIndex]) * RenderCm);
			const double Scale = bNear ? 1.0 : RenderCm / PhysicalCm;

			Owner->Modify();
			Owner->SetActorLocation(RenderWorld);
			Owner->SetActorScale3D(FVector(Scale));

			if (AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Owner))
			{
				Planet->SetFarFieldMode(!bNear);
			}
			else if (AStarBodyActor* Star = Cast<AStarBodyActor>(Owner))
			{
				Star->ApplyScaleMode(true);
			}

			UE_LOG(LogTemp, Log,
				TEXT("[映射预览] %s：AU (%.6e, %.6e, %.6e)，R %.3e AU，%s"),
				*Owner->GetActorLabel(), Positions[Index].X, Positions[Index].Y, Positions[Index].Z,
				Radii[Index], bNear ? TEXT("近场 1:1") : TEXT("远场压缩"));
		}

		UE_LOG(LogTemp, Log, TEXT("[映射预览] 完成：%d 个天体，当前天体 %d，飞船 %d"),
			Components.Num(), CurrentBody, ShipIndex);
	}

	FAutoConsoleCommand GPreviewMappedLayout(
		TEXT("StellarEra.Cosmos.PreviewMappedLayout"),
		TEXT("编辑器态按映射摆放天体并打印 AU 坐标（需 bPhysicalScale=true）"),
		FConsoleCommandDelegate::CreateStatic(&ApplyMappedLayout));
}

#endif // WITH_EDITOR
