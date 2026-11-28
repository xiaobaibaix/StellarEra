// Copyright Voxel Plugin SAS. All Rights Reserved.
// 把地质流水线的快照封装为可直接摆放的 Actor。

#include "VoxelPlanetActor.h"
#include "GeoBakeCache.h"

#include "VoxelWorldCreateInfo.h"
// 世界线程池：多颗行星共用同一个池，必须在任何 CreateWorld 之前建好。
#include "VoxelTools/VoxelBlueprintLibrary.h"
// 基类只前置声明了 UVoxelWorldRootComponent，修正组件树要调它的方法，这里要完整类型。
#include "VoxelWorldRootComponent.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "PlanetSunLightComponent.h"
#include "Sky/PlanetSkyLightComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureCube.h"
#include "Engine/Texture2D.h"
// 地质调色板：快照矿物表 → 256×3 LUT，材质按行中心采样。
#include "GeoPalette.h"
#include "GeoMineral.h"
#include "GeoRng.h"
#include "Engine/World.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData/VoxelSave.h"
#include "VoxelData/VoxelSaveUtilities.h"
#include "VoxelTools/VoxelDataTools.h"
// 新地质流水线：配方 → 后台烘焙 → 无 UObject 的快照 → 体素生成器。
#include "PlanetRecipe.h"
#include "GeoMineralCatalog.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoGenerator.h"
#include "PlanetGeoDebugRender.h"
#include "PlanetShapeEval.h"
#include "PlanetSurfaceRay.h"
#include "VoxelData/VoxelData.h"
#include "VoxelData/VoxelDataIncludes.h"
#include "VoxelData/VoxelDataLock.h"
#include "VoxelIntBox.h"
#include "VoxelValue.h"
#include "VoxelVector.h"
// 后台存档任务（Async(EAsyncExecution::ThreadPool, ...)）。
#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

///////////////////////////////////////////////////////////////////////////////
// 存档迁移工具
///////////////////////////////////////////////////////////////////////////////

/**
 * 把当前世界所有行星的存档按**当前** CVar 重存一次，用于把 Special 值格式的旧存档
 * 迁到普通格式。迁移须「按旧格式读、按新格式写」：读时 CVar 仍为 1，写前切 0 ——
 * 只改 ini 不够，存读必须同设置。
 */
static FAutoConsoleCommandWithWorldAndArgs CVoxelPlanetResaveAll(
	TEXT("StellarEra.Planet.ResaveAll"),
	TEXT("按 voxel.data.StoreSpecialValueForGeneratorValuesInSaves 重存当前世界里所有行星的存档。\n")
	TEXT("可选参数 0 / 1：先在写之前把那个 CVar 切过去。迁移旧存档时传 0 ——\n")
	TEXT("读的时候它必须还是 1，所以切换不能提前到命令行，只能在这里、加载之后。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Args](float) -> bool
		{
			TArray<AVoxelPlanetActor*> Planets;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* const World = Context.World();
				if (!World)
				{
					continue;
				}
				for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
				{
					Planets.Add(*It);
				}
				if (!Planets.IsEmpty())
				{
					break;
				}
			}

			if (Planets.IsEmpty())
			{
				// 世界还没起来，下一帧再试。
				return true;
			}

			if (!Args.IsEmpty())
			{
				const FString Command = FString::Printf(
					TEXT("voxel.data.StoreSpecialValueForGeneratorValuesInSaves %s"), *Args[0]);
				if (UWorld* const World = Planets[0]->GetWorld())
				{
					if (GEngine)
					{
						GEngine->Exec(World, *Command);
					}
					UE_LOG(LogTemp, Log, TEXT("重存前切换：%s"), *Command);
				}
			}

			for (AVoxelPlanetActor* const Planet : Planets)
			{
				// SaveEditsToSaveObject 无脏编辑就早退；这里要无条件重存，先置脏。
				Planet->NotifyVoxelEdited();
				Planet->SaveEditsToSaveObject();
				UE_LOG(LogTemp, Log, TEXT("重存行星存档：%s 完成。"), *Planet->GetName());
			}
			return false;
		}));
	}));

AVoxelPlanetActor::AVoxelPlanetActor()
{
	// 基类构造函数已把 WorldRoot 建成 RootComponent，这里不再另建根组件。

	// 世界由 RebuildPlanet 显式创建。基类 BeginPlay 会在 bCreateWorldAutomatically
	// 为真时排一个「下一帧 CreateWorld」，会与我们撞成建两次。显式关掉。
	bCreateWorldAutomatically = false;

	// 世界线程池由 RebuildPlanet 在 CreateWorld 前建一次。基类默认 true 会让每颗
	// 行星各自抢建：先建的定线程数、后建的刷 already created。统一 false 只复用。
	bCreateGlobalPool = false;

	// 策展默认值：基类 2 线程喂不饱地球量级行星（背面区块晚到成方形空洞）。16 核机器
	// 留 4 核给游戏线程。写构造函数而非重建流程，避免覆盖用户面板设置。
	NumberOfThreads = 10;
	MaxLOD = 6;

	// LOD 切换不做淡入淡出：基类默认开抖动，新旧两套 LOD 网格会同时可见数帧
	// （mesh 重叠）。关掉后旧块在替换块全建好后当场删除，代价是 LOD 边界会跳。
	// 改回 true 时 ChunksDitheringDuration 才生效。
	bDitherChunks = false;
	ChunksDitheringDuration = 0.3f;

	// 每帧应用已完成块网格的时间预算（ms）。基类默认 1000，一批完成时游戏线程
	// 卡满一整秒；16ms 约一帧，比 8ms 快一倍而仍不整帧超支。只在 Game play type 生效。
	MeshUpdatesBudget = 16.f;

	// 块级 MID 是每块一次的游戏线程开销（建实例 + 拷贝参数），本项目材质是世界级
	// BiomeMaterial，用不到块参数。
	bCreateMaterialInstances = false;

#if WITH_EDITOR
	// 每颗行星默认用项目顶点色材质。仅编辑器有效：打包后实例自带序列化值。
	// M_GeoPlanet 不在这里查：资产还没建时 FObjectFinder 会在 CDO 上打 Error，且 static 缓存住
	// 后即使资产后来生成也不会再找；父材质在建 MID 时由 ChooseBiomeParentMaterial 决定。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlanetMaterialFinder(
		TEXT("/Game/Code/Planet/Materials/M_PlanetVertexColor.M_PlanetVertexColor"));
	if (PlanetMaterialFinder.Succeeded())
	{
		VoxelMaterial = PlanetMaterialFinder.Object;
	}
#endif

	// 生成器在 RebuildPlanet 里懒创建：构造函数里建会在 CDO 上实例化子对象，
	// 保存时报 "Illegal reference to private object"。

	// LOD Invoker 挂在 WorldRoot 下，变换随组件树自动跟随。
	LODInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("PlanetLODInvoker"));
	LODInvoker->SetupAttachment(RootComponent);
	// 只做 LOD 基准；碰撞与导航 Invoker 应由贴近表面的 Pawn 携带。
	LODInvoker->bUseForLOD = true;
	LODInvoker->bUseForCollisions = false;
	LODInvoker->bUseForNavmesh = false;
	LODInvoker->bUseForEvents = false;
	LODInvoker->bUseForPriorities = false;
	// 占位值，RebuildPlanet 会按半径重算。
	LODInvoker->LODToSet = 6;
	LODInvoker->LODRange = 150000.f;

	// 天光不是可选项：没有它环境光永远是同一张静态立方图。骨架留 C++，蓝图不用管。
	// 组件本身就是那盏灯（USkyLightComponent 子类），挂在根下随行星移动。
	PlanetSkyLight = CreateDefaultSubobject<UPlanetSkyLightComponent>(TEXT("PlanetSkyLight"));
	PlanetSkyLight->SetupAttachment(RootComponent);

	// 恒星灯驱动不再随类创建：代理预制体下由代理持有，独立行星在 BeginPlay 里自己补。

	// 大气壳、矿物观察与表面编辑网格由 BP_VoxelPlanet 挂载；原生实例不带，
	// 编辑相关调用走 FindEditGrid()，找不到就跳过。
}

float AVoxelPlanetActor::GetScaledRadiusVoxels() const
{
	// 只采用等比缩放（非等比会剪切体素网格）。已知不一致：网格世界半径是
	// Radius·S²·VoxelSize，而 GetSurfaceRadiusCm 返回 Radius·S·VoxelSize，
	// S≠1 时会埋进星体；本关卡 S=1。修法涉及哈希/重建契约，不能只改一行。
	const float UniformScale = GetActorScale3D().GetAbsMax();
	return Radius * UniformScale;
}

