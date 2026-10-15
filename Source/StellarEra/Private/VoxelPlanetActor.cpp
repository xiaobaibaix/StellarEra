// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#include "VoxelPlanetActor.h"

#include "VoxelWorld.h"
#include "VoxelWorldCreateInfo.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelPlanetGenerator.h"
#include "VoxelPlanetTerrainModule.h"
#include "Materials/MaterialInterface.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

AVoxelPlanetActor::AVoxelPlanetActor()
{
#if WITH_EDITOR
	// 每颗行星默认使用项目的顶点色材质。仅编辑器有效：打包后的游戏里
	// 实例自带序列化值。早于此属性存在的已摆放 Actor 没有对应标签，
	// 加载时同样会采用这个 CDO 默认值。
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> PlanetMaterialFinder(
		TEXT("/Game/Materials/M_PlanetVertexColor.M_PlanetVertexColor"));
	if (PlanetMaterialFinder.Succeeded())
	{
		PlanetMaterial = PlanetMaterialFinder.Object;
	}
#endif

	// 生成器在 RebuildPlanet 里懒创建：若在构造函数中创建，会在类默认对象上
	// 实例化一个子对象，摆好的 Actor 保存时就会因引用解析进 /Script 而报
	// "Illegal reference to private object"。
	// 实际渲染由 Voxel World 完成。
	VoxelWorld = CreateDefaultSubobject<AVoxelWorld>(TEXT("VoxelWorld"));

	// 没有 RootComponent 的 Actor 无法被 Gizmo 移动（GetActorLocation 恒为零）。
	// 根组件位于球心，LOD Invoker 挂在其下，随行星一起变换。
	PlanetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetRoot"));
	SetRootComponent(PlanetRoot);

	LODInvoker = CreateDefaultSubobject<UVoxelSimpleInvokerComponent>(TEXT("PlanetLODInvoker"));
	LODInvoker->SetupAttachment(PlanetRoot);
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
}

