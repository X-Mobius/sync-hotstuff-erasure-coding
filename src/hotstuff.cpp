/**
 * Copyright 2018 VMware
 * Copyright 2018 Ted Yin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "hotstuff/hotstuff.h"
#include "hotstuff/client.h"
#include "hotstuff/liveness.h"
#include <cstdlib>
#include <cstring>
#include <random>
#include "code_function.h"

using salticidae::static_pointer_cast;

#define LOG_INFO HOTSTUFF_LOG_INFO
#define LOG_DEBUG HOTSTUFF_LOG_DEBUG
#define LOG_WARN HOTSTUFF_LOG_WARN

namespace hotstuff {

namespace {

int get_drop_propose_pct() {
    static int pct = []() {
        const char *env = std::getenv("HOTSTUFF_DROP_PROPOSE_PCT");
        if (!env) return 0;
        int value = std::atoi(env);
        if (value < 0) return 0;
        if (value > 100) return 100;
        return value;
    }();
    return pct;
}

bool should_drop_proposal() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 100);
    int pct = get_drop_propose_pct();
    return pct > 0 && dist(rng) <= pct;
}

bool require_erasure_reconstruction() {
    const char *env = std::getenv("HOTSTUFF_REQUIRE_ERASURE_RECONSTRUCTION");
    return env && *env && std::strcmp(env, "0") != 0;
}

uint32_t get_nfaulty(size_t nreplicas, uint32_t fallback) {
    const char *env = std::getenv("HOTSTUFF_NFAULTY");
    if (!env || !*env) return fallback;
    char *end = nullptr;
    unsigned long value = std::strtoul(env, &end, 10);
    if (*end != '\0' || value >= nreplicas)
        throw HotStuffError("invalid HOTSTUFF_NFAULTY=%s for %lu replicas",
                env, nreplicas);
    return static_cast<uint32_t>(value);
}

} // namespace

const opcode_t MsgPropose::opcode;
MsgPropose::MsgPropose(const Proposal &proposal) { serialized << proposal; }
void MsgPropose::postponed_parse(HotStuffCore *hsc) {
    proposal.hsc = hsc;
    serialized >> proposal;
}

const opcode_t MsgRepropose::opcode;
MsgRepropose::MsgRepropose(const Proposal &proposal) { serialized << proposal; }
void MsgRepropose::postponed_parse(HotStuffCore *hsc) {
    proposal.hsc = hsc;
    serialized >> proposal;
}

const opcode_t MsgVote::opcode;
MsgVote::MsgVote(const Vote &vote) { serialized << vote; }
void MsgVote::postponed_parse(HotStuffCore *hsc) {
    vote.hsc = hsc;
    serialized >> vote;
}

const opcode_t MsgNotify::opcode;
MsgNotify::MsgNotify(const Notify &notify) { serialized << notify; }
void MsgNotify::postponed_parse(HotStuffCore *hsc) {
    notify.hsc = hsc;
    serialized >> notify;
}

const opcode_t MsgBlame::opcode;
MsgBlame::MsgBlame(const Blame &blame) { serialized << blame; }
void MsgBlame::postponed_parse(HotStuffCore *hsc) {
    blame.hsc = hsc;
    serialized >> blame;
}

const opcode_t MsgBlameNotify::opcode;
MsgBlameNotify::MsgBlameNotify(const BlameNotify &bn) { serialized << bn; }
void MsgBlameNotify::postponed_parse(HotStuffCore *hsc) {
    bn.hsc = hsc;
    serialized >> bn;
}

const opcode_t MsgReqBlock::opcode;
MsgReqBlock::MsgReqBlock(const std::vector<uint256_t> &blk_hashes) {
    serialized << htole((uint32_t)blk_hashes.size());
    for (const auto &h: blk_hashes)
        serialized << h;
}

MsgReqBlock::MsgReqBlock(DataStream &&s) {
    uint32_t size;
    s >> size;
    size = letoh(size);
    blk_hashes.resize(size);
    for (auto &h: blk_hashes) s >> h;
}

const opcode_t MsgRespBlock::opcode;
MsgRespBlock::MsgRespBlock(const std::vector<block_t> &blks) {
    serialized << htole((uint32_t)blks.size());
    for (auto blk: blks) serialized << *blk;
}

void MsgRespBlock::postponed_parse(HotStuffCore *hsc) {
    uint32_t size;
    serialized >> size;
    size = letoh(size);
    blks.resize(size);
    for (auto &blk: blks)
    {
        Block _blk;
        _blk.unserialize(serialized, hsc);
        blk = hsc->storage->add_blk(std::move(_blk), hsc->get_config());
    }
}

// TODO: improve this function
void HotStuffBase::exec_command(uint256_t cmd_hash, commit_cb_t callback) {
    cmd_pending.enqueue(std::make_pair(cmd_hash, callback));
}

void HotStuffBase::on_fetch_blk(const block_t &blk) {
#ifdef HOTSTUFF_BLK_PROFILE
    blk_profiler.get_tx(blk->get_hash());
#endif
    LOG_DEBUG("fetched %.10s", get_hex(blk->get_hash()).c_str());
    part_fetched++;
    fetched++;
    //for (auto cmd: blk->get_cmds()) on_fetch_cmd(cmd);
    const uint256_t &blk_hash = blk->get_hash();
    auto it = blk_fetch_waiting.find(blk_hash);
    if (it != blk_fetch_waiting.end())
    {
        it->second.resolve(blk);
        blk_fetch_waiting.erase(it);
    }
}

void HotStuffBase::on_deliver_blk(const block_t &blk) {
    const uint256_t &blk_hash = blk->get_hash();
    bool valid;
    /* sanity check: all parents must be delivered */
    for (const auto &p: blk->get_parent_hashes())
        assert(storage->is_blk_delivered(p));
    if ((valid = HotStuffCore::on_deliver_blk(blk)))
    {
        LOG_DEBUG("block %.10s delivered",
                get_hex(blk_hash).c_str());
        part_parent_size += blk->get_parent_hashes().size();
        part_delivered++;
        delivered++;
    }
    else
    {
        LOG_WARN("dropping invalid block");
    }

    auto it = blk_delivery_waiting.find(blk_hash);
    if (it != blk_delivery_waiting.end())
    {
        auto &pm = it->second;
        if (valid)
        {
            pm.elapsed.stop(false);
            auto sec = pm.elapsed.elapsed_sec;
            part_delivery_time += sec;
            part_delivery_time_min = std::min(part_delivery_time_min, sec);
            part_delivery_time_max = std::max(part_delivery_time_max, sec);

            pm.resolve(blk);
        }
        else
        {
            pm.reject(blk);
            // TODO: do we need to also free it from storage?
        }
        blk_delivery_waiting.erase(it);
    }
}

