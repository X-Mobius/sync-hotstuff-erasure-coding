#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
expected=384d9277458cbd90c94d1510e5d20d96b26010c8
git -C "$root" submodule update --init --recursive
actual="$(git -C "$root/salticidae" rev-parse HEAD)"
if [[ "$actual" != "$expected" ]]; then
    printf 'Unexpected Salticidae commit: %s (expected %s)\n' "$actual" "$expected" >&2
    exit 1
fi
for name in salticidae-wire-stats.patch salticidae-uv-handle-delete.patch; do
    patch="$root/patches/$name"
    if git -C "$root/salticidae" apply --unidiff-zero --reverse --check "$patch"; then
        printf 'Already applied: %s\n' "$name"
    elif git -C "$root/salticidae" apply --unidiff-zero --check "$patch"; then
        git -C "$root/salticidae" apply --unidiff-zero "$patch"
        printf 'Applied: %s\n' "$name"
    else
        printf 'Cannot apply %s cleanly; inspect Salticidae working tree.\n' "$name" >&2
        exit 1
    fi
done
