// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraHUD.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "SpaceshipPawn.h"
#include "StarMapCameraPawn.h"
#include "StarMapUISystem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** 状态条背景：半透明黑，别挡住场景。 */
	const FLinearColor PanelBackground(0.f, 0.f, 0.f, 0.55f);

	/** 提示框背景：更实一点，它出现在画面中间，得压得住场景。 */
	const FLinearColor NoticeBackground(0.f, 0.f, 0.f, 0.75f);

	const FLinearColor TitleColor(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor ValueColor(1.f, 1.f, 1.f, 1.f);
	const FLinearColor BorderColor(1.f, 0.85f, 0.30f, 1.f);

	/** 边框线宽（像素）。 */
	constexpr float BorderThickness = 1.f;

	/** 画一圈 1 像素边框：上、下、左、右四条细矩形。 */
	void DrawBorder(AHUD* Hud, float X, float Y, float W, float H, const FLinearColor& Color)
	{
		Hud->DrawRect(Color, X, Y, W, BorderThickness);
		Hud->DrawRect(Color, X, Y + H - BorderThickness, W, BorderThickness);
		Hud->DrawRect(Color, X, Y, BorderThickness, H);
		Hud->DrawRect(Color, X + W - BorderThickness, Y, BorderThickness, H);
	}

	/** 按 Alpha 缩放整个颜色（含透明度），用于淡出。 */
	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Color.A * Alpha);
	}
}

void AStellarEraHUD::DrawHUD()
{
	Super::DrawHUD();

	// Canvas 由 AHUD 在每帧绘制前建好；没有它说明这一帧不该画。
	if (!Canvas)
	{
		return;
	}

	// 不在飞船里就不画状态条（走路时这些数据没有意义）。提示框独立于飞船，
	// 照常画 —— 否则下船瞬间正在显示的提示会凭空消失。
	if (const ASpaceshipPawn* Ship = GetViewedShip())
	{
		const FSpaceshipStatus Status = Ship->GetStatus();
		const float GravityKmPerS2 = GetShipGravityKmPerS2(Ship);

		// 日志每秒一条：面板只显示一帧的瞬时值，起飞失败往往要回看几秒的
		// 重力/推力对比，日志比截图省事。用世界时间判间隔，掉帧也不会刷屏。
		const UWorld* World = GetWorld();
		const float Now = World ? World->GetTimeSeconds() : 0.f;
		if (LastGravityLogTime < 0.f || Now - LastGravityLogTime >= 1.f)
		{
			LastGravityLogTime = Now;
			UE_LOG(LogTemp, Display,
				TEXT("[Ship] 重力 %.3f km/s²（%.0f m/s²），推力上限 %.2f km/s²，速度 %.2f km/s（相对 %.2f），大气阻力 %.3f km/s²"),
				GravityKmPerS2, GravityKmPerS2 * 1000.f,
				Status.MaxThrustAccelerationKmPerS2, Status.SpeedKmPerSecond,
				Status.RelativeSpeedKmPerSecond, Status.AtmosphereDragKmPerS2);
		}

		DrawStatusPanel(Status, GravityKmPerS2);
	}

	// 星图模式的信息面板：只在当前 Pawn 是星图相机时画。
	if (const APlayerController* PlayerController = GetOwningPlayerController())
	{
		if (Cast<AStarMapCameraPawn>(PlayerController->GetPawn()))
		{
			if (!StarMapUI)
			{
				StarMapUI = NewObject<UStarMapUISystem>(this);
			}
			StarMapUI->Draw(*this);
		}
	}

	// 帧率面板：与当前 Pawn 无关，走路、开飞船、星图都画。
	DrawFrameRatePanel();

	DrawNotice();
}

FVector2D AStellarEraHUD::GetViewportSize() const
{
	return Canvas ? FVector2D(Canvas->SizeX, Canvas->SizeY) : FVector2D::ZeroVector;
}

FString AStellarEraHUD::FormatFrameRateLine(const float SmoothedFps)
{
	// 还没有样本，或数值异常（NaN/Inf）：给占位符。直接 Printf 会往画面上
	// 印 "nan"/"inf"，首帧之前也不该假装已经有一个帧率。
	if (!FMath::IsFinite(SmoothedFps) || SmoothedFps <= 0.f)
	{
		return TEXT("帧率      --");
	}

	// 帧时间比 FPS 更能反映卡顿：30 与 29 fps 看着差不多，帧时间差 1 ms 反而明显。
	return FString::Printf(TEXT("帧率      %.1f FPS（%.1f ms）"),
		SmoothedFps, 1000.f / SmoothedFps);
}

