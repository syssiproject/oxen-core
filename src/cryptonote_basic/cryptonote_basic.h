// Copyright (c) 2014-2019, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#pragma once

#include <fmt/format.h>

#include <atomic>
#include <variant>
#include <vector>

#include "common/exception.h"
#include "common/format.h"
#include "crypto/crypto.h"
#include "crypto/hash.h"
#include "cryptonote_config.h"
#include "device/device.hpp"
#include "epee/serialization/keyvalue_serialization.h"  // eepe named serialization
#include "logging/oxen_logger.h"
#include "ringct/rctTypes.h"
#include "serialization/binary_archive.h"
#include "serialization/crypto.h"
#include "serialization/variant.h"
#include "serialization/vector.h"
#include "serialization/vector_bool.h"
#include "txtypes.h"

namespace service_nodes {
struct quorum_signature {
    uint16_t voter_index;
    char padding[6] = {0};
    crypto::signature signature;

    quorum_signature() = default;
    quorum_signature(uint16_t voter_index, crypto::signature const& signature) :
            voter_index(voter_index), signature(signature) {}

    BEGIN_SERIALIZE_OBJECT()
    FIELD(voter_index)
    FIELD(signature)
    END_SERIALIZE()
};
};  // namespace service_nodes

namespace cryptonote {
/* outputs */
struct txout_to_script {
    std::vector<crypto::public_key> keys;
    std::vector<uint8_t> script;

    BEGIN_SERIALIZE_OBJECT()
    FIELD(keys)
    FIELD(script)
    END_SERIALIZE()
};

struct txout_to_scripthash {
    crypto::hash hash;
};

struct txout_to_key {
    txout_to_key() = default;
    txout_to_key(const crypto::public_key& _key) : key(_key) {}
    crypto::public_key key;
};

/* inputs */

struct txin_gen {
    size_t height;

    BEGIN_SERIALIZE_OBJECT()
    VARINT_FIELD(height)
    END_SERIALIZE()
};

struct txin_to_script {
    crypto::hash prev;
    size_t prevout;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
    FIELD(prev)
    VARINT_FIELD(prevout)
    FIELD(sigset)
    END_SERIALIZE()
};

struct txin_to_scripthash {
    crypto::hash prev;
    size_t prevout;
    txout_to_script script;
    std::vector<uint8_t> sigset;

    BEGIN_SERIALIZE_OBJECT()
    FIELD(prev)
    VARINT_FIELD(prevout)
    FIELD(script)
    FIELD(sigset)
    END_SERIALIZE()
};

struct txin_to_key {
    uint64_t amount;
    std::vector<uint64_t> key_offsets;
    crypto::key_image k_image;  // double spending protection

    BEGIN_SERIALIZE_OBJECT()
    VARINT_FIELD(amount)
    FIELD(key_offsets)
    FIELD(k_image)
    END_SERIALIZE()
};

using txin_v = std::variant<txin_gen, txin_to_script, txin_to_scripthash, txin_to_key>;

using txout_target_v = std::variant<txout_to_script, txout_to_scripthash, txout_to_key>;

// typedef std::pair<uint64_t, txout> out_t;
struct tx_out {
    uint64_t amount;
    txout_target_v target;

    BEGIN_SERIALIZE_OBJECT()
    VARINT_FIELD(amount)
    FIELD(target)
    END_SERIALIZE()
};

// Blink quorum statuses.  Note that the underlying numeric values is used in the RPC.  `none` is
// only used in places like the RPC where we return a value even if not a blink at all.
enum class blink_result { none = 0, rejected, accepted, timeout };

class transaction_prefix {

  public:
    static constexpr txversion get_min_version_for_hf(hf hf_version);
    static txversion get_max_version_for_hf(hf hf_version);
    static constexpr txtype get_max_type_for_hf(hf hf_version);

    // tx information
    txversion version;
    txtype type;

    bool is_transfer() const {
        return type == txtype::standard || type == txtype::stake ||
               type == txtype::oxen_name_system;
    }

    // True if this is a miner tx: that it, has a single input of type txin_gen.
    bool is_miner_tx() const { return vin.size() == 1 && std::holds_alternative<txin_gen>(vin[0]); }