promise_t HotStuffBase::async_fetch_blk(const uint256_t &blk_hash,
                                        const NetAddr *replica_id,
                                        bool fetch_now) {
    if (storage->is_blk_fetched(blk_hash))
        return promise_t([this, &blk_hash](promise_t pm){
            pm.resolve(storage->find_blk(blk_hash));
        });
    auto it = blk_fetch_waiting.find(blk_hash);
    if (it == blk_fetch_waiting.end())
    {
#ifdef HOTSTUFF_BLK_PROFILE
        blk_profiler.rec_tx(blk_hash, false);
#endif
        it = blk_fetch_waiting.insert(
            std::make_pair(
                blk_hash,
                BlockFetchContext(blk_hash, this))).first;
    }
    if (replica_id != nullptr)
        it->second.add_replica(*replica_id, fetch_now);
    return static_cast<promise_t &>(it->second);
}

promise_t HotStuffBase::async_deliver_blk(const uint256_t &blk_hash,
                                        const NetAddr &replica_id) {
    if (storage->is_blk_delivered(blk_hash))
        return promise_t([this, &blk_hash](promise_t pm) {
            pm.resolve(storage->find_blk(blk_hash));
        });
    auto it = blk_delivery_waiting.find(blk_hash);
    if (it != blk_delivery_waiting.end())
        return static_cast<promise_t &>(it->second);
    BlockDeliveryContext pm{[](promise_t){}};
    it = blk_delivery_waiting.insert(std::make_pair(blk_hash, pm)).first;
    /* otherwise the on_deliver_batch will resolve */
    async_fetch_blk(blk_hash, &replica_id).then([this, replica_id](block_t blk) {
        /* qc_ref should be fetched */
        std::vector<promise_t> pms;
        const auto &qc = blk->get_qc();
        if (qc)
            pms.push_back(async_fetch_blk(blk->get_qc_ref_hash(), &replica_id));
        /* the parents should be delivered */
        for (const auto &phash: blk->get_parent_hashes())
            pms.push_back(async_deliver_blk(phash, replica_id));
        if (blk != get_genesis())
            pms.push_back(blk->verify(get_config(), vpool));
        promise::all(pms).then([this, blk]() {
            on_deliver_blk(blk);
        });
    });
    return static_cast<promise_t &>(pm);
}

