// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#include "VoxelPlanetActor.h"

#include "VoxelWorldCreateInfo.h"
// 世界线程池：多颗行星共用同一个池，必须在任何 CreateWorld 之前建好。
#include "VoxelTools/VoxelBlueprintLibrary.h"
// 基类只前置声明了 UVoxelWorldRootComponent，修正组件树要调它的方法，这里要完整类型。
#include "VoxelWorldRootComponent.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetTerrainModule.h"
#include "PlanetSurfaceGridComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetGeologyPreset.h"
#include "PlanetWaterComponent.h"
#include "Sky/PlanetSkyLightComponent.h"
// biome cubemap：配色按方向烘成贴图，材质逐像素查表 —— 颜色与网格密度解耦。
#include "PlanetBiomeCubemap.h"
#include "VoxelPlanetNoiseTerrain.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData/VoxelSave.h"
#include "VoxelData/VoxelSaveUtilities.h"
#include "VoxelTools/VoxelDataTools.h"
#include "PlanetSurfaceRay.h"
#include "VoxelData/VoxelData.h"
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

	// 策展默认值：线程基类 2 个喂不饱大行星（背面区块晚到成方形空洞）；MaxLOD
	// 基类的 32 对本项目 1km 星球过细。写构造函数而非重建流程，避免覆盖用户面板设置。
	NumberOfThreads = 4;
	MaxLOD = 6;

	// LOD 切换不做淡入淡出：基类默认开抖动，新旧两套 LOD 网格会同时可见数帧
	// （mesh 重叠）。关掉后旧块在替换块全建好后当场删除，代价是 LOD 边界会跳。
	// 改回 true 时 ChunksDitheringDuration 才生效。
	bDitherChunks = false;
	ChunksDitheringDuration = 0.3f;

	// 每帧应用已完成块网格的时间预算（ms）。基类默认 1000，一批完成时游戏线程
	// 卡满一整秒；8ms 分摊到多帧，代价是 LOD 收敛慢几帧。只在 Game play type 生效。
	MeshUpdatesBudget = 8.f;

#if WITH_EDITOR
	// 每颗行星默认用项目顶点色材质。仅编辑器有效：打包后实例自带序列化值。
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

	// 水面组件；海洋/湖泊/河流分量由它自己在重建时创建。
	WaterComponent = CreateDefaultSubobject<UPlanetWaterComponent>(TEXT("PlanetWater"));

	// 天光不是可选项：没有它环境光永远是同一张静态立方图。骨架留 C++，蓝图不用管。
	PlanetSkyLight = CreateDefaultSubobject<UPlanetSkyLightComponent>(TEXT("PlanetSkyLight"));

	// 大气壳、矿物观察与表面编辑网格由 BP_VoxelPlanet 挂载；原生实例不带，
	// 编辑相关调用走 FindEditGrid()，找不到就跳过。
}

