#!/usr/bin/env bash
# fetch-vault.sh — make HCS's `old/` vault available to this repository.
#
# The vault holds ~50 compression projects, several of which are not standalone
# directories but whole codec sets inside another project: FreeArc's Delta, Dict,
# GRZip, LZP, PPMD, REP and Tornado all live under `freearc-c-main/src/Compression/`,
# not in `old/grzip` and friends. Searching `old/` for a directory named after the
# codec therefore reports them absent when they are present.
#
# The vault is 128 MB and does not belong in every repository's history. This clones it
# beside the checkout instead, at a pinned ref, so every repo sees the same sources.
#
#   tools/fetch-vault.sh                  # clone into ../HCS-vault at the pinned ref
#   HCS_REF=main tools/fetch-vault.sh     # a different ref
#   VAULT_DIR=/tmp/vault tools/fetch-vault.sh
#
# Then point a build at it:
#
#   CXX=zig-cxx tools/build-7z.py "$VAULT_DIR/old/7z" /tmp/7z
set -euo pipefail

REPO="${HCS_REPO:-https://github.com/drsamonline/HCS.git}"
REF="${HCS_REF:-main}"
DEST="${VAULT_DIR:-$(cd "$(dirname "$0")/.." && pwd)/../HCS-vault}"

if [ -d "$DEST/.git" ]; then
  echo "vault already at $DEST"
  git -C "$DEST" fetch --depth 1 origin "$REF"
  git -C "$DEST" checkout -q FETCH_HEAD
else
  echo "cloning $REPO at $REF into $DEST"
  git clone --depth 1 --branch "$REF" "$REPO" "$DEST"
fi

# The submodules are the individual projects; without them `old/` is a list of empty
# directories and every build fails with "no such file".
echo "initialising submodules (this is most of the 128 MB)"
git -C "$DEST" submodule update --init --depth 1 --recursive

count="$(find "$DEST/old" -mindepth 1 -maxdepth 1 -type d | wc -l)"
echo "vault ready: $count projects under $DEST/old"
echo
echo "What is where, for the tools that are not standalone directories:"
echo "  Delta, Dict, GRZip, LZP, PPMD, REP, Tornado"
echo "      -> $DEST/old/freearc-c-main/src/Compression/"
echo "  xz        -> $DEST/old/xz-master"
echo "  precomp   -> $DEST/old/precomp-cpp-master"
echo "  7-Zip     -> $DEST/old/7z"
echo "  srep      -> $DEST/old/srep-main"
echo "  FreeArc's method table (Lua, not .ini)"
echo "      -> $DEST/old/FA-master/0.20/CompressionMethod.lua"
