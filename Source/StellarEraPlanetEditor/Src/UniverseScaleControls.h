// 宇宙尺度控制：改 DistanceScale 时，把所有天体相对参考恒星的位置按基准距离重新缩放。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UniverseScaleControls.generated.h"

UCLASS(meta = (DisplayName = "UniverseScaleControls"))
class STELLARERAPLANETEDITOR_API AUniverseScaleControls : public AActor
{
	GENERATED_BODY()

public:
	AUniverseScaleControls();

	virtual bool IsEditorOnly() const override { return true; }
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** 真实"行星—恒星"距离（厘米），缩放基准：DistanceScale=1 时行星应落在这个距离上。 */
	UPROPERTY(EditAnywhere, Category = "Universe|Scale",
		meta = (DisplayName = "BaselineDistanceCm", ClampMin = "1.0"))
	double BaselineDistanceCm = 1.496e13;

	/** 压缩倍率：行星距恒星 = BaselineDistanceCm / 该值；其余天体按同一比例相对恒星缩放。 */
	UPROPERTY(EditAnywhere, Category = "Universe|Scale",
		meta = (DisplayName = "DistanceScale", ClampMin = "1.0"))
	double DistanceScale = 100.0;

	/** 模拟年 / 真实秒：PIE 与打包游戏都以此为准（覆盖子系统的 Config）。 */
	UPROPERTY(EditAnywhere, Category = "Universe|Scale",
		meta = (DisplayName = "TimeScale", ClampMin = "0.0000001"))
	double TimeScale = 0.00333;

	/** 缩放中心（恒星）。留空 = 自动取质量最大的天体。 */
	UPROPERTY(EditAnywhere, Category = "Universe|Scale", meta = (DisplayName = "ReferenceStar"))
	TSoftObjectPtr<AActor> ReferenceStar;

	/** 决定基准距离的行星。留空 = 自动取关卡里的体素行星。 */
	UPROPERTY(EditAnywhere, Category = "Universe|Scale", meta = (DisplayName = "ReferenceBody"))
	TSoftObjectPtr<AActor> ReferenceBody;

	/** 按当前 DistanceScale 重新摆放所有天体（改倍率时自动调用）。 */
	UFUNCTION(CallInEditor, Category = "Universe|Scale", meta = (DisplayName = "ApplyScale"))
	void ApplyScale();
};
