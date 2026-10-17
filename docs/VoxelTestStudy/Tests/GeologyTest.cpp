// 地质系统验证 —— 不依赖 UE，可独立编译运行。
//
// 用法：
//   clang++ -std=c++17 GeologyTest.cpp -o test && ./test
//
// 地质系统是纯计算（只有数学，没有引擎 API），所以能完整地测。
// 这对"露天矿""纯度""成分混合"这些行为是必要的 —— 它们是玩法的基础。

#include <cstdint>
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

// ── 最小类型替身 ────────────────────────────────────────
using uint8 = std::uint8_t;
using int32 = std::int32_t;
using uint32 = std::uint32_t;

// ── 极简向量与颜色 ──────────────────────────────────────
struct FVector
{
    double X = 0, Y = 0, Z = 0;
    FVector() = default;
    FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}

    double SizeSquared() const { return X * X + Y * Y + Z * Z; }
    double Size() const { return std::sqrt(SizeSquared()); }
    bool IsNearlyZero() const { return SizeSquared() < 1e-12; }
    FVector GetSafeNormal() const
    {
        const double S = Size();
        if (S < 1e-12) return FVector();
        return FVector(X / S, Y / S, Z / S);
    }
    FVector operator*(double S) const { return FVector(X * S, Y * S, Z * S); }
};

struct FLinearColor
{
    float R = 1, G = 1, B = 1, A = 1;
    FLinearColor() = default;
    FLinearColor(float r, float g, float b) : R(r), G(g), B(b) {}
};

static const double KINDA_SMALL_NUMBER = 1e-4;
static const double PI = 3.14159265358979323846;

struct FMath
{
    static double Clamp(double V, double Min, double Max)
    { return V < Min ? Min : (V < Max ? V : Max); }
    static double Exp(double X) { return std::exp(X); }
    static double Sqrt(double X) { return std::sqrt(X); }
    static double Abs(double X) { return std::fabs(X); }
    static float ClampF(float V, float Min, float Max)
    { return V < Min ? Min : (V < Max ? V : Max); }
};

static float Mix(float A, float B, float T) { return A + (B - A) * T; }

// ── 极简噪声（与 FVoxelFastNoise 的接口一致）──
class FVoxelFastNoise
{
public:
    void SetSeed(int32 NewSeed) { Seed = NewSeed; }
    void SetFractalOctavesAndGain(int32 Oct, float Gain) { Octaves = Oct; }

    // 用确定性的多频正弦近似 Perlin，接口一致即可
    double GetPerlinFractal_3D(double x, double y, double z, double freq, int32 oct) const
    {
        double Sum = 0.0;
        double Amp = 1.0;
        double Freq = freq;
        double Norm = 0.0;
        for (int32 i = 0; i < oct; ++i)
        {
            Sum += Amp * Sin3D(x * Freq + Seed * 0.017, y * Freq, z * Freq);
            Norm += Amp;
            Amp *= 0.5;
            Freq *= 2.0;
        }
        return Norm > 0.0 ? Sum / Norm : 0.0;
    }

private:
    double Sin3D(double X, double Y, double Z) const
    {
        return std::sin(X * 1.7 + Y * 2.3 + Z * 3.1)
             * std::sin(X * 0.9 - Y * 1.7 + Z * 2.2);
    }

    int32 Seed = 0;
    int32 Octaves = 3;
};

// ── 地质类型（与 GeologyTypes.h 一致）──
enum class EGemType : uint8 { None = 0, Iron, Copper, Silicon, Titanium, Uranium, Water, Count };

struct FGemConfig
{
    bool bPresent = false;
    float GlobalWeight = 0.f;
    double PeakDepth = -50.0;
    double LayerThickness = 200.0;
    float SpatialFrequency = 2.0f;
    float SpatialVariance = 0.6f;
    float Hardness = 1.0f;
    float BaseYield = 1.0f;
    FLinearColor DisplayColor;
};

struct FGeologyConfig
{
    FGemConfig Iron, Copper, Silicon, Titanium, Uranium, Water;
    int32 PlanetSeed = 1337;
    double PlanetRadius = 1000.0;
    float MiningThreshold = 0.12f;
    float OutcropThreshold = 0.22f;
    FLinearColor MixedRockColor = FLinearColor(0.32f, 0.28f, 0.24f);
    float PurityForFullColor = 0.75f;

