#!/usr/bin/env bash
# shipped-profiles.sh — do the profiles we ship actually work with the engine we ship?
#
# Nothing did this. `integration.real-engine` proves the boundary with an engine it
# configures itself, so the *shipped* profiles were never run against the real
# binary — and every one of them passed `-c lzma -9` to an engine that has no `-c`
# at all. Each profile failed at the engine step with "unknown option '-c'", and no
# test noticed, because no test used the profiles.
#
# This runs each profile's ziptastic step, with that profile's own arguments, against
# a fixture. It skips when no engine is present, for the same reason real-engine.sh
# does: a fresh clone has none, and a skip that says why beats a red build that says
# nothing.
#
#   tests/shipped-profiles.sh <fixtures-dir> [repo-root]
set -euo pipefail

# These fixtures build POSIX shell scripts as stand-in tools and compare
# files with cmp, so they only run where a POSIX shell does. Windows runs
# the C++ unit tests; the shell fixtures say why they are not there.
case "${OS:-}" in
  Windows_NT) echo "skip: POSIX shell fixture (Windows)"; exit 0 ;;
esac

FIXTURES="${1:?usage: shipped-profiles.sh <fixtures-dir> [repo-root]}"
ROOT="${2:-$(cd "$(dirname "$0")/.." && pwd)}"

ENGINE="$(ls -1 "$ROOT"/bin/ziptastic-* 2>/dev/null | head -1 || true)"
if [ -z "${ENGINE:-}" ]; then
  echo "skip: no engine in bin/ — run the sync-binaries workflow, or place a ziptastic release binary there"
  exit 0
fi
echo "engine: $ENGINE"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
INPUT="$FIXTURES/notes.txt"
[ -f "$INPUT" ] || { echo "fail: fixture $INPUT is missing"; exit 1; }

checked=0
failed=0
for profile in "$ROOT"/config/profiles/*.ini; do
  name="$(basename "$profile" .ini)"
  # The ziptastic section is `[ziptastic]`, or `[ziptastic:<label>]` if a profile
  # labels it. Either way its `args` are what the engine will be handed.
  args="$(awk '
    /^\[ziptastic(:[^]]*)?\]/ { inside = 1; next }
    /^\[/ { inside = 0 }
    inside && /^[[:space:]]*args[[:space:]]*=/ { sub(/^[^=]*=[[:space:]]*/, ""); print; exit }
  ' "$profile")"

  if [ -z "${args:-}" ] && ! grep -q '^\[ziptastic' "$profile"; then
    echo "  skip  $name — no ziptastic step"
    continue
  fi

  # shellcheck disable=SC2086
  if "$ENGINE" $args "$INPUT" "$WORK/$name.out" >"$WORK/$name.log" 2>&1; then
    echo "  ok    $name  (ziptastic $args)"
    checked=$((checked + 1))
  else
    echo "  FAIL  $name  (ziptastic $args)"
    sed 's/^/          /' "$WORK/$name.log" | head -3
    failed=$((failed + 1))
  fi
done

echo
if [ "$failed" -gt 0 ]; then
  echo "$failed profile(s) cannot drive the shipped engine; $checked work"
  exit 1
fi
echo "$checked profile(s) drive the shipped engine with their own arguments"
