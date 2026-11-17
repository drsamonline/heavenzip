#!/usr/bin/env python3
"""Sweep heavenzip's profiles over a corpus and report what actually won.

The point is to stop guessing which chain is best. Every row is a measurement: the
archive size, the wall time, and whether `compress` verified its own output by
reversing it.

    python3 tools/sweep.py <corpus-dir> [--profiles a,b,c] [--json out.json]

Each file is compressed with every profile that applies to it — a profile whose route
does not match the content refuses, and a refusal is reported as "n/a" rather than a
failure. That distinction matters: `max-deflate` refusing a text file is correct
behaviour, not a bug.
"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys
import time

HEAVENZIP = pathlib.Path(__file__).resolve().parent.parent / "build" / "heavenzip"
DEFAULT_PROFILES = [
    "plain", "strong", "text", "media", "max-ratio", "max-deflate", "max-ratio-7z",
    "freearc-9b", "freearc", "sevenzip", "zstd", "xz", "gzip", "zpaq",
]


def run(cmd: list[str], cwd: pathlib.Path, timeout: int = 300):
    started = time.time()
    # Some steps write binary to stdout (a tool that streams its result), so the output
    # is decoded leniently rather than as UTF-8 — a sweep must not die on a stray byte.
    done = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=timeout,
                          errors="replace")
    return done, time.time() - started


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("corpus")
    ap.add_argument("--profiles", default=",".join(DEFAULT_PROFILES))
    ap.add_argument("--json")
    ap.add_argument("--heavenzip", default=str(HEAVENZIP))
    args = ap.parse_args()

    binary = pathlib.Path(args.heavenzip).resolve()
    if not binary.exists():
        print(f"no heavenzip at {binary}", file=sys.stderr)
        return 1
    # heavenzip resolves `bin/` relative to where it runs, so it must run from the
    # repository root — the profiles name `bin/ziptastic.exe` and friends. Inputs are
    # passed as absolute paths instead.
    workdir = binary.parent.parent
    profiles = [p.strip() for p in args.profiles.split(",") if p.strip()]
    corpus = pathlib.Path(args.corpus).resolve()
    # heavenzip writes its logs and archives beside the input, so the corpus fills with
    # its own output as the sweep runs. Anything with these markers is not an input.
    NOISE = (".hzpkg", ".hzpipeline", ".stderr", ".stdout", ".tmp", ".reverse")
    files = sorted(p for p in corpus.iterdir()
                   if p.is_file() and not any(n in p.name for n in NOISE))

    rows = []
    for source in files:
        original = source.stat().st_size
        for profile in profiles:
            # Each attempt gets a clean copy: a leftover archive from the previous
            # profile would be measured instead of this one.
            for stale in corpus.glob(source.name + ".hzpkg*"):
                stale.unlink()
            done, seconds = run([str(binary), "compress", str(source), "--profile", profile],
                                workdir)
            archive = corpus / (source.name + ".hzpkg")
            if done.returncode != 0:
                reason = (done.stderr or done.stdout).strip().splitlines()
                text = reason[-1] if reason else "failed"
                state = "n/a" if "does not apply" in text else "FAILED"
                rows.append({"file": source.name, "profile": profile, "bytes": None,
                             "ratio": None, "seconds": round(seconds, 2), "state": state,
                             "note": text[:90]})
                continue
            size = archive.stat().st_size if archive.exists() else None
            rows.append({"file": source.name, "profile": profile, "bytes": size,
                         "ratio": round(size / original, 4) if size and original else None,
                         "seconds": round(seconds, 2), "state": "verified",
                         "note": ""})
        for stale in corpus.glob(source.name + ".hzpkg*"):
            stale.unlink()

    # Report: one block per file, best first.
    for source in files:
        mine = [r for r in rows if r["file"] == source.name and r["bytes"]]
        mine.sort(key=lambda r: r["bytes"])
        print(f"\n{source.name}  ({source.stat().st_size} bytes)")
        print(f"  {'profile':<14} {'bytes':>8} {'ratio':>7} {'time':>7}")
        for r in mine:
            print(f"  {r['profile']:<14} {r['bytes']:>8} {r['ratio']:>7} {r['seconds']:>6}s")
        for r in rows:
            if r["file"] == source.name and not r["bytes"]:
                print(f"  {r['profile']:<14} {r['state']:>8}  {r['note']}")

    # Which profile wins most often, and by how much.
    wins = {}
    for source in files:
        mine = [r for r in rows if r["file"] == source.name and r["bytes"]]
        if mine:
            best = min(mine, key=lambda r: r["bytes"])
            wins[best["profile"]] = wins.get(best["profile"], 0) + 1
    if wins:
        print("\nwins across the corpus:")
        for profile, count in sorted(wins.items(), key=lambda kv: -kv[1]):
            print(f"  {profile:<14} {count}")

    if args.json:
        pathlib.Path(args.json).write_text(json.dumps(rows, indent=1))
        print(f"\nwrote {args.json}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
