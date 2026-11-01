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

	// 线程池是**时序前置条件**，不是「这里一定有」：本世界自己不建池，只复用
	// IVoxelPool::GetPoolForWorld 找到的那个。
	//
	// 行星在 OnConstruction 里建世，并且用 IsWorldVoxelPoolCreated /
	// CreateWorldVoxelThreadPool 保证池先存在（VoxelPlanetActor.cpp:749-761）。
	// 矿物世界由 CreateForElement 显式建世，因此**调用点必须排在行星建世之后**。
	// 池不存在时插件不会失败：它打一条 "global pool isn't created!" 警告，就地建
	// 一个 2 线程池（VoxelWorld.cpp:846-854）—— 功能照常，性能悄悄掉下来。
	// Task 4/5 接线时要守住「先行星、后矿物」这个顺序。
	//
	// 基类默认 true 会让每个矿物世界各自抢着建池。
	bCreateGlobalPool = false;

	// 矿物体素世界不参与玩法碰撞与导航。
	//
	// 这只对**游戏世界**（PlayType == Game）成立：基类 UpdateDynamicRendererSettings
	// 对任何非 Game 的 PlayType 强制打开碰撞 —— bEnableCollisions /
	// bComputeVisibleChunksCollisions / VisibleChunksCollisionsMaxLOD 三项一起被覆盖
	//（VoxelWorld.cpp:1197-1199）。我们的编辑器分支走 Toggle() → CreateInEditor →
	// PlayType = Preview，所以编辑器预览里的矿物体素世界**仍然带碰撞**，跟这两行无关。
	// 打包 / PIE 的游戏路径不受影响。
	bEnableCollisions = false;
	bEnableNavmesh = false;
}

bool AVoxelMineralFieldActor::CreateForElement(
	const FPlanetMineralVoxelParams& Params,
	const FLinearColor& Color,
	const AVoxelWorld& PlanetWorld,
	UMaterialInterface* BaseMaterial)
{
	// 没有世界就没有体素世界可建：AVoxelWorld::CreateWorld 第一件事就是取
	// GetWorld()->WorldType（VoxelWorld.cpp:244-246），空指针当场访问违例，且不带
	// 任何诊断。AVoxelPlanetActor::RebuildPlanet 对同一条约束有同款守卫
	//（VoxelPlanetActor.cpp:585-590），这里跟它一致。
	//
	// 早退不丢东西：本 Actor 没有任何在「进世界之前」被调用的入口
	//（不覆写 OnConstruction / PostEditChangeProperty），所以不存在
	// 「CDO 上被调用、真进世界时不会重来」的那条路径。
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界创建失败：Actor 不在任何世界里"));
		return false;
	}

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
	// 逐 LOD 材质一并复制是规格要求，但有个副作用要知道：它**优先于** VoxelMaterial
	// —— 基类只在 LODData.Material 为空时才回退到 VoxelMaterial
	//（VoxelWorld.cpp:1229-1232）。行星若真的给某个 LOD 填了材质，矿物世界里那些
	// LOD 的区块会画成**行星的材质**，而不是下面那张矿物 MID。当前资产都没填
	// LODMaterials（空数组），所以这里只是把坑登记下来。
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
	// VoxelMaterial 同样是 UPROPERTY（VoxelWorld.h:251），指向下面这张 MID，MID 又
	// 提着父材质 —— 不清的话这整条链跟着 Actor 活到销毁。和 Generator 是同一类
	// 残留，一起清，本函数的契约才是统一的「什么都不再持有」。
	VoxelMaterial = nullptr;
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
