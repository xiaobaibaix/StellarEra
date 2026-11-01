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
 * 行星的水面组件。
 *
 * 三块几何（海洋 / 湖泊 / 河流）各是一张 UDynamicMeshComponent：它们的重建频率
 * 相同、材质相同，但分开的好处是「没有湖」时不会为湖留一个空分量。
 *
 * 组件本身不做任何 Tick：水面是静态几何，只在行星重建时整体重来一次。
 * 水面的高度取自水文场，与体素世界无关 —— 所以它在编辑器世界里也能显示。
 */
UCLASS(ClassGroup = (StellarEra), meta = (BlueprintSpawnableComponent))
class STELLARERAPLANET_API UPlanetWaterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPlanetWaterComponent();

	/**
	 * 重建水面。Field 为空 = 拆掉全部分量（这颗星不要水）。
	 *
	 * @param VoxelSize          体素边长（cm），把体素半径换算成行星局部空间的厘米
	 * @param PlanetRadiusVoxels 基准半径（体素），把河宽换算成球面角
	 * @param Terrain            地形模块。不为空时按它的 GetRawOffset 采样地表高度，
	 *                           把湖泊球冠的 2 格余量裁到岸线；为空 = 不裁剪。
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
	 * 水材质。为空时先按默认路径 `/Game/Code/Planet/Materials/M_PlanetWater`
	 * 加载（Task 7 的构建器生成）；仍然为空就只打一条 Warning 并跳过，不会崩。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体")
	TObjectPtr<UMaterialInterface> WaterMaterial;

	/** 海洋球壳每面的格数。96 在 1 km 半径行星上约 3 m 边长。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "水体", meta = (ClampMin = "8", ClampMax = "512"))
	int32 OceanResolution = 96;

private:
	void ClearWaterMeshes();
	void AddWaterMesh(UE::Geometry::FDynamicMesh3& Mesh, const FPlanetWaterConfig& Config, double VoxelSize);

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDynamicMeshComponent>> WaterMeshes;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WaterMaterials;
};
