// Copyright Voxel Plugin SAS. All Rights Reserved.
// Procedural spherical planet terrain generator.

#include "VoxelPlanetGenerator.h"
#include "FastNoise/VoxelFastNoise.inl"
#include "VoxelMaterialBuilder.h"

TVoxelSharedRef<FVoxelGeneratorInstance> UVoxelPlanetGenerator::GetInstance()
{
	return MakeVoxelShared<FVoxelPlanetGeneratorInstance>(*this);
}

///////////////////////////////////////////////////////////////////////////////

FVoxelPlanetGeneratorInstance::FVoxelPlanetGeneratorInstance(UVoxelPlanetGenerator& MyGenerator)
	: Super(&MyGenerator)
	, Radius(MyGenerator.Radius)
	, Center(MyGenerator.Center)
	, bUseContinents(MyGenerator.bUseContinents)
	, ContinentFrequency(MyGenerator.ContinentFrequency)
	, ContinentHeight(MyGenerator.ContinentHeight)
	, ContinentOctaves(MyGenerator.ContinentOctaves)
	, ContinentContrast(MyGenerator.ContinentContrast)
	, bUseMountains(MyGenerator.bUseMountains)
	, MountainFrequency(MyGenerator.MountainFrequency)
	, MountainHeight(MyGenerator.MountainHeight)
	, MountainSharpness(MyGenerator.MountainSharpness)
	, MountainLandBias(MyGenerator.MountainLandBias)
	, bUseDetail(MyGenerator.bUseDetail)
	, DetailFrequency(MyGenerator.DetailFrequency)
	, DetailHeight(MyGenerator.DetailHeight)
	, bUseRavines(MyGenerator.bUseRavines)
	, RavineFrequency(MyGenerator.RavineFrequency)
	, RavineDepth(MyGenerator.RavineDepth)
	, CaveAmount(MyGenerator.CaveAmount)
	, CaveFrequency(MyGenerator.CaveFrequency)
	, ShellThickness(MyGenerator.ShellThickness)
	, SeaLevel(MyGenerator.SeaLevel)
	, DeepWaterColor(MyGenerator.DeepWaterColor)
	, ShoreColor(MyGenerator.ShoreColor)
	, LowlandColor(MyGenerator.LowlandColor)
	, HighlandColor(MyGenerator.HighlandColor)
	, PeakColor(MyGenerator.PeakColor)
	, CoreColor(MyGenerator.CoreColor)
	, SnowLine(MyGenerator.SnowLine)
	, Seed(MyGenerator.Seed)
{
}

void FVoxelPlanetGeneratorInstance::Init(const FVoxelGeneratorInit& InitStruct)
{
	ContinentNoise.SetSeed(Seed);
	MountainNoise.SetSeed(Seed + 1);
	DetailNoise.SetSeed(Seed + 2);
	RavineNoise.SetSeed(Seed + 3);
	CaveNoise.SetSeed(Seed + 4);

	ContinentNoise.SetFractalOctavesAndGain(ContinentOctaves, 0.5f);
	MountainNoise.SetFractalOctavesAndGain(4, 0.5f);
	DetailNoise.SetFractalOctavesAndGain(3, 0.5f);
	RavineNoise.SetFractalOctavesAndGain(2, 0.5f);
	CaveNoise.SetFractalOctavesAndGain(2, 0.5f);
}

double FVoxelPlanetGeneratorInstance::GetSurfaceRadius(double NX, double NY, double NZ) const
{
	double RadiusOffset = 0.0;

	// --- Layer 1: continents -------------------------------------------------
	// Fractal Perlin in roughly [-1, 1]. Negative = ocean basin, positive = land.
	double Continent = 0.0;
	if (bUseContinents)
	{
		Continent = ContinentNoise.GetPerlinFractal_3D(NX, NY, NZ, ContinentFrequency, ContinentOctaves);

		// Contrast: push values away from zero so oceans get deeper and land gets
		// taller. Without this the terrain looks like gentle rolling hills.
		Continent = FMath::Clamp(Continent, -1.0, 1.0) * (1.0 + FMath::Max(ContinentContrast, 0.0));
		Continent = FMath::Clamp(Continent, -1.0, 1.0);
	}
	RadiusOffset += Continent * ContinentHeight;

	// --- Layer 2: mountains --------------------------------------------------
	// Mountains only grow where the continent noise is already high, which is how
	// real mountain ranges form at plate boundaries rather than in the middle of
	// the ocean.
	if (bUseMountains)
	{
		const double Mountain = MountainNoise.GetPerlinFractal_3D(NX, NY, NZ, MountainFrequency, 4);

		// Take only the positive half: mountain noise must not dig basins.
		double MountainMask = FMath::Max(Mountain, 0.0);

		// Sharpen: exponent > 1 turns rounded hills into narrow peaks.
		MountainMask = FMath::Pow(MountainMask, FMath::Max(MountainSharpness, 1.0));

		// Bias by continental height.
		const double LandFactor = FMath::Clamp(Continent * 0.5 + 0.5, 0.0, 1.0);
		const double Bias = FMath::Lerp(1.0, LandFactor, FMath::Clamp(MountainLandBias, 0.0, 1.0));

		RadiusOffset += MountainMask * Bias * MountainHeight;
	}

	// --- Layer 3: detail -----------------------------------------------------
	if (bUseDetail)
	{
		RadiusOffset += DetailNoise.GetPerlin_3D(NX, NY, NZ, DetailFrequency) * DetailHeight;
	}

	// --- Layer 4: ravines ----------------------------------------------------
	// Ridged noise: 1 - |n| gives sharp positive ridges where the noise crosses
	// zero. We invert it so the ridges become cuts instead of raised walls.
	if (bUseRavines)
	{
		const double Ravine = RavineNoise.GetPerlinFractal_3D(NX, NY, NZ, RavineFrequency, 2);
		const double Ridge = 1.0 - FMath::Abs(Ravine);

		// Only carve where the ridge is strong, and never build up.
		const double Cut = FMath::Clamp((Ridge - 0.6) / 0.4, 0.0, 1.0);
		RadiusOffset -= Cut * RavineDepth;
	}

	return Radius;
}

