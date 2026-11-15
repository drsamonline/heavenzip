#!/usr/bin/env python3
"""Fail when the repository's description of itself stops being true.

Three claims, each checked against a fact the repository holds:

1. the version is the same in `VERSION`, `CMakeLists.txt` and `config/syc.ini`
   (three places, and nothing compared them — `VERSION` said 1.0.0 while the
   binary reported 0.1.0);
2. the tests badge states the number of ctest suites that are actually
   registered (`tests: 8 ctest suites` while the badge read "none yet");
3. the build badge states the phase range the README phase table marks done.

    python3 tools/check-claims.py            # exit 1 on drift
    python3 tools/check-claims.py --fix      # rewrite the badges, then re-check
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import importlib.util

_spec = importlib.util.spec_from_file_location(
    "make_badges", Path(__file__).resolve().parent / "make-badges.py")
mb = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mb)

ROOT = Path(__file__).resolve().parent.parent


def version_claims() -> dict[str, str]:
    claims = {}
    vf = ROOT / "VERSION"
    if vf.is_file():
        claims["VERSION"] = vf.read_text(encoding="utf-8").strip()
    cm = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    m = re.search(r"project\(\s*heavenzip\s*\n?\s*VERSION\s+([0-9.]+)", cm)
    claims["CMakeLists.txt"] = m.group(1) if m else "?"
    ini = (ROOT / "config" / "syc.ini").read_text(encoding="utf-8")
    m = re.search(r"^\s*version\s*=\s*([0-9.]+)", ini, re.M)
    claims["config/syc.ini"] = m.group(1) if m else "?"
    return claims


def badge_claim(name: str) -> tuple[str, str] | None:
    p = ROOT / "docs" / "assets" / name
    if not p.is_file():
        return None
    m = re.search(r"<title>([^:]+):\s*([^<]+)</title>", p.read_text(encoding="utf-8"))
    return (m.group(1), m.group(2)) if m else None


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--fix", action="store_true", help="rewrite the badges first")
    args = ap.parse_args()

    if args.fix:
        mb.main()

    failures: list[str] = []

    versions = version_claims()
    distinct = set(versions.values())
    if len(distinct) != 1 or "?" in distinct:
        detail = ", ".join(f"{k}={v}" for k, v in versions.items())
        failures.append(f"version disagrees between the places that state it: {detail}")
    else:
        print(f"ok    version {distinct.pop()} in {len(versions)} places")

    for name, (label, value) in mb.expected().items():
        if value == "unknown":
            failures.append(f"{name}: cannot determine the fact it states, "
                            f"so the badge cannot be checked")
            continue
        got = badge_claim(name)
        if got is None:
            failures.append(f"{name}: badge missing or has no <title>")
            continue
        if got != (label, value):
            failures.append(f"{name}: says \"{got[0]}: {got[1]}\", "
                            f"the repository says \"{label}: {value}\"")
        else:
            print(f"ok    {name}: {label}: {value}")

    for f in failures:
        print(f"FAIL  {f}")
    if failures:
        print(f"\n{len(failures)} claim(s) disagree with the repository. "
              f"Run `python3 tools/check-claims.py --fix` to regenerate the badges.")
        return 1
    print("\nevery claim matches the repository")
    return 0


if __name__ == "__main__":
    sys.exit(main())