int32 AVoxelPlanetActor::ComputeOrbitLOD() const
{
	// 最粗三角边长（2^LOD 体素）约半径 1/16，弦高误差 ~R/2048。
	// 1km 星得 LOD 6（64m 边长），100m 星得 LOD 3，6378km 星得 LOD 18。
	// 上界 25 就是「块边长 ≈ 1 体素」之上限；写 8 会把大星球压到爆块数。
	const float ScaledRadius = FMath::Max(GetScaledRadiusVoxels(), 1.f);
	return FMath::Clamp(FMath::RoundToInt(FMath::Log2(ScaledRadius / 16.f)), 0, 25);
}

void AVoxelPlanetActor::RefreshLODInvoker()
{
	if (!LODInvoker)
	{
		return;
	}

	// 拖动星球时每帧都调：没有任何输入变化、invoker 也没被手改，就别去刷全局 invoker。
	const float RangeSource = GetScaledRadiusVoxels() * FMath::Max(VoxelSize, 0.0001f);
	const int32 DesiredLOD = ComputeOrbitLOD();

	// geo 模式下比自动档更细会把整片 invoker 范围压到 2^LOD 体素块，撞
	// voxel.renderer.MaxRenderOctreeChunks 后渲染八叉树停更。不替用户改值，只提示。
	if (IsGeoMode() && LODInvoker->LODToSet >= 0 && LODInvoker->LODToSet < DesiredLOD)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AVoxelPlanetActor %s: 行星 invoker LOD %d 比自动档 %d 更细；若出现 Render octree update was stopped! 就调大 voxel.renderer.MaxRenderOctreeChunks"),
			*GetName(), LODInvoker->LODToSet, DesiredLOD);
	}

	const bool bFirstSync = !bLODInvokerSynced;
	const bool bRangeSourceChanged = bFirstSync || !FMath::IsNearlyEqual(LastAppliedRangeSource, RangeSource);
	// 值还等于上次写入的 → 归系统；被人改过 → 归用户，之后不再覆盖。首次同步没有历史可比，
	// 拿系统值当基准 —— 否则第一次手改会被「首次同步」吞掉。
	const bool bHandEdited = LODInvoker->LODToSet != (bFirstSync ? DesiredLOD : LastWrittenLODToSet);

	if (!bRangeSourceChanged && !bHandEdited)
	{
		return;
	}

	bLODInvokerSynced = true;
	LastAppliedRangeSource = RangeSource;

	if (bRangeSourceChanged)
	{
		// LOD Range 是纯几何量，只能跟半径 / VoxelSize 走，否则 LOD 覆盖不到整颗星。
		LODInvoker->LODRange = RangeSource * 1.25f + 1000.f;
	}

	// LOD to Set 只在首次同步时由系统写；之后面板里手改的值归用户，重建不再覆盖。
	if (bHandEdited)
	{
		// Invoker 自身没有 PostEditChangeProperty 刷新，这里顺手让它立刻生效。
		LastWrittenLODToSet = LODInvoker->LODToSet;
	}
	else if (bFirstSync)
	{
		LODInvoker->LODToSet = DesiredLOD;
		LastWrittenLODToSet = DesiredLOD;
		// 实际层级 = min(LODToSet, MaxLOD)：MaxLOD 更低会把值压回去。只在系统写入时抬一次，
		// 之后用户把 MaxLOD 调低是故意的（更细），不再顶回去。
		if (MaxLOD < DesiredLOD)
		{
			MaxLOD = FMath::Clamp(DesiredLOD, 0, 25);
			if (IsCreated())
			{
				UVoxelBlueprintLibrary::ApplyLODSettings(this);
			}
		}
	}

	// 面板上两个字段分开填很容易撞上「取了小值」：不改你的数，只把实际生效值说清楚。
	if (MaxLOD < LODInvoker->LODToSet)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("AVoxelPlanetActor: %s 的 MaxLOD %d 低于 invoker LOD %d，实际层级按较小值 %d 生效"),
			*GetName(), MaxLOD, LODInvoker->LODToSet, MaxLOD);
	}

	UVoxelInvokerComponentBase::RefreshAllVoxelInvokers();
}

void AVoxelPlanetActor::RepairLegacyRootComponent()
{
	// 基类构造时把 WorldRoot 立为根组件（全部网格挂其下）；旧实例的 RootComponent
	// 仍是自建的 PlanetRoot，WorldRoot 游离 → 网格画在世界原点，而 GlobalToLocal /
	// invoker 换算按 Actor 位置算，两者错位使 LODBounds 永不与 OctreeBounds 相交，
	// LOD 完全失效。修法：恢复「WorldRoot 是根组件」，沿用旧根的位置。
	if (!WorldRoot || GetRootComponent() == WorldRoot.Get())
	{
		return;
	}

	USceneComponent* const LegacyRoot = GetRootComponent();
	const FTransform LegacyTransform = LegacyRoot->GetComponentTransform();

	UE_LOG(LogTemp, Warning,
		TEXT("AVoxelPlanetActor: %s 的根组件是遗留的 %s 而非 WorldRoot，修正组件树（体素网格此前画在世界原点、LOD 失效）"),
		*GetName(), *LegacyRoot->GetName());

	// 先脱后摆，顺序不能反：还挂着时 SetWorldTransform 写的是相对变换，
	// 紧接着的 SetRootComponent 会把它当成世界变换用。
	WorldRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	WorldRoot->SetWorldTransform(LegacyTransform);
	SetRootComponent(WorldRoot.Get());

	// LOD Invoker 跟着搬到新根并保持世界位置，否则球心偏出行星中心、LOD 基准错。
	if (LODInvoker && LODInvoker->GetAttachParent() != WorldRoot.Get())
	{
		LODInvoker->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		LODInvoker->AttachToComponent(WorldRoot.Get(), FAttachmentTransformRules::KeepWorldTransform);
	}
}

FString AVoxelPlanetActor::MakePlanetSaveObjectAssetPath(const FString& MapName, const FString& ActorLabel)
{
	// PIE 世界名形如 UEDPIE_0_UniverseMap；不剥前缀则每次开 PIE 路径都变。
	FString Map = MapName;
	FString PiePrefix;
	FString PieRest;
	if (Map.StartsWith(TEXT("UEDPIE_")) &&
		Map.Split(TEXT("_"), &PiePrefix, &PieRest, ESearchCase::CaseSensitive, ESearchDir::FromStart))
	{
		FString PieIndex;
		FString MapWithoutIndex;
		if (PieRest.Split(TEXT("_"), &PieIndex, &MapWithoutIndex))
		{
			Map = MapWithoutIndex;
		}
	}

	// 资产名只允许字母数字与下划线；空格、冒号、斜杠会让 CreatePackage 失败。
	auto Sanitize = [](const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (const TCHAR Char : In)
		{
			Out.AppendChar((FChar::IsAlnum(Char) || Char == TEXT('_')) ? Char : TEXT('_'));
		}
		return Out;
	};

	const FString SanitizedMap = Sanitize(Map);
	const FString SanitizedLabel = Sanitize(ActorLabel);
	const FString SafeMap = SanitizedMap.IsEmpty() ? FString(TEXT("Untitled")) : SanitizedMap;
	const FString SafeLabel = SanitizedLabel.IsEmpty() ? FString(TEXT("Planet")) : SanitizedLabel;

	return FString::Printf(TEXT("/Game/Code/Maps/Saves/PlanetVoxel/%s/%s"), *SafeMap, *SafeLabel);
}

UMaterialInterface* AVoxelPlanetActor::StripMaterialInstanceDynamic(UMaterialInterface* Candidate)
{
	if (!Candidate)
	{
		return nullptr;
	}

	// 只沿 MID 往上走，剥到 MaterialInstanceConstant 就停 —— 再剥会丢掉用户在
	// MIC 里设的参数。上限 8 层只防脏数据成环，正常链长 1~2 层。
	UMaterialInterface* Current = Candidate;
	for (int32 Depth = 0; Depth < 8; ++Depth)
	{
		const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Current);
		if (!Dynamic)
		{
			return Current;
		}

		Current = Dynamic->Parent;
		if (!Current)
		{
			// 父为空的 MID 什么都剥不出来：返回 nullptr 让调用方兜底，别递坏 MID。
			return nullptr;
		}
	}

	return nullptr;
}

