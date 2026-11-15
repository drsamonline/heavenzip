#!/usr/bin/env python3
"""Render the README's badges from facts, so the picture cannot drift.

The badges in docs/assets/ are hand-drawn SVGs, and two of them had been saying
"tests: none yet" and "build: phase 0" long after eight ctest suites existed and
the plan's phases were complete. A hand-drawn status is a claim nobody checks.

`tools/check-claims.py` imports this module, recomputes the same text from the
repository, and fails when a badge disagrees with it.

    python3 tools/make-badges.py          # rewrite the SVGs
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "docs" / "assets"

FONT = "ui-sans-serif,Segoe UI,Helvetica,Arial,sans-serif"


def text_width(s: str) -> int:
    """Rough advance width at font-size 11, as shields.io computes it."""
    return int(round(len(s) * 6.2))


def render(label: str, value: str, colour: str = "#2e6b45") -> str:
    lw = text_width(label) + 20
    vw = text_width(value) + 20
    total = lw + vw
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{total}" height="22" role="img" '
        f'aria-label="{label}: {value}"><title>{label}: {value}</title>'
        '<linearGradient id="s" x2="0" y2="100%"><stop offset="0" stop-color="#fff" '
        'stop-opacity=".08"/><stop offset="1" stop-color="#000" stop-opacity=".10"/>'
        '</linearGradient><clipPath id="r"><rect width="100%" height="22" rx="4" '
        'fill="#fff"/></clipPath><g clip-path="url(#r)"><rect width="'
        f'{lw}" height="22" fill="#14110e"/><rect x="{lw}" width="{vw}" height="22" '
        f'fill="{colour}"/><rect width="{total}" height="22" fill="url(#s)"/></g>'
        f'<g fill="#f7f2e9" font-family="{FONT}" font-size="11" text-anchor="middle">'
        f'<text x="{lw // 2}" y="15">{label}</text>'
        f'<text x="{lw + vw // 2}" y="15">{value}</text></g></svg>\n'
    )


def ctest_count() -> int | None:
    """Registered ctest tests: from the build directory if configured, else the
    number of add_test() calls in CMakeLists.txt."""
    import subprocess

    for build in ("build",):
        d = ROOT / build
        if d.is_dir():
            try:
                out = subprocess.run(["ctest", "--test-dir", str(d), "-N"],
                                     capture_output=True, text=True, timeout=120).stdout
                m = re.search(r"Total Tests:\s*(\d+)", out)
                if m:
                    return int(m.group(1))
            except (OSError, subprocess.SubprocessError):
                pass
    cm = ROOT / "CMakeLists.txt"
    if cm.is_file():
        return len(re.findall(r"^\s*add_test\(", cm.read_text(encoding="utf-8"), re.M))
    return None


def phases_done() -> list[int]:
    """Phase numbers marked done in the README phase table."""
    readme = ROOT / "README.md"
    if not readme.is_file():
        return []
    done = []
    for line in readme.read_text(encoding="utf-8").splitlines():
        m = re.match(r"\|\s*(\d+)\s*\|\s*(done|built)\s*\|", line.strip())
        if m:
            done.append(int(m.group(1)))
    return sorted(done)


def expected() -> dict[str, tuple[str, str]]:
    """badge filename -> (label, value) computed from the repository."""
    out: dict[str, tuple[str, str]] = {}
    n = ctest_count()
    out["badge-tests.svg"] = ("tests", f"{n} ctest suites" if n else "unknown")
    ph = phases_done()
    if ph:
        lo, hi = min(ph), max(ph)
        out["badge-build.svg"] = ("build", f"phases {lo}\u2013{hi}" if lo != hi else f"phase {hi}")
    else:
        out["badge-build.svg"] = ("build", "unknown")
    return out


def main() -> int:
    for name, (label, value) in expected().items():
        p = ASSETS / name
        if value == "unknown":
            print(f"skip  {name}: cannot determine the fact it states", file=sys.stderr)
            continue
        p.write_text(render(label, value), encoding="utf-8")
        print(f"wrote {p.relative_to(ROOT)}  ({label}: {value})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