    const FGemConfig* GetGemConfig(EGemType G) const
    {
        switch (G)
        {
        case EGemType::Iron:     return &Iron;
        case EGemType::Copper:   return &Copper;
        case EGemType::Silicon:  return &Silicon;
        case EGemType::Titanium: return &Titanium;
        case EGemType::Uranium:  return &Uranium;
        case EGemType::Water:    return &Water;
        default: return nullptr;
        }
    }
};

struct FGeologySample
{
    std::vector<float> Weights;
    float TotalWeight = 0.f;
    float Purity = 0.f;
    EGemType DominantGem = EGemType::None;
    bool bWorthMining = false;

    bool IsEmpty() const { return TotalWeight <= KINDA_SMALL_NUMBER; }
    float GetWeight(EGemType G) const
    {
        const int32 I = int32(G);
        return (Weights.size() > size_t(I)) ? Weights[I] : 0.f;
    }
};

// ── 地质系统（与 GeologySystem.cpp 一致）──
class FGeologySystem
{
public:
    FGeologySystem() { Init(MakeDefaultTerrestrial()); }

    static FGeologyConfig MakeDefaultTerrestrial(double R = 1000.0)
    {
        FGeologyConfig C;
        C.PlanetRadius = R;
        const double S = FMath::Clamp(R / 1000.0, 0.2, 3.0);

        // 层厚 = |峰值深度| × 0.65，保证高斯尾部快速衰减、纯度够高
        const auto Thick = [S](double P)
        { double t = FMath::Abs(P) * 0.65; if (t < 12.0) t = 12.0; return t * S; };

        C.Iron.bPresent = true;     C.Iron.GlobalWeight = 0.75f;
        C.Iron.PeakDepth = -30.0 * S;  C.Iron.LayerThickness = Thick(C.Iron.PeakDepth);
        C.Iron.SpatialFrequency = 2.0f;  C.Iron.BaseYield = 2.0f;

        C.Copper.bPresent = true;   C.Copper.GlobalWeight = 0.45f;
        C.Copper.PeakDepth = -65.0 * S; C.Copper.LayerThickness = Thick(C.Copper.PeakDepth);
        C.Copper.SpatialFrequency = 2.6f; C.Copper.BaseYield = 1.5f;

        C.Silicon.bPresent = true;  C.Silicon.GlobalWeight = 0.55f;
        C.Silicon.PeakDepth = -48.0 * S; C.Silicon.LayerThickness = Thick(C.Silicon.PeakDepth);
        C.Silicon.SpatialFrequency = 1.5f; C.Silicon.BaseYield = 2.0f;

        C.Titanium.bPresent = true; C.Titanium.GlobalWeight = 0.14f;
        C.Titanium.PeakDepth = -110.0 * S; C.Titanium.LayerThickness = Thick(C.Titanium.PeakDepth);
        C.Titanium.SpatialFrequency = 3.2f; C.Titanium.BaseYield = 1.0f;

        C.Uranium.bPresent = true;  C.Uranium.GlobalWeight = 0.06f;
        C.Uranium.PeakDepth = -190.0 * S; C.Uranium.LayerThickness = Thick(C.Uranium.PeakDepth);
        C.Uranium.SpatialFrequency = 4.0f; C.Uranium.BaseYield = 0.5f;

        C.Water.bPresent = true;    C.Water.GlobalWeight = 0.45f;
        C.Water.PeakDepth = -12.0 * S;   C.Water.LayerThickness = Thick(C.Water.PeakDepth);
        C.Water.SpatialFrequency = 3.0f; C.Water.BaseYield = 3.0f;

        return C;
    }

    void Init(const FGeologyConfig& C)
    {
        Config = C;
        SpatialNoise.SetSeed(Config.PlanetSeed);
    }