double FVoxelPlanetGeneratorInstance::GetElevationAt(double NX, double NY, double NZ, double CrustRadius, double Distance) const
{
	// Reserved for future per-voxel effects that must not change the terrain
	// radius, e.g. fine surface noise that fades with depth.
	return CrustRadius;
}

v_flt FVoxelPlanetGeneratorInstance::GetValueImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	// Offset from the planet centre, with a tiny epsilon so we never return
	// exactly 0 density, which upsets the mesher.
	const double PX = double(X) - Center.X + 0.001;
	const double PY = double(Y) - Center.Y + 0.001;
	const double PZ = double(Z) - Center.Z + 0.001;

	const double DistanceSq = PX * PX + PY * PY + PZ * PZ;
	const double Distance = FMath::Sqrt(DistanceSq);
	if (Distance < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVoxelValue::Full().ToFloat();
	}

	// Sample all noise on the UNIT sphere. This is the single most important
	// detail of the whole generator: feeding raw world coordinates would make the
	// effective noise frequency depend on the radius, producing smeared
	// singularities at the poles and stretched features near the equator.
	const double InvDistance = 1.0 / Distance;
	const double NX = PX * InvDistance;
	const double NY = PY * InvDistance;
	const double NZ = PZ * InvDistance;

	const double SurfaceRadius = Radius + GetSurfaceRadius(NX, NY, NZ);

	// Base signed distance field: negative inside the crust, positive outside
	double Value = Distance - SurfaceRadius;

	// Hollow shell: keep only the outer ShellThickness of the crust
	if (ShellThickness > 0.f)
	{
		const double InnerSdf = Distance - (Radius - ShellThickness);
		Value = FMath::Max(Value, -InnerSdf);
	}

	// Caves: ridged field subtracted from the crust, faded near the surface so it
	// cannot perforate the outer shell
	if (CaveAmount > 0.f)
	{
		const double CaveNoiseValue = CaveNoise.GetPerlin_3D(NX, NY, NZ, CaveFrequency);
		const double Ridged = 1.0 - FMath::Abs(CaveNoiseValue);
		const double DepthFade = FMath::Clamp((SurfaceRadius - Distance) / FMath::Max(ContinentHeight, 1.f), 0.0, 1.0);
		const double CaveSdf = (Ridged - CaveAmount) * CaveFrequency * DepthFade;
		Value = FMath::Max(Value, -CaveSdf);
	}

	// Density is clamped to [-1, 1] downstream. Dividing by a constant rescales
	// the gradient, and the shading normal is derived from that gradient.
	// Too small a divisor saturates the gradient into flat shading, too large a
	// divisor makes the normals noisy. 5 is what the official examples use.
	Value /= 5.0;

	return v_flt(FMath::Clamp(Value, -1.0, 1.0));
}

