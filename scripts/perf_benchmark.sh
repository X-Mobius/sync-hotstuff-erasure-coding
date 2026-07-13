#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASYNC_VALUES="${ASYNC_VALUES:-1 3 8 16}"
WARMUP_SECONDS="${WARMUP_SECONDS:-3}"
CLIENT_SECONDS="${CLIENT_SECONDS:-30}"
REPETITIONS="${REPETITIONS:-3}"
REPLICAS="${REPLICAS:-0 1 2}"
OUT_DIR="${OUT_DIR:-benchmarks/$(date +%Y%m%d-%H%M%S)}"
CLIENT_IDX="${CLIENT_IDX:-0}"
CONF_DIR="${CONF_DIR:-$ROOT_DIR}"
CLIENT_CONF="${CLIENT_CONF:-hotstuff.conf}"
APP_CONF_PREFIX="${APP_CONF_PREFIX:-hotstuff-sec}"
DROP_PROPOSE_PCT="${DROP_PROPOSE_PCT:-0}"
NFAULTY="${NFAULTY:-}"
IMPLEMENTATION="${IMPLEMENTATION:-unknown}"
SCENARIO="${SCENARIO:-custom}"
RESUME="${RESUME:-0}"

mkdir -p "$OUT_DIR"
OUT_DIR="$(cd "$OUT_DIR" && pwd)"
SUMMARY_CSV="$OUT_DIR/summary.csv"

if pgrep -f "hotstuff-app --conf" >/dev/null 2>&1; then
    echo "Existing hotstuff-app processes are running. Stop them before benchmarking." >&2
    exit 1
fi

HEADER="implementation,scenario,nfaulty,quorum,configured_nodes,active_nodes,drop_propose_pct,repeat,async,client_seconds,confirmed,max_height,avg_latency_s,min_latency_s,p50_latency_s,p95_latency_s,max_latency_s,throughput_ops_s,client_rc,replica_sent_msgs,replica_recv_msgs,proposal_wire_bytes,proposal_bytes_per_confirmed,max_decided,live"
if [[ "$RESUME" != 1 || ! -s "$SUMMARY_CSV" ]]; then
    echo "$HEADER" > "$SUMMARY_CSV"
elif [[ "$(head -n 1 "$SUMMARY_CSV")" != "$HEADER" ]]; then
    echo "Cannot resume: unexpected CSV header in $SUMMARY_CSV" >&2
    exit 1
fi

is_completed() {
    local repeat="$1" async="$2"
    awk -F, -v repeat="$repeat" -v async="$async" \
        'NR > 1 && $8 == repeat && $9 == async { found = 1 } END { exit !found }' \
        "$SUMMARY_CSV"
}