    // not used after version 2, but remains for compatibility
    uint64_t unlock_time;  // number of block (or time), used as a limitation like: spend this tx
                           // not early then block/time
    std::vector<txin_v> vin;
    std::vector<tx_out> vout;
    std::vector<uint8_t> extra;
    std::vector<uint64_t> output_unlock_times;

    BEGIN_SERIALIZE()
    ENUM_FIELD(version, version >= txversion::v1 && version < txversion::_count);
    if (version >= txversion::v3_per_output_unlock_times) {
        FIELD(output_unlock_times)
        if (version == txversion::v3_per_output_unlock_times) {
            bool is_state_change = type == txtype::state_change;
            FIELD(is_state_change)
            type = is_state_change ? txtype::state_change : txtype::standard;
        }
    }
    VARINT_FIELD(unlock_time)
    FIELD(vin)
    FIELD(vout)
    if (version >= txversion::v3_per_output_unlock_times &&
        vout.size() != output_unlock_times.size()) {
        throw oxen::traced<std::invalid_argument>{"v3 tx without correct unlock times"};
    }
    FIELD(extra)
    if (version >= txversion::v4_tx_types)
        ENUM_FIELD_N("type", type, type < txtype::_count);
    END_SERIALIZE()

    transaction_prefix() { set_null(); }
    void set_null();

    // This function is inlined because device_ledger code needs to call it, but doesn't link
    // against cryptonote_basic.
    uint64_t get_unlock_time(size_t out_index) const {
        if (version >= txversion::v3_per_output_unlock_times) {
            if (out_index >= output_unlock_times.size()) {
                log::error(
                        globallogcat,
                        "Tried to get unlock time of a v3 transaction with missing output unlock "
                        "time");
                return unlock_time;
            }
            return output_unlock_times[out_index];
        }
        return unlock_time;
    }

    std::vector<crypto::public_key> get_public_keys() const;
};

class transaction final : public transaction_prefix {
  private:
    // hash cache
    mutable std::atomic<bool> hash_valid;
    mutable std::atomic<bool> blob_size_valid;

  public:
    std::vector<std::vector<crypto::signature>>
            signatures;  // count signatures  always the same as inputs count
    rct::rctSig rct_signatures;

    // hash cache
    mutable crypto::hash hash;
    mutable size_t blob_size;

    bool pruned;

    std::atomic<unsigned int> unprunable_size;
    std::atomic<unsigned int> prefix_size;

    transaction() { set_null(); }
    transaction(const transaction& t);
    transaction& operator=(const transaction& t);
    void set_null();
    void invalidate_hashes();
    bool is_hash_valid() const { return hash_valid.load(std::memory_order_acquire); }
    void set_hash_valid(bool v) const { hash_valid.store(v, std::memory_order_release); }
    bool is_blob_size_valid() const { return blob_size_valid.load(std::memory_order_acquire); }
    void set_blob_size_valid(bool v) const { blob_size_valid.store(v, std::memory_order_release); }
    void set_hash(const crypto::hash& h) {
        hash = h;
        set_hash_valid(true);
    }
    void set_blob_size(size_t sz) {
        blob_size = sz;
        set_blob_size_valid(true);
    }

    BEGIN_SERIALIZE_OBJECT()
    constexpr bool Binary = serialization::is_binary<Archive>;

    if (Archive::is_deserializer) {
        set_hash_valid(false);
        set_blob_size_valid(false);
    }

    unsigned int start_pos = 0;
    if constexpr (Binary)
        start_pos = ar.streampos();

    serialization::value(ar, static_cast<transaction_prefix&>(*this));

    if constexpr (Binary)
        prefix_size = ar.streampos() - start_pos;

