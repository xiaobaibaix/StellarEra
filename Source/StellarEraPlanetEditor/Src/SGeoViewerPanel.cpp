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
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	constexpr int32 GeoViewerMapW = 1024, GeoViewerMapH = 512, GeoViewerSection = 512;

	/** 悬停读数用：鼠标在图片上的归一化位置交给回调。 */
	class SGeoHoverImage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGeoHoverImage) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
		SLATE_END_ARGS()

		TFunction<void(FVector2f)> OnHover;

		void Construct(const FArguments& InArgs)
		{
			ChildSlot[SNew(SImage).Image(InArgs._Brush)];
		}

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			const FVector2f Local = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
			const FVector2f Size = Geometry.GetLocalSize();
			if (OnHover && Size.X > 0.f && Size.Y > 0.f)
			{
				OnHover(FVector2f(Local.X / Size.X, Local.Y / Size.Y));
			}
			return FReply::Handled();
		}
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

	const FGeoDebugLayer* GeoViewerLayer(int32 Index)
	{
		const TArray<FGeoDebugLayer>& Layers = FPlanetBakePipeline::DefaultDebugLayers();
		return Layers.IsValidIndex(Index) ? &Layers[Index] : nullptr;
	}
}

void SGeoViewerPanel::Construct(const FArguments& InArgs)
{
	for (const FGeoDebugLayer& L : FPlanetBakePipeline::DefaultDebugLayers())
	{
		LayerItems.Add(MakeShared<FString>(FString::Printf(TEXT("阶段 %u · %s%s"), L.Stage, *L.DisplayName,
			L.Domain == EGeoDebugDomain::Volume ? TEXT("（地下）") : TEXT(""))));
	}
	PlaneItems = { MakeShared<FString>(TEXT("XZ 剖面（过两极）")), MakeShared<FString>(TEXT("YZ 剖面（过两极）")),
		MakeShared<FString>(TEXT("XY 剖面（赤道面）")) };
	RefreshPlanets();

	TSharedRef<SGeoHoverImage> MapImage = SNew(SGeoHoverImage).Brush(&Map.Brush);
	MapImage->OnHover = [this](FVector2f UV) { OnMapHover(UV); };
	TSharedRef<SGeoHoverImage> SectionImage = SNew(SGeoHoverImage).Brush(&Section.Brush);
	SectionImage->OnHover = [this](FVector2f UV) { OnSectionHover(UV); };

	ChildSlot
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot().Padding(6)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
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
						Render();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(STextComboBox).OptionsSource(&PlaneItems).InitiallySelectedItem(PlaneItems[0])
					.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Item, ESelectInfo::Type)
					{
						PlaneIndex = FMath::Max(0, PlaneItems.IndexOfByKey(Item));
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
				SNew(STextBlock).Text_Lambda([this]() { return Readout; })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[MapImage]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[SectionImage]
				+ SHorizontalBox::Slot().AutoWidth()[SAssignNew(LegendBox, SVerticalBox)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SNew(STextBlock).Text_Lambda([this]() { return Status; }).AutoWrapText(true)
			]
		]
	];
	Render();
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

void SGeoViewerPanel::Upload(FImageView& View, int32 W, int32 H, const TArray<FColor>& Colors, FVector2D DisplaySize)
{
	if (!View.Texture || View.W != W || View.H != H)
	{
		UTexture2D* T = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
		if (!T) { return; }
		T->SRGB = true;
		T->Filter = TF_Nearest;
		View.Texture.Reset(T);
		View.W = W;
		View.H = H;
	}
	FTexture2DMipMap& Mip = View.Texture->GetPlatformData()->Mips[0];
	void* Dest = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Dest, Colors.GetData(), Colors.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	View.Texture->UpdateResource();
	View.Brush.SetResourceObject(View.Texture.Get());
	View.Brush.ImageSize = DisplaySize;
	View.Brush.DrawAs = ESlateBrushDrawType::Image;
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
		return;
	}
	const double T0 = FPlatformTime::Seconds();
	TArray<FColor> Colors;
	PlanetGeoDebugRender::RenderMap(*Snap, *Layer, GeoViewerMapW, GeoViewerMapH, Colors, &Map.Values);
	Upload(Map, GeoViewerMapW, GeoViewerMapH, Colors, FVector2D(768, 384));
	PlanetGeoDebugRender::RenderSection(*Snap, *Layer, GeoViewerSection,
		PlanetGeoDebugRender::ESectionPlane(PlaneIndex), Colors, &Section.Values);
	Upload(Section, GeoViewerSection, GeoViewerSection, Colors, FVector2D(384, 384));
	Status = FText::FromString(FString::Printf(TEXT("「%s」出图 %.0f ms。左：全球展开（经度 −180°…180°，上北下南）；右：过球心剖面。%s"),
		*Layer->DisplayName, (FPlatformTime::Seconds() - T0) * 1000.0,
		Snap->DebugFields.IsEmpty() ? TEXT("快照里没有调试场（非编辑器烘焙），阶段 2 的辅助图层会显示为无数据。") : TEXT("")));
	RebuildLegend();
}

void SGeoViewerPanel::RebuildLegend()
{
	if (!LegendBox) { return; }
	LegendBox->ClearChildren();
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Snap || !Layer) { return; }
	LegendBox->AddSlot().AutoHeight().Padding(0, 0, 0, 4)[SNew(STextBlock).Text(FText::FromString(Layer->DisplayName))];
	if (Layer->Kind == EGeoDebugKind::Categorical && !Layer->Legend.IsEmpty())
	{
		for (const FGeoDebugLegendEntry& E : Layer->Legend)
		{
			LegendBox->AddSlot().AutoHeight()[GeoViewerSwatch(E.Color, E.Label)];
		}
	}
	else
	{
		const FVector2f R = Layer->GetRange(*Snap);
		for (int32 i = 4; i >= 0; --i)
		{
			const float V = FMath::Lerp(R.X, R.Y, float(i) / 4.f);
			LegendBox->AddSlot().AutoHeight()[GeoViewerSwatch(Layer->ColorOf(*Snap, V), Layer->FormatValue(V))];
		}
	}
	LegendBox->AddSlot().AutoHeight()[GeoViewerSwatch(FGeoDebugLayer::NoDataColor(), TEXT("无数据"))];
}

void SGeoViewerPanel::OnMapHover(FVector2f UV)
{
	const FGeoDebugLayer* Layer = GeoViewerLayer(LayerIndex);
	if (!Layer || Map.Values.Num() != Map.W * Map.H || Map.W == 0) { return; }
	const int32 X = FMath::Clamp(int32(UV.X * Map.W), 0, Map.W - 1), Y = FMath::Clamp(int32(UV.Y * Map.H), 0, Map.H - 1);
	const float Lon = (float(X) + 0.5f) / float(Map.W) * 360.f - 180.f, Lat = 90.f - (float(Y) + 0.5f) / float(Map.H) * 180.f;
	Readout = FText::FromString(FString::Printf(TEXT("地图  纬度 %.1f°  经度 %.1f°  →  %s"), Lat, Lon,
		*Layer->FormatValue(Map.Values[Y * Map.W + X])));
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
	const double Meters = Planet ? Depth * double(Planet->VoxelSize) / 100.0 : 0.0;
	Readout = FText::FromString(FString::Printf(TEXT("剖面  距海平面深度 %.0f 体素（%.1f km）  →  %s"),
		Depth, Meters / 1000.0, *Layer->FormatValue(Section.Values[Y * Section.W + X])));
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
