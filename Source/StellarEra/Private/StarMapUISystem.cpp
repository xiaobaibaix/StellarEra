// Copyright Voxel Plugin SAS. All Rights Reserved.

#include "StarMapUISystem.h"

#include "CosmosOrbitComponent.h"
#include "CosmosSimulationSubsystem.h"
#include "CosmosTypes.h"
#include "StellarEraHUD.h"
#include "StellarEraPlayerController.h"
#include "PlanetCutawayComponent.h"
#include "PlanetGeologyField.h"
#include "VoxelPlanetActor.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

bool UStarMapUISystem::BuildBodyInfo(AActor* Body, FStarMapBodyInfo& OutInfo)
{
	OutInfo = FStarMapBodyInfo();
	if (!IsValid(Body))
	{
		return false;
	}

	const UCosmosOrbitComponent* Orbit =
		Body->FindComponentByClass<UCosmosOrbitComponent>();
	if (!Orbit)
	{
		return false;
	}

	OutInfo.Name = Body->GetActorNameOrLabel();
	OutInfo.MassEarth = Orbit->MassEarth;
	OutInfo.MassSolar = CosmosUnits::EarthMassToSolarMass(Orbit->MassEarth);
	OutInfo.SimulatorIndex = Orbit->SimulatorIndex;
	OutInfo.bSimulated = Orbit->bSimulated;
	OutInfo.bExertsGravity = Orbit->bExertsGravity;

	// 位置/速度按需换算：组件不再每帧回显。优先用运行期子系统的标定与求解器
	// 速度；编辑器世界没有子系统，标定退回同一份 CDO 配置，速度退回初速。
	const UCosmosSimulationSubsystem* CosmosSubsystem = nullptr;
	double CmPerAu = 0.0;
	if (const UWorld* World = Body->GetWorld())
	{
		CosmosSubsystem = World->GetSubsystem<UCosmosSimulationSubsystem>();
		if (CosmosSubsystem)
		{
			CmPerAu = CosmosSubsystem->CentimetersPerAu;
		}
	}
	if (CmPerAu <= 0.0)
	{
		CmPerAu = GetDefault<UCosmosSimulationSubsystem>()->CentimetersPerAu;
	}

	OutInfo.PositionAu = CmPerAu > 0.0
		? Body->GetActorLocation() / CmPerAu
		: FVector::ZeroVector;

	OutInfo.VelocityKmPerSecond = Orbit->InitialVelocityKmPerSecond;
	if (CosmosSubsystem && OutInfo.SimulatorIndex != INDEX_NONE)
	{
		OutInfo.VelocityKmPerSecond = CosmosUnits::AuPerYearToKilometerPerSecond(
			CosmosSubsystem->GetBodyVelocityGameUnits(OutInfo.SimulatorIndex));
	}

	double RadiusCm = 0.0;
	if (const AVoxelPlanetActor* Planet = Cast<AVoxelPlanetActor>(Body))
	{
		RadiusCm = Planet->GetSurfaceRadiusCm();
	}
	else
	{
		RadiusCm = double(Body->GetSimpleCollisionRadius());
	}

	OutInfo.RadiusKm = RadiusCm / CosmosUnits::CentimetersPerKilometer;
	OutInfo.SurfaceGravityMps2 =
		ComputeSurfaceGravityMps2(OutInfo.MassEarth, OutInfo.RadiusKm);

	if (OutInfo.SimulatorIndex != INDEX_NONE)
	{
		if (const UWorld* World = Body->GetWorld())
		{
			if (const UCosmosSimulationSubsystem* Subsystem =
				World->GetSubsystem<UCosmosSimulationSubsystem>())
			{
				OutInfo.OrbitalPeriodYears =
					Subsystem->GetOrbitalPeriodYears(OutInfo.SimulatorIndex);
			}
		}
	}

	return true;
}

