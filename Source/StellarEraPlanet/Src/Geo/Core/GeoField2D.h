// 6 面 cubemap 标量场。面编号 0..5 = +X −X +Y −Y +Z −Z；面内 U、V ∈ [-1,1]，texel 中心在 ((i+0.5)/Res)*2-1。
#pragma once
#include "CoreMinimal.h"

class STELLARERAPLANET_API FGeoField2D
{
public:
	void Init(int32 InRes);
	int32 Res = 0;
	int32 NumMips() const { return Levels.Num(); }
	int32 MipRes(int32 Mip) const { return FMath::Max(1, Res >> Mip); }

	float& At(int32 Face, int32 X, int32 Y, int32 Mip = 0) { const int32 R = MipRes(Mip); return Levels[Mip][(Face * R + Y) * R + X]; }
	float At(int32 Face, int32 X, int32 Y, int32 Mip = 0) const { const int32 R = MipRes(Mip); return Levels[Mip][(Face * R + Y) * R + X]; }

	static void DirToFace(const FVector3f& Dir, int32& OutFace, float& OutU, float& OutV);
	static FVector3f FaceToDir(int32 Face, float U, float V);
	FVector3f TexelDir(int32 Face, int32 X, int32 Y) const;

	/** 每个 texel 只写自己，单线程与并行逐位一致（确定性前提）。 */
	void Fill(TFunctionRef<float(const FVector3f&)> Func, bool bSingleThread = false);
	/** Count 含第 0 级；2×2 盒式下采样。 */
	void BuildMips(int32 Count);

	/** 面内双线性，越界钳到边缘 texel：接缝误差约半个 texel，M1 的宏观量够用。 */
	float Sample(const FVector3f& Dir, int32 Mip = 0) const;

	uint64 Hash() const;
	SIZE_T GetAllocatedSize() const;

private:
	TArray<TArray<float>> Levels;
};
