// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "PlanetMineralViewActor.h"

#include "PlanetMineralMaterialBuilder.h"
#include "VoxelMineralFieldActor.h"

#include "Async/ParallelFor.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshNormals.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "VoxelWorld.h"

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

	// 可见性两套标志按世界类型各认一套：游戏世界认 bHidden（SetActorHiddenInGame）；编辑器世界里 SetActorHiddenInGame 对渲染无效，
	// ShouldRender 读的是 bShowInEditor（只看 bHiddenEdTemporary / IsHiddenEd），所以必须同时翻非序列化的临时隐藏标志，否则关卡会被标脏。
	void SetFieldVisible(AVoxelMineralFieldActor& Field, bool bVisible)
	{
		Field.SetActorHiddenInGame(!bVisible);
#if WITH_EDITOR
		if (Field.GetWorld() && Field.GetWorld()->WorldType == EWorldType::Editor)
		{
			Field.SetIsTemporarilyHiddenInEditor(!bVisible);
		}
#endif
	}

	bool IsFieldVisible(const AVoxelMineralFieldActor& Field)
	{
#if WITH_EDITOR
		if (Field.GetWorld() && Field.GetWorld()->WorldType == EWorldType::Editor)
		{
			return !Field.IsTemporarilyHiddenInEditor();
		}
#endif
		return !Field.IsHidden();
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
	// 不可重入：MineralMeshes.Reset() 不销毁旧组件，组件名又固定，第二次 NewObject 会在同一 Outer 上撞名并致命退出。
	// 将来做「改 CellsPerFeature 后重建」必须先销毁旧组件再清这个标志。
	ensureMsgf(!bHasBuilt, TEXT("[矿物] APlanetMineralViewActor::Build 不可重入"));
	if (bHasBuilt)
	{
		return false;
	}

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

	// 到这里已经没有「什么都没建就失败」的退路了：从这里开始建组件，重入必然撞名。
	bHasBuilt = true;

	// --- 球壳 ---
	CreateShell(SphereMesh, GhostMaterial, ShellColor, SurfaceRadiusCm);

	// --- 每个元素一个动态网格 ---
	// 分三相，线程约束相反：① 建组件 + 材质只能在游戏线程；② 灌网格是纯数据、逐元素独立，可并行（最贵，约 1.4 s）；
	// ③ SetMesh 碰渲染资源，回游戏线程。元素之间不写同一份数据，并行不改变结果。
	MineralMeshes.Reset();
	MineralMeshes.Reserve(MeshesByElement.Num());

	TArray<TObjectPtr<UDynamicMeshComponent>> Components;
	Components.SetNum(MeshesByElement.Num());

	for (int32 ElementIndex = 0; ElementIndex < MeshesByElement.Num(); ++ElementIndex)
	{
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
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[矿物] 元素 #%d 的动态材质创建失败，矿体将留在默认材质"), ElementIndex);
		}

		Components[ElementIndex] = Component;
	}

	TArray<UE::Geometry::FDynamicMesh3> Meshes;
	Meshes.SetNum(MeshesByElement.Num());
	TArray<int32> RejectedByElement;
	RejectedByElement.SetNumZeroed(MeshesByElement.Num());

	ParallelFor(MeshesByElement.Num(), [&MeshesByElement, &Meshes, &RejectedByElement]
		(int32 ElementIndex)
	{
		const FPlanetMineralMesh& Source = MeshesByElement[ElementIndex];
		UE::Geometry::FDynamicMesh3& Mesh = Meshes[ElementIndex];
		Mesh.EnableAttributes();

		// 下标先过一遍映射表：等值面给出的顶点将来若出现空洞，这里才不会静默错位。
		TArray<int32> VertexIds;
		VertexIds.Reserve(Source.VerticesCm.Num());
		for (const FVector3f& VertexCm : Source.VerticesCm)
		{
			VertexIds.Add(Mesh.AppendVertex(UE::Math::TVector<double>(
				double(VertexCm.X), double(VertexCm.Y), double(VertexCm.Z))));
		}

		// AppendTriangle 在退化三角形上返回 -1 而不抛错：丢掉是正确行为，但数量要进日志 —— 全丢时画面上就是「这个元素没矿」。
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
		RejectedByElement[ElementIndex] = Rejected;

		// 等值面只给顶点与三角形，没有法线；材质靠 VertexNormalWS 取它，这里按面积加权补顶点法线。UV 层留空（材质不采样 UV）。
		if (Mesh.TriangleCount() > 0)
		{
			UE::Geometry::FMeshNormals::InitializeOverlayToPerVertexNormals(
				Mesh.Attributes()->PrimaryNormals());
		}
	});

	for (int32 ElementIndex = 0; ElementIndex < MeshesByElement.Num(); ++ElementIndex)
	{
		// 日志放在游戏线程：并行块里打日志会把行序和线程搅在一起。
		if (RejectedByElement[ElementIndex] > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[矿物] 元素 #%d：%d 个三角形被丢弃（退化或越界）"),
				ElementIndex, RejectedByElement[ElementIndex]);
		}

		Components[ElementIndex]->SetMesh(MoveTemp(Meshes[ElementIndex]));
		MineralMeshes.Add(Components[ElementIndex]);
	}

	// 两条路径共用同一个基准：没人调 SyncToPlanetLocation 时它也不该白写一遍位置。
	LastPlanetCenterWorld = GetActorLocation();

	return true;
}