bool HotStuffBase::accept_erasure_fragment(Proposal &&prop, const NetAddr &peer,
                                            bool directed) {
    if (!prop.is_erasure_part || !prop.blk) return false;
    const size_t expected_n = get_config().nreplicas;
    const size_t expected_k = expected_n - get_config().nmajority + 1;
    if (prop.erasure_n != expected_n || prop.erasure_k != expected_k ||
            prop.erasure_part >= prop.erasure_n || prop.erasure_k == 0 ||
            prop.erasure_shard_size == 0 ||
            prop.erasure_shard_size % sizeof(uint256_t) != 0 ||
            prop.erasure_payload_size !=
                prop.erasure_cmd_count * sizeof(uint256_t)) {
        LOG_WARN("dropping erasure fragment with invalid metadata");
        return false;
    }
    if (directed) {
        if (prop.erasure_part != get_id() ||
                peer != get_config().get_addr(prop.proposer)) {
            LOG_WARN("dropping misdirected erasure proposal");
            return false;
        }
    } else if (peer != get_config().get_addr(prop.erasure_part)) {
        LOG_WARN("dropping erasure fragment forwarded by wrong replica");
        return false;
    }

    const auto &fragment_cmds = prop.blk->get_cmds();
    if (fragment_cmds.size() * sizeof(uint256_t) != prop.erasure_shard_size)
        return false;
    const uint256_t origin_hash = prop.erasure_origin_hash;
    if (completed_erasure.count(origin_hash)) return true;
    auto found = erasure_assemblies.find(origin_hash);
    if (found == erasure_assemblies.end()) {
        ErasureAssembly assembly;
        assembly.proposer = prop.proposer;
        assembly.n = prop.erasure_n;
        assembly.k = prop.erasure_k;
        assembly.shard_size = prop.erasure_shard_size;
        assembly.payload_size = prop.erasure_payload_size;
        assembly.cmd_count = prop.erasure_cmd_count;
        assembly.template_blk = prop.blk;
        assembly.shards.resize(assembly.n);
        assembly.present.assign(assembly.n, 0);
        found = erasure_assemblies.emplace(origin_hash, std::move(assembly)).first;
    }
    ErasureAssembly &assembly = found->second;
    if (assembly.proposer != prop.proposer || assembly.n != prop.erasure_n ||
            assembly.k != prop.erasure_k ||
            assembly.shard_size != prop.erasure_shard_size ||
            assembly.payload_size != prop.erasure_payload_size ||
            assembly.cmd_count != prop.erasure_cmd_count) {
        LOG_WARN("dropping inconsistent erasure fragment");
        return false;
    }
    const size_t part = prop.erasure_part;
    if (!assembly.present[part]) {
        assembly.shards[part].resize(assembly.shard_size);
        for (size_t i = 0; i < fragment_cmds.size(); ++i) {
            bytearray_t raw = fragment_cmds[i];
            memcpy(assembly.shards[part].data() + i * sizeof(uint256_t),
                    raw.data(), sizeof(uint256_t));
        }
        assembly.present[part] = 1;
        ++assembly.received;
        HOTSTUFF_LOG_INFO("erasure_fragment_received: origin=%s part=%lu count=%lu k=%u",
                get_hex10(origin_hash).c_str(), part, assembly.received, assembly.k);
    }
    if (assembly.received < assembly.k) return true;

    std::vector<unsigned char *> shard_ptrs(assembly.n, nullptr);
    for (size_t i = 0; i < assembly.n; ++i) {
        void *buf = nullptr;
        if (posix_memalign(&buf, 64, assembly.shard_size)) {
            for (auto ptr: shard_ptrs) free(ptr);
            return false;
        }
        shard_ptrs[i] = static_cast<unsigned char *>(buf);
        memset(shard_ptrs[i], 0, assembly.shard_size);
        if (assembly.present[i])
            memcpy(shard_ptrs[i], assembly.shards[i].data(), assembly.shard_size);
    }
    if (rs_reconstruct_data(assembly.n, assembly.k, assembly.shard_size,
                shard_ptrs.data(), assembly.present.data()) != 0) {
        for (auto ptr: shard_ptrs) free(ptr);
        LOG_WARN("failed to reconstruct erasure proposal");
        return false;
    }
    std::vector<unsigned char> payload(assembly.payload_size);
    for (size_t i = 0; i < assembly.k; ++i) {
        const size_t offset = i * assembly.shard_size;
        const size_t count = offset < assembly.payload_size ?
                std::min<size_t>(assembly.shard_size,
                    assembly.payload_size - offset) : 0;
        if (count) memcpy(payload.data() + offset, shard_ptrs[i], count);
    }
    for (auto ptr: shard_ptrs) free(ptr);

    std::vector<uint256_t> final_cmds;
    final_cmds.reserve(assembly.cmd_count);
    for (size_t offset = 0; offset < payload.size(); offset += sizeof(uint256_t))
        final_cmds.emplace_back(payload.data() + offset);
    std::vector<block_t> parents;
    for (const auto &parent_hash: assembly.template_blk->get_parent_hashes()) {
        block_t parent = storage->find_blk(parent_hash);
        if (!parent) return false;
        parents.push_back(parent);
    }
    block_t qc_ref = storage->find_blk(assembly.template_blk->get_qc_ref_hash());
    if (assembly.template_blk->get_qc() && !qc_ref) return false;
    block_t origin_blk = storage->add_blk(new Block(parents, final_cmds,
                assembly.template_blk->get_qc() ?
                    assembly.template_blk->get_qc()->clone() : nullptr,
                bytearray_t(assembly.template_blk->get_extra()),
                assembly.template_blk->get_height(), qc_ref, nullptr));
    if (origin_blk->get_hash() != origin_hash) {
        block_t known = storage->find_blk(origin_hash);
        LOG_WARN("dropping erasure proposal with mismatched original hash expected=%s actual=%s cmd0=%s known=%d known_cmd0=%s parents=%lu extra=%lu",
                get_hex10(origin_hash).c_str(),
                get_hex10(origin_blk->get_hash()).c_str(),
                final_cmds.empty() ? "none" : get_hex10(final_cmds[0]).c_str(),
                known ? 1 : 0,
                (!known || known->get_cmds().empty()) ? "none" :
                    get_hex10(known->get_cmds()[0]).c_str(),
                parents.size(), assembly.template_blk->get_extra().size());
        erasure_assemblies.erase(origin_hash);
        return false;
    }
    Proposal complete(assembly.proposer, origin_blk, nullptr);
    erasure_assemblies.erase(origin_hash);
    completed_erasure.insert(origin_hash);
    HOTSTUFF_LOG_INFO("erasure_reconstructed: origin=%s shards=%u",
            get_hex10(origin_hash).c_str(), prop.erasure_k);
    promise::all(std::vector<promise_t>{async_deliver_blk(origin_hash, peer)})
        .then([this, complete = std::move(complete)]() {
            on_receive_proposal(complete);
        });
    return true;
}

