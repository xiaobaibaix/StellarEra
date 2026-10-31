"""
把 StellarEraGeology 整个模块并入 StellarEraPlanet/Src/Minerals/。

落位（用户选定：矿物域一个目录）：
    Minerals/Geology/        元素表 · 地质场 · 预设       5 个文件
    Minerals/Distributions/  矿脉形态基类 + 四变体        10 个文件
    Minerals/Surface/        矿物表面几何 · 材质构建       4 个文件
    Minerals/View/           观测 Actor · 观测组件         4 个文件

Geology 的测试并入 Planet/Src/Tests/（那一层已有 12 个测试）。

不搬就走不通的原因：PlanetGeologyField / PlanetGeologyPreset 必须 include
PlanetGeologyDistribution，若只搬 Distributions，Geology 就要反向依赖 Planet，
与 Planet → Geology 构成模块环，UBT 直接报 circular dependency。

调 git mv 而不是 os.rename：保留重命名检测，历史能跟上。
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Source"
GEO = SRC / "StellarEraGeology"
PLANET = SRC / "StellarEraPlanet"
MIN = PLANET / "Src" / "Minerals"

MOVES = [
    # 数据层：元素表、地质场、预设
    (GEO / "Src" / "PlanetGeologyTypes.h", MIN / "Geology"),
    (GEO / "Src" / "PlanetGeologyField.h", MIN / "Geology"),
    (GEO / "Src" / "PlanetGeologyField.cpp", MIN / "Geology"),
    (GEO / "Src" / "PlanetGeologyPreset.h", MIN / "Geology"),
    (GEO / "Src" / "PlanetGeologyPreset.cpp", MIN / "Geology"),

    # 形态层
    (GEO / "Src" / "Distributions" / "PlanetGeologyDistribution.h", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyDistribution.cpp", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologySpherePodsDistribution.h", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologySpherePodsDistribution.cpp", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyVeinDistribution.h", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyVeinDistribution.cpp", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyLensDistribution.h", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyLensDistribution.cpp", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyDisseminatedDistribution.h", MIN / "Distributions"),
    (GEO / "Src" / "Distributions" / "PlanetGeologyDisseminatedDistribution.cpp", MIN / "Distributions"),

    # 测试并入 Planet 的统一测试目录
    (GEO / "Src" / "Tests" / "PlanetGeologyDistributionTests.cpp", PLANET / "Src" / "Tests"),

    # 表现层重排：表面与材质、观测视图
    (MIN / "PlanetMineralSurface.h", MIN / "Surface"),
    (MIN / "PlanetMineralSurface.cpp", MIN / "Surface"),
    (MIN / "PlanetMineralMaterialBuilder.h", MIN / "Surface"),
    (MIN / "PlanetMineralMaterialBuilder.cpp", MIN / "Surface"),
    (MIN / "PlanetMineralViewActor.h", MIN / "View"),
    (MIN / "PlanetMineralViewActor.cpp", MIN / "View"),
    (MIN / "PlanetMineralViewComponent.h", MIN / "View"),
    (MIN / "PlanetMineralViewComponent.cpp", MIN / "View"),
]

# 搬完即删的模块外壳（模块不存在了，入口文件也没有意义）
OBSOLETE = [
    GEO / "StellarEraGeology.Build.cs",
    GEO / "StellarEraGeology.h",
    GEO / "StellarEraGeology.cpp",
]


def main():
    missing = [str(src) for src, _ in MOVES if not src.exists()]
    if missing:
        print("源文件不存在，中止：")
        for item in missing:
            print("  " + item)
        return 1

    for src, dst_dir in MOVES:
        dst_dir.mkdir(parents=True, exist_ok=True)
        dst = dst_dir / src.name
        if dst.exists():
            print(f"目标已存在，中止：{dst}")
            return 1
        subprocess.run(
            ["git", "mv", str(src), str(dst)],
            cwd=ROOT, check=True,
        )
        print(f"  {src.relative_to(SRC)}  ->  {dst.relative_to(SRC)}")

    for path in OBSOLETE:
        if path.exists():
            subprocess.run(["git", "rm", "-q", str(path)], cwd=ROOT, check=True)
            print(f"  删除 {path.relative_to(SRC)}")

    # 剩余空目录（Src/Distributions、Src/Tests、Src）清掉
    for leftover in sorted(GEO.rglob("*"), reverse=True):
        if leftover.is_dir():
            try:
                leftover.rmdir()
            except OSError:
                pass
    if GEO.exists():
        try:
            GEO.rmdir()
            print("  删除空目录 Source/StellarEraGeology")
        except OSError as error:
            print(f"  警告：模块目录未清空 {error}")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