UPlanetSurfaceGridComponent* AVoxelPlanetActor::FindEditGrid() const
{
	return FindComponentByClass<UPlanetSurfaceGridComponent>();
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
	if (OrbitLOD >= 0)
	{
		return FMath::Clamp(OrbitLOD, 0, 26);
	}
	// 自动档：最粗三角边长（2^LOD 体素）约半径 1/16，弦高误差 ~R/2048。
	// 1km 星得 LOD 6（64m 边长），100m 星得 LOD 3。想要更粗就填显式值。
	const float ScaledRadius = FMath::Max(GetScaledRadiusVoxels(), 1.f);
	return FMath::Clamp(FMath::RoundToInt(FMath::Log2(ScaledRadius / 16.f)), 0, 8);
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
	const bool bFirstSync = !bLODInvokerSynced;
	const bool bOrbitLODChanged = !bFirstSync && LastAppliedOrbitLOD != OrbitLOD;
	const bool bRangeSourceChanged = bFirstSync || !FMath::IsNearlyEqual(LastAppliedRangeSource, RangeSource);
	// 值还等于上次写入的 → 归系统；被人改过 → 归用户，之后不再覆盖。首次同步没有历史可比，
	// 拿系统值当基准 —— 否则第一次手改会被「首次同步」吞掉。
	const bool bHandEdited = LODInvoker->LODToSet != (bFirstSync ? DesiredLOD : LastWrittenLODToSet);

	if (!bOrbitLODChanged && !bRangeSourceChanged && !bHandEdited)
	{
		return;
	}

	bLODInvokerSynced = true;
	LastAppliedOrbitLOD = OrbitLOD;
	LastAppliedRangeSource = RangeSource;

	if (bRangeSourceChanged)
	{
		// LOD Range 是纯几何量，只能跟半径 / VoxelSize 走，否则 LOD 覆盖不到整颗星。
		LODInvoker->LODRange = RangeSource * 1.25f + 1000.f;
	}

	// LOD to Set 只在 OrbitLOD 改动（含首次）时由系统写；命中手改就采纳用户的值。
	if (bHandEdited)
	{
		// Invoker 自身没有 PostEditChangeProperty 刷新，这里顺手让它立刻生效。
		LastWrittenLODToSet = LODInvoker->LODToSet;
	}
	else if (bFirstSync || bOrbitLODChanged)
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

#if WITH_EDITOR
void AVoxelPlanetActor::EnsureSaveObject()
{
	if (SaveObject)
	{
		return;
	}

	UWorld* World = GetWorld();
	// PIE 世界从编辑器实例复制，SaveObject 会继承；再找一遍既没必要也不可预测。
	if (!World || World->WorldType != EWorldType::Editor)
	{
		return;
	}

	const FString AssetPath = MakePlanetSaveObjectAssetPath(
		World->GetMapName(), GetActorLabel(/*bIncludeFolder=*/false));

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
	RebuildPlanet();
}

#if WITH_EDITOR
void AVoxelPlanetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// 基类先跑：它负责自己 meta=(Recreate)/(UpdateLODs) 的属性及其世界重建；
	// 这些属性不放进 ComputeParamsHash，否则同一次编辑会重建两次。
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// 地质预设换了要通知镜像方（矿物组件上的槽），否则先改行星再选组件会看到
	// 过期值。新的预设仍需按「重建行星」才生效。
	if (PropertyChangedEvent.GetPropertyName()
		== GET_MEMBER_NAME_CHECKED(AVoxelPlanetActor, GeologyAsset))
	{
		OnGeologyAssetChanged.Broadcast();
	}

	// 分辨率/混合权重只改瞬态贴图与材质参数，不该推倒体素世界：先不强制重烘
	// 安装一次，随后 RebuildPlanet 因哈希未变早退。也覆盖用户换 VoxelMaterial 的情况。
	if (InstallBiomeMaterial(/*bForceBake=*/false) && IsCreated())
	{
		UVoxelBlueprintLibrary::ApplyNewMaterials(this);
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
	if (!GetWorld())
	{
		return;
	}

	// 也在这里修一次（BeginPlay / PostEditChangeProperty 入口同样可能撞上旧树），
	// 且必须排在哈希守卫之前，否则修正会被一起吞掉。函数自带已是新树就早退。
	RepairLegacyRootComponent();

	// Invoker 刷新排在哈希守卫之前：这三项只影响 LOD 提示、不必重建整颗星，
	// 函数自带值没变就早退。
	RefreshLODInvoker();

	// 水面也有自己的刷新路径，同样在哈希守卫之前：VoxelSize 不进哈希、只触发基类
	// 重建，IsCreated() 仍为真，守卫会早退；但水面要用它折算厘米，不补就停在旧尺寸。
	// 水文场以体素为单位、与 VoxelSize 无关，首次重建时生成器未建，由 3.6 建水面。
	if (PlanetGenerator && !FMath::IsNearlyEqual(LastWaterVoxelSize, VoxelSize))
	{
		RebuildWater();
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

	if (!PlanetGenerator)
	{
		PlanetGenerator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));
	}

	// --- 1. 把参数推入生成器 ---
	PlanetGenerator->Radius = GetScaledRadiusVoxels();
	// 球心锚在体素世界的局部原点，而体素世界就是本 Actor，所以球心恒在 Actor
	// 原点、移动与重建无关。早先的 GetCenterInVoxels（逆变换 Actor 位置）正是
	// 「移动即重建」的来源。
	PlanetGenerator->Center = FVector::ZeroVector;
	PlanetGenerator->SurfaceColor = SurfaceColor;
	// 模块是「这种地形」，不带位置尺寸，同一份资产可喂给任意多颗行星。
	PlanetGenerator->Terrain = Terrain;
	// 地质预设与地形模块一样，是「这颗行星」的参数。
	PlanetGenerator->GeologyPreset = GeologyAsset;

	// --- 1.5 水文场 ---
	// 必须在 FVoxelGeneratorPicker 之前：密度场要读 Context.Hydrology 才知道
	// 哪里下切。烘焙同步（512 分辨率 0.5–1 s），只在缓存键变化时发生。
	PlanetGenerator->RefreshHydrology();

	// --- 2. 把世界边长收到刚好裹住星球 ---
	// WorldSize 默认 32768 体素而星球只占 6%，ShouldSubdivideByDistance 会把整个立方体
	// 强行切到 MaxLOD 层（4096 chunk / ~5 万任务）。收紧到半径 3 倍并取 2 的幂后降到 8。
	// 必须在 CreateWorld / Toggle 之前。
	const int32 DesiredWorldSize = FMath::RoundUpToPowerOfTwo(
		FMath::CeilToInt(GetScaledRadiusVoxels() * 3.f));
	SetWorldSize(DesiredWorldSize);

	// 材质无须在此赋值（VoxelMaterial 是本 Actor 属性），但必须在 CreateWorld 前
	// 就位：渲染器会在创建时快照它，缺材质会记 Invalid VoxelMaterial 并渲染成灰。

	// Type=Object 交出参数已设好的活实例；Type=Class 会改所有行星共享的 CDO。
	Generator = FVoxelGeneratorPicker(PlanetGenerator.Get());

	// --- 3. 把生效的八叉树参数钉进日志 ---
	// 「任务只涨不落」没法从屏幕计数定位，必须知道实际生效的世界边长。根 Height
	// 就是 RenderOctreeDepth，与 MaxLOD 的差决定被强行切几层。别删这行日志。
	UE_LOG(LogTemp, Log,
		TEXT("AVoxelPlanetActor: 体素世界边长 %u 体素（RenderOctreeDepth %d），星球半径 %.0f 体素，MaxLOD %d，行星 invoker LOD %d 范围 %.0f 厘米"),
		WorldSizeInVoxel, RenderOctreeDepth,
		GetScaledRadiusVoxels(), MaxLOD,
		LODInvoker ? LODInvoker->LODToSet : -1,
		LODInvoker ? LODInvoker->LODRange : 0.f);

	// --- 3.5 biome cubemap ---
	//
	// 必须排在 CreateWorld / Toggle 之前（渲染器在创建时快照材质）；但放在哈希
	// 守卫之后 —— 烘焙很重，拖动星球时本函数每帧被调，只有真正重建才烘一次。
	InstallBiomeMaterial(/*bForceBake=*/true);

	// --- 3.6 水面 ---
	//
	// 排在 CreateWorld 之前：编辑器世界会在 Toggle() 早退，而水面只依赖水文场；
	// 不提前装好，编辑器视口里永远看不到水。
	RebuildWater();

	// --- 4. 重建世界，让新生成器生效 ---
	// CreateWorld 会报 already created 并保留旧生成器（参数在创建时快照），
	// 不先 DestroyWorld 的话参数编辑被静默忽略。
	if (IsCreated())
	{
		DestroyWorld();
	}

#if WITH_EDITOR
	// 编辑器世界必须走插件的 Toggle() 而非直接 CreateWorld()：OnPreBeginPIE 在
	// Preview 下会拆世界，OnEndPIE 只在 bIsToggled 为真时建回来，而只有 Toggle()
	// 会置真 —— 否则 PIE 结束后整颗星消失。编辑器 Actor 默认不 Tick，无法兜底。
	// 只在编辑器世界这样做：Toggle() 会把 PlayType 设成 Preview。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		Toggle();
		return;
	}
