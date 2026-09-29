#!/usr/bin/env bash
set -euo pipefail
root="$HOME/librightstuff"
baseline="$(cat "$HOME/full-command-baseline-path.txt")"
results="$(mktemp -d "$HOME/full-command-performance-XXXXXXXX")"
printf '%s\n' "$results" > "$HOME/full-command-performance-path.txt"
cp "$baseline/BASELINE_MANIFEST.json" "$results/"
git -C "$root" rev-parse HEAD > "$results/source-base-commit.txt"
git -C "$root" diff --binary > "$results/source-working.patch"
git -C "$root/salticidae" diff --binary > "$results/salticidae-working.patch"
for payload in 1024 16384 65536; do
    for repo in "$root" "$baseline"; do
        name="$(basename "$repo")"
        cmake -S "$repo" -B "$repo" -DCMAKE_CXX_FLAGS="-DHOTSTUFF_CMD_REQSIZE=$payload" > "$results/build-$name-$payload.log" 2>&1
        make -C "$repo" -j2 >> "$results/build-$name-$payload.log" 2>&1
    done
    python3 "$root/scripts/full_command_matrix.py" --phase performance \
        --baseline "$baseline" --payload "$payload" --out "$results/payload-$payload"
done
printf 'Completed all 81 performance runs: %s\n' "$results"
