#include "hotstuff/hotstuff.h"
#include "hotstuff/liveness.h"
#include "hotstuff/client.h"
#include "code_function.h"
#include <cassert>
#include <iostream>
using namespace hotstuff;
class Node: public HotStuffSecp256k1 {
public:
    Node(const bytearray_t &key): HotStuffSecp256k1(16,0,key,NetAddr("127.0.0.1:0"),new PaceMakerDummyFixed(0,1)) {}
    void state_machine_execute(const Finality &) override {}
    void do_broadcast_vote(const Vote &) override {}
    void set_commit_timer(const block_t &,double) override {}
    bool validate_command_body(const bytearray_t &b) override {
        try { DataStream s(b); CommandDummy c;s >> c;return !s.size(); } catch(...) { return false; }
    }
    bool recovered(const uint256_t &h) { return reconstructed_commands.count(h); }
    using HotStuffSecp256k1::create_quorum_cert;
};
int main() {
    setenv("HOTSTUFF_ERASURE_FULL_COMMANDS","1",1);
    setenv("HOTSTUFF_REQUIRE_COMMAND_RECONSTRUCTION","1",1);
    setenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES","33",1);
    PrivKeySecp256k1 sk;sk.from_rand();DataStream key;key << sk;
    Node node(stream_bytes(key));
    for(int i=0;i<7;++i)node.add_replica(i,NetAddr("127.0.0.1",20000+i),sk.get_pubkey());
    node.on_init(2,1);node.get_pace_maker()->init(&node);
    node.set_vote_disabled(true);
    auto sign=[&](Proposal &p) { p.leader_cert=new PartCertSecp256k1(sk,p.body_commitment()); };
    auto make=[&](int sequence) {
        CommandDummy cmd(1,sequence);DataStream cs;cs << cmd;
        auto payload=encode_command_batch({stream_bytes(cs)});
        auto genesis=node.get_genesis();
        auto blk=block_t(new Block({genesis},{cmd.get_hash()},
            node.create_quorum_cert(Vote::proof_obj_hash(genesis->get_hash())),{},1,genesis,nullptr));
        size_t len=((payload.size()+2)/3+31)/32*32;
        std::vector<bytearray_t> shards(7,bytearray_t(len,0));std::vector<unsigned char*> ptrs(7);
        for(size_t i=0;i<payload.size();++i)shards[i/len][i%len]=payload[i];
        for(int i=0;i<7;++i)ptrs[i]=shards[i].data();
        assert(!rs_encode_shards(7,3,len,ptrs.data()));
        std::vector<Proposal> props;
        for(int i=0;i<7;++i) {
            Proposal p(0,blk,&node);p.is_erasure_part=p.full_body=true;
            p.erasure_n=7;p.erasure_k=3;p.erasure_part=i;p.erasure_shard_size=len;
            p.erasure_payload_size=payload.size();p.erasure_cmd_count=1;
            p.erasure_origin_hash=blk->get_hash();p.body_height=1;
            DataStream ps(payload);p.payload_hash=ps.get_hash();p.body_shard=shards[i];sign(p);
            props.push_back(p);
        }
        return props;
    };
    auto feed=[&](Proposal p) { return node.accept_erasure_fragment(std::move(p),node.get_config().get_addr(p.erasure_part),false); };
    auto a=make(1),b=make(2);
    assert(feed(a[6]));assert(feed(a[6]));assert(!node.recovered(a[0].erasure_origin_hash));
    assert(feed(b[5]));assert(feed(a[5]));
    Proposal bad(a[6]);bad.body_shard[0]^=1;sign(bad);assert(!feed(bad));
    Proposal metadata(a[0]);metadata.erasure_payload_size++;sign(metadata);assert(!feed(metadata));
    Proposal old(a[0]);old.body_view=1;sign(old);assert(!feed(old));
    Proposal oversized(a[0]);oversized.erasure_shard_size=UINT32_MAX;sign(oversized);assert(!feed(oversized));
    assert(feed(a[0]));assert(node.recovered(a[0].erasure_origin_hash));
    assert(feed(a[5]));assert(feed(b[6]));assert(feed(b[0]));assert(node.recovered(b[0].erasure_origin_hash));
    auto c=make(3);Proposal corrupt(c[6]);corrupt.body_shard[0]^=1;sign(corrupt);
    assert(feed(corrupt));assert(feed(c[5]));assert(!feed(c[0]));assert(!node.recovered(c[0].erasure_origin_hash));
    assert(feed(c[6]));assert(feed(c[5]));assert(feed(c[0]));assert(node.recovered(c[0].erasure_origin_hash));
    CommandDummy bc(1,4);DataStream bs;bs << bc;
    auto bp=encode_command_batch({stream_bytes(bs)});
    Proposal control(make(4)[0]);control.full_broadcast=true;control.erasure_k=1;
    control.body_shard=bp;control.erasure_shard_size=control.erasure_payload_size=bp.size();
    DataStream ph(bp);control.payload_hash=ph.get_hash();sign(control);
    const auto addr=node.get_config().get_addr(0);
    assert(!node.accept_erasure_fragment(Proposal(control),addr,true));
    setenv("HOTSTUFF_BENCHMARK_FULL_BROADCAST","1",1);
    assert(!feed(control)); // Full broadcast bodies cannot enter through Re-propose.
    Proposal corrupted(control);corrupted.body_shard[0]^=1;sign(corrupted);
    assert(!node.accept_erasure_fragment(std::move(corrupted),addr,true));
    assert(!node.recovered(control.erasure_origin_hash));
    assert(node.accept_erasure_fragment(Proposal(control),addr,true));
    assert(node.recovered(control.erasure_origin_hash));
    unsetenv("HOTSTUFF_BENCHMARK_FULL_BROADCAST");
    std::cout << "full command assembly duplicates, reverse order, conflicts, metadata, old view, parallel origins, failed recovery and retry passed" << std::endl;
}
