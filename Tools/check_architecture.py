#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
StellarEra 架构体检。

只读扫描，不改任何文件。检查四类问题：
  1. 模块依赖环（Build.cs 的 Public/Private 依赖）
  2. 规模失控（单文件 / 单头文件 / 单模块行数）
  3. 耦合广度（单个 cpp include 了多少个不同模块的头文件）
  4. 分层污染（测试文件混在业务模块、UI 反向依赖、底层模块含表现层类）

用法：
  python Tools/check_architecture.py
  python Tools/check_architecture.py --json     输出 JSON 供 CI 使用
"""

import json
import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "Source")

# 阈值：超过即视为需要处理的信号
LIMITS = {
    "file_lines": 600,        # 单个 cpp 行数
    "header_lines": 300,      # 单个 .h 行数
    "module_lines": 4000,     # 单个模块 C++ 总行数
    "include_modules": 6,     # 单个文件跨模块 include 数
}

# 表现层基类名：出现在底层模块的 Public 头里就是分层倒挂
PRESENTATION_BASENAMES = (
    "AActor", "APawn", "ACharacter", "AStaticMeshActor",
    "UStaticMeshComponent", "USkeletalMeshComponent",
)

# 这些模块应当只含数据与纯计算，Public 里出现 Actor 即视为分层倒挂
PURE_LOGIC_MODULES = {"StellarEraCore", "StellarEraGeology", "StellarEraTerrain"}


def read(path):
    for enc in ("utf-8-sig", "utf-8", "gbk"):
        try:
            with open(path, encoding=enc) as f:
                return f.read()
        except UnicodeDecodeError:
            continue
    return ""


def scan_files():
    """返回 [(module, kind, path, lines, text)]"""
    items = []
    for module in sorted(os.listdir(SOURCE)):
        mdir = os.path.join(SOURCE, module)
        if not os.path.isdir(mdir):
            continue
        for dirpath, _, filenames in os.walk(mdir):
            for fn in filenames:
                if not fn.endswith((".h", ".cpp")):
                    continue
                path = os.path.join(dirpath, fn)
                text = read(path)
                kind = "header" if fn.endswith(".h") else "source"
                items.append((module, kind, path, text.count("\n") + 1, text))
    return items


def parse_dependencies():
    """解析每个模块 Build.cs 的项目内依赖：{module: set(modules)}"""
    deps = {}
    for module in sorted(os.listdir(SOURCE)):
        mdir = os.path.join(SOURCE, module)
        if not os.path.isdir(mdir):
            continue
        build = os.path.join(mdir, f"{module}.Build.cs")
        if not os.path.isfile(build):
            continue
        text = read(build)
        found = set()
        for dep in re.findall(r'"(StellarEra\w*)"', text):
            if dep != module:
                found.add(dep)
        deps[module] = found
    return deps


def find_cycles(deps):
    """DFS 找依赖环"""
    cycles, state, stack = [], {}, []

    def dfs(node):
        state[node] = 1
        stack.append(node)
        for nxt in deps.get(node, ()):
            if nxt not in deps:
                continue
            if state.get(nxt, 0) == 1:
                i = stack.index(nxt)
                cycles.append(stack[i:] + [nxt])
            elif state.get(nxt, 0) == 0:
                dfs(nxt)
        stack.pop()
        state[node] = 2

    for node in deps:
        if state.get(node, 0) == 0:
            dfs(node)
    return cycles


def build_header_index(files):
    """{头文件名: 所属模块}，用于把 include 反查成模块依赖。"""
    index = {}
    for module, kind, path, _, _ in files:
        if kind == "header":
            index[os.path.basename(path)] = module
    return index


def compute_depth(deps):
    """每个模块到叶子节点的最长路径长度，即它在依赖图里的层号。"""
    memo = {}

    def depth_of(node, path=()):
        if node in memo:
            return memo[node]
        if node in path:
            return 0
        children = [d for d in deps.get(node, ()) if d in deps]
        val = 0 if not children else 1 + max(depth_of(c, path + (node,)) for c in children)
        memo[node] = val
        return val

    for node in deps:
        depth_of(node)
    return memo


def print_tree(files, deps):
    """按依赖深度分层打印，回答「这些模块谁在上谁在下」。"""
    depth = compute_depth(deps)
    lines, files_n = defaultdict(int), defaultdict(int)
    for module, _, _, n, _ in files:
        lines[module] += n
        files_n[module] += 1

    reverse = defaultdict(set)
    for m, ds in deps.items():
        for d in ds:
            reverse[d].add(m)

    print(f"\nSource/  逻辑分层（层号 = 依赖深度，不是目录结构）\n")
    total = sum(lines.values())
    for layer in sorted(set(depth.values())):
        members = sorted((m for m in depth if depth[m] == layer), key=lambda m: -lines[m])
        print(f"L{layer}")
        for i, m in enumerate(members):
            last = i == len(members) - 1
            mark = "└──" if last else "├──"
            used_by = len(reverse.get(m, ()))
            tag = f"被 {used_by} 个依赖" if used_by else "无人依赖（顶层）"
            print(f"  {mark} {m:<24}{lines[m]:>6} 行  {tag}")
        print()
    print(f"{len(depth)} 个模块 · {total} 行\n")


def main():
    as_json = "--json" in sys.argv
    files = scan_files()
    deps = parse_dependencies()

    if "--tree" in sys.argv:
        print_tree(files, deps)
        return 0

    header_index = build_header_index(files)

    module_lines = defaultdict(int)
    module_files = defaultdict(int)
    module_test_lines = defaultdict(int)
    for module, kind, path, lines, _ in files:
        module_lines[module] += lines
        module_files[module] += 1
        if "Tests" in os.path.basename(path):
            module_test_lines[module] += lines

    report = {
        "modules": {},
        "cycles": find_cycles(deps),
        "oversize_files": [],
        "oversize_headers": [],
        "test_in_business": [],
        "wide_coupling": [],
        "layer_leaks": [],
    }

    for module in sorted(module_lines):
        report["modules"][module] = {
            "files": module_files[module],
            "lines": module_lines[module],
            "test_lines": module_test_lines[module],
            "business_lines": module_lines[module] - module_test_lines[module],
            "deps": sorted(deps.get(module, [])),
        }

    for module, kind, path, lines, text in files:
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        base = os.path.basename(path)

        if kind == "source" and lines > LIMITS["file_lines"]:
            report["oversize_files"].append((rel, lines))
        if kind == "header" and lines > LIMITS["header_lines"]:
            report["oversize_headers"].append((rel, lines))

        # 测试文件应当独立成 Tests 模块
        if "Tests" in base and "Tests" not in module:
            report["test_in_business"].append(rel)

        # 跨模块 include 广度：按被 include 头文件的归属模块统计
        if kind == "source":
            hits = set()
            for inc in re.findall(r'#include\s+"([^"]+\.h)"', text):
                owner = header_index.get(os.path.basename(inc))
                if owner and owner != module:
                    hits.add(owner)
            if len(hits) >= LIMITS["include_modules"]:
                report["wide_coupling"].append((rel, sorted(hits)))

        # 底层模块的 Public 头暴露表现层基类（表现层模块暴露 Actor 属正常）
        if (kind == "header" and module in PURE_LOGIC_MODULES
                and "/Public/" in path.replace("\\", "/")):
            if any(f": public {b}" in text for b in PRESENTATION_BASENAMES):
                report["layer_leaks"].append((rel, module))

    report["oversize_files"].sort(key=lambda x: -x[1])
    report["oversize_headers"].sort(key=lambda x: -x[1])
    report["wide_coupling"].sort(key=lambda x: -len(x[1]))

    if as_json:
        print(json.dumps(report, ensure_ascii=False, indent=2))
        return 1 if (report["cycles"] or report["layer_leaks"]) else 0

    # ---- 人类可读输出 ----
    total = sum(module_lines.values())
    print(f"\nStellarEra 架构体检    {len(module_lines)} 个模块 / "
          f"{sum(module_files.values())} 个 C++ 文件 / {total} 行\n")

    print("── 模块规模与依赖 ─────────────────────────────────────")
    print(f"{'模块':<24}{'文件':>5}{'业务行':>8}{'测试行':>8}  依赖")
    for m, info in sorted(report["modules"].items(), key=lambda kv: -kv[1]["lines"]):
        flag = " *" if info["lines"] > LIMITS["module_lines"] else ""
        dep = ", ".join(d.replace("StellarEra", "") for d in info["deps"]) or "—"
        print(f"{m:<24}{info['files']:>5}{info['business_lines']:>8}"
              f"{info['test_lines']:>8}{flag}  {dep}")

    def section(title, rows, fmt):
        print(f"\n── {title} ─────────────────────────────────────")
        if not rows:
            print("  无")
            return
        for r in rows:
            print("  " + fmt(r))

    section("依赖环", report["cycles"], lambda c: " → ".join(c))
    section(f"超大源文件（>{LIMITS['file_lines']} 行）",
            report["oversize_files"], lambda x: f"{x[1]:>5} 行  {x[0]}")
    section(f"超大头文件（>{LIMITS['header_lines']} 行）",
            report["oversize_headers"], lambda x: f"{x[1]:>5} 行  {x[0]}")
    section("测试混杂在业务模块", report["test_in_business"], lambda x: x)
    section(f"跨模块 include ≥{LIMITS['include_modules']} 个",
            report["wide_coupling"],
            lambda x: f"{len(x[1])} 个模块  {x[0]}")
    section("Public 头暴露表现层基类（分层倒挂）",
            report["layer_leaks"], lambda x: f"{x[1]:<22}{x[0]}")

    issues = (len(report["cycles"]) + len(report["layer_leaks"])
              + len(report["oversize_files"]) + len(report["wide_coupling"]))
    print(f"\n合计 {issues} 项结构性信号。\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