UMaterialInstanceDynamic* AVoxelPlanetActor::CreateBiomeMaterialMID(UMaterialInterface* Parent)
{
	// Outer 必须是瞬态包，理由见头文件：插件在编辑器态把这张 MID 直接登记进
	// 全局管理器（GC root），Outer 指向行星 Actor 会把整张旧关卡钉住。
	return UMaterialInstanceDynamic::Create(Parent, GetTransientPackage(), TEXT("BiomeMaterialMID"));
}

UTexture2D* AVoxelPlanetActor::CreatePaletteTexture(const TArray<FColor>& Pixels)
{
	if (Pixels.Num() != GeoPalette::Width * GeoPalette::Rows)
	{
		return nullptr;
	}

	// CreateTransient 的 Outer 就是瞬态包；理由见头文件（切图泄漏）。
	UTexture2D* Texture = UTexture2D::CreateTransient(
		GeoPalette::Width, GeoPalette::Rows, PF_B8G8R8A8, TEXT("GeoPalette"));
	if (!Texture || !Texture->GetPlatformData() || Texture->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}

	// 与 biome cubemap 同约定：CreateTransient 还没有 RHI 资源，直接写 Mip0 再 UpdateResource。
	Texture->SRGB = true;
	// 编号查表必须最近邻：双线性会把矿 1 与 0 号黑混色；Wrap 会让 255 绕回 0。
	Texture->Filter = TF_Nearest;
	Texture->AddressX = TA_Clamp;
	Texture->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
	uint8* Dest = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!Dest)
	{
		return nullptr;
	}
	FMemory::Memcpy(Dest, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();

	Texture->UpdateResource();
	return Texture;
}

UMaterialInterface* AVoxelPlanetActor::ChooseBiomeParentMaterial(UMaterialInterface* Fallback)
{
	// LoadObject 找不到时返回 null 且不缓存失败：资产由构建器稍后生成也能立刻生效。
	// LOAD_NoWarn/Quiet：M_GeoPlanet 在构建器跑之前不存在，缺资产不是错误，别刷警告。
	if (UMaterialInterface* GeoMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/Code/Planet/Materials/M_GeoPlanet.M_GeoPlanet"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		return GeoMaterial;
	}
	return Fallback;
}

void AVoxelPlanetActor::BindGeoPalette(UMaterialInstanceDynamic* MID)
{
	if (!MID || !GeoSnapshot.IsValid())
	{
		return;
	}

	const TArray<FGeoMineralInfo>& Minerals = GeoSnapshot->Minerals;
	uint64 PaletteHash = GeoHash::Mix64(uint64(Minerals.Num()));
	for (const FGeoMineralInfo& Mineral : Minerals)
	{
		PaletteHash = GeoHash::Combine(PaletteHash, uint64(Mineral.Color.ToPackedARGB()));
	}

	if (!GeoPaletteTexture || GeoPaletteHash != PaletteHash)
	{
		// 旧的瞬态纹理在赋值时丢掉最后一个引用，交给 GC。
		GeoPaletteTexture = CreatePaletteTexture(GeoPalette::BuildLUT(Minerals));
		GeoPaletteHash = GeoPaletteTexture ? PaletteHash : 0;
	}

	// 材质里没这个参数时 SetTextureParameterValue 静默不生效。
	if (GeoPaletteTexture)
	{
		MID->SetTextureParameterValue(TEXT("GeoPalette"), GeoPaletteTexture);
	}
}

#if WITH_EDITOR
void AVoxelPlanetActor::EnsureSaveObject()
{
	if (SaveObject)
	{
		return;
	}

	// geo 模式不落盘：地形随阶段迭代整体重算，旧编辑会盖在新地表上，读回去就是坏星。
	if (IsGeoMode())
	{
		return;
	}

	UWorld* World = GetWorld();
	// PIE 世界从编辑器实例复制，SaveObject 会继承；再找一遍既没必要也不可预测。
	if (!World || World->WorldType != EWorldType::Editor)
	{
		return;
	}

	// 代理预制体：存档沿用代理的名字，子天体的实例名是自动生成且不稳定的。
	// 挂接可能晚于这里，此时用 outer 组件反查 Owner。
	AActor* LabelActor = GetAttachParentActor();
	if (!LabelActor)
	{
		LabelActor = GetOwner();
	}
	if (!LabelActor)
	{
		LabelActor = GetTypedOuter<UActorComponent>() ? GetTypedOuter<UActorComponent>()->GetOwner() : this;
	}
	const FString SaveLabel = CacheKey.IsEmpty()
		? LabelActor->GetActorLabel(/*bIncludeFolder=*/false)
		: CacheKey;
	const FString AssetPath = MakePlanetSaveObjectAssetPath(World->GetMapName(), SaveLabel);

	UVoxelWorldSaveObject* Loaded = nullptr;
	bool bCreatedNew = false;
	if (FPackageName::DoesPackageExist(AssetPath))
	{
		Loaded = LoadObject<UVoxelWorldSaveObject>(nullptr, *AssetPath);
	}
	else
	{
		UPackage* Package = CreatePackage(*AssetPath);
		if (Package)
		{
			Loaded = NewObject<UVoxelWorldSaveObject>(Package, *FPaths::GetBaseFilename(AssetPath),
				RF_Public | RF_Standalone | RF_Transactional);
		}
		if (Loaded)
		{
			bCreatedNew = true;
			// 新建 SaveObject 的 Depth 是 0，而未存过体素的压缩存档是 -1；不补会被
			// 当成 Invalid Save Object。
			Loaded->CopyDepthFromSave();
			FAssetRegistryModule::AssetCreated(Loaded);
		}
	}

	if (!Loaded)
	{
		if (!bLoggedMissingSaveObject)
		{
			bLoggedMissingSaveObject = true;
			UE_LOG(LogTemp, Warning,
				TEXT("AVoxelPlanetActor %s: 创建 / 加载存档资产失败（%s），编辑仍只存在内存里。"),
				*GetName(), *AssetPath);
		}
		return;
	}

	Modify();
	SaveObject = Loaded;
	bLoggedMissingSaveObject = false;

	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor %s: 绑定存档资产 %s"), *GetName(), *AssetPath);

	// 新资产只在内存包里，顺手写盘；已存在的资产不动，避免白改 mtime。
	if (bCreatedNew)
	{
		SaveSaveObjectToDisk();
	}
}

bool AVoxelPlanetActor::SaveSaveObjectToDisk()
{
	if (!SaveObject)
	{
		return false;
	}

	UPackage* Package = SaveObject->GetOutermost();
	if (!Package || Package->HasAnyFlags(RF_Transient))
	{
		return false;
	}

	const FString FileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), /*Tree=*/true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	// SAVE_NoError：自动化 / 无头跑不能弹「保存失败」对话框，失败由下面的日志暴露。
	SaveArgs.SaveFlags = SAVE_NoError;
	// 不加 SAVE_Async：它只挪文件写入，序列化仍在调用线程，且不保证进程退出前写完。

	if (UPackage::SavePackage(Package, SaveObject, *FileName, SaveArgs))
	{
		bLoggedSaveDiskFailure = false;
		return true;
	}

	if (!bLoggedSaveDiskFailure)
	{
		bLoggedSaveDiskFailure = true;
		UE_LOG(LogTemp, Warning,
			TEXT("AVoxelPlanetActor %s: 存档资产写盘失败（%s），本次运行的编辑只在内存里。"),
			*GetName(), *FileName);
	}
	return false;
}
#endif

void AVoxelPlanetActor::OnConstruction(const FTransform& Transform)
{
	// 先修组件树再让基类跑：基类 OnConstruction 可能当场按错误根组件建预览世界。
	RepairLegacyRootComponent();

#if WITH_EDITOR
	// 必须排在 Super::OnConstruction / RebuildPlanet 之前：后者 CreateWorld 时
	// 当场按 SaveObject 加载，晚赋值本次运行仍是空白世界。
	EnsureSaveObject();
#endif

	Super::OnConstruction(Transform);

#if WITH_EDITOR
	// World Partition 会按包围盒把空间加载 Actor 烘进流送单元，跨边界就销毁重建。
	// 退出后烘进常驻单元，下次保存地图生效。
	if (bAlwaysLoaded && GetIsSpatiallyLoaded() && CanChangeIsSpatiallyLoadedFlag())
	{
		Modify();
		SetIsSpatiallyLoaded(false);
	}
#endif

	RebuildPlanet();
}

void AVoxelPlanetActor::BeginPlay()
{
	Super::BeginPlay();

	// 独立行星（没有代理父级）自己补一份太阳光驱动；代理下的行星由代理负责，避免双驱动。
	if (!GetAttachParentActor() && !SunLightDriver)
	{
		SunLightDriver = NewObject<UPlanetSunLightComponent>(this, TEXT("SunLightDriver"));
		SunLightDriver->RegisterComponent();
	}

	RebuildPlanet();
}

