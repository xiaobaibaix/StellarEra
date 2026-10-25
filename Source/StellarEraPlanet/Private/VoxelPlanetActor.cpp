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
#include "PlanetAtmosphereComponent.h"
#include "PlanetSurfaceGridComponent.h"
#include "PlanetGeologyField.h"
#include "PlanetMineralViewComponent.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
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

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#endif

AVoxelPlanetActor::AVoxelPlanetActor()
{
	// 基类构造函数已经把 WorldRoot 建好并设成 RootComponent —— 体素世界就是本
	// Actor 自己，这里不再另建根组件，也没有「PlanetRoot」这个东西了。

	// 世界由 RebuildPlanet 显式创建。基类的 BeginPlay 会在
	// bCreateWorldAutomatically 为真时排一个「下一帧 CreateWorld」的定时器，
	// 那会和我们自己那次撞成建两次（第二次因 already created 被静默忽略，保留
	// 第一次的生成器快照）。显式关掉，不依赖默认值。
	bCreateWorldAutomatically = false;

	// 世界线程池由 RebuildPlanet 在 CreateWorld 之前显式建一次（见那里）。
	// 基类默认 true 会让每颗行星各自抢着建池：先建的那颗定下线程数，后建的就刷
	// 「already created, NumberOfThreads will be ignored」；若某个实例被改成
	// false，第一颗反而刷「global pool isn't created」。统一 false，只复用。
	// （运行时还会在 CreateWorld 前再压一次，防止蓝图实例覆盖。）
	bCreateGlobalPool = false;

	// 这两个是我们的策展默认值，基类的默认不适合本项目：
	//   - 线程：基类 2 个，喂不饱大行星的流送，背面区块比正面晚到很久，表现为
	//     表面上的方形空洞；
	//   - MaxLOD：基类是 ClampMesherDepth(32)，对本项目的 1km 星球过细。
	//
	// 写在构造函数里而不是 RebuildPlanet 里：它们现在是**用户自己的**属性
	//（基类的 NumberOfThreads / MaxLOD），每次重建都去覆盖就等于把面板上的
	// 控件变成摆设。
	NumberOfThreads = 4;
	MaxLOD = 6;

	// LOD 切换的过渡时长（基类默认 1 秒）：插件在旧块被新块完全替代后，要等
	// 2 × 这个值才把旧块淡出（VoxelDefaultRenderer.cpp:900），期间两套不同 LOD
	// 的网格同时可见。1 秒 ⇒ 2 秒的叠影窗口对高速飞行太长；0.3 秒压到 0.6 秒。
	ChunksDitheringDuration = 0.3f;

	// 每帧应用已完成块网格的时间预算（毫秒）。基类默认 1000 毫秒，意味着一批
	// 块同时完成时游戏线程可以卡满一整秒 —— 高速飞行「卡一下」的直接来源。
	// 8 毫秒把这一批分摊到多帧；代价是 LOD 收敛慢几帧。
	// 只在 Game play type 生效；编辑器预览由插件固定为 1000。
	MeshUpdatesBudget = 8.f;

#if WITH_EDITOR
	// 每颗行星默认使用项目的顶点色材质。仅编辑器有效：打包后的游戏里
	// 实例自带序列化值。早于此属性存在的已摆放 Actor 没有对应标签，
	// 加载时同样会采用这个 CDO 默认值。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlanetMaterialFinder(
		TEXT("/Game/Materials/M_PlanetVertexColor.M_PlanetVertexColor"));
	if (PlanetMaterialFinder.Succeeded())
	{
		VoxelMaterial = PlanetMaterialFinder.Object;
	}