size_t HotStuffBase::erasure_send_copies() const {
    const char *env = std::getenv("HOTSTUFF_ERASURE_SEND_COPIES");
    if (!env || !*env) return 1;
    char *end = nullptr;
    unsigned long value = std::strtoul(env, &end, 10);
    if (*end != '\0' || value == 0 || value > 10)
        throw HotStuffError("invalid HOTSTUFF_ERASURE_SEND_COPIES=%s", env);
    return static_cast<size_t>(value);
}

void HotStuffBase::propose_handler(MsgPropose &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    auto &prop = msg.proposal;
    block_t blk = prop.blk;
    if (!blk) return;
    if (should_drop_proposal()) {
        LOG_WARN("dropping proposal due to HOTSTUFF_DROP_PROPOSE_PCT=%d", get_drop_propose_pct());
        return;
    }

    if (!prop.is_erasure_part)
    {
        promise::all(std::vector<promise_t>{
            async_deliver_blk(blk->get_hash(), peer)
        }).then([this, prop = std::move(prop)]() {
            on_receive_proposal(prop);
        });
        return;
    }

    const uint256_t origin_hash = prop.erasure_origin_hash;
    if (!accept_erasure_fragment(Proposal(prop), peer, true)) return;
    if (forwarded_erasure.insert(origin_hash).second)
        broadcast_reproposal(prop);
}

