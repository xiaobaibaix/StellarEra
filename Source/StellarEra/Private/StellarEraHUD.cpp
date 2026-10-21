// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StellarEraHUD.h"

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
	if (ASpaceshipPawn* Ship = GetViewedShip())
	{
		DrawStatusPanel(Ship->GetStatus());
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

	DrawNotice();
}

FVector2D AStellarEraHUD::GetViewportSize() const
{
	return Canvas ? FVector2D(Canvas->SizeX, Canvas->SizeY) : FVector2D::ZeroVector;
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

void AStellarEraHUD::DrawStatusPanel(const FSpaceshipStatus& Status)
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
	const FString SpeedLine = Status.MaxSpeedKmPerSecond > 0.f
		? FString::Printf(TEXT("速度      %.2f / %.2f km/s（世界速度）"),
			Status.SpeedKmPerSecond, Status.MaxSpeedKmPerSecond)
		: FString::Printf(TEXT("速度      %.2f km/s（世界速度，巡航无上限）"),
			Status.SpeedKmPerSecond);

	// 大气：密度为 0 就是真空（太空里、或在大气壳之外）。有密度时把阻力也印出来，
	// 调阻力系数时不用另开日志。
	const FString AtmosphereLine = Status.AtmosphereDensityRatio > 0.f
		? FString::Printf(TEXT("大气      密度 %.1f%%  阻力 %.2f km/s²"),
			Status.AtmosphereDensityRatio * 100.f, Status.AtmosphereDragKmPerS2)
		: FString(TEXT("大气      真空"));

	const TArray<FString> Lines =
	{
		SpeedLine,
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
