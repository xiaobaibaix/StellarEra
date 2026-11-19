#include "SGeoViewerPanel.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"
#include "PlanetBakePipeline.h"
#include "PlanetGeoDebugRender.h"
#include "Styling/AppStyle.h"
#include "VoxelPlanetActor.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 GeoViewerMapW = 1024, GeoViewerMapH = 512, GeoViewerSection = 512;
	// 拖动时用低分辨率保证跟手，松手 0.25 s 后补一张高分辨率。
	// 交互期只求手感，静止后再出全分辨率：3D 成本 ∝ 像素 × 步数，两者都按此调低。
	constexpr int32 GeoViewer3DFast = 128, GeoViewer3DFull = 512;
	constexpr double GeoViewerSettleSeconds = 0.25;

	/** 悬停读数用：鼠标在图片上的归一化位置交给回调。 */
	class SGeoHoverImage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGeoHoverImage) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
		SLATE_END_ARGS()

		TFunction<void(FVector2f)> OnHover;

		void Construct(const FArguments& InArgs) { ChildSlot[SNew(SImage).Image(InArgs._Brush)]; }

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			const FVector2f Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
			const FVector2f Size = Geometry.GetLocalSize();
			if (OnHover && Size.X > 0.f && Size.Y > 0.f) { OnHover(FVector2f(Local.X / Size.X, Local.Y / Size.Y)); }
			return FReply::Handled();
		}
	};

	/** 3D 视图：左键拖动旋转、右键拖动平移、滚轮缩放、悬停读数。 */
	class SGeoOrbitImage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGeoOrbitImage) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
		SLATE_END_ARGS()

		TFunction<void(FVector2f)> OnHover;
		TFunction<void(FVector2f, bool)> OnDrag;   // 像素位移；true = 平移
		TFunction<void(float)> OnZoom;

		void Construct(const FArguments& InArgs) { ChildSlot[SNew(SImage).Image(InArgs._Brush)]; }

		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() == EKeys::LeftMouseButton || Event.GetEffectingButton() == EKeys::RightMouseButton)
			{
				bPan = Event.GetEffectingButton() == EKeys::RightMouseButton;
				bDragging = true;
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
			return FReply::Unhandled();
		}
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Event) override
		{
			if (bDragging) { bDragging = false; return FReply::Handled().ReleaseMouseCapture(); }
			return FReply::Unhandled();
		}
		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (bDragging && OnDrag) { OnDrag(FVector2f(Event.GetCursorDelta()), bPan); return FReply::Handled(); }
			const FVector2f Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
			const FVector2f Size = Geometry.GetLocalSize();
			if (OnHover && Size.X > 0.f && Size.Y > 0.f) { OnHover(FVector2f(Local.X / Size.X, Local.Y / Size.Y)); }
			return FReply::Handled();
		}
		virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent& Event) override
		{
			if (OnZoom) { OnZoom(Event.GetWheelDelta()); }
			return FReply::Handled();
		}
		virtual FCursorReply OnCursorQuery(const FGeometry&, const FPointerEvent&) const override
		{
			return FCursorReply::Cursor(bDragging ? EMouseCursor::GrabHandClosed : EMouseCursor::GrabHand);
		}

	private:
		bool bDragging = false;
		bool bPan = false;
	};

	TSharedRef<SWidget> GeoViewerSwatch(const FColor& C, const FString& Label)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 1, 6, 1)
			[
				SNew(SBox).WidthOverride(18).HeightOverride(12)
				[
					SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor(C))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(FText::FromString(Label))];
	}

	TSharedRef<SWidget> GeoViewerLabeled(const TCHAR* Label, TSharedRef<SWidget> Widget)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(STextBlock).Text(FText::FromString(Label))]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Widget];
	}

	const FGeoDebugLayer* GeoViewerLayer(int32 Index)
	{
		const TArray<FGeoDebugLayer>& Layers = FPlanetBakePipeline::DefaultDebugLayers();
		return Layers.IsValidIndex(Index) ? &Layers[Index] : nullptr;
	}
}

