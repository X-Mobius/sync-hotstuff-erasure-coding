#ifndef HOTSTUFF_COMMAND_BATCH_H
#define HOTSTUFF_COMMAND_BATCH_H
#include "hotstuff/type.h"
#include <stdexcept>
#include <cstdlib>
#include <cstring>
namespace hotstuff {
inline bool full_commands() {
    const char *p = std::getenv("HOTSTUFF_ERASURE_FULL_COMMANDS");
    return p && std::strcmp(p, "1") == 0;
}
inline bool strict_commands() {
    const char *p = std::getenv("HOTSTUFF_REQUIRE_COMMAND_RECONSTRUCTION");
    return p && std::strcmp(p, "1") == 0;
}
// Explicit benchmark-only control: replicate the complete batch without RS.
// It is never enabled by a message received from the network.
inline bool benchmark_full_broadcast() {
    const char *p = std::getenv("HOTSTUFF_BENCHMARK_FULL_BROADCAST");
    return p && std::strcmp(p, "1") == 0;
}
constexpr uint32_t MAX_BATCH_COMMANDS = 4096;
constexpr uint32_t MAX_COMMAND_BYTES = 1024 * 1024;
constexpr uint32_t MAX_BATCH_BYTES = 8 * 1024 * 1024;
inline bytearray_t stream_bytes(DataStream &s) {
    if (!s.size()) return {};
    return bytearray_t(s.data(), s.data() + s.size());
}
inline bytearray_t encode_command_batch(const std::vector<bytearray_t> &commands) {
    if (commands.size() > MAX_BATCH_COMMANDS) throw std::runtime_error("batch count limit");
    DataStream s;
    s << htole(uint32_t(1)) << htole(uint32_t(commands.size()));
    for (const auto &cmd: commands) {
        if (cmd.size() > MAX_COMMAND_BYTES || s.size() + 4 + cmd.size() > MAX_BATCH_BYTES)
            throw std::runtime_error("batch byte limit");
        s << htole(uint32_t(cmd.size())) << cmd;
    }
    return stream_bytes(s);
}
// Lengths are checked before DataStream reads, including NOCHECK builds.
inline std::vector<bytearray_t> decode_command_batch(const bytearray_t &bytes,
        uint32_t declared_size, const std::vector<uint256_t> &hashes) {
    if (bytes.size() != declared_size || bytes.size() < 8 || bytes.size() > MAX_BATCH_BYTES ||
            hashes.size() > MAX_BATCH_COMMANDS) throw std::runtime_error("batch size mismatch");
    DataStream s(bytes);
    uint32_t version, count;
    s >> version >> count;
    if (letoh(version) != 1 || letoh(count) != hashes.size())
        throw std::runtime_error("batch version/count mismatch");
    std::vector<bytearray_t> result;
    for (const auto &hash: hashes) {
        if (s.size() < 4) throw std::runtime_error("truncated command length");
        uint32_t len; s >> len; len = letoh(len);
        if (len > MAX_COMMAND_BYTES || len > s.size()) throw std::runtime_error("command length limit");
        bytearray_t cmd;
        if (len) { const auto *p = s.get_data_inplace(len); cmd.assign(p, p + len); }
        DataStream cs(cmd);
        if (cs.get_hash() != hash) throw std::runtime_error("command hash mismatch");
        result.push_back(std::move(cmd));
    }
    if (s.size()) throw std::runtime_error("batch trailing data");
    return result;
}
}
#endif