double UStarMapUISystem::ComputeSurfaceGravityMps2(double MassEarth, double RadiusKm)
{
	if (!FMath::IsFinite(MassEarth) || !FMath::IsFinite(RadiusKm) ||
		MassEarth <= 0.0 || RadiusKm <= 0.0)
	{
		return 0.0;
	}

	const double MassKg =
		MassEarth * CosmosUnits::EarthMass * CosmosUnits::SolarMassToKilogram;
	const double RadiusCm = RadiusKm * CosmosUnits::CentimetersPerKilometer;

	// G_SI 的单位是 cm³/(kg·s²)；除以 100 换成 m/s²。
	return CosmosUnits::GravitationalConstant_SI * MassKg /
		(RadiusCm * RadiusCm) / 100.0;
}

TArray<FString> UStarMapUISystem::FormatBodyInfoLines(const FStarMapBodyInfo& Info)
{
	TArray<FString> Lines;
	Lines.Reserve(9);

	// 第一行是标题（名字），DrawBodyPanel 把它画成金色标题。
	Lines.Add(Info.Name);

	if (FMath::IsFinite(Info.MassEarth) && Info.MassEarth > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("质量        %.4g M⊕（%.4g M☉）"),
			Info.MassEarth, Info.MassSolar));
	}
	else
	{
		Lines.Add(TEXT("质量        —"));
	}

	Lines.Add(FString::Printf(TEXT("位置        (%.4f, %.4f, %.4f) AU"),
		Info.PositionAu.X, Info.PositionAu.Y, Info.PositionAu.Z));

	Lines.Add(FString::Printf(TEXT("速度        %.3f km/s"),
		Info.VelocityKmPerSecond.Size()));

	if (FMath::IsFinite(Info.OrbitalPeriodYears) && Info.OrbitalPeriodYears > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("轨道周期    %.3f 年"),
			Info.OrbitalPeriodYears));
	}
	else
	{
		Lines.Add(TEXT("轨道周期    未知"));
	}

	if (FMath::IsFinite(Info.RadiusKm) && Info.RadiusKm > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("半径        %.1f km"), Info.RadiusKm));
	}
	else
	{
		Lines.Add(TEXT("半径        —"));
	}

	if (FMath::IsFinite(Info.SurfaceGravityMps2) && Info.SurfaceGravityMps2 > 0.0)
	{
		Lines.Add(FString::Printf(TEXT("表面引力    %.3f m/s²（%.3f g）"),
			Info.SurfaceGravityMps2, Info.SurfaceGravityMps2 / 9.80665));
	}
	else
	{
		Lines.Add(TEXT("表面引力    —"));
	}

	Lines.Add(Info.SimulatorIndex == INDEX_NONE
		? TEXT("Simulator   #-1（未注册）")
		: FString::Printf(TEXT("Simulator   #%d"), Info.SimulatorIndex));

	Lines.Add(FString::Printf(TEXT("模拟        %s    引力源    %s"),
		Info.bSimulated ? TEXT("是") : TEXT("否"),
		Info.bExertsGravity ? TEXT("是") : TEXT("否")));

	return Lines;
}

TArray<FString> UStarMapUISystem::FormatCutawayLegendLines(
	const FString& PlanetName,
	TArrayView<const FCutawayLegendEntry> Entries)
{
	TArray<FString> Lines;
	Lines.Reserve(Entries.Num() + 2);

	Lines.Add(FString::Printf(TEXT("矿物剖面 — %s"), *PlanetName));

	if (Entries.Num() == 0)
	{
		Lines.Add(TEXT("（无矿物元素）"));
	}
	else
	{
		for (const FCutawayLegendEntry& Entry : Entries)
		{
			const FString Name = Entry.DisplayName.IsEmpty()
				? FString(TEXT("?"))
				: Entry.DisplayName;
			const int32 Percent = FMath::RoundToInt(
				FMath::Clamp(Entry.Abundance, 0.f, 1.f) * 100.f);
			Lines.Add(FString::Printf(TEXT("%s  %d%%"), *Name, Percent));
		}
	}

	Lines.Add(TEXT("H 退出   左键拖圆环转切面   右键拖转视角   滚轮缩放"));
	return Lines;
}

