// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图模式的信息 UI 控制系统：选中天体面板与悬停天体面板。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StarMapUISystem.generated.h"

class AActor;
class AStellarEraHUD;

/** 一颗天体的显示数据。纯数据，不含 UObject 引用。 */
struct FStarMapBodyInfo
{
	FString Name;
	double MassEarth = 0.0;
	double MassSolar = 0.0;
	FVector PositionAu = FVector::ZeroVector;
	FVector VelocityKmPerSecond = FVector::ZeroVector;
	double OrbitalPeriodYears = 0.0;
	double RadiusKm = 0.0;
	double SurfaceGravityMps2 = 0.0;
	int32 SimulatorIndex = INDEX_NONE;
	bool bSimulated = false;
	bool bExertsGravity = false;
};

/** 面板矩形（Canvas 像素）。 */
struct FStarMapPanelRect
{
	float X = 0.f;
	float Y = 0.f;
	float W = 0.f;
	float H = 0.f;
};

/**
 * 星图模式的信息 UI 控制系统。
 *
 * 不 Tick、不保存跨帧状态：每帧由 AStellarEraHUD::DrawHUD 调用 Draw，
 * 现拉选中/悬停天体、现算布局、现画。纯函数单独暴露，便于无头测试。
 */
UCLASS()
class UStarMapUISystem : public UObject
{
	GENERATED_BODY()

public:
	/** 画一帧星图 UI。只在星图模式下由 HUD 调用。 */
	void Draw(AStellarEraHUD& Hud);

	/** 从已注册天体采集显示数据。没有 UCosmosOrbitComponent 时返回 false。 */
	static bool BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo);

	/** 格式化成面板行；**第一行是名字**，其余是属性。选中与悬停共用。 */
	static TArray<FString> FormatBodyInfoLines(const FStarMapBodyInfo& Info);

	/** 天体表面重力，单位 m/s²。质量或半径无效时返回 0。 */
	static double ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm);

	/**
	 * 把面板放在锚点右侧；右侧放不下翻到左侧，最后整体夹进视口。
	 * 鼠标锚点传 AnchorRadiusPx = 0，天体锚点传屏幕轮廓半径。
	 */
	static FStarMapPanelRect PlacePanelBesideAnchor(
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& PanelSizePx,
		const FVector2D& ViewportSizePx,
		float GapPx,
		float MarginPx);

private:
	void DrawBodyPanel(
		AStellarEraHUD& Hud,
		const FStarMapBodyInfo& Info,
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& ViewportSizePx);
};
