# Performance Benchmark

This script runs a reproducible local HotStuff demo benchmark and summarizes
client finality latency and replica message counts.

## Quick Smoke Test

```bash
cd ~/librightstuff
CLIENT_SECONDS=12 ASYNC_VALUES=3 ./scripts/perf_benchmark.sh
```

## Full Benchmark

```bash
cd ~/librightstuff
CLIENT_SECONDS=60 ASYNC_VALUES="1 3 8 16" ./scripts/perf_benchmark.sh
```

The script creates a timestamped directory under `benchmarks/`, for example:

```text
benchmarks/20260703-112319/
```

Important files:

```text
summary.csv                 Combined benchmark results
async-3/client.log          Client finality log
async-3/replica0.log        Replica 0 log
async-3/replica1.log        Replica 1 log
async-3/replica2.log        Replica 2 log
async-3/summary.txt         Human-readable result for one run
```

## Metrics

- `confirmed`: number of finalized client commands observed during the run.
- `avg_latency_s`: average finality latency reported by the client.
- `p50_latency_s`: median finality latency.
- `p95_latency_s`: 95th percentile finality latency.
- `throughput_ops_s`: finalized commands per second during the measured client window.
- `replica_sent_msgs`: sum of the largest `sent:` counters reported by replica logs.
- `replica_recv_msgs`: sum of the largest `recv:` counters reported by replica logs.
- `max_height`: highest finalized command height observed by the client.

`client_rc=124` is expected because the benchmark runs the client with
`timeout`. It means the measured time window ended normally.

## Resume Wording

Use measured metrics only after running the full benchmark on your machine.
For example:

```text
Ran a 3-replica benchmark with max_async=3 for 60 seconds; the prototype
finalized N commands with average finality latency X.XXXs and p95 latency
Y.YYYs.
```

Avoid claiming total communication reduction unless you also run a baseline
comparison against unmodified libhotstuff under the same parameters.
