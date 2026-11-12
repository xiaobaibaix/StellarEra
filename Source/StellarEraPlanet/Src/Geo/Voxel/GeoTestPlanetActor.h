// 新地质流水线的独立测试星球：M0–M3 只在 GeoTestMap 里，不碰旧行星；M4 切换后删除。
#pragma once
#include "CoreMinimal.h"
#include "VoxelWorld.h"
#include "PlanetSnapshot.h"
#include "GeoTestPlanetActor.generated.h"

class UPlanetGeoGenerator;
class UPlanetRecipe;

UCLASS()
class STELLARERAPLANET_API AGeoTestPlanetActor : public AVoxelWorld
{
	GENERATED_BODY()

public:
	AGeoTestPlanetActor();

	UPROPERTY(EditAnywhere, Category = "Geo")
	TObjectPtr<UPlanetRecipe> Recipe;

	/** 拷配方 → 后台烘焙 → 回游戏线程建世界。烘焙中再次调用会被忽略。 */
	void StartBake();
	bool IsBakeInFlight() const { return bBakeInFlight; }
	TSharedPtr<const FPlanetSnapshot> GetSnapshot() const { return Snapshot; }

	static int32 ComputeWorldSize(double RadiusVoxels);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UPlanetGeoGenerator> GeoGenerator;

	TSharedPtr<const FPlanetSnapshot> Snapshot;
	bool bBakeInFlight = false;
	// 后台烘焙回调可能晚于 PIE 结束；此时再 CreateWorld 会撞上世界拆除。
	bool bShuttingDown = false;

	void OnBakeFinished(TSharedPtr<const FPlanetSnapshot> InSnapshot, double RadiusVoxels, double BakeSeconds);
};
