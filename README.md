# Sync HotStuff with Erasure-Coded Command Propagation

This course research prototype extends [librightstuff](https://github.com/hot-stuff/librightstuff) with Reed–Solomon (RS) propagation of complete command batches. The original project history and Apache-2.0 license are retained. This is an experimental consensus implementation, not a production BFT system.

## What this version implements

In strict mode, a client sends a complete command to the leader. The leader forms a versioned command batch, creates `n` RS shards, signs each proposal, and sends a shard to each replica. Replicas forward their authenticated shards in a Re-propose step. After collecting `k` distinct shards, a replica reconstructs the batch, checks its payload hash, re-parses and hashes every command, rebuilds the original block, and verifies its hash and QC. Voting and commit processing require successful reconstruction. The leader follows the same reconstruction gate.

The earlier hash-list RS path remains for compatibility. Shadow mode compares reconstruction with a locally available complete command. A benchmark-only full-broadcast mode sends the same complete batch to every replica without RS; it provides a like-for-like leader-ingress comparison with strict RS. These modes are selected explicitly by the test environment; see the [final experimental report](benchmarks/full-command-payload-20260909-final/REPORT.md) for wire formats, controls, and limits.

## Build from a clean clone

The project was tested on an Ubuntu VM with CMake, a C/C++ toolchain, OpenSSL, libuv, and the repository's secp256k1 and Salticidae submodules. Install the platform dependencies first, then run:

```bash
git clone https://github.com/X-Mobius/sync-hotstuff-erasure-coding.git
cd sync-hotstuff-erasure-coding
git submodule update --init --recursive
bash scripts/apply_salticidae_patches.sh
cmake -S . -B . -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON
make -j2
```

The pinned upstream Salticidae commit needs two small, tracked patches: serialized-message byte accounting and a libuv handle destruction fix. `apply_salticidae_patches.sh` checks the pinned commit and applies them idempotently. The Salticidae checkout then appears modified by design; no unpublished submodule commit is required.

## Focused tests

```bash
./test/test_command_batch
./test/test_full_proposal
./test/test_full_command_state
./test/test_reproposal_rs
bash scripts/run_full_command_asan.sh
```

The test suite covers command format boundaries, 944 RS recovery subsets, signature and byte-tampering rejection, fragment ordering/conflicts, and state recovery. The ASan script builds and tests the protocol and dependency objects with leak detection. The archived full matrix uses seven local replicas and is described in the final report; its scripts require an explicitly prepared original baseline and generated seven-node configuration.

## Results and interpretation

The [final report and CSVs](benchmarks/full-command-payload-20260909-final/REPORT.md) contain the 81/81 performance runs, 15/15 positive correctness runs, and the insufficient-shards negative test. In the seven-replica, 64 KiB command, batch-64 setting, strict RS versus the *full-broadcast comparator* reduced leader Proposal bytes per confirmed command by **74.91%** and all leader outgoing message bytes by **49.81%**. Cluster outgoing bytes increased by about **99.39%**, and throughput decreased by about **2.42%**. This comparison uses the same client-to-leader entry path in both modes.

The native baseline is a different topology: its client sends complete commands to all replicas while the leader mainly proposes hashes. It must not be used to claim that RS reduces the native baseline's leader or total network traffic. Earlier 28.4%/52.2% figures describe the hash-list implementation, not this complete-command version.

## Limits

The tests use seven processes on one Ubuntu VM and a stable leader. Offline-node and missing-shard tests do not establish resistance to malicious fragments or a malicious leader. The client does not automatically follow view changes. The implementation has bounded in-memory caches and no production-grade backpressure, persistence, or long-term state transfer. Network-byte measurements count application serialized messages and Salticidae frames, not TCP/IP retransmissions or physical-link overhead. Full details are in the report.

## Attribution

This repository is derived from the upstream [librightstuff](https://github.com/hot-stuff/librightstuff) / [libhotstuff](https://github.com/hot-stuff/libhotstuff) work. The original commit history, notices, license, and submodule attribution remain in place. RS helper code draws on Intel ISA-L examples, as described in the original project files.
