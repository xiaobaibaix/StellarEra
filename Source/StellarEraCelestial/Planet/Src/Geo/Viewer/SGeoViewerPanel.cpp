#if WITH_EDITOR
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
#include "Async/Async.h"
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
	/** 导出 PNG 的地图宽度（2D 面板已移除，只剩导出用）。 */
	constexpr int32 GeoViewerMapW = 1024;
	// 拖动时用低分辨率保证跟手，松手 0.25 s 后补一张高分辨率。
	// 交互期只求手感，静止后再出全分辨率：3D 成本 ∝ 像素 × 步数，两者都按此调低。
	constexpr int32 GeoViewer3DFast = 128, GeoViewer3DFull = 512;
	constexpr double GeoViewerSettleSeconds = 0.25;

	/** 3D 视图：左键拖动旋转、右键拖动平移、滚轮缩放、悬停读数。 */
	class SGeoOrbitImage : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SGeoOrbitImage) {}
			SLATE_ARGUMENT(const FSlateBrush*, Brush)
		SLATE_END_ARGS()

		TFunction<void(FVector2f)> OnHover;
		TFunction<void(FVector2f, bool)> OnDrag;   // 像素位移；true = 平移
		TFunction<void()> OnDragEnd;
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
			if (bDragging) { bDragging = false; if (OnDragEnd) { OnDragEnd(); } return FReply::Handled().ReleaseMouseCapture(); }
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

}

