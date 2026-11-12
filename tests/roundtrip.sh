#!/usr/bin/env bash
# roundtrip.sh — the phase-3 exit test: compress, then decompress, then compare.
#
# The binaries are stubs, which is the point: phase 3 is about the process
# boundary and the manifest, not about which compressor is behind it. Each stub
# gzips on `c` and gunzips on `d`, so a byte-exact round trip proves the argv we
# build in both directions is right.
#
# Note the profiles' `args = c`: the step's arguments carry the tool's verb, the
# same way the shipped profiles pass `-m3f` and `-c lzma -9`. The manifest's
# reverse command assumes the matching verb is `d`.
#
#   tests/roundtrip.sh <path-to-heavenzip> <fixtures-dir>
set -euo pipefail

BIN="${1:?usage: roundtrip.sh <heavenzip> <fixtures-dir>}"
FIXTURES="${2:?usage: roundtrip.sh <heavenzip> <fixtures-dir>}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/bin" "$WORK/config/profiles"

# One stub, copied under two names: `c` compresses, `d` decompresses.
cat > "$WORK/bin/stub.py" <<'PY'
import gzip, sys
verb, src, dst = sys.argv[1], sys.argv[-2], sys.argv[-1]
data = open(src, "rb").read()
if verb == "c":
    open(dst, "wb").write(gzip.compress(data, 9))
elif verb == "d":
    open(dst, "wb").write(gzip.decompress(data))
else:
    sys.exit(f"stub: unknown verb {verb}")
PY
cat > "$WORK/bin/ziptastic" <<PY
#!/bin/sh
exec python3 "$WORK/bin/stub.py" "\$@"
PY
cat > "$WORK/bin/srep" <<PY
#!/bin/sh
exec python3 "$WORK/bin/stub.py" "\$@"
PY
chmod +x "$WORK/bin/ziptastic" "$WORK/bin/srep"

cat > "$WORK/config/syc.ini" <<INI
[general]
version = 0.1.0
log_level = info
ziptastic_bin = $WORK/bin/ziptastic

[profiles]
default = two-step
available = two-step
INI

cat > "$WORK/config/profiles/two-step.ini" <<INI
[route]
detect = text
pipeline = first,second

[first]
bin = $WORK/bin/srep
args = c

[second]
args = c
INI

cp "$FIXTURES/notes.txt" "$WORK/input.txt"
cp "$WORK/input.txt" "$WORK/original.txt"

echo "-- compress"
"$BIN" compress "$WORK/input.txt" --config "$WORK/config"

test -f "$WORK/input.txt.hzpkg" || { echo "FAIL: no .hzpkg"; exit 1; }
test -f "$WORK/input.txt.hzpipeline" || { echo "FAIL: no .hzpipeline"; exit 1; }
grep -q '"profile": "two-step"' "$WORK/input.txt.hzpipeline" || { echo "FAIL: manifest has no profile"; exit 1; }
grep -q '"reverse"' "$WORK/input.txt.hzpipeline" || { echo "FAIL: manifest has no reverse commands"; exit 1; }

echo "-- decompress"
"$BIN" decompress "$WORK/input.txt.hzpkg" --out "$WORK/restored.txt" --config "$WORK/config"

cmp "$WORK/original.txt" "$WORK/restored.txt" || { echo "FAIL: round trip is not byte-exact"; exit 1; }

echo "-- a failing step must fail the run and say which"
cat > "$WORK/bin/broken" <<'SH2'
#!/bin/sh
echo "cannot open input: on purpose" >&2
exit 3
SH2
chmod +x "$WORK/bin/broken"
# Swap the first step's binary for one that fails, keeping the section valid: the
# parser rejects a duplicate key, so replace rather than add.
sed -i "s|bin = $WORK/bin/srep|bin = $WORK/bin/broken|" "$WORK/config/profiles/two-step.ini"
if "$BIN" compress "$WORK/input.txt" --config "$WORK/config" >/dev/null 2>"$WORK/err.txt"; then
  echo "FAIL: a failing step exited 0"
  exit 1
fi
grep -q 'step `first` failed with exit 3' "$WORK/err.txt" || { echo "FAIL: the failure did not name the step and exit"; cat "$WORK/err.txt"; exit 1; }
grep -q '"ok": false' "$WORK/input.txt.hzpipeline" || { echo "FAIL: the manifest does not record the failure"; exit 1; }

echo "-- a missing binary must refuse before touching the input"
cat > "$WORK/config/profiles/two-step.ini" <<INI
[route]
detect = text
pipeline = first
[first]
bin = $WORK/bin/gone
args = c
INI
rm -f "$WORK/input.txt.hzpkg" "$WORK/input.txt.hzpipeline"
if "$BIN" compress "$WORK/input.txt" --config "$WORK/config" >/dev/null 2>"$WORK/err2.txt"; then
  echo "FAIL: a missing binary exited 0"
  exit 1
fi
grep -q 'is not on PATH' "$WORK/err2.txt" || { echo "FAIL: the missing binary was not named"; cat "$WORK/err2.txt"; exit 1; }
test ! -f "$WORK/input.txt.hzpkg" || { echo "FAIL: it wrote output before refusing"; exit 1; }
cmp "$WORK/original.txt" "$WORK/input.txt" || { echo "FAIL: it modified the input"; exit 1; }

echo "-- bench prints a table and measures"
cat > "$WORK/config/profiles/two-step.ini" <<INI
[route]
detect = text
pipeline = first
[first]
bin = $WORK/bin/srep
args = c
INI
mkdir -p "$WORK/corpus"
cp "$WORK/original.txt" "$WORK/corpus/a.txt"
"$BIN" bench "$WORK/corpus" --config "$WORK/config" > "$WORK/bench.txt" 2>&1 || {
  echo "FAIL: bench exited non-zero"; cat "$WORK/bench.txt"; exit 1; }
grep -q 'ratio' "$WORK/bench.txt" || { echo "FAIL: bench printed no header"; cat "$WORK/bench.txt"; exit 1; }
grep -qE '^two-step .*[0-9]+%' "$WORK/bench.txt" || { echo "FAIL: bench printed no measured row"; cat "$WORK/bench.txt"; exit 1; }
tail -1 "$WORK/bench.txt"

echo "ok: compress -> decompress byte-exact, manifest written, failure named, preflight refuses, bench measures"