void SGeoViewerPanel::Construct(const FArguments& InArgs)
{
	OuterItems.Add(MakeShared<FString>(TEXT("外表面：同主图层")));
	OuterIds.Add(NAME_None);
	for (const FGeoDebugLayer& L : FPlanetBakePipeline::DefaultDebugLayers())
	{
		LayerItems.Add(MakeShared<FString>(FString::Printf(TEXT("%s · %s%s"),
			L.Stage == 0 ? TEXT("矿物") : *FString::Printf(TEXT("阶段 %u"), L.Stage), *L.DisplayName,
			L.Domain == EGeoDebugDomain::Volume ? TEXT("（地下）") : TEXT(""))));
		if (L.Domain == EGeoDebugDomain::Surface)
		{
			OuterItems.Add(MakeShared<FString>(FString::Printf(TEXT("外表面：%s"), *L.DisplayName)));
			OuterIds.Add(L.Id);
		}
	}
	// 默认外表面看海陆、切面看圈层：一眼同时看到地表与内部。
	OuterIndex = FMath::Max(0, OuterIds.IndexOfByKey(FName(TEXT("Elevation"))));
	PlaneItems = { MakeShared<FString>(TEXT("XZ 剖面（过两极）")), MakeShared<FString>(TEXT("YZ 剖面（过两极）")),
		MakeShared<FString>(TEXT("XY 剖面（赤道面）")) };
	ClipItems = { MakeShared<FString>(TEXT("不切")), MakeShared<FString>(TEXT("切掉 +X 半球")),
		MakeShared<FString>(TEXT("切掉四分之一")), MakeShared<FString>(TEXT("正对视线切开（可调深度）")) };
	RefreshPlanets();

	TSharedRef<SGeoHoverImage> MapImage = SNew(SGeoHoverImage).Brush(&Map.Brush);
	MapImage->OnHover = [this](FVector2f UV) { OnMapHover(UV); };
	TSharedRef<SGeoHoverImage> SectionImage = SNew(SGeoHoverImage).Brush(&Section.Brush);
	SectionImage->OnHover = [this](FVector2f UV) { OnSectionHover(UV); };
	TSharedRef<SGeoOrbitImage> OrbitImage = SNew(SGeoOrbitImage).Brush(&View3D.Brush);
	OrbitImage->OnHover = [this](FVector2f UV) { OnView3DHover(UV); };
	OrbitImage->OnDrag = [this](FVector2f Delta, bool bPan)
	{
		if (bPan)
		{
			const float K = 0.0025f * View.Distance;
			View.Pan += FVector2f(-Delta.X * K, Delta.Y * K);
		}
		else
		{
			View.YawDeg -= Delta.X * 0.4f;
			View.PitchDeg = FMath::Clamp(View.PitchDeg + Delta.Y * 0.4f, -89.f, 89.f);
		}
		MarkView3DDirty();
	};
	OrbitImage->OnZoom = [this](float Wheel)
	{
		// 距离是到视线中心的，最近能贴到地表附近看细节。
		View.Distance = FMath::Clamp(View.Distance * FMath::Pow(0.88f, Wheel), 0.08f, 12.f);
		MarkView3DDirty();
	};

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("星球")))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
				[
					SAssignNew(PlanetCombo, STextComboBox).OptionsSource(&PlanetItems)
					.InitiallySelectedItem(PlanetItems.IsEmpty() ? nullptr : PlanetItems[0])
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						PlanetIndex = FMath::Max(0, PlanetItems.IndexOfByKey(Item));
						Render();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton).Text(FText::FromString(TEXT("刷新"))).OnClicked_Lambda([this]()
					{
						RefreshPlanets();
						if (PlanetCombo) { PlanetCombo->RefreshOptions(); PlanetCombo->SetSelectedItem(PlanetItems.IsEmpty() ? nullptr : PlanetItems[0]); }
						Render();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("图层")))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
				[
					SNew(STextComboBox).OptionsSource(&LayerItems)
					.InitiallySelectedItem(LayerItems.IsEmpty() ? nullptr : LayerItems[0])
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						LayerIndex = FMath::Max(0, LayerItems.IndexOfByKey(Item));
						View.Hidden.Reset();
						Render();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(FText::FromString(TEXT("导出全部图层 PNG"))).OnClicked_Lambda([this]() { Export(); return FReply::Handled(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
				[
					SNew(STextComboBox).OptionsSource(&OuterItems).InitiallySelectedItem(OuterItems[OuterIndex])
					.ToolTipText(FText::FromString(TEXT("从太空直接看到的外表面用哪个图层上色；切面与内部始终用主图层")))
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						OuterIndex = FMath::Max(0, OuterItems.IndexOfByKey(Item));
						MarkView3DDirty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
				[
					SNew(STextComboBox).OptionsSource(&ClipItems).InitiallySelectedItem(ClipItems[int32(View.Clip)])
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						View.Clip = PlanetGeoDebugView3D::EClip(FMath::Max(0, ClipItems.IndexOfByKey(Item)));
						MarkView3DDirty();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
				[
					GeoViewerLabeled(TEXT("切面位置"), SNew(SBox).WidthOverride(140)
					[
						SNew(SSlider).MinValue(-1.f).MaxValue(1.f).Value_Lambda([this]() { return View.ClipOffset; })
						.ToolTipText(FText::FromString(TEXT("「正对视线切开」时切面离球心的距离：0 过球心，向右切得越少（看浅层），向左切得越深")))
						.OnValueChanged_Lambda([this](float V) { View.ClipOffset = V; MarkView3DDirty(); })
					])
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
				[
					GeoViewerLabeled(TEXT("不透明度"), SNew(SBox).WidthOverride(140)
					[
						SNew(SSlider).MinValue(0.05f).MaxValue(1.f).Value_Lambda([this]() { return View.Opacity; })
						.ToolTipText(FText::FromString(TEXT("小于 1 时每一层都半透明，能透过外层看到里层的形状")))
						.OnValueChanged_Lambda([this](float V) { View.Opacity = V; MarkView3DDirty(); })
					])
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this]() { return bShowMinerals ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
					.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bShowMinerals = S == ECheckBoxState::Checked; Render(); })
					.ToolTipText(FText::FromString(TEXT("在 3D 切面与 2D 剖面上叠加矿体颜色")))
					[SNew(STextBlock).Text(FText::FromString(TEXT("切面显示矿体")))]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
				[
					GeoViewerLabeled(TEXT("矿体显示放大"), SNew(SBox).WidthOverride(140)
					[
						SNew(SSlider).MinValue(1.f).MaxValue(50.f).Value_Lambda([this]() { return MineralScale; })
						.ToolTipText(FText::FromString(TEXT("只影响显示：真实尺寸的矿体在整星视图里不到一个像素")))
						.OnValueChanged_Lambda([this](float V) { MineralScale = V; Render(); })
					])
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
				[
					SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%.0f×"), MineralScale)); })
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton).Text(FText::FromString(TEXT("重置视角"))).OnClicked_Lambda([this]() { ResetView(); return FReply::Handled(); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[
				SNew(STextBlock).Text_Lambda([this]() { return Readout; })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[OrbitImage]
					+ SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text_Lambda([this]() { return Status3D; })]
				]
				+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(LegendBox, SVerticalBox)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("2D：左 全球展开（经度 −180°…180°，上北下南）；右 过球心剖面")))
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(STextComboBox).OptionsSource(&PlaneItems).InitiallySelectedItem(PlaneItems[0])
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						PlaneIndex = FMath::Max(0, PlaneItems.IndexOfByKey(Item));
						Render();
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[MapImage]
				+ SHorizontalBox::Slot().AutoWidth()[SectionImage]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SNew(STextBlock).Text_Lambda([this]() { return Status; }).AutoWrapText(true)
			]
		]
	];
	Render();
}

void SGeoViewerPanel::ResetView()
{
	const TArray<bool> Hidden = View.Hidden;
	View = PlanetGeoDebugView3D::FView();
	View.Hidden = Hidden;
	MarkView3DDirty();
}

void SGeoViewerPanel::MarkView3DDirty()
{
	bView3DDirty = true;
	bView3DFull = false;
	LastInteraction = FPlatformTime::Seconds();
}

void SGeoViewerPanel::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// 3D 图占满面板：减掉右侧图例与边距，剩下的宽度按 16:9 出图。
	const float Want = FMath::Clamp(float(AllottedGeometry.GetLocalSize().X) - 300.f, 320.f, 1600.f);
	if (FMath::Abs(Want - View3DWidth) > 16.f)
	{
		View3DWidth = Want;
		MarkView3DDirty();
	}
	// 烘焙是异步的：没烘完就每秒重试一次，烘好了自己出图。
	if (!bRendered && FPlatformTime::Seconds() - LastAutoRetry > 1.0)
	{
		LastAutoRetry = FPlatformTime::Seconds();
		if (CurrentSnapshot().IsValid())
		{
			RefreshPlanets();
			Render();
			MarkView3DDirty();
		}
	}
	if (bView3DDirty)
	{
		bView3DDirty = false;
		Render3D(false);
	}
	else if (!bView3DFull && FPlatformTime::Seconds() - LastInteraction > GeoViewerSettleSeconds)
	{
		bView3DFull = true;
		Render3D(true);
	}
}

void SGeoViewerPanel::RefreshPlanets()
{
	PlanetItems.Reset();
	Planets.Reset();
	if (!GEditor) { return; }
	// PIE 在跑时优先看 PIE 世界：那份快照才是正在玩的那颗星。
	TArray<UWorld*> Worlds;
	if (GEditor->PlayWorld) { Worlds.Add(GEditor->PlayWorld); }
	Worlds.Add(GEditor->GetEditorWorldContext().World());
	for (UWorld* World : Worlds)
	{
		if (!World) { continue; }
		for (TActorIterator<AVoxelPlanetActor> It(World); It; ++It)
		{
			if (!It->IsGeoMode()) { continue; }
			Planets.Add(*It);
			PlanetItems.Add(MakeShared<FString>(FString::Printf(TEXT("%s%s"), *It->GetActorLabel(),
				World->IsPlayInEditor() ? TEXT("（PIE）") : TEXT(""))));
		}
	}
	PlanetIndex = 0;
}

TSharedPtr<const FPlanetSnapshot> SGeoViewerPanel::CurrentSnapshot() const
{
	const AVoxelPlanetActor* Planet = Planets.IsValidIndex(PlanetIndex) ? Planets[PlanetIndex].Get() : nullptr;
	return Planet ? Planet->GetGeoSnapshot() : nullptr;
}

const FGeoDebugLayer* SGeoViewerPanel::OuterLayer() const
{
	return OuterIds.IsValidIndex(OuterIndex) ? FPlanetBakePipeline::FindDefaultDebugLayer(OuterIds[OuterIndex]) : nullptr;
}

void SGeoViewerPanel::Upload(FImageView& Img, int32 W, int32 H, const TArray<FColor>& Colors, FVector2D DisplaySize)
{
	if (!Img.Texture || Img.W != W || Img.H != H)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!T) { return; }
		T->SRGB = true;
		T->Filter = TF_Bilinear;
		Img.Texture.Reset(T);
		Img.W = W;
		Img.H = H;
	}
	FTexture2DMipMap& Mip = Img.Texture->GetPlatformData()->Mips[0];
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Colors.GetData(), Colors.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Img.Texture->UpdateResource();
	Img.Brush.SetResourceObject(Img.Texture.Get());
	Img.Brush.ImageSize = DisplaySize;
	Img.Brush.DrawAs = ESlateBrushDrawType::Image;
}

