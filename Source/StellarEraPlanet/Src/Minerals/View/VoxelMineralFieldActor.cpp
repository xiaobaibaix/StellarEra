// 一个元素一个体素世界。

#include "VoxelMineralFieldActor.h"

#include "Minerals/View/PlanetMineralVoxelGenerator.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AVoxelMineralFieldActor::AVoxelMineralFieldActor()
{
	// 基类构造函数已经把 WorldRoot 建好并设成 RootComponent。

	// 世界由 CreateForElement 显式创建。基类在 bCreateWorldAutomatically 为真时
	// 会排一个「下一帧 CreateWorld」的定时器，会和我们自己那次撞成建两次。
	bCreateWorldAutomatically = false;

	// 线程池由行星在建世时已经建好，这里只复用。基类默认 true 会让每个矿物世界
	// 各自抢着建池。
	bCreateGlobalPool = false;

	// 矿物体素世界不参与任何玩法碰撞与导航。
	bEnableCollisions = false;
	bEnableNavmesh = false;
}

bool AVoxelMineralFieldActor::CreateForElement(
	const FPlanetMineralVoxelParams& Params,
	const FLinearColor& Color,
	const AVoxelWorld& PlanetWorld,
	UMaterialInterface* BaseMaterial)
{
	// --- 世界参数整份复制：两边体素索引必须一一对应 ---
	//
	// 生成器在体素索引空间里直接读行星数据，不做任何坐标换算，前提就是这一组值
	// 与 Actor 变换完全一致。
	VoxelSize = PlanetWorld.VoxelSize;
	RenderOctreeDepth = PlanetWorld.RenderOctreeDepth;
	WorldSizeInVoxel = PlanetWorld.WorldSizeInVoxel;
	bUseCustomWorldBounds = PlanetWorld.bUseCustomWorldBounds;
	CustomWorldBounds = PlanetWorld.CustomWorldBounds;
	MaxLOD = PlanetWorld.MaxLOD;
	MinLOD = PlanetWorld.MinLOD;
	LODMaterials = PlanetWorld.LODMaterials;
	MaterialConfig = EVoxelMaterialConfig::RGB;

	SetActorTransform(PlanetWorld.GetActorTransform());

	// --- 材质 ---
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界缺少父材质"));
		return false;
	}

	FieldMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	if (!FieldMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界的动态材质创建失败"));
		return false;
	}
	FieldMaterial->SetVectorParameterValue(TEXT("BlockColor"), Color);
	VoxelMaterial = FieldMaterial;

	// --- 生成器 ---
	//
	// 必须在 CreateWorld 之前赋值：Generator 带 meta=(Recreate)，建世之后改它
	// 会报 "already created" 并保留旧生成器实例。
	MineralGenerator = NewObject<UPlanetMineralVoxelGenerator>(
		this, TEXT("MineralVoxelGenerator"));
	MineralGenerator->Configure(Params, Color);
	Generator = FVoxelGeneratorPicker(MineralGenerator.Get());

#if WITH_EDITOR
	// 编辑器世界里必须走插件自己的预览开关 Toggle()，不能直接调 CreateWorld()。
	//
	// AVoxelWorld::OnPreBeginPIE 在 PlayType == Preview 时无条件拆掉体素世界，
	// 而 OnEndPIE 只在 bIsToggled 为真时才建回来；bIsToggled 只有 Toggle() 会置真。
	// 直接 CreateWorld() 的话，编辑器里一进 PIE 矿体就消失且再也不会回来。
	// 与 AVoxelPlanetActor::RebuildPlanet 的同名分支是同一个理由。
	if (GetWorld() && GetWorld()->WorldType == EWorldType::Editor)
	{
		Toggle();
	}
	else
#endif
	{
		CreateWorld(FVoxelWorldCreateInfo());
	}

	if (!IsCreated())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界创建失败"));
		return false;
	}

	return true;
}

void AVoxelMineralFieldActor::DestroyField()
{
	if (IsCreated())
	{
		DestroyWorld();
	}
	// Generator 是 FVoxelGeneratorPicker，它自己持有一份 MineralGenerator 的强引用
	// （TVoxelGeneratorPicker::Object）。只清 MineralGenerator 的话，生成器会被
	// 这个选择器吊着活到 Actor 销毁，带着已经过期的参数。
	Generator = FVoxelGeneratorPicker();
	MineralGenerator = nullptr;
	FieldMaterial = nullptr;
}

void AVoxelMineralFieldActor::SetShellColor(const FLinearColor& Color)
{
	if (FieldMaterial)
	{
		FieldMaterial->SetVectorParameterValue(TEXT("BlockColor"), Color);
	}
}
