#!/usr/bin/env bash
# Fetch the mpir_gc_x64 tree pinned in scripts/mpir_gc_x64.commit
# into ./mpir_gc_x64 at the repo root.
# The Windows CI cache key is that pin.
set -euo pipefail

export GIT_TERMINAL_PROMPT=0

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
rev="$(tr -d '[:space:]' < "$root/scripts/mpir_gc_x64.commit")"
dest="$root/mpir_gc_x64"

if [[ ! "$rev" =~ ^[0-9a-f]{40}$ ]]; then
  echo "mpir_gc_x64 pin must be a 40-character commit, got: ${rev:-<empty>}" >&2
  exit 1
fi

rm -rf "$dest"
git init -q "$dest"
git -C "$dest" fetch --depth 1 "https://github.com/Chia-Network/mpir_gc_x64.git" "$rev"
git -C "$dest" checkout -q --detach FETCH_HEAD

got="$(git -C "$dest" rev-parse HEAD)"
if [[ "$got" != "$rev" ]]; then
  echo "mpir_gc_x64 checkout $got does not match pin $rev" >&2
  exit 1
fi
echo "mpir_gc_x64 $rev"
