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

#include "cryptonote_core/blockchain.h"

namespace cryptonote {
// This class is meant to create a batch when none currently exists.
// If a batch exists, it can't be from another thread, since we can
// only be called with the txpool lock taken, and it is held during
// the whole prepare/handle/cleanup incoming block sequence.
class LockedTXN {
  public:
    LockedTXN(Blockchain& b) : m_db{b.db()} { m_batch = m_db.batch_start(); }
    LockedTXN(const LockedTXN&) = delete;
    LockedTXN& operator=(const LockedTXN&) = delete;
    LockedTXN(LockedTXN&& o) : m_db{o.m_db}, m_batch{o.m_batch} { o.m_batch = false; }
    LockedTXN& operator=(LockedTXN&&) = delete;

    void commit() {
        try {
            if (m_batch) {
                m_db.batch_stop();
                m_batch = false;
            }
        } catch (const std::exception& e) {
            log::warning(globallogcat, "LockedTXN::commit filtering exception: {}", e.what());
        }
    }
    void abort() {
        try {
            if (m_batch) {
                m_db.batch_abort();
                m_batch = false;
            }
        } catch (const std::exception& e) {
            log::warning(globallogcat, "LockedTXN::abort filtering exception: {}", e.what());
        }
    }
    ~LockedTXN() { this->abort(); }

  private:
    BlockchainDB& m_db;
    bool m_batch;
};
}  // namespace cryptonote
