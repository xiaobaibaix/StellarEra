// 轨道拖尾 —— 位置历史条带。
//
// 方案 A：纯 C++ 实现，不依赖 Niagara 等引擎特效系统。
// 每帧记录当前位置，连成一条渐隐的三角带。
//
// 为什么不用 Niagara：
//   1. Niagara System 资产必须手工在编辑器里做，代码无法生成
//   2. Niagara 寿命按「时间」计，而 TimeScale 调大后逻辑时间与
//      渲染时间不同步，尾迹长度会变得不可预测
//   3. 纯 C++ 版本可在无渲染环境下测试，也更容易控制精度
//
// 实现要点：
//   · 顶点位置存在组件局部空间，用 SetRelativeLocation 搬到世界
//   · 条带朝向由相邻两点的方向决定，用「看向摄像机」的方式展开
//   · 用固定「宽度」而非屏幕空间宽度，避免远近粗细差异过大

#pragma once

#include "CoreMinimal.h"
#include "Components/MeshComponent.h"
#include "CosmosTrail.generated.h"

/**
 * 轨道拖尾组件。
 *
 * 挂在天体上，记录它最近 N 帧的位置并渲染成渐隐的带子。
 */
UCLASS(ClassGroup = (Cosmos), meta = (BlueprintSpawnableComponent))
class VOXELCOSMOS_API UCosmosTrailComponent : public UMeshComponent
{
	GENERATED_BODY()

public:
	UCosmosTrailComponent();

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

	//////////////////////////////////////////////////////////////////////
	// 参数

	/** 是否显示拖尾。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail")
	bool bEnabled = true;

	/**
	 * 历史点数量。
	 * 越多越长，但也越吃内存和填充率。
	 * 64 点在 60fps 下大约是 1 秒的轨迹。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "4", ClampMax = "512"))
	int32 TrailLength = 64;

	/** 条带宽度，单位厘米。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "1.0"))
	float TrailWidth = 2000.f;

	/** 头部颜色（最新位置）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail")
	FLinearColor HeadColor = FLinearColor(0.4f, 0.8f, 1.f, 1.f);

	/** 尾部颜色（最旧位置）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail")
	FLinearColor TailColor = FLinearColor(0.1f, 0.3f, 0.8f, 0.f);

	/**
	 * 采样间隔，单位秒。
	 * 大于 0 时按固定时间间隔采样，而不是每帧都采。
	 * 这样 TimeScale 变化时拖尾长度不会跟着剧变。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "0.0"))
	float SampleInterval = 0.05f;

	/**
	 * 最小采样距离，单位 AU。
	 * 天体移动太小时不记录新点，避免轨道极慢时拖尾挤成一团。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail", meta = (ClampMin = "0.0"))
	double MinSampleDistanceAu = 0.0;

	//////////////////////////////////////////////////////////////////////
	// 运行时控制

	/** 清空历史轨迹。 */
	UFUNCTION(BlueprintCallable, Category = "Trail")
	void ResetTrail();

	/** 当前记录了多少个有效点。 */
	UFUNCTION(BlueprintPure, Category = "Trail")
	int32 GetSampleCount() const { return TrailPoints.Num(); }

private:
	/** 记录一个采样点。 */
	void AddSample(const FVector& WorldLocation);

	/** 重建条带网格。 */
	void RebuildRibbon(const FVector& CameraLocation);

	/** 已记录的历史位置（世界坐标，环形缓冲）。 */
	TArray<FVector> TrailPoints;

	/** 采样计时器。 */
	float SampleTimer = 0.f;
};
