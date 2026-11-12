#!/usr/bin/env bash
# real-engine.sh — phase 4: drive the actual ziptastic, not a stub.
#
# Phase 3 proved the boundary with stubs. This proves it with the engine: pack a
# text file through `bin/ziptastic-*`, then unpack it, then compare the bytes. It
# skips with a clear message when no engine is in `bin/`, because a fresh clone has
# none and a skipped test that says why is better than a red build that says
# nothing.
#
#   tests/real-engine.sh <path-to-heavenzip> <fixtures-dir> [repo-root]
set -euo pipefail

BIN="${1:?usage: real-engine.sh <heavenzip> <fixtures-dir> [repo-root]}"
FIXTURES="${2:?usage: real-engine.sh <heavenzip> <fixtures-dir> [repo-root]}"
ROOT="${3:-$(cd "$(dirname "$0")/.." && pwd)}"

ENGINE="$(ls -1 "$ROOT"/bin/ziptastic-* 2>/dev/null | head -1 || true)"
if [ -z "${ENGINE:-}" ]; then
  echo "skip: no engine in bin/ — run the sync-binaries workflow, or place a ziptastic release binary there"
  exit 0
fi
echo "engine: $ENGINE"
"$ENGINE" version

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/config/profiles"

cat > "$WORK/config/syc.ini" <<INI
[general]
version = 0.1.0
log_level = info
ziptastic_bin = $ENGINE

[profiles]
default = engine
available = engine
INI

# ziptastic takes `c` to compress and `d` to decompress; the manifest's reverse
# command uses `d`, which is exactly this tool's verb.
cat > "$WORK/config/profiles/engine.ini" <<INI
[route]
detect = text
pipeline = ziptastic

[ziptastic]
args = c -9
INI

cp "$FIXTURES/notes.txt" "$WORK/input.txt"
cp "$WORK/input.txt" "$WORK/original.txt"
# Big enough to be worth compressing, still text.
for _ in $(seq 1 40); do cat "$WORK/original.txt"; done > "$WORK/input.txt"
cp "$WORK/input.txt" "$WORK/original.txt"

echo "-- compress through the real engine"
"$BIN" compress "$WORK/input.txt" --config "$WORK/config"

echo "-- the engine really compressed it"
raw=$(wc -c < "$WORK/original.txt")
packed=$(wc -c < "$WORK/input.txt.hzpkg")
echo "   $raw -> $packed bytes"
if [ "$packed" -ge "$raw" ]; then
  echo "FAIL: the engine produced nothing smaller ($raw -> $packed)"
  exit 1
fi
grep -q '"ok": true' "$WORK/input.txt.hzpipeline" || { echo "FAIL: manifest says the run failed"; exit 1; }

echo "-- decompress through the real engine"
"$BIN" decompress "$WORK/input.txt.hzpkg" --out "$WORK/restored.txt" --config "$WORK/config"
cmp "$WORK/original.txt" "$WORK/restored.txt" || { echo "FAIL: round trip is not byte-exact"; exit 1; }

echo "ok: real ziptastic round trip byte-exact ($raw -> $packed bytes)"
