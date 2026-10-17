// 天体 Actor —— 挂在关卡里的可拖拽天体。
//
// 职责极简：暴露参数给编辑器、把参数注册进 N 体求解器、
// 每帧从求解器读回自己的位置并同步给渲染组件。
//
// 它不执行任何引力计算——那是求解器的事。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "CosmosTypes.h"
#include "CosmosBodyActor.generated.h"

class FNBodySimulator;
class AVoxelPlanetActor;
class UCosmosTrailComponent;

/**
 * 天体 Actor。
 *
 * 使用方式：
 *   1. 关卡里拖入若干个
 *   2. 每个设置 Mass、Radius、以及轨道参数
 *   3. PIE 运行
 *
 * 行星用球体网格，恒星额外带点光源。
 */
UCLASS(Blueprintable)
class VOXELCOSMOS_API ACosmosBodyActor : public AActor
{
	GENERATED_BODY()

public:
	ACosmosBodyActor();

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End AActor Interface

	//////////////////////////////////////////////////////////////////////
	// 天体属性

	/** 天体类型。影响默认外观与是否发光。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos")
	ECosmosBodyType BodyType = ECosmosBodyType::Planet;

	/**
	 * 质量，单位 M☉。
	 * 常用值：恒星 1.0，地球 3.003e-6，木星 9.546e-4，
	 *         火星 3.227e-7，水星 1.66e-7。
	 * 质量同时决定引力强度与轨道速度，别填错。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos", meta = (ClampMin = "0.0"))
	double MassSolar = 3.0034893e-6;

	/**
	 * 视觉半径，单位 AU（游戏单位）。
	 * 注意这是「显示用」的缩放，与物理无关。
	 * 地球半径 4.26e-5 AU，但真实比例下根本看不见，所以要放大。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Visual", meta = (ClampMin = "0.0"))
	double VisualRadiusAu = 0.02;

	/** 显示颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Visual")
	FLinearColor DisplayColor = FLinearColor(0.3f, 0.6f, 0.9f);

	//////////////////////////////////////////////////////////////////////
	// 轨道

	/**
	 * 轨道初始条件。
	 * 用「半长轴 + 偏心率」比直接填位置速度更安全——
	 * 后者一旦两者不匹配，天体就会飞向中心或逃逸出系统。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit")
	FCosmosOrbitInit Orbit;

	/**
	 * 引力中心天体的质量，单位 M☉。
	 * 决定轨道速度。填 1.0 表示绕一颗太阳质量的恒星公转。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Orbit", meta = (ClampMin = "0.0"))
	double CentralMassSolar = 1.0;

	/**
	 * 是否由求解器驱动运动。
	 * 关掉之后这个 Actor 就是个静态装饰物，适合放远处的背景恒星。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos")
	bool bSimulated = true;

	//////////////////////////////////////////////////////////////////////
	// 外观

	/** 使用的网格。为空时用引擎自带球体。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Visual")
	TObjectPtr<UStaticMesh> BodyMesh;

	//////////////////////////////////////////////////////////////////////
	// 恒星光照
	//
	// 为什么不用点光源：UE 的点光源是平方反比衰减，
	// 公式 I = Is/d² · saturate(1−(d/r)⁴)²。
	// 1 AU = 1.5e13 cm，而默认衰减半径只有 1000 cm——差 10 个数量级。
	// 即使强行放大半径，行星处强度也会是恒星处的 1e-24，需要天文数字级强度，
	// 而且靠近恒星的行星全被烧成纯白。
	//
	// 正确解法：平行光（DirectionalLight）。它是无限远光源，没有距离衰减，
	// 正好对应「恒星照亮整个星系」这个物理事实。
	// 多恒星系统下，每颗恒星给每个行星各提供一个平行光，强度可叠加。

	/** 恒星的辐射强度，单位 lux（地球接收值约 1.0）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting", meta = (ClampMin = "0.0"))
	float LuminosityLux = 1.0f;

	/** 恒星色温映射的颜色。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting")
	FLinearColor LightColor = FLinearColor(1.f, 0.95f, 0.85f);

	/**
	 * 光照模式。
	 *
	 * Directional（默认，推荐）：
	 *   每颗恒星为每个受照天体提供一个平行光，从恒星方向照来。
	 *   适合太阳系及多恒星系统。
	 *
	 * Point：
	 *   旧的点光源实现。仅适合半径小于几 AU 的紧凑系统。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting")
	ECosmosLightMode LightMode = ECosmosLightMode::Directional;

	/** 点光源强度。仅 LightMode = Point 时生效。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting", meta = (EditCondition = "LightMode == ECosmosLightMode::Point"))
	float PointLightIntensity = 20000.f;

	/** 点光源衰减半径，单位 AU。0 = 不衰减。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting", meta = (EditCondition = "LightMode == ECosmosLightMode::Point", ClampMin = "0.0"))
	double PointLightAttenuationRadiusAu = 0.0;

	/**
	 * 这个天体接受哪些恒星的照明。
	 * 留空则自动使用场景中所有恒星。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting")
	TArray<TWeakObjectPtr<ACosmosBodyActor>> IlluminatingStars;

	/**
	 * 恒星的照明半径，单位 AU。
	 * 超过这个距离就不提供平行光，省掉大量无意义的灯光。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Lighting", meta = (ClampMin = "0.0"))
	double StarLightRangeAu = 0.0;

	//////////////////////////////////////////////////////////////////////
	// 体素星球对接
	//
	// 核心约束：Voxel Plugin 用「世界绝对坐标」寻址体素，
	// 所以体素世界一旦建立就不能随 Actor 移动——
	// 每帧改生成器 Center 会让整棵 LOD 树失效重建，帧率归零。
	//
	// 解法是分而治之：
	//   · 远景用本 Actor 自己的球体网格，跟着 N 体系统跑
	//   · 近景才把体素星球挂进来，而它固定在本地坐标，不做轨道运动
	// 这样宏观轨道由 N 体驱动，微观地形保持静止，两者互不干扰。

	/** 关联的体素星球 Actor。为空则这个天体只用球体网格。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel")
	TObjectPtr<AActor> VoxelPlanetActor;

	/**
	 * 是否在 BeginPlay 时自动查找挂在本体下的体素星球。
	 * 开启后，在关卡里把 AVoxelPlanetActor 拖到本 Actor 下方
	 * 形成父子关系即可，不必手动填 VoxelPlanetActor 字段。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel")
	bool bAutoFindChildVoxelPlanet = true;

	/**
	 * 激活体素地形的距离阈值，单位 AU。
	 * 相机比这更远时不加载体素（用球体网格凑合）。
	 * 0 表示始终挂载体素。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel", meta = (ClampMin = "0.0"))
	double VoxelActivationDistanceAu = 0.3;

	/**
	 * 体素星球相对本 Actor 的局部偏移，单位 AU。
	 * 体素世界固定不动，偏移只在「本 Actor 停靠」时使用。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel")
	FVector VoxelLocalOffset = FVector::ZeroVector;

	/**
	 * 自转速度，单位「度/秒」。绕轨道法线旋转。
	 * 0 表示不自转（潮汐锁定就是设0）。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel")
	double SpinDegreesPerSecond = 0.5;

	/**
	 * 自转轴倾角，度。相对轨道法线倾斜。
	 * 23.4 度就是地球的黄赤交角。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cosmos|Voxel", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	double AxialTiltDegrees = 23.4;

	//////////////////////////////////////////////////////////////////////
	// 运行时

	/** 当前在求解器中的索引。由 InitializeBody 写入。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos")
	int32 SimulatorIndex = INDEX_NONE;

	/** 体素星球当前是否已激活（近景才加载）。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Cosmos|Voxel")
	bool bVoxelPlanetActive = false;

	//////////////////////////////////////////////////////////////////////
	// 拖尾

	/**
	 * 是否显示轨道拖尾。
	 *
	 * 拖尾的价值场景：让远景下不可见的运动变得可感知。
	 * 注意太阳系里行星速度 30 km/s，1 AU 外看每帧位移几乎为零——
	 * 拖尾不会让它动得更快，只是提供视觉辅助。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trail")
	bool bShowTrail = true;

	/** 拖尾组件。只读，实际参数在组件上。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Trail")
	TObjectPtr<UCosmosTrailComponent> TrailComponent;

	//////////////////////////////////////////////////////////////////////
	// 光照

	/**
	 * 重建本天体的照明。
	 * GameMode 在收集完所有天体后调用一次。
	 *
	 * 恒星不自带光源，而是给每个受照天体各挂一个平行光——
	 * 这样一颗恒星可以同时照亮多颗行星，而每颗行星的光方向都正确。
	 */
	void RebuildLighting(const TArray<ACosmosBodyActor*>& AllBodies);

