// Copyright Voxel Plugin SAS. All Rights Reserved.
// 屏幕 HUD：常驻的飞船状态条 + 按 Tab 等操作弹出的短暂提示。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StellarEraNoticeSink.h"
#include "StellarEraHUD.generated.h"

class ASpaceshipPawn;
class UPlanetEditComponent;
class UStarMapUISystem;
struct FSpaceshipStatus;

/**
 * 纯 C++ 的调试 HUD：Canvas 直接画，不建 UMG 资产。
 * 飞船是私有依赖，只前置声明、include 放 .cpp，否则会逼着提成公开依赖。
 * 字体用引擎默认字体，中文能否显示取决于它有没有中文字形。
 */
UCLASS()
class STELLARERAUI_API AStellarEraHUD : public AHUD, public IStellarEraNoticeSink
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/**
	 * 弹一条提示，NoticeDuration 秒后自动消失（末尾 NoticeFadeDuration 秒淡出）。
	 * 重复调用替换上一条，不排队。飞船（下游模块）拿不到本类类型，经接口调进来。
	 */
	virtual void ShowNotice(const FString& Text) override;

	/** 当前画布尺寸（像素）；没有 Canvas 时返回零向量。 */
	FVector2D GetViewportSize() const;

	/**
	 * 帧率面板的一行文案，例如 `帧率      59.9 FPS（16.7 ms）`。
	 * 帧率无效（<= 0 或非有限值）时返回占位符。
	 */
	static FString FormatFrameRateLine(float SmoothedFps);

	/**
	 * 指数平滑帧率（一阶低通）：SmoothingTau 秒内收敛到瞬时值。
	 * 无样本（<= 0）时直接采用 1/DeltaSeconds；DeltaSeconds <= 0 时保持原值。
	 */
	static float UpdateSmoothedFps(float CurrentSmoothedFps, float DeltaSeconds, float SmoothingTau);

	/**
	 * 重力读数的一行文案，例如 `重力      43.78 km/s²（43776 m/s²）`。
	 * GravityKmPerS2 < 0 或非有限值时返回占位符；大于 MaxThrustKmPerS2 时追加「超出推力」。
	 */
	static FString FormatGravityLine(float GravityKmPerS2, float MaxThrustKmPerS2);

	/**
	 * 两条速度文案：绝对速度与相对参考行星的速度。MaxKmPerSecond <= 0 表示没有上限；
	 * 无参考行星时相对速度印占位符；非有限值不能印成 nan/inf。
	 */
	static void FormatSpeedLines(float AbsoluteKmPerSecond, float RelativeKmPerSecond,
		float MaxKmPerSecond, bool bHasReferencePlanet,
		FString& OutAbsoluteLine, FString& OutRelativeLine);

protected:
	/** 右上角是否显示帧率面板。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	bool bShowFrameRate = true;

	/** 帧率平滑时间常数（秒）：越小越跟手，越大越稳。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float FrameRateSmoothingTau = 0.5f;

	/** 提示停留时长（秒），之后开始淡出。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float NoticeDuration = 3.f;

	/** 淡出时长（秒），取 NoticeDuration 的末尾这一段。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float NoticeFadeDuration = 0.6f;

	/** 状态条距屏幕左上角的边距（像素）。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float PanelMargin = 20.f;

	/** 面板内边距（像素），同时用作提示框的内边距。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float PanelPadding = 10.f;

	/** 行间距（像素）。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float LineSpacing = 4.f;

	/** 文字缩放。 */
	UPROPERTY(EditAnywhere, Category = "HUD")
	float TextScale = 1.1f;

private:
	/** 当前 Possess 的 Pawn 是飞船就返回它，否则返回空（此时不画状态条）。 */
	ASpaceshipPawn* GetViewedShip() const;

	/** 当前视角下开着的编辑组件（角色或星图相机上的），没开着就 nullptr。 */
	const UPlanetEditComponent* GetViewedEditComponent() const;

	/** 画底部居中的两个笔刷按钮与提示。角色与星图的编辑模式下都会调用。 */
	void DrawPlanetEditPanel(const UPlanetEditComponent& Edit);

	/** 飞船当前位置的合引力（世界 km/s²），读求解器本帧刚算出的值；拿不到返回 -1。 */
	float GetShipGravityKmPerS2(const ASpaceshipPawn* Ship) const;

	/** 画左上角的状态条。GravityKmPerS2 < 0 表示当地重力未知。 */
	void DrawStatusPanel(const FSpaceshipStatus& Status, float GravityKmPerS2);

	/** 画屏幕中间的提示框；没有提示或已过期就什么都不做。 */
	void DrawNotice();

	/** 画右上角帧率面板并推进平滑值。每帧都画，与当前 Pawn 无关。 */
	void DrawFrameRatePanel();

	/** 当前提示的文案。空表示没有提示。 */
	FString NoticeText;

	/** 提示出现的时刻（世界时间，秒）。 */
	float NoticeStartTime = 0.f;

	/** 平滑后的帧率（FPS）；<= 0 表示还没有样本。 */
	float SmoothedFps = 0.f;

	/** 上一次往日志里打重力读数的世界时间（秒）；< 0 表示还没打过。 */
	float LastGravityLogTime = -1.f;

	/** 星图模式的信息面板。第一次进星图时惰性创建。 */
	UPROPERTY(Transient)
	TObjectPtr<UStarMapUISystem> StarMapUI;
};