TArray<FString> UStarMapUISystem::FormatMineralLegendLines(
	const FString& PlanetName,
	TArrayView<const FMineralLegendEntry> Entries)
{
	TArray<FString> Lines;
	Lines.Reserve(Entries.Num() + 2);

	Lines.Add(FString::Printf(TEXT("矿物观察 — %s"), *PlanetName));

	if (Entries.Num() == 0)
	{
		Lines.Add(TEXT("（无矿物元素）"));
	}
	else
	{
		for (const FMineralLegendEntry& Entry : Entries)
		{
			const FString Name = Entry.DisplayName.IsEmpty()
				? FString(TEXT("?"))
				: Entry.DisplayName;
			const int32 Percent = FMath::RoundToInt(
				FMath::Clamp(Entry.Abundance, 0.f, 1.f) * 100.f);
			Lines.Add(FString::Printf(TEXT("%s  %d%%  %s"), *Name, Percent,
				Entry.bVisible ? TEXT("开") : TEXT("关")));
		}
	}

	Lines.Add(TEXT("H 退出   点击图例/数字键筛选   右键拖转视角   滚轮缩放"));
	return Lines;
}

void UStarMapUISystem::ComputeMineralLegendLayout(
	const FVector2D& ViewportSizePx,
	int32 EntryCount,
	FStarMapPanelRect& OutPanel,
	TArray<FStarMapPanelRect>& OutRowRects)
{
	OutPanel = PlacePanelBesideAnchor(
		FVector2D(MineralLegendLayout::MarginPx, MineralLegendLayout::MarginPx),
		0.f,
		FVector2D(MineralLegendLayout::PanelWidthPx,
			MineralLegendLayout::PanelHeightPx(EntryCount)),
		ViewportSizePx, 0.f, MineralLegendLayout::MarginPx);

	const int32 SlotCount = FMath::Max(EntryCount, 1);
	OutRowRects.Reset();
	OutRowRects.Reserve(SlotCount);

	// 行区起点 = 内边距 + 标题 + 上分隔线（与 DrawMineralLegend 的排版同源）。
	const float RowsTop = OutPanel.Y + MineralLegendLayout::Padding
		+ MineralLegendLayout::TitleLineHeight
		+ MineralLegendLayout::SeparatorSpacing + MineralLegendLayout::SeparatorThickness
		+ MineralLegendLayout::SeparatorSpacing;

	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		FStarMapPanelRect& Row = OutRowRects.AddDefaulted_GetRef();
		Row.X = OutPanel.X + MineralLegendLayout::Padding;
		Row.Y = RowsTop + SlotIndex * MineralLegendLayout::RowSlotHeight;
		Row.W = OutPanel.W - MineralLegendLayout::Padding * 2.f;
		Row.H = MineralLegendLayout::RowSlotHeight;
	}
}

int32 UStarMapUISystem::HitTestMineralLegendRow(
	const TArray<FStarMapPanelRect>& RowRects,
	const FVector2D& ScreenPosPx)
{
	for (int32 RowIndex = 0; RowIndex < RowRects.Num(); ++RowIndex)
	{
		const FStarMapPanelRect& Row = RowRects[RowIndex];
		// 上下各缩 HitInsetY：两行之间没有缝，不缩边会让接缝算作命中。
		if (ScreenPosPx.X >= Row.X && ScreenPosPx.X <= Row.X + Row.W
			&& ScreenPosPx.Y >= Row.Y + MineralLegendLayout::HitInsetY
			&& ScreenPosPx.Y <= Row.Y + Row.H - MineralLegendLayout::HitInsetY)
		{
			return RowIndex;
		}
	}
	return INDEX_NONE;
}