#endif

	// --- 5. 先统一建世界线程池，再建世界 ---
	// 插件约定多体素世界要外部先建全局池，各世界 bCreateGlobalPool=false 只复用；
	// 否则建池责任落到第一个跑到的行星上，刷 already created / global pool isn't created。
	// 只对 game/PIE 世界建，编辑器走 Toggle() 自建预览池。
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

	// --- 6. 存档 ---
	// CreateWorld 内部同步走 LoadFromSaveObject，代价几乎全在装填、取决于存档格式
	// （见文件顶部 ResaveAll）：144 MB 在 Special 格式 34 s、普通格式 3.4 s。
	// 要治的是格式，不是挪线程。
	CreateWorld(FVoxelWorldCreateInfo());

	// 世界重建后表面网格采样全部作废。只刷新网格、**不**调 NotifyVoxelEdited()：
	// 重建不是编辑，不能标脏触发保存。
	if (UPlanetSurfaceGridComponent* Grid = FindEditGrid())
	{
		Grid->NotifyVoxelEdited();
	}
}

bool AVoxelPlanetActor::InstallBiomeMaterial(bool bForceBake)
{
	if (BiomeCubemapResolution <= 0)
	{
		return false;
	}

	// 生成器还没建 = 世界从没建过，等真正的重建再烘。
	if (!PlanetGenerator)
	{
		return false;
	}

	const UVoxelPlanetNoiseTerrain* NoiseTerrain =
		Cast<UVoxelPlanetNoiseTerrain>(PlanetGenerator->Terrain);
	if (!NoiseTerrain)
	{
		// 纯球模块没有配色，颜色来自 SurfaceColor 顶点色，不需要 cubemap。
		return false;
	}

	// 父材质永远是用户设的那个：VoxelMaterial 此刻可能还指着上次重建留下的 MID。
	if (!BiomeMaterial || VoxelMaterial != BiomeMaterial)
	{
		BaseVoxelMaterial = StripMaterialInstanceDynamic(VoxelMaterial);

		// PIE 复制关卡时 VoxelMaterial 被拷成上一轮 MID 的副本，BiomeMaterial /
		// BaseVoxelMaterial 被清空；副本的父还指着真材质，剥不出才退回类默认值。
		if (!BaseVoxelMaterial)
		{
			const AVoxelPlanetActor* Defaults = GetClass()->GetDefaultObject<AVoxelPlanetActor>();
			if (Defaults)
			{
				BaseVoxelMaterial = StripMaterialInstanceDynamic(Defaults->VoxelMaterial);
			}
		}
	}
	if (!BaseVoxelMaterial)
	{
		// 用户没设材质时世界本就该报 Invalid VoxelMaterial，插一脚只会变成纯色星。
		return false;
	}

	if (bForceBake || !BiomeCubemap || BakedCubemapResolution != BiomeCubemapResolution)
	{
		const double BakeStart = FPlatformTime::Seconds();

		UTextureCube* Cube = PlanetBiomeCubemap::BakeTexture(
			NoiseTerrain->MakeParams(),
			PlanetGenerator->GetGeology().Get(),
			PlanetGenerator->GetHydrology().Get(),
			double(GetScaledRadiusVoxels()),
			BiomeCubemapResolution);

		if (!Cube)
		{
			if (!bLoggedBiomeBakeFailure)
			{
				bLoggedBiomeBakeFailure = true;
				UE_LOG(LogTemp, Warning,
					TEXT("AVoxelPlanetActor %s: biome cubemap 烘焙失败（分辨率 %d），本颗星继续用顶点色。"),
					*GetName(), BiomeCubemapResolution);
			}
			return false;
		}

		BiomeCubemap = Cube;
		BakedCubemapResolution = BiomeCubemapResolution;

		// 打一行耗时：这是「烘焙到底贵不贵」的唯一可观测证据。
		UE_LOG(LogTemp, Log,
			TEXT("AVoxelPlanetActor %s: biome cubemap %d² × 6 面烘焙完成，用时 %.1f 毫秒"),
			*GetName(), BiomeCubemapResolution,
			(FPlatformTime::Seconds() - BakeStart) * 1000.0);
	}

	if (!BiomeMaterial || BiomeMaterial->Parent != BaseVoxelMaterial)
	{
		BiomeMaterial = CreateBiomeMaterialMID(BaseVoxelMaterial);
		if (!BiomeMaterial)
		{
			return false;
		}

		// 父材质类型不被接受时 Create 不返回 null，而是留下 Parent 为空的 MID；
		// 交给插件会在 VoxelRenderUtilities.cpp:518 ensure、整星变灰。宁可继续用顶点色。
		if (!BiomeMaterial->Parent)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("AVoxelPlanetActor %s: 无法为用户材质 %s 创建 biome MID（父材质被拒绝），本颗星继续用顶点色。"),
				*GetName(), *GetNameSafe(BaseVoxelMaterial));
			BiomeMaterial = nullptr;
			return false;
		}
	}

	// 参数名须与 M_PlanetVertexColor 对得上；材质里没有时静默不生效（回到纯顶点色）。
	BiomeMaterial->SetTextureParameterValue(TEXT("BiomeCube"), BiomeCubemap);
	BiomeMaterial->SetScalarParameterValue(TEXT("BiomeCubeBlend"), BiomeCubemapBlend);

	const bool bSwitched = (VoxelMaterial != BiomeMaterial);
	VoxelMaterial = BiomeMaterial;
	return bSwitched;
}