    if (version == txversion::v1) {
        if constexpr (Binary)
            unprunable_size = ar.streampos() - start_pos;

        ar.tag("signatures");
        auto arr = ar.begin_array();
        if (Archive::is_deserializer)
            signatures.resize(vin.size());
        bool signatures_expected = !signatures.empty();
        if (signatures_expected && vin.size() != signatures.size())
            throw oxen::traced<std::invalid_argument>{"Incorrect number of signatures"};

        const size_t vin_sigs = pruned ? 0 : vin.size();
        for (size_t i = 0; i < vin_sigs; ++i) {
            size_t signature_size = get_signature_size(vin[i]);
            if (!signatures_expected) {
                if (signature_size > 0)
                    throw oxen::traced<std::invalid_argument>{"Invalid unexpected signature"};
                continue;
            }

            if (Archive::is_deserializer)
                signatures[i].resize(signature_size);
            else if (signature_size != signatures[i].size())
                throw oxen::traced<std::invalid_argument>{
                        "Invalid signature size (expected " + std::to_string(signature_size) +
                        ", have " + std::to_string(signatures[i].size()) + ")"};

            value(ar, signatures[i]);
        }
    } else {
        if (!vin.empty()) {
            {
                ar.tag("rct_signatures");
                auto obj = ar.begin_object();
                rct_signatures.serialize_rctsig_base(ar, vin.size(), vout.size());
            }

            if constexpr (Binary)
                unprunable_size = ar.streampos() - start_pos;

            if (!pruned && rct_signatures.type != rct::RCTType::Null) {
                ar.tag("rctsig_prunable");
                auto obj = ar.begin_object();
                rct_signatures.p.serialize_rctsig_prunable(
                        ar,
                        rct_signatures.type,
                        vin.size(),
                        vout.size(),
                        vin.size() > 0 && std::holds_alternative<txin_to_key>(vin[0])
                                ? var::get<txin_to_key>(vin[0]).key_offsets.size() - 1
                                : 0);
            }
        }
    }
    if (Archive::is_deserializer)
        pruned = false;
    END_SERIALIZE()

    template <class Archive>
    void serialize_base(Archive& ar) {
        serialization::value(ar, static_cast<transaction_prefix&>(*this));

        if (version != txversion::v1) {
            if (!vin.empty()) {
                ar.tag("rct_signatures");
                auto obj = ar.begin_object();
                rct_signatures.serialize_rctsig_base(ar, vin.size(), vout.size());
            }
        }
        if (Archive::is_deserializer)
            pruned = true;
    }

  private:
    static size_t get_signature_size(const txin_v& tx_in);
};

/************************************************************************/
/*                                                                      */
/************************************************************************/
struct pulse_random_value {
    unsigned char data[16];
    bool operator==(pulse_random_value const& other) const {
        return std::memcmp(data, other.data, sizeof(data)) == 0;
    }

    static constexpr bool binary_serializable = true;
};

struct pulse_header {
    pulse_random_value random_value;
    uint8_t round;
    uint16_t validator_bitset;

    bool empty() const;
};

template <typename Archive>
void serialize_value(Archive& ar, pulse_header& p) {
    auto obj = ar.begin_object();
    serialization::field(ar, "random_value", p.random_value);
    serialization::field(ar, "round", p.round);
    serialization::field(ar, "validator_bitset", p.validator_bitset);
}

struct block_header {
    hf major_version = hf::hf7;
    uint8_t minor_version = 0;
    uint64_t timestamp;
    crypto::hash prev_id;
    uint32_t nonce;
    // HF16+
    pulse_header pulse = {};

    bool has_pulse_header() const { return major_version >= feature::PULSE && !pulse.empty(); }

    // HF19, HF21+:
    // The height of this block.  This is not used by current Oxen code for the height before HF21+,
    // but must be present and correct in HF19 for compatibility with Oxen 10.x nodes which *did*
    // (sometimes) use it.  Before HF21 the height is always present in the miner_tx's txin_gen,
    // which is always present (even if sometimes mostly empty with just a txin_gen with the height
    // and no outputs).
    //
    // In HF19 this field is serialized as part of the block, rather than block header; in HF20 it
    // is not serialized at all (because it is redundant with the txin_gen); and from HF21 it is
    // part of the block header and thus affects the block hash.
    //
    // See also the notes in serialization, below, about this value under HF19 once Oxen 10.x
    // compatibility is dropped.
    //
    // Note that you should generally use the `block::get_height()` method rather than using this
    // value directly to retrieve the block height properly.
    uint64_t _height = 0;

    // Block reward, in atomic units.  This field is used slightly differently depending on the HF:
    // HF19 - the total Oxen reward the pulse block leader is claiming, which must equal the base
    //        pulse reward (16.5 OXEN, shared among all SNs) plus any tx fees (earned just by the
    //        block leader's stakers).  This is serialized as part of the *block*, not the block
    //        header, for backwards compatibility with Oxen 10.x clients that had it in the block.
    //        Does not affect the block hash as a result.
    // HF20 - same meaning as HF19, except that it is serialized as part of the block header instead
    //        of the block, and thus affects the block hash.
    // HF21 - the current SENT per-block reward rate, with various rules imposed upon how it
    //        can change from one block to the next.  This value is implicitly determined by the
    //        l2_reward value published in recent blocks.
    //
    // Note that this value is technically redundant for any of these HF versions (it can be
    // computed separately) but serializing it helps ensure that the network is in complete
    // agreement as to what the precise current reward rate is, and helps simplify internal
    // calculations by storing it alongside the block.
    uint64_t reward = 0;