	/** 每帧更新平行光的方向与强度（恒星移动时要跟着转）。 */
	void UpdateLighting();

	/** 当前被多少颗恒星照亮。诊断用。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos|Lighting")
	int32 GetIlluminatorCount() const { return DirectionalLights.Num(); }

	/** 当前状态。只读，真实数据在求解器里。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos")
	FVector GetCurrentPosition() const;

	/** 当前速度（世界坐标，厘米/秒）。 */
	UFUNCTION(BlueprintPure, Category = "Cosmos")
	FVector GetCurrentVelocity() const;

	/** 注册进求解器并应用轨道初始条件。由 GameMode 调用。 */
	void InitializeBody(FNBodySimulator& Simulator);

	/** 从求解器读回状态并同步到渲染。GameMode 每帧调用。 */
	void SyncFromSimulator(const FNBodySimulator& Simulator);

	/**
	 * 根据相机距离决定是否激活体素星球。
	 * GameMode 每帧调用。
	 */
	void UpdateVoxelPlanetLOD(const FVector& CameraWorldLocation);

	/**
	 * 按自转速度与轨道法线更新朝向。
	 * GameMode 每帧调用。
	 */
	void UpdateSpin(double DeltaGameSeconds, const FNBodySimulator& Simulator);

	/** 手动切换体素星球激活状态。会同步隐藏/显示球体网格。 */
	UFUNCTION(BlueprintCallable, Category = "Cosmos|Voxel")
	void SetVoxelPlanetActive(bool bActive);

protected:
	/**
	 * 查找挂在本体下的体素星球。
	 * 遍历所有层级，不只是直接子级。找不到返回 nullptr。
	 */
	AVoxelPlanetActor* FindAttachedVoxelPlanet() const;

