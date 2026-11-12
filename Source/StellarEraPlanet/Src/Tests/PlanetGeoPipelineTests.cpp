// 「后阶段只读前阶段」靠这里机械检查：读了没人写的字段必须在烘焙前报错，否则下游静默拿到空场。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "PlanetBakePipeline.h"
#include "PlanetStage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	class FFakeStage final : public IPlanetStage
	{
	public:
		FFakeStage(FName InName, uint32 InIndex, EPlanetField InReads, EPlanetField InWrites)
			: Name(InName), Index(InIndex), Reads(InReads), Writes(InWrites) {}
		virtual FName GetName() const override { return Name; }
		virtual uint32 GetStageIndex() const override { return Index; }
		virtual EPlanetField GetReads() const override { return Reads; }
		virtual EPlanetField GetWrites() const override { return Writes; }
		virtual void Bake(FPlanetBakeContext& Ctx) const override
		{
			if (EnumHasAnyFlags(Writes, EPlanetField::Radial)) { Ctx.Fields.Radial.Layers = { { 1.f, 1, 0.f } }; }
		}
	private:
		FName Name; uint32 Index; EPlanetField Reads, Writes;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPipelineTest, "StellarEra.Planet.Geo.Pipeline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPipelineTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig Config;
	Config.Radius = 1000.0;
	Config.FaceRes = 16;

	FPlanetBakePipeline Bad;
	Bad.AddStage(MakeUnique<FFakeStage>(TEXT("Reader"), 1, EPlanetField::Radial, EPlanetField::None));
	FPlanetBakeReport BadReport;
	TestFalse(TEXT("读了没人写的字段 → 校验失败"), Bad.Validate(BadReport));
	TestTrue(TEXT("校验失败写进报告"), BadReport.HasErrors());
	FPlanetBakeReport BakeBadReport;
	TestFalse(TEXT("校验失败时拒绝烘焙"), Bad.Bake(Config, BakeBadReport).IsValid());

	FPlanetBakePipeline Unordered;
	Unordered.AddStage(MakeUnique<FFakeStage>(TEXT("Reader"), 2, EPlanetField::Radial, EPlanetField::None));
	Unordered.AddStage(MakeUnique<FFakeStage>(TEXT("Writer"), 1, EPlanetField::None, EPlanetField::Radial));
	FPlanetBakeReport Report;
	TestTrue(TEXT("按阶段号排序，不按添加顺序"), Unordered.Validate(Report));
	const TSharedPtr<const FPlanetSnapshot> Snap = Unordered.Bake(Config, Report);
	TestTrue(TEXT("产出快照"), Snap.IsValid());
	if (Snap)
	{
		TestEqual(TEXT("快照 R0 = 配方半径"), Snap->R0, 1000.0);
		TestEqual(TEXT("阶段写的剖面进了快照"), Snap->Radial.Layers.Num(), 1);
		TestTrue(TEXT("快照有内容哈希"), Snap->ContentHash != 0);
	}
	TestEqual(TEXT("每阶段都有计时"), Report.StageSeconds.Num(), 2);

	FPlanetRecipeConfig Other = Config;
	Other.Seed = 1338;
	TestTrue(TEXT("改种子配方哈希变"), Config.Hash() != Other.Hash());
	return true;
}

#endif