FStarMapPanelRect UStarMapUISystem::PlacePanelBesideAnchor(
	const FVector2D& AnchorPx,
	float AnchorRadiusPx,
	const FVector2D& PanelSizePx,
	const FVector2D& ViewportSizePx,
	float GapPx,
	float MarginPx)
{
	FStarMapPanelRect Rect;
	Rect.W = float(PanelSizePx.X);
	Rect.H = float(PanelSizePx.Y);

	const float RightX = float(AnchorPx.X) + AnchorRadiusPx + GapPx;
	const float LeftX = float(AnchorPx.X) - AnchorRadiusPx - GapPx - Rect.W;
	const float MaxX = FMath::Max(MarginPx, float(ViewportSizePx.X) - MarginPx - Rect.W);
	const float MaxY = FMath::Max(MarginPx, float(ViewportSizePx.Y) - MarginPx - Rect.H);

	Rect.X = RightX;
	if (RightX + Rect.W > float(ViewportSizePx.X) - MarginPx && LeftX >= MarginPx)
	{
		Rect.X = LeftX;
	}

	Rect.X = FMath::Clamp(Rect.X, MarginPx, MaxX);
	Rect.Y = FMath::Clamp(float(AnchorPx.Y) - Rect.H * 0.5f, MarginPx, MaxY);
	return Rect;
}

void UStarMapUISystem::Draw(AStellarEraHUD& Hud)
{
	const FVector2D ViewportSize = Hud.GetViewportSize();
	if (ViewportSize.X <= 0.0 || ViewportSize.Y <= 0.0)
	{
		return;
	}

	AStellarEraPlayerController* PlayerController =
		Cast<AStellarEraPlayerController>(Hud.GetOwningPlayerController());
	if (!PlayerController)
	{
		return;
	}

	const UWorld* World = Hud.GetWorld();
	UCosmosSimulationSubsystem* Subsystem = World
		? World->GetSubsystem<UCosmosSimulationSubsystem>()
		: nullptr;
	if (!Subsystem)
	{
		return;
	}

	AActor* Selected = Subsystem->GetSelectedBody();

	double MouseX = 0.0;
	double MouseY = 0.0;
	const bool bHasMouse = PlayerController->GetMousePosition(MouseX, MouseY);
	const FVector2D MousePosition(MouseX, MouseY);
	const bool bMouseInside = bHasMouse &&
		MousePosition.X >= 0.0 && MousePosition.Y >= 0.0 &&
		MousePosition.X <= ViewportSize.X && MousePosition.Y <= ViewportSize.Y;

	AActor* Hovered = bMouseInside
		? PlayerController->PickBodyUnderScreenPosition(MousePosition)
		: nullptr;

	// 选中面板：贴天体投影中心右侧。中心不在视口内就不画。
	if (IsValid(Selected))
	{
		FVector2D Center = FVector2D::ZeroVector;
		float RadiusPx = 0.f;
		const bool bProjected =
			PlayerController->ProjectBodyToScreen(Selected, Center, RadiusPx);
		const bool bCenterInside =
			Center.X >= 0.0 && Center.Y >= 0.0 &&
			Center.X <= ViewportSize.X && Center.Y <= ViewportSize.Y;

		if (bProjected && bCenterInside)
		{
			FStarMapBodyInfo Info;
			if (BuildBodyInfo(Selected, Info))
			{
				DrawBodyPanel(Hud, Info, Center, RadiusPx, ViewportSize);
			}
		}
	}

	// 悬停面板：贴鼠标右侧；悬停的正是选中天体时不重复弹。
	if (bMouseInside && IsValid(Hovered) && Hovered != Selected)
	{
		FStarMapBodyInfo Info;
		if (BuildBodyInfo(Hovered, Info))
		{
			DrawBodyPanel(Hud, Info, MousePosition, 0.f, ViewportSize);
		}
	}

	// 矿物剖面图例：固定左上角，画在最后（压在所有天体面板之上）。
	if (UPlanetCutawayComponent* Cutaway = PlayerController->GetActiveCutaway())
	{
		AVoxelPlanetActor* CutawayPlanet = Cutaway->GetCutawayPlanet();
		if (CutawayPlanet)
		{
			TArray<FCutawayLegendEntry> Entries;
			if (const TSharedPtr<const FPlanetGeologyField> Geology = Cutaway->GetGeologyField();
				Geology.IsValid())
			{
				const TArray<FPlanetGeologyElement> Elements = Geology->GetElementDefinitions();
				const float TotalAbundance = FMath::Max(Geology->GetTotalAbundance(), 1e-6f);
				Entries.Reserve(Elements.Num());
				for (const FPlanetGeologyElement& Element : Elements)
				{
					FCutawayLegendEntry& Entry = Entries.AddDefaulted_GetRef();
					Entry.DisplayName = Element.DisplayName.IsEmpty()
						? Element.Id
						: Element.DisplayName;
					Entry.Color = Element.Color;
					Entry.Abundance = Element.Abundance / TotalAbundance;
				}
			}

			DrawCutawayLegend(
				Hud, CutawayPlanet->GetActorNameOrLabel(), Entries, ViewportSize);
		}
	}
}

