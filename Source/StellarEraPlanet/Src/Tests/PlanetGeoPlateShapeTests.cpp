// 板块形状：边界扭曲后每块板块仍是一整块（没有被撕成飞地），且边界确实比规则 Voronoi 曲折。
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "GeoField2D.h"
#include "GeoRng.h"
#include "Stage02Plates.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	int32 PlateShapeNeighbor(int32 N, int32 Face, int32 X, int32 Y, int32 DX, int32 DY)
	{
		const float U = (float(X + DX) + 0.5f) / float(N) * 2.f - 1.f;
		const float V = (float(Y + DY) + 0.5f) / float(N) * 2.f - 1.f;
		int32 F2; float U2, V2;
		FGeoField2D::DirToFace(FGeoField2D::FaceToDir(Face, U, V), F2, U2, V2);
		const int32 X2 = FMath::Clamp(int32((U2 + 1.f) * 0.5f * N), 0, N - 1);
		const int32 Y2 = FMath::Clamp(int32((V2 + 1.f) * 0.5f * N), 0, N - 1);
		return (F2 * N + Y2) * N + X2;
	}

	struct FPlateShapeStats { double Stray = 0.0; int32 BoundaryTexels = 0; };

	FPlateShapeStats PlateShapeMeasure(const FPlanetRecipeConfig& C)
	{
		FPlanetBakeFields F;
		FPlanetBakeReport R;
		FPlanetBakeContext Ctx{ C, F, FGeoRng::Derive(C.Seed, 2, 0), R };
		FStage02Plates().Bake(Ctx);
		const FGeoField2D& Id = F.PlateId;
		const int32 N = Id.Res, Total = 6 * N * N;
		TArray<int32> Plate; Plate.SetNum(Total);
		for (int32 Face = 0; Face < 6; ++Face) for (int32 Y = 0; Y < N; ++Y) for (int32 X = 0; X < N; ++X)
		{
			Plate[(Face * N + Y) * N + X] = FMath::RoundToInt(Id.At(Face, X, Y));
		}
		FPlateShapeStats Out;
		TArray<int32> Label; Label.Init(-1, Total);
		TMap<int32, int32> Largest, Count;
		TArray<int32> Stack;
		for (int32 Seed = 0; Seed < Total; ++Seed)
		{
			const int32 Face = Seed / (N * N), Y = (Seed / N) % N, X = Seed % N;
			const int32 Nb[4] = { PlateShapeNeighbor(N, Face, X, Y, 1, 0), PlateShapeNeighbor(N, Face, X, Y, -1, 0),
				PlateShapeNeighbor(N, Face, X, Y, 0, 1), PlateShapeNeighbor(N, Face, X, Y, 0, -1) };
			for (const int32 J : Nb) { if (Plate[J] != Plate[Seed]) { ++Out.BoundaryTexels; break; } }
			if (Label[Seed] >= 0) { continue; }
			int32 Size = 0;
			Stack.Reset(); Stack.Add(Seed); Label[Seed] = Seed;
			while (Stack.Num())
			{
				const int32 I = Stack.Pop(EAllowShrinking::No);
				++Size;
				const int32 Fi = I / (N * N), Yi = (I / N) % N, Xi = I % N;
				const int32 Nn[4] = { PlateShapeNeighbor(N, Fi, Xi, Yi, 1, 0), PlateShapeNeighbor(N, Fi, Xi, Yi, -1, 0),
					PlateShapeNeighbor(N, Fi, Xi, Yi, 0, 1), PlateShapeNeighbor(N, Fi, Xi, Yi, 0, -1) };
				for (const int32 J : Nn) { if (Plate[J] == Plate[Seed] && Label[J] < 0) { Label[J] = Seed; Stack.Add(J); } }
			}
			Count.FindOrAdd(Plate[Seed]) += Size;
			int32& L = Largest.FindOrAdd(Plate[Seed]);
			L = FMath::Max(L, Size);
		}
		int32 Stray = 0;
		for (const TPair<int32, int32>& P : Count) { Stray += P.Value - Largest[P.Key]; }
		Out.Stray = double(Stray) / double(Total);
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGeoPlateShapeTest, "StellarEra.Planet.Geo.PlateShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGeoPlateShapeTest::RunTest(const FString& Parameters)
{
	FPlanetRecipeConfig C;
	C.FaceRes = 1024;
	const FPlateShapeStats Warped = PlateShapeMeasure(C);
	FPlanetRecipeConfig Plain = C;
	Plain.Plates.WarpAmp = 0.f;
	Plain.Plates.DetailWarpAmp = 0.f;
	const FPlateShapeStats Round = PlateShapeMeasure(Plain);
	const double Ratio = double(Warped.BoundaryTexels) / FMath::Max(double(Round.BoundaryTexels), 1.0);
	AddInfo(FString::Printf(TEXT("飞地占比 %.4f；边界长度 扭曲 %d / 规则 %d = %.2f"), Warped.Stray, Warped.BoundaryTexels, Round.BoundaryTexels, Ratio));
	TestTrue(FString::Printf(TEXT("板块没被撕碎：飞地 %.4f < 0.005"), Warped.Stray), Warped.Stray < 0.005);
	TestTrue(FString::Printf(TEXT("边界比规则 Voronoi 曲折：长度比 %.2f > 1.3"), Ratio), Ratio > 1.3);
	return true;
}

#endif
