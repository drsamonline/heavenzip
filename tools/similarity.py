#!/usr/bin/env python3
"""similarity.py — order files so that near-duplicates sit together.

Why heavenzip and not ziptastic
------------------------------
ziptastic compresses one file at a time and cannot see that a directory holds 1,139
near-copies of the same tree. heavenzip sees the whole directory, so it can order
the work — and ordering is free compression when the codec has a window.

The algorithm
-------------
Locality-sensitive hashing, as described in DwarFS's write-up (Marcus Holland-Moritz,
GPL-3.0): a rolling 32-bit hash over every 4-byte window is histogrammed into 256
buckets, and the **four most common bucket indices** are packed into 32 bits. Files
that share common 4-byte substrings share buckets, so they land close together.

Implemented here from that description rather than copied: DwarFS is GPL-3.0 and this
repository is MIT. The mix function is the one the description names
(https://github.com/skeeto/hash-prospector/issues/19).

What this does NOT do
---------------------
It does not deduplicate, and it does not change any file. It only decides an ORDER.
A wrong order costs nothing but the opportunity; it cannot corrupt anything.

Usage
-----
    tools/similarity.py <dir>              # print files ordered by similarity
    tools/similarity.py <dir> --measure    # compare grouped vs name order
"""
from __future__ import annotations

import argparse
import itertools
import pathlib
import sys
import zlib

HIST_BITS = 8


def _mix32(x: int) -> int:
    """low_bias_mix32 from hash-prospector, as named in the DwarFS description."""
    x &= 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x21F0AAAD) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x735A2D97) & 0xFFFFFFFF
    x ^= x >> 15
    return x


def _rehash_to_bits(x: int, bits: int) -> int:
    return _mix32(x + 0x9E3779B9) >> (32 - bits)


def similarity(data: bytes) -> int:
    """A 32-bit locality-sensitive hash: the four most common window buckets."""
    hist = [0] * (1 << HIST_BITS)
    val = 0
    for i, byte in enumerate(data):
        val = ((val << 8) | byte) & 0xFFFFFFFF
        if i >= 3:
            hist[_rehash_to_bits(val, HIST_BITS)] += 1
    # four most common, ties broken by bucket index so the result is deterministic
    order = sorted(range(len(hist)), key=lambda i: (-hist[i], i))[:4]
    return (order[0] << 24) | (order[1] << 16) | (order[2] << 8) | order[3]


def order_by_similarity(paths: list[pathlib.Path]) -> list[pathlib.Path]:
    """Group by hash, then by size within a group — big-first so a window fills."""
    hashed = [(similarity(p.read_bytes()), p.stat().st_size, p) for p in paths]
    hashed.sort(key=lambda t: (t[0], -t[1], str(t[2])))
    return [p for _, _, p in hashed]


def measure(directory: pathlib.Path) -> int:
    """Does the order actually compress better? Measure, do not assume."""
    files = sorted(p for p in directory.iterdir() if p.is_file())
    if len(files) < 2:
        print("need at least two files to compare orders", file=sys.stderr)
        return 2

    def total(paths: list[pathlib.Path]) -> tuple[int, int]:
        blob = b"".join(p.read_bytes() for p in paths)
        return len(blob), len(zlib.compress(blob, 9))

    by_name = files
    by_sim = order_by_similarity(files)
    raw_n, comp_n = total(by_name)
    raw_s, comp_s = total(by_sim)

    print(f"{len(files)} files, {raw_n} bytes of content")
    print(f"  name order        {comp_n:>8} bytes")
    print(f"  similarity order  {comp_s:>8} bytes", end="")
    if comp_s < comp_n:
        print(f"   ({(comp_n - comp_s) * 100.0 / comp_n:.1f}% smaller)")
    elif comp_s > comp_n:
        print(f"   ({(comp_s - comp_n) * 100.0 / comp_n:.1f}% LARGER — the order hurt)")
    else:
        print("   (identical)")
    print("\n  similarity order:")
    for p in by_sim:
        print(f"    {similarity(p.read_bytes()):08x}  {p.name}")
    return 0


def selftest() -> int:
    """Assert the hash's properties without a test framework.

    This repository's CI is ctest plus a few python3 scripts; there is no pytest
    step, so a test file here would be a test nobody runs. The properties are the
    same ones a unit test would assert: near-duplicates hash together, unrelated
    content does not, the hash is deterministic, and ordering groups families.
    """
    base = b"the quick brown fox jumps over the lazy dog. " * 200
    if similarity(base + b"revision one") != similarity(base + b"revision two"):
        print("selftest: near-duplicates hashed apart", file=sys.stderr)
        return 1
    alpha = b"alpha module source code with repeated identifiers. " * 200
    beta = b"beta report prose about something else entirely. " * 200
    if similarity(alpha) == similarity(beta):
        print("selftest: unrelated content hashed together", file=sys.stderr)
        return 1
    for short in (b"", b"ab", b"abc"):
        if not isinstance(similarity(short), int):
            print("selftest: short input broke", file=sys.stderr)
            return 1

    import tempfile
    with tempfile.TemporaryDirectory() as d:
        root = pathlib.Path(d)
        for i in range(4):
            (root / f"{i:02d}-a.txt").write_bytes(alpha)
            (root / f"{i:02d}-b.txt").write_bytes(beta)
        kinds = ["a" if "-a." in p.name else "b"
                 for p in order_by_similarity(sorted(root.iterdir()))]
        if kinds != sorted(kinds):
            print(f"selftest: families interleaved: {kinds}", file=sys.stderr)
            return 1
    print("selftest: 4 properties hold")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", nargs="?")
    ap.add_argument("--measure", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.directory:
        ap.error("give a directory, or --selftest")
    d = pathlib.Path(args.directory)
    if not d.is_dir():
        print(f"not a directory: {d}", file=sys.stderr)
        return 2
    if args.measure:
        return measure(d)
    for p in order_by_similarity(sorted(x for x in d.iterdir() if x.is_file())):
        print(f"{similarity(p.read_bytes()):08x}  {p}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