float AVoxelPlanetActor::GetScaledRadiusVoxels() const
{
	// 只采用等比缩放。非等比缩放会剪切体素网格，而密度场定义在体素空间中，
	// 得到的将是扭曲的畸形而非缩放后的行星。
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

FVector AVoxelPlanetActor::GetCenterInVoxels() const
{
	// 生成器以体素为单位工作，而 Actor 变换的单位是厘米。
	//
	// 取的是 Actor 原点在 VoxelWorld 局部空间里的位置，而不是直接
	// GetActorLocation() / VoxelSize：渲染时 LocalToGlobal 还会再乘一次
	// VoxelWorld 的变换，只有先做逆变换，球心的世界位置才恰好落在 Actor 原点上。
	// 少了这一步，VoxelWorld 的变换会被重复计入，整颗星就偏离 Actor。
	const FVector ActorLocation = GetActorLocation();
	const FVector LocalLocation = VoxelWorld
		? VoxelWorld->GetActorTransform().InverseTransformPosition(ActorLocation)
		: ActorLocation;
	return LocalLocation / FMath::Max(VoxelSize, 0.0001f);
}

void AVoxelPlanetActor::OnConstruction(const FTransform& Transform)
{
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
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// OnConstruction 只在移动/添加时重新执行：否则单纯在细节面板里改参数
	// 永远不会重建世界，改动会静默失效。
	RebuildPlanet();
}
#endif

void AVoxelPlanetActor::SyncVoxelWorldTransform()
{
	if (!VoxelWorld)
	{
		return;
	}

	// AVoxelWorld 是 Actor 子对象：构造函数里的 CreateDefaultSubobject 只建出一个
	// 实例，它的变换不会跟着 PlanetRoot 走。而它自己的变换是会被序列化下来的 ——
	// 世界原点重定基（AVoxelWorld::ApplyWorldOffset）留下的陈旧位移会一直留在关卡
	// 里，把整颗星顶到离 Actor 几公里外。
	//
	// 先比较再赋值，免得每次都把组件标脏。
	//
	// 局部坐标模式下本函数还会被 N 体子系统**每帧**调用：星球每帧移动，这个变换
	// 不跟上的话渲染网格会留在原地、星球跑掉。
	const FTransform ActorTransform = GetActorTransform();
	if (!VoxelWorld->GetActorTransform().Equals(ActorTransform, 0.001f))
	{
		VoxelWorld->SetActorTransform(ActorTransform);
	}
}

void AVoxelPlanetActor::RebuildPlanet()
{
	if (!VoxelWorld)
	{
		return;
	}

	// --- 0. 让体素世界的原点与 Actor 的原点重合 ---
	// AVoxelWorld 是 Actor 子对象，它的变换不跟着 PlanetRoot 走（详见
	// SyncVoxelWorldTransform）。必须排在哈希守卫**之前**：编辑器里拖动星球时
	// 哈希不变、会走早退分支，只有放在守卫前面渲染网格才会跟着 Actor 走。
	// 也必须排在取球心之前 —— 球心按该变换的逆变换算出来。
	SyncVoxelWorldTransform();

	// 细节面板的一次编辑会同时触发 PostEditChangeProperty 和重新执行
	// OnConstruction：没有这道守卫，改一个参数就会把世界重建两次，生成时间
	// 翻倍，流送重置还会再次打开背面的空洞。这里复查 IsCreated()，保证全新
	// 的世界（地图加载、PIE 复制）总能构建。
	const uint64 ParamsHash = ComputeParamsHash();
	if (ParamsHash == LastParamsHash && VoxelWorld->IsCreated())
	{
		return;
	}
	LastParamsHash = ParamsHash;

	// 重建一次打一行：这是「移动星球到底有没有触发重建」的唯一可观测证据。
	// 正常情况下拖动星球不应该产生新行（局部坐标模式），
	// 若每帧刷屏就说明哈希守卫失效了。
	UE_LOG(LogTemp, Log, TEXT("AVoxelPlanetActor: 重建 %s（参数哈希 %llu）"), *GetName(), ParamsHash);

	if (!Generator)
	{
		Generator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));
	}

	// --- 1. 把参数推入生成器 ---
	Generator->Radius = GetScaledRadiusVoxels();
	Generator->Center = GetCenterInVoxels();
	Generator->SurfaceColor = SurfaceColor;
	// 地形模块也推给生成器：生成器是「这颗行星」的，模块是「这种地形」的。
	// 模块本身不带位置和尺寸，同一份资产可以喂给任意多颗行星。
	Generator->Terrain = Terrain;

	// --- 2. 配置 Voxel World ---
	// VoxelSize 必须一致，否则碰撞将与网格对不上。
	VoxelWorld->VoxelSize = VoxelSize;
	// 2 个线程（插件默认值）喂不饱大行星的流送：背面区块比正面晚到很久，
	// 表现为表面上的方形空洞。更多网格化线程能更快把它们补上。
	VoxelWorld->NumberOfThreads = FMath::Clamp(VoxelThreads, 1, 16);
	VoxelWorld->MaxLOD = FMath::Clamp(MaxLOD, 0, 25);

	// 表面材质必须在 CreateWorld 之前就位：渲染器会在创建时把它快照进各
	// LOD 的设置里；缺材质构建的世界会记录 "Invalid VoxelMaterial" 并渲染
	// 成平面灰色，之后再赋什么都没用。
	if (PlanetMaterial)
	{
		VoxelWorld->VoxelMaterial = PlanetMaterial;
	}

	// Type=Object 交出一个参数已设置完毕的活实例。
	// 若用 Type=Class，改动的会是类默认对象，而它被关卡中所有行星全局共享。
	VoxelWorld->Generator = FVoxelGeneratorPicker(Generator.Get());

	// --- 3. 行星全局 LOD 基准 ---
	// Invoker 设置在每次 LOD 更新时都会被实时读取，这里趁重建前刷新一次，
	// 保证新建世界第一帧就用上正确精度。
	if (LODInvoker)
	{
		LODInvoker->LODToSet = ComputeOrbitLOD();
		// 范围以球心为原点向六方延伸，略超出半径即可覆盖整颗星。
		LODInvoker->LODRange = GetScaledRadiusVoxels() * FMath::Max(VoxelSize, 0.0001f) * 1.25f + 1000.f;
		UVoxelInvokerComponentBase::RefreshAllVoxelInvokers();
	}

	// --- 4. 重建世界，让新生成器生效 ---
	// CreateWorld 会报 "already created" 并保留旧生成器实例（它在创建时把
	// 所有参数快照为 const），因此不先销毁的话，参数编辑会被静默忽略。
	if (VoxelWorld->IsCreated())
	{
		VoxelWorld->DestroyWorld();
	}

