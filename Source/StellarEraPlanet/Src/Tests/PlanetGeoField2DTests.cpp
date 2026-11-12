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

	const FVector3f Axes[6] = { FVector3f(1, 0, 0), FVector3f(-1, 0, 0), FVector3f(0, 1, 0), FVector3f(0, -1, 0), FVector3f(0, 0, 1), FVector3f(0, 0, -1) };
	for (int32 Expect = 0; Expect < 6; ++Expect)
	{
		int32 Face; float U, V;
		FGeoField2D::DirToFace(Axes[Expect], Face, U, V);
		TestEqual(FString::Printf(TEXT("轴向 %d 落在面 %d"), Expect, Expect), Face, Expect);
		TestTrue(TEXT("轴向的面内坐标为 (0,0)"), FMath::IsNearlyZero(U, 1e-6f) && FMath::IsNearlyZero(V, 1e-6f));
	}

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

	{
		const int32 Probe[4] = { 5, 20, 32, 58 };
		float MaxCentreErr = 0.f;
		for (int32 Face = 0; Face < 6; ++Face)
		for (const int32 Y : Probe)
		for (const int32 X : Probe)
		{
			MaxCentreErr = FMath::Max(MaxCentreErr, FMath::Abs(Field.Sample(Field.TexelDir(Face, X, Y)) - Field.At(Face, X, Y)));
		}
		TestTrue(TEXT("texel 中心采样等于该 texel（< 1e-4，抓半格偏移）"), MaxCentreErr < 1e-4f);
	}

	FGeoField2D Mutated;
	Mutated.Init(64); Mutated.Fill(Linear);
	Mutated.At(3, 10, 20) += 1.f;
	TestTrue(TEXT("改动单个 texel 后哈希不同"), Mutated.Hash() != Field.Hash());

	FGeoField2D Const;
	Const.Init(32); Const.Fill([](const FVector3f&) { return 7.f; }); Const.BuildMips(3);
	TestEqual(TEXT("mip 数"), Const.NumMips(), 3);
	TestEqual(TEXT("mip2 分辨率"), Const.MipRes(2), 8);
	TestEqual(TEXT("常量场的 mip2 仍为常量"), Const.Sample(RandomDir(), 2), 7.f);

	FGeoField2D Grad;
	Grad.Init(4);
	for (int32 Face = 0; Face < 6; ++Face)
	for (int32 Y = 0; Y < 4; ++Y)
	for (int32 X = 0; X < 4; ++X) { Grad.At(Face, X, Y) = float(Face * 100 + Y * 10 + X); }
	Grad.BuildMips(3);
	TestEqual(TEXT("mip1 分辨率"), Grad.MipRes(1), 2);
	TestEqual(TEXT("mip2 分辨率为 1"), Grad.MipRes(2), 1);
	TestEqual(TEXT("mip1(2,0,0) = 四个父 texel 均值"), Grad.At(2, 0, 0, 1), 205.5f);
	TestEqual(TEXT("mip1(1,1,1) = 四个父 texel 均值"), Grad.At(1, 1, 1, 1), 100.f + 20.f + 2.f + 5.5f);
	TestEqual(TEXT("1×1 级为整面均值"), Grad.At(4, 0, 0, 2), 416.5f);
	TestEqual(TEXT("Mip 超出范围钳到最后一级"), Grad.Sample(FVector3f(1, 0, 0), 9), Grad.Sample(FVector3f(1, 0, 0), 2));
	TestEqual(TEXT("最后一级采样 +X 面均值"), Grad.Sample(FVector3f(1, 0, 0), 2), 16.5f);

	const FGeoField2D Empty;
	TestEqual(TEXT("未 Init 的场采样为 0"), Empty.Sample(FVector3f(0, 0, 1)), 0.f);
	return true;
}

#endif