void SGeoViewerPanel::Render3D(bool bFull)
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer) { return; }
	const double T0 = FPlatformTime::Seconds();
	const int32 Width = FMath::RoundToInt(View3DWidth);
	const int32 Height = FMath::Max(1, FMath::RoundToInt(View3DWidth * 9.f / 16.f));
	// 交互中按 1/4 宽高光线步进，只降采样不改宽高比：全分辨率每拖动一帧都太贵。
	const int32 Scale = bFull ? 1 : 4;
	const int32 RenderW = FMath::Max(1, Width / Scale), RenderH = FMath::Max(1, Height / Scale);
	PlanetGeoDebugView3D::FView V = View;
	V.Steps = bFull ? 320 : 48;
	V.MineralScale = bShowMinerals && !Layer->RayHits ? MineralScale : 0.f;
	TArray<FColor> Colors;
	PlanetGeoDebugView3D::Render(*Snap, *Layer, OuterLayer(), V, RenderW, RenderH, Colors, &HitPos, &HitValues);
	Upload(View3D, RenderW, RenderH, Colors, FVector2D(Width, Height));
	Status3D = FText::FromString(FString::Printf(TEXT("3D：左键拖动旋转 · 右键拖动平移 · 滚轮缩放   %d×%d，%.0f ms"),
		Width, Height, (FPlatformTime::Seconds() - T0) * 1000.0));
}