run_one() {
    local repeat="$1" async="$2"
    local run_dir="$OUT_DIR/repeat-${repeat}/async-${async}"
    mkdir -p "$run_dir"
    local pids=()

    pushd "$CONF_DIR" >/dev/null
    for replica in $REPLICAS; do
        HOTSTUFF_DROP_PROPOSE_PCT="$DROP_PROPOSE_PCT" \
        HOTSTUFF_NFAULTY="$NFAULTY" \
        "$ROOT_DIR/examples/hotstuff-app" --conf "${APP_CONF_PREFIX}${replica}.conf" \
            > "$run_dir/replica${replica}.log" 2>&1 &
        pids+=("$!")
    done
    popd >/dev/null

    cleanup() {
        for pid in "${pids[@]}"; do kill "$pid" >/dev/null 2>&1 || true; done
        for pid in "${pids[@]}"; do wait "$pid" >/dev/null 2>&1 || true; done
    }
    trap cleanup RETURN
    sleep "$WARMUP_SECONDS"

    local start_epoch end_epoch client_rc
    start_epoch="$(date +%s.%N)"
    set +e
    (
        cd "$CONF_DIR" || exit 1
        timeout "$CLIENT_SECONDS" "$ROOT_DIR/examples/hotstuff-client" \
            --conf "$CLIENT_CONF" --idx "$CLIENT_IDX" --iter -1 --max-async "$async"
    ) > "$run_dir/client.stdout" 2> "$run_dir/client.log"
    client_rc="$?"
    set -e
    end_epoch="$(date +%s.%N)"
    cleanup
    trap - RETURN

    python3 - "$run_dir" "$SUMMARY_CSV" "$IMPLEMENTATION" "$SCENARIO" \
        "$NFAULTY" "$REPLICAS" "$DROP_PROPOSE_PCT" "$repeat" "$async" \
        "$CLIENT_SECONDS" "$client_rc" "$start_epoch" "$end_epoch" <<'PY'
import csv, pathlib, re, statistics, sys

(run_dir_s, summary_s, implementation, scenario, nfaulty_s, replicas_s,
 drop_s, repeat_s, async_s, seconds_s, rc_s, start_s, end_s) = sys.argv[1:]
run_dir, summary = pathlib.Path(run_dir_s), pathlib.Path(summary_s)
fin_re = re.compile(r"got <fin .*?cmd_height=(\d+).*?wall: ([0-9.]+)")
sent_re = re.compile(r"\[hotstuff info\] sent: (\d+)")
recv_re = re.compile(r"\[hotstuff info\] recv: (\d+)")
decided_re = re.compile(r"\[hotstuff info\] decided: (\d+)")
proposal_re = re.compile(r"proposal_wire_bytes: (\d+)")
config_re = re.compile(r"benchmark_config replicas=(\d+) nfaulty=(\d+) quorum=(\d+)")

latencies, heights = [], []
for line in (run_dir / "client.log").read_text(errors="replace").splitlines():
    match = fin_re.search(line)
    if match:
        heights.append(int(match.group(1)))
        latencies.append(float(match.group(2)))

def percentile(values, pct):
    if not values: return 0.0
    ordered = sorted(values)
    return ordered[int(round((pct / 100) * (len(ordered) - 1)))]

sent = recv = decided = proposal_bytes = configured = quorum = 0
for path in sorted(run_dir.glob("replica*.log")):
    text = path.read_text(errors="replace")
    vals = [int(x) for x in sent_re.findall(text)]
    sent += max(vals) if vals else 0
    vals = [int(x) for x in recv_re.findall(text)]
    recv += max(vals) if vals else 0
    vals = [int(x) for x in decided_re.findall(text)]
    decided = max([decided] + vals)
    proposal_bytes += sum(int(x) for x in proposal_re.findall(text))
    match = config_re.search(text)
    if match:
        configured, quorum = int(match.group(1)), int(match.group(3))

confirmed = len(latencies)
elapsed = max(0.000001, float(end_s) - float(start_s))
row = {
    "implementation": implementation, "scenario": scenario,
    "nfaulty": nfaulty_s, "quorum": quorum,
    "configured_nodes": configured, "active_nodes": len(replicas_s.split()),
    "drop_propose_pct": drop_s, "repeat": repeat_s, "async": async_s,
    "client_seconds": seconds_s, "confirmed": confirmed,
    "max_height": max(heights) if heights else 0,
    "avg_latency_s": f"{statistics.mean(latencies):.6f}" if latencies else "0.000000",
    "min_latency_s": f"{min(latencies):.6f}" if latencies else "0.000000",
    "p50_latency_s": f"{percentile(latencies, 50):.6f}",
    "p95_latency_s": f"{percentile(latencies, 95):.6f}",
    "max_latency_s": f"{max(latencies):.6f}" if latencies else "0.000000",
    "throughput_ops_s": f"{confirmed / elapsed:.6f}", "client_rc": rc_s,
    "replica_sent_msgs": sent, "replica_recv_msgs": recv,
    "proposal_wire_bytes": proposal_bytes,
    "proposal_bytes_per_confirmed": f"{proposal_bytes / confirmed:.3f}" if confirmed else "0.000",
    "max_decided": decided, "live": int(confirmed > 0),
}
with summary.open("a", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=row.keys())
    writer.writerow(row)
(run_dir / "summary.txt").write_text("\n".join(f"{k}: {v}" for k, v in row.items()) + "\n")
print((run_dir / "summary.txt").read_text(), end="")
PY
}

for repeat in $(seq 1 "$REPETITIONS"); do
    for async in $ASYNC_VALUES; do
        if [[ "$RESUME" == 1 ]] && is_completed "$repeat" "$async"; then
            echo "=== Skipping completed: $IMPLEMENTATION $SCENARIO repeat=$repeat async=$async f=$NFAULTY ==="
            continue
        fi
        echo "=== $IMPLEMENTATION $SCENARIO repeat=$repeat async=$async f=$NFAULTY replicas=[$REPLICAS] ==="
        run_one "$repeat" "$async"
    done
done

echo "Summary CSV: $SUMMARY_CSV"
