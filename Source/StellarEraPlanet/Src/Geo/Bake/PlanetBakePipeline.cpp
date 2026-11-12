#include "PlanetBakePipeline.h"
#include "HAL/PlatformTime.h"
#include "PlanetStage.h"
#include "Stage01Sphere.h"
#include "Stage02Plates.h"

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
	const IPlanetStage* Prev = nullptr;
	for (const IPlanetStage* S : Sorted())
	{
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
	if (!Validate(Report)) { return nullptr; }

	// 报告可能被复用：只认本次烘焙新增的错误。
	const int32 ErrorCount = Report.Errors.Num();
	FPlanetBakeFields Fields;
	for (const IPlanetStage* S : Sorted())
	{
		const double T0 = FPlatformTime::Seconds();
		FPlanetBakeContext Ctx{ Config, Fields, FGeoRng::Derive(Config.Seed, S->GetStageIndex(), 0), Report };
		S->Bake(Ctx);
		Report.StageSeconds.Emplace(S->GetName(), FPlatformTime::Seconds() - T0);
		if (Report.Errors.Num() > ErrorCount) { return nullptr; }
	}

	TSharedPtr<FPlanetSnapshot> Snap = MakeShared<FPlanetSnapshot>();
	Snap->R0 = Config.Radius;
	Snap->Radial = MoveTemp(Fields.Radial);
	Snap->MohoWobble = MoveTemp(Fields.MohoWobble);
	Snap->Primitives.Build(MoveTemp(Fields.Primitives));
	// 只有阶段 1 时没有高程场，地表恒为 R0；有阶段 2 就用实测上下界。
	const bool bHasElevation = Fields.Elevation.Res > 0;
	Snap->Elevation = MoveTemp(Fields.Elevation);
	Snap->CrustSplitElevation = -0.5f * Config.Plates.OceanBasinVoxels;
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
	return P;
}
