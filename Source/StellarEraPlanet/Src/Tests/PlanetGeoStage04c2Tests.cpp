// 阶段 4c-2：碳酸岩岩性与放置、花岗岩基里的锡钨脉与伟晶岩团。跑真实 2→3→4 烘焙。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoLithology.h"
#include "GeoMineralRule.h"
#include "GeoMineralRules.h"
#include "GeoPrimitive.h"
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
	int32 NoHost = 0, BadZ = 0, BadXY = 0, BadKind = 0, BadShape = 0, BadPose = 0;
	float ZLo = TNumericLimits<float>::Max(), ZHi = -ZLo, WorstXY = 0.f;
	for (const FGeoPrimitive* Body : Bodies)
	{
		const FGeoPrimitive* Host = Stage04c2NearestHost(Hosts, Body->Center);
		if (!Host) { ++NoHost; continue; }
		const FVector3f L = Stage04c2Local(*Host, Body->Center);
		const float AZ = FMath::Abs(L.Z);
		const float AXY = FMath::Max(FMath::Abs(L.X), FMath::Abs(L.Y));
		ZLo = FMath::Min(ZLo, AZ); ZHi = FMath::Max(ZHi, AZ); WorstXY = FMath::Max(WorstXY, AXY);
		BadZ += AZ >= 0.5f && AZ <= 1.02f ? 0 : 1;
		BadXY += AXY <= 0.6f ? 0 : 1;
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
	AddInfo(FString::Printf(TEXT("局部 |z| %.3f–%.3f，最差 |x|/|y| %.3f；越界 z %d / xy %d / 非 OrientedBlob %d / 尺寸 %d / 朝向 %d"),
		ZLo, ZHi, WorstXY, BadZ, BadXY, BadKind, BadShape, BadPose));
	TestEqual(TEXT("没有无宿主的矿体"), NoHost, 0);
	TestEqual(TEXT("局部 |z| 都在 [0.5, 1.02]（岩基顶面壳层）"), BadZ, 0);
	TestEqual(TEXT("局部 |x|,|y| 都 ≤ 0.6"), BadXY, 0);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoStage04c2ChromiumPGE, "StellarEra.Planet.Geo.Stage04c2.ChromiumPGE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoStage04c2ChromiumPGE::RunTest(const FString& Parameters)
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

#endif
