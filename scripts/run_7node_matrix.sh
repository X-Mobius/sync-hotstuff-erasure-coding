#!/usr/bin/env bash
set -euo pipefail

STAMP="${STAMP:-$(date +%Y%m%d-%H%M%S)}"
CLIENT_SECONDS="${CLIENT_SECONDS:-30}"
REPETITIONS="${REPETITIONS:-3}"
ASYNC_VALUES="${ASYNC_VALUES:-1 3 8 16}"
RESUME="${RESUME:-0}"
RESULT_ROOT="${RESULT_ROOT:-$HOME/benchmarks-7node-$STAMP}"

run_case() {
    local root="$1" implementation="$2" nfaulty="$3" scenario="$4" replicas="$5" drop="$6"
    local out="$RESULT_ROOT/$implementation/f$nfaulty/$scenario"
    cd "$root"
    IMPLEMENTATION="$implementation" SCENARIO="$scenario" NFAULTY="$nfaulty" \
    REPLICAS="$replicas" DROP_PROPOSE_PCT="$drop" CLIENT_SECONDS="$CLIENT_SECONDS" \
    REPETITIONS="$REPETITIONS" ASYNC_VALUES="$ASYNC_VALUES" \
    RESUME="$RESUME" \
    CONF_DIR="$root/benchmarks/conf-7node" CLIENT_CONF="hotstuff-7.conf" \
    APP_CONF_PREFIX="hotstuff-sec" OUT_DIR="$out" ./scripts/perf_benchmark.sh
}

mkdir -p "$RESULT_ROOT"
for spec in "$HOME/librightstuff:erasure" "$HOME/librightstuff-baseline:baseline"; do
    root="${spec%%:*}"; implementation="${spec##*:}"
    for f in 2 3; do
        run_case "$root" "$implementation" "$f" normal "0 1 2 3 4 5 6" 0
        run_case "$root" "$implementation" "$f" missing-1 "0 1 2 3 4 5" 0
        run_case "$root" "$implementation" "$f" missing-2 "0 1 2 3 4" 0
        run_case "$root" "$implementation" "$f" noise-30 "0 1 2 3 4 5 6" 30
        run_case "$root" "$implementation" "$f" noise-70 "0 1 2 3 4 5 6" 70
        if [[ "$f" == 3 ]]; then
            run_case "$root" "$implementation" "$f" missing-3 "0 1 2 3" 0
        fi
    done
done

python3 "$HOME/librightstuff/scripts/summarize_7node.py" "$RESULT_ROOT"
echo "7-node benchmark complete: $RESULT_ROOT"
