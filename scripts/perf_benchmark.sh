#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ASYNC_VALUES="${ASYNC_VALUES:-1 3 8 16}"
WARMUP_SECONDS="${WARMUP_SECONDS:-3}"
CLIENT_SECONDS="${CLIENT_SECONDS:-30}"
REPLICAS="${REPLICAS:-0 1 2}"
OUT_DIR="${OUT_DIR:-benchmarks/$(date +%Y%m%d-%H%M%S)}"
CLIENT_IDX="${CLIENT_IDX:-0}"
CONF_DIR="${CONF_DIR:-$ROOT_DIR}"
CLIENT_CONF="${CLIENT_CONF:-hotstuff.conf}"
APP_CONF_PREFIX="${APP_CONF_PREFIX:-hotstuff-sec}"

mkdir -p "$OUT_DIR"
SUMMARY_CSV="$OUT_DIR/summary.csv"

if pgrep -f "hotstuff-app --conf" >/dev/null 2>&1; then
    echo "Existing hotstuff-app processes are running. Stop them before benchmarking." >&2
    exit 1
fi

echo "async,client_seconds,confirmed,max_height,avg_latency_s,min_latency_s,p50_latency_s,p95_latency_s,max_latency_s,throughput_ops_s,client_rc,replica_sent_msgs,replica_recv_msgs,max_decided" > "$SUMMARY_CSV"

run_one() {
    local async="$1"
    local run_dir="$OUT_DIR/async-${async}"
    mkdir -p "$run_dir"

    local pids=()
    for replica in $REPLICAS; do
        (
            cd "$CONF_DIR"
            "$ROOT_DIR/examples/hotstuff-app" --conf "${APP_CONF_PREFIX}${replica}.conf"
        ) \
            > "$run_dir/replica${replica}.log" 2>&1 &
        pids+=("$!")
    done

    cleanup() {
        for pid in "${pids[@]}"; do
            kill "$pid" >/dev/null 2>&1 || true
        done
        for pid in "${pids[@]}"; do
            wait "$pid" >/dev/null 2>&1 || true
        done
    }
    trap cleanup RETURN

    sleep "$WARMUP_SECONDS"

    local start_epoch end_epoch client_rc
    start_epoch="$(date +%s.%N)"
    set +e
    (
        cd "$CONF_DIR"
        timeout "$CLIENT_SECONDS" "$ROOT_DIR/examples/hotstuff-client" \
        --conf "$CLIENT_CONF" \
        --idx "$CLIENT_IDX" \
        --iter -1 \
        --max-async "$async" \
    ) > "$run_dir/client.stdout" 2> "$run_dir/client.log"
    client_rc="$?"
    set -e
    end_epoch="$(date +%s.%N)"

    cleanup
    trap - RETURN

    python3 - "$run_dir" "$SUMMARY_CSV" "$async" "$CLIENT_SECONDS" "$client_rc" "$start_epoch" "$end_epoch" <<'PY'
import csv
import datetime as dt
import pathlib
import re
import statistics
import sys

run_dir = pathlib.Path(sys.argv[1])
summary_csv = pathlib.Path(sys.argv[2])
async_value = sys.argv[3]
client_seconds = sys.argv[4]
client_rc = sys.argv[5]
start_epoch = float(sys.argv[6])
end_epoch = float(sys.argv[7])

fin_re = re.compile(
    r"^(?P<ts>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+).*"
    r"got <fin .*?cmd_height=(?P<height>\d+).*?"
    r"wall: (?P<wall>[0-9.]+)"
)
sent_re = re.compile(r"\[hotstuff info\] sent: (?P<n>\d+)")
recv_re = re.compile(r"\[hotstuff info\] recv: (?P<n>\d+)")
decided_re = re.compile(r"\[hotstuff info\] decided: (?P<n>\d+)")

latencies = []
heights = []

client_log = run_dir / "client.log"
for line in client_log.read_text(errors="replace").splitlines():
    m = fin_re.search(line)
    if not m:
        continue
    latencies.append(float(m.group("wall")))
    heights.append(int(m.group("height")))

def percentile(values, pct):
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = int(round((pct / 100.0) * (len(ordered) - 1)))
    return ordered[idx]

confirmed = len(latencies)
elapsed = max(0.000001, end_epoch - start_epoch)
throughput = confirmed / elapsed

sent_total = 0
recv_total = 0
max_decided = 0
for log_path in sorted(run_dir.glob("replica*.log")):
    text = log_path.read_text(errors="replace")
    sent_values = [int(m.group("n")) for m in sent_re.finditer(text)]
    recv_values = [int(m.group("n")) for m in recv_re.finditer(text)]
    decided_values = [int(m.group("n")) for m in decided_re.finditer(text)]
    if sent_values:
        sent_total += max(sent_values)
    if recv_values:
        recv_total += max(recv_values)
    if decided_values:
        max_decided = max(max_decided, max(decided_values))

row = {
    "async": async_value,
    "client_seconds": client_seconds,
    "confirmed": confirmed,
    "max_height": max(heights) if heights else 0,
    "avg_latency_s": f"{statistics.mean(latencies):.6f}" if latencies else "0.000000",
    "min_latency_s": f"{min(latencies):.6f}" if latencies else "0.000000",
    "p50_latency_s": f"{percentile(latencies, 50):.6f}",
    "p95_latency_s": f"{percentile(latencies, 95):.6f}",
    "max_latency_s": f"{max(latencies):.6f}" if latencies else "0.000000",
    "throughput_ops_s": f"{throughput:.6f}",
    "client_rc": client_rc,
    "replica_sent_msgs": sent_total,
    "replica_recv_msgs": recv_total,
    "max_decided": max_decided,
}

with summary_csv.open("a", newline="") as f:
    writer = csv.DictWriter(f, fieldnames=list(row.keys()))
    writer.writerow(row)

summary = run_dir / "summary.txt"
summary.write_text(
    "\n".join(f"{k}: {v}" for k, v in row.items()) + "\n",
    encoding="utf-8",
)
print(summary.read_text(), end="")
PY
}

for async in $ASYNC_VALUES; do
    echo "=== Running benchmark: seconds=$CLIENT_SECONDS max_async=$async replicas=[$REPLICAS] ==="
    run_one "$async"
done

echo "Benchmark results written to: $OUT_DIR"
echo "Summary CSV: $SUMMARY_CSV"
