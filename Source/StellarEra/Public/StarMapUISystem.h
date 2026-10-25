// Copyright Voxel Plugin SAS. All Rights Reserved.
// 星图模式的信息 UI 控制系统：选中天体面板与悬停天体面板。

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StarMapUISystem.generated.h"

class AActor;
class AStellarEraHUD;
class IStarMapUIInputs;

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
 * 矿物观察图例的固定布局。
 *
 * 宽度与字体无关：输入回调（鼠标命中）里拿不到 AHUD::Canvas，量不了字，
 * 所以宽度写死、高度只随行数变。绘制与命中各自调一次，几何完全一致。
 */
namespace MineralLegendLayout
{
	constexpr float Padding = 10.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float SeparatorThickness = 1.f;
	/** 每行占的高度。行与行之间没有缝，命中判定直接用这个矩形。 */
	constexpr float RowSlotHeight = 24.f;
	/** 标题 / 提示行高：固定值，不量字体。 */
	constexpr float TitleLineHeight = 22.f;
	constexpr float HintLineHeight = 22.f;
	/** 面板宽度：按最长的一行提示（约 34 个汉字宽）留出的固定值。 */
	constexpr float PanelWidthPx = 460.f;
	constexpr float MarginPx = 8.f;
	/** 命中判定的上下缩进：点在两行接缝上时不误命中相邻行。 */
	constexpr float HitInsetY = 2.f;

	/**
	 * 标题文字下沿 → 第一行顶部：上下各留一次间距，中间夹一条分隔线。
	 * 面板高度预算与 ComputeMineralLegendLayout 的行起点共用这一处算式。
	 */
	FORCEINLINE float TitleToRowsTopOffset()
	{
		return SeparatorSpacing + SeparatorThickness + SeparatorSpacing;
	}

	/**
	 * 最后一行下沿 → 提示文字顶部：与标题段同构（间距 / 分隔线 / 间距）。
	 * 面板高度预算与绘制时的提示区块共用这一处算式。
	 */
	FORCEINLINE float RowsBottomToHintTopOffset()
	{
		return SeparatorSpacing + SeparatorThickness + SeparatorSpacing;
	}

	/**
	 * 面板高度（夹进视口之前）。EntryCount = 0 时仍留一行占位。
	 *
	 * 两段分隔块都要计入：只算 SeparatorSpacing + SeparatorThickness 会短 12 px，
	 * 缺口全挤在「最后一行 ↔ 提示行」之间，提示分隔线会画进最后一行。
	 */
	FORCEINLINE float PanelHeightPx(int32 EntryCount)
	{
		const int32 Slots = EntryCount > 0 ? EntryCount : 1;
		return Padding * 2.f + TitleLineHeight + RowSlotHeight * Slots + HintLineHeight
			+ TitleToRowsTopOffset() + RowsBottomToHintTopOffset();
	}

	/** 提示文字顶部的 Y：贴面板底，只留 Padding。 */
	FORCEINLINE float HintTextTopY(const FStarMapPanelRect& Panel)
	{
		return Panel.Y + Panel.H - Padding - HintLineHeight;
	}

	/** 提示分隔线的 Y：位于提示文字上方的一个间距块之后（与标题段同构）。 */
	FORCEINLINE float HintSeparatorY(const FStarMapPanelRect& Panel)
	{
		return HintTextTopY(Panel) - SeparatorSpacing - SeparatorThickness;
	}
}

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
	void Draw(AStellarEraHUD& Hud, IStarMapUIInputs& Inputs);

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

	/** 算面板与每一行的矩形。EntryCount = 0 时 OutRowRects 里仍有一行（占位行）。 */
	static void ComputeMineralLegendLayout(
		const FVector2D& ViewportSizePx,
		int32 EntryCount,
		FStarMapPanelRect& OutPanel,
		TArray<FStarMapPanelRect>& OutRowRects);

	/** 屏幕坐标命中哪一行；没命中返回 INDEX_NONE。 */
	static int32 HitTestMineralLegendRow(
		const TArray<FStarMapPanelRect>& RowRects,
		const FVector2D& ScreenPosPx);

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

	void DrawMineralLegend(
		AStellarEraHUD& Hud,
		const FString& PlanetName,
		const TArray<FMineralLegendEntry>& Entries,
		const FVector2D& ViewportSizePx);
};
