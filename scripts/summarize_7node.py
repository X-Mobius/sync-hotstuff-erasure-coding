#!/usr/bin/env python3
import csv
from collections import defaultdict
from pathlib import Path
import statistics
import sys

root = Path(sys.argv[1]).resolve()
rows = []
for path in root.glob("**/summary.csv"):
    with path.open(newline="") as handle:
        rows.extend(csv.DictReader(handle))

keys = ("implementation", "nfaulty", "quorum", "scenario", "active_nodes", "drop_propose_pct", "async")
groups = defaultdict(list)
for row in rows:
    groups[tuple(row[key] for key in keys)].append(row)

fields = list(keys) + ["runs", "live_runs", "throughput_mean", "throughput_stddev",
                       "p95_latency_mean", "p95_latency_stddev", "proposal_bytes_mean",
                       "proposal_bytes_per_confirmed_mean"]
aggregates = []
for key, values in sorted(groups.items()):
    metric = lambda name: [float(row[name]) for row in values]
    std = lambda xs: statistics.stdev(xs) if len(xs) > 1 else 0.0
    throughput, p95 = metric("throughput_ops_s"), metric("p95_latency_s")
    aggregates.append(dict(zip(keys, key), runs=len(values),
        live_runs=sum(int(row["live"]) for row in values),
        throughput_mean=f"{statistics.mean(throughput):.6f}", throughput_stddev=f"{std(throughput):.6f}",
        p95_latency_mean=f"{statistics.mean(p95):.6f}", p95_latency_stddev=f"{std(p95):.6f}",
        proposal_bytes_mean=f"{statistics.mean(metric('proposal_wire_bytes')):.3f}",
        proposal_bytes_per_confirmed_mean=f"{statistics.mean(metric('proposal_bytes_per_confirmed')):.3f}"))

with (root / "aggregate.csv").open("w", newline="") as handle:
    writer = csv.DictWriter(handle, fieldnames=fields)
    writer.writeheader(); writer.writerows(aggregates)

lines = ["# 7-Node Benchmark Report", "", f"Raw runs: {len(rows)}", "",
         "Proposal loss is simulated message loss, not bit corruption or link-layer retransmission.", "",
         "| Implementation | f | Quorum | Scenario | Active | Async | Live | Throughput | P95 latency | Proposal bytes/confirmed |",
         "|---|---:|---:|---|---:|---:|---:|---:|---:|---:|"]
for row in aggregates:
    lines.append(f"| {row['implementation']} | {row['nfaulty']} | {row['quorum']} | {row['scenario']} | "
                 f"{row['active_nodes']} | {row['async']} | {row['live_runs']}/{row['runs']} | "
                 f"{row['throughput_mean']} +/- {row['throughput_stddev']} | "
                 f"{row['p95_latency_mean']} +/- {row['p95_latency_stddev']} | "
                 f"{row['proposal_bytes_per_confirmed_mean']} |")
(root / "REPORT.md").write_text("\n".join(lines) + "\n")
print(root / "REPORT.md")
