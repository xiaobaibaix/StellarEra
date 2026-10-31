"""把各模块的 Public/ 与 Private/ 合并为 Src/，头文件与源文件同目录并列。

规则：
  Public/<X>   -> Src/<X>
  Private/<X>  -> Src/<X>
  模块根 <X>   -> Src/<X>      （模块入口 StellarEraXxx.h/.cpp）

UBT 会递归收集模块目录下的 .cpp（Configuration/UEBuildModuleCPP.cs:2978），
所以 Src/ 里的源文件无需登记；只有**包含路径**需要显式声明，见各 Build.cs。

幂等：目标已存在且源不存在时跳过，可重复执行。
"""

import os
import subprocess
import sys

REPO = r"F:\workspace\ue\StellarEra-refactor"
SRC = os.path.join(REPO, "Source")

# Core 的 Interfaces/ 只有一个契约头，拍平到 Src/ 根，避免单文件目录
FLATTEN = {
    "StellarEraCore/Interfaces/VoxelViewpointInvokers.h",
}

SKIP_DIRS = {"Src", "Binaries", "Intermediate"}


def git(*args):
    subprocess.run(["git"] + list(args), cwd=REPO, check=True)
    return True


def main():
    modules = sorted(
        d for d in os.listdir(SRC)
        if os.path.isdir(os.path.join(SRC, d))
        and os.path.isfile(os.path.join(SRC, d, d + ".Build.cs"))
    )

    moved = 0
    skipped = 0
    for module in modules:
        mroot = os.path.join(SRC, module)
        for root, dirs, files in os.walk(mroot):
            dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
            for f in sorted(files):
                if not (f.endswith(".h") or f.endswith(".cpp")):
                    continue
                abspath = os.path.join(root, f)
                rel = os.path.relpath(abspath, mroot).replace("\\", "/")

                if rel in FLATTEN:
                    rel2 = os.path.basename(rel)
                elif rel.startswith("Public/"):
                    rel2 = rel[len("Public/"):]
                elif rel.startswith("Private/"):
                    rel2 = rel[len("Private/"):]
                else:
                    rel2 = rel

                dest_rel = module + "/Src/" + rel2
                dest_abs = os.path.join(REPO, "Source", dest_rel.replace("/", os.sep))
                src_rel = "Source/" + module + "/" + rel

                if os.path.exists(dest_abs):
                    skipped += 1
                    continue

                os.makedirs(os.path.dirname(dest_abs), exist_ok=True)
                git("mv", src_rel, "Source/" + dest_rel)
                moved += 1

    print(f"移动 {moved} 个文件，跳过 {skipped} 个（已存在）")

    # 清理空的 Public / Private / Interfaces 目录
    removed = 0
    for module in modules:
        for name in ("Public", "Private", "Interfaces"):
            p = os.path.join(SRC, module, name)
            if os.path.isdir(p):
                for root, dirs, files in os.walk(p, topdown=False):
                    if not os.listdir(root):
                        os.rmdir(root)
                        removed += 1
    print(f"删除 {removed} 个空目录")


if __name__ == "__main__":
    sys.exit(main())