#endif

	// 生成器在 RebuildPlanet 里懒创建：若在构造函数中创建，会在类默认对象上
	// 实例化一个子对象，摆好的 Actor 保存时就会因引用解析进 /Script 而报
	// "Illegal reference to private object"。

	// LOD Invoker 挂在 WorldRoot 下。它和行星之间是**组件树**关系，变换自动
	// 跟随 —— 这正是体素世界当子对象时缺的那一环。
	LODInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("PlanetLODInvoker"));
	LODInvoker->SetupAttachment(RootComponent);
	// 只做 LOD 基准：碰撞与导航的 Invoker 应由贴近表面的 Pawn 自己携带，
	// 挂在球心反而会优先喂行星内部的区块。
	LODInvoker->bUseForLOD = true;
	LODInvoker->bUseForCollisions = false;
	LODInvoker->bUseForNavmesh = false;
	LODInvoker->bUseForEvents = false;
	LODInvoker->bUseForPriorities = false;
	// 占位值，RebuildPlanet 会按半径重算。
	LODInvoker->LODToSet = 6;
	LODInvoker->LODRange = 150000.f;

	// 大气壳。挂到根组件上，相对位置为零 —— 球心与行星球心重合。
	// 材质与缩放都在 BeginPlay 里按 GetSurfaceRadiusCm() 定，构造期拿不到最终半径。
	AtmosphereComponent = CreateDefaultSubobject<UPlanetAtmosphereComponent>(TEXT("PlanetAtmosphere"));
	AtmosphereComponent->SetupAttachment(RootComponent);

	// 矿物透视组件：默认不 Tick，Activate 时才采样并 spawn 观察体。
	MineralViewComponent = CreateDefaultSubobject<UPlanetMineralViewComponent>(TEXT("PlanetMineralView"));
	MineralViewComponent->SetupAttachment(RootComponent);

	// 表面网格：默认隐藏、不 tick，编辑模式打开时才工作。
	// 它是纯 UActorComponent（不带变换），跟着 Owner Tick 即可，不需要 SetupAttachment
	// —— 那个函数只存在于 USceneComponent。
	EditGrid = CreateDefaultSubobject<UPlanetSurfaceGridComponent>(TEXT("PlanetEditGrid"));
}

float AVoxelPlanetActor::GetScaledRadiusVoxels() const
{
	// 只采用等比缩放。非等比缩放会剪切体素网格，而密度场定义在体素空间中，
	// 得到的将是扭曲的畸形而非缩放后的行星。
	//
	// ⚠ 已知不一致，本次不修（只有缩放 S ≠ 1 时才显形；本关卡所有星球 S = 1）：
	// 渲染时 AVoxelWorld::LocalToGlobalFloat 是 TransformPosition(VoxelSize · p)，
	// 那个变换里**已经含** S，所以网格的世界半径是 Radius · S² · VoxelSize，而
	// GetSurfaceRadiusCm() 返回 Radius · S · VoxelSize。S = 2 时网格 4 km、角色与
	// 飞船却按 2 km 摆，埋进星体 2 km。
	//
	// 改成继承之后这条**没有变好也没有变坏**：本 Actor 自己的变换就是那个变换，
	// 里面照样含 S。要修得先定下「体素空间半径含不含 S」以及随之而来的哈希 /
	// 重建契约（见 SDD 台账最终复审小节），不能只改这里一行。
	const float UniformScale = GetActorScale3D().GetAbsMax();
	return Radius * UniformScale;
}

int32 AVoxelPlanetActor::ComputeOrbitLOD() const
{
	if (OrbitLOD >= 0)
	{
		return FMath::Clamp(OrbitLOD, 0, 26);
	}
	// 自动档：让最粗的表面三角边长（2^LOD 体素）约为半径的 1/16。
	// 由此弦高误差 ~ (R/16)^2 / (8R) = R/2048，任何尺寸的行星从轨道看都是
	// 光滑球面；1km 星得到 LOD 6（64m 边长），100m 星得到 LOD 3（8m 边长）。
	const float ScaledRadius = FMath::Max(GetScaledRadiusVoxels(), 1.f);
	return FMath::Clamp(FMath::RoundToInt(FMath::Log2(ScaledRadius / 16.f)), 0, 8);
}

void AVoxelPlanetActor::RefreshLODInvoker()
{
	if (!LODInvoker)
	{
		return;
	}

	const int32 NewLOD = ComputeOrbitLOD();
	// 范围以球心为原点向六方延伸，略超出半径即可覆盖整颗星。
	const float NewRange =
		GetScaledRadiusVoxels() * FMath::Max(VoxelSize, 0.0001f) * 1.25f + 1000.f;

	// 拖动星球时本函数每帧都被调用，所以先比较再赋值：RefreshAllVoxelInvokers
	// 是全局刷新，每帧无条件跑一遍会拖慢编辑器。
	if (LODInvoker->LODToSet == NewLOD && FMath::IsNearlyEqual(LODInvoker->LODRange, NewRange))
	{
		return;
	}

	LODInvoker->LODToSet = NewLOD;
	LODInvoker->LODRange = NewRange;
	UVoxelInvokerComponentBase::RefreshAllVoxelInvokers();
}

