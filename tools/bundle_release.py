#!/usr/bin/env python3
"""Copy non-system shared libraries into <prefix>/lib and rewrite rpaths.

Makes `cmake --install` prefixes relocatable for GitHub release tarballs.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

LINUX_SKIP_PREFIXES = (
    "libc.so",
    "libm.so",
    "libdl.so",
    "libpthread.so",
    "librt.so",
    "libutil.so",
    "ld-linux",
    "libgcc_s",
    "libstdc++",
    "libGL.so",
    "libOpenGL.so",
    "libGLdispatch",
    "libGLX",
    "libEGL.so",
    "libX11",
    "libXext",
    "libXrandr",
    "libXinerama",
    "libXcursor",
    "libXi.",
    "libXrender",
    "libXfixes",
    "libxcb",
    "libXau",
    "libXdmcp",
    "libwayland",
    "libxkbcommon",
    "libdecor",
    "libdrm",
    "libgbm",
    "libz.so",
    "libresolv",
    "libbsd",
    "linux-vdso",
)


def run(cmd: list[str], check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, check=check, text=True, capture_output=True)


def linux_skip(name: str) -> bool:
    base = os.path.basename(name)
    return any(base.startswith(p) or p in base for p in LINUX_SKIP_PREFIXES)


def macos_system(path: str) -> bool:
    if path.startswith("@") or path.startswith("/usr/lib/") or path.startswith("/System/"):
        return True
    if ".framework/" in path or path.endswith(".framework"):
        return True
    return False


def collect_linux(binary: Path) -> dict[str, Path]:
    """Map soname -> real file path."""
    mapping: dict[str, Path] = {}
    queue = [binary]
    seen: set[str] = set()
    while queue:
        cur = queue.pop()
        key = str(cur)
        if key in seen:
            continue
        seen.add(key)
        proc = run(["ldd", str(cur)], check=False)
        for line in proc.stdout.splitlines():
            line = line.strip()
            if " => " not in line:
                continue
            soname, rest = line.split(" => ", 1)
            soname = os.path.basename(soname.strip())
            dest = rest.split(" (", 1)[0].strip()
            if not dest or dest == "not found" or linux_skip(soname):
                continue
            path = Path(dest)
            if not path.is_file():
                continue
            mapping[soname] = path.resolve()
            queue.append(path)
    return mapping


def collect_macos(binary: Path) -> dict[str, Path]:
    """Map original install name -> real file path."""
    mapping: dict[str, Path] = {}
    queue = [str(binary)]
    seen: set[str] = set()
    while queue:
        cur = queue.pop()
        if cur in seen:
            continue
        seen.add(cur)
        proc = run(["otool", "-L", cur], check=False)
        lines = proc.stdout.splitlines()
        for line in lines[1:]:
            dep = line.strip().split(" (", 1)[0].strip()
            if not dep or macos_system(dep):
                continue
            real = Path(dep)
            if not real.is_file():
                continue
            mapping[dep] = real.resolve()
            queue.append(str(real))
    return mapping


def has_rpath_macos(binary: Path, rpath: str) -> bool:
    proc = run(["otool", "-l", str(binary)], check=False)
    return rpath in proc.stdout


def bundle_linux(prefix: Path, binary: Path) -> None:
    libdir = prefix / "lib"
    libdir.mkdir(parents=True, exist_ok=True)
    libs = collect_linux(binary)
    for soname, src in libs.items():
        dest = libdir / soname
        if not dest.exists():
            shutil.copy2(src, dest)
        if shutil.which("patchelf"):
            run(["patchelf", "--set-rpath", "$ORIGIN", str(dest)], check=False)
    if shutil.which("patchelf"):
        run(["patchelf", "--set-rpath", "$ORIGIN/../lib", str(binary)], check=False)
    else:
        print("warning: patchelf not found; install it so bundled libs resolve via $ORIGIN",
              file=sys.stderr)
    print(f"bundled {len(libs)} linux libraries into {libdir}")


def bundle_macos(prefix: Path, binary: Path) -> None:
    libdir = prefix / "lib"
    libdir.mkdir(parents=True, exist_ok=True)
    mapping = collect_macos(binary)
    copied: dict[str, Path] = {}
    for orig, src in mapping.items():
        dest = libdir / src.name
        if not dest.exists():
            shutil.copy2(src, dest)
        copied[orig] = dest
        run(["install_name_tool", "-id", f"@rpath/{dest.name}", str(dest)], check=False)

    targets = [binary, *copied.values()]
    for target in targets:
        proc = run(["otool", "-L", str(target)], check=False)
        for line in proc.stdout.splitlines()[1:]:
            dep = line.strip().split(" (", 1)[0].strip()
            if dep in copied:
                new = f"@rpath/{copied[dep].name}"
                run(["install_name_tool", "-change", dep, new, str(target)], check=False)

    rpath = "@executable_path/../lib"
    if not has_rpath_macos(binary, rpath):
        run(["install_name_tool", "-add_rpath", rpath, str(binary)], check=False)
    run(["codesign", "--force", "--sign", "-", str(binary)], check=False)
    for dest in copied.values():
        run(["codesign", "--force", "--sign", "-", str(dest)], check=False)
    print(f"bundled {len(copied)} macos libraries into {libdir}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", required=True, help="cmake install prefix")
    args = parser.parse_args()
    prefix = Path(args.prefix).resolve()
    binary = prefix / "bin" / "traash"
    if not binary.is_file():
        print(f"error: missing {binary}", file=sys.stderr)
        return 1
    if sys.platform == "darwin":
        bundle_macos(prefix, binary)
    elif sys.platform.startswith("linux"):
        bundle_linux(prefix, binary)
    else:
        print(f"skip bundling on {sys.platform}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
