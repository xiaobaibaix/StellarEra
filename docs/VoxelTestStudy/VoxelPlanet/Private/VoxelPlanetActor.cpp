// 可放置的星球 Actor —— 实现。
//
// 这个类很薄：只做三件事
//   1. 创建一个生成器 UObject 和一个体素世界
//   2. 把设置同步给生成器
//   3. 重建体素世界
//
// 地形和材质计算都在纯计算类里，不在这里。

#include "VoxelPlanetActor.h"

#include "VoxelWorld.h"
#include "VoxelWorldCreateInfo.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "VoxelPlanetGenerator.h"

AVoxelPlanetActor::AVoxelPlanetActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 生成器是纯 UObject：没有位置、没有变换、不参与渲染
	Generator = NewObject<UVoxelPlanetGenerator>(this, TEXT("VoxelPlanetGenerator"));

	// 真正负责渲染的是体素世界
	VoxelWorld = CreateDefaultSubobject<AVoxelWorld>(TEXT("VoxelWorld"));
}

float AVoxelPlanetActor::GetScaledRadiusVoxels() const
{
	// 只取等比缩放。
	// 非等比缩放会切变体素网格，而密度场定义在体素空间，
	// 结果会是块畸形的斜块，而不是等比缩放的球。
	const float UniformScale = GetActorScale3D().GetAbsMax();
	return Radius * UniformScale;
}

FVector AVoxelPlanetActor::GetCenterInVoxels() const
{
	if (bUseLocalCoordinates)
	{
		// 局部坐标模式：密度场的球心锚在生成器原点，
		// 由 Actor 变换把它搬到轨道位置。
		//
		// 这是宇宙场景必须用的模式。Voxel Plugin 的渲染网格挂在
		// WorldRoot 下，会完整跟随 Actor 变换，但生成器参数
		// 只在 OnConstruction / BeginPlay 时求值一次——
		// 运行时 Actor 被 N 体系统移动，不会再触发重建。
		// 若此时 Center 还是「世界原点换算值」，
		// 密度场就会留在原地，而 Actor 已经飞走了。
		return FVector::ZeroVector;
	}

	// 世界坐标模式：球心跟着 Actor 走。
	// 适合固定不动的场景。
	return GetActorLocation() / FMath::Max(Scale.VoxelSize, 0.0001f);
}

void AVoxelPlanetActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPlanet();
}

void AVoxelPlanetActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildPlanet();
}

void AVoxelPlanetActor::RebuildPlanet()
{
	if (!Generator || !VoxelWorld)
	{
		return;
	}

	// --- 1. 同步基础参数 ---
	Generator->Radius = GetScaledRadiusVoxels();
	Generator->Center = GetCenterInVoxels();
	Generator->Seed = Seed;

	// --- 2. 同步地形分层 ---
	Generator->Continents = Continents;
	Generator->Mountains = Mountains;
	Generator->Details = Details;
	Generator->Ravines = Ravines;

	// --- 3. 同步内部结构 ---
	Generator->Caves = Caves;
	Generator->Shell = Shell;

	// --- 4. 同步表面 ---
	Generator->Sea = Sea;
	Generator->Land = Land;

	// --- 5. 同步尺寸 ---
	Generator->Scale = Scale;

	// --- 6. 配置体素世界 ---
	// VoxelSize 必须一致，否则碰撞体与可见网格会对不上
	VoxelWorld->VoxelSize = Scale.VoxelSize;

	// 用 Type=Object 交出一个活实例，参数已经设好。
	// 若用 Type=Class，改的是类默认对象(CDO)，
	// 会被关卡里所有星球共享 —— 一处改动全部受污染。
	VoxelWorld->Generator = FVoxelGeneratorPicker(Generator);

	// --- 7. 重建，让新生成器生效 ---
	VoxelWorld->CreateWorld(FVoxelWorldCreateInfo());
}
