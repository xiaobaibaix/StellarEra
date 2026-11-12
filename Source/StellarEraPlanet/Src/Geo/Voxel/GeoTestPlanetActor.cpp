#include "GeoTestPlanetActor.h"
#include "Async/Async.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelTools/VoxelBlueprintLibrary.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoGenerator.h"
#include "PlanetRecipe.h"

AGeoTestPlanetActor::AGeoTestPlanetActor()
{
	// 世界在烘焙完成后显式创建；基类自动建会在快照就绪前用空生成器建一次。
	bCreateWorldAutomatically = false;
	bCreateGlobalPool = false;
	MaterialConfig = EVoxelMaterialConfig::RGB;
	MaxLOD = 12;
	// 与行星同值：基类 1000 ms 预算会让一批网格在一帧内全提交。
	bDitherChunks = false;
	MeshUpdatesBudget = 8.f;
#if WITH_EDITOR
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(
		TEXT("/Game/Code/Planet/Materials/M_PlanetVertexColor.M_PlanetVertexColor"));
	if (Mat.Succeeded()) { VoxelMaterial = Mat.Object; }
#endif
}

int32 AGeoTestPlanetActor::ComputeWorldSize(double RadiusVoxels)
{
	// 夹到 [1, 2^30]：非正值会得 0，过大值会让 uint32 溢出回绕成 0。
	const double Clamped = FMath::Clamp(RadiusVoxels * 3.0, 1.0, double(1 << 30));
	return int32(FMath::RoundUpToPowerOfTwo(uint32(FMath::CeilToInt(Clamped))));
}

void AGeoTestPlanetActor::BeginPlay()
{
	Super::BeginPlay();
	bShuttingDown = false;
	StartBake();
}

void AGeoTestPlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bShuttingDown = true;
	Super::EndPlay(EndPlayReason);
}

void AGeoTestPlanetActor::StartBake()
{
	if (bBakeInFlight) { return; }
	if (!Recipe)
	{
		UE_LOG(LogTemp, Error, TEXT("[地质] %s 没有配方"), *GetName());
		return;
	}
	bBakeInFlight = true;
	const FPlanetRecipeConfig Config = Recipe->ToConfig();   // 唯一一次碰 UObject
	TWeakObjectPtr<AGeoTestPlanetActor> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Config]()
	{
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeReport Report;
		TSharedPtr<const FPlanetSnapshot> Snap = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
		const double Seconds = FPlatformTime::Seconds() - T0;
		for (const FString& E : Report.Errors) { UE_LOG(LogTemp, Error, TEXT("%s"), *E); }
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Snap, Radius = Config.Radius, Seconds]()
		{
			if (AGeoTestPlanetActor* Self = WeakThis.Get()) { Self->OnBakeFinished(Snap, Radius, Seconds); }
		});
	});
}

void AGeoTestPlanetActor::OnBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double RadiusVoxels, double BakeSeconds)
{
	bBakeInFlight = false;
	UWorld* World = GetWorld();
	if (bShuttingDown || !World || World->bIsTearingDown) { return; }
	if (!InSnapshot) { return; }
	Snapshot = InSnapshot;
	UE_LOG(LogTemp, Log, TEXT("[地质] %s 烘焙完成：半径 %.0f 体素，%.2f s，快照 %.1f MB，哈希 %016llx"),
		*GetName(), RadiusVoxels, BakeSeconds, double(Snapshot->GetAllocatedSize()) / (1024.0 * 1024.0), Snapshot->ContentHash);

	// 先拆旧世界：已创建时 SetWorldSize 会被拒，且旧世界还在用旧生成器。
	if (IsCreated()) { DestroyWorld(); }

	if (!GeoGenerator) { GeoGenerator = NewObject<UPlanetGeoGenerator>(this, TEXT("GeoGenerator")); }
	GeoGenerator->SetSnapshot(Snapshot);
	SetWorldSize(ComputeWorldSize(RadiusVoxels));
	// 必须在 CreateWorld 前：插件在创建时快照生成器。
	Generator = FVoxelGeneratorPicker(GeoGenerator.Get());

	if (World && World->IsGameWorld() && !UVoxelBlueprintLibrary::IsWorldVoxelPoolCreated(World))
	{
		UVoxelBlueprintLibrary::CreateWorldVoxelThreadPool(World, PriorityCategories, PriorityOffsets, NumberOfThreads, bConstantPriorities);
	}
	CreateWorld(FVoxelWorldCreateInfo());
}
