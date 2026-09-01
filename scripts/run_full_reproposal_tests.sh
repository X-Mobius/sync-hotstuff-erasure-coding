#!/usr/bin/env bash
set -euo pipefail

CLIENT_SECONDS="${CLIENT_SECONDS:-20}"
REPETITIONS="${REPETITIONS:-3}"
STAMP="${STAMP:-$(date +%Y%m%d-%H%M%S)}"
RESULT_ROOT="${RESULT_ROOT:-$HOME/full-reproposal-results-$STAMP}"

prepare_conf() {
    local root="$1" block_size="$2"
    local source="$root/benchmarks/conf-7node"
    local target="$root/benchmarks/conf-7node-b${block_size}"
    rm -rf "$target"
    cp -a "$source" "$target"
    sed -i "s/^block-size = .*/block-size = $block_size/" "$target"/*.conf
}

run_case() {
    local root="$1" implementation="$2" nfaulty="$3" scenario="$4"
    local replicas="$5" drop="$6" block_size="$7" async="$8" strict="$9"
    local copies="${10:-1}"
    local conf="$root/benchmarks/conf-7node-b${block_size}"
    local out="$RESULT_ROOT/$implementation/f$nfaulty/$scenario"
    echo "### $implementation $scenario block=$block_size async=$async"
    cd "$root"
    CLIENT_SECONDS="$CLIENT_SECONDS" REPETITIONS="$REPETITIONS" \
    ASYNC_VALUES="$async" IMPLEMENTATION="$implementation" SCENARIO="$scenario" \
    NFAULTY="$nfaulty" REQUIRE_ERASURE="$strict" REPLICAS="$replicas" \
    DROP_PROPOSE_PCT="$drop" BLOCK_SIZE="$block_size" ERASURE_SEND_COPIES="$copies" \
    CONF_DIR="$conf" CLIENT_CONF="hotstuff-7.conf" APP_CONF_PREFIX="hotstuff-sec" \
    OUT_DIR="$out" ./scripts/perf_benchmark.sh
}

mkdir -p "$RESULT_ROOT"
for root in "$HOME/librightstuff" "$HOME/librightstuff-baseline"; do
    for block_size in 1 16 64; do prepare_conf "$root" "$block_size"; done
done

# Strict protocol-path validation under crash and message-drop disturbances.
run_case "$HOME/librightstuff" full-reproposal 2 b1-normal \
    "0 1 2 3 4 5 6" 0 1 16 1
run_case "$HOME/librightstuff" full-reproposal 2 b1-missing2 \
    "0 1 2 3 4" 0 1 16 1
run_case "$HOME/librightstuff" full-reproposal 2 b1-noise30-c1 \
    "0 1 2 3 4 5 6" 30 1 16 1 1
run_case "$HOME/librightstuff" full-reproposal 2 b1-noise70-c1 \
    "0 1 2 3 4 5 6" 70 1 16 1 1
run_case "$HOME/librightstuff" full-reproposal 2 b1-noise70-c3 \
    "0 1 2 3 4 5 6" 70 1 16 1 3
run_case "$HOME/librightstuff" full-reproposal 2 b1-noise70-c5 \
    "0 1 2 3 4 5 6" 70 1 16 1 5
run_case "$HOME/librightstuff" full-reproposal 3 b1-missing3 \
    "0 1 2 3" 0 1 16 1

# Payload scaling: compare leader-directed and cluster-total bytes.
run_case "$HOME/librightstuff" full-reproposal 2 b16-normal \
    "0 1 2 3 4 5 6" 0 16 64 1
run_case "$HOME/librightstuff" full-reproposal 2 b64-normal \
    "0 1 2 3 4 5 6" 0 64 128 1
run_case "$HOME/librightstuff-baseline" baseline 2 b1-normal \
    "0 1 2 3 4 5 6" 0 1 16 0
run_case "$HOME/librightstuff-baseline" baseline 2 b16-normal \
    "0 1 2 3 4 5 6" 0 16 64 0
run_case "$HOME/librightstuff-baseline" baseline 2 b64-normal \
    "0 1 2 3 4 5 6" 0 64 128 0

echo "$RESULT_ROOT" > "$HOME/full-reproposal-latest-result.txt"
echo "Complete: $RESULT_ROOT"
