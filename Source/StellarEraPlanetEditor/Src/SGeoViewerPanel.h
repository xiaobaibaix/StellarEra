// 地质查看器：3D 视图（左键旋转、右键平移、滚轮缩放、切开、隐藏圈层、半透明），悬停读数、图例、导出 PNG。
// 只消费 FPlanetBakePipeline::DefaultDebugLayers()，新阶段注册图层后自动出现。
#pragma once
#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "PlanetGeoDebugView3D.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class AVoxelPlanetActor;
class SVerticalBox;
class STextComboBox;
class UTexture2D;
struct FGeoDebugLayer;
struct FPlanetSnapshot;

class SGeoViewerPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGeoViewerPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	struct FImageView
	{
		TStrongObjectPtr<UTexture2D> Texture;
		FSlateBrush Brush;
		TArray<float> Values;
		int32 W = 0, H = 0;
	};

	TArray<TSharedPtr<FString>> PlanetItems, LayerItems, OuterItems, ClipItems;
	TArray<FName> OuterIds;
	/** 当前星球实际烘出来的图层（按快照的阶段位掩码过滤），下标与 LayerItems 一一对应。 */
	TArray<const FGeoDebugLayer*> VisibleLayers;
	TArray<TWeakObjectPtr<AVoxelPlanetActor>> Planets;
	int32 PlanetIndex = 0, LayerIndex = 0, OuterIndex = 0;
	/** 上次重建图层列表用的快照与阶段掩码：变了才重建下拉框。 */
	const FPlanetSnapshot* LastLayerSnapshot = nullptr;
	uint32 LastLayerDisabled = 0;
	bool bLayerListBuilt = false;
	FImageView View3D;
	FText Readout, Status, Status3D;
	TSharedPtr<SVerticalBox> LegendBox;
	TSharedPtr<STextComboBox> PlanetCombo, LayerCombo, OuterCombo;

	PlanetGeoDebugView3D::FView View;
	TArray<FVector3d> HitPos;
	TArray<float> HitValues;
	/** 矿体 20× 放大要给每根光线做一次大范围 BVH 查询，静止出图 1.4 s；默认关，按需开。 */
	bool bShowMinerals = false;
	float MineralScale = 20.f;
	bool bView3DDirty = true;
	bool bView3DFull = false;
	/** 3D 图的渲染宽度跟随面板：布局每帧把可用宽度写进来。 */
	float View3DWidth = 512.f;
	/** 快照还没烘好时每秒重试一次，省得用户手点「刷新」。 */
	double LastAutoRetry = 0.0;
	bool bRendered = false;
	double LastInteraction = 0.0;

	void RefreshPlanets();
	/** 按当前快照重列图层；没烘完时先列全部。 */
	void RefreshLayers();
	const FGeoDebugLayer* LayerAt(int32 Index) const;
	void Render();
	void Render3D(bool bFull);
	void MarkView3DDirty();
	void ResetView();
	void RebuildLegend();
	void Export();
	void OnView3DHover(FVector2f UV);
	TSharedPtr<const FPlanetSnapshot> CurrentSnapshot() const;
	const FGeoDebugLayer* OuterLayer() const;
	static void Upload(FImageView& View, int32 W, int32 H, const TArray<FColor>& Colors, FVector2D DisplaySize);
};