#if WITH_EDITOR
void AVoxelPlanetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 基类先跑：它负责自己 meta=(Recreate)/(UpdateLODs) 的属性及其世界重建；
	// 这些属性不放进 ComputeParamsHash，否则同一次编辑会重建两次。
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 下拉选中的是「中文名（Id）」标签，这里立刻规范成标签形式；老值（裸 Id）也一并升级。

	if (IsGeoMode() && ApplyGeoDebugOverlay() && IsCreated())
	{
		UVoxelBlueprintLibrary::ApplyNewMaterials(this);
	}

	// 半径两套单位互相同步：米是给手填的，体素是生成器真正吃的。
	const FName ChangedProperty = PropertyChangedEvent.GetPropertyName();
	if (ChangedProperty == GET_MEMBER_NAME_CHECKED(AVoxelPlanetActor, RadiusMeters))
	{
		Radius = FMath::Max(16.f, FMath::RoundToFloat(RadiusMeters * 100.f / FMath::Max(VoxelSize, 0.01f)));
		// 回写对齐后的值：体素是整数，改米不一定能精确落回。
		RadiusMeters = Radius * VoxelSize / 100.f;
	}
	else if (ChangedProperty == GET_MEMBER_NAME_CHECKED(AVoxelPlanetActor, Radius)
		|| ChangedProperty == FName(TEXT("VoxelSize")))
	{
		RadiusMeters = Radius * VoxelSize / 100.f;
	}

	// OnConstruction 只在移动/添加时重跑，细节面板改参数要在这里重建。
	RebuildPlanet();
}
#endif

void AVoxelPlanetActor::RebuildPlanet()
{
	// 没有世界就没有体素世界可建：蓝图 CDO（Default__Xxx_C）不在任何世界，
	// GetWorld() 为空而 CreateWorld 第一件事就是取其 WorldType，会空指针违例。
	// 在类默认值面板改属性就会走到这里。早退不丢东西，进世界后 OnConstruction 重建。
	UWorld* const PreviewWorld = GetWorld();
	if (!PreviewWorld)
	{
		return;
	}

	// 蓝图编辑器/预览的瞬态世界不烘焙：缓存键会落在 PC_Transient_*，永远不命中，
	// 每次打开蓝图都全量重烤一分多钟。真正的编辑器关卡与 PIE 世界不受影响。
	if (PreviewWorld->GetMapName() == TEXT("Transient"))
	{
		return;
	}

	// 也在这里修一次（BeginPlay / PostEditChangeProperty 入口同样可能撞上旧树），
	// 且必须排在哈希守卫之前，否则修正会被一起吞掉。函数自带已是新树就早退。
	RepairLegacyRootComponent();

	// LOD 档位由半径派生，必须先于哈希修正。
	if (IsGeoMode())
	{
		SyncGeoDimensions();
	}

	// Invoker 刷新排在哈希守卫之前：这三项只影响 LOD 提示、不必重建整颗星，
	// 函数自带值没变就早退。
	RefreshLODInvoker();

	// 旋转不进参数哈希，但地表立方图按世界方向采样：转过行星要重烘底图（内部按哈希早退）。
	if (IsGeoMode() && ApplyGeoDebugOverlay() && IsCreated())
	{
		UVoxelBlueprintLibrary::ApplyNewMaterials(this);
	}

	// 细节面板一次编辑会同时触发 PostEditChangeProperty 和 OnConstruction；
	// 没有守卫就重建两次，生成时间翻倍。复查 IsCreated() 保证全新世界总能构建。
	const uint64 ParamsHash = ComputeParamsHash();
	if (ParamsHash == LastParamsHash && IsCreated())
	{
		return;
	}
	LastParamsHash = ParamsHash;

	// 重建一次打一行：拖动星球不应产生新行，每帧刷屏即哈希守卫失效。
	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor: 重建 %s（参数哈希 %llu）"), *GetName(), ParamsHash);

	// 所有行星都走地质流水线；没挂配方的旧关卡不建世界。
	if (!IsGeoMode())
	{
		return;
	}

	RebuildGeoPlanet();
}

void AVoxelPlanetActor::SyncGeoDimensions()
{
	if (!IsGeoMode())
	{
		return;
	}

	// 半径换量级后，按旧尺度调的 LOD 档位全部失效：MaxLOD 6 配 40000 体素半径会让渲染
	// 八叉树撞 voxel.renderer.MaxRenderOctreeChunks（100 万，报 "Render octree update
	// was stopped!"）。MaxLOD 只封顶「最粗」，调大不伤近处细节。
	const float AutoLODRadius = FMath::Max(GetScaledRadiusVoxels(), 1.f);
	const int32 AutoLOD = FMath::Clamp(
		FMath::RoundToInt(FMath::Log2(AutoLODRadius / 16.f)), 0, 8);
	if (MaxLOD < AutoLOD + 4)
	{
		UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor %s: MaxLOD %d → %d（半径 %.0f 体素）"),
			*GetName(), MaxLOD, AutoLOD + 4, AutoLODRadius);
		MaxLOD = AutoLOD + 4;
	}
}

FGeoBakeStartedEvent AVoxelPlanetActor::GeoBakeStarted;
FGeoBakeFinishedEvent AVoxelPlanetActor::GeoBakeFinished;

void AVoxelPlanetActor::BakeNow()
{
	// 强制重烘：丢掉内存快照，让 RebuildGeoPlanet 走 StartGeoBake。
	bForceRebake = true;
	bGeoBakeFailed = false;
	GeoSnapshot.Reset();
	StartGeoBake();
}

void AVoxelPlanetActor::StartGeoBake()
{
	const UPlanetRecipe* Recipe = GeoRecipe.Get();
	if (bGeoBakeInFlight || !Recipe)
	{
		return;
	}
	bGeoBakeInFlight = true;
	GeoBakeStarted.Broadcast(this);

	// ToConfig 是唯一一次碰 UObject：烘焙线程只拿 POD。
	FPlanetRecipeConfig Config = Recipe->ToConfig(double(GetScaledRadiusVoxels()));
	// 编辑器里多留一份中间场给调试图层；打包后的游戏不需要，也不该多占 12 MB。
	Config.bKeepDebugFields = GIsEditor;
	PendingCacheHash = Config.Hash();
#if WITH_EDITOR
	// 命中缓存就直接用：反序列化约 1–2 s，比重新烘焙快一个数量级。
	if (!bForceRebake)
	{
		const int32 CacheRadius = FMath::RoundToInt(GetScaledRadiusVoxels());
		const FString CachePath = CacheKey.IsEmpty()
			? GeoBakeCache::PackagePathFor(*this)
			: GeoBakeCache::PackagePathFor(*this, CacheKey);
		UE_LOG(LogTemp, Display, TEXT("[烘焙缓存] 查询 %s"), *CachePath);
		if (TSharedPtr<const FPlanetSnapshot> Cached =
			GeoBakeCache::Load(CachePath, int64(PendingCacheHash), CacheRadius))
		{
			bSnapshotFromCache = true;
			OnGeoBakeFinished(Cached, 0.0);
			bSnapshotFromCache = false;
			return;
		}
	}
	bForceRebake = false;
#endif
	TWeakObjectPtr<AVoxelPlanetActor> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Config]()
	{
		const double Start = FPlatformTime::Seconds();
		FPlanetBakeReport Report;
		TSharedPtr<const FPlanetSnapshot> Snapshot = FPlanetBakePipeline::MakeDefault().Bake(Config, Report);
		const double Seconds = FPlatformTime::Seconds() - Start;
		for (const FString& Error : Report.Errors)
		{
			UE_LOG(LogTemp, Error, TEXT("[地质] %s"), *Error);
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Snapshot, Seconds]()
		{
			if (AVoxelPlanetActor* Self = WeakThis.Get())
			{
				Self->OnGeoBakeFinished(Snapshot, Seconds);
			}
		});
	});
}

