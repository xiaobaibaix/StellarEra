// 全项目唯一的材质通道约定（附录 11-material-field.md §2）。改布局只改这里 + MF_GeoDecode。
#pragma once
#include "CoreMinimal.h"
#include "GeoLithology.h"

struct FGeoSample
{
	uint8 Lithology = 0;
	uint8 Mineral = 0;
	uint8 Grade = 0;
	uint8 Clue = 0;          // 0..15
	uint8 ClueStrength = 0;  // 0..15
	uint8 SecondaryMineral = 0;
	uint8 SecondaryGrade = 0;
};

struct FGeoPackedMaterial
{
	uint8 R = 0, G = 0, B = 0, A = 0, U0 = 0, V0 = 0, U1 = 0, V1 = 0;
};

namespace GeoMaterialCodec
{
	FORCEINLINE FGeoPackedMaterial Encode(const FGeoSample& S)
	{
		FGeoPackedMaterial M;
		M.R = S.Lithology;
		M.G = S.Mineral;
		M.B = S.Grade;
		M.A = uint8((FMath::Min<uint8>(S.Clue, 15) << 4) | FMath::Min<uint8>(S.ClueStrength, 15));
		M.U0 = S.SecondaryMineral;
		M.V0 = S.SecondaryGrade;
		return M;
	}

	FORCEINLINE FGeoSample Decode(const FGeoPackedMaterial& M)
	{
		FGeoSample S;
		S.Lithology = M.R;
		S.Mineral = M.G;
		S.Grade = M.B;
		S.Clue = uint8(M.A >> 4);
		S.ClueStrength = uint8(M.A & 15);
		S.SecondaryMineral = M.U0;
		S.SecondaryGrade = M.V0;
		return S;
	}

	/** M1 调试：RGB 直接写岩性调试色，让现成顶点色材质看得出分层。Task 14 选定方案后删除。 */
	FORCEINLINE FGeoPackedMaterial EncodeDebugColor(const FGeoSample& S)
	{
		FGeoPackedMaterial M = Encode(S);
		const FColor C = GeoLithology::DebugColor(S.Lithology);
		M.R = C.R; M.G = C.G; M.B = C.B;
		return M;
	}
}