void UStarMapUISystem::DrawBodyPanel(
	AStellarEraHUD& Hud,
	const FStarMapBodyInfo& Info,
	const FVector2D& AnchorPx,
	float AnchorRadiusPx,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatBodyInfoLines(Info);
	if (Lines.Num() == 0)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float Padding = 10.f;
	constexpr float LineSpacing = 4.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float Gap = 12.f;
	constexpr float Margin = 8.f;

	const FString& Title = Lines[0];

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	Hud.GetTextSize(Title, TitleWidth, TitleHeight, Font, TextScale);

	float ContentWidth = TitleWidth;
	float LineHeight = 0.f;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		float LineWidth = 0.f;
		float ThisHeight = 0.f;
		Hud.GetTextSize(Lines[Index], LineWidth, ThisHeight, Font, TextScale);
		ContentWidth = FMath::Max(ContentWidth, LineWidth);
		LineHeight = FMath::Max(LineHeight, ThisHeight);
	}

	const int32 BodyLineCount = Lines.Num() - 1;
	const float PanelWidth = ContentWidth + Padding * 2.f;
	const float PanelHeight = Padding * 2.f + TitleHeight
		+ SeparatorSpacing * 2.f + 1.f
		+ BodyLineCount * (LineHeight + LineSpacing);

	const FStarMapPanelRect Rect = PlacePanelBesideAnchor(
		AnchorPx, AnchorRadiusPx,
		FVector2D(PanelWidth, PanelHeight), ViewportSizePx, Gap, Margin);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);

	Hud.DrawRect(Background, Rect.X, Rect.Y, Rect.W, Rect.H);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y + Rect.H - 1.f, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, 1.f, Rect.H);
	Hud.DrawRect(Gold, Rect.X + Rect.W - 1.f, Rect.Y, 1.f, Rect.H);

	float CursorY = Rect.Y + Padding;
	Hud.DrawText(Title, Gold, Rect.X + Padding, CursorY, Font, TextScale);
	CursorY += TitleHeight + SeparatorSpacing;

	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		Hud.DrawText(Lines[Index], White, Rect.X + Padding, CursorY, Font, TextScale);
		CursorY += LineHeight + LineSpacing;
	}
}

