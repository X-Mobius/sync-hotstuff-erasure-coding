/**
 * Copyright 2018 VMware
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

#ifndef _HOTSTUFF_CLIENT_H
#define _HOTSTUFF_CLIENT_H
#ifndef HOTSTUFF_CMD_REQSIZE
#define HOTSTUFF_CMD_REQSIZE 0
#endif

#include "salticidae/msg.h"
#include "hotstuff/command_batch.h"
#include "hotstuff/type.h"
#include "hotstuff/entity.h"
#include "hotstuff/consensus.h"

namespace hotstuff {

struct MsgReqCmd {
    static const opcode_t opcode = 0x4;
    DataStream serialized;
    command_t cmd;
    MsgReqCmd(const Command &cmd) { serialized << cmd; }
    MsgReqCmd(DataStream &&s): serialized(std::move(s)) {}
};

struct MsgRespCmd {
    static const opcode_t opcode = 0x5;
    DataStream serialized;
#if HOTSTUFF_CMD_RESPSIZE > 0
    uint8_t payload[HOTSTUFF_CMD_RESPSIZE];
#endif
    Finality fin;
    MsgRespCmd(const Finality &fin) {
        serialized << fin;
#if HOTSTUFF_CMD_RESPSIZE > 0
        serialized.put_data(payload, payload + sizeof(payload));
#endif
    }
    MsgRespCmd(DataStream &&s) {
        s >> fin;
    }
};

#ifdef SYNCHS_AUTOCLI
struct MsgDemandCmd {
    static const opcode_t opcode = 0x6;
    DataStream serialized;
    size_t ncmd;
    MsgDemandCmd(size_t ncmd) { serialized << ncmd; }
    MsgDemandCmd(DataStream &&s) { s >> ncmd; }
};
#endif

struct MsgWatchCmd {
    static const opcode_t opcode = 0x7;
    DataStream serialized;
    uint256_t hash;
    MsgWatchCmd(const uint256_t &h): hash(h) { serialized << h; }
    MsgWatchCmd(DataStream &&s) { if(s.size()!=32) throw std::runtime_error("watch length"); s >> hash; }
};

class CommandDummy: public Command {
    uint32_t cid;
    uint32_t n;
    uint256_t hash;
    bytearray_t dynamic_payload;
    static bool dynamic_format() { return std::getenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES") != nullptr; }
#if HOTSTUFF_CMD_REQSIZE > 0
    uint8_t payload[HOTSTUFF_CMD_REQSIZE];
#endif

    public:
    CommandDummy() {}
    ~CommandDummy() override {}

    CommandDummy(uint32_t cid, uint32_t n):
        cid(cid), n(n) {
#if HOTSTUFF_CMD_REQSIZE > 0
        std::fill(payload, payload + HOTSTUFF_CMD_REQSIZE, 0);
#endif
        if (dynamic_format()) {
            char *end = nullptr;
            unsigned long length = std::strtoul(std::getenv("HOTSTUFF_COMMAND_PAYLOAD_BYTES"), &end, 10);
            if (*end || length > MAX_COMMAND_BYTES - 12) throw std::runtime_error("dummy payload limit");
            dynamic_payload.resize(length);
            for (size_t i=0;i<length;++i) dynamic_payload[i]=uint8_t(cid+n+i);
        }
        hash = salticidae::get_hash(*this);
    }

    void serialize(DataStream &s) const override {
        if (dynamic_format()) {
            s << htole(cid) << htole(n) << htole(uint32_t(dynamic_payload.size())) << dynamic_payload;
            return;
        }
        s << cid << n;
#if HOTSTUFF_CMD_REQSIZE > 0
        s.put_data(payload, payload + sizeof(payload));
#endif
    }

    void unserialize(DataStream &s) override {
        if (dynamic_format()) {
            if (s.size()<12) throw std::runtime_error("truncated dummy");
            uint32_t len; s >> cid >> n >> len;
            cid=letoh(cid); n=letoh(n); len=letoh(len);
            if (len>MAX_COMMAND_BYTES-12 || len!=s.size()) throw std::runtime_error("dummy length mismatch");
            dynamic_payload.clear();
            if(len) { const auto *p=s.get_data_inplace(len); dynamic_payload.assign(p,p+len); }
            hash=salticidae::get_hash(*this);
            return;
        }
        if (s.size() != 8 + HOTSTUFF_CMD_REQSIZE) throw std::runtime_error("fixed dummy length mismatch");
        s >> cid >> n;
#if HOTSTUFF_CMD_REQSIZE > 0
        auto base = s.get_data_inplace(HOTSTUFF_CMD_REQSIZE);
        memmove(payload, base, sizeof(payload));
#endif
        hash = salticidae::get_hash(*this);
    }

    const uint256_t &get_hash() const override {
        return hash;
    }

    bool verify() const override {
        return true;
    }
};

}

#endif