#if WITH_EDITOR
	// 编辑器世界里必须走插件自己的「预览开关」Toggle()，不能直接调 CreateWorld()。
	//
	// AVoxelWorld::OnPreBeginPIE（VoxelWorld.cpp:1585）在 PlayType == Preview 时
	// 无条件拆掉体素世界并打 "Unloading world"，而 OnEndPIE（:1593）只在
	// bIsToggled 为真时才把它建回来。bIsToggled（VoxelWorld.h:767）默认 false，
	// 且只有 Toggle()（:1338）会置真 —— 我们直接调 CreateWorld() 从没按过那个
	// 开关，于是 PIE 一结束整颗星就从视口里消失，直到有人手动改一次属性触发
	// PostEditChangeProperty 才回来。
	//
	// 不能改用 Tick 兜底：编辑器世界的 Actor 默认不 Tick
	// （FTickFunction::bTickInEditor 默认为 false），那种代码在编辑器里从不执行。
	//
	// 只在编辑器世界这么做：PIE 与打包世界要的是 PlayType == Game，
	// 而 Toggle() 会把 PlayType 设成 Preview。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		VoxelWorld->Toggle();
		return;
	}
#endif

	VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
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
	if (!Generator || !Generator->Terrain)
	{
		return GetSurfaceRadiusCm();
	}

	const FVector Direction = WorldDirection.GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return GetSurfaceRadiusCm();
	}

	// 上下文与生成器里那份保持一致：模块在两条路径（密度场、地表查询）上必须
	// 拿到同一个半径和球心，否则角色脚下的地面和眼睛看到的地面会不是同一个面。
	FVoxelPlanetTerrainContext Context;
	Context.Radius = GetScaledRadiusVoxels();
	Context.Center = GetCenterInVoxels();
	Context.BaseColor = SurfaceColor;

	// 体素空间与世界空间之间只差一个等比缩放，没有旋转，方向可以原样传递。
	return Generator->Terrain->GetSurfaceRadiusVoxels(Context, Direction) * VoxelSize;
}

uint64 AVoxelPlanetActor::ComputeParamsHash() const
{
	// 对 RebuildPlanet 推入生成器或 Voxel World 的每项设置的原始字节做
	// FNV-1a，外加 Actor 变换（原始位置与派生的半径）。每个字段单独混合，
	// 结构体填充字节就不会把未初始化的数据漏进哈希。
	uint64 Hash = 1469598103934665603ULL;
	const auto Mix = [&Hash](const void* Data, int64 Size)
	{
		const uint8* Bytes = static_cast<const uint8*>(Data);
		for (int64 Index = 0; Index < Size; Index++)
		{
			Hash = (Hash ^ Bytes[Index]) * 1099511628211ULL;
		}
	};

	// 混原始位置而不是换算后的球心：球心会与 VoxelWorld 的变换相消、退化成常量，
	// 拿它当哈希的话移动 Actor 不会触发重建，体素世界就跟不上了。
	// （局部坐标模式下这正是我们想要的，见下面的守卫：移动本就无需重建。）
	const float ScaledRadius = GetScaledRadiusVoxels();
	Mix(&ScaledRadius, sizeof(ScaledRadius));

	// 局部坐标模式下**不能**混 ActorLocation：N 体子系统每帧都在移动这颗星球，
	// 混进去等于每帧销毁并重建整个体素世界。球心锚在局部原点、世界位置由 Actor
	// 变换承担，与生成器的参数无关，所以移动本来就不需要重建。
	if (!bUseLocalCoordinates)
	{
		const FVector ActorLocation = GetActorLocation();
		Mix(&ActorLocation, sizeof(ActorLocation));
	}

	Mix(&SurfaceColor, sizeof(SurfaceColor));

	Mix(&VoxelSize, sizeof(VoxelSize));
	Mix(&VoxelThreads, sizeof(VoxelThreads));
	Mix(&MaxLOD, sizeof(MaxLOD));
	Mix(&OrbitLOD, sizeof(OrbitLOD));

	// 材质按资产路径混合：若直接混入指针，重载后（地址变了、资产没变）
	// 哈希就会被破坏。
	const FString PlanetMaterialPath = PlanetMaterial ? PlanetMaterial->GetPathName() : FString();
	Mix(*PlanetMaterialPath, PlanetMaterialPath.Len() * sizeof(TCHAR));

	// 地形模块同理，也是按路径混。注意这里只覆盖「换了一个模块」；「改了模块
	// 自己的参数」不在本函数职责内 —— 那由模块的 PostEditChangeProperty 调
	// InvalidateAndRebuild 处理（它连哈希都不看）。
	const FString TerrainPath = Terrain ? Terrain->GetPathName() : FString();
	Mix(*TerrainPath, TerrainPath.Len() * sizeof(TCHAR));

	return Hash;
}