///////////////////////////////////////////////////////////////////////////////
// 体素世界路径

void APlanetMineralViewActor::CreateShell(UStaticMesh* SphereMesh, UMaterialInterface* GhostMaterial,
	const FLinearColor& ShellColor, float SurfaceRadiusCm)
{
	// --- 球壳：引擎球是 100 cm 直径，缩放到行星直径 ---
	// 运行期建组件必须在 RegisterComponent 之前设好 Mobility：注册后再改会走重新注册路径，而这块网格第一帧起就要跟着行星移动。
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
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[矿物] 球壳动态材质创建失败，球壳将留在默认材质"));
	}
}

bool APlanetMineralViewActor::BuildVoxelWorlds(
	const TArray<FPlanetMineralVoxelParams>& ParamsByElement,
	const AVoxelWorld& PlanetWorld,
	const FLinearColor& ShellColor,
	float SurfaceRadiusCm,
	const TArray<FPlanetGeologyElement>& Elements)
{
	ensureMsgf(!bHasBuilt, TEXT("[矿物] APlanetMineralViewActor 已建过，不可重入"));
	if (bHasBuilt)
	{
		return false;
	}

	if (SurfaceRadiusCm <= 0.f)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 参数非法：半径 %.1f cm"), SurfaceRadiusCm);
		return false;
	}

	if (ParamsByElement.Num() != Elements.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 体素世界数不等于元素数：%d / %d"),
			ParamsByElement.Num(), Elements.Num());
		return false;
	}

	// 三个资产都在这里加载：LoadMineralViewMaterial 是本文件匿名命名空间的函数，组件那侧看不见，不能让调用方传材质进来。
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

	// 没有 World 就没有体素世界可建（SpawnActor 要非空 World；AVoxelWorld::CreateWorld 在编辑器构建里第一件事就是解引用
	// GetWorld()->WorldType，空指针当场访问违例且无诊断）。放在 bHasBuilt 之前：失败时什么都没建，不该标成「已建」。
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("[矿物] 体素世界创建失败：观察体不在任何世界里"));
		return false;
	}

	// 到这里已经没有「什么都没建就失败」的退路了：后面要么全成、要么整体回滚。
	bHasBuilt = true;

	// --- 球壳：与 marching cubes 路径完全一样，不改 ---
	CreateShell(SphereMesh, GhostMaterial, ShellColor, SurfaceRadiusCm);

	// --- 每元素一个体素世界 ---
	// 失败就整体回滚：留一半的世界比彻底没建更糟 —— 图例下标会对上一半的矿。
	ElementFields.Reset();
	ElementFields.Reserve(ParamsByElement.Num());

	// AlwaysSpawn：碰撞拒绝的 spawn 会静默返回 nullptr，读起来像创建失败。Owner 指向本观察体以便在 Outliner 找到。
	// ObjectFlags 必须镜像观察体自己的 RF_Transient：UWorld::SpawnActor 不继承 flags，否则编辑器预览会在保存关卡时把 N 个体素世界写进 umap。
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.ObjectFlags |= GetFlags() & RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 循环外取一次变换：CreateForElement 稍后会用同一份 Actor 变换覆盖 spawn 位置。
	const FTransform PlanetTransform = PlanetWorld.GetActorTransform();
	for (int32 ElementIndex = 0; ElementIndex < ParamsByElement.Num(); ++ElementIndex)
	{
		AVoxelMineralFieldActor* Field = World->SpawnActor<AVoxelMineralFieldActor>(
			AVoxelMineralFieldActor::StaticClass(),
			PlanetTransform.GetLocation(), PlanetTransform.Rotator(), SpawnParams);

		if (!Field
			|| !Field->CreateForElement(ParamsByElement[ElementIndex],
				Elements[ElementIndex].Color, PlanetWorld, BlockMaterial))
		{
			UE_LOG(LogTemp, Error, TEXT("[矿物] 元素 #%d 的体素世界创建失败"), ElementIndex);
			if (Field)
			{
				// 与 ReleaseAllFields 同一条清理：漏了 DestroyField 的话，体素世界在编辑器世界里要吊到 GC 才消失。
				Field->DestroyField();
				Field->Destroy();
			}
			ReleaseAllFields();
			return false;
		}

		ElementFields.Add(Field);
	}

	// 先把观察体落到球心再取基准：「基准 = 球心」由本函数构造，不依赖调用方 spawn 时给的位置。
	SetActorLocation(PlanetWorld.GetActorLocation());
	LastPlanetCenterWorld = GetActorLocation();
	return true;
}