void AVoxelPlanetActor::RepairLegacyRootComponent()
{
	// ── 这个函数在修什么 ───────────────────────────────────────────────────
	// 基类 AVoxelWorld 的构造函数把 WorldRoot 立为根组件（VoxelWorld.cpp:83-86），
	// 渲染器也是拿 WorldRoot 建的体素世界（:866）—— 全部网格组件都挂在它下面。
	//
	// 但本类**曾经**不是继承 AVoxelWorld：那时以自己建的 PlanetRoot 为根，把
	// AVoxelWorld 当子对象持有。改成继承之后，已保存的关卡实例里 RootComponent
	// 仍是反序列化出来的 PlanetRoot，而基类新建的 WorldRoot 成了游离组件（没有
	// 父组件，变换恒为单位阵）。同一个体素世界于是被两套变换描述，差了一整个
	// Actor 位移：
	//
	//   - 网格渲染走 WorldRoot 的世界变换              → 画在世界原点 (0,0,0)；
	//   - GlobalToLocal / LocalToGlobal / invoker 换算走 GetTransform()，而
	//     AActor::GetTransform() 取的就是 RootComponent（VoxelWorld.cpp:347 /
	//     367 / 372）                                  → 算的是 Actor 位置。
	//
	// 两者相差 4704 体素，而八叉树半径只有 2048 体素：任何 invoker 的 LODBounds
	// 都不可能和 OctreeBounds 相交，ShouldSubdivideByDistance 于是永不细分。症状
	// 就是「星球不进行 LOD，相机怎么动都没用」，同时星球画在原点、和 Actor 的
	// gizmo 对不上 —— 两个看似无关的现象其实是同一个错位。
	//
	// 修法：把基类的不变量恢复回来 —— WorldRoot 必须是根组件，位置沿用旧根的，
	// 摆在关卡里的位置因此原样保留。
	if (!WorldRoot || GetRootComponent() == WorldRoot.Get())
	{
		return;
	}

	USceneComponent* const LegacyRoot = GetRootComponent();
	const FTransform LegacyTransform = LegacyRoot->GetComponentTransform();

	UE_LOG(LogTemp, Warning,
		TEXT("AVoxelPlanetActor: %s 的根组件是遗留的 %s 而非 WorldRoot，修正组件树（体素网格此前画在世界原点、LOD 失效）"),
		*GetName(), *LegacyRoot->GetName());

	// 先脱后摆，顺序不能反：还挂着的时候 SetWorldTransform 写进去的是相对变换，
	// 紧接着的 SetRootComponent 会把它当成世界变换用。
	WorldRoot->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	WorldRoot->SetWorldTransform(LegacyTransform);
	SetRootComponent(WorldRoot.Get());

	// LOD Invoker 原本挂在 PlanetRoot 下，跟着搬到新根，并保持世界位置不变 ——
	// 否则球心会从行星中心偏出去，整颗星的 LOD 基准跟着错。
	if (LODInvoker && LODInvoker->GetAttachParent() != WorldRoot.Get())
	{
		LODInvoker->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		LODInvoker->AttachToComponent(WorldRoot.Get(), FAttachmentTransformRules::KeepWorldTransform);
	}
}

FString AVoxelPlanetActor::MakePlanetSaveObjectAssetPath(const FString& MapName, const FString& ActorLabel)
{
	// PIE 世界名形如 UEDPIE_0_UniverseMap。不剥前缀的话，每次开 PIE 路径都变，
	// 编辑永远落不到同一个资产上。
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

	// 资产名只允许字母数字与下划线；地图名 / Actor 标签里的空格、冒号、斜杠
	// 都会让 CreatePackage 失败。
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

	return FString::Printf(TEXT("/Game/Saves/PlanetVoxel/%s/%s"), *SafeMap, *SafeLabel);
}

#if WITH_EDITOR
void AVoxelPlanetActor::EnsureSaveObject()
{
	if (SaveObject)
	{
		return;
	}

	UWorld* World = GetWorld();
	// PIE 世界里的 Actor 是从编辑器实例复制来的，SaveObject 会继承过来；
	// 在这里再找一遍资产既没必要，也会让「编辑到底存在哪」变得不可预测。
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
			// 新建 SaveObject 的 Depth 是 0，而「还没存过任何体素」的压缩存档是 -1。
			// 不补这一步，CreateWorld → LoadFromSaveObject 会把它当成 Invalid Save Object。
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

	// 新资产此刻只在内存包里；顺手写盘，否则重启编辑器它就不存在了。
	// 已存在的资产不动：没编辑就重写一遍没有意义，还会平白改磁盘 mtime。
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
	// 先修组件树再让基类跑：基类的 OnConstruction 在 bIsToggled 为真时会当场
	// 建出预览世界，用错误的根组件建出来的几何也是错的。
	RepairLegacyRootComponent();

#if WITH_EDITOR
	// 必须排在 Super::OnConstruction / RebuildPlanet 之前：后者会 CreateWorld，
	// 而 CreateWorld 当场就按 SaveObject 加载体素。晚一步赋值，这次运行仍从
	// 空白世界开始，用户看起来就是「存档没生效」。
	EnsureSaveObject();
#endif

	Super::OnConstruction(Transform);

#if WITH_EDITOR
	// World Partition 会按包围盒把空间加载的 Actor 烘进流送单元。摄像机每跨
	// 过一次单元边界，该 Actor 就被销毁重建，整个行星生成随之重启。选择退出
	// 后它会被烘进常驻加载单元；下次保存地图时生效。
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
	// 基类先跑：它负责自己那些 meta=(Recreate) / (UpdateLODs) 属性（VoxelSize、
	// NumberOfThreads、VoxelMaterial、MaxLOD……）。那些属性的世界重建由基类
	// 自己发起，所以我们**不把它们放进 ComputeParamsHash**，否则同一次编辑会
	// 触发两次「销毁 + 重建」。
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// OnConstruction 只在移动/添加时重新执行：否则单纯在细节面板里改参数
	// 永远不会重建世界，改动会静默失效。
	RebuildPlanet();
}
#endif

