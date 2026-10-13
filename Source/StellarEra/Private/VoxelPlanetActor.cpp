// Copyright Voxel Plugin SAS. All Rights Reserved.
// 将 UVoxelPlanetGenerator 封装为可直接摆放的 Actor。

#include "VoxelPlanetActor.h"

#include "VoxelWorld.h"
#include "VoxelWorldCreateInfo.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelComponents/VoxelInvokerComponent.h"
#include "VoxelPlanetGenerator.h"
#include "Materials/MaterialInterface.h"
#include "Components/SceneComponent.h"
#include "UObject/ConstructorHelpers.h"

AVoxelPlanetActor::AVoxelPlanetActor()
{
	PrimaryActorTick.bCanEverTick = false;

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
	return GetActorLocation() / FMath::Max(VoxelSize, 0.0001f);
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

void AVoxelPlanetActor::RebuildPlanet()
{
	if (!VoxelWorld)
	{
		return;
	}

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

	if (!Generator)
	{
		Generator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));
	}

	// --- 1. 把参数推入生成器 ---
	Generator->Radius = GetScaledRadiusVoxels();
	Generator->Center = GetCenterInVoxels();
	Generator->SurfaceColor = SurfaceColor;

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
	VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
}

uint64 AVoxelPlanetActor::ComputeParamsHash() const
{
	// 对 RebuildPlanet 推入生成器或 Voxel World 的每项设置的原始字节做
	// FNV-1a，外加 Actor 变换（经由派生的半径与球心）。每个字段单独混合，
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

	const float ScaledRadius = GetScaledRadiusVoxels();
	const FVector Center = GetCenterInVoxels();
	Mix(&ScaledRadius, sizeof(ScaledRadius));
	Mix(&Center, sizeof(Center));

	Mix(&SurfaceColor, sizeof(SurfaceColor));

	Mix(&VoxelSize, sizeof(VoxelSize));
	Mix(&VoxelThreads, sizeof(VoxelThreads));
	Mix(&MaxLOD, sizeof(MaxLOD));
	Mix(&OrbitLOD, sizeof(OrbitLOD));

	// 材质按资产路径混合：若直接混入指针，重载后（地址变了、资产没变）
	// 哈希就会被破坏。
	const FString PlanetMaterialPath = PlanetMaterial ? PlanetMaterial->GetPathName() : FString();
	Mix(*PlanetMaterialPath, PlanetMaterialPath.Len() * sizeof(TCHAR));

	return Hash;
}