void HotStuffBase::repropose_handler(MsgRepropose &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    if (should_drop_proposal()) {
        LOG_WARN("dropping re-proposal due to HOTSTUFF_DROP_PROPOSE_PCT=%d",
                get_drop_propose_pct());
        return;
    }
    accept_erasure_fragment(std::move(msg.proposal), peer, false);
}

void HotStuffBase::vote_handler(MsgVote &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    //auto &vote = msg.vote;
    RcObj<Vote> v(new Vote(std::move(msg.vote)));
    if (require_erasure_reconstruction() &&
            !storage->is_blk_fetched(v->blk_hash)) {
        LOG_WARN("dropping vote for block not reconstructed from erasure shards");
        return;
    }
    promise::all(std::vector<promise_t>{
        async_deliver_blk(v->blk_hash, peer),
        v->verify(vpool),
    }).then([this, v=std::move(v)](const promise::values_t values) {
        if (!promise::any_cast<bool>(values[1]))
            LOG_WARN("invalid vote from %d", v->voter);
        else
            on_receive_vote(*v);
    });
}

void HotStuffBase::notify_handler(MsgNotify &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    RcObj<Notify> n(new Notify(std::move(msg.notify)));
    promise::all(std::vector<promise_t>{
        async_deliver_blk(n->blk_hash, peer),
        n->verify(vpool)
    }).then([this, n, peer](const promise::values_t values) {
        if (!promise::any_cast<bool>(values[1]))
            LOG_WARN("invalid notify message from %s", std::string(peer).c_str());
        else
            on_receive_notify(*n);
    });
}

void HotStuffBase::blame_handler(MsgBlame &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    RcObj<Blame> b(new Blame(std::move(msg.blame)));
    b->verify(vpool).then([this, b, peer](bool result) {
        if (!result)
            LOG_WARN("invalid blame message from %s", std::string(peer).c_str());
        else
            on_receive_blame(*b);
    });
}

void HotStuffBase::blamenotify_handler(MsgBlameNotify &&msg, const Net::conn_t &conn) {
    const NetAddr &peer = conn->get_peer_addr();
    if (peer.is_null()) return;
    msg.postponed_parse(this);
    RcObj<BlameNotify> bn(new BlameNotify(std::move(msg.bn)));
    promise::all(std::vector<promise_t>{
        async_deliver_blk(bn->hqc_hash, peer),
        bn->verify(vpool)
    }).then([this, bn, peer](promise::values_t values) {
        auto result = promise::any_cast<bool>(values[1]);
        if (!result)
            LOG_WARN("invalid blamenotify message from %s", std::string(peer).c_str());
        else
            on_receive_blamenotify(*bn);
    });
}

