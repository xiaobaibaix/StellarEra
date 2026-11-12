#include "PlanetBakePipeline.h"
#include "HAL/PlatformTime.h"
#include "PlanetStage.h"

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

bool FPlanetBakePipeline::Validate(FPlanetBakeReport& Report) const
{
	EPlanetField Written = EPlanetField::None;
	bool bOk = true;
	for (const IPlanetStage* S : Sorted())
	{
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
	if (!Validate(Report)) { return nullptr; }

	FPlanetBakeFields Fields;
	for (const IPlanetStage* S : Sorted())
	{
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeContext Ctx{ Config, Fields, FGeoRng::Derive(Config.Seed, S->GetStageIndex(), 0), Report };
		S->Bake(Ctx);
		Report.StageSeconds.Emplace(S->GetName(), FPlatformTime::Seconds() - T0);
		if (Report.HasErrors()) { return nullptr; }
	}

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	Snap->R0 = Config.Radius;
	Snap->Radial = MoveTemp(Fields.Radial);
	Snap->MohoWobble = MoveTemp(Fields.MohoWobble);
	Snap->Primitives.Build(MoveTemp(Fields.Primitives));
	// M1 没有地形起伏：地表恒为 R0。M3 起由阶段 3 写入实测上下界。
	Snap->MinSurfaceRadius = Snap->MaxSurfaceRadius = Config.Radius;
	Snap->FinalizeHash();
	return Snap;
}

FPlanetBakePipeline FPlanetBakePipeline::MakeDefault()
{
	return FPlanetBakePipeline();
}