    FGeologySample SampleComposition(const FVector& P, double SurfaceRadius = 0.0) const
    {
        FGeologySample S;
        S.Weights.assign(size_t(EGemType::Count), 0.f);

        const double D2 = P.SizeSquared();
        if (D2 <= 1.0) return S;

        const double D = FMath::Sqrt(D2);
        const FVector N = P.GetSafeNormal();
        const double Depth = D - Config.PlanetRadius;

        double Total = 0.0;
        int32 BestIndex = 0;
        float BestWeight = 0.f;

        for (int32 i = 1; i < int32(EGemType::Count); ++i)
        {
            const EGemType G = EGemType(i);
            const FGemConfig* GC = Config.GetGemConfig(G);
            if (!GC) continue;

            const float W = ComputeWeight(*GC, N, Depth, i);
            S.Weights[i] = W;
            Total += W;
            if (W > BestWeight) { BestWeight = W; BestIndex = i; }
        }

        S.TotalWeight = float(Total);
        if (Total > KINDA_SMALL_NUMBER)
        {
            S.Purity = float(BestWeight / Total);
            S.DominantGem = EGemType(BestIndex);
            S.bWorthMining = S.TotalWeight >= Config.MiningThreshold;
        }
        (void)SurfaceRadius;
        return S;
    }

private:
    float ComputeWeight(const FGemConfig& G, const FVector& N, double Depth, int32 Idx) const
    {
        if (!G.bPresent || G.GlobalWeight <= 0.f || G.LayerThickness <= 1.0) return 0.f;

        const double Delta = Depth - G.PeakDepth;
        const double Norm = Delta / G.LayerThickness;
        const double DepthFactor = FMath::Exp(-Norm * Norm);
        if (DepthFactor <= 0.0001) return 0.f;

        double Spatial = 1.0;
        if (G.SpatialVariance > 0.001f)
        {
            const double Freq = G.SpatialFrequency * (1.0 + Idx * 0.17);
            const FVector SP(N.X, N.Y, N.Z + float(Depth / G.LayerThickness) * 0.3);
            const double Raw = SpatialNoise.GetPerlinFractal_3D(SP.X, SP.Y, SP.Z, Freq, 3);
            const double N01 = (Raw + 1.0) * 0.5;
            Spatial = 1.0 - G.SpatialVariance + G.SpatialVariance * N01;
        }
        return float(G.GlobalWeight * DepthFactor * Spatial);
    }

    FGeologyConfig Config;
    FVoxelFastNoise SpatialNoise;
};

// ── 测试 ────────────────────────────────────────────────
static int FailCount = 0;
static void Check(bool Cond, const char* What)
{
    if (Cond) { printf("  [OK]   %s\n", What); }
    else      { printf("  [FAIL] %s\n", What); ++FailCount; }
}

