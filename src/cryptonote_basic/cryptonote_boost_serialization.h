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

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/portable_binary_iarchive.hpp>
#include <boost/archive/portable_binary_oarchive.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/serialization/variant.hpp>
#include <boost/serialization/vector.hpp>

#include "common/unordered_containers_boost_serialization.h"
#include "common/util.h"
#include "crypto/crypto.h"
#include "crypto/eth.h"
#include "cryptonote_basic.h"
#include "ringct/rctOps.h"
#include "ringct/rctTypes.h"

namespace boost::serialization {

//---------------------------------------------------
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::public_key& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::public_key)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::secret_key& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::secret_key)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::key_derivation& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::key_derivation)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::key_image& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::key_image)]>(x);
}

template <class Archive>
inline void serialize(
        Archive& a,
        crypto::signature& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::signature)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::hash& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::hash)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::hash8& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::hash8)]>(x);
}
template <class Archive>
inline void serialize(
        Archive& a,
        crypto::hash4& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(crypto::hash4)]>(x);
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txout_to_script& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.keys;
    a& x.script;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txout_to_key& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.key;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txout_to_scripthash& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.hash;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txin_gen& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.height;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txin_to_script& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.prev;
    a& x.prevout;
    a& x.sigset;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txin_to_scripthash& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.prev;
    a& x.prevout;
    a& x.script;
    a& x.sigset;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txin_to_key& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.amount;
    a& x.key_offsets;
    a& x.k_image;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::tx_out& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.amount;
    a& x.target;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txversion& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    uint16_t v = static_cast<uint16_t>(x);
    a& v;
    if (v >= tools::enum_count<cryptonote::txversion>)
        throw boost::archive::archive_exception(
                boost::archive::archive_exception::other_exception, "Unsupported tx version");
    x = static_cast<cryptonote::txversion>(v);
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::txtype& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    uint16_t txtype = static_cast<uint16_t>(x);
    a& txtype;
    if (txtype >= tools::enum_count<cryptonote::txtype>)
        throw boost::archive::archive_exception(
                boost::archive::archive_exception::other_exception, "Unsupported tx type");
    x = static_cast<cryptonote::txtype>(txtype);
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::transaction_prefix& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.version;
    if (x.version >= cryptonote::txversion::v3_per_output_unlock_times) {
        a& x.output_unlock_times;
        if (x.version == cryptonote::txversion::v3_per_output_unlock_times) {
            bool is_deregister = x.type == cryptonote::txtype::state_change;
            a& is_deregister;
            x.type =
                    is_deregister ? cryptonote::txtype::state_change : cryptonote::txtype::standard;
        }
    }
    a& x.unlock_time;
    a& x.vin;
    a& x.vout;
    a& x.extra;
    if (x.version >= cryptonote::txversion::v4_tx_types)
        a& x.type;
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::transaction& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    serialize(a, static_cast<cryptonote::transaction_prefix&>(x), ver);
    if (x.version == cryptonote::txversion::v1) {
        a& x.signatures;
    } else {
        a&(rct::rctSigBase&)x.rct_signatures;
        if (x.rct_signatures.type != rct::RCTType::Null)
            a& x.rct_signatures.p;
    }
}

template <class Archive>
inline void serialize(
        Archive& a,
        cryptonote::block& b,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& b.major_version;
    a& b.minor_version;
    a& b.timestamp;
    a& b.prev_id;
    a& b.nonce;
    if constexpr (std::is_same_v<Archive, boost::archive::portable_binary_oarchive>) {
        assert(ver >= 1u);
        bool have_miner_tx = b.miner_tx.has_value();
        a&have_miner_tx;
        if (have_miner_tx)
            a& *b.miner_tx;
    } else {
        static_assert(std::is_same_v<Archive, boost::archive::portable_binary_iarchive>);
        bool have_miner_tx;
        if (ver >= 1u)
            a&have_miner_tx;
        else
            have_miner_tx = true;
        if (have_miner_tx)
            a & b.miner_tx.emplace();
    }

    a& b.tx_hashes;
    if (ver < 1u)
        return;
    a& b._height;
    a& b.reward;
    a& b.sn_winner_tail;
    a& b.l2_height;
    a& b.l2_reward;
}

template <class Archive>
inline void serialize(
        Archive& a, rct::key& x, [[maybe_unused]] const boost::serialization::version_type ver) {
    a& reinterpret_cast<char(&)[sizeof(rct::key)]>(x);
}