void AVoxelPlanetActor::OnGeoBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double BakeSeconds)
{
	bGeoBakeInFlight = false;

	// 回调可能晚于 PIE 结束 / 关卡切换，那时再建世界会撞上拆除中的世界。
	UWorld* const World = GetWorld();
	if (!World || World->bIsTearingDown)
	{
		GeoBakeFinished.Broadcast(this, false, BakeSeconds);
		return;
	}
	if (!InSnapshot)
	{
		// 失败不自动重试：否则 RebuildPlanet ↔ StartGeoBake 会无限循环。
		bGeoBakeFailed = true;
		UE_LOG(LogTemp, Error, TEXT("AVoxelPlanetActor %s: 地质烘焙失败，本颗星不建世界"), *GetName());
		GeoBakeFinished.Broadcast(this, false, BakeSeconds);
		return;
	}

	GeoSnapshot = InSnapshot;
	GeoBakeFinished.Broadcast(this, true, BakeSeconds);
#if WITH_EDITOR
	// 只有真正烘焙出来的结果才回写缓存；命中的不再重复序列化。
	if (!bSnapshotFromCache)
	{
		GeoBakeCache::Store(GeoBakeCache::PackagePathFor(*this), *GeoSnapshot,
			int64(PendingCacheHash), FMath::RoundToInt(GetScaledRadiusVoxels()));
	}
#endif
	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor %s: 地质烘焙完成 %.2f s，半径 %.0f 体素，快照 %.1f MB，哈希 %016llx"),
		*GetName(), BakeSeconds, float(Radius),
		double(GeoSnapshot->GetAllocatedSize()) / (1024.0 * 1024.0), GeoSnapshot->ContentHash);

	// 快照换了必重建；哈希里虽然混了 ContentHash，抹零让语义显式。
	LastParamsHash = 0;
	RebuildPlanet();
}

void AVoxelPlanetActor::RebuildGeoPlanet()
{
	// 行星半径（含缩放）是唯一真源：快照按旧半径烘的就作废重烘。
	if (GeoSnapshot.IsValid() && !FMath::IsNearlyEqual(GeoSnapshot->R0, double(GetScaledRadiusVoxels()), 0.5))
	{
		GeoSnapshot.Reset();
		bGeoBakeFailed = false;
	}
	if (!GeoSnapshot.IsValid())
	{
		// 没快照建出来是一颗空气球，白跑一次世界创建。
		if (!bGeoBakeFailed)
		{
			StartGeoBake();
		}
		return;
	}

	if (!GeoGenerator)
	{
		GeoGenerator = NewObject<UPlanetGeoGenerator>(this, TEXT("GeoGenerator"));
	}
	GeoGenerator->SetSnapshot(GeoSnapshot);
	GeoGenerator->DebugColor = EPlanetGeoDebugColor::None;
	GeoGenerator->Cutaway = EPlanetGeoCutaway::None;
	GeoGenerator->SurfaceLayer = NAME_None;
	GeoGenerator->VolumeLayer = NAME_None;
	// 材质必须在建世界前就位：渲染器创建时快照它。
	ApplyGeoDebugOverlay();

	// 与旧路径同一收紧策略：边长收到半径 3 倍并取 2 的幂。
	SetWorldSize(FMath::RoundUpToPowerOfTwo(FMath::CeilToInt(GetScaledRadiusVoxels() * 3.f)));
	Generator = FVoxelGeneratorPicker(GeoGenerator.Get());

	UE_LOG(LogTemp, Log,
		TEXT("AVoxelPlanetActor: 地质世界边长 %u 体素，半径 %.0f 体素，MaxLOD %d，行星 invoker LOD %d 范围 %.0f 厘米"),
		WorldSizeInVoxel, GetScaledRadiusVoxels(), MaxLOD,
		LODInvoker ? LODInvoker->LODToSet : -1,
		LODInvoker ? LODInvoker->LODRange : 0.f);

	if (IsCreated())
	{
		DestroyWorld();
	}

#if WITH_EDITOR
	// 编辑器世界必须走 Toggle()：OnPreBeginPIE 拆世界后只有 Toggle 会建回来。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		Toggle();
		return;
	}
#endif

	UWorld* const World = GetWorld();
	if (World && World->IsGameWorld())
	{
		bCreateGlobalPool = false;
		if (!UVoxelBlueprintLibrary::IsWorldVoxelPoolCreated(World))
		{
			UVoxelBlueprintLibrary::CreateWorldVoxelThreadPool(
				World,
				PriorityCategories,
				PriorityOffsets,
				NumberOfThreads,
				bConstantPriorities);
		}
	}

	CreateWorld(FVoxelWorldCreateInfo());
}

void AVoxelPlanetActor::PreSave(FObjectPreSaveContext ObjectSaveContext)
{
	// 运行期 VoxelMaterial 指向瞬态 MID，直接存会写成空引用、下次加载是灰球。
	// 存前换回用户材质，PostSaveRoot 再换回来（期间渲染仍是 MID）。
	if (BiomeMaterial && VoxelMaterial == BiomeMaterial)
	{
		VoxelMaterial = BaseVoxelMaterial;
		bRestoreVoxelMaterialAfterSave = true;
	}

	Super::PreSave(ObjectSaveContext);
}

void AVoxelPlanetActor::PostSaveRoot(FObjectPostSaveRootContext ObjectSaveContext)
{
	Super::PostSaveRoot(ObjectSaveContext);

	if (bRestoreVoxelMaterialAfterSave)
	{
		bRestoreVoxelMaterialAfterSave = false;

		// MID 可能已被 GC（保存期间换关卡）；为空就保持父材质，下次重建会重装。
		if (BiomeMaterial)
		{
			VoxelMaterial = BiomeMaterial;
		}
	}
}

void AVoxelPlanetActor::InvalidateAndRebuild()
{
	// 触发这次重建的是地形资产参数，不参与本 Actor 的哈希；不抹掉 LastParamsHash
	// 守卫会吞掉重建。
	LastParamsHash = 0;

	// geo 模式的「地形参数」就是配方内容：配方没有哈希，只能丢快照重烘。
	if (IsGeoMode())
	{
		GeoSnapshot.Reset();
		bGeoBakeFailed = false;
	}

	RebuildPlanet();
}

void AVoxelPlanetActor::NotifyVoxelEdited()
{
	bHasUnsavedEdits = true;
}

void AVoxelPlanetActor::RequestSaveEdits()
{
	// 不落盘的星球连「缺 SaveObject」的警告都不该刷。
	if (IsGeoMode() || !bHasUnsavedEdits)
	{
		return;
	}

	// 不在这里清标志：没有 SaveObject / 世界没建时保存没成功，脏数据要留着等
	// 下次或 EndPlay 再试；「只警告一次」由 bLoggedMissingSaveObject 负责。
	if (!SaveObject)
	{
		if (!bLoggedMissingSaveObject)
		{
			bLoggedMissingSaveObject = true;
			UE_LOG(LogTemp, Warning,
				TEXT("AVoxelPlanetActor %s 没有 SaveObject，编辑只存在于本次运行的内存里。"),
				*GetName());
		}
		return;
	}

	if (!IsCreated())
	{
		return;
	}

	// 已经有一份任务在读体素数据：只记「回来再存一次」，不并发第二份。
	if (SaveJob.IsValid())
	{
		bSavePending = true;
		return;
	}

	// 先清脏：任务跑起来之后的新编辑会把它重新置起来，收尾时据此再排一次。
	bHasUnsavedEdits = false;
	bSavePending = false;

	UWorld* const World = GetWorld();

	// 没有世界就没有定时器可轮询，世界拆除中后台任务回来也没人收尾，都退回同步。
	// 「拆除中」不是防御代码：走到这里说明同步那次没成，再起后台任务会读到正在释放的数据。
	if (!World || World->bIsTearingDown)
	{
		bHasUnsavedEdits = true;
		SaveEditsToSaveObject();
		return;
	}

	// 捕获弱引用：任务在后台跑，Actor 可能已经进入销毁流程。
	TWeakObjectPtr<AVoxelPlanetActor> WeakThis(this);
	SaveJob = Async(EAsyncExecution::ThreadPool,
		[WeakThis]() -> TSharedPtr<FVoxelCompressedWorldSave>
		{
			AVoxelPlanetActor* const Self = WeakThis.Get();
			if (!Self)
			{
				// 任务排上之前 Actor 就没了：返回空，收尾时据此把脏标志还回去。
				return nullptr;
			}

			// GetSave 持读锁、压缩是纯数据变换，都不需要游戏线程；插件自己的
			// GetCompressedSaveAsync 也是在工作线程跑同一段。
			TSharedPtr<FVoxelCompressedWorldSave> Compressed =
				MakeShared<FVoxelCompressedWorldSave>();
			UVoxelDataTools::GetCompressedSave(Self, *Compressed);
			return Compressed;
		});

	// 轮询而非 AsyncTask(GameThread)：EndPlay 要先等任务跑完再同步兜底，
	// 轮询把顺序交给游戏线程，不依赖任务队列投递时序。
	World->GetTimerManager().SetTimer(SavePollTimerHandle,
		FTimerDelegate::CreateUObject(this, &AVoxelPlanetActor::PollSaveJob),
		0.05f, /*bLoop=*/true);
}

