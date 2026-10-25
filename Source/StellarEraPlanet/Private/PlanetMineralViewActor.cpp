// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewActor.h"

#include "PlanetMineralBlocks.h"
#include "PlanetMineralMaterialBuilder.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* const SphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const TCHAR* const CubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	/** 方块朝向的随机翻转幅度（度）。 */
	constexpr float BlockTiltDegrees = 25.f;

	UMaterialInterface* LoadMaterial(const TCHAR* PackagePath, const TCHAR* AssetName)
	{
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), PackagePath, AssetName);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
#if WITH_EDITOR
		if (!Material)
		{
			// 首次运行、或材质资产还没生成：就地建一次再加载。打包版没有这条路。
			FString Error;
			if (PlanetMineralMaterials::Build(Error))
			{
				Material = LoadObject<UMaterialInterface>(nullptr, *ObjectPath);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[矿物] 材质生成失败：%s"), *Error);
			}
		}
#endif
		return Material;
	}
}

APlanetMineralViewActor::APlanetMineralViewActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// 必须有根组件：没有根，SetActorLocation 不生效（新 spawn 的裸 Actor 位置恒为 0）。
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

bool APlanetMineralViewActor::Build(
	const TArray<TArray<FPlanetMineralBlock>>& BlocksByElement,
	const FLinearColor& ShellColor, float SurfaceRadiusCm, float BlockSizeCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	if (SurfaceRadiusCm <= 0.f || BlockSizeCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm，方块 %.1f cm"),
			SurfaceRadiusCm, BlockSizeCm);
		return false;
	}

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, SphereMeshPath);
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, CubeMeshPath);
	UMaterialInterface* GhostMaterial = LoadMaterial(
		PlanetMineralMaterials::GetGhostMaterialPath(), TEXT("M_PlanetGhost"));
	UMaterialInterface* BlockMaterial = LoadMaterial(
		PlanetMineralMaterials::GetBlockMaterialPath(), TEXT("M_MineralBlock"));

	if (!SphereMesh || !CubeMesh || !GhostMaterial || !BlockMaterial)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[矿物] 网格或材质加载失败：球=%s 方块=%s 球壳材质=%s 方块材质=%s"),
			SphereMesh ? TEXT("ok") : TEXT("缺"),
			CubeMesh ? TEXT("ok") : TEXT("缺"),
			GhostMaterial ? TEXT("ok") : TEXT("缺"),
			BlockMaterial ? TEXT("ok") : TEXT("缺"));
		return false;
	}

	// --- 球壳：引擎球是 100 cm 直径，缩放到行星直径 ---
	// 运行期建的组件必须在 RegisterComponent 之前设好 Mobility：注册之后再改会走
	// 重新注册的路径，而这块网格从第一帧起就要跟着行星移动。
	const float DiameterCm = SurfaceRadiusCm * 2.f;
	ShellMesh = NewObject<UStaticMeshComponent>(this, TEXT("MineralShell"));
	ShellMesh->SetupAttachment(SceneRoot);
	ShellMesh->SetMobility(EComponentMobility::Movable);
	ShellMesh->SetStaticMesh(SphereMesh);
	ShellMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShellMesh->SetCastShadow(false);
	ShellMesh->RegisterComponent();
	ShellMesh->SetRelativeLocation(FVector::ZeroVector);
	ShellMesh->SetRelativeScale3D(FVector(DiameterCm / 100.f));

	ShellMaterial = UMaterialInstanceDynamic::Create(GhostMaterial, this);
	if (ShellMaterial)
	{
		ShellMaterial->SetVectorParameterValue(TEXT("ShellColor"), ShellColor);
		ShellMesh->SetMaterial(0, ShellMaterial);
	}

	// --- 每个元素一个 ISM ---
	if (BlocksByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[矿物] 分组数与元素数不一致：%d 组 / %d 个元素"),
			BlocksByElement.Num(), Elements.Num());
		return false;
	}

	const float CubeScale = BlockSizeCm / 100.f;
	BlockMeshes.Reset();
	BlockMeshes.Reserve(BlocksByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < BlocksByElement.Num(); ++ElementIndex)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(
			this, *FString::Printf(TEXT("MineralBlocks%d"), ElementIndex));
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(CubeMesh);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BlockMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BlockColor"), Elements[ElementIndex].Color);
			Component->SetMaterial(0, MID);
		}

		// 每个元素一个独立随机流：元素增删不会打乱别的元素已有的方块朝向。
		FRandomStream OrientationRandom(ElementIndex + 1);
		const TArray<FPlanetMineralBlock>& Group = BlocksByElement[ElementIndex];
		for (const FPlanetMineralBlock& Block : Group)
		{
			// 绕随机轴 ±25° 翻身，避免整片方块像规则网格。
			const FQuat Rotation(
				OrientationRandom.GetUnitVector(),
				FMath::DegreesToRadians(
					OrientationRandom.FRandRange(-BlockTiltDegrees, BlockTiltDegrees)));
			Component->AddInstance(FTransform(Rotation, Block.LocationCm, FVector(CubeScale)));
		}

		Component->SetVisibility(true);
		BlockMeshes.Add(Component);
	}

	return true;
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	return BlockMeshes.IsValidIndex(Index) && BlockMeshes[Index]
		&& BlockMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (!BlockMeshes.IsValidIndex(Index) || !BlockMeshes[Index])
	{
		return;
	}
	// bPropagateToChildren = true：ISM 没有子组件，但保持一致语义。
	BlockMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (UInstancedStaticMeshComponent* Component : BlockMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
