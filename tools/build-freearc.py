#!/usr/bin/env python3
"""Build FreeArc's `arc` and `unarc` without make.

`freearc-c-main` ships a Makefile and this project's build machines have no make, so
the object rules are read out of it — the same approach as `build-7z.py`.

    CXX=zig-cxx python3 tools/build-freearc.py <freearc-c-root> <output-dir>

Produces `<output-dir>/arc` and `<output-dir>/unarc`. The second is not optional: `arc
x` shells out to `unarc`, and without it extraction fails with "failed to execute
unarc", which reads like a missing feature rather than a missing binary.

**Known issue, not fixed here:** `arc` segfaults on the `lzma` method (and on `-m4`,
`-m5`, `-mx`, which resolve to it). `storing`, `tor`, `ppmd`, `grzip`, `rep` and `lzp`
work. Until that is understood, this binary is not wired into the bundle — an archiver
whose best codec crashes is worse than none.
"""
import os
import pathlib
import re
import subprocess
import sys

CXX = os.environ.get("CXX", "g++")
TARGET = os.environ.get("ZIG_TARGET", "x86_64-linux-gnu.2.28")
IS_ZIG = "zig" in pathlib.Path(CXX).name

BASE_FLAGS = [
    "-O2", "-DFREEARC_UNIX", "-DFREEARC_INTEL_BYTE_ORDER", "-DFREEARC_64BIT",
    # Environment.cpp defines its own `basename`, which collides with glibc's
    # noexcept-qualified declaration. Renaming ours is the least invasive fix.
    "-Dbasename=fa_basename",
    # The MM codec and the crypto layer include by bare name.
    "-I.", "-ICompression", "-ICompression/_Encryption/headers",
    "-fno-exceptions", "-fno-rtti", "-w",
]
EXTRACTOR_FLAGS = ["-DFREEARC_DECOMPRESS_ONLY", "-DUNARC"]


def sources_for(mk_text: str, link_var: str) -> list[str]:
    rules = {}
    for obj, src in re.findall(r'\$\((?:ENC|DEC)_OBJDIR\)/([A-Za-z0-9_]+)\.o:\s*(\S+\.cpp)',
                               mk_text):
        rules.setdefault(obj, src)
    block = re.search(link_var + r"\s*=\s*(.*?)(?=\n[A-Za-z_]+ *[:=])", mk_text, re.S)
    if not block:
        raise SystemExit(f"no {link_var} in the Makefile")
    objs = re.findall(r'\$\((?:ENC|DEC)_OBJDIR\)/([A-Za-z0-9_]+)\.o', block.group(1))
    missing = [o for o in objs if o not in rules]
    if missing:
        raise SystemExit(f"no source rule for: {', '.join(missing)}")
    return [rules[o] for o in objs]


def build(root: pathlib.Path, out_dir: pathlib.Path, name: str, srcs: list[str],
          extra: list[str], obj_dir: pathlib.Path) -> None:
    obj_dir.mkdir(parents=True, exist_ok=True)
    objects = []
    for src in srcs:
        source = root / "src" / src
        obj = obj_dir / (src.replace("/", "_").replace(".cpp", ".o"))
        if not obj.exists() or obj.stat().st_mtime < source.stat().st_mtime:
            argv = [CXX, *(["-target", TARGET] if IS_ZIG else []), *BASE_FLAGS, *extra,
                    "-c", "-o", str(obj), str(source)]
            done = subprocess.run(argv, capture_output=True, text=True, cwd=root / "src")
            if done.returncode != 0:
                print(f"FAILED {src}\n{done.stderr[:1500]}", file=sys.stderr)
                raise SystemExit(1)
        objects.append(str(obj))
    out = out_dir / name
    link = subprocess.run([CXX, *(["-target", TARGET] if IS_ZIG else []), "-o", str(out),
                           *objects, "-lpthread"], capture_output=True, text=True)
    if link.returncode != 0:
        print(f"link failed for {name}\n{link.stderr[:2000]}", file=sys.stderr)
        raise SystemExit(1)
    print(f"built {out} ({len(objects)} objects)")


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    out_dir = pathlib.Path(sys.argv[2]).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    mk = (root / "src" / "Makefile").read_text()

    # The crypto layer is not in either link list; CompressionLibrary calls into it.
    # Only C_Encryption.cpp — the cipher sources are `#include`d by it, and adding the
    # .c files as objects too duplicates every symbol they define.
    enc = ["Compression/_Encryption/C_Encryption.cpp"]

    def with_crypto(srcs: list[str]) -> list[str]:
        # C_Encryption.cpp `#include`s the cipher sources, so it must be compiled once.
        # The link lists already name it in some configurations; adding it again
        # duplicates every symbol the ciphers define.
        return srcs if any("C_Encryption" in x for x in srcs) else srcs + enc

    build(root, out_dir, "arc", with_crypto(sources_for(mk, "ENC_LINKOBJ")), [],
          root / ".build_freearc")
    build(root, out_dir, "unarc", with_crypto(sources_for(mk, "DEC_LINKOBJ")), EXTRACTOR_FLAGS,
          root / ".build_unarc")
    return 0


if __name__ == "__main__":
    sys.exit(main())