void AVoxelPlanetActor::PollSaveJob()
{
	if (!SaveJob.IsValid() || !SaveJob.IsReady())
	{
		return;
	}

	FinishSaveJob();

	if (bSavePending)
	{
		RequestSaveEdits();
	}
}

void AVoxelPlanetActor::FinishSaveJob()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SavePollTimerHandle);
	}

	if (!SaveJob.IsValid())
	{
		return;
	}

	// 后台任务只读体素数据，游戏线程在这里等它不会死锁。
	SaveJob.Wait();
	const TSharedPtr<FVoxelCompressedWorldSave> Compressed = SaveJob.Get();
	SaveJob.Reset();

	if (!Compressed || !SaveObject)
	{
		// 任务没产出（世界中途失效）：把脏标志还回去，EndPlay 还能再试。
		bHasUnsavedEdits = true;
		return;
	}

	const double FinishStartSeconds = FPlatformTime::Seconds();

	SaveObject->Save = MoveTemp(*Compressed);
	// Depth 不参与压缩，必须从 Save 补上；否则下次加载时世界尺寸对不上。
	SaveObject->CopyDepthFromSave();
	SaveObject->MarkPackageDirty();

#if WITH_EDITOR
	// MarkPackageDirty 只改内存脏标志，PIE 结束时资产副本直接丢弃，必须同步落盘。
	SaveSaveObjectToDisk();
#endif

	const double FinishMs = (FPlatformTime::Seconds() - FinishStartSeconds) * 1000.0;
	if (FinishMs >= 20.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Dig] 整星存档 %s：后台已完成，游戏线程收尾 %.0f ms"), *GetName(), FinishMs);
	}
}

void AVoxelPlanetActor::SaveEditsToSaveObject()
{
	// 与 EnsureSaveObject 同一理由：geo 测试星球的挖掘不持久化。
	if (IsGeoMode() || !bHasUnsavedEdits)
	{
		return;
	}

	if (!SaveObject)
	{
		if (!bLoggedMissingSaveObject)
		{
			bLoggedMissingSaveObject = true;
			UE_LOG(LogTemp, Warning,
				TEXT("AVoxelPlanetActor %s 没有 SaveObject，编辑只存在于本次运行的内存里。"),
				*GetName());
		}
		return;
	}

	if (!IsCreated())
	{
		return;
	}

	const double SaveStartSeconds = FPlatformTime::Seconds();

	FVoxelUncompressedWorldSave UncompressedSave;
	UVoxelDataTools::GetSave(this, UncompressedSave);
	const double GetSaveMs = (FPlatformTime::Seconds() - SaveStartSeconds) * 1000.0;

	UVoxelSaveUtilities::CompressVoxelSave(UncompressedSave, SaveObject->Save);
	const double CompressMs = (FPlatformTime::Seconds() - SaveStartSeconds) * 1000.0 - GetSaveMs;

	// Depth 不参与压缩，必须从 Save 补上；否则下次加载时世界尺寸对不上。
	SaveObject->CopyDepthFromSave();
	SaveObject->MarkPackageDirty();

#if WITH_EDITOR
	SaveSaveObjectToDisk();
#endif

	const double TotalMs = (FPlatformTime::Seconds() - SaveStartSeconds) * 1000.0;
	if (TotalMs >= 20.0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Dig] 整星存档（同步）%s：GetSave %.0f ms，压缩 %.0f ms，总 %.0f ms"),
			*GetName(), GetSaveMs, CompressMs, TotalMs);
	}

	// 到这里才算真的写进了 SaveObject，脏标志才能清。
	bHasUnsavedEdits = false;
}

void AVoxelPlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SavePollTimerHandle);
	}

	// 后台任务在跑就先等它收尾，再同步兜底之后的新编辑；两步都要，否则并发读或丢最后一笔。
	FinishSaveJob();
	SaveEditsToSaveObject();

	Super::EndPlay(EndPlayReason);
}

void AVoxelPlanetActor::PostLoad()
{
	Super::PostLoad();

	// 老存档没有 RadiusMeters：读盘后按体素值补一个，面板上才是当前真实半径。
	RadiusMeters = Radius * VoxelSize / 100.f;

	// CDO（Default__BP_VoxelPlanet_C）不持有生成器。CDO 上出现同名子对象后，各行星的
	// 同名子对象会按名字把它当原型（UObjectArchetype.cpp:127），保存外部 Actor 包时连带
	// harvest 出 "Illegal reference to private object"。这里清的是历史上已落盘的那份。
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		Generator = FVoxelGeneratorPicker();
	}

}

void AVoxelPlanetActor::ReloadGeology()
{
	InvalidateAndRebuild();
}

void AVoxelPlanetActor::RebuildStarLights()
{
	// 实现在蓝图（本类不知道 SunStars 容器）；按名字派发，找不到就静默不做事。
	if (UFunction* Rebuild = FindFunction(TEXT("RebuildStarLightsInternal")))
	{
		ProcessEvent(Rebuild, nullptr);
	}
}

float AVoxelPlanetActor::GetSurfaceRadiusCm() const
{
	// 半径以体素计，乘以单个体素的厘米尺寸才是世界单位。
	return GetScaledRadiusVoxels() * VoxelSize;
}

float AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const
{
	// 地表 = 快照的 R0 + 高程场；快照未到时退回基准球面。
	if (!GeoSnapshot.IsValid())
	{
		return GetSurfaceRadiusCm();
	}

	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	return float(PlanetShapeEval::SurfaceRadius(*GeoSnapshot, FVector3f(Direction))) * VoxelSize;
}

float AVoxelPlanetActor::GetMinSurfaceRadiusCm() const
{
	// 大气拿它当地面下界，必须给真正的最低点，否则整层壳浮空/陷地。
	if (!GeoSnapshot.IsValid())
	{
		return GetSurfaceRadiusCm();
	}

	return float(GeoSnapshot->MinSurfaceRadius) * VoxelSize;
}

