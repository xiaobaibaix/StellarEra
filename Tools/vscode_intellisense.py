#!/usr/bin/env python3
"""把 UBT 编译参数回填进 .vscode/c_cpp_properties.json，修复头文件 IntelliSense。

背景（UE 5.8 / UBT 的 VSCode 生成器硬编码行为）：
    Source/Programs/UnrealBuildTool/ProjectFiles/VisualStudioCode/VSCodeProjectFileGenerator.cs
    WriteCompileCommands() 里 SourceFilesForClangCompilationDatabase 只挑 .cpp/.cc/.cxx/.c，
    **头文件永远不会进 compileCommands_*.json**。
    cpptools 对「数据库里查不到的文件」回退到 c_cpp_properties 的 includePath / forcedInclude /
    defines，而 UBT 生成的这份 c_cpp_properties 只有 compileCommands 一个字段 ——
    于是每个 .h 都拿不到任何 -I，第一行 #include 就报「无法打开源文件」。

本脚本做的事：
    1. 从 compileCommands_<target>.json 引用的 .rsp 里抽出全部 /I（包含目录）与 /FI（强制包含头）
    2. 去重、剔除不存在的路径、项目自身的目录排前面
    3. 写回 c_cpp_properties.json 的每个 configuration

注意：/D 宏在 UE5 里并不出现在命令行上，而是 UBT 生成的
    Intermediate/Build/<plat>/<arch>/UnrealEditor/<Config>/<Module>/Definitions.<Module>.h
被 /FI 强制包含。所以 forcedInclude 必须带上它，否则 WITH_EDITOR 之类的宏全丢，
头文件会换个姿势继续报错。

用法：
    python Tools/vscode_intellisense.py                # 回填（默认 target StellarEra）
    python Tools/vscode_intellisense.py --shared-pch   # 额外强制包含 SharedPCH（更准但更慢）
    python Tools/vscode_intellisense.py --target StellarEra
每次跑完 UBT -ProjectFiles 之后都要再跑一次本脚本（UBT 会把 c_cpp_properties.json 覆盖回原样）。
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VSCODE_DIR = os.path.join(PROJECT_ROOT, ".vscode")

RE_INCLUDE = re.compile(r'/I\s*"([^"]*)"')
RE_FORCED = re.compile(r'/FI\s*"([^"]*)"')


def collect_rsps(db_path: str) -> list[str]:
    """compileCommands 里每个条目的 arguments 只有 [cl.exe, @xxx.rsp]，真正的参数在 rsp 里。"""
    with open(db_path, encoding="utf-8") as f:
        entries = json.load(f)
    rsps: list[str] = []
    seen = set()
    for entry in entries:
        for arg in entry.get("arguments", []):
            if arg.startswith("@"):
                path = arg[1:]
                if path not in seen:
                    seen.add(path)
                    rsps.append(path)
    return rsps


def collect_flags(rsps: list[str]) -> tuple[list[str], list[str]]:
    includes: list[str] = []
    forced: list[str] = []
    seen_i, seen_f = set(), set()
    for rsp in sorted(rsps):
        if not os.path.isfile(rsp):
            continue
        with open(rsp, encoding="utf-8", errors="replace") as f:
            text = f.read()
        for path in RE_INCLUDE.findall(text):
            key = path.lower()
            if key not in seen_i:
                seen_i.add(key)
                includes.append(path)
        for path in RE_FORCED.findall(text):
            key = path.lower()
            if key not in seen_f:
                seen_f.add(key)
                forced.append(path)
    return includes, forced


def order_and_filter(includes: list[str]) -> list[str]:
    """项目/插件目录优先（F:\\ 盘上的），引擎目录在后；顺手剔掉已不存在的路径。"""
    project = [p for p in includes if os.path.isdir(p) and not p.lower().startswith("d:\\")]
    engine = [p for p in includes if os.path.isdir(p) and p.lower().startswith("d:\\")]
    return project + engine


def pick_forced(forced: list[str], target: str, want_shared_pch: bool) -> list[str]:
    """只强制包含**一份** Definitions 头；SharedPCH 默认不加（会明显拖慢 IntelliSense）。

    不能把 13 个模块的 Definitions 全塞进 forcedInclude：它们互相 #undef / #define
    同一批 *_API 与 UE_MODULE_NAME，叠加只会让宏定义互相打架。
    任取一份即可 —— 它内部 #include 的 SharedDefinitions.*.h 已经把全工程模块的
    *_API 都定义成 DLLIMPORT 了。
    """
    defs = [p for p in forced if os.path.basename(p).lower().startswith("definitions.")]
    shared = [p for p in forced if os.path.basename(p).lower().startswith("sharedpch.")]

    defs = [p for p in defs if os.path.isfile(p)]
    defs.sort(key=lambda p: 0 if f"definitions.{target.lower()}.h" in p.lower() else 1)
    picked = defs[:1]

    if want_shared_pch:
        picked += [p for p in shared if os.path.isfile(p)][:1]
    return picked


def patch_properties(props_path: str, include_path: list[str], forced_include: list[str]) -> int:
    with open(props_path, encoding="utf-8") as f:
        props = json.load(f)
    configs = props.get("configurations", [])
    for cfg in configs:
        cfg["includePath"] = include_path
        cfg["forcedInclude"] = forced_include
    with open(props_path, "w", encoding="utf-8") as f:
        json.dump(props, f, indent=4, ensure_ascii=False)
        f.write("\n")
    return len(configs)


def main() -> int:
    ap = argparse.ArgumentParser(description="回填 UE 的 includePath / forcedInclude 到头文件 IntelliSense 配置")
    ap.add_argument("--target", default="StellarEra", help="编译数据库的目标名（默认 StellarEra）")
    ap.add_argument("--shared-pch", action="store_true", help="额外强制包含 SharedPCH（默认关闭）")
    args = ap.parse_args()

    db_path = os.path.join(VSCODE_DIR, f"compileCommands_{args.target}.json")
    props_path = os.path.join(VSCODE_DIR, "c_cpp_properties.json")
    for path in (db_path, props_path):
        if not os.path.isfile(path):
            print(f"[x] 找不到 {path}\n    先跑一次 UBT -ProjectFiles -VSCode", file=sys.stderr)
            return 1

    rsps = collect_rsps(db_path)
    includes, forced = collect_flags(rsps)
    include_path = order_and_filter(includes)
    forced_include = pick_forced(forced, args.target, args.shared_pch)
    cfg_count = patch_properties(props_path, include_path, forced_include)

    print(f"rsp 文件      : {len(rsps)}")
    print(f"include 目录  : {len(includes)} -> 写入 {len(include_path)}（剔除不存在的）")
    print(f"forcedInclude : {len(forced_include)}")
    for path in forced_include:
        print(f"    {path}")
    print(f"已更新 {cfg_count} 个 configuration -> {os.path.relpath(props_path, PROJECT_ROOT)}")
    print("接着在 VS Code 执行 C/C++: Reset IntelliSense Database（或重载窗口）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
