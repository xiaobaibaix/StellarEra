#include "PlanetBakePipeline.h"
#include "HAL/PlatformTime.h"
#include "PlanetStage.h"
#include "GeoMineralRules.h"
#include "Stage01Sphere.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"
#include "Stage05Climate.h"
#include "Stage06Erosion.h"
#include "Stage06bExposure.h"

FPlanetBakePipeline::FPlanetBakePipeline() = default;
FPlanetBakePipeline::~FPlanetBakePipeline() = default;
FPlanetBakePipeline::FPlanetBakePipeline(FPlanetBakePipeline&&) = default;
FPlanetBakePipeline& FPlanetBakePipeline::operator=(FPlanetBakePipeline&&) = default;

void FPlanetBakePipeline::AddStage(TUniquePtr<IPlanetStage> Stage)
{
	Stages.Add(MoveTemp(Stage));
}

TArray<const IPlanetStage*> FPlanetBakePipeline::Sorted() const
{
	TArray<const IPlanetStage*> Out;
	for (const TUniquePtr<IPlanetStage>& S : Stages) { Out.Add(S.Get()); }
	Out.StableSort([](const IPlanetStage& A, const IPlanetStage& B) { return A.GetStageIndex() < B.GetStageIndex(); });
	return Out;
}

bool FPlanetBakePipeline::Validate(FPlanetBakeReport& Report, const uint32 DisabledStages) const
{
	EPlanetField Written = EPlanetField::None;
	bool bOk = true;
	const IPlanetStage* Prev = nullptr;
	for (const IPlanetStage* S : Sorted())
	{
		if (DisabledStages & (1u << (S->GetStageIndex() - 1))) { continue; }
		// 同号阶段会派生出相同的随机流，且执行先后取决于添加顺序，必须拒绝。
		if (Prev && Prev->GetStageIndex() == S->GetStageIndex())
		{
			Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 %s 与 %s 的阶段号相同（%u）"),
				*Prev->GetName().ToString(), *S->GetName().ToString(), S->GetStageIndex()));
			bOk = false;
		}
		Prev = S;
		const EPlanetField Missing = S->GetReads() & ~Written;
		if (Missing != EPlanetField::None)
		{
			Report.Errors.Add(FString::Printf(TEXT("[地质] 阶段 %s 读了尚未写入的字段 0x%x"),
				*S->GetName().ToString(), uint32(Missing)));
			bOk = false;
		}
		Written |= S->GetWrites();
	}
	return bOk;
}

