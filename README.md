# Sync HotStuff Erasure Coding Prototype

This repository is a course research prototype based on
[libhotstuff](https://github.com/hot-stuff/libhotstuff). It explores how
erasure coding can be integrated into the proposal path of HotStuff-style BFT
state machine replication.

The project focuses on a prototype-level modification rather than a production
consensus implementation. Its main goal is to study whether a leader can reduce
the amount of full-block data it sends during the `Propose` phase by sending
encoded fragments and allowing replicas to reconstruct the original block before
voting.

## Background

HotStuff is a Byzantine fault-tolerant consensus protocol designed for
blockchain-style state machine replication. In the original proposal flow, the
leader sends the proposed block data to replicas. When block data becomes large,
this can make the leader a communication bottleneck.

This project studies an erasure-coding-based proposal prototype:

1. The leader encodes block commands into fragments.
2. Encoded proposal parts carry metadata such as the fragment index, original
   command count, and original block hash.
3. Replicas collect enough encoded parts, decode the original command list, and
   rebuild the original block.
4. The rebuilt block is checked against the original block hash before it enters
   the normal HotStuff proposal handling path.

The current implementation keeps the original libhotstuff safety and liveness
logic mostly intact, and adds the erasure-coding path around proposal delivery.

## Main Changes

- Added Reed-Solomon style erasure coding helpers derived from Intel ISA-L test
  code.
- Integrated encoding before the leader broadcasts proposal data.
- Extended `Proposal` messages with erasure-coding metadata.
- Added replica-side fragment caching and decode-before-vote logic.
- Rebuilt decoded blocks and verified their hash before passing them to the
  original HotStuff proposal handler.
- Fixed several integration issues found during testing, including fragment
  indexing, metadata transmission, buffer initialization, memory management, and
  block reconstruction.

## Repository Layout

```text
include/code_function.h      Erasure coding function interface
include/ec_base.h            GF table data used by the coding functions
src/code_function.c          Encoding and decoding implementation
src/consensus.cpp            Proposal-side encoding integration
src/hotstuff.cpp             Replica-side decoding and proposal handling
test/test_function.c         Standalone erasure coding recovery test
```

The original libhotstuff structure is otherwise preserved.

## Build

The project was tested on Ubuntu 20.04.

Install dependencies:

```bash
sudo apt-get install libssl-dev libuv1-dev cmake make g++
```

Build:

```bash
cd ~/librightstuff
make -j2
```

If building from a clean checkout, initialize submodules and configure CMake as
in the original libhotstuff project:

```bash
git submodule update --init --recursive
cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED=ON -DHOTSTUFF_PROTO_LOG=ON .
make -j2
```

## Run Tests

Run the standalone erasure coding test:

```bash
./test/test_functions
```

Expected output:

```text
Success!
```

Run the original secp256k1 test:

```bash
./test/test_secp256k1
```

## Run Demo

Start replicas in one terminal:

```bash
./scripts/run_demo.sh 0 1 2
```

Start the client in another terminal:

```bash
./scripts/run_demo_client.sh
```

Healthy output contains repeated finality messages such as:

```text
got <fin decision=1 cmd_idx=0 cmd_height=...>
send new cmd ...
```

You can stop the client and replicas with `Ctrl+C`. Replica logs are written to
`log0`, `log1`, and `log2`.

## Notes and Limitations

- This is a research/course prototype, not a production-ready BFT system.
- The current demo-oriented implementation broadcasts encoded proposal parts to
  all replicas to keep the original libhotstuff demo path stable.
- A fully optimized design should implement the complete `Re-propose` step,
  where replicas forward fragments to each other and decode after collecting
  enough valid fragments.
- Erasure coding can reduce the leader's full-block broadcast pressure in the
  intended design, but redundant fragments and re-proposal forwarding may add
  extra communication. End-to-end communication benefits should be evaluated
  with experiments.

## Acknowledgements

This project is based on the open-source
[libhotstuff](https://github.com/hot-stuff/libhotstuff) prototype.

Original HotStuff papers:

- [HotStuff: BFT Consensus in the Lens of Blockchain](https://arxiv.org/abs/1803.05069)
- [PODC 2019 paper](https://dl.acm.org/citation.cfm?id=3331591)

Erasure coding code in this project is adapted from concepts and test code in
Intel ISA-L.
