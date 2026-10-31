#!/usr/bin/env python3
"""把 Planet/Terrain 的 18 个文件按职责三分，把 Geology/Src 的 15 个文件拆出 Distributions/。

判据：一个目录里存在两类以上职责就拆，拆完每族 >=4 个文件才值得建子目录。
include 全是扁平写法、Build.cs 递归加目录，所以只搬文件不改代码。
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "Source"

PLANET = SRC / "StellarEraPlanet" / "Src"
GEOLOGY = SRC / "StellarEraGeology" / "Src"

MOVES = [
    # ── Planet/Terrain → Invokers/ ：体素 LOD 接线，被 Pawns 与 UI 跨模块引用 ──
    (PLANET / "Terrain" / "AlwaysLocalVoxelInvokerComponent.h", PLANET / "Invokers"),
    (PLANET / "Terrain" / "AlwaysLocalVoxelInvokerComponent.cpp", PLANET / "Invokers"),
    (PLANET / "Terrain" / "VoxelViewpointInvokers.cpp", PLANET / "Invokers"),
    (PLANET / "Terrain" / "VoxelViewpointInvokerUtils.h", PLANET / "Invokers"),

    # ── Planet/Terrain → Surface/ ：球面几何与编辑辅助网格 ──
    (PLANET / "Terrain" / "PlanetSurfaceRay.h", PLANET / "Surface"),
    (PLANET / "Terrain" / "PlanetSurfaceRay.cpp", PLANET / "Surface"),
    (PLANET / "Terrain" / "PlanetSurfaceGridComponent.h", PLANET / "Surface"),
    (PLANET / "Terrain" / "PlanetSurfaceGridComponent.cpp", PLANET / "Surface"),

    # ── Geology/Src → Distributions/ ：分布基类与 4 个变体 ──
    (GEOLOGY / "PlanetGeologyDistribution.h", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyDistribution.cpp", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyDisseminatedDistribution.h", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyDisseminatedDistribution.cpp", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyLensDistribution.h", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyLensDistribution.cpp", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologySpherePodsDistribution.h", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologySpherePodsDistribution.cpp", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyVeinDistribution.h", GEOLOGY / "Distributions"),
    (GEOLOGY / "PlanetGeologyVeinDistribution.cpp", GEOLOGY / "Distributions"),
]


def main() -> int:
    missing = [str(src) for src, _ in MOVES if not src.exists()]
    if missing:
        print("以下文件不存在，脚本中止：", file=sys.stderr)
        for m in missing:
            print("  " + m, file=sys.stderr)
        return 1

    for dst_dir in {dst for _, dst in MOVES}:
        dst_dir.mkdir(parents=True, exist_ok=True)

    for src, dst_dir in MOVES:
        dst = dst_dir / src.name
        subprocess.run(
            ["git", "mv", str(src.relative_to(ROOT)), str(dst.relative_to(ROOT))],
            cwd=ROOT, check=True,
        )
        print(f"{src.relative_to(SRC)}  →  {dst.relative_to(SRC)}")

    print(f"\n共移动 {len(MOVES)} 个文件。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