void UStarMapUISystem::DrawCutawayLegend(
	AStellarEraHUD& Hud,
	const FString& PlanetName,
	const TArray<FCutawayLegendEntry>& Entries,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatCutawayLegendLines(PlanetName, Entries);
	if (Lines.Num() == 0)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float Padding = 10.f;
	constexpr float LineSpacing = 4.f;
	constexpr float SeparatorSpacing = 6.f;
	constexpr float SwatchSize = 14.f;
	constexpr float SwatchGap = 8.f;
	constexpr float Margin = 8.f;

	// 结构固定：标题 / 元素行（每条一行）/ 操作提示。
	const FString& Title = Lines[0];
	const FString& Hint = Lines.Last();

	float TitleWidth = 0.f;
	float TitleHeight = 0.f;
	Hud.GetTextSize(Title, TitleWidth, TitleHeight, Font, TextScale);

	float RowTextWidth = 0.f;
	float RowHeight = 0.f;
	for (int32 Index = 1; Index < Lines.Num() - 1; ++Index)
	{
		float LineWidth = 0.f;
		float ThisHeight = 0.f;
		Hud.GetTextSize(Lines[Index], LineWidth, ThisHeight, Font, TextScale);
		RowTextWidth = FMath::Max(RowTextWidth, LineWidth);
		RowHeight = FMath::Max(RowHeight, ThisHeight);
	}

	float HintWidth = 0.f;
	float HintHeight = 0.f;
	Hud.GetTextSize(Hint, HintWidth, HintHeight, Font, TextScale);

	const int32 EntryCount = Entries.Num();
	const float TextColumnWidth =
		FMath::Max(TitleWidth, FMath::Max(RowTextWidth, HintWidth));
	const float PanelWidth = Padding * 2.f + SwatchSize + SwatchGap + TextColumnWidth;
	const float RowSlotHeight = FMath::Max(RowHeight, SwatchSize);
	const float RowsHeight = (EntryCount > 0 ? EntryCount : 1) * (RowSlotHeight + LineSpacing);
	const float PanelHeight = Padding * 2.f + TitleHeight
		+ SeparatorSpacing * 2.f + 1.f
		+ RowsHeight
		+ SeparatorSpacing * 2.f + 1.f + HintHeight;

	// 左上角 = 锚点就在边距上、半径 0、间距 0（PlacePanelBesideAnchor 会把 Y 夹到 Margin）。
	const FStarMapPanelRect Rect = PlacePanelBesideAnchor(
		FVector2D(Margin, Margin), 0.f,
		FVector2D(PanelWidth, PanelHeight), ViewportSizePx, 0.f, Margin);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(0.75f, 0.75f, 0.75f, 1.f);

	Hud.DrawRect(Background, Rect.X, Rect.Y, Rect.W, Rect.H);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y + Rect.H - 1.f, Rect.W, 1.f);
	Hud.DrawRect(Gold, Rect.X, Rect.Y, 1.f, Rect.H);
	Hud.DrawRect(Gold, Rect.X + Rect.W - 1.f, Rect.Y, 1.f, Rect.H);

	float CursorY = Rect.Y + Padding;
	Hud.DrawText(Title, Gold, Rect.X + Padding, CursorY, Font, TextScale);
	CursorY += TitleHeight + SeparatorSpacing;

	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	if (EntryCount > 0)
	{
		for (int32 Index = 0; Index < EntryCount; ++Index)
		{
			const float SwatchY = CursorY + (RowSlotHeight - SwatchSize) * 0.5f;
			Hud.DrawRect(Entries[Index].Color,
				Rect.X + Padding, SwatchY, SwatchSize, SwatchSize);
			Hud.DrawText(Lines[Index + 1], White,
				Rect.X + Padding + SwatchSize + SwatchGap, CursorY, Font, TextScale);
			CursorY += RowSlotHeight + LineSpacing;
		}
	}
	else
	{
		Hud.DrawText(Lines[1], White, Rect.X + Padding, CursorY, Font, TextScale);
		CursorY += RowSlotHeight + LineSpacing;
	}

	CursorY += SeparatorSpacing;
	Hud.DrawRect(Gold, Rect.X + Padding, CursorY, Rect.W - Padding * 2.f, 1.f);
	CursorY += 1.f + SeparatorSpacing;

	Hud.DrawText(Hint, Dim, Rect.X + Padding, CursorY, Font, TextScale);
}

