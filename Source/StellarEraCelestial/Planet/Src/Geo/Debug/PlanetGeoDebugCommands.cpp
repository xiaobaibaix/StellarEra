// 无界面导出：编辑器、PIE、-game 都能用，给回归对比与远程自查。
#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoDebugRender.h"
#include "VoxelPlanetActor.h"

static FAutoConsoleCommand CGeoExportLayers(
	TEXT("StellarEra.Geo.ExportLayers"),
	TEXT("把每颗 geo 星球的全部调试图层导出成 PNG（展开地图 + XZ 剖面）到 Saved/GeoDebug/<星球>/。\n")
	TEXT("可选参数：地图宽度（像素，默认 1024）。"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		const int32 Width = Args.Num() > 0 ? FMath::Max(64, FCString::Atoi(*Args[0])) : 1024;
		int32 Planets = 0, Files = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World) { continue; }
			for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
			{
				const TSharedPtr<const FPlanetSnapshot> Snap = It->GetGeoSnapshot();
				if (!Snap) { continue; }
				const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GeoDebug"),
					FString::Printf(TEXT("%s_%s"), *It->GetActorNameOrLabel(), Context.WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Editor")));
				Files += PlanetGeoDebugRender::ExportAll(*Snap, FPlanetBakePipeline::DefaultDebugLayers(), Dir, Width);
				++Planets;
				UE_LOG(LogTemp, Display, TEXT("[地质调试] %s → %s"), *It->GetActorNameOrLabel(), *FPaths::ConvertRelativePathToFull(Dir));
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[地质调试] 导出 %d 颗星球，%d 张图"), Planets, Files);
	}));
