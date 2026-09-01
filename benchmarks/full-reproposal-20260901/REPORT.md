# Full Re-propose 7-node validation

## Method

- Seven local replica processes on one Ubuntu VM; `f=2/quorum=5` unless noted.
- Strict mode rejects votes for blocks not reconstructed from erasure shards.
- Each reported group contains three 20-second runs.
- Proposal loss is independent application-level message dropping, not bit corruption.
- Proposal bytes include serialized proposal metadata; retransmitted copies are counted.

## Payload scaling

| Block size | Implementation | Throughput ops/s | P95 s | Leader B/confirmed | Cluster B/confirmed |
|---:|---|---:|---:|---:|---:|
| 1 | baseline | 7.197 +/- 0.001 | 2.044 | 3153.3 | 3153.3 |
| 1 | full-reproposal | 7.198 +/- 0.000 | 2.036 | 3493.3 | 27946.7 |
| 16 | baseline | 28.791 +/- 0.004 | 2.016 | 394.6 | 394.6 |
| 16 | full-reproposal | 28.792 +/- 0.002 | 2.016 | 282.5 | 2260.0 |
| 64 | baseline | 57.584 +/- 0.002 | 2.022 | 257.8 | 257.8 |
| 64 | full-reproposal | 57.582 +/- 0.005 | 2.020 | 123.1 | 985.0 |

## Fault and loss validation

| Scenario | Live runs | Throughput ops/s | P95 s | Send copies | Cluster B/confirmed | Hash failures |
|---|---:|---:|---:|---:|---:|---:|
| b1-missing2 | 3/3 | 7.198 +/- 0.000 | 2.028 | 1 | 20960.0 | 0 |
| b1-missing3 | 3/3 | 7.195 +/- 0.006 | 2.049 | 1 | 15346.7 | 0 |
| noise30-c1 | 3/3 | 5.415 +/- 1.826 | 2.049 | 1 | 20315.4 | 0 |
| noise70-c3 | 3/3 | 6.281 +/- 1.587 | 2.057 | 3 | 61134.3 | 0 |
| noise70-c5 | 3/3 | 7.198 +/- 0.000 | 2.058 | 5 | 122705.0 | 0 |

## Conclusions

- At block size 16, leader proposal bytes per confirmation fell by 28.4%.
- At block size 64, leader proposal bytes per confirmation fell by 52.2%.
- Cluster-total proposal traffic remained above baseline because every shard is re-proposed to every peer.
- Five copies restored stable progress in the 70% drop experiment, but at very high communication cost.
- These experiments validate crash/drop liveness and data reconstruction, not Byzantine shard authentication.
