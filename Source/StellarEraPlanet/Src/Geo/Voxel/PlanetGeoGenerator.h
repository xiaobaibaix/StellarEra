// VoxelFree 适配器：只把体素坐标转给快照求值核，自己不含地质数学。
#pragma once
#include "CoreMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "PlanetSnapshot.h"
#include "PlanetGeoGenerator.generated.h"

/** 体素 RGB 调试着色：None 写真实编码；Elevation 只染地表附近，地下仍按岩性。 */
UENUM(BlueprintType)
enum class EPlanetGeoDebugColor : uint8
{
	None,
	Lithology,
	Elevation,
};

UCLASS()
class STELLARERAPLANET_API UPlanetGeoGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;

	void SetSnapshot(TSharedPtr<const FPlanetSnapshot> InSnapshot) { Snapshot = MoveTemp(InSnapshot); }
	TSharedPtr<const FPlanetSnapshot> GetSnapshot() const { return Snapshot; }

	/** 必须 VisibleAnywhere：UVoxelGenerator 会把 EditAnywhere 属性当生成器参数枚举，只认少数类型。 */
	UPROPERTY(VisibleAnywhere, Category = "Geo")
	EPlanetGeoDebugColor DebugColor = EPlanetGeoDebugColor::Lithology;

private:
	TSharedPtr<const FPlanetSnapshot> Snapshot;
};

class FPlanetGeoGeneratorInstance : public TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>;

	FPlanetGeoGeneratorInstance(UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, EPlanetGeoDebugColor InDebugColor);

	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;
	/** 基类纯虚，Helper 不提供默认实现：行星上「上」是径向。 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;

private:
	const TSharedPtr<const FPlanetSnapshot> Snapshot;
	const EPlanetGeoDebugColor DebugColor;
};