void AVoxelPlanetActor::RebuildWater()
{
	// 记下本次尺寸：RebuildPlanet 在哈希守卫前拿它比较（组件判空之前也要记）。
	LastWaterVoxelSize = VoxelSize;

	if (!WaterComponent)
	{
		return;
	}

	// 根组件可能晚于组件出现（RepairLegacyRootComponent），附着放在这里做一次。
	if (!WaterComponent->GetAttachParent() && GetRootComponent())
	{
		WaterComponent->AttachToComponent(
			GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}

	FPlanetWaterConfig Config;
	const bool bHasWater = PlanetGenerator
		&& PlanetGenerator->Terrain
		&& PlanetGenerator->Terrain->GetWaterConfig(Config)
		&& PlanetGenerator->GetHydrology().IsValid();

	// 地形模块也传下去：湖泊的 2 格余量要采样地表高度，否则水面悬在岸外。
	// 没有水时传空 → 组件会拆掉全部分量。
	WaterComponent->RebuildWater(
		bHasWater ? PlanetGenerator->GetHydrology().Get() : nullptr,
		Config,
		double(VoxelSize),
		double(GetScaledRadiusVoxels()),
		bHasWater ? PlanetGenerator->Terrain.Get() : nullptr);
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
	// 地形资产变了，水文场也过期；显式失效让「先失效、后重建」的顺序可见。
	if (PlanetGenerator)
	{
		PlanetGenerator->InvalidateHydrology();
	}

	// 触发这次重建的是地形资产参数，不参与本 Actor 的哈希；不抹掉 LastParamsHash
	// 守卫会吞掉重建。
	LastParamsHash = 0;
	RebuildPlanet();

	// 地形布局变了，表面网格必须重新采样（组件内部按防抖时间重建）。
	if (UPlanetSurfaceGridComponent* Grid = FindEditGrid())
	{
		Grid->NotifyVoxelEdited();
	}
}

void AVoxelPlanetActor::SetEditGridVisible(const bool bVisible)
{
	// 组件由蓝图挂载，原生实例静默跳过：编辑照常可用，只是看不到网格基线。
	if (UPlanetSurfaceGridComponent* Grid = FindEditGrid())
	{
		Grid->SetGridVisible(bVisible);
	}
}

void AVoxelPlanetActor::NotifyVoxelEdited()
{
	bHasUnsavedEdits = true;

	if (UPlanetSurfaceGridComponent* Grid = FindEditGrid())
	{
		Grid->NotifyVoxelEdited();
	}
}

void AVoxelPlanetActor::RequestSaveEdits()
{
	if (!bHasUnsavedEdits)
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
	if (!bHasUnsavedEdits)
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

	// CDO（Default__BP_VoxelPlanet_C）不持有生成器。CDO 上出现名为 VoxelPlanetGenerator
	// 的子对象后，各行星的同名子对象会按名字把它当原型（UObjectArchetype.cpp:127），
	// 保存外部 Actor 包时连带 harvest 出 "Illegal reference to private object"。
	// 主防线是 RebuildPlanet 的「没有世界就早退」；这里清的是历史上已落盘的那份。
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		PlanetGenerator = nullptr;
		Generator = FVoxelGeneratorPicker();
	}

	// 迁移：旧关卡存的是 JSON 预设名，找不到资产就保留字符串、不报错（资产可能还没建）。
	// 同步 TryLoad 是有意取舍：一次性编辑器迁移、预设只有几 KB；将来运行时依赖须改异步。
	if (!GeologyAsset && !GeologyPreset.IsEmpty())
	{
		const FSoftObjectPath LegacyPath = MakeLegacyGeologyAssetPath(GeologyPreset);
		if (UPlanetGeologyPreset* Loaded = Cast<UPlanetGeologyPreset>(LegacyPath.TryLoad()))
		{
			GeologyAsset = Loaded;
			UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor::PostLoad: %s 的地质预设 '%s' → %s"),
				*GetName(), *GeologyPreset, *LegacyPath.ToString());
			// 迁移成功后清空旧字段并持久化：之后不再重复迁移；作者清空 GeologyAsset
			// 后也才能真正得到无矿行星，而不是下次加载又被迁回 earth。
			GeologyPreset.Reset();
		}
	}
}