void AVoxelPlanetActor::RebuildPlanet()
{
	// 也在这里修一次：OnConstruction 之外还有两条入口（BeginPlay 与
	// PostEditChangeProperty），它们同样可能撞上未修正的组件树。必须排在下面
	// 的哈希守卫**之前** —— 守卫在「哈希没变且世界已建好」时会直接返回，把修正
	// 连同重建一起吞掉。函数自带「已经是新树就早退」，重复调用只是一次指针比较。
	RepairLegacyRootComponent();

	// Invoker 先刷新，且排在哈希守卫**之前**：OrbitLOD / 半径 / VoxelSize 只影响
	// LOD 提示，不影响世界几何，改了不必重建整颗星。函数内部自带「值没变就早退」，
	// 所以拖动星球这条热路径上它是廉价的。
	RefreshLODInvoker();

	// 细节面板的一次编辑会同时触发 PostEditChangeProperty 和重新执行
	// OnConstruction：没有这道守卫，改一个参数就会把世界重建两次，生成时间
	// 翻倍，流送重置还会再次打开背面的空洞。这里复查 IsCreated()，保证全新
	// 的世界（地图加载、PIE 复制）总能构建。
	const uint64 ParamsHash = ComputeParamsHash();
	if (ParamsHash == LastParamsHash && IsCreated())
	{
		return;
	}
	LastParamsHash = ParamsHash;

	// 重建一次打一行：这是「移动星球到底有没有触发重建」的唯一可观测证据。
	// 正常情况下拖动星球不应该产生新行，若每帧刷屏就说明哈希守卫失效了。
	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor: 重建 %s（参数哈希 %llu）"), *GetName(), ParamsHash);

	if (!PlanetGenerator)
	{
		PlanetGenerator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));
	}

	// --- 1. 把参数推入生成器 ---
	PlanetGenerator->Radius = GetScaledRadiusVoxels();
	// 球心锚在**体素世界的局部原点**，而体素世界就是本 Actor —— 于是球心恒在
	// Actor 原点，与 Actor 摆在哪、怎么移动完全无关。这就是「移动星球不需要
	// 重建」的全部依据。
	//
	// 早先这里是 GetCenterInVoxels()：把 Actor 世界位置逆变换成体素坐标喂给
	// 生成器。那套只在「体素世界是子对象、和行星各有一个位置」时才需要，而且
	// 正是「移动就得重建」的来源。继承之后它退化成常量零。
	PlanetGenerator->Center = FVector::ZeroVector;
	PlanetGenerator->SurfaceColor = SurfaceColor;
	// 地形模块也推给生成器：生成器是「这颗行星」的，模块是「这种地形」的。
	// 模块本身不带位置和尺寸，同一份资产可以喂给任意多颗行星。
	PlanetGenerator->Terrain = Terrain;
	// 地质预设与地形模块一样，是「这颗行星」的参数。
	PlanetGenerator->GeologyPreset = GeologyPreset;

	// --- 2. 把世界边长收到刚好裹住星球 ---
	// AVoxelWorld 的 RenderOctreeDepth 默认是 10，即世界边长 32768 体素，
	// 而本星球半径只有 1000 体素 —— 星球只占边长的 6%。
	//
	// 致命处在于 FVoxelRenderOctree::ShouldSubdivideByDistance
	//（VoxelRenderOctree.cpp:601）对 Height > MaxLOD 的节点**无条件** return true，
	// 跟「那里有没有地形」无关；它唯一的地理判据是 OctreeBounds 与 WorldBounds
	// 相交，而 WorldBounds 就是上面那个 32768³ 立方体本身。于是整个立方体会被强行
	// 细分到第 MaxLOD 层：32768 / (32 · 2^6) = 16，即 16³ = 4096 个 chunk、
	// 约 5 万个任务。2~4 个线程永远追不上，池里任务只增不减，几分钟内拖死机器。
	//
	// 收紧到半径的 3 倍（2 倍半径 + 地形起伏余量）再向上取 2 的幂：
	// 半径 1000 → 3000 → 4096 体素，根 Height = 7，只比 MaxLOD = 6 多一层，
	// chunk 数从 4096 降到 2³ = 8。半径 10000 的星球得到 32768，行为不变。
	//
	// 必须排在 CreateWorld / Toggle 之前：世界边界是在那一刻按 RenderOctreeDepth
	// 算出并快照进渲染器的。SetWorldSize 自身只写两个字段，不会自己建世界。
	const int32 DesiredWorldSize = FMath::RoundUpToPowerOfTwo(
		FMath::CeilToInt(GetScaledRadiusVoxels() * 3.f));
	SetWorldSize(DesiredWorldSize);

	// 材质不必在这里赋值：VoxelMaterial 现在就是本 Actor 自己的属性，用户设的
	// 值一直都在。但「必须在 CreateWorld 之前就位」这条约束仍然成立 —— 渲染器
	// 会在创建时把它快照进各 LOD 的设置里；缺材质构建的世界会记录
	// "Invalid VoxelMaterial" 并渲染成平面灰色，之后再赋什么都没用。

	// Type=Object 交出一个参数已设置完毕的活实例。
	// 若用 Type=Class，改动的会是类默认对象，而它被关卡中所有行星全局共享。
	Generator = FVoxelGeneratorPicker(PlanetGenerator.Get());

	// --- 3. 把生效的八叉树参数钉进日志 ---
	// 「任务只涨不落」这类问题没法从屏幕上那两个计数定位，必须知道实际生效的世界边长。
	// 根节点的 Height 就等于 RenderOctreeDepth，而 ShouldSubdivideByDistance 对
	// Height > MaxLOD 的节点无条件细分 —— 这两个数的差就是「整块立方体被强行切几层」，
	// 差为 0 时只有 1 个根节点，差为 n 时根层之下有 8^n 个 chunk。
	// 别删这一行：它是判断 SetWorldSize 有没有生效的唯一证据。
	UE_LOG(LogTemp, Log,
		TEXT("AVoxelPlanetActor: 体素世界边长 %u 体素（RenderOctreeDepth %d），星球半径 %.0f 体素，MaxLOD %d，行星 invoker LOD %d 范围 %.0f 厘米"),
		WorldSizeInVoxel, RenderOctreeDepth,
		GetScaledRadiusVoxels(), MaxLOD,
		LODInvoker ? LODInvoker->LODToSet : -1,
		LODInvoker ? LODInvoker->LODRange : 0.f);

	// --- 4. 重建世界，让新生成器生效 ---
	// CreateWorld 会报 "already created" 并保留旧生成器实例（它在创建时把
	// 所有参数快照为 const），因此不先销毁的话，参数编辑会被静默忽略。
	if (IsCreated())
	{
		DestroyWorld();
	}

