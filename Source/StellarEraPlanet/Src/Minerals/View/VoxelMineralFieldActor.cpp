// 一个元素一个体素世界。

#include "VoxelMineralFieldActor.h"

#include "Minerals/View/PlanetMineralVoxelGenerator.h"
#include "VoxelGenerators/VoxelGeneratorPicker.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

AVoxelMineralFieldActor::AVoxelMineralFieldActor()
{
	// 基类构造函数已经把 WorldRoot 建好并设成 RootComponent。

	// 世界由 CreateForElement 显式创建；基类在 bCreateWorldAutomatically 为真时会排「下一帧 CreateWorld」，会和这里撞成建两次。
	bCreateWorldAutomatically = false;

	// 线程池是时序前置条件，不是「这里一定有」：游戏世界里本世界不自建池，只复用 GetPoolForWorld 找到的那个。
	// 行星在 OnConstruction 里建世并保证池先存在，所以矿物世界（CreateForElement）的调用点必须排在行星建世之后。
	// 池不存在时插件不失败：打 "global pool isn't created!" 并就地建 2 线程池 —— 功能照常、性能悄悄掉。
	// 编辑器预览（PlayType=Preview）走 CreateOwnPool，不查全局池；基类默认 true 会让每个矿物世界各自抢着建池。
	bCreateGlobalPool = false;

	// 矿物体素世界不参与玩法碰撞与导航。只对游戏世界（PlayType == Game）成立：基类 UpdateDynamicLODSettings 对非 Game
	// 强制打开碰撞三项。编辑器分支走 Toggle() → CreateInEditor → PlayType = Preview，所以预览里的矿体世界仍带碰撞，与这两行无关。
	bEnableCollisions = false;
	bEnableNavmesh = false;

	// 相机一动 N 个世界同时重建网格、同一帧整批提交，帧率掉到个位数。LOD 只在建世时算一次（Game 模式才生效）。
	bConstantLOD = true;
	// 与行星同值：基类 1000 ms 会让一批网格在一帧内全提交；抖动会让新旧两套网格同时画并逐块建 MID。
	MeshUpdatesBudget = 8.f;
	bDitherChunks = false;
}

bool AVoxelMineralFieldActor::CreateForElement(
	const FPlanetMineralVoxelParams& Params,
	const FLinearColor& Color,
	const AVoxelWorld& PlanetWorld,
	UMaterialInterface* BaseMaterial)
{
	// 没有 World 就没有体素世界可建：AVoxelWorld::CreateWorld 在编辑器构建里第一件事就是解引用 GetWorld()->WorldType，
	// 空指针当场访问违例且无诊断。早退不丢东西：本 Actor 没有「进世界之前」被调用的入口（不覆写 OnConstruction 等）。
	if (!GetWorld())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 矿物体素世界创建失败：Actor 不在任何世界里"));
		return false;
	}

	// --- 世界参数整份复制：两边体素索引必须一一对应 ---
	// 生成器在体素索引空间直接读行星数据、不做坐标换算，前提就是这组值与 Actor 变换完全一致。
	VoxelSize = PlanetWorld.VoxelSize;
	RenderOctreeDepth = PlanetWorld.RenderOctreeDepth;
	WorldSizeInVoxel = PlanetWorld.WorldSizeInVoxel;
	bUseCustomWorldBounds = PlanetWorld.bUseCustomWorldBounds;
	CustomWorldBounds = PlanetWorld.CustomWorldBounds;
	MaxLOD = PlanetWorld.MaxLOD;
	MinLOD = PlanetWorld.MinLOD;
	// 逐 LOD 材质复制是规格要求，但注意它优先于 VoxelMaterial（基类只在 LODData.Material 为空时才回退）：
	// 行星若给某 LOD 填了材质，矿物世界那些 LOD 会画成行星材质。当前资产都没填 LODMaterials，这里只是登记坑。
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
	// 必须在 CreateWorld 之前赋值：Generator 带 meta=(Recreate)，建世后再改会报 "already created" 并保留旧实例。
	MineralGenerator = NewObject<UPlanetMineralVoxelGenerator>(
		this, TEXT("MineralVoxelGenerator"));
	MineralGenerator->Configure(Params, Color);
	Generator = FVoxelGeneratorPicker(MineralGenerator.Get());

#if WITH_EDITOR
	// 编辑器世界里必须走插件的预览开关 Toggle()，不能直接调 CreateWorld()：OnPreBeginPIE 在 PlayType == Preview 时
	// 无条件拆掉体素世界，而 OnEndPIE 只在 bIsToggled 为真时建回来 —— 直接建的话，进 PIE 矿体就消失且不再回来。
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
	// Generator 是 FVoxelGeneratorPicker，自己持有 MineralGenerator 的强引用；只清 MineralGenerator 的话，
	// 生成器会被选择器吊着活到 Actor 销毁，带着过期参数。
	Generator = FVoxelGeneratorPicker();
	// VoxelMaterial 也是 UPROPERTY，指向下面这张 MID，MID 又提着父材质；不清的话整条链跟着 Actor 活到销毁。
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
