#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$(mktemp -d "$HOME/full-command-asan-XXXXXXXX")"
mkdir -p "$out/build/secp256k1"
cp -a "$root/secp256k1/." "$out/build/secp256k1/"
cmake -S "$root" -B "$out/build" \
    -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer -g -O1' \
    -DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer -g -O1' > "$out/configure.log" 2>&1
# Build all project/network objects with ASan; the existing C crypto library is reused.
cmake --build "$out/build" -j2 --target hotstuff salticidae > "$out/build.log" 2>&1
for test in test_command_batch test_full_proposal test_full_command_state; do
    c++ -g -O1 -fsanitize=address -fno-omit-frame-pointer -std=c++17 \
        -I"$root/include" -I"$root/salticidae/include" -I"$root/secp256k1/include" \
        "$root/test/$test.cpp" "$out/build/CMakeFiles/hotstuff.dir/src/"*.o \
        "$out/build/salticidae/CMakeFiles/salticidae.dir/src/"*.o \
        "$root/secp256k1/.libs/libsecp256k1.a" -luv -lssl -lcrypto -lpthread -o "$out/$test"
    ASAN_OPTIONS=detect_leaks=1 "$out/$test" > "$out/$test.log" 2>&1
done
printf 'All ASan tests passed: %s\n' "$out"