	/** 刷新网格大小与颜色。参数变化时调用。 */
	void UpdateVisuals();

	/** 刷新光源设置。 */
	void UpdateLight();

	/** 计算本天体的轨道法线（自转轴的参考方向）。 */
	FVector GetOrbitNormal() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Cosmos")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/**
	 * 点光源。仅 LightMode = Point 时启用。
	 * 平行光模式下这个会被隐藏——平行光挂在各行星身上。
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cosmos")
	TObjectPtr<UPointLightComponent> PointLightComponent;

	/**
	 * 照向本天体的平行光数组。
	 * 多恒星系统下会有多个：每颗恒星提供一个。
	 */
	UPROPERTY(VisibleAnywhere, Category = "Cosmos|Lighting")
	TArray<TObjectPtr<UDirectionalLightComponent>> DirectionalLights;

	/**
	 * 与 DirectionalLights 一一对应的恒星引用。
	 * 用弱引用，恒星被销毁时自动失效，不会悬空。
	 */
	TArray<TWeakObjectPtr<ACosmosBodyActor>> Illuminators;

	/** 已累计的自转角度，度。 */
	double AccumulatedSpinDegrees = 0.0;

	/** 体素星球激活时冻结的自转角度。用于避免切换瞬间朝向跳变。 */
	double SpinAngleAtFreeze = 0.0;
};
