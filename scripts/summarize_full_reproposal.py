#!/usr/bin/env python3
import csv
import pathlib
import statistics
import sys


def read_rows(root):
    rows = []
    for path in pathlib.Path(root).rglob("summary.csv"):
        with path.open(newline="") as handle:
            for row in csv.DictReader(handle):
                row["source"] = str(path)
                rows.append(row)
    return rows


def number(row, key):
    try:
        return float(row.get(key, 0) or 0)
    except ValueError:
        return 0.0


def summarize(rows):
    groups = {}
    for row in rows:
        key = (row["implementation"], row["scenario"])
        groups.setdefault(key, []).append(row)
    result = []
    for (implementation, scenario), values in sorted(groups.items()):
        def mean(key):
            return statistics.mean(number(row, key) for row in values)
        def sd(key):
            data = [number(row, key) for row in values]
            return statistics.stdev(data) if len(data) > 1 else 0.0
        confirmed = mean("confirmed")
        total = mean("proposal_wire_bytes")
        direct = mean("proposal_direct_wire_bytes")
        leader = direct if direct else total
        result.append({
            "implementation": implementation,
            "scenario": scenario,
            "runs": len(values),
            "block_size": int(mean("block_size")),
            "drop_pct": int(mean("drop_propose_pct")),
            "send_copies": int(mean("erasure_send_copies") or 1),
            "confirmed_mean": confirmed,
            "throughput_mean": mean("throughput_ops_s"),
            "throughput_sd": sd("throughput_ops_s"),
            "p95_mean": mean("p95_latency_s"),
            "proposal_B_per_confirmed": total / confirmed if confirmed else 0,
            "leader_B_per_confirmed": leader / confirmed if confirmed else 0,
            "reconstructed_mean": mean("erasure_reconstructed"),
            "hash_failures": sum(number(row, "erasure_hash_failures") for row in values),
            "live_runs": sum(int(number(row, "live") > 0) for row in values),
        })
    return result


def write_report(summary, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    fields = list(summary[0].keys())
    with (out_dir / "aggregate.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(summary)

    lookup = {(r["implementation"], r["scenario"]): r for r in summary}
    lines = [
        "# Full Re-propose 7-node validation",
        "",
        "## Method",
        "",
        "- Seven local replica processes on one Ubuntu VM; `f=2/quorum=5` unless noted.",
        "- Strict mode rejects votes for blocks not reconstructed from erasure shards.",
        "- Each reported group contains three 20-second runs.",
        "- Proposal loss is independent application-level message dropping, not bit corruption.",
        "- Proposal bytes include serialized proposal metadata; retransmitted copies are counted.",
        "",
        "## Payload scaling",
        "",
        "| Block size | Implementation | Throughput ops/s | P95 s | Leader B/confirmed | Cluster B/confirmed |",
        "|---:|---|---:|---:|---:|---:|",
    ]
    for block in (1, 16, 64):
        scenario = f"b{block}-normal"
        for impl in ("baseline", "full-reproposal"):
            row = lookup[(impl, scenario)]
            lines.append(
                f"| {block} | {impl} | {row['throughput_mean']:.3f} +/- {row['throughput_sd']:.3f} "
                f"| {row['p95_mean']:.3f} | {row['leader_B_per_confirmed']:.1f} "
                f"| {row['proposal_B_per_confirmed']:.1f} |"
            )
    lines += ["", "## Fault and loss validation", "",
              "| Scenario | Live runs | Throughput ops/s | P95 s | Send copies | Cluster B/confirmed | Hash failures |",
              "|---|---:|---:|---:|---:|---:|---:|"]
    fault_keys = [
        ("full-reproposal", "b1-missing2"),
        ("full-reproposal", "b1-missing3"),
        ("full-reproposal", "noise30-c1"),
        ("full-reproposal", "noise70-c3"),
        ("full-reproposal", "noise70-c5"),
    ]
    for key in fault_keys:
        row = lookup[key]
        lines.append(
            f"| {row['scenario']} | {row['live_runs']}/{row['runs']} "
            f"| {row['throughput_mean']:.3f} +/- {row['throughput_sd']:.3f} "
            f"| {row['p95_mean']:.3f} | {row['send_copies']} "
            f"| {row['proposal_B_per_confirmed']:.1f} | {int(row['hash_failures'])} |"
        )
    b16 = lookup[("baseline", "b16-normal")]
    r16 = lookup[("full-reproposal", "b16-normal")]
    b64 = lookup[("baseline", "b64-normal")]
    r64 = lookup[("full-reproposal", "b64-normal")]
    lines += [
        "",
        "## Conclusions",
        "",
        f"- At block size 16, leader proposal bytes per confirmation fell by "
        f"{(1-r16['leader_B_per_confirmed']/b16['leader_B_per_confirmed'])*100:.1f}%.",
        f"- At block size 64, leader proposal bytes per confirmation fell by "
        f"{(1-r64['leader_B_per_confirmed']/b64['leader_B_per_confirmed'])*100:.1f}%.",
        "- Cluster-total proposal traffic remained above baseline because every shard is re-proposed to every peer.",
        "- Five copies restored stable progress in the 70% drop experiment, but at very high communication cost.",
        "- These experiments validate crash/drop liveness and data reconstruction, not Byzantine shard authentication.",
    ]
    (out_dir / "REPORT.md").write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        raise SystemExit("usage: summarize_full_reproposal.py MAIN_ROOT RETEST_ROOT OUT_DIR")
    rows = read_rows(sys.argv[1]) + read_rows(sys.argv[2])
    # Superseded loss runs from the first matrix are retained as raw data but
    # excluded from the canonical aggregate after the stale-timer fix.
    rows = [row for row in rows if row["scenario"] not in {"b1-noise30", "b1-noise70"}]
    write_report(summarize(rows), pathlib.Path(sys.argv[3]))