    // HF20+:

    // last 4 bytes of the SN winner pubkey, serving as a simple block winner validation used to
    // catch potential reward database errors (this field does not determine the actual winner; that
    // is always known by a syncing node).  Before HF20, the full SN winner is in a tx_extra field
    // of the miner_tx and this is not included.
    crypto::hash4 sn_winner_tail = crypto::null<crypto::hash4>;

    // HF21+:

    // A recent (but moderately lagged) height of the L2 tracker the block proposer is using; this
    // height is the one associated with `reward` and determines which recently observed L2 events
    // should enter the chain for Pulse quorum validators to approve the block.
    uint64_t l2_height = 0;

    // The pre-consensus L2 adjusted contract reward balance.  This is *not* the current reward rate
    // (see `reward` instead), as the normal reward rate is based on the l2_reward values over
    // several recent blocks.  In normal times, this is simply the true L2 contract pool reward
    // balance at `l2_height`, but there is a (consensus-required) maximum amount by which this
    // value can increase from one block to the next; thus sudden shifts in the pool balance can
    // take time to be fully reflected here, during which this value increases slowly towards the
    // appropriate value, but a compromised quorum is unable to noticeably affect the reward rate.
    uint64_t l2_reward = 0;

    // vector of L2 state changes for L2 state change transactions that are still unconfirmed in
    // this block.  true = vote for confirmation, false = vote for rejection.  Votes are sorted by
    // the hash of any unconfirmed transactions for this block height.
    std::vector<bool> l2_votes;
};

struct block : public block_header {
  private:
    // hash cache
    mutable std::atomic<bool> hash_valid{false};
    void copy_hash(const block& b) {
        bool v = b.is_hash_valid();
        hash = b.hash;
        set_hash_valid(v);
    }

  public:
    block() = default;
    block(const block& b);
    block(block&& b);
    block& operator=(const block& b);
    block& operator=(block&& b);
    void invalidate_hashes() { set_hash_valid(false); }
    bool is_hash_valid() const;
    void set_hash_valid(bool v) const;

    std::optional<transaction> miner_tx;
    crypto::public_key oxen10_pulse_producer;
    std::vector<crypto::hash> tx_hashes;
    // As of HF21 this value indicates how many leading values of `tx_hashes` are eth state change
    // transactions; these *must* be eth state change txes, and any remainder must *not* be.
    uint32_t tx_eth_count;

    // hash cache
    mutable crypto::hash hash;
    std::vector<service_nodes::quorum_signature> signatures;

    // Returns the height of this block, which comes in the block header in HF21+, and in the
    // miner_tx before HF21.
    uint64_t get_height() const;

