// 阶段 4c-2：碳酸岩与宿主型岩浆矿床（锡钨 / 伟晶岩 / 铜镍 / 铬铂 / 金刚石 / 稀土）。跑真实 2→3→4 烘焙。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoMineralCatalog.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
#include "GeoPrimitiveIndex.h"
#include "GeoRng.h"
#include "PlanetBakePipeline.h"
#include "PlanetRecipeConfig.h"
#include "Stage02Plates.h"
#include "Stage03Terrain.h"
#include "Stage04Magmatism.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// 跑阶段 2→3→4（阶段 4 内含 4b 放置）再按管线的方式撒矿：测试要拿到撒矿后的场才验得了矿体位置。
	void Stage04c2TestBake(const FPlanetRecipeConfig& C, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		FPlanetBakeContext C2{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R }; FStage02Plates().Bake(C2);
		FPlanetBakeContext C3{ C, F, FGeoRng::Derive(C.Seed, 3, 0), R }; FStage03Terrain().Bake(C3);
		FPlanetBakeContext C4{ C, F, FGeoRng::Derive(C.Seed, 4, 0), R }; FStage04Magmatism().Bake(C4);
		for (const TSharedPtr<const IGeoMineralRule>& Rule : C.MineralRules)
		{
			if (!Rule) { continue; }
			// 随机流与 FPlanetBakePipeline::Bake 逐位一致，测试量的就是生产撒出来的矿体。
			const uint64 Purpose = GeoHash::Combine(GeoHash::Combine(GeoHash::Str("scatter"), Rule->HashParams()), uint64(Rule->SeedOffset));
			FGeoScatterContext SC{ C, F, FGeoRng::Derive(C.Seed, Rule->GetStage(), Purpose), R.Errors };
			FGeoScatterOutput Out;
			Rule->Scatter(SC, Out);
			for (FGeoPrimitive& B : Out.Bodies) { B.MineralId = Rule->MineralId; F.Primitives.Add(B); }
			for (FGeoMineralOverlay& O : Out.Overlays) { O.MineralId = Rule->MineralId; F.MineralOverlays.Add(MoveTemp(O)); }
		}
	}
	int32 Stage04c2LithoCount(const TArray<FGeoPrimitive>& P, uint8 Litho)
	{
		int32 N = 0; for (const FGeoPrimitive& Q : P) { N += Q.LithologyId == Litho ? 1 : 0; } return N;
	}
	int32 Stage04c2BodyCount(const TArray<FGeoPrimitive>& P, uint8 MineralId)
	{
		int32 N = 0; for (const FGeoPrimitive& Q : P) { N += Q.MineralId == MineralId ? 1 : 0; } return N;
	}
	/** 宿主局部空间：世界点先绕宿主旋转回局部，再除半轴。与实现的辅助独立重写，形成交叉验证。 */
	FVector3f Stage04c2Local(const FGeoPrimitive& Host, const FVector3f& P)
	{
		const FVector3f D = P - Host.Center;
		const FVector3f L = Host.Kind == EGeoPrimitiveKind::OrientedBlob ? Host.Rotation.UnrotateVector(D) : D;
		return L / Host.Radii;
	}
	/** 按世界心距取最近宿主；宿主为空时返回 nullptr（= 这一体没有宿主）。 */
	const FGeoPrimitive* Stage04c2NearestHost(const TArray<const FGeoPrimitive*>& Hosts, const FVector3f& Center)
	{
		const FGeoPrimitive* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (const FGeoPrimitive* H : Hosts)
		{
			const float D = (H->Center - Center).SizeSquared();
			if (D < BestD) { BestD = D; Best = H; }
		}
		return Best;
	}
	/** 拆成「宿主」与「某种矿体」两列表：宿主只认该岩性且未撒矿（MineralId == 0）的原语。 */
	void Stage04c2Split(const TArray<FGeoPrimitive>& P, uint8 MineralId, uint8 HostLitho, TArray<const FGeoPrimitive*>& Hosts, TArray<const FGeoPrimitive*>& Bodies)
	{
		for (const FGeoPrimitive& Q : P)
		{
			if (Q.MineralId == MineralId) { Bodies.Add(&Q); }
			else if (Q.LithologyId == HostLitho && Q.MineralId == 0) { Hosts.Add(&Q); }
		}
	}
	// TArray<FGeoPrimitive> 没有 Hash：借快照索引的哈希，它逐字段混入（含 Center/Radii/Rotation）。
	uint64 Stage04c2PrimHash(const TArray<FGeoPrimitive>& P)
	{
		FGeoPrimitiveIndex I; I.Build(P); return I.Hash();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2CarbonatiteTest, "StellarEra.Planet.Geo.Stage04c2.Carbonatite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2CarbonatiteTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("岩性表多出碳酸岩一位"), int32(GeoLithology::Count), 16);
	TestEqual(TEXT("碳酸岩显示名"), FString(GeoLithology::DisplayName(GeoLithology::Carbonatite)), FString(TEXT("碳酸岩")));
	TestEqual(TEXT("碳酸岩调试色"), uint32(GeoLithology::DebugColor(GeoLithology::Carbonatite).ToPackedARGB()),
		uint32(FColor(200, 180, 210).ToPackedARGB()));

	FPlanetRecipeConfig C; C.FaceRes = 512;
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(TEXT("无错误"), R.HasErrors());

	const int32 NC = Stage04c2LithoCount(F.Primitives, GeoLithology::Carbonatite);
	AddInfo(FString::Printf(TEXT("碳酸岩 %d 个"), NC));
	TestTrue(TEXT("有碳酸岩"), NC > 0);

	// 放置只从裂谷 texel 出发：中心方向最近 texel 必须仍是 6（大陆裂谷），且陆上。
	int32 BadRift = 0, BadElev = 0, NotBlob = 0;
	for (const FGeoPrimitive& P : F.Primitives)
	{
		if (P.LithologyId != GeoLithology::Carbonatite) { continue; }
		const FVector3f Dir = P.Center.GetSafeNormal();
		BadRift += F.ReliefType.SampleNearest(Dir) == 6.f ? 0 : 1;
		BadElev += F.Elevation.Sample(Dir) > 0.f ? 0 : 1;
		NotBlob += P.Kind == EGeoPrimitiveKind::Blob ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("越界：裂谷 %d / 海面 %d / 非 Blob %d"), BadRift, BadElev, NotBlob));
	TestEqual(TEXT("碳酸岩都在大陆裂谷（ReliefType==6）"), BadRift, 0);
	TestEqual(TEXT("碳酸岩都在海平面以上"), BadElev, 0);
	TestEqual(TEXT("碳酸岩都是 Blob"), NotBlob, 0);

	// 尺度不变：半径减半后数量不变（候选按方向去重，与 R 无关）。
	FPlanetRecipeConfig Half = C; Half.Radius = C.Radius * 0.5;
	FPlanetBakeFields Fh; FPlanetBakeReport Rh;
	Stage04c2TestBake(Half, Fh, Rh);
	TestEqual(TEXT("半径减半碳酸岩数量不变"), Stage04c2LithoCount(Fh.Primitives, GeoLithology::Carbonatite), NC);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2TinTungstenTest, "StellarEra.Planet.Geo.Stage04c2.TinTungsten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2TinTungstenTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoTinTungstenRule> Rule = MakeShared<FGeoTinTungstenRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::Granite, Hosts, Bodies);
	if (!TestTrue(TEXT("有花岗岩岩基"), Hosts.Num() > 0)) { return false; }

	const int32 Expect = Hosts.Num() * Rule->P.PerBatholith;
	AddInfo(FString::Printf(TEXT("岩基 %d 座，锡钨脉 %d 条（期望 %d）"), Hosts.Num(), Bodies.Num(), Expect));
	TestTrue(TEXT("数量 = 岩基数 × PerBatholith（±5%）"),
		FMath::Abs(Bodies.Num() - Expect) <= FMath::Max(1, FMath::CeilToInt(0.05f * float(Expect))));

	const float Rx = float(C.Radius);
	const FVector3f Want(Rule->P.LengthRatio * Rx, Rule->P.RadiusRatio * Rx, Rule->P.ThicknessRatio * Rx);
	int32 NoHost = 0, BadZ = 0, BadXY = 0, BadKind = 0, BadShape = 0, BadPose = 0, BadNorm = 0;
	float ZLo = TNumericLimits<float>::Max(), ZHi = -ZLo, WorstXY = 0.f, WorstNorm = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++NoHost; continue; }
		const FVector3f L = Stage04c2Local(*Host, Body->Center);
		const float AZ = FMath::Abs(L.Z);
		const float AXY = FMath::Max(FMath::Abs(L.X), FMath::Abs(L.Y));
		const float Norm = L.Size();
		ZLo = FMath::Min(ZLo, AZ); ZHi = FMath::Max(ZHi, AZ); WorstXY = FMath::Max(WorstXY, AXY);
		WorstNorm = FMath::Max(WorstNorm, Norm);
		BadZ += AZ >= 0.5f && AZ <= 1.02f ? 0 : 1;
		BadXY += AXY <= 0.6f ? 0 : 1;
		// 宿主局部归一化距离 ≤ 1：角落的脉若不按椭圆截面收回，u,v,w 取满可到 1.18。
		BadNorm += Norm <= 1.f ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::OrientedBlob ? 0 : 1;
		// 局部 X 长、Z 厚的细脉：三条半轴必须各按自己的比例，别把长轴与厚度装反。
		BadShape += Body->Radii.X > Body->Radii.Y && Body->Radii.Y > Body->Radii.Z
			&& FMath::IsNearlyEqual(Body->Radii.X, Want.X, 1e-4f * Want.X)
			&& FMath::IsNearlyEqual(Body->Radii.Y, Want.Y, 1e-4f * Want.Y)
			&& FMath::IsNearlyEqual(Body->Radii.Z, Want.Z, 1e-4f * Want.Z) ? 0 : 1;
		// 与宿主同朝向：脉沿岩基长轴拉开靠的就是这条（四元数符号无关，比旋转后的轴）。
		const FVector3f BX = Body->Rotation.RotateVector(FVector3f::XAxisVector), HX = Host->Rotation.RotateVector(FVector3f::XAxisVector);
		const FVector3f BZ = Body->Rotation.RotateVector(FVector3f::ZAxisVector), HZ = Host->Rotation.RotateVector(FVector3f::ZAxisVector);
		BadPose += FVector3f::DotProduct(BX, HX) >= 0.999f && FVector3f::DotProduct(BZ, HZ) >= 0.999f ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("局部 |z| %.3f–%.3f，最差 |x|/|y| %.3f，最差局部距离 %.3f；越界 z %d / xy %d / 距离 %d / 非 OrientedBlob %d / 尺寸 %d / 朝向 %d"),
		ZLo, ZHi, WorstXY, WorstNorm, BadZ, BadXY, BadNorm, BadKind, BadShape, BadPose));
	TestEqual(TEXT("没有无宿主的矿体"), NoHost, 0);
	TestEqual(TEXT("局部 |z| 都在 [0.5, 1.02]（岩基顶面壳层）"), BadZ, 0);
	TestEqual(TEXT("局部 |x|,|y| 都 ≤ 0.6"), BadXY, 0);
	TestEqual(FString::Printf(TEXT("脉心到最近岩基的局部归一化距离 ≤ 1（实测最大 %.3f）"), WorstNorm), BadNorm, 0);
	TestEqual(TEXT("都是 OrientedBlob（能沿长轴拉伸）"), BadKind, 0);
	TestEqual(TEXT("半轴 = 比例 × R，X 最长 Z 最薄"), BadShape, 0);
	TestEqual(TEXT("朝向与宿主一致"), BadPose, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2PegmatiteTest, "StellarEra.Planet.Geo.Stage04c2.Pegmatite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2PegmatiteTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoPegmatiteRule> Rule = MakeShared<FGeoPegmatiteRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::Granite, Hosts, Bodies);
	if (!TestTrue(TEXT("有花岗岩岩基"), Hosts.Num() > 0)) { return false; }

	const int32 Expect = Hosts.Num() * Rule->P.PerBatholith;
	AddInfo(FString::Printf(TEXT("岩基 %d 座，伟晶岩 %d 个（期望 %d）"), Hosts.Num(), Bodies.Num(), Expect));
	TestTrue(TEXT("数量 = 岩基数 × PerBatholith（±5%）"),
		FMath::Abs(Bodies.Num() - Expect) <= FMath::Max(1, FMath::CeilToInt(0.05f * float(Expect))));

	const float WantRadius = Rule->P.RadiusRatio * float(C.Radius);
	int32 NoHost = 0, BadShell = 0, BadKind = 0, BadRadius = 0, Below = 0, Above = 0;
	float SLo = TNumericLimits<float>::Max(), SHi = -SLo;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++NoHost; continue; }
		const FVector3f L = Stage04c2Local(*Host, Body->Center);
		const float S = L.Size();
		SLo = FMath::Min(SLo, S); SHi = FMath::Max(SHi, S);
		BadShell += S >= 0.8f && S <= 1.05f ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::Blob ? 0 : 1;
		BadRadius += FMath::IsNearlyEqual(Body->Radii.X, WantRadius, 1e-4f * WantRadius)
			&& FMath::IsNearlyEqual(Body->Radii.Y, WantRadius, 1e-4f * WantRadius)
			&& FMath::IsNearlyEqual(Body->Radii.Z, WantRadius, 1e-4f * WantRadius) ? 0 : 1;
		// 方向在宿主局部球面上随机：两半球都该有，否则就是被钉在某条固定轴上。
		Below += L.Z < -0.2f ? 1 : 0;
		Above += L.Z > 0.2f ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("局部半径 %.3f–%.3f，局部 z < -0.2 的 %d 个 / > 0.2 的 %d 个；越界 %d / 非 Blob %d / 半径 %d"),
		SLo, SHi, Below, Above, BadShell, BadKind, BadRadius));
	TestEqual(TEXT("没有无宿主的矿体"), NoHost, 0);
	TestEqual(TEXT("局部半径在 [0.8, 1.05]（岩基边缘壳层）"), BadShell, 0);
	TestEqual(TEXT("都是 Blob"), BadKind, 0);
	TestEqual(TEXT("半径 = RadiusRatio × R（各向同性）"), BadRadius, 0);
	TestTrue(TEXT("方向铺满两半球（不是钉在固定轴上）"), Below > 0 && Above > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2CopperNickelTest, "StellarEra.Planet.Geo.Stage04c2.CopperNickel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2CopperNickelTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoCopperNickelRule> Rule = MakeShared<FGeoCopperNickelRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::Gabbro, Hosts, Bodies);
	if (!TestTrue(TEXT("有辉长岩宿主（岩床 / 岩墙）"), Hosts.Num() > 0)) { return false; }

	const int32 Expect = Hosts.Num() * Rule->P.PerHost;
	AddInfo(FString::Printf(TEXT("辉长岩宿主 %d 个，铜镍矿体 %d 个（期望 %d）"), Hosts.Num(), Bodies.Num(), Expect));
	TestTrue(TEXT("数量 = 宿主数 × PerHost（±5%）"),
		FMath::Abs(Bodies.Num() - Expect) <= FMath::Max(1, FMath::CeilToInt(0.05f * float(Expect))));

	int32 NoHost = 0, BadZ = 0, BadXY = 0, BadBottom = 0, BadTop = 0, BadKind = 0, BadShape = 0, BadPose = 0;
	float ZLo = TNumericLimits<float>::Max(), ZHi = -ZLo, WorstXY = 0.f;
	float BottomLo = TNumericLimits<float>::Max(), TopHi = -TNumericLimits<float>::Max();
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++NoHost; continue; }
		const FVector3f L = Stage04c2Local(*Host, Body->Center);
		const float AXY = FMath::Max(FMath::Abs(L.X), FMath::Abs(L.Y));
		ZLo = FMath::Min(ZLo, L.Z); ZHi = FMath::Max(ZHi, L.Z); WorstXY = FMath::Max(WorstXY, AXY);
		BadZ += L.Z >= -1.02f && L.Z <= -0.4f ? 0 : 1;
		BadXY += AXY <= 1.f ? 0 : 1;
		// 竖直跨度按宿主局部 Z 算：朝向断言已证明矿体与宿主同朝向，直接除宿主 Rz 即可。
		const float HalfZ = Body->Radii.Z / Host->Radii.Z;
		const float Bottom = L.Z - HalfZ, Top = L.Z + HalfZ;
		BottomLo = FMath::Min(BottomLo, Bottom); TopHi = FMath::Max(TopHi, Top);
		BadBottom += Bottom >= -1.02f ? 0 : 1;
		BadTop += Top <= -0.2f + 1e-4f ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::OrientedBlob ? 0 : 1;
		// 半轴按宿主自己的三条半轴缩放：岩墙（竖直薄板）与岩床（水平薄板）都靠这条跟着宿主变扁。
		const FVector3f Want(Host->Radii.X * Rule->P.WidthScale, Host->Radii.Y * Rule->P.WidthScale, Host->Radii.Z * Rule->P.ThicknessScale);
		BadShape += FMath::IsNearlyEqual(Body->Radii.X, Want.X, 1e-4f * Want.X)
			&& FMath::IsNearlyEqual(Body->Radii.Y, Want.Y, 1e-4f * Want.Y)
			&& FMath::IsNearlyEqual(Body->Radii.Z, Want.Z, 1e-4f * Want.Z) ? 0 : 1;
		const FVector3f BX = Body->Rotation.RotateVector(FVector3f::XAxisVector), HX = Host->Rotation.RotateVector(FVector3f::XAxisVector);
		const FVector3f BZ = Body->Rotation.RotateVector(FVector3f::ZAxisVector), HZ = Host->Rotation.RotateVector(FVector3f::ZAxisVector);
		BadPose += FVector3f::DotProduct(BX, HX) >= 0.999f && FVector3f::DotProduct(BZ, HZ) >= 0.999f ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("中心局部 z %.3f–%.3f，最差 |x|/|y| %.3f；竖直跨度 底 %.3f / 顶 %.3f；越界 z %d / xy %d / 底 %d / 顶 %d / 非 OrientedBlob %d / 尺寸 %d / 朝向 %d"),
		ZLo, ZHi, WorstXY, BottomLo, TopHi, BadZ, BadXY, BadBottom, BadTop, BadKind, BadShape, BadPose));
	TestEqual(TEXT("没有无宿主的矿体"), NoHost, 0);
	TestEqual(TEXT("中心在 [-1.02, -0.4]（宿主底部壳层）"), BadZ, 0);
	TestEqual(TEXT("中心 |x|,|y| ≤ 1（不出宿主横截面）"), BadXY, 0);
	TestEqual(TEXT("矿体底不越出宿主底（≥ -1.02）"), BadBottom, 0);
	TestEqual(TEXT("矿体顶在宿主中线以下（≤ -0.2）"), BadTop, 0);
	TestEqual(TEXT("都是 OrientedBlob（能贴合宿主朝向）"), BadKind, 0);
	TestEqual(TEXT("半轴 = 宿主半轴 × WidthScale / ThicknessScale"), BadShape, 0);
	TestEqual(TEXT("朝向与宿主一致"), BadPose, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2ChromiumPGETest, "StellarEra.Planet.Geo.Stage04c2.ChromiumPGE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2ChromiumPGETest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoChromiumPGERule> Rule = MakeShared<FGeoChromiumPGERule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::LayeredMafic, Hosts, Bodies);
	if (!TestTrue(TEXT("有层状镁铁岩薄板"), Hosts.Num() > 0)) { return false; }

	const int32 Expect = Hosts.Num() * Rule->P.PerSlab;
	AddInfo(FString::Printf(TEXT("层状镁铁岩薄板 %d 片，铬铂层 %d 个（期望 %d）"), Hosts.Num(), Bodies.Num(), Expect));
	TestTrue(TEXT("数量 = 薄板数 × PerSlab（±5%）"),
		FMath::Abs(Bodies.Num() - Expect) <= FMath::Max(1, FMath::CeilToInt(0.05f * float(Expect))));

	const float MaxChord = 0.05f * float(C.Radius);
	int32 NoHost = 0, BadChord = 0, BadPose = 0, BadThin = 0, BadKind = 0, BadShape = 0;
	float WorstChord = 0.f, WorstDot = 1.f, WorstThin = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++NoHost; continue; }
		const float Chord = (Host->Center - Body->Center).Size();
		const FVector3f BZ = Body->Rotation.RotateVector(FVector3f::ZAxisVector), HZ = Host->Rotation.RotateVector(FVector3f::ZAxisVector);
		const float Dot = FVector3f::DotProduct(BZ, HZ);
		WorstChord = FMath::Max(WorstChord, Chord); WorstDot = FMath::Min(WorstDot, Dot);
		WorstThin = FMath::Max(WorstThin, Body->Radii.Z / Host->Radii.Z);
		BadChord += Chord <= MaxChord ? 0 : 1;
		BadPose += Dot >= 0.99f ? 0 : 1;
		BadThin += Body->Radii.Z <= 0.5f * Host->Radii.Z ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::OrientedBlob ? 0 : 1;
		const FVector3f Want(Host->Radii.X, Host->Radii.Y, Host->Radii.Z * Rule->P.ThicknessScale);
		BadShape += FMath::IsNearlyEqual(Body->Radii.X, Want.X, 1e-4f * Want.X)
			&& FMath::IsNearlyEqual(Body->Radii.Y, Want.Y, 1e-4f * Want.Y)
			&& FMath::IsNearlyEqual(Body->Radii.Z, Want.Z, 1e-4f * Want.Z) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("中心距最差 %.1f（上限 %.1f），Z 轴点积最差 %.4f，半厚比最差 %.2f；越界 中心 %d / 朝向 %d / 厚度 %d / 非 OrientedBlob %d / 尺寸 %d"),
		WorstChord, MaxChord, WorstDot, WorstThin, BadChord, BadPose, BadThin, BadKind, BadShape));
	TestEqual(TEXT("没有无宿主的矿体"), NoHost, 0);
	TestEqual(TEXT("与宿主同中心（≤ 0.05R）"), BadChord, 0);
	TestEqual(TEXT("朝向与宿主一致（Z 轴点积 ≥ 0.99）"), BadPose, 0);
	TestEqual(TEXT("半厚 ≤ 0.5 × 宿主 Rz（夹在薄板里）"), BadThin, 0);
	TestEqual(TEXT("都是 OrientedBlob"), BadKind, 0);
	TestEqual(TEXT("半轴 = (Rx, Ry, Rz × ThicknessScale)"), BadShape, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2DiamondTest, "StellarEra.Planet.Geo.Stage04c2.Diamond",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2DiamondTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoDiamondRule> Rule = MakeShared<FGeoDiamondRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::Kimberlite, Hosts, Bodies);
	if (!TestTrue(TEXT("有金伯利岩管"), Hosts.Num() > 0)) { return false; }

	const float WantRadius = Rule->P.RadiusRatio * float(C.Radius);
	TSet<const FGeoPrimitive*> FertilePipes;
	int32 BadLocal = 0, BadKind = 0, BadRadii = 0;
	float WorstLocal = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++BadLocal; continue; }
		FertilePipes.Add(Host);
		// 宿主局部空间里的中心距：> 1.05 就是长到管外了，也算「这一颗没有宿主」。
		const float Local = Stage04c2Local(*Host, Body->Center).Size();
		WorstLocal = FMath::Max(WorstLocal, Local);
		BadLocal += Local <= 1.05f ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::Blob ? 0 : 1;
		BadRadii += FMath::IsNearlyEqual(Body->Radii.X, WantRadius, 1e-4f * WantRadius)
			&& FMath::IsNearlyEqual(Body->Radii.Y, WantRadius, 1e-4f * WantRadius)
			&& FMath::IsNearlyEqual(Body->Radii.Z, WantRadius, 1e-4f * WantRadius) ? 0 : 1;
	}
	const float FertileRate = Hosts.Num() > 0 ? float(FertilePipes.Num()) / float(Hosts.Num()) : 0.f;
	AddInfo(FString::Printf(TEXT("金伯利岩管 %d 根，含矿 %d 根，金刚石 %d 颗；含矿率 %.3f，最差局部半径 %.3f；越界 %d / 非 Blob %d / 半径 %d"),
		Hosts.Num(), FertilePipes.Num(), Bodies.Num(), FertileRate, WorstLocal, BadLocal, BadKind, BadRadii));
	TestTrue(TEXT("有金刚石矿体"), Bodies.Num() > 0);
	TestEqual(TEXT("数量 = 含矿管数 × PerPipe"), Bodies.Num(), FertilePipes.Num() * Rule->P.PerPipe);
	// 管数少时下限没统计意义：p=0.15 在 8 根管里约四成概率一颗都不含矿，spec §5.3 允许放宽。
	if (Hosts.Num() < 20)
	{
		TestTrue(FString::Printf(TEXT("含矿 %d / 总管 %d ≤ 0.3"), FertilePipes.Num(), Hosts.Num()),
			float(FertilePipes.Num()) <= 0.3f * float(Hosts.Num()));
	}
	else
	{
		TestTrue(FString::Printf(TEXT("含矿率 %.3f 在 [0.05, 0.3]（总管 %d）"), FertileRate, Hosts.Num()),
			FertileRate >= 0.05f && FertileRate <= 0.3f);
	}
	TestEqual(TEXT("没有无宿主的矿体（管内局部半径 ≤ 1.05）"), BadLocal, 0);
	TestEqual(TEXT("都是 Blob"), BadKind, 0);
	TestEqual(TEXT("半径 = RadiusRatio × R（各向同性）"), BadRadii, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2RareEarthTest, "StellarEra.Planet.Geo.Stage04c2.RareEarth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2RareEarthTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	TSharedPtr<FGeoRareEarthRule> Rule = MakeShared<FGeoRareEarthRule>();
	Rule->MineralId = 1;
	C.MineralRules = { Rule };
	FPlanetBakeFields F; FPlanetBakeReport R;
	Stage04c2TestBake(C, F, R);
	TestFalse(FString::Printf(TEXT("无错误（%s）"), *FString::Join(R.Errors, TEXT(" | "))), R.HasErrors());

	TArray<const FGeoPrimitive*> Hosts, Bodies;
	Stage04c2Split(F.Primitives, Rule->MineralId, GeoLithology::Carbonatite, Hosts, Bodies);
	if (!TestTrue(TEXT("有碳酸岩体"), Hosts.Num() > 0)) { return false; }

	const int32 Expect = Hosts.Num() * Rule->P.PerBody;
	AddInfo(FString::Printf(TEXT("碳酸岩 %d 个，稀土矿体 %d 个（期望 %d）"), Hosts.Num(), Bodies.Num(), Expect));
	TestTrue(TEXT("数量 = 碳酸岩数 × PerBody（±5%）"),
		FMath::Abs(Bodies.Num() - Expect) <= FMath::Max(1, FMath::CeilToInt(0.05f * float(Expect))));

	int32 BadLocal = 0, BadKind = 0, BadScale = 0;
	float WorstLocal = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++BadLocal; continue; }
		const float Local = Stage04c2Local(*Host, Body->Center).Size();
		WorstLocal = FMath::Max(WorstLocal, Local);
		BadLocal += Local <= 1.05f ? 0 : 1;
		BadKind += Body->Kind == EGeoPrimitiveKind::Blob ? 0 : 1;
		// 半径按宿主三条半轴等比缩小：碳酸岩是各向同性球，改形状也不会再把矿体戳出岩体。
		const FVector3f Want = Host->Radii * Rule->P.RadiusScale;
		BadScale += FMath::IsNearlyEqual(Body->Radii.X, Want.X, 1e-4f * Want.X)
			&& FMath::IsNearlyEqual(Body->Radii.Y, Want.Y, 1e-4f * Want.Y)
			&& FMath::IsNearlyEqual(Body->Radii.Z, Want.Z, 1e-4f * Want.Z) ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("最差局部半径 %.3f；越界 %d / 非 Blob %d / 缩放 %d"), WorstLocal, BadLocal, BadKind, BadScale));
	TestEqual(TEXT("没有无宿主的矿体（碳酸岩内局部半径 ≤ 1.05）"), BadLocal, 0);
	TestEqual(TEXT("都是 Blob"), BadKind, 0);
	TestEqual(TEXT("半径 = RadiusScale × 宿主半径"), BadScale, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2SwitchesTest, "StellarEra.Planet.Geo.Stage04c2.Switches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2SwitchesTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C; C.FaceRes = 512;
	// 确定性对拍要强制单线程：字段用途见 FPlanetRecipeConfig::bSingleThreadBake。
	C.bSingleThreadBake = true;
	TSharedPtr<FGeoTinTungstenRule> TinTungsten = MakeShared<FGeoTinTungstenRule>(); TinTungsten->MineralId = 1;
	TSharedPtr<FGeoPegmatiteRule> Pegmatite = MakeShared<FGeoPegmatiteRule>(); Pegmatite->MineralId = 2;
	TSharedPtr<FGeoCopperNickelRule> CopperNickel = MakeShared<FGeoCopperNickelRule>(); CopperNickel->MineralId = 3;
	TSharedPtr<FGeoChromiumPGERule> ChromiumPGE = MakeShared<FGeoChromiumPGERule>(); ChromiumPGE->MineralId = 4;
	TSharedPtr<FGeoDiamondRule> Diamond = MakeShared<FGeoDiamondRule>(); Diamond->MineralId = 5;
	TSharedPtr<FGeoRareEarthRule> RareEarth = MakeShared<FGeoRareEarthRule>(); RareEarth->MineralId = 6;

	const TArray<FString> Names = { TEXT("锡钨"), TEXT("伟晶岩"), TEXT("铜镍"), TEXT("铬铂"), TEXT("金刚石"), TEXT("稀土") };
	const TArray<uint8> Ids = { 1, 2, 3, 4, 5, 6 };
	const TArray<TSharedPtr<const IGeoMineralRule>> Rules = { TinTungsten, Pegmatite, CopperNickel, ChromiumPGE, Diamond, RareEarth };

	// Omit < 0 = 全开；否则只去掉这一条，其余照旧。
	auto Bake = [&](int32 Omit, FPlanetBakeFields& F, FPlanetBakeReport& R)
	{
		C.MineralRules.Empty();
		for (int32 i = 0; i < Rules.Num(); ++i) { if (i != Omit) { C.MineralRules.Add(Rules[i]); } }
		Stage04c2TestBake(C, F, R);
	};

	FPlanetBakeFields Full; FPlanetBakeReport RF;
	Bake(INDEX_NONE, Full, RF);
	TestFalse(FString::Printf(TEXT("全开无错误（%s）"), *FString::Join(RF.Errors, TEXT(" | "))), RF.HasErrors());
	TArray<int32> FullCounts;
	for (int32 i = 0; i < Rules.Num(); ++i)
	{
		FullCounts.Add(Stage04c2BodyCount(Full.Primitives, Ids[i]));
		TestTrue(FString::Printf(TEXT("全开有%s"), *Names[i]), FullCounts[i] > 0);
	}
	// 每条规则各用独立随机流（种子由 HashParams 派生），去掉一条后其余五种的矿体数必须逐位不变。
	for (int32 Omit = 0; Omit < Rules.Num(); ++Omit)
	{
		FPlanetBakeFields F; FPlanetBakeReport R;
		Bake(Omit, F, R);
		TestEqual(FString::Printf(TEXT("去掉%s规则后%s为 0"), *Names[Omit], *Names[Omit]),
			Stage04c2BodyCount(F.Primitives, Ids[Omit]), 0);
		for (int32 i = 0; i < Rules.Num(); ++i)
		{
			if (i == Omit) { continue; }
			TestEqual(FString::Printf(TEXT("去掉%s不影响%s"), *Names[Omit], *Names[i]),
				Stage04c2BodyCount(F.Primitives, Ids[i]), FullCounts[i]);
		}
	}
	// 确定性：同配方两次烘焙，原语哈希一致。
	{
		FPlanetBakeFields F2; FPlanetBakeReport R2;
		Bake(INDEX_NONE, F2, R2);
		TestEqual(TEXT("两次烘焙原语哈希一致"), int64(Stage04c2PrimHash(Full.Primitives)), int64(Stage04c2PrimHash(F2.Primitives)));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2CatalogTest, "StellarEra.Planet.Geo.Stage04c2.Catalog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2CatalogTest::RunTest(const FString& Parameters)
{
	// 全部 13 种矿：撒矿器只按名字引用目录，编号 = 下标 + 1（PlanetRecipe.cpp ToConfig）。
	const TCHAR* Ids[] = { TEXT("BIF"), TEXT("VMS"), TEXT("MnNodule"), TEXT("Chromite"), TEXT("Sulfur"),
		TEXT("EpithermalAuAg"), TEXT("PorphyryCu"), TEXT("TinTungsten"), TEXT("PegmatiteLi"),
		TEXT("CopperNickel"), TEXT("ChromiumPGE"), TEXT("Diamond"), TEXT("RareEarth") };
	UGeoMineralCatalog* Cat = NewObject<UGeoMineralCatalog>();
	for (const TCHAR* Id : Ids)
	{
		FGeoMineralDef D;
		D.Id = FName(Id);
		D.DisplayName = Id;
		Cat->Minerals.Add(D);
	}
	TestEqual(TEXT("目录 13 个矿种"), Cat->Minerals.Num(), 13);

	TSet<uint8> Seen;
	for (const TCHAR* Id : Ids)
	{
		const int32 Index = Cat->FindIndex(FName(Id));
		const uint8 MineralId = Index == INDEX_NONE ? 0 : uint8(Index + 1);
		TestTrue(FString::Printf(TEXT("「%s」能解析出非零矿物编号"), Id), MineralId != 0);
		TestTrue(FString::Printf(TEXT("「%s」的编号落在调色板范围内"), Id), Cat->Minerals.IsValidIndex(Index));
		if (MineralId != 0) { Seen.Add(MineralId); }
	}
	TestEqual(TEXT("13 个矿物编号两两不同且非零"), Seen.Num(), 13);
	return true;
}

#endif
