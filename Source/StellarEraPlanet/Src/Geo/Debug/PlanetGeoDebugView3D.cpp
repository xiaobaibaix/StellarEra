#include "PlanetGeoDebugView3D.h"
#include "Async/ParallelFor.h"
#include "PlanetGeoDebugRender.h"
#include "PlanetShapeEval.h"

namespace
{
	using namespace PlanetGeoDebugView3D;

	const FLinearColor View3DSpace(0.015f, 0.015f, 0.03f);
	constexpr int32 View3DRefine = 8;

	struct FView3DCam
	{
		FVector3d Pos, Fwd, Right, Up, Dir;
		double TanHalf = 0.0;
	};

	FView3DCam View3DMakeCam(const FPlanetSnapshot& S, const FView& V)
	{
		const double Yaw = FMath::DegreesToRadians(double(V.YawDeg));
		const double Pitch = FMath::DegreesToRadians(FMath::Clamp(double(V.PitchDeg), -89.0, 89.0));
		FView3DCam C;
		C.Dir = FVector3d(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw), FMath::Sin(Pitch));
		C.Fwd = -C.Dir;
		// UE 左手系：面朝 F、上为 Z 时右方向是 Z × F。
		C.Right = FVector3d::CrossProduct(FVector3d::UnitZ(), C.Fwd).GetSafeNormal();
		if (C.Right.IsNearlyZero()) { C.Right = FVector3d::UnitY(); }
		C.Up = FVector3d::CrossProduct(C.Fwd, C.Right);
		const FVector3d Center = (C.Right * double(V.Pan.X) + C.Up * double(V.Pan.Y)) * S.R0;
		C.Pos = Center + C.Dir * (FMath::Max(double(V.Distance), 0.05) * S.R0);
		C.TanHalf = FMath::Tan(FMath::DegreesToRadians(22.5));
		return C;
	}

	int32 View3DCat(float V) { return FMath::IsNaN(V) ? MIN_int32 : FMath::RoundToInt(V); }
}