float AStellarEraHUD::UpdateSmoothedFps(
	const float CurrentSmoothedFps, const float DeltaSeconds, const float SmoothingTau)
{
	// 暂停或时间回绕时 dt <= 0，指数项没有意义，保持原值。
	if (DeltaSeconds <= 0.f)
	{
		return CurrentSmoothedFps;
	}

	const float InstantFps = 1.f / DeltaSeconds;

	// 第一个样本没有历史可平滑，直接用瞬时值 —— 否则开局会从 0 慢慢爬上来。
	if (CurrentSmoothedFps <= 0.f)
	{
		return InstantFps;
	}

	// 一阶低通：Alpha 由 dt 和 Tau 算出，而不是固定系数，这样掉帧时
	// （dt 变大）收敛速度仍然按真实时间算。
	const float Alpha = 1.f - FMath::Exp(-DeltaSeconds / FMath::Max(SmoothingTau, 1.e-3f));
	return FMath::Lerp(CurrentSmoothedFps, InstantFps, Alpha);
}

void AStellarEraHUD::ShowNotice(const FString& Text)
{
	NoticeText = Text;

	const UWorld* World = GetWorld();
	NoticeStartTime = World ? World->GetTimeSeconds() : 0.f;
}

ASpaceshipPawn* AStellarEraHUD::GetViewedShip() const
{
	const APlayerController* PlayerController = GetOwningPlayerController();
	return PlayerController ? Cast<ASpaceshipPawn>(PlayerController->GetPawn()) : nullptr;
}

float AStellarEraHUD::GetShipGravityKmPerS2(const ASpaceshipPawn* Ship) const
{
	if (!Ship)
	{
		return -1.f;
	}

	// 直接读求解器本帧算出的引力合力：HUD 只做单位换算，不重算引力，
	// 否则显示的数和真正施加到飞船上的数可能对不上。
	UWorld* World = GetWorld();
	UCosmosSimulationSubsystem* Simulation =
		World ? World->GetSubsystem<UCosmosSimulationSubsystem>() : nullptr;
	const UCosmosOrbitComponent* Orbit = Ship->FindComponentByClass<UCosmosOrbitComponent>();
	if (!Simulation || !Orbit || Orbit->SimulatorIndex == INDEX_NONE)
	{
		return -1.f;
	}

	const FVector3d GravityWorld = CosmosUnits::GameAccelerationToWorldCmPerS2(
		Simulation->GetBodyGravityGameUnits(Orbit->SimulatorIndex),
		Simulation->CentimetersPerAu, Simulation->TimeScale);
	return static_cast<float>(GravityWorld.Size() / 1.0e5);
}

FString AStellarEraHUD::FormatGravityLine(
	const float GravityKmPerS2, const float MaxThrustKmPerS2)
{
	if (!FMath::IsFinite(GravityKmPerS2) || GravityKmPerS2 < 0.f)
	{
		return TEXT("重力      --");
	}

	FString Line = FString::Printf(TEXT("重力      %.2f km/s²（%.0f m/s²）"),
		GravityKmPerS2, GravityKmPerS2 * 1000.f);

	// 当地重力压过标称推力时给出判定：这一栏就是「为什么抬不起来」的答案。
	if (FMath::IsFinite(MaxThrustKmPerS2) && MaxThrustKmPerS2 > 0.f
		&& GravityKmPerS2 > MaxThrustKmPerS2)
	{
		Line += TEXT("，超出推力");
	}
	return Line;
}

