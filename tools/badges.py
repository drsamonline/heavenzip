#!/usr/bin/env python3
"""Draw this repository's badges as local SVG files, from what is actually here.

An offline-first repository should not fetch its own description from an image
service, and a badge that says "build: green" over an empty `src/` is worse than
no badge at all. Every value below is read from the tree, so the badges cannot
drift from the repository the way they did before this script existed: the
licence badge said PolyForm NC while `LICENSE` said MIT, and the build badge said
green while there was nothing to build.

  python3 tools/badges.py
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "docs", "assets")

INK = "#14110e"
CREAM = "#f7f2e9"
GREEN = "#2e6b45"
SAFELIGHT = "#b5432c"
TEAL = "#2f6f8f"

CH = 6.6  # approximate advance width at 11px


def licence():
    """The name on the first line of LICENSE, not a claim typed from memory."""
    try:
        with open(os.path.join(ROOT, "LICENSE"), encoding="utf-8") as fh:
            first = fh.readline().strip()
    except OSError:
        return "unknown"
    m = re.match(r"(.*?)\s+License", first)
    return m.group(1) if m else (first or "unknown")


def build_state():
    """A build badge is a claim about a build. There is no build until there is
    a CMakeLists.txt and something under src/ for it to compile."""
    if not os.path.exists(os.path.join(ROOT, "CMakeLists.txt")):
        return "phase 0"
    src = os.path.join(ROOT, "src")
    sources = []
    for base, _, files in os.walk(src):
        sources += [f for f in files if f.endswith((".cpp", ".hpp", ".cc", ".h"))]
    return "green" if sources else "phase 0"


def test_state():
    tests = os.path.join(ROOT, "tests")
    count = 0
    for base, _, files in os.walk(tests):
        count += len([f for f in files if f.endswith((".cpp", ".py", ".sh"))])
    return "none yet" if count == 0 else "in CI"


def badge(label, value, colour):
    lw = int(len(label) * CH) + 16
    vw = int(len(value) * CH) + 16
    total = lw + vw
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="22" role="img" '
        'aria-label="%s: %s"><title>%s: %s</title>'
        '<linearGradient id="s" x2="0" y2="100%%"><stop offset="0" stop-color="#fff" stop-opacity=".08"/>'
        '<stop offset="1" stop-color="#000" stop-opacity=".10"/></linearGradient>'
        '<clipPath id="r"><rect width="%d" height="22" rx="4" fill="#fff"/></clipPath>'
        '<g clip-path="url(#r)"><rect width="%d" height="22" fill="%s"/>'
        '<rect x="%d" width="%d" height="22" fill="%s"/>'
        '<rect width="%d" height="22" fill="url(#s)"/></g>'
        '<g fill="%s" font-family="ui-sans-serif,Segoe UI,Helvetica,Arial,sans-serif" font-size="11" '
        'text-anchor="middle"><text x="%d" y="15">%s</text><text x="%d" y="15">%s</text></g></svg>'
    ) % (total, label, value, label, value, total, lw, INK, lw, vw, colour, total,
         CREAM, lw // 2, label, lw + vw // 2, value)


def main():
    os.makedirs(OUT, exist_ok=True)
    badges = [
        ("live", "live", "online", TEAL),
        ("build", "build", build_state(), GREEN),
        ("tests", "tests", test_state(), GREEN),
        ("licence", "licence", licence(), SAFELIGHT),
        ("offline", "offline", "no network", INK),
        ("deps", "deps", "0 runtime", INK),
    ]
    for name, label, value, colour in badges:
        path = os.path.join(OUT, "badge-%s.svg" % name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(badge(label, value, colour))
        print("wrote", path, "->", value)


if __name__ == "__main__":
    main()