int main()
{
    printf("===== 地质系统验证 =====\n\n");
    FGeologySystem Geo;

    // ── 测试 1：露天矿的自然出现 ──
    printf("测试 1：露天矿（PeakDepth ≈ 0 的矿应在地表显现）\n");
    {
        // 地表点：距离略小于基准半径
        const FVector Surface(0, 0, 999.0);
        const FGeologySample S = Geo.SampleComposition(Surface, 1000.0);

        printf("         地表总权重 %.3f，主矿 %d，纯度 %.2f\n",
            S.TotalWeight, int(S.DominantGem), S.Purity);

        // 水和铁的 PeakDepth 很浅（-10 / -30），地表应该能测到
        Check(S.GetWeight(EGemType::Water) > 0.0f ||
              S.GetWeight(EGemType::Iron) > 0.0f,
              "浅层矿（铁/水）在地表有浓度");

        // 钛铀 PeakDepth 很深（-100 / -180），地表应该几乎测不到
        Check(S.GetWeight(EGemType::Titanium) < 0.05f,
              "深层矿（钛）在地表几乎为 0");
        Check(S.GetWeight(EGemType::Uranium) < 0.02f,
              "极深矿（铀）在地表几乎没有");
    }

    // ── 测试 2：深挖能找到深层矿 ──
    printf("\n测试 2：深挖能碰到深层矿\n");
    {
        // 深度 -180（铀的峰值深度附近）
        const FVector Deep(0, 0, 820.0);
        const FGeologySample S = Geo.SampleComposition(Deep, 1000.0);
        printf("         深度 -180 处总权重 %.3f，主矿 %d\n", S.TotalWeight, int(S.DominantGem));
        Check(S.TotalWeight > 0.0f, "深层有矿");
    }

    // ── 测试 3：成分确定（无随机）──
    printf("\n测试 3：采样确定性\n");
    {
        const FVector P(300, 400, 800);
        const FGeologySample A = Geo.SampleComposition(P);
        const FGeologySample B = Geo.SampleComposition(P);
        bool bSame = true;
        for (int i = 0; i < int32(EGemType::Count); ++i)
        {
            if (std::fabs(A.Weights[i] - B.Weights[i]) > 1e-9) bSame = false;
        }
        Check(bSame, "同一坐标两次采样结果完全一致");
    }

    // ── 测试 4：不同种子 → 不同布局 ──
    printf("\n测试 4：种子影响布局\n");
    {
        FGeologyConfig C1 = FGeologySystem::MakeDefaultTerrestrial();
        C1.PlanetSeed = 1337;
        FGeologyConfig C2 = FGeologySystem::MakeDefaultTerrestrial();
        C2.PlanetSeed = 9999;

        FGeologySystem G1; G1.Init(C1);
        FGeologySystem G2; G2.Init(C2);

        const FVector P(500, 300, 700);
        const FGeologySample S1 = G1.SampleComposition(P);
        const FGeologySample S2 = G2.SampleComposition(P);

        Check(std::fabs(S1.TotalWeight - S2.TotalWeight) > 1e-6f,
              "不同种子在同一位置结果不同");
    }

    // ── 测试 5：纯度范围 ──
    printf("\n测试 5：纯度与阈值\n");
    {
        bool bAllValid = true;
        int nPure = 0, nMixed = 0, nMid = 0, nSampled = 0;

        // 采样点覆盖球面上多个深度与方位，
        // 模拟玩家实际会遇到的矿区类型。
        for (int i = 0; i < 400; ++i)
        {
            const double theta = double(i % 20) / 20.0 * 2.0 * PI;
            const double phi = double(i / 20 % 20) / 20.0 * PI;
            // 半径从地表一直覆盖到地心附近
            const double r = 820.0 + (i / 400) * 160.0;
            const FVector P(
                std::sin(phi) * std::cos(theta) * r,
                std::sin(phi) * std::sin(theta) * r,
                std::cos(phi) * r);
            const FGeologySample S = Geo.SampleComposition(P);

            if (S.Purity < 0.f || S.Purity > 1.0f) bAllValid = false;
            if (S.TotalWeight <= 0.001f) continue;   // 跳过无矿区
            ++nSampled;

            if (S.Purity > 0.65f) ++nPure;
            else if (S.Purity > 0.5f) ++nMid;
            if (S.Purity < 0.5f) ++nMixed;
        }

        printf("         %d 个有矿采样点：高纯度 %d，中纯度 %d，低纯度 %d\n",
               nSampled, nPure, nMid, nMixed);
        printf("         纯度分布：");
        for (int b = 0; b <= 10; ++b)
        {
            const double Lo = b / 10.0, Hi = (b + 1) / 10.0;
            int Count = 0;
            for (int i = 0; i < 400; ++i)
            {
                const double th = double(i % 20) / 20.0 * 2.0 * PI;
                const double ph = double(i / 20 % 20) / 20.0 * PI;
                const double rr = 820.0 + (i / 400) * 160.0;
                const FVector PP(std::sin(ph) * std::cos(th) * rr,
                                 std::sin(ph) * std::sin(th) * rr,
                                 std::cos(ph) * rr);
                const FGeologySample SS = Geo.SampleComposition(PP);
                if (SS.TotalWeight <= 0.001f) continue;
                if (SS.Purity >= Lo && SS.Purity < Hi) ++Count;
            }
            printf("%d ", Count);
        }
        printf("\n         （每个数字是纯度落在 0.0/0.1/.../1.0 区间的采样点数）\n");
        Check(bAllValid, "纯度始终在 0~1 之间");
        Check(nPure > 0, "存在高纯度的纯矿点");
        Check(nSampled > nPure, "存在非纯矿点（不是所有地方都是纯矿）");
    }

    printf("\n===== 结果 =====\n");
    if (FailCount == 0) { printf("全部通过\n"); return 0; }
    printf("%d 项失败\n", FailCount);
    return 1;
}