void PlanetGeoDebugView3D::Render(const FPlanetSnapshot& S, const FGeoDebugLayer& Layer, const FGeoDebugLayer* SurfaceLayer,
	const FView& View, int32 W, int32 H, TArray<FColor>& OutColors, TArray<FVector3d>* OutHitPos, TArray<float>* OutHitValues)
{
	W = FMath::Max(W, 1); H = FMath::Max(H, 1);
	OutColors.SetNumUninitialized(W * H);
	if (OutHitPos) { OutHitPos->SetNumUninitialized(W * H); }
	if (OutHitValues) { OutHitValues->SetNumUninitialized(W * H); }

	const FView3DCam Cam = View3DMakeCam(S, View);
	const double R0 = S.R0;
	const double Rb = FMath::Max(S.MaxSurfaceRadius, R0) * 1.001;
	const int32 Steps = FMath::Clamp(View.Steps, 16, 2048);
	const double Dt = 2.0 * Rb / double(Steps);
	const double Alpha = View.Opacity >= 0.999f ? 1.0 : FMath::Clamp(double(View.Opacity), 0.02, 1.0);
	const bool bCategorical = Layer.Kind == EGeoDebugKind::Categorical;
	const FVector3d Light = (Cam.Dir + Cam.Up * 0.6 - Cam.Right * 0.4).GetSafeNormal();
	const double ClipPlane = double(View.ClipOffset) * R0;
	const double GradH = R0 * 0.002 + 1.0;

	const auto Clipped = [&](const FVector3d& P)
	{
		switch (View.Clip)
		{
		case EClip::Half:         return P.X > 0.0;
		case EClip::Quarter:      return P.X > 0.0 && P.Y < 0.0;
		case EClip::FacingCamera: return FVector3d::DotProduct(P, Cam.Dir) > ClipPlane;
		default:                  return false;
		}
	};
	const auto Inside = [&](const FVector3d& P, bool& bOutClipped)
	{
		bOutClipped = Clipped(P);
		return !bOutClipped && PlanetShapeEval::Value(S, P) <= 0.f;
	};
	const auto LayerValue = [&](const FGeoDebugLayer& L, const FVector3d& P)
	{
		if (!L.Sample) { return FGeoDebugLayer::NoData(); }
		const FVector3d Q = L.Domain == EGeoDebugDomain::Volume ? P
			: PlanetGeoDebugRender::SurfacePoint(S, FVector3f(P.GetSafeNormal()), L.Domain);
		return L.Sample(S, Q);
	};
	const auto IsHidden = [&](float V)
	{
		if (!bCategorical || FMath::IsNaN(V)) { return false; }
		const int32 I = FMath::RoundToInt(V);
		return View.Hidden.IsValidIndex(I) && View.Hidden[I];
	};
	const auto CutNormal = [&](const FVector3d& E)
	{
		switch (View.Clip)
		{
		case EClip::Half:         return FVector3d::UnitX();
		case EClip::Quarter:      return FMath::Abs(E.X) <= FMath::Abs(E.Y) ? FVector3d::UnitX() : -FVector3d::UnitY();
		case EClip::FacingCamera: return Cam.Dir;
		default:                  return E.GetSafeNormal();
		}
	};
	const auto SurfaceNormal = [&](const FVector3d& E)
	{
		const FVector3d G(
			PlanetShapeEval::Value(S, E + FVector3d(GradH, 0, 0)) - PlanetShapeEval::Value(S, E - FVector3d(GradH, 0, 0)),
			PlanetShapeEval::Value(S, E + FVector3d(0, GradH, 0)) - PlanetShapeEval::Value(S, E - FVector3d(0, GradH, 0)),
			PlanetShapeEval::Value(S, E + FVector3d(0, 0, GradH)) - PlanetShapeEval::Value(S, E - FVector3d(0, 0, GradH)));
		return G.IsNearlyZero() ? E.GetSafeNormal() : G.GetSafeNormal();
	};

	ParallelFor(H, [&](int32 Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			const int32 Index = Y * W + X;
			const double Nx = ((double(X) + 0.5) / double(W) * 2.0 - 1.0) * Cam.TanHalf * double(W) / double(H);
			const double Ny = (1.0 - (double(Y) + 0.5) / double(H) * 2.0) * Cam.TanHalf;
			const FVector3d D = (Cam.Fwd + Cam.Right * Nx + Cam.Up * Ny).GetSafeNormal();
			TArray<TPair<double, float>> Extra;
			if (Layer.RayHits)
			{
				Layer.RayHits(S, Cam.Pos, D, Extra);
				Extra.Sort([](const TPair<double, float>& A, const TPair<double, float>& B) { return A.Key < B.Key; });
			}
			int32 ExtraNext = 0;

			FLinearColor Acc(0.f, 0.f, 0.f, 0.f);
			double A = 0.0;
			bool bHit = false;
			FVector3d HitPos(FGeoDebugLayer::NoData());
			float HitValue = FGeoDebugLayer::NoData();

			const auto Composite = [&](const FColor& C, FVector3d N, const FVector3d& E, float MainValue)
			{
				if (FVector3d::DotProduct(N, D) > 0.0) { N = -N; }
				const float Shade = float(0.35 + 0.65 * FMath::Max(0.0, FVector3d::DotProduct(N, Light)));
				const FLinearColor Lc = FLinearColor::FromSRGBColor(C) * Shade;
				Acc += Lc * float((1.0 - A) * Alpha);
				A += (1.0 - A) * Alpha;
				if (!bHit) { bHit = true; HitPos = E; HitValue = MainValue; }
			};

			// 光线与包围球求交：没打中的像素是太空。
			const double B = FVector3d::DotProduct(Cam.Pos, D);
			const double Disc = B * B - (Cam.Pos.SizeSquared() - Rb * Rb);
			if (Disc > 0.0)
			{
				double T = FMath::Max(-B - FMath::Sqrt(Disc), 0.0);
				const double T1 = -B + FMath::Sqrt(Disc);
				bool bClipPrev = false;
				bool bInPrev = Inside(Cam.Pos + D * T, bClipPrev);
				int32 SegCat = bInPrev ? View3DCat(LayerValue(Layer, Cam.Pos + D * T)) : MIN_int32;
				for (int32 i = 0; i <= Steps + 2 && T < T1 && A < 0.995; ++i)
				{
					const double Tn = FMath::Min(T + Dt, T1);
					bool bClipN = false;
					const bool bInN = Inside(Cam.Pos + D * Tn, bClipN);
					if (!bInPrev && bInN)
					{
						// 二分到进入点；进入前那一侧被切掉了，说明打中的是切面而不是地表。
						double Lo = T, Hi = Tn;
						bool bLoClipped = bClipPrev;
						for (int32 k = 0; k < View3DRefine; ++k)
						{
							const double Mid = 0.5 * (Lo + Hi);
							bool bC = false;
							if (Inside(Cam.Pos + D * Mid, bC)) { Hi = Mid; } else { Lo = Mid; bLoClipped = bC; }
						}
						const FVector3d E = Cam.Pos + D * Hi;
						const bool bCut = bLoClipped;
						const float MainValue = LayerValue(Layer, E);
						SegCat = View3DCat(MainValue);
						if (!IsHidden(MainValue))
						{
							const bool bUseSurface = !bCut && SurfaceLayer;
							const FGeoDebugLayer& L = bUseSurface ? *SurfaceLayer : Layer;
							const float V = bUseSurface ? LayerValue(L, E) : MainValue;
							FColor Col = L.ColorOf(S, V);
							if (bCut && View.MineralScale > 0.f)
							{
								const uint8 Ore = PlanetGeoDebugRender::MineralAt(S, E, View.MineralScale);
								if (Ore != 0) { Col = S.MineralColor(Ore); }
							}
							Composite(Col, bCut ? CutNormal(E) : SurfaceNormal(E), E, MainValue);
						}
					}
					else if (bInPrev && bInN && bCategorical && !Layer.RayHits)
					{
						const int32 CatN = View3DCat(LayerValue(Layer, Cam.Pos + D * Tn));
						if (CatN != SegCat)
						{
							double Lo = T, Hi = Tn;
							for (int32 k = 0; k < View3DRefine; ++k)
							{
								const double Mid = 0.5 * (Lo + Hi);
								if (View3DCat(LayerValue(Layer, Cam.Pos + D * Mid)) == SegCat) { Lo = Mid; } else { Hi = Mid; }
							}
							const FVector3d E = Cam.Pos + D * Hi;
							const float V = LayerValue(Layer, E);
							SegCat = View3DCat(V);
							// 圈层是同心的：层间分界的法线取径向就够看出层次。
							if (!IsHidden(V)) { Composite(Layer.ColorOf(S, V), E.GetSafeNormal(), E, V); }
						}
					}
					while (ExtraNext < Extra.Num() && Extra[ExtraNext].Key <= Tn && A < 0.995)
					{
						const TPair<double, float>& Hx = Extra[ExtraNext++];
						const FVector3d E = Cam.Pos + D * Hx.Key;
						bool bC = false;
						if (Inside(E, bC) && !IsHidden(Hx.Value)) { Composite(Layer.ColorOf(S, Hx.Value), E.GetSafeNormal(), E, Hx.Value); }
					}
					T = Tn;
					bInPrev = bInN;
					bClipPrev = bClipN;
				}
			}

			const FLinearColor Out = Acc + View3DSpace * float(1.0 - A);
			OutColors[Index] = FLinearColor(Out.R, Out.G, Out.B, 1.f).ToFColorSRGB();
			if (OutHitPos) { (*OutHitPos)[Index] = HitPos; }
			if (OutHitValues) { (*OutHitValues)[Index] = HitValue; }
		}
	});
}