void HotStuffBase::set_commit_timer(const block_t &blk, double t_sec) {
#ifdef SYNCHS_NOTIMER
    on_commit_timeout(blk);
#else
    auto height = blk->get_height();
    auto &timer = commit_timers[height] =
        TimerEvent(ec, [this, blk=std::move(blk), height](TimerEvent &) {
            on_commit_timeout(blk);
            stop_commit_timer(height);
        });
    timer.add(t_sec);
#endif
}

void HotStuffBase::stop_commit_timer(uint32_t height) {
    commit_timers.erase(height);
}

void HotStuffBase::stop_commit_timer_all() {
    commit_timers.clear();
}

void HotStuffBase::set_blame_timer(double t_sec) {
    blame_timer = TimerEvent(ec, [this](TimerEvent &) {
        on_blame_timeout();
        stop_blame_timer();
    });
    blame_timer.add(t_sec);
}

void HotStuffBase::stop_blame_timer() {
    blame_timer.clear();
}

void HotStuffBase::set_viewtrans_timer(double t_sec) {
    viewtrans_timer = TimerEvent(ec, [this](TimerEvent &) {
        on_viewtrans_timeout();
        stop_viewtrans_timer();
    });
    viewtrans_timer.add(t_sec);
}

void HotStuffBase::stop_viewtrans_timer() {
    viewtrans_timer.clear();
}

void HotStuffBase::req_blk_handler(MsgReqBlock &&msg, const Net::conn_t &conn) {
    const NetAddr replica = conn->get_peer_addr();
    if (replica.is_null()) return;
    auto &blk_hashes = msg.blk_hashes;
    std::vector<promise_t> pms;
    for (const auto &h: blk_hashes)
        pms.push_back(async_fetch_blk(h, nullptr));
    promise::all(pms).then([replica, this](const promise::values_t values) {
        std::vector<block_t> blks;
        for (auto &v: values)
        {
            auto blk = promise::any_cast<block_t>(v);
            blks.push_back(blk);
        }
        pn.send_msg(MsgRespBlock(blks), replica);
    });
}

void HotStuffBase::resp_blk_handler(MsgRespBlock &&msg, const Net::conn_t &) {
    msg.postponed_parse(this);
    for (const auto &blk: msg.blks)
        if (blk) on_fetch_blk(blk);
}

bool HotStuffBase::conn_handler(const salticidae::ConnPool::conn_t &conn, bool connected) {
    if (connected)
    {
        auto cert = conn->get_peer_cert();
        //SALTICIDAE_LOG_INFO("%s", salticidae::get_hash(cert->get_der()).to_hex().c_str());
        return (!cert) || valid_tls_certs.count(salticidae::get_hash(cert->get_der()));
    }
    return true;
}