void SGeoViewerPanel::Render()
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer)
	{
		Status = FText::FromString(Planets.IsEmpty()
			? TEXT("当前关卡没有挂配方的星球（GeoRecipe 为空），或还没烘焙完；点「刷新」重试。")
			: TEXT("这颗星球还没烘焙完，稍后点「刷新」。"));
		RebuildLegend();
		bRendered = false;
		return;
	}
	bRendered = true;
	const double T0 = FPlatformTime::Seconds();
	TArray<FColor> Colors;
	PlanetGeoDebugRender::RenderMap(*Snap, *Layer, GeoViewerMapW, GeoViewerMapH, Colors, &Map.Values);
	Upload(Map, GeoViewerMapW, GeoViewerMapH, Colors, FVector2D(640, 320));
	PlanetGeoDebugRender::RenderSection(*Snap, *Layer, GeoViewerSection,
		PlanetGeoDebugRender::ESectionPlane(PlaneIndex), Colors, &Section.Values, bShowMinerals && !Layer->RayHits ? MineralScale : 0.f);
	Upload(Section, GeoViewerSection, GeoViewerSection, Colors, FVector2D(320, 320));
	Status = FText::FromString(FString::Printf(TEXT("「%s」2D 出图 %.0f ms。%s"),
		*Layer->DisplayName, (FPlatformTime::Seconds() - T0) * 1000.0,
		Snap->DebugFields.IsEmpty() ? TEXT("快照里没有调试场（非编辑器烘焙），阶段 2 的辅助图层会显示为无数据。") : TEXT("")));
	RebuildLegend();
	MarkView3DDirty();
}

