#!/usr/bin/env bash
# profile-from-docs.sh — phase 7's exit test: the worked example in
# docs/PROFILES.md must actually work, from a copy of the shipped config, without
# touching code.
#
#   tests/profile-from-docs.sh <heavenzip> <repo-root> <fixtures-dir>
set -euo pipefail

BIN="${1:?usage: profile-from-docs.sh <heavenzip> <repo-root> <fixtures-dir>}"
ROOT="${2:?usage: profile-from-docs.sh <heavenzip> <repo-root> <fixtures-dir>}"
FIXTURES="${3:?usage: profile-from-docs.sh <heavenzip> <repo-root> <fixtures-dir>}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cp -r "$ROOT/config" "$WORK/config"

# Step 1 and 2 of the worked example: the file, and the name in `available`.
cat > "$WORK/config/profiles/music.ini" <<'INI'
[route]
detect = media
pipeline = ziptastic

[ziptastic]
args = c -9
INI
sed -i 's/^available = .*/available = text, media, max-ratio, music/' "$WORK/config/syc.ini"

echo "-- validate sees it"
"$BIN" validate --config "$WORK/config" | grep -q 'ok    config/profiles/music.ini' || {
  echo "FAIL: validate did not report music.ini"; exit 1; }

echo "-- list-profiles shows it"
"$BIN" list-profiles --config "$WORK/config" | grep -q '^music' || {
  echo "FAIL: list-profiles did not show music"; exit 1; }

echo "-- it fires on media and refuses text"
"$BIN" compress "$FIXTURES/image.png" --profile music --dry-run --config "$WORK/config" >/dev/null || {
  echo "FAIL: music did not plan for a PNG"; exit 1; }
if "$BIN" compress "$FIXTURES/notes.txt" --profile music --dry-run --config "$WORK/config" >/dev/null 2>&1; then
  echo "FAIL: music planned for a text file"; exit 1
fi

echo "-- and the dry run prints the argv"
"$BIN" compress "$FIXTURES/image.png" --profile music --dry-run --config "$WORK/config" | grep -q 'ziptastic' || {
  echo "FAIL: dry run printed no step"; exit 1; }

echo "ok: a profile written from docs/PROFILES.md works without touching code"