#if WITH_EDITOR
	// 编辑器世界里必须走插件自己的「预览开关」Toggle()，不能直接调 CreateWorld()。
	//
	// AVoxelWorld::OnPreBeginPIE（VoxelWorld.cpp:1585）在 PlayType == Preview 时
	// 无条件拆掉体素世界并打 "Unloading world"，而 OnEndPIE（:1593）只在
	// bIsToggled 为真时才把它建回来。bIsToggled（VoxelWorld.h:767）默认 false，
	// 且只有 Toggle()（:1326）会置真 —— 我们直接调 CreateWorld() 从没按过那个
	// 开关，于是 PIE 一结束整颗星就从视口里消失，直到有人手动改一次属性触发
	// PostEditChangeProperty 才回来。
	//
	// 不能改用 Tick 兜底：编辑器世界的 Actor 默认不 Tick
	//（FTickFunction::bTickInEditor 默认为 false），那种代码在编辑器里从不执行。
	//
	// 只在编辑器世界这么做：PIE 与打包世界要的是 PlayType == Game，
	// 而 Toggle() 会把 PlayType 设成 Preview。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		Toggle();
		return;
	}
#endif

	// --- 5. 先统一建世界线程池，再建世界 ---
	//
	// 本关卡有多颗行星，每颗都会走到这里 CreateWorld → CreatePool。插件对
	// 「多体素世界」的约定是：**先由外部建好一个世界池**，各世界把
	// bCreateGlobalPool 设 false 只复用（VoxelWorld.cpp:838-856）。没人先建时：
	//   - bCreateGlobalPool=true 的那颗会建池，后面的每颗都刷
	//     「already created, NumberOfThreads will be ignored」；
	//   - 被实例改成 false 的那颗若排在前面，则刷
	//     「global pool isn't created」，池的线程数由它顺手决定。
	// 两条警告是同一个根因：建池的责任落到了「第一个跑到的行星」身上。
	//
	// 放在 CreateWorld 之前而不是 BeginPlay：游戏世界的首次建世发生在
	// OnConstruction（PIE 里也会跑），BeginPlay 已经太晚。这里是非编辑器
	// 世界建池的唯一入口 —— 编辑器走上面的 Toggle()（Preview 自建池）已返回。
	//
	// 再压一次 bCreateGlobalPool 是因为蓝图实例可能把它覆盖回 true：只改
	// 构造函数默认值管不到已摆放的实例。
	// 只对 game / PIE 世界建池：编辑器世界的预览池由 Toggle() 自己按 Preview
	// 创建，EditorPreview 世界也不该留下一个没人复用的世界池。
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

	// 世界刚被 DestroyWorld + CreateWorld 重建，旧的表面网格采样全部作废。
	// 这里只刷新网格、**不**调用本 Actor 的 NotifyVoxelEdited()：重建不是编辑，
	// 不能因此把行星标脏并触发保存。
	if (EditGrid)
	{
		EditGrid->NotifyVoxelEdited();
	}
}

