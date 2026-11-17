#!/usr/bin/env python3
"""Build 7-Zip's standalone Unix CLI without `make`.

The box has no make, so the makefile is read instead of run. Its object list names the
bundle's own layer; the layers it links against are named by rules in `7zip_gcc.mak`,
and each one pulls in the next — leave one out and the link reports a missing symbol
from the layer below. The set below is what actually links, arrived at by following
those reports: C, the Windows shim files that compile on Unix, the two Common layers,
the codecs, the archive helpers, and the 7z format handler itself.

    CXX=zig-cxx python3 tools/build-7z.py <7z-source-root> <output-binary>

`CXX` defaults to `g++`; when it looks like zig, the target triple is passed so the
binary's glibc floor and CPU baseline are pinned rather than inherited from the host.
"""
import os
import pathlib
import re
import subprocess
import sys

CXX = os.environ.get("CXX", "g++")
TARGET = os.environ.get("ZIG_TARGET", "x86_64-linux-gnu.2.28")
IS_ZIG = "zig" in pathlib.Path(CXX).name

CXXFLAGS = [
    "-O2", "-DNDEBUG", "-D_REENTRANT", "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE_SOURCE",
    "-fPIC", "-w",  # the sources predate several modern warnings; not ours to fix
]
CFLAGS = ["-O2", "-DNDEBUG", "-w"]

# Directory, object-name prefix. Order matters only for readability.
LAYERS = [
    ("CPP/Common", "base_"),
    ("CPP/7zip/Common", "zcommon_"),
    ("CPP/7zip/Compress", "codec_"),
    ("CPP/7zip/Archive/Common", "arc_"),
    ("CPP/7zip/Archive/7z", "h7_"),
    ("C", "c_"),
    ("CPP/Windows", "win_"),
]
# Files whose definitions collide with the ones the bundle already provides.
SKIP_NAMES = {"DllExportsCompress", "CodecExports", "DllExports2", "DllExports"}


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    out = pathlib.Path(sys.argv[2]).resolve()
    sevenzip = root / "CPP" / "7zip"
    bundle = sevenzip / "Bundles" / "Alone2"
    build = root / ".build7z"
    build.mkdir(exist_ok=True)

    # 1. The bundle's own objects, and the rules that name their sources.
    makefile = (bundle / "makefile.gcc").read_text()
    wanted = list(re.findall(r"\$O/([A-Za-z0-9_/]+)\.o", makefile))
    rules = {}
    for mak in (root / "CPP").rglob("*.mak"):
        for obj, src in re.findall(r"\$O/([A-Za-z0-9_/]+)\.o:\s*(\S+\.cpp)", mak.read_text()):
            rules.setdefault(obj, src)

    def compile_one(source: pathlib.Path, obj: pathlib.Path) -> bool:
        if obj.exists() and obj.stat().st_mtime >= source.stat().st_mtime:
            return True
        flags = CFLAGS if source.suffix == ".c" else CXXFLAGS
        argv = [CXX, *(["-target", TARGET] if IS_ZIG else []), *flags,
                "-c", "-o", str(obj), str(source)]
        done = subprocess.run(argv, capture_output=True, text=True)
        if done.returncode != 0:
            print(f"  skip {source.name}: {done.stderr.splitlines()[0][:120]}", file=sys.stderr)
            obj.unlink(missing_ok=True)
            return False
        return True

    compiled = 0
    for name in sorted(set(wanted)):
        key = name.rsplit("/", 1)[-1]
        if key not in rules:
            continue  # DllSecur, resource: Windows-only, no Unix source
        src = (bundle / rules[key]).resolve()
        if "/Windows/" in src.as_posix():
            continue  # handled by the shim layer below, which tries each file
        if compile_one(src, build / f"{key}.o"):
            compiled += 1

    for directory, prefix in LAYERS:
        for source in sorted((root / directory).glob("*.c*")):
            if source.suffix not in (".c", ".cpp"):
                continue
            if source.stem in SKIP_NAMES:
                continue
            # A file the bundle already compiled keeps its own object.
            if (build / f"{source.stem}.o").exists():
                continue
            if compile_one(source, build / f"{prefix}{source.stem}.o"):
                compiled += 1

    objects = sorted(str(p) for p in build.glob("*.o"))
    if not objects:
        print("nothing compiled", file=sys.stderr)
        return 1

    link = subprocess.run([CXX, *(["-target", TARGET] if IS_ZIG else []), "-o", str(out),
                           *objects, "-lpthread", "-ldl"], capture_output=True, text=True)
    if link.returncode != 0:
        print(f"link failed\n{link.stderr[:3000]}", file=sys.stderr)
        return 1
    print(f"built {out} from {len(objects)} objects")
    return 0


if __name__ == "__main__":
    sys.exit(main())