void SGeoViewerPanel::Construct(const FArguments& InArgs)
{
	ClipItems = { MakeShared<FString>(TEXT("不切")), MakeShared<FString>(TEXT("切掉 +X 半球")),
		MakeShared<FString>(TEXT("切掉四分之一")), MakeShared<FString>(TEXT("正对视线切开（可调深度）")) };
	RefreshPlanets();
	RefreshLayers();

	TSharedRef<SGeoOrbitImage> OrbitImage = SNew(SGeoOrbitImage).Brush(&View3D.Brush);
	OrbitImage->OnHover = [this](FVector2f UV) { OnView3DHover(UV); };
	OrbitImage->OnDrag = [this](FVector2f Delta, bool bPan)
	{
		if (bPan)
		{
			const float K = 0.0025f * View.Distance;
			View.Pan += FVector2f(Delta.X * K, -Delta.Y * K);
		}
		else
		{
			View.YawDeg += Delta.X * 0.4f;
			View.PitchDeg = FMath::Clamp(View.PitchDeg + Delta.Y * 0.4f, -89.f, 89.f);
		}
		// 交互那一遍 2 ms，拖动时就实时出图；贵的矿体查询只在静止那一遍做。
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
						RefreshLayers();
						Render();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)
				[
					SNew(SButton).Text(FText::FromString(TEXT("刷新"))).OnClicked_Lambda([this]()
					{
						RefreshPlanets();
						if (PlanetCombo) { PlanetCombo->RefreshOptions(); PlanetCombo->SetSelectedItem(PlanetItems.IsEmpty() ? nullptr : PlanetItems[0]); }
						RefreshLayers();
						Render();
						return FReply::Handled();
					})
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)[SNew(STextBlock).Text(FText::FromString(TEXT("图层")))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 8, 0)
				[
					SAssignNew(LayerCombo, STextComboBox).OptionsSource(&LayerItems)
					.InitiallySelectedItem(LayerItems.IsValidIndex(LayerIndex) ? LayerItems[LayerIndex] : nullptr)
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
					SAssignNew(OuterCombo, STextComboBox).OptionsSource(&OuterItems).InitiallySelectedItem(OuterItems.IsValidIndex(OuterIndex) ? OuterItems[OuterIndex] : nullptr)
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
				+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 8, 0)[OrbitImage]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox).WidthOverride(320)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[SAssignNew(LegendBox, SVerticalBox)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
						[SNew(STextBlock).Text_Lambda([this]() { return Status3D; }).AutoWrapText(true)]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[SNew(STextBlock).Text_Lambda([this]() { return Status; }).AutoWrapText(true)]
					]
				]
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
	// 重新烘焙（改了配方/阶段开关）后快照换了：自动重列图层并出图，别停在旧阶段。
	const FPlanetSnapshot* SnapNow = CurrentSnapshot().Get();
	if (SnapNow && (SnapNow != LastLayerSnapshot || SnapNow->DisabledStages != LastLayerDisabled))
	{
		RefreshLayers();
		Render();
		MarkView3DDirty();
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

const FGeoDebugLayer* SGeoViewerPanel::LayerAt(int32 Index) const
{
	return VisibleLayers.IsValidIndex(Index) ? VisibleLayers[Index] : nullptr;
}

void SGeoViewerPanel::RefreshLayers()
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	LastLayerSnapshot = Snap.Get();
	LastLayerDisabled = Snap.IsValid() ? Snap->DisabledStages : ~0u;
	bLayerListBuilt = true;

	const FGeoDebugLayer* PrevLayer = LayerAt(LayerIndex);
	const FName PrevOuter = OuterIds.IsValidIndex(OuterIndex) ? OuterIds[OuterIndex] : NAME_None;

	LayerItems.Reset();
	VisibleLayers.Reset();
	OuterItems.Reset();
	OuterIds.Reset();
	OuterItems.Add(MakeShared<FString>(TEXT("外表面：同主图层")));
	OuterIds.Add(NAME_None);
	for (const FGeoDebugLayer& L : FPlanetBakePipeline::DefaultDebugLayers())
	{
		// 没烘完时不知道哪些阶段关了，先全列；烘完只留本次真正跑过的阶段。
		if (Snap.IsValid() && !Snap->HasStage(L.Stage)) { continue; }
		VisibleLayers.Add(&L);
		LayerItems.Add(MakeShared<FString>(FString::Printf(TEXT("%s · %s%s"),
			L.Stage == 0 ? TEXT("矿物") : *FString::Printf(TEXT("阶段 %u"), L.Stage), *L.DisplayName,
			L.Domain == EGeoDebugDomain::Volume ? TEXT("（地下）") : TEXT(""))));
		if (L.Domain == EGeoDebugDomain::Surface)
		{
			OuterItems.Add(MakeShared<FString>(FString::Printf(TEXT("外表面：%s"), *L.DisplayName)));
			OuterIds.Add(L.Id);
		}
	}

	LayerIndex = 0;
	if (PrevLayer)
	{
		const int32 Found = VisibleLayers.IndexOfByPredicate([PrevLayer](const FGeoDebugLayer* L) { return L == PrevLayer; });
		if (Found != INDEX_NONE) { LayerIndex = Found; }
	}
	OuterIndex = OuterIds.IndexOfByKey(PrevOuter);
	if (OuterIndex < 0) { OuterIndex = FMath::Max(0, OuterIds.IndexOfByKey(FName(TEXT("Elevation")))); }

	if (LayerCombo)
	{
		LayerCombo->RefreshOptions();
		LayerCombo->SetSelectedItem(LayerItems.IsValidIndex(LayerIndex) ? LayerItems[LayerIndex] : nullptr);
	}
	if (OuterCombo)
	{
		OuterCombo->RefreshOptions();
		OuterCombo->SetSelectedItem(OuterItems.IsValidIndex(OuterIndex) ? OuterItems[OuterIndex] : nullptr);
	}
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
	const FGeoDebugLayer* Layer = LayerAt(LayerIndex);
	if (!Snap || !Layer) { return; }
	const double T0 = FPlatformTime::Seconds();
	const int32 DisplayW = FMath::RoundToInt(View3DWidth);
	const int32 DisplayH = FMath::Max(1, FMath::RoundToInt(View3DWidth * 9.f / 16.f));
	// 出图尺寸封顶：代价 = 像素数 × 步数，面板再宽也不涨；显示按面板拉伸。
	// 开「切面显示矿体」时每根光线要做一次 20× 的 BVH 盒查询（~24 µs/根），出图再降一档。
	const bool bWantMinerals = bShowMinerals && !Layer->RayHits;
	const int32 MaxRenderW = bFull ? (bWantMinerals ? 320 : 512) : 256;
	const int32 RenderW = FMath::Max(1, FMath::Min(DisplayW, MaxRenderW));
	const int32 RenderH = FMath::Max(1, FMath::RoundToInt(float(RenderW) * 9.f / 16.f));
	PlanetGeoDebugView3D::FView V = View;
	// 切面可见时每步都要做裁剪判断，步数就是唯一的大头：交互 32 步、静止 160 步。
	V.Steps = bFull ? 64 : 24;
	// 矿体放大 20× 时每根光线要做一次巨大的 BVH 盒查询（每根 ~24 µs），只留给静止那一遍。
	V.MineralScale = (bFull && bWantMinerals) ? MineralScale : 0.f;
	TArray<FColor> Colors;
	PlanetGeoDebugView3D::Render(*Snap, *Layer, OuterLayer(), V, RenderW, RenderH, Colors, &HitPos, &HitValues);
	Upload(View3D, RenderW, RenderH, Colors, FVector2D(DisplayW, DisplayH));
	const double Ms = (FPlatformTime::Seconds() - T0) * 1000.0;
	UE_LOG(LogTemp, Display, TEXT("[查看器] 3D %dx%d 步 %d %s 用时 %.0f ms"),
		RenderW, RenderH, V.Steps, bFull ? TEXT("全质量") : TEXT("交互"), Ms);
	Status3D = FText::FromString(FString::Printf(TEXT("3D：左键拖动旋转 · 右键拖动平移 · 滚轮缩放   %d×%d，%.0f ms"),
		RenderW, RenderH, Ms));
}

