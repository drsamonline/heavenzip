#!/usr/bin/env bash
# check-pipes.sh — fail when a workflow pipes into `head` under `set -o pipefail`.
#
# This cost three separate CI cycles in one day: `ldd --version | head -1`,
# `find ... | head -1`, and `tar -tzf ... | head -5`. Each time the producer died with
# SIGPIPE (exit 141) *after* doing its work, so the failure looked like a broken step
# rather than a broken pipe. Written down in docs/TRAPS.md, and then hit twice more —
# which is the argument for a gate instead of a paragraph.
#
# The fix is never `|| true`; it is to stop the producer itself (`-print -quit`,
# `getconf`, a redirect) or to drop the pipe.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
failures=0

for file in "$root"/.github/workflows/*.yml; do
  [ -f "$file" ] || continue
  # Only lines that actually pipe into head, and only in files that set pipefail.
  if ! grep -q 'pipefail' "$file"; then
    continue
  fi
  while IFS=: read -r line_no text; do
    case "$text" in
      *'#'*) continue ;;   # a comment explaining the rule is not a violation
    esac
    printf 'FAIL %s:%s pipes into head under pipefail\n      %s\n' \
      "${file#"$root"/}" "$line_no" "$(printf '%s' "$text" | sed 's/^ *//')"
    failures=$((failures + 1))
  done < <(grep -n '| *head' "$file" || true)
done

if [ "$failures" -gt 0 ]; then
  echo
  echo "$failures pipeline(s) into head under pipefail. head closes the pipe, the"
  echo "producer gets SIGPIPE and exits 141, and the step fails after doing its work."
  exit 1
fi
echo "no pipelines into head under pipefail"