void UStarMapUISystem::DrawMineralLegend(
	AStellarEraHUD& Hud,
	const FString& PlanetName,
	const TArray<FMineralLegendEntry>& Entries,
	const FVector2D& ViewportSizePx)
{
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (!Font)
	{
		return;
	}

	const TArray<FString> Lines = FormatMineralLegendLines(PlanetName, Entries);
	if (Lines.Num() < 3)
	{
		return;
	}

	constexpr float TextScale = 1.f;
	constexpr float SwatchSize = 14.f;
	constexpr float SwatchGap = 8.f;

	FStarMapPanelRect Panel;
	TArray<FStarMapPanelRect> RowRects;
	ComputeMineralLegendLayout(ViewportSizePx, Entries.Num(), Panel, RowRects);

	const FLinearColor Background(0.f, 0.f, 0.f, 0.65f);
	const FLinearColor Gold(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor White(1.f, 1.f, 1.f, 1.f);
	const FLinearColor Dim(0.75f, 0.75f, 0.75f, 1.f);

	Hud.DrawRect(Background, Panel.X, Panel.Y, Panel.W, Panel.H);
	Hud.DrawRect(Gold, Panel.X, Panel.Y, Panel.W, MineralLegendLayout::SeparatorThickness);
	Hud.DrawRect(Gold, Panel.X, Panel.Y + Panel.H - MineralLegendLayout::SeparatorThickness,
		Panel.W, MineralLegendLayout::SeparatorThickness);
	Hud.DrawRect(Gold, Panel.X, Panel.Y, MineralLegendLayout::SeparatorThickness, Panel.H);
	Hud.DrawRect(Gold, Panel.X + Panel.W - MineralLegendLayout::SeparatorThickness, Panel.Y,
		MineralLegendLayout::SeparatorThickness, Panel.H);

	const float TitleY = Panel.Y + MineralLegendLayout::Padding;
	Hud.DrawText(Lines[0], Gold, Panel.X + MineralLegendLayout::Padding, TitleY,
		Font, TextScale);

	const float SeparatorY = TitleY + MineralLegendLayout::TitleLineHeight
		+ MineralLegendLayout::SeparatorSpacing;
	Hud.DrawRect(Gold, Panel.X + MineralLegendLayout::Padding, SeparatorY,
		Panel.W - MineralLegendLayout::Padding * 2.f,
		MineralLegendLayout::SeparatorThickness);

	if (Entries.Num() > 0)
	{
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const FMineralLegendEntry& Entry = Entries[Index];
			const FStarMapPanelRect& Row = RowRects[Index];
			const float SwatchY = Row.Y + (Row.H - SwatchSize) * 0.5f;

			// 关掉的元素整行变暗：色块压到 35% 亮度，文字用 Dim。
			const FLinearColor SwatchColor = Entry.bVisible
				? Entry.Color
				: FLinearColor(Entry.Color.R * 0.35f, Entry.Color.G * 0.35f,
					Entry.Color.B * 0.35f, Entry.Color.A);
			Hud.DrawRect(SwatchColor, Panel.X + MineralLegendLayout::Padding, SwatchY,
				SwatchSize, SwatchSize);
			Hud.DrawText(Lines[Index + 1], Entry.bVisible ? White : Dim,
				Panel.X + MineralLegendLayout::Padding + SwatchSize + SwatchGap,
				Row.Y, Font, TextScale);
		}
	}
	else
	{
		Hud.DrawText(Lines[1], Dim, Panel.X + MineralLegendLayout::Padding,
			RowRects[0].Y, Font, TextScale);
	}

	// 提示行贴面板底：行数变化时提示不跟着上下跳。
	const float HintY = Panel.Y + Panel.H - MineralLegendLayout::Padding
		- MineralLegendLayout::HintLineHeight;
	Hud.DrawRect(Gold, Panel.X + MineralLegendLayout::Padding,
		HintY - MineralLegendLayout::SeparatorSpacing
			- MineralLegendLayout::SeparatorThickness,
		Panel.W - MineralLegendLayout::Padding * 2.f,
		MineralLegendLayout::SeparatorThickness);
	Hud.DrawText(Lines.Last(), Dim, Panel.X + MineralLegendLayout::Padding, HintY,
		Font, TextScale);
}
