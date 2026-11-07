// 行星水面：一组跟随行星的 UDynamicMeshComponent。

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PlanetHydrologyTypes.h"
#include "PlanetWaterComponent.generated.h"

class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UVoxelPlanetTerrainModule;
class FPlanetHydrologyField;

/**
 * 行星的水面组件。三块几何（海洋 / 湖泊 / 河流）各是一张 UDynamicMeshComponent，
 * 分开的好处是「没有湖」时不留空分量。不做 Tick：水面高度取自水文场、与体素世界
 * 无关，只在行星重建时整体重来，所以编辑器世界里也能显示。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetWaterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetWaterComponent();

	/**
	 * 重建水面。Field 为空 = 拆掉全部分量（这颗星不要水）。
	 * Terrain 不为空时按 GetRawOffset 采样地表高度，把湖冠的 2 格余量裁到岸线；
	 * 为空 = 不裁剪。
	 */
	void RebuildWater(
		const FPlanetHydrologyField* Field,
		const FPlanetWaterConfig& Config,
		double VoxelSize,
		double PlanetRadiusVoxels,
		const UVoxelPlanetTerrainModule* Terrain = nullptr);

	/** 当前水面分量数（0–3）。给测试与调试用。 */
	int32 GetWaterMeshCount() const { return WaterMeshes.Num(); }

	/**
	 * 水材质。为空时按默认路径 `/Game/Code/Planet/Materials/M_PlanetWater` 加载；
	 * 仍然为空就只打一条 Warning 并跳过，不会崩。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/**
	 * 海洋球壳每面的格数。96 在 1 km 半径上约 16 m/格；规格的 2–4 m 需要
	 * 2–8 亿三角形，当前预算下不可达。海洋观感靠半透明 + 菲涅尔，不靠几何密度。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体", meta = (ClampMin = "8", ClampMax = "512"))
	int32 OceanResolution = 96;

private:
	void ClearWaterMeshes();

	/**
	 * 建一个水面分量。MeshName 固定（Water_Ocean / Water_Lakes / Water_Rivers）：
	 * 测试与探针按名字读，不依赖创建顺序猜下标。
	 */
	void AddWaterMesh(
		UE::Geometry::FDynamicMesh3& Mesh,
		const FPlanetWaterConfig& Config,
		double VoxelSize,
		const TCHAR* MeshName);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> WaterMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;
};