FVoxelMaterial FVoxelPlanetGeneratorInstance::GetMaterialImpl(v_flt X, v_flt Y, v_flt Z, int32 LOD, const FVoxelItemStack& Items) const
{
	const double PX = double(X) - Center.X + 0.001;
	const double PY = double(Y) - Center.Y + 0.001;
	const double PZ = double(Z) - Center.Z + 0.001;
	const double Distance = FMath::Sqrt(PX * PX + PY * PY + PZ * PZ);

	FVoxelMaterialBuilder Builder;
	Builder.SetMaterialConfig(EVoxelMaterialConfig::RGB);

	// Hollow shell interior
	if (ShellThickness > 0.f && Distance < Radius - ShellThickness)
	{
		Builder.SetColor(CoreColor);
		return Builder.Build();
	}

	// Deep crust shortcut. For a planet the vast majority of queried voxels are
	// buried, and skipping the noise evaluation here saves a lot.
	const double MaxCrustRadius = Radius + ContinentHeight + MountainHeight + DetailHeight;
	if (Distance < MaxCrustRadius - 2.0 * (ContinentHeight + MountainHeight + DetailHeight))
	{
		Builder.SetColor(LowlandColor);
		return Builder.Build();
	}

	const double InvDistance = Distance > UE_DOUBLE_SMALL_NUMBER ? 1.0 / Distance : 0.0;
	const double NX = PX * InvDistance;
	const double NY = PY * InvDistance;
	const double NZ = PZ * InvDistance;

	const double SurfaceRadius = Radius + GetSurfaceRadius(NX, NY, NZ);

	// --- Height band colour ramp --------------------------------------------
	const double SeaRadius = Radius - SeaLevel;

	if (SurfaceRadius < SeaRadius)
	{
		// Underwater: deep water at the bottom, shore colour near the surface
		const double Depth = FMath::Clamp((SeaRadius - SurfaceRadius) / FMath::Max(SeaLevel, 1.f), 0.0, 1.0);
		Builder.SetColor(FLinearColor::LerpUsingHSV(ShoreColor, DeepWaterColor, FMath::Pow(float(Depth), 0.6f)));
		return Builder.Build();
	}

	// Land ramp: lowland -> highland
	const double LandHeight = FMath::Max(SurfaceRadius - Radius, 0.0);
	const double LandMax = FMath::Max(ContinentHeight + MountainHeight + DetailHeight, 1.0);
	const float LandT = float(FMath::Clamp(LandHeight / LandMax, 0.0, 1.0));

	FLinearColor LandColor = FLinearColor::LerpUsingHSV(LowlandColor, HighlandColor, FMath::Pow(LandT, 0.7f));

	// Snow line overrides everything above it
	if (SnowLine > 0.f && SurfaceRadius > Radius + SnowLine)
	{
		const float SnowT = float(FMath::Clamp((SurfaceRadius - (Radius + SnowLine)) / FMath::Max(SnowLine, 1.f), 0.0, 1.0));
		LandColor = FLinearColor::LerpUsingHSV(LandColor, PeakColor, SnowT);
	}

	Builder.SetColor(LandColor);
	return Builder.Build();
}

TVoxelRange<v_flt> FVoxelPlanetGeneratorInstance::GetValueRangeImpl(const FVoxelIntBox& Bounds, int32 LOD, const FVoxelItemStack& Items) const
{
	// THE performance lever for a planet. A radius-1000 planet occupies a cube of
	// side 2000 that is more than 99% empty space. Returning an accurate range
	// lets the plugin skip whole chunks without touching a single voxel.
	//
	// Contract: the returned range MUST contain every value GetValueImpl can
	// produce inside Bounds. Under-estimate it and you get holes in the planet.

	// Closest point of the box to the planet centre: clamp each axis so the
	// coordinate is at or above 0 relative to the centre.
	const double MinX = FMath::Max(0.0, double(Bounds.Min.X) - Center.X);
	const double MinY = FMath::Max(0.0, double(Bounds.Min.Y) - Center.Y);
	const double MinZ = FMath::Max(0.0, double(Bounds.Min.Z) - Center.Z);

	const double MinDistance = FMath::Sqrt(MinX * MinX + MinY * MinY + MinZ * MinZ);

	// Worst case displacement of the surface. Every layer is bounded by its
	// amplitude: Continent is clamped to [-1, 1], Mountain is a clamped pow of a
	// non-negative value, Detail is a raw perlin, Ravine only subtracts.
	const double MaxAmplitude = ContinentHeight + MountainHeight + DetailHeight + RavineDepth;

	const double MinValue = (MinDistance - (Radius + MaxAmplitude)) / 5.0;

	// The shell and cave terms are wrapped in FMath::Max(Value, ...), so they can
	// only ever push the value up. That keeps MinValue a valid lower bound. The
	// upper bound cannot be tightened below the clamp ceiling.
	//
	// Failing to prove "this whole chunk is solid" costs nothing: the interior of
	// a planet is never visible, and the lower bound above already skips all the
	// empty space, which is the expensive part.
	return TVoxelRange<v_flt>(v_flt(MinValue), v_flt(1.0));
}

FVector FVoxelPlanetGeneratorInstance::GetUpVector(v_flt X, v_flt Y, v_flt Z) const
{
	// On a planet "up" points away from the core, not along world Z. Spawners use
	// this to orient grass and rocks. Returning FVector::UpVector would make
	// everything lie flat at the poles and stick out sideways at the equator.
	const FVector P(double(X) - Center.X, double(Y) - Center.Y, double(Z) - Center.Z);
	if (P.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector::UpVector;
	}
	return P.GetSafeNormal();
}
