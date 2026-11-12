// 球面场是所有宏观量的载体：方向换算必须可逆，跨面采样必须连续，否则地形在 cubemap 接缝处裂开。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoRng.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoField2DTest, "StellarEra.Planet.Geo.Field2D",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoField2DTest::RunTest(const FString& Parameters)
{
	FGeoRng Rng = FGeoRng::Derive(1, 0, 0);
	const auto RandomDir = [&Rng]()
	{
		const float Z = Rng.Range(-1.f, 1.f), Phi = Rng.Range(0.f, 2.f * PI), S = FMath::Sqrt(1.f - Z * Z);
		return FVector3f(S * FMath::Cos(Phi), S * FMath::Sin(Phi), Z);
	};

	float MaxRoundTrip = 0.f;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FVector3f D = RandomDir();
		int32 Face; float U, V;
		FGeoField2D::DirToFace(D, Face, U, V);
		MaxRoundTrip = FMath::Max(MaxRoundTrip, (FGeoField2D::FaceToDir(Face, U, V) - D).Size());
	}
	TestTrue(TEXT("方向 → 面坐标 → 方向 可逆"), MaxRoundTrip < 1e-5f);

	const auto Linear = [](const FVector3f& D) { return D.X + 2.f * D.Y + 3.f * D.Z; };
	FGeoField2D Field;
	Field.Init(64);
	Field.Fill(Linear);
	float MaxErr = 0.f;
	for (int32 i = 0; i < 2000; ++i)
	{
		const FVector3f D = RandomDir();
		MaxErr = FMath::Max(MaxErr, FMath::Abs(Field.Sample(D) - Linear(D)));
	}
	TestTrue(TEXT("64² 双线性误差 < 0.15（边缘钳到边缘 texel，误差约半格梯度）"), MaxErr < 0.15f);

	const FVector3f A = FVector3f(1.f, 0.999f, 0.3f).GetSafeNormal(), B = FVector3f(0.999f, 1.f, 0.3f).GetSafeNormal();
	TestTrue(TEXT("+X / +Y 接缝两侧连续（< 0.15）"), FMath::Abs(Field.Sample(A) - Field.Sample(B)) < 0.15f);

	FGeoField2D Same, SingleThread;
	Same.Init(64); Same.Fill(Linear);
	SingleThread.Init(64); SingleThread.Fill(Linear, /*bSingleThread=*/true);
	TestTrue(TEXT("同输入同哈希"), Field.Hash() == Same.Hash());
	TestTrue(TEXT("单线程与并行填充逐位一致"), Field.Hash() == SingleThread.Hash());

	FGeoField2D Const;
	Const.Init(32); Const.Fill([](const FVector3f&) { return 7.f; }); Const.BuildMips(3);
	TestEqual(TEXT("mip 数"), Const.NumMips(), 3);
	TestEqual(TEXT("mip2 分辨率"), Const.MipRes(2), 8);
	TestEqual(TEXT("常量场的 mip2 仍为常量"), Const.Sample(RandomDir(), 2), 7.f);
	return true;
}

#endif
