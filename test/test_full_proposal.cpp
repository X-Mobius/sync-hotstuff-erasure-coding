#include "hotstuff/consensus.h"
#include <cassert>
#include <iostream>
using namespace hotstuff;
class Fixture: public HotStuffCore {
    static privkey_bt key() { auto *p=new PrivKeySecp256k1(); p->from_rand(); return p; }
public:
    Fixture(): HotStuffCore(0,key()) {}
    part_cert_bt create_part_cert(const PrivKey &p,const uint256_t &h) override { return new PartCertSecp256k1(static_cast<const PrivKeySecp256k1 &>(p),h); }
    part_cert_bt parse_part_cert(DataStream &s) override { auto p=part_cert_bt(new PartCertSecp256k1()); s >> *p; return p; }
    quorum_cert_bt create_quorum_cert(const uint256_t &h) override { return new QuorumCertSecp256k1(get_config(),h); }
    quorum_cert_bt parse_quorum_cert(DataStream &s) override { auto q=quorum_cert_bt(new QuorumCertSecp256k1()); s >> *q; return q; }
    void do_decide(Finality &&) override {}
    void do_consensus(const block_t &) override {}
    void do_broadcast_proposal(const Proposal &) override {}
    void do_send_erasure_proposals(const std::vector<Proposal> &) override {}
    void do_broadcast_vote(const Vote &) override {}
    void do_broadcast_blame(const Blame &) override {}
    void do_broadcast_blamenotify(const BlameNotify &) override {}
    void do_notify(const Notify &) override {}
    void set_commit_timer(const block_t &,double) override {}
    void set_blame_timer(double) override {}
    void stop_commit_timer(uint32_t) override {}
    void stop_commit_timer_all() override {}
    void stop_blame_timer() override {}
    void set_viewtrans_timer(double) override {}
    void stop_viewtrans_timer() override {}
};
int main() {
    setenv("HOTSTUFF_ERASURE_FULL_COMMANDS","1",1);
    setenv("HOTSTUFF_REQUIRE_COMMAND_RECONSTRUCTION","1",1);
    Fixture f;
    PrivKeySecp256k1 sk;sk.from_rand();auto pk=sk.get_pubkey();
    auto blk=block_t(new Block({f.get_genesis()},{},nullptr,{},1,nullptr,nullptr));
    Proposal p(0,blk,&f);p.is_erasure_part=true;p.full_body=true;
    p.erasure_n=7;p.erasure_k=3;p.erasure_part=2;p.erasure_shard_size=32;
    p.erasure_payload_size=8;p.erasure_cmd_count=0;p.erasure_origin_hash=blk->get_hash();
    p.body_view=0;p.body_height=1;p.body_shard=bytearray_t(32,19);
    DataStream bs(p.body_shard);p.payload_hash=bs.get_hash();
    p.leader_cert=f.create_part_cert(sk,p.body_commitment());
    DataStream s;s << p;auto original=stream_bytes(s);
    auto accepted=[&](const bytearray_t &bytes) {
        try {
            DataStream in(bytes);Proposal parsed;parsed.hsc=&f;in >> parsed;
            if(!parsed.full_body || in.size() || !parsed.leader_cert) return false;
            DataStream canonical;canonical << parsed;
            return stream_bytes(canonical)==bytes && parsed.leader_cert->get_obj_hash()==parsed.body_commitment() && parsed.leader_cert->verify(*pk);
        } catch(const std::exception &) { return false; }
    };
    assert(accepted(original));
    for(size_t len=0;len<original.size();++len) assert(!accepted(bytearray_t(original.begin(),original.begin()+len)));
    for(size_t pos=0;pos<original.size();++pos) {
        auto bad=original;bad[pos]^=1;assert(!accepted(bad));
    }
    auto bad=original;bad.push_back(0);assert(!accepted(bad));
    assert(!f.storage->is_blk_fetched(blk->get_hash()));
    p.full_broadcast=true;p.erasure_k=1;p.erasure_payload_size=32;
    p.leader_cert=f.create_part_cert(sk,p.body_commitment());
    DataStream control;control << p;auto control_bytes=stream_bytes(control);
    assert(!accepted(control_bytes));
    setenv("HOTSTUFF_BENCHMARK_FULL_BROADCAST","1",1);
    assert(accepted(control_bytes));assert(!accepted(original));
    for(size_t pos=0;pos<control_bytes.size();++pos) {
        auto changed=control_bytes;changed[pos]^=1;assert(!accepted(changed));
    }
    unsetenv("HOTSTUFF_BENCHMARK_FULL_BROADCAST");
    std::cout << "full proposal signature, truncation, byte tampering and storage isolation passed; bytes=" << original.size() << std::endl;
}
