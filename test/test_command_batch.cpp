#include "hotstuff/command_batch.h"
#include "hotstuff/client.h"
#include "code_function.h"
#include <cassert>
#include <functional>
#include <iostream>
using namespace hotstuff;
void rejects(const std::function<void()> &fn) {
    bool rejected = false; try { fn(); } catch (const std::exception &) { rejected = true; }
    assert(rejected);
}
int main() {
    unsetenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES");
    { CommandDummy c(7,9); DataStream s; s << c; auto bytes=stream_bytes(s);
      CommandDummy d; s >> d; assert(!s.size() && c.get_hash()==d.get_hash()); }
    for(const char *length: {"0","1","31","32","33","1024","16384","65536"}) {
        setenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES",length,1);
        CommandDummy c(7,9); DataStream s; s << c; auto bytes=stream_bytes(s);
        CommandDummy d; s >> d; assert(!s.size() && c.get_hash()==d.get_hash());
        DataStream out; out << d; assert(stream_bytes(out)==bytes);
        auto bad=bytes; bad.pop_back();
        rejects([&]{ DataStream truncated(bad); CommandDummy x; truncated >> x; });
        bad=bytes; bad.push_back(0);
        rejects([&]{ DataStream extra(bad); CommandDummy x; extra >> x; });
    }
    unsetenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES");
    size_t trials = 0;
    for (size_t len: {0,1,31,32,33,1024,16384,65536}) {
        std::vector<bytearray_t> cmds{bytearray_t(len, 0xa5), bytearray_t(len/2, 0x17)};
        std::vector<uint256_t> hashes;
        for (auto &c: cmds) { DataStream s(c); hashes.push_back(s.get_hash()); }
        auto payload = encode_command_batch(cmds);
        assert(decode_command_batch(payload, payload.size(), hashes) == cmds);
        rejects([&] { decode_command_batch(payload, payload.size()+1, hashes); });
        auto bad = payload; bad.pop_back();
        rejects([&] { decode_command_batch(bad, bad.size(), hashes); });
        bad = payload; bad.push_back(0);
        rejects([&] { decode_command_batch(bad, bad.size(), hashes); });
        for (size_t i=0; i<payload.size(); i += len > 1024 ? 997 : 1) {
            bad=payload; bad[i]^=1;
            rejects([&] { decode_command_batch(bad,bad.size(),hashes); });
        }
        for (int n: {4,7}) for (int k=2;k<=n-2;++k) {
            int shard_size = ((payload.size()+k-1)/k+31)/32*32;
            std::vector<bytearray_t> shards(n, bytearray_t(shard_size,0));
            std::vector<unsigned char*> ptrs(n);
            for(int i=0;i<n;++i) ptrs[i]=shards[i].data();
            for(size_t i=0;i<payload.size();++i) shards[i/shard_size][i%shard_size]=payload[i];
            assert(rs_encode_shards(n,k,shard_size,ptrs.data())==0);
            for (unsigned mask=0; mask<(1u<<n);++mask) {
                if (__builtin_popcount(mask)!=k) continue;
                auto copy=shards;
                std::vector<unsigned char> present(n,0);
                // Reverse arrival order; duplicate arrivals retain the same slot.
                for(int i=n-1;i>=0;--i) {
                    present[i]=(mask>>i)&1;
                    if(!present[i]) std::fill(copy[i].begin(),copy[i].end(),0);
                    ptrs[i]=copy[i].data();
                }
                assert(rs_reconstruct_data(n,k,shard_size,ptrs.data(),present.data())==0);
                bytearray_t restored(payload.size());
                for(size_t i=0;i<restored.size();++i) restored[i]=copy[i/shard_size][i%shard_size];
                assert(decode_command_batch(restored,restored.size(),hashes)==cmds);
                ++trials;
            }
        }
    }
    std::cout << "command batch tests passed; RS subsets=" << trials << std::endl;
}