void AVoxelPlanetActor::InvalidateAndRebuild()
{
	// 把上次的哈希抹掉再重建。
	//
	// RebuildPlanet 顶部有一道「哈希没变且世界已建好就跳过」的守卫，而触发这次
	// 重建的是**地形资产**里的参数，不是本 Actor 的属性 —— 它根本不参与那个
	// 哈希。不抹掉的话守卫会把这次重建整个吞掉。
	LastParamsHash = 0;
	RebuildPlanet();

	// 地形布局变了，表面网格必须重新采样（组件内部按防抖时间重建）。
	if (EditGrid)
	{
		EditGrid->NotifyVoxelEdited();
	}
}

void AVoxelPlanetActor::SetEditGridVisible(const bool bVisible)
{
	if (EditGrid)
	{
		EditGrid->SetGridVisible(bVisible);
	}
}

void AVoxelPlanetActor::NotifyVoxelEdited()
{
	bHasUnsavedEdits = true;

	if (EditGrid)
	{
		EditGrid->NotifyVoxelEdited();
	}

	if (UWorld* World = GetWorld())
	{
		// SaveDebounceSeconds = 0 时引擎不会建定时器（SetTimer 只在 rate > 0 时生效），
		// 语义是「只在 EndPlay 保存」，不是「立即保存」。
		World->GetTimerManager().SetTimer(SaveTimerHandle,
			FTimerDelegate::CreateUObject(this, &AVoxelPlanetActor::SaveEditsToSaveObject),
			FMath::Max(SaveDebounceSeconds, 0.f), false);
	}
}

void AVoxelPlanetActor::SaveEditsToSaveObject()
{
	if (!bHasUnsavedEdits)
	{
		return;
	}

	// 不在这里清标志：没有 SaveObject / 世界没建时这次保存**没成功**，脏数据
	// 必须留着，等 SaveObject 后补上（延迟赋值的场景）后由下一次保存或 EndPlay
	// 再试。「只警告一次」由 bLoggedMissingSaveObject 负责，不靠提前清标志。
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

	FVoxelUncompressedWorldSave UncompressedSave;
	UVoxelDataTools::GetSave(this, UncompressedSave);
	UVoxelSaveUtilities::CompressVoxelSave(UncompressedSave, SaveObject->Save);

	// Depth 不参与压缩，必须从 Save 补上；否则下次加载时世界尺寸对不上。
	SaveObject->CopyDepthFromSave();
	SaveObject->MarkPackageDirty();

#if WITH_EDITOR
	// MarkPackageDirty 只改内存里的脏标志：PIE 结束时那份资产副本直接丢弃，
	// 用户看到的就是「编辑没存」。每次防抖保存都顺手落盘，编辑才真正活过重启。
	SaveSaveObjectToDisk();
#endif

	// 到这里才算真的写进了 SaveObject，脏标志才能清。
	bHasUnsavedEdits = false;
}

void AVoxelPlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SaveTimerHandle);
	}

	// 防抖窗口内退出 PIE 也不能丢最后一次编辑。
	SaveEditsToSaveObject();

	Super::EndPlay(EndPlayReason);
}

void AVoxelPlanetActor::ReloadGeology()
{
	// 生成器缓存必须在重建之前作废：重建会重新 GetInstance()，那时才读新 JSON。
	if (PlanetGenerator)
	{
		PlanetGenerator->InvalidateGeology();
	}
	InvalidateAndRebuild();
}