void HotStuffBase::print_stat() const {
    LOG_INFO("===== begin stats =====");
    LOG_INFO("-------- queues -------");
    LOG_INFO("blk_fetch_waiting: %lu", blk_fetch_waiting.size());
    LOG_INFO("blk_delivery_waiting: %lu", blk_delivery_waiting.size());
    LOG_INFO("decision_waiting: %lu", decision_waiting.size());
    LOG_INFO("commit_timers: %lu", commit_timers.size());
    LOG_INFO("-------- misc ---------");
    LOG_INFO("fetched: %lu", fetched);
    LOG_INFO("delivered: %lu", delivered);
#ifdef SYNCHS_LATBREAKDOWN
    LOG_INFO("lat_propose: %.3f ms",
            part_decided ? part_lat_proposed / part_decided * 1e3 : 0);
    LOG_INFO("lat_commit: +%.3f ms",
            part_decided ? part_lat_committed / part_decided * 1e3 : 0);
#endif
    LOG_INFO("cmd_cache: %lu", storage->get_cmd_cache_size());
    LOG_INFO("blk_cache: %lu", storage->get_blk_cache_size());
    LOG_INFO("------ misc (10s) -----");
    LOG_INFO("fetched: %lu", part_fetched);
    LOG_INFO("delivered: %lu", part_delivered);
    LOG_INFO("decided: %lu", part_decided);
    LOG_INFO("gened: %lu", part_gened);
    LOG_INFO("avg. parent_size: %.3f",
            part_delivered ? part_parent_size / double(part_delivered) : 0);
    LOG_INFO("delivery time: %.3f avg, %.3f min, %.3f max",
            part_delivered ? part_delivery_time / double(part_delivered) : 0,
            part_delivery_time_min == double_inf ? 0 : part_delivery_time_min,
            part_delivery_time_max);

    part_parent_size = 0;
    part_fetched = 0;
    part_delivered = 0;
    part_decided = 0;
#ifdef SYNCHS_LATBREAKDOWN
    part_lat_proposed = 0;
    part_lat_committed = 0;
#endif
    part_gened = 0;
    part_delivery_time = 0;
    part_delivery_time_min = double_inf;
    part_delivery_time_max = 0;
#ifdef HOTSTUFF_MSG_STAT
    LOG_INFO("--- replica msg. (10s) ---");
    size_t _nsent = 0;
    size_t _nrecv = 0;
    for (const auto &replica: peers)
    {
        auto conn = pn.get_peer_conn(replica);
        if (conn == nullptr) continue;
        size_t ns = conn->get_nsent();
        size_t nr = conn->get_nrecv();
        size_t nsb = conn->get_nsentb();
        size_t nrb = conn->get_nrecvb();
        conn->clear_msgstat();
        LOG_INFO("%s: %u(%u), %u(%u), %u",
            std::string(replica).c_str(), ns, nsb, nr, nrb, part_fetched_replica[replica]);
        _nsent += ns;
        _nrecv += nr;
        part_fetched_replica[replica] = 0;
    }
    nsent += _nsent;
    nrecv += _nrecv;
    LOG_INFO("sent: %lu", _nsent);
    LOG_INFO("recv: %lu", _nrecv);
    LOG_INFO("--- replica msg. total ---");
    LOG_INFO("sent: %lu", nsent);
    LOG_INFO("recv: %lu", nrecv);
#endif
    LOG_INFO("====== end stats ======");
}

HotStuffBase::HotStuffBase(uint32_t blk_size,
                    ReplicaID rid,
                    privkey_bt &&priv_key,
                    NetAddr listen_addr,
                    pacemaker_bt pmaker,
                    EventContext ec,
                    size_t nworker,
                    const Net::Config &netconfig):
        HotStuffCore(rid, std::move(priv_key)),
        listen_addr(listen_addr),
        blk_size(blk_size),
        ec(ec),
        tcall(ec),
        vpool(ec, nworker),
        pn(ec, netconfig),
        pmaker(std::move(pmaker)),

        fetched(0), delivered(0),
        nsent(0), nrecv(0),
        part_parent_size(0),
        part_fetched(0),
        part_delivered(0),
        part_decided(0),
        part_gened(0),
        part_delivery_time(0),
        part_delivery_time_min(double_inf),
        part_delivery_time_max(0)
#ifdef SYNCHS_LATBREAKDOWN
    ,   part_lat_proposed(0),
        part_lat_committed(0)
#endif

{
    /* register the handlers for msg from replicas */
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::propose_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::repropose_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::vote_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::notify_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::blame_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::blamenotify_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::req_blk_handler, this, _1, _2));
    pn.reg_handler(salticidae::generic_bind(&HotStuffBase::resp_blk_handler, this, _1, _2));
    pn.reg_conn_handler(salticidae::generic_bind(&HotStuffBase::conn_handler, this, _1, _2));
    pn.start();
    pn.listen(listen_addr);
}
void HotStuffBase::do_consensus(const block_t &blk) {
    pmaker->on_consensus(blk);
}

