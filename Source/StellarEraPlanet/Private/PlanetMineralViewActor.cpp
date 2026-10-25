// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewActor.h"

#include "PlanetMineralMaterialBuilder.h"

#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* const MineralViewSphereMeshPath = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	UMaterialInterface* LoadMineralViewMaterial(const TCHAR* PackagePath, const TCHAR* AssetName)
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

bool APlanetMineralViewActor::Build(const TArray<FPlanetMineralMesh>& MeshesByElement,
	const FLinearColor& ShellColor, float SurfaceRadiusCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	if (SurfaceRadiusCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm"), SurfaceRadiusCm);
		return false;
	}

	if (MeshesByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 网格数不等于元素数：%d / %d"),
			MeshesByElement.Num(), Elements.Num());
		return false;
	}

	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, MineralViewSphereMeshPath);
	UMaterialInterface* GhostMaterial = LoadMineralViewMaterial(
		PlanetMineralMaterials::GetGhostMaterialPath(), TEXT("M_PlanetGhost"));
	UMaterialInterface* BlockMaterial = LoadMineralViewMaterial(
		PlanetMineralMaterials::GetBlockMaterialPath(), TEXT("M_MineralBlock"));

	if (!SphereMesh || !GhostMaterial || !BlockMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 网格或材质加载失败：球=%s 球壳材质=%s 矿体材质=%s"),
			SphereMesh ? TEXT("ok") : TEXT("缺"),
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

	// --- 每个元素一个动态网格 ---
	MineralMeshes.Reset();
	MineralMeshes.Reserve(MeshesByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < MeshesByElement.Num(); ++ElementIndex)
	{
		const FPlanetMineralMesh& Source = MeshesByElement[ElementIndex];

		UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(
			this, *FString::Printf(TEXT("MineralMesh%d"), ElementIndex));
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->RegisterComponent();

		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BlockMaterial, this);
		if (MID)
		{
			MID->SetVectorParameterValue(TEXT("BlockColor"), Elements[ElementIndex].Color);
			Component->SetMaterial(0, MID);
		}

		UE::Geometry::FDynamicMesh3 Mesh;
		Mesh.EnableAttributes();

		// 下标先过一遍映射表：等值面给出的顶点将来若出现空洞，这里才不会静默错位。
		TArray<int32> VertexIds;
		VertexIds.Reserve(Source.VerticesCm.Num());
		for (const FVector3f& VertexCm : Source.VerticesCm)
		{
			VertexIds.Add(Mesh.AppendVertex(UE::Math::TVector<double>(
				double(VertexCm.X), double(VertexCm.Y), double(VertexCm.Z))));
		}

		// AppendTriangle 在退化三角形（三点共线 / 面积为 0）上返回 -1 而不是抛错。
		// 丢掉它们是正确行为，但数量要进日志 —— 全被丢掉时画面上就是「这个元素没矿」。
		int32 Rejected = 0;
		for (int32 Index = 0; Index + 2 < Source.Triangles.Num(); Index += 3)
		{
			const int32 A = Source.Triangles[Index];
			const int32 B = Source.Triangles[Index + 1];
			const int32 C = Source.Triangles[Index + 2];
			if (!VertexIds.IsValidIndex(A) || !VertexIds.IsValidIndex(B)
				|| !VertexIds.IsValidIndex(C)
				|| Mesh.AppendTriangle(VertexIds[A], VertexIds[B], VertexIds[C]) < 0)
			{
				++Rejected;
			}
		}
		if (Rejected > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[矿物] 元素 #%d：%d 个三角形被丢弃（退化或越界）"),
				ElementIndex, Rejected);
		}

		// 等值面抽取只给顶点与三角形，没有法线；材质靠 VertexNormalWS 取它，
		// 所以这里按面积加权的顶点法线补一遍。UV 层留空：材质不采样 UV。
		if (Mesh.TriangleCount() > 0)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(
				Mesh.Attributes()->PrimaryNormals());
		}

		Component->SetMesh(MoveTemp(Mesh));
		MineralMeshes.Add(Component);
	}

	return true;
}

UDynamicMeshComponent* APlanetMineralViewActor::GetElementMesh(int32 Index) const
{
	return MineralMeshes.IsValidIndex(Index) ? MineralMeshes[Index].Get() : nullptr;
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	return MineralMeshes.IsValidIndex(Index) && MineralMeshes[Index]
		&& MineralMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (!MineralMeshes.IsValidIndex(Index) || !MineralMeshes[Index])
	{
		return;
	}
	MineralMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (UDynamicMeshComponent* Component : MineralMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