void APlanetMineralViewActor::ReleaseAllFields()
{
	for (AVoxelMineralFieldActor* Field : ElementFields)
	{
		if (Field)
		{
			Field->DestroyField();
			Field->Destroy();
		}
	}
	ElementFields.Reset();
}

void APlanetMineralViewActor::SyncToPlanetLocation(const FVector& PlanetCenterWorld)
{
	// 每帧都调：位置没变就别写，SetActorLocation 会把 AVoxelWorld 的 primitive transform 标脏。
	if (PlanetCenterWorld != LastPlanetCenterWorld)
	{
		// 观察体自己（球壳）跟着走。
		SetActorLocation(PlanetCenterWorld);

		// 体素世界不 attach（AVoxelWorld 把 WorldRoot 立为根，挂父级会让局部/全局变换语义打架），直接写位置。
		for (AVoxelMineralFieldActor* Field : ElementFields)
		{
			if (Field)
			{
				Field->SetActorLocation(PlanetCenterWorld);
			}
		}

		LastPlanetCenterWorld = PlanetCenterWorld;
	}
}

AVoxelMineralFieldActor* APlanetMineralViewActor::GetElementField(int32 Index) const
{
	return ElementFields.IsValidIndex(Index) ? ElementFields[Index].Get() : nullptr;
}

bool APlanetMineralViewActor::SetElementColor(int32 Index, const FLinearColor& Color)
{
	AVoxelMineralFieldActor* Field = GetElementField(Index);
	if (!Field)
	{
		return false;
	}
	Field->SetShellColor(Color);
	return true;
}

void APlanetMineralViewActor::Destroyed()
{
	// Destroy()：UWorld::DestroyActor 是 Destroyed() 的唯一触发点，编辑器与游戏世界都会走；编辑器预览不跑 BeginPlay、EndPlay 被跳过，
	// 这是那条路径唯一的钩子。体素世界不 attach、不级联销毁，必须在这里显式销毁。关卡卸载与整世界拆解走 EndPlay / BeginDestroy。
	ReleaseAllFields();
	Super::Destroyed();
}

void APlanetMineralViewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 无缝切图 / 关卡卸载里「有 EndPlay 但不 Destroy」的路径；编辑器预览走不到，那条归 Destroyed()。
	// 两者都幂等，同一个体素世界只会被销毁一次。
	ReleaseAllFields();
	Super::EndPlay(EndPlayReason);
}

UDynamicMeshComponent* APlanetMineralViewActor::GetElementMesh(int32 Index) const
{
	return MineralMeshes.IsValidIndex(Index) ? MineralMeshes[Index].Get() : nullptr;
}

int32 APlanetMineralViewActor::NumElements() const
{
	// 两条路径互斥，同一时刻只有一条非空。体素路径优先。
	return ElementFields.Num() > 0 ? ElementFields.Num() : MineralMeshes.Num();
}

bool APlanetMineralViewActor::IsElementVisible(int32 Index) const
{
	if (ElementFields.IsValidIndex(Index) && ElementFields[Index])
	{
		return IsFieldVisible(*ElementFields[Index]);
	}
	return MineralMeshes.IsValidIndex(Index) && MineralMeshes[Index]
		&& MineralMeshes[Index]->IsVisible();
}

void APlanetMineralViewActor::SetElementVisible(int32 Index, bool bVisible)
{
	if (ElementFields.IsValidIndex(Index) && ElementFields[Index])
	{
		SetFieldVisible(*ElementFields[Index], bVisible);
		return;
	}
	if (!MineralMeshes.IsValidIndex(Index) || !MineralMeshes[Index])
	{
		return;
	}
	MineralMeshes[Index]->SetVisibility(bVisible, true);
}

void APlanetMineralViewActor::SetAllElementsVisible(bool bVisible)
{
	for (AVoxelMineralFieldActor* Field : ElementFields)
	{
		if (Field)
		{
			SetFieldVisible(*Field, bVisible);
		}
	}
	for (UDynamicMeshComponent* Component : MineralMeshes)
	{
		if (Component)
		{
			Component->SetVisibility(bVisible, true);
		}
	}
}
