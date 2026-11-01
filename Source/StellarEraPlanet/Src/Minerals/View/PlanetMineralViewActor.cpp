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

	// 可见性有两套标志，按世界类型各认一套：
	//
	// - 游戏世界只认 AActor::bHidden（SetActorHiddenInGame）。
	// - 编辑器世界里 SetActorHiddenInGame **对渲染没有效果**：USceneComponent::ShouldRender
	//   用的是 bShowInEditor = !bHiddenEdTemporary && !Owner->IsHiddenEd()
	//   （SceneComponent.cpp:3485 起），而 IsHiddenEd() 不读 bHidden。所以编辑器预览
	//   必须同时翻临时隐藏标志；它是非序列化状态（SetIsTemporarilyHiddenInEditor），
	//   不会把关卡标脏 —— 与 PlanetMineralViewComponent.cpp:271-272 隐藏行星用的是同一招。
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
	// 不可重入：MineralMeshes.Reset() 不销毁旧组件，组件名又是固定的，第二次进来
	// NewObject(..., TEXT("MineralShell") / TEXT("MineralMesh%d")) 会在同一个 Outer 上
	// 撞名并致命退出。将来做「改 CellsPerFeature 后重建」必须先销毁旧组件再清这里的标志。
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
	//
	// 分三相，因为两边的线程约束正好相反：
	//   ① 建组件 + 材质：NewObject / RegisterComponent / SetMaterial 只能在游戏线程；
	//   ② 灌网格：FDynamicMesh3 的顶点、三角形、法线都是**纯数据**，逐元素独立，
	//      可以并行 —— 这是整个构建里最贵的一段（地球上 376 万三角形约 1.4 s，
	//      单线程灌满）；
	//   ③ SetMesh 送回组件：碰渲染资源，回游戏线程。
	//
	// 元素之间互不写入同一份数据，所以并行不改变结果：网格内容与顺序都与串行一致。
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
		RejectedByElement[ElementIndex] = Rejected;

		// 等值面抽取只给顶点与三角形，没有法线；材质靠 VertexNormalWS 取它，
		// 所以这里按面积加权的顶点法线补一遍。UV 层留空：材质不采样 UV。
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

	// 三个资产都在这里加载：LoadMineralViewMaterial 是本文件的匿名命名空间函数，
	// 组件那一侧看不见它，所以不能让调用方把材质传进来。
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

	// 没有世界就没有体素世界可建：
	//  - 下面的 World->SpawnActor 要非空 World；
	//  - AVoxelWorld::CreateWorld 在**编辑器构建**里第一件事就是解引用
	//    GetWorld()->WorldType（VoxelWorld.cpp:244-246：244 行是 #if WITH_EDITOR，
	//    245-246 才是解引用；这段在非编辑器构建里被编译掉，那时它走后面的路径）。
	// 空指针当场访问违例且不带任何诊断。AVoxelMineralFieldActor::CreateForElement 对
	// 同一条约束有同款守卫（VoxelMineralFieldActor.cpp:47-62），这里跟它一致。
	//
	// 放在 bHasBuilt 之前：这一条失败时本 Actor 什么都没建，不该被标成「已建」。
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
	//
	// 失败就整体回滚：留一半的世界比彻底没建更糟 —— 图例下标会对上一半的矿。
	ElementFields.Reset();
	ElementFields.Reserve(ParamsByElement.Num());

	// AlwaysSpawn：碰撞拒绝的 spawn 会静默返回 nullptr，读起来像「体素世界创建失败」。
	// Owner 指向本观察体：Actor 图里能看出归属，也方便在 Outliner 里找到。
	//
	// ObjectFlags 必须把观察体自己的 RF_Transient 镜像过去：编辑器世界里观察体就是
	// 用 RF_Transient spawn 的（PlanetMineralViewComponent.cpp:231-236），目的正是
	// 永不落进 umap；但 UWorld::SpawnActor 只从 spawn 参数取 flags 与 Level
	// （LevelActor.cpp:669 / :537），不会继承。不镜像的话，编辑器预览会在保存
	// UniverseMap 时把 N 个矿物体素世界一并写进关卡。
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.ObjectFlags |= GetFlags() & RF_Transient;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// 循环里不再重算：CreateForElement 稍后会用同一份 Actor 变换覆盖 spawn 位置，
	// 循环内每轮取一次纯属白费。
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
				// 与 ReleaseAllFields 走同一条清理：漏了 DestroyField 的话，
				// 体素世界在编辑器世界里要一直吊到 GC 才消失。
				Field->DestroyField();
				Field->Destroy();
			}
			ReleaseAllFields();
			return false;
		}

		ElementFields.Add(Field);
	}

	// 先把观察体自己落到球心再取基准：Build 用的是 GetActorLocation()，这里也是 ——
	// 「基准 = 球心」由本函数构造，不再依赖调用方 spawn 时给的位置。
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
	// 每帧都会调用：位置没变就别写。SetActorLocation 会把 AVoxelWorld 的
	// primitive transform 标脏，N 个世界逐个标脏一遍纯属白费。
	if (PlanetCenterWorld != LastPlanetCenterWorld)
	{
		// 观察体自己（球壳）跟着走。
		SetActorLocation(PlanetCenterWorld);

		// 体素世界不 attach 到本 Actor：AVoxelWorld 在构造时把 WorldRoot 立为根组件，
		// 再挂一个父级会让体素世界的局部/全局变换语义打架。改成直接写位置。
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
	// Destroy()：UWorld::DestroyActor 是 Destroyed() 的**唯一**触发点
	// （LevelActor.cpp:926），编辑器世界与游戏世界都会走。编辑器预览不跑 BeginPlay、
	// EndPlay 会被跳过，所以这是那条路径上唯一的钩子。体素世界不 attach、不随本
	// Actor 级联销毁，必须在这里显式销毁。
	//
	// 关卡卸载与整世界拆解**不**经过这里（前者的流送移除走 EndPlay，
	// Level.cpp:3931-3948；后者走 BeginDestroy）。
	ReleaseAllFields();
	Super::Destroyed();
}

void APlanetMineralViewActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 无缝切图 / 关卡卸载里那些「有 EndPlay 但不 Destroy」的路径。
	// 编辑器预览保护不到（World 不跑 BeginPlay，RouteEndPlay 直接跳过），那条归
	// Destroyed()。两者都幂等：同一个体素世界只会被销毁一次，第二次是空操作。
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