void HotStuffBase::do_decide(Finality &&fin) {
    part_decided++;
    state_machine_execute(fin);
    auto it = decision_waiting.find(fin.cmd_hash);
    if (it != decision_waiting.end())
    {
        it->second(std::move(fin));
        decision_waiting.erase(it);
    }
}

void HotStuffBase::do_notify(const Notify &notify) {
    MsgNotify m(notify);
    ReplicaID next_proposer = pmaker->get_proposer();
    if (next_proposer != get_id())
        pn.send_msg(m, get_config().get_addr(next_proposer));
    else
        on_receive_notify(notify);
}

HotStuffBase::~HotStuffBase() {}

void HotStuffBase::start(
        std::vector<std::tuple<NetAddr, pubkey_bt, uint256_t>> &&replicas,
        double delta, bool ec_loop) {
    for (size_t i = 0; i < replicas.size(); i++)
    {
        auto &addr = std::get<0>(replicas[i]);
        HotStuffCore::add_replica(i, addr, std::move(std::get<1>(replicas[i])));
        valid_tls_certs.insert(std::move(std::get<2>(replicas[i])));
        if (addr != listen_addr)
        {
            peers.push_back(addr);
            pn.add_peer(addr);
        }
    }

    /* ((n - 1) + 1 - 1) / 2 */
    const size_t nreplicas = get_config().nreplicas;
    uint32_t nfaulty = get_nfaulty(nreplicas, peers.size() / 2);
    if (nfaulty == 0)
        LOG_WARN("too few replicas in the system to tolerate any failure");
    LOG_INFO("benchmark_config replicas=%lu nfaulty=%u quorum=%lu",
            nreplicas, nfaulty, nreplicas - nfaulty);
    on_init(nfaulty, delta);
    pmaker->init(this);
    if (ec_loop)
        ec.dispatch();

    cmd_pending.reg_handler(ec, [this](cmd_queue_t &q) {
        std::pair<uint256_t, commit_cb_t> e;
        while (q.try_dequeue(e))
        {
            ReplicaID proposer = pmaker->get_proposer();

            const auto &cmd_hash = e.first;
            auto it = decision_waiting.find(cmd_hash);
            if (it == decision_waiting.end())
            {
                it = decision_waiting.insert(std::make_pair(cmd_hash, e.second)).first;
#ifdef SYNCHS_LATBREAKDOWN
                cmd_lats[cmd_hash].on_init();
#endif
            }
            else
                e.second(Finality(id, 0, 0, 0, cmd_hash, uint256_t()));
            if (proposer != get_id()) continue;
            cmd_pending_buffer.push(cmd_hash);
            if (cmd_pending_buffer.size() >= blk_size)
            {
                std::vector<uint256_t> cmds;
                for (uint32_t i = 0; i < blk_size; i++)
                {
                    cmds.push_back(cmd_pending_buffer.front());
                    cmd_pending_buffer.pop();
                }
                pmaker->beat().then([this, cmds = std::move(cmds)](ReplicaID proposer) {
                    if (proposer == get_id())
                    {
                        on_propose(cmds, pmaker->get_parents());
#ifdef SYNCHS_LATBREAKDOWN
                        for (auto &ch: cmds)
                            cmd_lats[ch].on_propose();
#endif
#ifdef SYNCHS_AUTOCLI
                        for (size_t i = pmaker->get_pending_size(); i < 1; i++)
                            do_demand_commands(blk_size);
#endif
                    }
                });
                return true;
            }
#ifdef SYNCHS_LATBREAKDOWN
            auto orig_cb = std::move(it.second);
            it.second = [this](Finality &fin) {
                auto cl = cmd_lats.find(fin.cmd_hash);
                cl->second.on_commit();
                part_lat_proposed += cl->second.proposed;
                part_lat_committed += cl->second.committed;
                cmd_lats.erase(cl);
                orig_cb(fin);
            };
#endif
        }
        return false;
    });
}

}