void AStellarEraHUD::FormatSpeedLines(
	const float AbsoluteKmPerSecond, const float RelativeKmPerSecond,
	const float MaxKmPerSecond, const bool bHasReferencePlanet,
	FString& OutAbsoluteLine, FString& OutRelativeLine)
{
	// 绝对速度：非有限值 / 负值不能印 nan/inf。
	if (!FMath::IsFinite(AbsoluteKmPerSecond) || AbsoluteKmPerSecond < 0.f)
	{
		OutAbsoluteLine = TEXT("绝对速度  --");
	}
	else if (FMath::IsFinite(MaxKmPerSecond) && MaxKmPerSecond > 0.f)
	{
		OutAbsoluteLine = FString::Printf(TEXT("绝对速度  %.2f / %.2f km/s"),
			AbsoluteKmPerSecond, MaxKmPerSecond);
	}
	else
	{
		OutAbsoluteLine = FString::Printf(TEXT("绝对速度  %.2f km/s（无上限）"),
			AbsoluteKmPerSecond);
	}

	// 相对速度必须有参考行星才有意义；太空里（PlanetActor 为空）印占位符，
	// 否则读者会以为「相对速度」也是某个真实参考系。
	if (!bHasReferencePlanet)
	{
		OutRelativeLine = TEXT("相对速度  --（无参考行星）");
	}
	else if (!FMath::IsFinite(RelativeKmPerSecond) || RelativeKmPerSecond < 0.f)
	{
		OutRelativeLine = TEXT("相对速度  --");
	}
	else
	{
		OutRelativeLine = FString::Printf(TEXT("相对速度  %.2f km/s"), RelativeKmPerSecond);
	}
}

void AStellarEraHUD::DrawStatusPanel(const FSpaceshipStatus& Status, const float GravityKmPerS2)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	// 推力标签：`bThrustLocked`（旧「保底速度锁定」）随移动组件一起没了 ——
	// 阻尼模型下按 W 自然收敛到极速，不需要「保底 + 锁速」这个机制。
	FString ThrustLabel;
	if (Status.ThrustInput > 0.f)
	{
		ThrustLabel = TEXT("W 加速");
	}
	else if (Status.ThrustInput < 0.f)
	{
		ThrustLabel = TEXT("S 减速");
	}
	else
	{
		ThrustLabel = TEXT("无");
	}

	// 极速为 0 表示**什么上限都没有**（巡航开着 = 阻尼关掉，且飞船的硬上限也
	// 设成了 0）—— 那是「没有上限」，不是「不能动」，所以分两种印法而不是直接印 0。
	// 巡航开着但设了硬上限时，飞船报上来的就是那个硬上限，照常印「x / y」。
	//
	// 速度印两条：绝对（惯性系，速度上限按这条生效）与相对参考行星（大气阻力 /
	// 「停在地表上」按这条）。贴着一颗公转中的行星静止时，两条的差别就是行星速度。
	FString AbsoluteSpeedLine;
	FString RelativeSpeedLine;
	FormatSpeedLines(Status.SpeedKmPerSecond, Status.RelativeSpeedKmPerSecond,
		Status.MaxSpeedKmPerSecond, Status.bHasReferencePlanet,
		AbsoluteSpeedLine, RelativeSpeedLine);

	// 大气：密度为 0 就是真空（太空里、或在大气壳之外）。有密度时把阻力也印出来，
	// 调阻力系数时不用另开日志。
	const FString AtmosphereLine = Status.AtmosphereDensityRatio > 0.f
		? FString::Printf(TEXT("大气      密度 %.1f%%  阻力 %.2f km/s²"),
			Status.AtmosphereDensityRatio * 100.f, Status.AtmosphereDragKmPerS2)
		: FString(TEXT("大气      真空"));

	const TArray<FString> Lines =
	{
		AbsoluteSpeedLine,
		RelativeSpeedLine,
		FormatGravityLine(GravityKmPerS2, Status.MaxThrustAccelerationKmPerS2),
		FString::Printf(TEXT("推力上限  %.2f km/s²"), Status.MaxThrustAccelerationKmPerS2),
		FString::Printf(TEXT("推力      %s"), *ThrustLabel),
		FString::Printf(TEXT("加速      %s"), Status.bBoosting ? TEXT("ON") : TEXT("OFF")),
		FString::Printf(TEXT("巡航      %s"), Status.bPreserveVelocity ? TEXT("开") : TEXT("关")),
		FString::Printf(TEXT("落地      %s"), Status.bLanded ? TEXT("是") : TEXT("否")),
		AtmosphereLine,
	};

	const FString Title(TEXT("飞船状态"));

	// 先量出最宽的一行，面板宽度才包得住所有文字 —— 写死宽度的话，
	// 改一个字的标签就会被裁掉。
	float ContentWidth = 0.f;
	float TitleHeight = 0.f;
	{
		float TitleWidth = 0.f;
		GetTextSize(Title, TitleWidth, TitleHeight, Font, TextScale);
		ContentWidth = TitleWidth;
	}

	float LineHeight = 0.f;
	for (const FString& Line : Lines)
	{
		float LineWidth = 0.f;
		float ThisHeight = 0.f;
		GetTextSize(Line, LineWidth, ThisHeight, Font, TextScale);
		ContentWidth = FMath::Max(ContentWidth, LineWidth);
		LineHeight = FMath::Max(LineHeight, ThisHeight);
	}

	const float PanelWidth = ContentWidth + PanelPadding * 2.f;
	const float PanelHeight =
		TitleHeight + LineSpacing + Lines.Num() * (LineHeight + LineSpacing) + PanelPadding * 2.f;
	const float PanelX = PanelMargin;
	const float PanelY = PanelMargin;

	DrawRect(PanelBackground, PanelX, PanelY, PanelWidth, PanelHeight);
	DrawBorder(this, PanelX, PanelY, PanelWidth, PanelHeight, BorderColor);

	float CursorY = PanelY + PanelPadding;

	DrawText(Title, TitleColor, PanelX + PanelPadding, CursorY, Font, TextScale);
	CursorY += TitleHeight + LineSpacing;

	for (const FString& Line : Lines)
	{
		DrawText(Line, ValueColor, PanelX + PanelPadding, CursorY, Font, TextScale);
		CursorY += LineHeight + LineSpacing;
	}
}

