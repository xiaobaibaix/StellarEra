// 行星水面组件。

#include "PlanetWaterComponent.h"

#include "Components/DynamicMeshComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetHydrologyField.h"
#include "PlanetWaterSurfaceBuilder.h"
#include "VoxelPlanetTerrainModule.h"

namespace
{
	/**
	 * Task 7 的 PlanetWaterMaterialBuilder 生成资产的固定路径。
	 *
	 * 全工程没有硬引用指向它（组件是运行期加载的），所以打包时靠
	 * `Config/DefaultGame.ini` 里的 `DirectoriesToAlwaysCook=(Path="/Game/Code/Planet/Materials")`
	 * 保底，否则 cook 会把它剔掉、打包版静默没有水。
	 */
	const TCHAR* const PlanetWaterComponentMaterialPath =
		TEXT("/Game/Code/Planet/Materials/M_PlanetWater.M_PlanetWater");
}

UPlanetWaterComponent::UPlanetWaterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// 组件本身没有碰撞体：碰撞是下面那张 DynamicMesh 的事（USceneComponent 没有
	// 碰撞接口）。水面是纯视觉，角色会直接走到海底（设计 §1）。
}

void UPlanetWaterComponent::RebuildWater(
	const FPlanetHydrologyField* Field,
	const FPlanetWaterConfig& Config,
	const double VoxelSize,
	const double PlanetRadiusVoxels,
	const UVoxelPlanetTerrainModule* Terrain)
{
	ClearWaterMeshes();

	// 没有世界就不建分量：未注册的 UDynamicMeshComponent 会留在关卡里，
	// 每次重建再叠一批，最后既看不见又删不掉。
	if (!Field || !Field->IsValid() || !GetWorld() || !GetOwner())
	{
		return;
	}

	// 细节面板没设过材质时按默认路径取一次。资产是 Task 7 的编辑器构建器生成的；
	// 没跑过构建器（或没 cook）时只警告、不生成水面，而不是让整个组件崩掉。
	if (!WaterMaterial)
	{
		WaterMaterial = LoadObject<UMaterialInterface>(nullptr, PlanetWaterComponentMaterialPath);
	}
	if (!WaterMaterial)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("行星水面组件没有材质：跳过水面生成（在编辑器里跑一次 StellarEra.BuildPlanetWaterMaterial，或在细节面板指定 WaterMaterial）"));
		return;
	}

	PlanetWaterSurfaceBuilder::FBuildParams BuildParams;
	BuildParams.VoxelSize = VoxelSize;
	BuildParams.PlanetRadiusVoxels = PlanetRadiusVoxels;
	BuildParams.OceanResolution = OceanResolution;

	// 地形高度只给湖泊用：湖冠的 2 格余量必须落在地形高过水位的地方，否则那圈
	// 水面底下是空气。地表半径与水文烘焙同一条公式：
	//   半径 = 基准半径 + Amplitude × 归一化位移。
	if (Terrain)
	{
		BuildParams.TerrainAmplitude = Config.Params.Amplitude;
		BuildParams.TerrainHeightFn = [Terrain](const FVector& UnitDir)
		{
			return Terrain->GetRawOffset(UnitDir);
		};
	}

	UE::Geometry::FDynamicMesh3 Mesh;
	PlanetWaterSurfaceBuilder::BuildOcean(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize, TEXT("Water_Ocean"));

	PlanetWaterSurfaceBuilder::BuildLakes(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize, TEXT("Water_Lakes"));

	PlanetWaterSurfaceBuilder::BuildRivers(*Field, BuildParams, Mesh);
	AddWaterMesh(Mesh, Config, VoxelSize, TEXT("Water_Rivers"));
}

void UPlanetWaterComponent::ClearWaterMeshes()
{
	for (const TObjectPtr<UDynamicMeshComponent>& Mesh : WaterMeshes)
	{
		if (IsValid(Mesh))
		{
			// 分量用的是固定名字（Water_Ocean 等）。销毁只是标脏，名字要到 GC 才
			// 释放；下一次重建再 NewObject 同名对象会撞上旧名字槽位。先把旧的挪到
			// 瞬态包并改成唯一名，把 Owner 下的名字腾出来。
			const FName TrashName = MakeUniqueObjectName(
				GetTransientPackage(), UDynamicMeshComponent::StaticClass(), TEXT("WaterMesh_Trash"));
			Mesh->Rename(*TrashName.ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
			Mesh->DestroyComponent();
		}
	}
	WaterMeshes.Reset();
	WaterMaterials.Reset();
}

void UPlanetWaterComponent::AddWaterMesh(
	UE::Geometry::FDynamicMesh3& Mesh,
	const FPlanetWaterConfig& Config,
	const double VoxelSize,
	const TCHAR* MeshName)
{
	if (Mesh.TriangleCount() == 0)
	{
		return;
	}

	// RF_Transient：这张分量只属于本次运行。不标的话，PIE 的内存复制会把所有
	// 非 Transient 的 OwnedComponents 一起复制进 PIE 世界，而 ClearWaterMeshes
	// 只认识本数组里的那些 —— 结果是两份水面叠在一起。
	UDynamicMeshComponent* Component = NewObject<UDynamicMeshComponent>(
		GetOwner(), FName(MeshName), RF_Transient);
	if (!Component)
	{
		return;
	}

	// 先设移动性再注册：注册之后再改会走一次组件重注册，还刷一条 Warning。
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetMesh(MoveTemp(Mesh));

	// 水面不参与碰撞、不投影：它是一张纯视觉的壳。
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetComplexAsSimpleCollisionEnabled(false);
	Component->SetCastShadow(false);

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(WaterMaterial, this);
	if (Material)
	{
		Material->SetVectorParameterValue(TEXT("ShallowColor"), Config.ShallowColor);
		Material->SetVectorParameterValue(TEXT("DeepColor"), Config.DeepColor);
		Material->SetVectorParameterValue(TEXT("FoamColor"), Config.FoamColor);
		// 材质里的 FoamDepth 是厘米；配置是体素。
		Material->SetScalarParameterValue(TEXT("FoamDepth"), Config.FoamDepthVoxels * VoxelSize);
		Material->SetScalarParameterValue(TEXT("MaxOpacity"), Config.MaxOpacity);
		Component->SetMaterial(0, Material);
	}

	Component->RegisterComponent();
	Component->AttachToComponent(this, FAttachmentTransformRules::KeepRelativeTransform);

	WaterMeshes.Add(Component);
	WaterMaterials.Add(Material);
}
