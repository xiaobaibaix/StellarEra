#!/usr/bin/env python3
"""用磁盘实际结构替换 docs/TREE_AFTER.md 里 StellarEraPlanet 的 Public/Private 历史树。

那份树是取消 Public/Private 分离之前写的，已经和代码对不上。留着的唯一后果
是误导，所以直接换成当前 Src 结构。
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "docs" / "TREE_AFTER.md"
PLANET = ROOT / "Source" / "StellarEraPlanet" / "Src"

START = "### StellarEraPlanet（原 Terrain + Atmosphere + Minerals + PlanetEdit）"
END = "### StellarEraPawns（原 Character + Spaceship）"


def collect(directory: Path) -> list[str]:
    """目录下的 .h/.cpp 文件名，按基名分组，同名头源合并成一行。"""
    stems: dict[str, list[str]] = {}
    for f in sorted(directory.iterdir()):
        if f.suffix in (".h", ".cpp") and f.is_file():
            stems.setdefault(f.stem, []).append(f.suffix)
    lines = []
    for stem, exts in stems.items():
        tag = "/".join(sorted((e.lstrip(".") for e in exts), key=lambda x: (x != "h", x)))
        lines.append(f"{stem}.{tag}")
    return lines


def build() -> str:
    out = [
        START,
        "",
        "> 这一节已于 2026-10-31 按实际磁盘结构重写。原先的 `Public/` `Private/` 双树",
        "> 是取消分层之前的记录，已删除 —— 保留只会误导。",
        "",
        "```",
        "StellarEraPlanet/",
        "├── StellarEraPlanet.Build.cs",
        "├── StellarEraPlanet.h / .cpp              模块入口（与 Build.cs 并排）",
        "└── Src/",
    ]

    groups = [("", "星球本体（模块门面，放 Src 根）")]
    for d in sorted(PLANET.iterdir()):
        if d.is_dir():
            groups.append((d.name, ""))

    for i, (name, note) in enumerate(groups):
        last = i == len(groups) - 1
        branch = "    └── " if last else "    ├── "
        files = collect(PLANET / name) if name else collect(PLANET)
        if not name:
            out.append(f"{branch}VoxelPlanetActor.h/.cpp         {note}")
            continue
        out.append(f"{branch}{name}/")
        sub = "        " if last else "    │   "
        for j, f in enumerate(files):
            out.append(f"{sub}{'└── ' if j == len(files) - 1 else '├── '}{f}")

    while out and out[-1] == "":
        out.pop()
    out += [
        "```",
        "",
        "**`Invokers/` 不在 `Terrain/` 里的理由**：那 4 个文件被 `StellarEraPawns`",
        "（角色、飞船）和 `StellarEraUI`（星图相机）跨模块 include，它们是 Voxel 插件",
        "LOD 的接线层，不是地形本身。判据看 include 关系，不看文件名前缀。",
        "",
        "`VoxelPlanetTerrainModule.h` 名字里带 Module 容易和模块入口混淆，但它是 `UDataAsset`",
        "基类，**不改名**——改文件名要动一堆 include，收益不抵成本。",
        "",
    ]
    return "\n".join(out)


def main() -> int:
    text = DOC.read_text(encoding="utf-8")
    i = text.index(START)
    j = text.index(END)
    new = text[:i] + build() + "\n" + text[j:]
    DOC.write_text(new, encoding="utf-8")
    print(f"已替换 TREE_AFTER.md 第 {text[:i].count(chr(10)) + 1} 行起的 Planet 一节")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