    // True if this block has pulse components, false if pre-pulse or if pulse fields are empty.
    bool has_pulse() const {
        return major_version >= feature::PULSE && (has_pulse_header() || signatures.size());
    }
};

template <class Archive>
void serialize_value(Archive& ar, block_header& b) {
    using namespace serialization;
    field(ar, "major_version", b.major_version);
    field_varint(ar, "minor_version", b.minor_version);
    field_varint(ar, "timestamp", b.timestamp);
    field(ar, "prev_id", b.prev_id);
    field(ar, "nonce", b.nonce);
    if (b.major_version >= hf::hf16_pulse)
        field(ar, "pulse", b.pulse);
    if (b.major_version >= feature::ETH_TRANSITION) {
        field_varint(ar, "reward", b.reward);
        field(ar, "sn_winner_tail", b.sn_winner_tail);
    }
    if (b.major_version >= feature::ETH_BLS) {
        field_varint(ar, "height", b._height);
        field_varint(ar, "l2_height", b.l2_height);
        field_varint(ar, "l2_reward", b.l2_reward);
        field(ar, "l2_votes", b.l2_votes);
    }
}

// (See comment in serialization below)
inline constexpr bool SUPPORT_OXEN10_INTEROP = true;

template <class Archive>
void serialize_value(Archive& ar, block& b) {
    auto _obj = ar.begin_object();
    if constexpr (Archive::is_deserializer)
        b.set_hash_valid(false);

    serialization::value(ar, static_cast<block_header&>(b));
    if (b.major_version >= feature::ETH_BLS) {
        // No miner txes ever under ETH_BLS, because we never generate OXEN anymore.
        if constexpr (Archive::is_deserializer)
            b.miner_tx = std::nullopt;
    } else {
        // Pre-eth, we always have a miner tx, though from HF19 it is only populated with ins/outs
        // when issuing a batched reward (if no batch payments are issued for the block, it's
        // basically an empty shell tx).  Before HF19 this contains the SN rewards, and if you go
        // back before pulse, it includes mining outputs (hence the name).
        if constexpr (Archive::is_deserializer)
            b.miner_tx.emplace();
        field(ar, "miner_tx", *b.miner_tx);
    }
    field(ar, "tx_hashes", b.tx_hashes);
    if (b.tx_hashes.size() > MAX_TX_PER_BLOCK)
        throw oxen::traced<std::invalid_argument>{"too many txs in block"};
    if (b.major_version >= hf::hf21_eth)
        field(ar, "tx_eth_count", b.tx_eth_count);

    if (b.major_version >= hf::hf16_pulse)
        field(ar, "signatures", b.signatures);
    if (b.major_version == hf::hf19_reward_batching) {
        // Oxen 10.x included three fields here, height, service_node_winner_key (which was actually
        // the pulse block producer, not necessarily the winner), and reward.  None are actually
        // needed (because we can always compute the values they must have), and only reward is used
        // as of Oxen 11.x (because it's very useful to keep the reward stored alongside the block).
        //
        // Dropping them is a bit tricky, though, because Oxen 10.x nodes still expect/use those
        // values so we have to keep sending them as long as there are Oxen 10 nodes on the network.
        // We build a bit of forward compatibility in here, however, by accepting *either*:
        //
        // - a single 0 varint value; this is only sent by a future Oxen 11.x release that has
        //   dropped Oxen 10 interoperability.  In this case we just set the height and producer
        //   fields to 0/null.  (Thus 11.1.x nodes with compatibility in place will still be able to
        //   sync from 11.2.x that stop sending these fields).
        // - Otherwise it's a height and we *also* read the Oxen 10-compatible height/producer
        //   values immediately after it.
        //
        // TODO: come back after all nodes are across the Oxen 11 fork and remove the
        // interoperability *and* the oxen10_pulse_provider field and leave just the dummy 0 value
        // height encoding (currently disabled in the `else` below).
        if constexpr (SUPPORT_OXEN10_INTEROP) {
            field_varint(ar, "height", b._height);
            if (Archive::is_deserializer && b._height == 0)
                // Incoming from the future where Oxen 11 doesn't use compat mode any more
                b.oxen10_pulse_producer = crypto::null<crypto::public_key>;
            else
                // Message to/from someone in Oxen 10 compat mode
                field(ar, "pulse_producer", b.oxen10_pulse_producer);
        } else {
            // TODO: enable this code once we only care about supporting Oxen 11+ nodes.

            // Serialize a 0 height, even if the value we have isn't 0 (which it might not be if we
            // added this block to our DB while running Oxen 10).
            [[maybe_unused]] uint64_t no_height = 0;
            field_varint(ar, "height", no_height);
            if (Archive::is_deserializer)
                b._height = 0;
        }
        field(ar, "reward", b.reward);
    }
}

/************************************************************************/
/*                                                                      */
/************************************************************************/
struct account_public_address {
    crypto::public_key m_spend_public_key;
    crypto::public_key m_view_public_key;

    BEGIN_SERIALIZE_OBJECT()
    FIELD(m_spend_public_key)
    FIELD(m_view_public_key)
    END_SERIALIZE()

    BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_spend_public_key)
    KV_SERIALIZE_VAL_POD_AS_BLOB_FORCE(m_view_public_key)
    END_KV_SERIALIZE_MAP()

    bool operator==(const account_public_address& rhs) const {
        return m_spend_public_key == rhs.m_spend_public_key &&
               m_view_public_key == rhs.m_view_public_key;
    }