void SGeoViewerPanel::RebuildLegend()
{
	if (!LegendBox) { return; }
	LegendBox->ClearChildren();
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer) { return; }
	LegendBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SNew(STextBlock).Text(FText::FromString(Layer->DisplayName))];
	const TArray<FGeoDebugLegendEntry> Legend = Layer->GetLegend(*Snap);
	if (Layer->Kind == EGeoDebugKind::Categorical && !Legend.IsEmpty())
	{
		if (View.Hidden.Num() != Legend.Num())
		{
			View.Hidden.Init(false, Legend.Num());
			for (const int32 i : Layer->DefaultHidden) { if (View.Hidden.IsValidIndex(i)) { View.Hidden[i] = true; } }
		}
		LegendBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("取消勾选 = 在 3D 里隐藏这一层")))
		];
		for (int32 i = 0; i < Legend.Num(); ++i)
		{
			const FGeoDebugLegendEntry& E = Legend[i];
			LegendBox->AddSlot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SCheckBox)
					.IsChecked_Lambda([this, i]() { return View.Hidden.IsValidIndex(i) && View.Hidden[i] ? ECheckBoxState::Unchecked : ECheckBoxState::Checked; })
					.OnCheckStateChanged_Lambda([this, i](ECheckBoxState State)
					{
						if (View.Hidden.IsValidIndex(i)) { View.Hidden[i] = State != ECheckBoxState::Checked; MarkView3DDirty(); }
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[GeoViewerSwatch(E.Color, E.Label)]
			];
		}
	}
	else
	{
		View.Hidden.Reset();
		const FVector2f R = Layer->GetRange(*Snap);
		for (int32 i = 4; i >= 0; --i)
		{
			const float V = FMath::Lerp(R.X, R.Y, float(i) / 4.f);
			LegendBox->AddSlot().AutoHeight()[GeoViewerSwatch(Layer->ColorOf(*Snap, V), Layer->FormatValue(*Snap, V))];
		}
	}
	LegendBox->AddSlot().AutoHeight()[GeoViewerSwatch(FGeoDebugLayer::NoDataColor(), TEXT("无数据"))];
}