void SGeoViewerPanel::Render()
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	// 烘焙结果或阶段开关变了：重列图层，下拉框里只留真实烘出来的阶段。
	const FPlanetSnapshot* SnapPtr = Snap.Get();
	const uint32 Disabled = SnapPtr ? SnapPtr->DisabledStages : ~0u;
	if (!bLayerListBuilt || SnapPtr != LastLayerSnapshot || Disabled != LastLayerDisabled)
	{
		RefreshLayers();
	}
	const FGeoDebugLayer* Layer = LayerAt(LayerIndex);
	if (!Snap || !Layer)
	{
		Status = FText::FromString(Planets.IsEmpty()
			? TEXT("当前关卡没有挂配方的星球（GeoRecipe 为空），或还没烘焙完；点「刷新」重试。")
			: Snap ? TEXT("这颗星当前没有可显示的图层：相关阶段在配方里被关闭了。")
			: TEXT("这颗星球还没烘焙完，稍后点「刷新」。"));
		RebuildLegend();
		bRendered = false;
		return;
	}
	bRendered = true;
	Status = FText::FromString(FString::Printf(TEXT("「%s」已就绪（本星烘焙出 %d 个图层）"), *Layer->DisplayName, VisibleLayers.Num()));
	RebuildLegend();
	MarkView3DDirty();
}

void SGeoViewerPanel::RebuildLegend()
{
	if (!LegendBox) { return; }
	LegendBox->ClearChildren();
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	const FGeoDebugLayer* Layer = LayerAt(LayerIndex);
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
	const FGeoDebugLayer* Layer = LayerAt(LayerIndex);
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

void SGeoViewerPanel::Export()
{
	const TSharedPtr<const FPlanetSnapshot> Snap = CurrentSnapshot();
	if (!Snap) { return; }
	const FString Name = PlanetItems.IsValidIndex(PlanetIndex) ? *PlanetItems[PlanetIndex] : FString(TEXT("Planet"));
	const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GeoDebug"), Name));
	// 导出同样只导本次烘焙存在的阶段。
	TArray<FGeoDebugLayer> Layers;
	for (const FGeoDebugLayer& L : FPlanetBakePipeline::DefaultDebugLayers())
	{
		if (Snap->HasStage(L.Stage)) { Layers.Add(L); }
	}
	const int32 N = PlanetGeoDebugRender::ExportAll(*Snap, Layers, Dir, GeoViewerMapW);
	Status = FText::FromString(FString::Printf(TEXT("导出 %d 张图到 %s"), N, *Dir));
}
#endif
