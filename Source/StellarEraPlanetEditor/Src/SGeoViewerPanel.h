// 地质查看器：选星球、选图层，同时看全球展开地图与过球心剖面；悬停读数、图例、导出 PNG。
// 只消费 FPlanetBakePipeline::DefaultDebugLayers()，新阶段注册图层后这里自动出现，不用改。
#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class AVoxelPlanetActor;
class SVerticalBox;
class STextComboBox;
class UTexture2D;
struct FPlanetSnapshot;

class SGeoViewerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGeoViewerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	struct FImageView
	{
		TStrongObjectPtr<UTexture2D> Texture;
		FSlateBrush Brush;
		TArray<float> Values;
		int32 W = 0, H = 0;
	};

	TArray<TSharedPtr<FString>> PlanetItems, LayerItems, PlaneItems;
	TArray<TWeakObjectPtr<AVoxelPlanetActor>> Planets;
	int32 PlanetIndex = 0, LayerIndex = 0, PlaneIndex = 0;
	FImageView Map, Section;
	FText Readout, Status;
	TSharedPtr<SVerticalBox> LegendBox;
	TSharedPtr<STextComboBox> PlanetCombo;

	void RefreshPlanets();
	void Render();
	void RebuildLegend();
	void Export();
	void OnMapHover(FVector2f UV);
	void OnSectionHover(FVector2f UV);
	TSharedPtr<const FPlanetSnapshot> CurrentSnapshot() const;
	static void Upload(FImageView& View, int32 W, int32 H, const TArray<FColor>& Colors, FVector2D DisplaySize);
};