TArray<FString> AVoxelPlanetActor::GetGeologyPresetOptions() const
{
	return FPlanetGeologyField::ListPresetNames();
}

float AVoxelPlanetActor::GetSurfaceRadiusCm() const
{
	// 半径以体素计，乘以单个体素的厘米尺寸才是世界单位。
	return GetScaledRadiusVoxels() * VoxelSize;
}

float AVoxelPlanetActor::GetSurfaceRadiusCmAtDirection(const FVector& WorldDirection) const
{
	// 没挂模块（或生成器还没建）时地表就是基准球面 —— 光滑球正是「没有地形」
	// 的样子，所以这里退回 GetSurfaceRadiusCm 是对的，不是权宜之计。
	if (!PlanetGenerator || !PlanetGenerator->Terrain)
	{
		return GetSurfaceRadiusCm();
	}

	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	// 上下文由生成器统一构造：地质场必须与密度场那份是同一个，
	// 否则角色脚下的地面和眼睛看到的地面不是同一个面。
	const FVoxelPlanetTerrainContext Context = PlanetGenerator->MakeTerrainContext();

	// 体素空间与世界空间之间只差一个等比缩放，没有旋转，方向可以原样传递。
	return PlanetGenerator->Terrain->GetSurfaceRadiusVoxels(Context, Direction) * VoxelSize;
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

	// 体素空间与世界空间只差一个等比缩放和行星位置，没有旋转 —— 与
	// GetSurfaceRadiusCmAtDirection 里那条说明同源。解析这一步全程用厘米。
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

	// 解析结果是「生成器的理想地表」，实际数据可能被挖低了整整一笔刷，也可能因为
	// 量化差一格。从解析命中点出发沿射线找空/实翻转。窗口取 128 体素，与星图笔刷
	// 的尺寸上限一致：一笔刷能挖多深，就能从多深的地面重新瞄回来。更深的竖井需要
	// 从坑沿往下打第二次 —— 这是刻度，不是 bug。
	constexpr int32 SurfaceRayWindowVoxels = 128;

	// 解析点换算成体素坐标，之后的行进全在体素空间（单位 = 体素）。
	// 本函数假定 Actor 缩放 S = 1（见 GetScaledRadiusVoxels 里的不一致说明）：
	// S ≠ 1 时体素空间里的实际表面半径是 Radius · S，而解析这一步用的是 Radius，
	// 偏差 (S − 1) · Radius 体素；超过窗口就打不中，星图会静默失效。
	const FVoxelVector HitLocalVoxels = GlobalToLocalFloat(
		RayOriginWorld + Direction * AnalyticDistanceCm);
	const FVector HitLocal(HitLocalVoxels.X, HitLocalVoxels.Y, HitLocalVoxels.Z);
	const FVector LocalDir = GetActorTransform()
		.InverseTransformVectorNoScale(Direction).GetSafeNormal();

	// 锁只盖住行进窗口（±Window），不盖整条射线：星图的射线长 50 AU，按射线原点
	// 与终点算锁会锁住整颗行星的大半个八叉树。
	const FVector MarchPlus = HitLocal + LocalDir * SurfaceRayWindowVoxels;
	const FVector MarchMinus = HitLocal - LocalDir * SurfaceRayWindowVoxels;
	const auto FloorVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::FloorToDouble(Value));
	};
	const auto CeilVoxel = [](const double Value)
	{
		return static_cast<int32>(FMath::CeilToDouble(Value));
	};

	const FIntVector ScanMin(
		FloorVoxel(FMath::Min3(HitLocal.X, MarchPlus.X, MarchMinus.X)) - 2,
		FloorVoxel(FMath::Min3(HitLocal.Y, MarchPlus.Y, MarchMinus.Y)) - 2,
		FloorVoxel(FMath::Min3(HitLocal.Z, MarchPlus.Z, MarchMinus.Z)) - 2);
	const FIntVector ScanMax(
		CeilVoxel(FMath::Max3(HitLocal.X, MarchPlus.X, MarchMinus.X)) + 2,
		CeilVoxel(FMath::Max3(HitLocal.Y, MarchPlus.Y, MarchMinus.Y)) + 2,
		CeilVoxel(FMath::Max3(HitLocal.Z, MarchPlus.Z, MarchMinus.Z)) + 2);

	// FVoxelIntBox 的 Max 是开区间，所以 +1。
	const FVoxelIntBox ScanBounds(ScanMin, ScanMax + FIntVector(1, 1, 1));

	const FVoxelReadScopeLock Lock(GetData(), ScanBounds, TEXT("PlanetSurfaceRaycast"));

	const auto SampleEmpty = [this, &HitLocal, &LocalDir](const int32 Step) -> bool
	{
		const FVector Sample = HitLocal + LocalDir * Step;
		const FIntVector Voxel(
			static_cast<int32>(FMath::FloorToInt(Sample.X)),
			static_cast<int32>(FMath::FloorToInt(Sample.Y)),
			static_cast<int32>(FMath::FloorToInt(Sample.Z)));
		return GetData().GetValue(Voxel, /*LOD=*/0).IsEmpty();
	};

	// 从解析点往里走是「空 → 实」（正常俯视），往外走是「实 → 空」（估计高了一格）。
	// 按起点空/实**选一个**方向走：起点空就往里找第一块实心，起点实就往外找第一格空气。
	// 失败面因此只剩「翻转点在窗口之外」这一种。
	const bool bStartsEmpty = SampleEmpty(0);
	const int32 StepSign = bStartsEmpty ? 1 : -1;

	int32 BoundaryStep = 0;
	for (int32 Step = 1; Step <= SurfaceRayWindowVoxels; ++Step)
	{
		if (SampleEmpty(StepSign * Step) != bStartsEmpty)
		{
			BoundaryStep = Step;
			break;
		}
	}

	if (BoundaryStep == 0)
	{
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

	// **只在成功时写 Out 参数**：调用方拿上一次的最好距离做剪枝，失败时残留的
	// 旧值会被当成命中距离。
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
	// 对**推入生成器**的每项设置做 FNV-1a，外加派生出来的缩放半径。
	// 每个字段单独混合，结构体填充字节就不会把未初始化的数据漏进哈希。
	//
	// 这里刻意**不含**基类的 VoxelSize / MaxLOD / NumberOfThreads / VoxelMaterial：
	// 它们是本 Actor 自己的属性，改动由基类的 PostEditChangeProperty 直接走
	// meta=(Recreate) 重建，我们再哈希一遍只会让同一次编辑重建两次。
	// 也不含 OrbitLOD —— 它只喂 LOD Invoker，由 RefreshLODInvoker 单独处理。
	uint64 Hash = 1469598103934665603ULL;
	// 形参名别叫 Data：基类有一个私有成员 AVoxelWorld::Data（TVoxelSharedPtr<FVoxelData>），
	// 遮蔽它会触发 C4458，而本项目按「警告即错误」编译。
	const auto Mix = [&Hash](const void* Source, int64 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Source);
		for (int64 Index = 0; Index < Size; Index++)
		{
			Hash = (Hash ^ Bytes[Index]) * 1099511628211ULL;
		}
	};

	// 混派生半径而不是原始 Radius：Actor 等比缩放改了同样要重建，而半径是
	// 唯一真正推给生成器的那个数。
	//
	// 早先这里还混过原始 ActorLocation（非局部坐标模式下），那是「移动 Actor
	// 即重建」的来源。现在球心恒在局部原点，移动永远不进哈希。
	const float ScaledRadius = GetScaledRadiusVoxels();
	Mix(&ScaledRadius, sizeof(ScaledRadius));

	Mix(&SurfaceColor, sizeof(SurfaceColor));

	// 地形模块按资产路径混合：若直接混入指针，重载后（地址变了、资产没变）
	// 哈希就会被破坏。注意这里只覆盖「换了一个模块」；「改了模块自己的参数」
	// 不在本函数职责内 —— 那由模块的 PostEditChangeProperty 调 InvalidateAndRebuild
	// 处理（它连哈希都不看）。
	const FString TerrainPath = Terrain ? Terrain->GetPathName() : FString();
	Mix(*TerrainPath, TerrainPath.Len() * sizeof(TCHAR));

	// 地质预设与地形模块同理：只混名字，不混地质场指针。
	const FString GeologyPresetName = GeologyPreset;
	Mix(*GeologyPresetName, GeologyPresetName.Len() * sizeof(TCHAR));

	return Hash;
}

#if WITH_EDITOR
namespace
{
	// 改完 Content/Geology/*.json 的快捷入口，省掉重启编辑器。
	FAutoConsoleCommandWithWorld ReloadGeologyCommand(
		TEXT("StellarEra.ReloadGeology"),
		TEXT("重新读取 Content/Geology 下的 JSON，并重建场景中所有体素行星。"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (!World)
			{
				return;
			}

			int32 PlanetCount = 0;
			for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
			{
				It->ReloadGeology();
				++PlanetCount;
			}

			UE_LOG(LogTemp, Log, TEXT("StellarEra.ReloadGeology: 重建了 %d 颗行星"), PlanetCount);
		}));
}
#endif