    bool operator!=(const account_public_address& rhs) const { return !(*this == rhs); }

    uint64_t modulus(uint64_t interval) const;
    uint64_t next_payout_height(uint64_t current_height, uint64_t interval) const;
};
inline constexpr account_public_address null_address{};

struct keypair {
    crypto::public_key pub;
    crypto::secret_key sec;

    keypair() = default;

    // Constructs from a copied public/secret key
    keypair(const crypto::public_key& pub, const crypto::secret_key& sec) : pub{pub}, sec{sec} {}
    // Default copy and move
    keypair(const keypair&) = default;
    keypair(keypair&&) = default;
    keypair& operator=(const keypair&) = default;
    keypair& operator=(keypair&&) = default;

    // Constructs by generating a keypair via the given hardware device:
    explicit keypair(hw::device& hwdev) { hwdev.generate_keys(pub, sec); }
};

using byte_and_output_fees = std::pair<uint64_t, uint64_t>;

//---------------------------------------------------------------
constexpr txversion transaction_prefix::get_min_version_for_hf(hf hf_version) {
    if (hf_version >= hf::hf7 && hf_version <= hf::hf10_bulletproofs)
        return txversion::v2_ringct;
    return txversion::v4_tx_types;
}

// Used in the test suite to disable the older max version values below so that some test suite
// tests can still use particular hard forks without needing to actually generate pre-v4 txes.
namespace hack {
    inline bool test_suite_permissive_txes = false;
}

inline txversion transaction_prefix::get_max_version_for_hf(hf hf_version) {
    if (!hack::test_suite_permissive_txes) {
        if (hf_version >= hf::hf7 && hf_version <= hf::hf8)
            return txversion::v2_ringct;

        if (hf_version >= hf::hf9_service_nodes && hf_version <= hf::hf10_bulletproofs)
            return txversion::v3_per_output_unlock_times;
    }

    return txversion::v4_tx_types;
}

constexpr txtype transaction_prefix::get_max_type_for_hf(hf hf_version) {
    txtype result = txtype::standard;
    if (hf_version >= cryptonote::feature::ETH_BLS)
        result = txtype::ethereum_service_node_removal;
    else if (hf_version >= hf::hf15_ons)
        result = txtype::oxen_name_system;
    else if (hf_version >= hf::hf14_blink)
        result = txtype::stake;
    else if (hf_version >= hf::hf11_infinite_staking)
        result = txtype::key_image_unlock;
    else if (hf_version >= hf::hf9_service_nodes)
        result = txtype::state_change;

    return result;
}

// Serialization for the `hf` type; this is simply writing/reading the underlying uint8_t value
template <class Archive>
void serialize_value(Archive& ar, hf& x) {
    auto val = static_cast<std::underlying_type_t<hf>>(x);
    serialization::value(ar, val);
    if constexpr (Archive::is_deserializer)
        x = static_cast<hf>(val);
}
}  // namespace cryptonote

namespace std {
template <>
struct hash<cryptonote::account_public_address> {
    std::size_t operator()(const cryptonote::account_public_address& addr) const {
        // https://stackoverflow.com/a/17017281
        size_t res = 17;
        res = res * 31 + hash<crypto::public_key>()(addr.m_spend_public_key);
        res = res * 31 + hash<crypto::public_key>()(addr.m_view_public_key);
        return res;
    }
};
}  // namespace std

BLOB_SERIALIZER(cryptonote::txout_to_key);
BLOB_SERIALIZER(cryptonote::txout_to_scripthash);

VARIANT_TAG(cryptonote::txin_gen, "gen", 0xff);
VARIANT_TAG(cryptonote::txin_to_script, "script", 0x0);
VARIANT_TAG(cryptonote::txin_to_scripthash, "scripthash", 0x1);
VARIANT_TAG(cryptonote::txin_to_key, "key", 0x2);
VARIANT_TAG(cryptonote::txout_to_script, "script", 0x0);
VARIANT_TAG(cryptonote::txout_to_scripthash, "scripthash", 0x1);
VARIANT_TAG(cryptonote::txout_to_key, "key", 0x2);
VARIANT_TAG(cryptonote::transaction, "tx", 0xcc);
VARIANT_TAG(cryptonote::block, "block", 0xbb);

template <>
inline constexpr bool formattable::via_to_string<cryptonote::transaction> = true;