FSoftObjectPath AVoxelPlanetActor::MakeLegacyGeologyAssetPath(const FString& LegacyName)
{
	// 转发而非自己拼：路径规则只有一份，迁移目标必须与 CreateEarthGeologyPreset
	// 建的资产逐字一致，否则旧关卡永久没矿。
	return UPlanetGeologyPreset::MakeAssetPath(LegacyName);
}

void AVoxelPlanetActor::ReloadGeology()
{
	// 预设在资产里的修订号变了，丢缓存后重建即可。
	if (PlanetGenerator)
	{
		PlanetGenerator->InvalidateGeology();
	}
	InvalidateAndRebuild();
}

float AVoxelPlanetActor::GetSurfaceRadiusCm() const
{
	// 半径以体素计，乘以单个体素的厘米尺寸才是世界单位。
	return GetScaledRadiusVoxels() * VoxelSize;
}

float AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const
{
	// 没挂模块（或生成器未建）时地表就是基准球面 —— 光滑球正是没有地形的样子。
	if (!PlanetGenerator || !PlanetGenerator->Terrain)
	{
		return GetSurfaceRadiusCm();
	}

	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	// 上下文由生成器统一构造：地质场必须与密度场那份相同，否则脚下与看到的地面不同。
	const FVoxelPlanetTerrainContext Context = PlanetGenerator->MakeTerrainContext();

	// 体素空间与世界空间之间只差一个等比缩放，没有旋转，方向可以原样传递。
	return PlanetGenerator->Terrain->GetSurfaceRadiusVoxels(Context, Direction) * VoxelSize;
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

TSharedPtr<const FPlanetGeologyField> AVoxelPlanetActor::GetGeologyField()
{
	return PlanetGenerator ? PlanetGenerator->GetGeology() : nullptr;
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
	// 重建，再哈希会重建两次）与 OrbitLOD（单独处理）；VoxelSize 另由 RebuildPlanet
	// 单独比较以刷新水面。
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

	Mix(&SurfaceColor, sizeof(SurfaceColor));

	// 按资产路径混合：混指针会在重载后（地址变、资产没变）破坏哈希。这里只覆盖
	// 「换了模块」；改模块自身参数由它的 PostEditChangeProperty 走 InvalidateAndRebuild。
	const FString TerrainPath = Terrain ? Terrain->GetPathName() : FString();
	Mix(*TerrainPath, TerrainPath.Len() * sizeof(TCHAR));

	// 按资产路径混合不混指针；也不混 Revision —— 改预设内容靠显式的「重建行星」。
	const FString GeologyAssetPath = GeologyAsset ? GeologyAsset->GetPathName() : FString();
	Mix(*GeologyAssetPath, GeologyAssetPath.Len() * sizeof(TCHAR));

	return Hash;
}