template <class Archive>
inline void serialize(
        Archive& a, rct::ctkey& x, [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.dest;
    a& x.mask;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::rangeSig& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.asig;
    a& x.Ci;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::Bulletproof& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.V;
    a& x.A;
    a& x.S;
    a& x.T1;
    a& x.T2;
    a& x.taux;
    a& x.mu;
    a& x.L;
    a& x.R;
    a& x.a;
    a& x.b;
    a& x.t;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::boroSig& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.s0;
    a& x.s1;
    a& x.ee;
}

template <class Archive>
inline void serialize(
        Archive& a, rct::mgSig& x, [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.ss;
    a& x.cc;
    // a & x.II; // not serialized, we can recover it from the tx vin
}

template <class Archive>
inline void serialize(
        Archive& a, rct::clsag& x, [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.s;
    a& x.c1;
    // a & x.I; // not serialized, we can recover it from the tx vin
    a& x.D;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::ecdhTuple& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.mask;
    a& x.amount;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::multisig_kLRki& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.k;
    a& x.L;
    a& x.R;
    a& x.ki;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::multisig_out& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.c;
    if (ver < 1)
        return;
    a& x.mu_p;
}

template <class Archive>
inline typename std::enable_if<Archive::is_loading::value, void>::type serializeOutPk(
        Archive& a,
        rct::ctkeyV& outPk_,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    rct::keyV outPk;
    a& outPk;
    outPk_.resize(outPk.size());
    for (size_t n = 0; n < outPk_.size(); ++n) {
        outPk_[n].dest = rct::identity();
        outPk_[n].mask = outPk[n];
    }
}

template <class Archive>
inline typename std::enable_if<Archive::is_saving::value, void>::type serializeOutPk(
        Archive& a,
        rct::ctkeyV& outPk_,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    rct::keyV outPk(outPk_.size());
    for (size_t n = 0; n < outPk_.size(); ++n)
        outPk[n] = outPk_[n].mask;
    a& outPk;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::rctSigBase& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.type;
    if (x.type == rct::RCTType::Null)
        return;
    if (!tools::equals_any(
                x.type,
                rct::RCTType::Full,
                rct::RCTType::Simple,
                rct::RCTType::Bulletproof,
                rct::RCTType::Bulletproof2,
                rct::RCTType::CLSAG))
        throw boost::archive::archive_exception(
                boost::archive::archive_exception::other_exception, "Unsupported rct type");
    // a & x.message; message is not serialized, as it can be reconstructed from the tx data
    // a & x.mixRing; mixRing is not serialized, as it can be reconstructed from the offsets
    if (x.type == rct::RCTType::Simple)  // moved to prunable with bulletproofs
        a& x.pseudoOuts;
    a& x.ecdhInfo;
    serializeOutPk(a, x.outPk, ver);
    a& x.txnFee;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::rctSigPrunable& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.rangeSigs;
    if (x.rangeSigs.empty())
        a& x.bulletproofs;
    a& x.MGs;
    if (ver >= 1u)
        a& x.CLSAGs;
    if (x.rangeSigs.empty())
        a& x.pseudoOuts;
}

template <class Archive>
inline void serialize(
        Archive& a, rct::rctSig& x, [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.type;
    if (x.type == rct::RCTType::Null)
        return;
    if (!tools::equals_any(
                x.type,
                rct::RCTType::Full,
                rct::RCTType::Simple,
                rct::RCTType::Bulletproof,
                rct::RCTType::Bulletproof2,
                rct::RCTType::CLSAG))
        throw boost::archive::archive_exception(
                boost::archive::archive_exception::other_exception, "Unsupported rct type");
    // a & x.message; message is not serialized, as it can be reconstructed from the tx data
    // a & x.mixRing; mixRing is not serialized, as it can be reconstructed from the offsets
    if (x.type == rct::RCTType::Simple)
        a& x.pseudoOuts;
    a& x.ecdhInfo;
    serializeOutPk(a, x.outPk, ver);
    a& x.txnFee;
    //--------------
    a& x.p.rangeSigs;
    if (x.p.rangeSigs.empty())
        a& x.p.bulletproofs;
    a& x.p.MGs;
    if (ver >= 1u)
        a& x.p.CLSAGs;
    if (rct::is_rct_bulletproof(x.type))
        a& x.p.pseudoOuts;
}

template <class Archive>
inline void serialize(
        Archive& a,
        rct::RCTConfig& x,
        [[maybe_unused]] const boost::serialization::version_type ver) {
    a& x.range_proof_type;
    a& x.bp_version;
}
}  // namespace boost::serialization

BOOST_CLASS_VERSION(rct::rctSigPrunable, 1)
BOOST_CLASS_VERSION(rct::rctSig, 1)
BOOST_CLASS_VERSION(rct::multisig_out, 1)
BOOST_CLASS_VERSION(cryptonote::block, 1)