void AStellarEraHUD::DrawNotice()
{
	if (NoticeText.IsEmpty())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Elapsed = World->GetTimeSeconds() - NoticeStartTime;

	// Elapsed < 0 只在世界时间回绕时可能出现，一并当过期处理。
	if (Elapsed < 0.f || Elapsed > NoticeDuration)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
	if (!Font)
	{
		return;
	}

	// 末尾 NoticeFadeDuration 秒线性淡出，避免"啪"地消失。
	const float Alpha = NoticeFadeDuration > KINDA_SMALL_NUMBER
		? FMath::Clamp((NoticeDuration - Elapsed) / NoticeFadeDuration, 0.f, 1.f)
		: 1.f;

	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(NoticeText, TextWidth, TextHeight, Font, TextScale);

	const float BoxWidth = TextWidth + PanelPadding * 2.f;
	const float BoxHeight = TextHeight + PanelPadding * 2.f;

	// 水平居中、纵向偏上：偏上不会挡住机头方向的视野。
	const float BoxX = (Canvas->SizeX - BoxWidth) * 0.5f;
	const float BoxY = Canvas->SizeY * 0.32f;

	DrawRect(WithAlpha(NoticeBackground, Alpha), BoxX, BoxY, BoxWidth, BoxHeight);
	DrawBorder(this, BoxX, BoxY, BoxWidth, BoxHeight, WithAlpha(BorderColor, Alpha));
	DrawText(NoticeText, FLinearColor(1.f, 1.f, 1.f, Alpha),
		BoxX + PanelPadding, BoxY + PanelPadding, Font, TextScale);
}

void AStellarEraHUD::DrawFrameRatePanel()
{
	const UWorld* World = GetWorld();
	const float DeltaSeconds = World ? World->GetDeltaSeconds() : 0.f;

	// 平滑每帧都推进：面板被关掉再打开时不用从零重新爬。
	SmoothedFps = UpdateSmoothedFps(SmoothedFps, DeltaSeconds, FrameRateSmoothingTau);

	if (!bShowFrameRate)
	{
		return;
	}

	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font || !Canvas)
	{
		return;
	}

	const FString Line = FormatFrameRateLine(SmoothedFps);

	float TextWidth = 0.f;
	float TextHeight = 0.f;
	GetTextSize(Line, TextWidth, TextHeight, Font, TextScale);

	const float PanelWidth = TextWidth + PanelPadding * 2.f;
	const float PanelHeight = TextHeight + PanelPadding * 2.f;

	// 右上角：右边缘留 PanelMargin，宽度按文字实测，换字体/字号也不会裁字。
	const float PanelX = Canvas->SizeX - PanelMargin - PanelWidth;
	const float PanelY = PanelMargin;

	DrawRect(PanelBackground, PanelX, PanelY, PanelWidth, PanelHeight);
	DrawBorder(this, PanelX, PanelY, PanelWidth, PanelHeight, BorderColor);
	DrawText(Line, ValueColor, PanelX + PanelPadding, PanelY + PanelPadding, Font, TextScale);
}
