// VoxelFree 适配器：只把体素坐标转给快照求值核，自己不含地质数学。调试着色与剖切也在这一层。
#pragma once
#include "CoreMinimal.h"
#include "VoxelGenerators/VoxelGeneratorHelpers.h"
#include "PlanetSnapshot.h"
#include "PlanetGeoGenerator.generated.h"

struct FGeoDebugLayer;

/** 体素 RGB 调试着色：None 写真实编码；Elevation 只染地表附近，地下仍按岩性。 */
UENUM(BlueprintType)
enum class EPlanetGeoDebugColor : uint8
{
	None,
	Lithology,
	Elevation,
};

/** 剖切：把星球切掉一块露出内部，切面按地下图层着色。改它要重建网格（约 2 s）。 */
UENUM(BlueprintType)
enum class EPlanetGeoCutaway : uint8
{
	None    UMETA(DisplayName = "不切"),
	Half    UMETA(DisplayName = "切掉 +X 半球"),
	Quarter UMETA(DisplayName = "切掉 +X、−Y 四分之一"),
};

UCLASS()
class STELLARERACELESTIAL_API UPlanetGeoGenerator : public UVoxelGenerator
{
	GENERATED_BODY()

public:
	virtual TVoxelSharedRef<FVoxelGeneratorInstance> GetInstance() override;

	void SetSnapshot(TSharedPtr<const FPlanetSnapshot> InSnapshot) { Snapshot = MoveTemp(InSnapshot); }
	TSharedPtr<const FPlanetSnapshot> GetSnapshot() const { return Snapshot; }

	// 必须 VisibleAnywhere：UVoxelGenerator 会把 EditAnywhere 属性当生成器参数枚举，只认少数类型。
	UPROPERTY(VisibleAnywhere, Category = "Geo")
	EPlanetGeoDebugColor DebugColor = EPlanetGeoDebugColor::Lithology;

	UPROPERTY(VisibleAnywhere, Category = "Geo")
	EPlanetGeoCutaway Cutaway = EPlanetGeoCutaway::None;

	/** 非空时地表按这个图层着色（覆盖 DebugColor 的地表部分）。 */
	UPROPERTY(VisibleAnywhere, Category = "Geo")
	FName SurfaceLayer;

	/** 剖切面着色用的图层。空 = 保留地质编码，由材质解码；非空时盖调试色。 */
	UPROPERTY(VisibleAnywhere, Category = "Geo")
	FName VolumeLayer;

private:
	TSharedPtr<const FPlanetSnapshot> Snapshot;
};

struct FPlanetGeoDebugView
{
	EPlanetGeoDebugColor Color = EPlanetGeoDebugColor::Lithology;
	EPlanetGeoCutaway Cutaway = EPlanetGeoCutaway::None;
	/** 指向默认图层表（静态，生命周期覆盖进程）。 */
	const FGeoDebugLayer* SurfaceLayer = nullptr;
	const FGeoDebugLayer* VolumeLayer = nullptr;
};

class FPlanetGeoGeneratorInstance : public TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>
{
public:
	using Super = TVoxelGeneratorInstanceHelper<FPlanetGeoGeneratorInstance, UPlanetGeoGenerator>;

	FPlanetGeoGeneratorInstance(UPlanetGeoGenerator& Owner, TSharedPtr<const FPlanetSnapshot> InSnapshot, const FPlanetGeoDebugView& InView);

	v_flt GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	FVoxelMaterial GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const;
	TVoxelRange<v_flt> GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const;
	/** 基类纯虚，Helper 不提供默认实现：行星上「上」是径向。 */
	virtual FVector GetUpVector(v_flt X, v_flt Y, v_flt Z) const override final;

private:
	const TSharedPtr<const FPlanetSnapshot> Snapshot;
	const FPlanetGeoDebugView View;
};