TSharedPtr<const FPlanetSnapshot> FPlanetBakePipeline::Bake(const FPlanetRecipeConfig& Config, FPlanetBakeReport& Report) const
{
	if (!Validate(Report, Config.DisabledStages)) { return nullptr; }

	// 报告可能被复用：只认本次烘焙新增的错误。
	const int32 ErrorCount = Report.Errors.Num();
	for (const TSharedPtr<const IGeoMineralRule>& Rule : Config.MineralRules)
	{
		if (Rule && !Sorted().ContainsByPredicate([&Rule](const IPlanetStage* S) { return S->GetStageIndex() == Rule->GetStage(); }))
		{
			Report.Errors.Add(FString::Printf(TEXT("[地质] 撒矿器「%s」挂在阶段 %u，但管线里没有这个阶段"), Rule->GetRuleName(), Rule->GetStage()));
			return nullptr;
		}
	}
	FPlanetBakeFields Fields;
	for (const IPlanetStage* S : Sorted())
	{
		if (Config.DisabledStages & (1u << (S->GetStageIndex() - 1))) { continue; }
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeContext Ctx{ Config, Fields, FGeoRng::Derive(Config.Seed, S->GetStageIndex(), 0), Report };
		S->Bake(Ctx);
		// 本阶段的撒矿器紧跟阶段运行：读到的是这一阶段刚写完的场，后面的阶段看得到它们撒的矿。
		for (const TSharedPtr<const IGeoMineralRule>& Rule : Config.MineralRules)
		{
			if (!Rule || Rule->GetStage() != S->GetStageIndex()) { continue; }
			const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()), uint64(Rule->SeedOffset));
			FGeoScatterContext SC{ Config, Fields, FGeoRng::Derive(Config.Seed, S->GetStageIndex(), Purpose), Report.Errors };
			FGeoScatterOutput Scattered;
			Rule->Scatter(SC, Scattered);
			for (FGeoPrimitive& B : Scattered.Bodies) { B.MineralId = Rule->MineralId; Fields.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Scattered.Overlays) { O.MineralId = Rule->MineralId; Fields.MineralOverlays.Add(MoveTemp(O)); }
		}
		Report.StageSeconds.Emplace(S->GetName(), FPlatformTime::Seconds() - T0);
		if (Report.Errors.Num() > ErrorCount) { return nullptr; }
	}

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	// 先导出调试场：下面的 MoveTemp 会把 Fields 搬空。
	if (Config.bKeepDebugFields)
	{
		for (const IPlanetStage* S : Sorted())
		{
			if (Config.DisabledStages & (1u << (S->GetStageIndex() - 1))) { continue; }
			S->ExportDebugFields(Fields, Snap->DebugFields);
		}
	}
	Snap->R0 = Config.Radius;
	Snap->Radial = MoveTemp(Fields.Radial);
	Snap->MohoWobble = MoveTemp(Fields.MohoWobble);
	Snap->Primitives.Build(MoveTemp(Fields.Primitives));
	Snap->MineralOverlays = MoveTemp(Fields.MineralOverlays);
	Snap->Minerals = Config.Minerals;
	// 必须在 FinalizeHash 之前：AlterBands 进内容哈希。
	for (int32 i = 0; i < 3; ++i) { Snap->AlterBands[i] = Fields.AlterBands[i]; }
	Snap->Volcanoes = MoveTemp(Fields.Volcanoes);
	// 只有阶段 1 时没有高程场，地表恒为 R0；有阶段 2 就用实测上下界。
	const bool bHasElevation = Fields.Elevation.Res > 0;
	Snap->Elevation = MoveTemp(Fields.Elevation);
	Snap->CrustSplitElevation = Fields.CrustSplitElevation;
	if (bHasElevation)
	{
		Snap->MinSurfaceRadius = Snap->R0 + double(Fields.MinElevation);
		Snap->MaxSurfaceRadius = Snap->R0 + double(Fields.MaxElevation);
	}
	else
	{
		Snap->MinSurfaceRadius = Snap->MaxSurfaceRadius = Snap->R0;
	}
	// 上下界必须在哈希之前定好：FinalizeHash 读的是当前值，晚写会让哈希与内容不符。
	Snap->FinalizeHash();
	return Snap;
}

FPlanetBakePipeline FPlanetBakePipeline::MakeDefault()
{
	FPlanetBakePipeline P;
	P.AddStage(MakeUnique<FStage01Sphere>());
	P.AddStage(MakeUnique<FStage02Plates>());
	P.AddStage(MakeUnique<FStage03Terrain>());
	P.AddStage(MakeUnique<FStage04Magmatism>());
	P.AddStage(MakeUnique<FStage05Climate>());
	P.AddStage(MakeUnique<FStage06Erosion>());
	P.AddStage(MakeUnique<FStage06bExposure>());
	return P;
}

TArray<FGeoDebugLayer> FPlanetBakePipeline::DescribeDebugLayers() const
{
	TArray<FGeoDebugLayer> Out;
	for (const IPlanetStage* S : Sorted()) { S->DescribeDebugLayers(Out); }
	GeoMineralRules::DescribeDebugLayers(Out);
	return Out;
}

const TArray<FGeoDebugLayer>& FPlanetBakePipeline::DefaultDebugLayers()
{
	static const TArray<FGeoDebugLayer> Layers = MakeDefault().DescribeDebugLayers();
	return Layers;
}

const FGeoDebugLayer* FPlanetBakePipeline::FindDefaultDebugLayer(FName Id)
{
	if (Id.IsNone()) { return nullptr; }
	return DefaultDebugLayers().FindByPredicate([Id](const FGeoDebugLayer& L) { return L.Id == Id; });
}
