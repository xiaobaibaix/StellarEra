# -*- coding: utf-8 -*-
"""把 StellarEraCore 拆掉，内容并入 Cosmos 与 Planet。

Core 模块总共 2687 行，其中 N 体求解器占 2411 行（90%），
剩下的是一个共享枚举头和一个纯虚接口。为一个求解器单开一个模块
不划算，于是：

    NBodySimulator.h/.cpp + Tests  → StellarEraCosmos/Src/   （和它的唯一使用方同住）
    CosmosTypes.h                  → StellarEraCosmos/Src/   （本来就是 Cosmos 的类型）
    VoxelViewpointInvokers.h       → StellarEraPlanet/Src/Invokers/（体素 LOD 接线层）

同时把 Planet 里同名但不同职责的 VoxelViewpointInvokers.cpp 改名为
VoxelViewpointInvokerUtils.cpp，与它的声明头 VoxelViewpointInvokerUtils.h 配对 ——
否则新搬进去的接口头和它撞名，看着像一对却毫不相干。
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Source"

# (源, 目标)
MOVES = [
    ("StellarEraCore/Src/NBodySimulator.h", "StellarEraCosmos/Src/NBodySimulator.h"),
    ("StellarEraCore/Src/NBodySimulator.cpp", "StellarEraCosmos/Src/NBodySimulator.cpp"),
    ("StellarEraCore/Src/Tests/NBodySimulatorTests.cpp", "StellarEraCosmos/Src/Tests/NBodySimulatorTests.cpp"),
    ("StellarEraCore/Src/CosmosTypes.h", "StellarEraCosmos/Src/CosmosTypes.h"),
    ("StellarEraCore/Src/Interfaces/VoxelViewpointInvokers.h", "StellarEraPlanet/Src/Invokers/VoxelViewpointInvokers.h"),
]

# 先改名再搬，避免与新搬进来的接口头撞名
RENAMES = [
    ("StellarEraPlanet/Src/Invokers/VoxelViewpointInvokers.cpp",
     "StellarEraPlanet/Src/Invokers/VoxelViewpointInvokerUtils.cpp"),
]


def git_mv(src: Path, dst: Path) -> None:
    dst.parent.mkdir(parents=True, exist_ok=True)
    r = subprocess.run(["git", "mv", str(src), str(dst)], cwd=ROOT,
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"  !! git mv 失败: {src} -> {dst}\n     {r.stderr.strip()}")
        sys.exit(1)
    print(f"  {src.relative_to(SRC)}  ->  {dst.relative_to(SRC)}")


def main() -> None:
    print("== 1. 改名（避免撞名） ==")
    for s, d in RENAMES:
        sp, dp = SRC / s, SRC / d
        if not sp.exists():
            print(f"  !! 不存在: {s}")
            sys.exit(1)
        if dp.exists():
            print(f"  !! 目标已存在: {d}")
            sys.exit(1)
        git_mv(sp, dp)

    print("\n== 2. 搬迁 ==")
    for s, d in MOVES:
        sp, dp = SRC / s, SRC / d
        if not sp.exists():
            print(f"  !! 不存在: {s}")
            sys.exit(1)
        if dp.exists():
            print(f"  !! 目标已存在: {d}")
            sys.exit(1)
        git_mv(sp, dp)

    print("\n== 3. 删除 Core 模块 ==")
    core = SRC / "StellarEraCore"
    leftovers = [p for p in core.rglob("*") if p.is_file()]
    if leftovers:
        print("  !! 仍有残留文件，先处理：")
        for p in leftovers:
            print(f"     {p.relative_to(SRC)}")
        sys.exit(1)
    for p in sorted(core.rglob("*"), reverse=True):
        if p.is_dir():
            p.rmdir()
    core.rmdir()
    print("  StellarEraCore/ 已删除")

    print("\n== 4. 换导出宏 ==")
    repl = {
        "NBodySimulator.h": ("STELLARERACORE_API", "STELLARERACOSMOS_API"),
        "VoxelViewpointInvokers.h": ("STELLARERACORE_API", "STELLARERAPLANET_API"),
    }
    for name, (old, new) in repl.items():
        hits = list(SRC.rglob(name))
        for p in hits:
            t = p.read_text(encoding="utf-8", errors="ignore")
            n = t.count(old)
            if n:
                p.write_text(t.replace(old, new), encoding="utf-8")
                print(f"  {p.relative_to(SRC)}  {old} -> {new}  ({n} 处)")
    remain = subprocess.run(["git", "grep", "-l", "STELLARERACORE_API"],
                            cwd=ROOT, capture_output=True, text=True)
    print("  残留检查: " + (remain.stdout.strip() or "无"))


if __name__ == "__main__":
    main()
