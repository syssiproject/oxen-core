// Copyright (c) 2017-2019, The Monero Project
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

#pragma once

#include <boost/serialization/serialization.hpp>
#include <boost/serialization/version.hpp>
#include <ostream>

#include "common/format.h"
#include "common/formattable.h"
#include "common/oxen.h"
#include "epee/serialization/keyvalue_serialization.h"

namespace cryptonote {
OXEN_RPC_DOC_INTROSPECT
struct subaddress_index {
    uint32_t major;  // The account index, major index
    uint32_t minor;  // The subaddress index, minor index
    bool operator==(const subaddress_index& rhs) const {
        return major == rhs.major && minor == rhs.minor;
    }
    bool operator!=(const subaddress_index& rhs) const { return !(*this == rhs); }
    bool is_zero() const { return major == 0 && minor == 0; }

    std::string to_string() const { return "{}/{}"_format(major, minor); }

    BEGIN_KV_SERIALIZE_MAP()
    KV_SERIALIZE(major)
    KV_SERIALIZE(minor)
    END_KV_SERIALIZE_MAP()
};

template <class Archive>
void serialize_value(Archive& ar, subaddress_index& x) {
    field(ar, "major", x.major);
    field(ar, "minor", x.minor);
}
}  // namespace cryptonote

template <>
inline constexpr bool formattable::via_to_string<cryptonote::subaddress_index> = true;

namespace std {
template <>
struct hash<cryptonote::subaddress_index> {
    size_t operator()(const cryptonote::subaddress_index& index) const {
        size_t res;
        if (sizeof(size_t) == 8) {
            res = ((uint64_t)index.major << 32) | index.minor;
        } else {
            // https://stackoverflow.com/a/17017281
            res = 17;
            res = res * 31 + hash<uint32_t>()(index.major);
            res = res * 31 + hash<uint32_t>()(index.minor);
        }
        return res;
    }
};
}  // namespace std

BOOST_CLASS_VERSION(cryptonote::subaddress_index, 0)

namespace boost::serialization {
template <class Archive>
inline void serialize(
        Archive& a, cryptonote::subaddress_index& x, const boost::serialization::version_type ver) {
    a& x.major;
    a& x.minor;
}
}  // namespace boost::serialization
