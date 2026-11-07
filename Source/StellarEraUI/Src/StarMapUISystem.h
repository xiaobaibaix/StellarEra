// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图模式的信息 UI 控制系统：选中天体面板与悬停天体面板。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StarMapPanelLayout.h"
#include "StarMapUISystem.generated.h"

class AActor;
class APlayerController;
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

/** 矿物观察图例的一行。纯数据，不含 UObject 引用。 */
struct FMineralLegendEntry
{
	FString DisplayName;

	/** 色块颜色 —— 与球内该元素矿体的显示色一致。 */
	FLinearColor Color = FLinearColor::White;

	/** 该元素在行星总含量里的份额（0..1，各行相加约为 1）。 */
	float Abundance = 0.f;

	/** 当前是否显示。关掉的行整行变暗。 */
	bool bVisible = true;
};

/**
 * 星图模式的信息 UI 控制系统。
 *
 * 不 Tick、不保存跨帧状态：每帧由 AStellarEraHUD::DrawHUD 调用 Draw，
 * 现拉选中/悬停天体、现算布局、现画。纯函数单独暴露，便于无头测试。
 */
UCLASS()
class STELLARERAUI_API UStarMapUISystem : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 画一帧星图 UI。只在星图模式下由 HUD 调用。
	 *
	 * 直接吃玩家控制器：鼠标坐标、屏幕拾取、矿物透视都是对引擎 API 与
	 * `StarMapPicking` 的一行转发，没有需要控制器独有的信息 —— 多一层接口只会
	 * 让同一件事有两个声明。
	 */
	void Draw(AStellarEraHUD& Hud, APlayerController& PlayerController);

	/** 从已注册天体采集显示数据。没有 UCosmosOrbitComponent 时返回 false。 */
	static bool BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo);

	/** 格式化成面板行；**第一行是名字**，其余是属性。选中与悬停共用。 */
	static TArray<FString> FormatBodyInfoLines(const FStarMapBodyInfo& Info);

	/**
	 * 矿物观察图例的文本行：标题「矿物观察 — 行星名」，其后每元素一行
	 * 「名称  百分比  开/关」，最后一行是操作提示。
	 * 纯函数，便于无头测试。
	 */
	static TArray<FString> FormatMineralLegendLines(
		const FString& PlanetName,
		TArrayView<const FMineralLegendEntry> Entries);

	/** 天体表面重力，单位 m/s²。质量或半径无效时返回 0。 */
	static double ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm);

private:
	void DrawBodyPanel(
		AStellarEraHUD& Hud,
		const FStarMapBodyInfo& Info,
		const FVector2D& AnchorPx,
		float AnchorRadiusPx,
		const FVector2D& ViewportSizePx);

	void DrawMineralLegend(
		AStellarEraHUD& Hud,
		const FString& PlanetName,
		const TArray<FMineralLegendEntry>& Entries,
		const FVector2D& ViewportSizePx);
};
