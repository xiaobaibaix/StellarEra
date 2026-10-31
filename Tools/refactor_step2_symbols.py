"""模块合并第二步：统一导出宏，并补齐接口拆分后的 include。

全部按字节替换，不动 BOM、不动 CRLF —— UE 源码在 Windows 上对这些敏感。
"""

import os
import sys

REPO = r"F:/workspace/ue/StellarEra-refactor"
SRC = os.path.join(REPO, "Source")

# 宏重命名：合并后同一模块只留一个导出宏
MACROS = [
    (b"STELLARERATERRAIN_API",    b"STELLARERAPLANET_API"),
    (b"STELLARERAATMOSPHERE_API", b"STELLARERAPLANET_API"),
    (b"STELLARERAMINERALS_API",   b"STELLARERAPLANET_API"),
    (b"STELLARERAPLANETEDIT_API", b"STELLARERAPLANET_API"),
    (b"STELLARERACHARACTER_API",  b"STELLARERAPAWNS_API"),
    (b"STELLARERASPACESHIP_API",  b"STELLARERAPAWNS_API"),
]

# StarMapCameraPawn 从 UI 搬到 Pawns，它一个头要单独换宏；
# UI 其余 5 处 STELLARERAUI_API 保持不动。
SINGLE_FILE_MACROS = [
    ("StellarEraPawns/Public/Camera/StarMapCameraPawn.h",
     [(b"STELLARERAUI_API", b"STELLARERAPAWNS_API")]),
]


def read_bytes(p):
    with open(p, "rb") as f:
        return f.read()


def write_bytes(p, data):
    with open(p, "wb") as f:
        f.write(data)


def walk(root, exts=(".h", ".cpp")):
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.endswith(exts):
                yield os.path.join(dirpath, n)


def main():
    changed = {}

    # 1. 全局换宏
    for p in walk(SRC):
        data = read_bytes(p)
        orig = data
        for old, new in MACROS:
            if old in data:
                data = data.replace(old, new)
                changed[old.decode()] = changed.get(old.decode(), 0) + 1
        if data != orig:
            write_bytes(p, data)

    print("宏替换（按文件计次）：")
    for k, v in sorted(changed.items()):
        print(f"  {k:<28} → {v} 个文件")

    # 2. 单文件换宏
    for rel, pairs in SINGLE_FILE_MACROS:
        p = os.path.join(SRC, rel)
        data = read_bytes(p)
        for old, new in pairs:
            data = data.replace(old, new)
        write_bytes(p, data)
        print(f"单文件替换：{rel}")

    # 3. VoxelViewpointInvokers.cpp 改为 include 拆分后的工具头
    impl = os.path.join(SRC, "StellarEraPlanet/Private/Terrain/VoxelViewpointInvokers.cpp")
    data = read_bytes(impl)
    data = data.replace(
        b'#include "VoxelViewpointInvokers.h"',
        b'#include "VoxelViewpointInvokerUtils.h"',
    )
    write_bytes(impl, data)
    print("实现文件改为 include VoxelViewpointInvokerUtils.h")

    # 4. 用了 SetEnabled 但还没 include 工具头的源文件，补一行
    utils_inc = b'#include "VoxelViewpointInvokerUtils.h"'
    for p in walk(SRC):
        if not p.endswith(".cpp"):
            continue
        data = read_bytes(p)
        if b"VoxelViewpointInvokers::SetEnabled" not in data:
            continue
        if b"VoxelViewpointInvokerUtils.h" in data:
            continue
        if b'#include "VoxelViewpointInvokers.h"' in data:
            data = data.replace(
                b'#include "VoxelViewpointInvokers.h"',
                b'#include "VoxelViewpointInvokers.h"\r\n' + utils_inc,
            )
        else:
            idx = data.rfind(b'#include "')
            eol = data.find(b"\r\n", idx)
            if eol < 0:
                eol = data.find(b"\n", idx)
            data = data[:eol] + b"\r\n" + utils_inc + data[eol:]
        write_bytes(p, data)
        print(f"补 include：{os.path.relpath(p, REPO)}")


if __name__ == "__main__":
    main()