void SGeoViewerPanel::OnView3DHover(FVector2f UV)
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	const int32 Res = View3D.W;
	if (!Snap || !Layer || Res <= 0 || HitPos.Num() != Res * Res) { return; }
	const int32 X = FMath::Clamp(int32(UV.X * Res), 0, Res - 1), Y = FMath::Clamp(int32(UV.Y * Res), 0, Res - 1);
	const FVector3d P = HitPos[Y * Res + X];
	if (P.ContainsNaN()) { Readout = FText::FromString(TEXT("3D  太空")); return; }
	const FVector3d Dir = P.GetSafeNormal();
	const double Depth = Snap->R0 - P.Size();
	const AVoxelPlanetActor* Planet = Planets.IsValidIndex(PlanetIndex) ? Planets[PlanetIndex].Get() : nullptr;
	const double Km = Planet ? Depth * double(Planet->VoxelSize) / 100000.0 : 0.0;
	Readout = FText::FromString(FString::Printf(TEXT("3D  纬度 %.1f°  经度 %.1f°  距海平面深度 %.0f 体素（%.1f km）  →  %s"),
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Dir.Z, -1.0, 1.0))), FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)),
		Depth, Km, *Layer->FormatValue(*Snap, HitValues[Y * Res + X])));
}

void SGeoViewerPanel::OnMapHover(FVector2f UV)
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer || Map.Values.Num() != Map.W * Map.H || Map.W == 0) { return; }
	const int32 X = FMath::Clamp(int32(UV.X * Map.W), 0, Map.W - 1), Y = FMath::Clamp(int32(UV.Y * Map.H), 0, Map.H - 1);
	const float Lon = (float(X) + 0.5f) / float(Map.W) * 360.f - 180.f, Lat = 90.f - (float(Y) + 0.5f) / float(Map.H) * 180.f;
	Readout = FText::FromString(FString::Printf(TEXT("地图  纬度 %.1f°  经度 %.1f°  →  %s"), Lat, Lon,
		*Layer->FormatValue(*Snap, Map.Values[Y * Map.W + X])));
}

void SGeoViewerPanel::OnSectionHover(FVector2f UV)
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer || Section.Values.Num() != Section.W * Section.H || Section.W == 0) { return; }
	const int32 X = FMath::Clamp(int32(UV.X * Section.W), 0, Section.W - 1), Y = FMath::Clamp(int32(UV.Y * Section.H), 0, Section.H - 1);
	const FVector3d P = PlanetGeoDebugRender::SectionPixelToPos(*Snap, X, Y, Section.W, PlanetGeoDebugRender::ESectionPlane(PlaneIndex));
	const double Depth = Snap->R0 - P.Size();
	const AVoxelPlanetActor* Planet = Planets.IsValidIndex(PlanetIndex) ? Planets[PlanetIndex].Get() : nullptr;
	const double Km = Planet ? Depth * double(Planet->VoxelSize) / 100000.0 : 0.0;
	Readout = FText::FromString(FString::Printf(TEXT("剖面  距海平面深度 %.0f 体素（%.1f km）  →  %s"),
		Depth, Km, *Layer->FormatValue(*Snap, Section.Values[Y * Section.W + X])));
}

void SGeoViewerPanel::Export()
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	if (!Snap) { return; }
	const FString Name = PlanetItems.IsValidIndex(PlanetIndex) ? *PlanetItems[PlanetIndex] : FString(TEXT("Planet"));
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GeoDebug"), Name));
	const int32 N = PlanetGeoDebugRender::ExportAll(*Snap, FPlanetBakePipeline::DefaultDebugLayers(), Dir, GeoViewerMapW);
	Status = FText::FromString(FString::Printf(TEXT("导出 %d 张图到 %s"), N, *Dir));
}