float AVoxelPlanetActor::GetVoxelSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const
{
	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	// 解析半径既是种子，也是所有兜底的答案：没体素数据时它是唯一能给的。
	const float AnalyticRadiusCm = GetSurfaceRadiusCmAtDirection(Direction);
	if (!IsCreated() || VoxelSize <= 0.f)
	{
		return AnalyticRadiusCm;
	}

	// 体素空间：球心在原点，单位 = 体素；与世界空间只差等比缩放与平移。本函数假定 S=1。
	const double BaseRadiusVoxels = double(GetSurfaceRadiusCm()) / VoxelSize;
	const double AnalyticRadiusVoxels = double(AnalyticRadiusCm) / VoxelSize;
	if (AnalyticRadiusVoxels <= 0.0)
	{
		return AnalyticRadiusCm;
	}

	// 种子点：解析半径上的那个点（体素坐标）。
	const FVector HitLocal = Direction * AnalyticRadiusVoxels;

	// 预算：向外到 1.25 倍包围球，向里到球心前一格。不复用 ComputeMarchStepRange：
	// 它的上界按「射线原点在球外」定义，径向代入会把方向读反。
	const int32 MaxForwardSteps = FMath::Max(1,
		FMath::CeilToInt(BaseRadiusVoxels * 1.25 - AnalyticRadiusVoxels));
	const int32 MaxBackwardSteps = FMath::Max(1,
		FMath::CeilToInt(AnalyticRadiusVoxels) - 1);
	const int32 MaxAbsStep = FMath::Max(MaxForwardSteps, MaxBackwardSteps);

	const auto VoxelOf = [](const FVector& Point)
	{
		return FIntVector(
			static_cast<int32>(FMath::FloorToInt(Point.X)),
			static_cast<int32>(FMath::FloorToInt(Point.Y)),
			static_cast<int32>(FMath::FloorToInt(Point.Z)));
	};

	// 读锁按 128 体素一段取：整条径向线一把锁会锁住大半棵八叉树，体素线程全被卡住。
	constexpr int32 SegmentVoxels = 128;
	int32 LockedSign = 0;      // 当前锁覆盖的方向（±1），0 = 还没取
	int32 LockedThrough = 0;   // 已覆盖到 |Step| = LockedThrough
	TUniquePtr<FVoxelReadScopeLock> SegmentLock;

	const auto SampleEmptyAt = [&](const int32 Step) -> bool
	{
		const int32 Sign = Step >= 0 ? 1 : -1;
		const int32 AbsStep = FMath::Abs(Step);

		if (Sign != LockedSign || AbsStep > LockedThrough)
		{
			// 先放旧锁再取新锁：两把同时在手时，另一条线程按相反顺序取锁会死锁。
			SegmentLock.Reset();

			const int32 SegmentBegin = FMath::Max(AbsStep, 1);
			const int32 SegmentEnd =
				FMath::Min(SegmentBegin + SegmentVoxels - 1, MaxAbsStep);

			// 盒子要盖住 SegmentBegin−1 到 SegmentEnd 的采样点（Step 0 在第一段），
			// +2 是取整余量。
			const FVector PointA =
				HitLocal + Direction * (static_cast<double>(Sign) * (SegmentBegin - 1));
			const FVector PointB =
				HitLocal + Direction * (static_cast<double>(Sign) * SegmentEnd);
			const FIntVector ScanMin(
				FMath::FloorToInt(FMath::Min(PointA.X, PointB.X)) - 2,
				FMath::FloorToInt(FMath::Min(PointA.Y, PointB.Y)) - 2,
				FMath::FloorToInt(FMath::Min(PointA.Z, PointB.Z)) - 2);
			const FIntVector ScanMax(
				FMath::CeilToInt(FMath::Max(PointA.X, PointB.X)) + 2,
				FMath::CeilToInt(FMath::Max(PointA.Y, PointB.Y)) + 2,
				FMath::CeilToInt(FMath::Max(PointA.Z, PointB.Z)) + 2);

			// FVoxelIntBox 的 Max 是开区间，所以 +1。
			SegmentLock = MakeUnique<FVoxelReadScopeLock>(
				GetData(), FVoxelIntBox(ScanMin, ScanMax + FIntVector(1, 1, 1)),
				TEXT("PlanetVoxelSurfaceRadius"));

			LockedSign = Sign;
			LockedThrough = SegmentEnd;
		}

		const FVector Sample = HitLocal + Direction * static_cast<double>(Step);
		return GetData().GetValue(VoxelOf(Sample), /*LOD=*/0).IsEmpty();
	};

	double BoundaryStep = 0.0;
	if (!PlanetSurfaceRay::FindSurfaceBoundaryStep(
		MaxForwardSteps, MaxBackwardSteps, SampleEmptyAt, BoundaryStep))
	{
		// 走完全部预算都没翻转（挖穿或全实）。用 Verbose，每帧都会走到时 Warning 会刷满。
		UE_LOG(LogTemp, Verbose,
			TEXT("%s：方向 %s 走了 %d 步仍未找到空/实翻转，退回解析半径 %.1f cm。"),
			*GetName(), *Direction.ToCompactString(), MaxAbsStep, AnalyticRadiusCm);
		return AnalyticRadiusCm;
	}

	// 边界是两格的中点，误差 ≤ 1 体素。
	return static_cast<float>((AnalyticRadiusVoxels + BoundaryStep) * VoxelSize);
}

bool AVoxelPlanetActor::RaycastSurfaceCm(
	const FVector& RayOriginWorld,
	const FVector& RayDirectionWorld,
	const float MaxDistanceCm,
	FVector& OutHitLocationWorld,
	FVector& OutHitNormalWorld,
	float& OutDistanceCm) const
{
	const FVector Direction = RayDirectionWorld.GetSafeNormal();
	if (Direction.IsNearlyZero() || MaxDistanceCm <= 0.f || !IsCreated())
	{
		return false;
	}

	// 体素空间与世界空间只差等比缩放和行星位置，没有旋转；解析这一步全程用厘米。
	const FVector PlanetCenterWorld = GetActorLocation();
	const FVector RelativeOrigin = RayOriginWorld - PlanetCenterWorld;
	if (RelativeOrigin.IsNearlyZero())
	{
		return false;
	}

	constexpr int32 AnalyticIterations = 3;

	double AnalyticDistanceCm = -1.0;
	if (!PlanetSurfaceRay::SolveAnalyticHit(
		RelativeOrigin, Direction, static_cast<double>(GetSurfaceRadiusCm()),
		AnalyticIterations,
		[this](const FVector& UnitDirection)
		{
			return static_cast<double>(GetSurfaceRadiusCmAtDirection(UnitDirection));
		},
		AnalyticDistanceCm))
	{
		return false;
	}

	if (AnalyticDistanceCm < 0.0 || AnalyticDistanceCm > MaxDistanceCm)
	{
		return false;
	}

	// 解析结果是生成器的理想地表，实际数据可能被挖低或量化差一格，从解析命中点
	// 沿射线找空/实翻转。深度不设人为上限：终点是半径 1.25 倍的包围球，挖多深都能
	// 重新瞄回来。推导在 PlanetSurfaceRay::ComputeMarchStepRange，可无头测试。
	const PlanetSurfaceRay::FMarchStepRange MarchRange =
		PlanetSurfaceRay::ComputeMarchStepRange(
			RelativeOrigin, Direction, AnalyticDistanceCm,
			static_cast<double>(GetSurfaceRadiusCm()) * 1.25,
			/*VoxelSizeCm=*/static_cast<double>(VoxelSize));

	// 解析点换算成体素坐标，之后行进全在体素空间。本函数假定 S=1：S≠1 时解析点
	// 会偏离真实地表，命中位置跟着偏。
	const FVoxelVector HitLocalVoxels = GlobalToLocalFloat(
		RayOriginWorld + Direction * AnalyticDistanceCm);
	const FVector HitLocal(HitLocalVoxels.X, HitLocalVoxels.Y, HitLocalVoxels.Z);
	const FVector LocalDir = GetActorTransform()
		.InverseTransformVectorNoScale(Direction).GetSafeNormal();

	const auto FloorVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::FloorToDouble(Value));
	};
	const auto CeilVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::CeilToDouble(Value));
	};
	const auto VoxelOf = [](const FVector& Point)
	{
		return FIntVector(
			static_cast<int32>(FMath::FloorToInt(Point.X)),
			static_cast<int32>(FMath::FloorToInt(Point.Y)),
			static_cast<int32>(FMath::FloorToInt(Point.Z)));
	};

	// 调用方必须已持有读锁。取锁范围按「一段」算，见下面的分段循环。
	const auto SampleEmpty = [this, &HitLocal, &LocalDir, &VoxelOf](const int32 Step) -> bool
	{
		const FVector Sample = HitLocal + LocalDir * Step;
		return GetData().GetValue(VoxelOf(Sample), /*LOD=*/0).IsEmpty();
	};

	// 起点单独取一次锁（它不在扫描盒里）。锁只盖行进区段不盖整条射线：星图射线
	// 长 50 AU，按射线算锁会锁住大半棵八叉树。
	const bool bStartsEmpty = [&]() -> bool
	{
		const FIntVector StartVoxel = VoxelOf(HitLocal);
		const FVoxelIntBox StartBounds(
			StartVoxel - FIntVector(1, 1, 1), StartVoxel + FIntVector(2, 2, 2));
		const FVoxelReadScopeLock Lock(
			GetData(), StartBounds, TEXT("PlanetSurfaceRaycast"));
		return SampleEmpty(0);
	}();

	// 按起点空/实选一个方向：空就往里找第一块实心，实就往外找第一格空气。
	const int32 StepSign = bStartsEmpty ? 1 : -1;
	const int32 MaxSteps =
		bStartsEmpty ? MarchRange.InwardMaxSteps : MarchRange.BackwardMaxSteps;

	int32 BoundaryStep = 0;

	// 分段取锁，每段 128 体素；128 是锁粒度不是深度上限，整条弦一把锁会卡住体素线程。
	constexpr int32 SegmentVoxels = 128;
	for (int32 SegmentStart = 1;
		SegmentStart <= MaxSteps && BoundaryStep == 0;
		SegmentStart += SegmentVoxels)
	{
		const int32 SegmentEnd =
			FMath::Min(SegmentStart + SegmentVoxels - 1, MaxSteps);

		// 盒子盖住 (SegmentStart−1) 到 SegmentEnd；Step 0 已单独取锁，+2 是取整余量。
		const FVector SegmentA = HitLocal
			+ LocalDir * (static_cast<double>(StepSign) * (SegmentStart - 1));
		const FVector SegmentB = HitLocal
			+ LocalDir * (static_cast<double>(StepSign) * SegmentEnd);
		const FIntVector ScanMin(
			FloorVoxel(FMath::Min(SegmentA.X, SegmentB.X)) - 2,
			FloorVoxel(FMath::Min(SegmentA.Y, SegmentB.Y)) - 2,
			FloorVoxel(FMath::Min(SegmentA.Z, SegmentB.Z)) - 2);
		const FIntVector ScanMax(
			CeilVoxel(FMath::Max(SegmentA.X, SegmentB.X)) + 2,
			CeilVoxel(FMath::Max(SegmentA.Y, SegmentB.Y)) + 2,
			CeilVoxel(FMath::Max(SegmentA.Z, SegmentB.Z)) + 2);

		// FVoxelIntBox 的 Max 是开区间，所以 +1。
		const FVoxelIntBox ScanBounds(ScanMin, ScanMax + FIntVector(1, 1, 1));
		const FVoxelReadScopeLock Lock(
			GetData(), ScanBounds, TEXT("PlanetSurfaceRaycast"));

		for (int32 Step = SegmentStart; Step <= SegmentEnd; ++Step)
		{
			if (SampleEmpty(StepSign * Step) != bStartsEmpty)
			{
				BoundaryStep = Step;
				break;
			}
		}
	}

	if (BoundaryStep == 0)
	{
		// 走到几何终点都没翻转（整条弦全空或全实）。用 Verbose，笔刷预览每帧都会走。
		UE_LOG(LogTemp, Verbose,
			TEXT("%s：射线在包围球内走了 %d 步（解析点处%s）仍未找到空/实翻转。"),
			*GetName(), MaxSteps, bStartsEmpty ? TEXT("为空") : TEXT("为实"));
		return false;
	}

	// 边界取两个采样点的中点：真实表面一定在这两格之间，误差 ≤ 1 体素。
	const FVector BoundaryLocal = HitLocal
		+ LocalDir * (static_cast<double>(StepSign) * (BoundaryStep - 0.5));
	const FVector BoundaryWorld = LocalToGlobalFloat(FVoxelVector(BoundaryLocal));
	const float BoundaryDistanceCm =
		static_cast<float>((BoundaryWorld - RayOriginWorld).Size());
	if (BoundaryDistanceCm > MaxDistanceCm)
	{
		return false;
	}

	// **只在成功时写 Out 参数**：调用方拿上次最好距离剪枝，失败时旧值会被当命中距离。
	OutHitLocationWorld = BoundaryWorld;
	OutDistanceCm = BoundaryDistanceCm;
	OutHitNormalWorld = (BoundaryWorld - PlanetCenterWorld).GetSafeNormal();
	if (OutHitNormalWorld.IsNearlyZero())
	{
		OutHitNormalWorld = -Direction;
	}
	return true;
}

uint64 AVoxelPlanetActor::ComputeParamsHash() const
{
	// 对推入生成器的每项设置做 FNV-1a 并混入派生缩放半径；每字段单独混合，避免填充
	// 字节漏进哈希。刻意不含 VoxelSize/MaxLOD/NumberOfThreads/VoxelMaterial（基类直接
	// 重建，再哈希会重建两次）；LOD 档位由 RefreshLODInvoker 单独处理。
	uint64 Hash = 1469598103934665603ULL;
	// 形参名别叫 Data：会遮蔽基类私有成员 AVoxelWorld::Data，触发 C4458（警告即错误）。
	const auto Mix = [&Hash](const void* Source, int64 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Source);
		for (int64 Index = 0; Index < Size; Index++)
		{
			Hash = (Hash ^ Bytes[Index]) * 1099511628211ULL;
		}
	};

	// 混派生半径而非原始 Radius：缩放改了也要重建，而半径是唯一真正推给生成器的数。
	// 移动不进哈希，因为球心恒在局部原点。
	const float ScaledRadius = GetScaledRadiusVoxels();
	Mix(&ScaledRadius, sizeof(ScaledRadius));

	// geo 模式：配方混路径（改内容走 InvalidateAndRebuild 重烘），再混快照内容哈希 ——
	// 烘焙完成那一刻哈希必变，世界才会被建起来。
	const FString GeoRecipePath = GeoRecipe ? GeoRecipe->GetPathName() : FString();
	Mix(*GeoRecipePath, GeoRecipePath.Len() * sizeof(TCHAR));
	const uint64 GeoContentHash = GeoSnapshot.IsValid() ? GeoSnapshot->ContentHash : 0;
	Mix(&GeoContentHash, sizeof(GeoContentHash));

	return Hash;
}

bool AVoxelPlanetActor::ApplyGeoDebugOverlay()
{
	const bool bGeo = GeoSnapshot.IsValid();

	// 父材质永远是用户设的那个：VoxelMaterial 此刻可能指着上次的 MID（PIE 复制时甚至是副本）。
	if (!BiomeMaterial || VoxelMaterial != BiomeMaterial)
	{
		BaseVoxelMaterial = StripMaterialInstanceDynamic(VoxelMaterial);
		if (!BaseVoxelMaterial)
		{
			if (const AVoxelPlanetActor* Defaults = GetClass()->GetDefaultObject<AVoxelPlanetActor>())
			{
				BaseVoxelMaterial = StripMaterialInstanceDynamic(Defaults->VoxelMaterial);
			}
		}
	}

	// 退回用户材质：只有真换回了才算「换了」，调用方据此决定是否 ApplyNewMaterials。
	const auto RestoreBaseMaterial = [this]() -> bool
	{
		if (BiomeMaterial && VoxelMaterial == BiomeMaterial && BaseVoxelMaterial)
		{
			VoxelMaterial = BaseVoxelMaterial;
			return true;
		}
		return false;
	};

	if (!bGeo)
	{
		return RestoreBaseMaterial();
	}
	if (!BaseVoxelMaterial)
	{
		return false;
	}

	// 地表外观固定成「高程 / 海陆」：深海蓝 → 浅海青 → 沙滩 → 平原绿 → 高地棕 → 雪白。
	// 这是地质查看器里那张外层图的同一套配色；调试图层切换只留在查看器面板，不再挂到星球上。
	const FGeoDebugLayer* CubeLayer = FPlanetBakePipeline::FindDefaultDebugLayer(TEXT("Elevation"));
	// 判据是「高程场真的有数据」：没有阶段 1/2 的海拔时烘出来是整片 NoData 灰，
	// 再以 Blend=1 盖满整星，比没有底图更糟。
	if (!CubeLayer || GeoSnapshot->Elevation.Res <= 0)
	{
		return RestoreBaseMaterial();
	}

	// 材质按世界方向采样立方图，而快照的场在行星局部坐标：Actor 转过就必须重烘，
	// 否则配色整体错开一个角度（海盆里画出陆地色）。
	const FQuat WorldToField = GetActorQuat().Inverse();
	const uint64 OverlayHash = GeoHash::Combine(GeoSnapshot->ContentHash,
		GeoHash::Combine(GeoHash::Float(WorldToField.X),
			GeoHash::Combine(GeoHash::Float(WorldToField.Y),
				GeoHash::Combine(GeoHash::Float(WorldToField.Z), GeoHash::Float(WorldToField.W)))));

	// 图层与快照都没变就不重烘。
	if (!BiomeCubemap || OverlayBakedLayer != CubeLayer->Id || OverlayBakedHash != OverlayHash)
	{
		const double T0 = FPlatformTime::Seconds();
		UE_LOG(LogTemp, Log, TEXT("[高程底图] R0=%.0f  Min=%.0f  Max=%.0f  CrustSplit=%.1f"),
			GeoSnapshot->R0, GeoSnapshot->MinSurfaceRadius, GeoSnapshot->MaxSurfaceRadius, GeoSnapshot->CrustSplitElevation);
		UTextureCube* Cube = PlanetGeoDebugRender::BakeCube(*GeoSnapshot, *CubeLayer, 1024, WorldToField);
		if (!Cube)
		{
			return RestoreBaseMaterial();
		}
		BiomeCubemap = Cube;
		OverlayBakedLayer = CubeLayer->Id;
		OverlayBakedHash = OverlayHash;
		UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor %s: 调试叠加「%s」烘焙 %.0f ms"),
			*GetName(), *CubeLayer->DisplayName, (FPlatformTime::Seconds() - T0) * 1000.0);
	}

	UMaterialInterface* ParentMaterial = ChooseBiomeParentMaterial(BaseVoxelMaterial);
	if (!BiomeMaterial || BiomeMaterial->Parent != ParentMaterial)
	{
		BiomeMaterial = CreateBiomeMaterialMID(ParentMaterial);
		if (!BiomeMaterial || !BiomeMaterial->Parent)
		{
			BiomeMaterial = nullptr;
			if (BaseVoxelMaterial)
			{
				VoxelMaterial = BaseVoxelMaterial;
			}
			return false;
		}
	}

	BiomeMaterial->SetTextureParameterValue(TEXT("BiomeCube"), BiomeCubemap);

	BindGeoPalette(BiomeMaterial);

	const bool bSwitched = VoxelMaterial != BiomeMaterial;
	VoxelMaterial = BiomeMaterial;
	return bSwitched;
}
