// Copyright (c)      2018, The Loki Project
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

#include "service_node_voting.h"

#include <string>
#include <vector>

#include "checkpoints/checkpoints.h"
#include "common/util.h"
#include "cryptonote_basic/connection_context.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/tx_extra.h"
#include "cryptonote_basic/verification_context.h"
#include "cryptonote_protocol/cryptonote_protocol_defs.h"
#include "epee/misc_log_ex.h"
#include "epee/string_tools.h"
#include "service_node_list.h"

using cryptonote::hf;

namespace service_nodes {
static auto logcat = log::Cat("service_nodes");

static crypto::hash make_state_change_vote_hash(
        uint64_t block_height, uint32_t service_node_index, new_state state) {
    uint16_t state_int = static_cast<uint16_t>(state);

    auto buf = tools::memcpy_le(block_height, service_node_index, state_int);

    auto size = buf.size();
    if (state == new_state::deregister)
        size -= sizeof(state_int);  // Don't include state value for deregs (to be backwards
                                    // compatible with pre-v12 dereg votes)

    crypto::hash result;
    crypto::cn_fast_hash(buf.data(), size, result);
    return result;
}

crypto::signature make_signature_from_vote(
        quorum_vote_t const& vote, const service_node_keys& keys) {
    crypto::signature result = {};
    switch (vote.type) {
        default: {
            log::info(logcat, "Unhandled vote type with value: {}", (int)vote.type);
            assert("Unhandled vote type" == 0);
            return result;
        };

        case quorum_type::obligations: {
            crypto::hash hash = make_state_change_vote_hash(
                    vote.block_height, vote.state_change.worker_index, vote.state_change.state);
            crypto::generate_signature(hash, keys.pub, keys.key, result);
        } break;

        case quorum_type::checkpointing: {
            crypto::hash hash = vote.checkpoint.block_hash;
            crypto::generate_signature(hash, keys.pub, keys.key, result);
        } break;
    }

    return result;
}

crypto::signature make_signature_from_tx_state_change(
        cryptonote::tx_extra_service_node_state_change const& state_change,
        const service_node_keys& keys) {
    crypto::signature result;
    crypto::hash hash = make_state_change_vote_hash(
            state_change.block_height, state_change.service_node_index, state_change.state);
    crypto::generate_signature(hash, keys.pub, keys.key, result);
    return result;
}

static bool bounds_check_worker_index(
        service_nodes::quorum const& quorum,
        uint32_t worker_index,
        cryptonote::vote_verification_context* vvc) {
    if (worker_index >= quorum.workers.size()) {
        if (vvc)
            vvc->m_worker_index_out_of_bounds = true;
        log::info(
                logcat,
                "Quorum worker index in was out of bounds: {}, expected to be in range of: [0, {}",
                worker_index,
                quorum.workers.size());
        return false;
    }
    return true;
}

static bool bounds_check_validator_index(
        service_nodes::quorum const& quorum,
        uint32_t validator_index,
        cryptonote::vote_verification_context* vvc) {
    if (validator_index >= quorum.validators.size()) {
        if (vvc)
            vvc->m_validator_index_out_of_bounds = true;
        log::info(
                logcat,
                "Validator's index was out of bounds: {}, expected to be in range of: [0, {}",
                validator_index,
                quorum.validators.size());
        return false;
    }
    return true;
}

static bool bad_tx(cryptonote::tx_verification_context& tvc) {
    tvc.m_verifivation_failed = true;
    return false;
}

bool verify_tx_state_change(
        const cryptonote::tx_extra_service_node_state_change& state_change,
        uint64_t latest_height,
        cryptonote::tx_verification_context& tvc,
        const service_nodes::quorum& quorum,
        const hf hf_version) {
    auto& vvc = tvc.m_vote_ctx;
    if (state_change.state != new_state::deregister && hf_version < hf::hf12_checkpointing) {
        log::info(logcat, "Non-deregister state changes are invalid before v12");
        return bad_tx(tvc);
    }

    if (state_change.state >= new_state::_count) {
        log::info(
                logcat,
                "Unknown state change to new state: {}",
                static_cast<uint16_t>(state_change.state));
        return bad_tx(tvc);
    }

    if (state_change.votes.size() < service_nodes::STATE_CHANGE_MIN_VOTES_TO_CHANGE_STATE) {
        log::info(logcat, "Not enough votes");
        vvc.m_not_enough_votes = true;
        return bad_tx(tvc);
    }

    if (state_change.votes.size() > service_nodes::STATE_CHANGE_QUORUM_SIZE) {
        log::info(logcat, "Too many votes");
        return bad_tx(tvc);
    }

    if (!bounds_check_worker_index(quorum, state_change.service_node_index, &vvc))
        return bad_tx(tvc);

    // Check if state_change is too old or too new to hold onto
    {
        if (state_change.block_height >= latest_height) {
            log::info(
                    logcat,
                    "Received state change tx for height: {} and service node: {}, is newer than "
                    "current height: {} blocks and has been rejected.",
                    state_change.block_height,
                    state_change.service_node_index,
                    latest_height);
            vvc.m_invalid_block_height = true;
            if (state_change.block_height >= latest_height + VOTE_OR_TX_VERIFY_HEIGHT_BUFFER)
                tvc.m_verifivation_failed = true;
            return false;
        }

        if (latest_height >=
            state_change.block_height + service_nodes::STATE_CHANGE_TX_LIFETIME_IN_BLOCKS) {
            log::info(
                    logcat,
                    "Received state change tx for height: {} and service node: {}, is older than: "
                    "{} (current height: {}) blocks and has been rejected.",
                    state_change.block_height,
                    state_change.service_node_index,
                    service_nodes::STATE_CHANGE_TX_LIFETIME_IN_BLOCKS,
                    latest_height);
            vvc.m_invalid_block_height = true;
            if (latest_height >=
                state_change.block_height + (service_nodes::STATE_CHANGE_TX_LIFETIME_IN_BLOCKS +
                                             VOTE_OR_TX_VERIFY_HEIGHT_BUFFER))
                tvc.m_verifivation_failed = true;
            return false;
        }
    }

    crypto::hash const hash = make_state_change_vote_hash(
            state_change.block_height, state_change.service_node_index, state_change.state);
    std::array<int, service_nodes::STATE_CHANGE_QUORUM_SIZE> validator_set = {};
    int validator_index_tracker = -1;
    for (const auto& vote : state_change.votes) {
        if (hf_version >= hf::hf13_enforce_checkpoints)  // NOTE: After HF13, votes must be stored
                                                         // in ascending order
        {
            if (validator_index_tracker >= static_cast<int>(vote.validator_index)) {
                vvc.m_votes_not_sorted = true;
                log::info(
                        logcat,
                        "Vote validator index is not stored in ascending order, prev validator "
                        "index: {}, curr index: {}",
                        validator_index_tracker,
                        vote.validator_index);
                return bad_tx(tvc);
            }
            validator_index_tracker = vote.validator_index;
        }

        if (!bounds_check_validator_index(quorum, vote.validator_index, &vvc))
            return bad_tx(tvc);

        if (++validator_set[vote.validator_index] > 1) {
            vvc.m_duplicate_voters = true;
            log::info(logcat, "Voter quorum index is duplicated: {}", vote.validator_index);
            return bad_tx(tvc);
        }

        crypto::public_key const& key = quorum.validators[vote.validator_index];
        if (!crypto::check_signature(hash, key, vote.signature)) {
            log::info(logcat, "Invalid signature for voter {}/{}", vote.validator_index, key);
            vvc.m_signature_not_valid = true;
            return bad_tx(tvc);
        }
    }

    return true;
}

bool verify_quorum_signatures(
        service_nodes::quorum const& quorum,
        service_nodes::quorum_type type,
        hf hf_version,
        uint64_t height,
        crypto::hash const& hash,
        std::vector<quorum_signature> const& signatures,
        const cryptonote::block* block) {
    bool enforce_vote_ordering = true;
    constexpr size_t MAX_QUORUM_SIZE =
            std::max(CHECKPOINT_QUORUM_SIZE, PULSE_QUORUM_NUM_VALIDATORS);
    std::array<size_t, MAX_QUORUM_SIZE> unique_vote_set = {};

    switch (type) {
        default:
            assert(!"Invalid Code Path");
            break;

            // TODO(oxen): DRY quorum verification with state change obligations.

        case quorum_type::checkpointing: {
            if (signatures.size() < service_nodes::CHECKPOINT_MIN_VOTES) {
                log::warning(
                        globallogcat,
                        "Checkpoint has insufficient signatures to be considered at height: {}",
                        height);
                return false;
            }

            if (signatures.size() > service_nodes::CHECKPOINT_QUORUM_SIZE) {
                log::warning(
                        globallogcat,
                        "Checkpoint has too many signatures to be considered at height: {}",
                        height);
                return false;
            }

            enforce_vote_ordering = hf_version >= hf::hf13_enforce_checkpoints;
        } break;

        case quorum_type::pulse: {
            if (signatures.size() != PULSE_BLOCK_REQUIRED_SIGNATURES) {
                log::warning(
                        globallogcat,
                        "Pulse block has {} signatures but requires {}",
                        signatures.size(),
                        PULSE_BLOCK_REQUIRED_SIGNATURES);
                return false;
            }

            if (!block) {
                log::warning(
                        globallogcat,
                        "Internal Error: Wrong type passed in any object, expected block.");
                return false;
            }

            if (block->pulse.validator_bitset >= (1 << PULSE_QUORUM_NUM_VALIDATORS)) {
                auto mask = std::bitset<sizeof(pulse_validator_bit_mask()) * 8>(
                        pulse_validator_bit_mask());
                auto other = std::bitset<sizeof(pulse_validator_bit_mask()) * 8>(
                        block->pulse.validator_bitset);
                log::warning(
                        globallogcat,
                        "Pulse block specifies validator participation bits out of bounds. "
                        "Expected the bit mask: {}, block: {}",
                        mask.to_string(),
                        other.to_string());
                return false;
            }
        } break;
    }

    for (size_t i = 0; i < signatures.size(); i++) {
        service_nodes::quorum_signature const& quorum_signature = signatures[i];
        if (enforce_vote_ordering && i < (signatures.size() - 1)) {
            auto curr = signatures[i].voter_index;
            auto next = signatures[i + 1].voter_index;

            if (curr >= next) {
                log::warning(
                        globallogcat,
                        "Voters in signatures are not given in ascending order, failed "
                        "verification at height: {}",
                        height);
                return false;
            }
        }

        if (!bounds_check_validator_index(quorum, quorum_signature.voter_index, nullptr))
            return false;

        if (type == quorum_type::pulse) {
            if (!block) {
                log::error(
                        globallogcat,
                        "Internal Error: Wrong type passed in any object, expected block.");
                return false;
            }

            uint16_t bit = 1 << quorum_signature.voter_index;
            if ((block->pulse.validator_bitset & bit) == 0) {
                log::warning(
                        globallogcat,
                        "Received pulse signature from validator {} that is not participating in "
                        "round {}",
                        static_cast<int>(quorum_signature.voter_index),
                        static_cast<int>(block->pulse.round));
                return false;
            }
        }

        crypto::public_key const& key = quorum.validators[quorum_signature.voter_index];
        if (quorum_signature.voter_index >= unique_vote_set.size()) {
            log::warning(
                    globallogcat,
                    "Internal Error: Voter Index indexes out of bounds of the vote set, index: "
                    "{}vote set size: {}",
                    quorum_signature.voter_index,
                    unique_vote_set.size());
            return false;
        }

        if (unique_vote_set[quorum_signature.voter_index]++) {
            log::warning(
                    globallogcat,
                    "Voter: {}, quorum index is duplicated: {}, failed verification at height: {}",
                    key,
                    quorum_signature.voter_index,
                    height);
            return false;
        }

        if (!crypto::check_signature(hash, key, quorum_signature.signature)) {
            log::warning(
                    globallogcat,
                    "Incorrect signature for vote, failed verification at height: {} for voter: "
                    "{}\n{}",
                    height,
                    key,
                    quorum);
            return false;
        }
    }

    return true;
}

bool verify_pulse_quorum_sizes(service_nodes::quorum const& quorum) {
    bool result =
            quorum.workers.size() == 1 && quorum.validators.size() == PULSE_QUORUM_NUM_VALIDATORS;
    return result;
}

bool verify_checkpoint(
        hf hf_version,
        cryptonote::checkpoint_t const& checkpoint,
        service_nodes::quorum const& quorum) {
    if (checkpoint.type == cryptonote::checkpoint_type::service_node) {
        if ((checkpoint.height % service_nodes::CHECKPOINT_INTERVAL) != 0) {
            log::info(
                    logcat,
                    "Checkpoint given but not expecting a checkpoint at height: {}",
                    checkpoint.height);
            return false;
        }

        if (!verify_quorum_signatures(
                    quorum,
                    quorum_type::checkpointing,
                    hf_version,
                    checkpoint.height,
                    checkpoint.block_hash,
                    checkpoint.signatures)) {
            log::info(
                    logcat,
                    "Checkpoint failed signature validation at block {} {}",
                    checkpoint.height,
                    checkpoint.block_hash);
            return false;
        }
    } else {
        if (checkpoint.signatures.size() != 0) {
            log::info(
                    logcat,
                    "Non service-node checkpoints should have no signatures, checkpoint failed at "
                    "height: {}",
                    checkpoint.height);
            return false;
        }
    }

    return true;
}

quorum_vote_t make_state_change_vote(
        uint64_t block_height,
        uint16_t validator_index,
        uint16_t worker_index,
        new_state state,
        uint16_t reason,
        const service_node_keys& keys) {
    quorum_vote_t result = {};
    result.type = quorum_type::obligations;
    result.block_height = block_height;
    result.group = quorum_group::validator;
    result.index_in_group = validator_index;
    result.state_change.worker_index = worker_index;
    result.state_change.state = state;
    result.state_change.reason = reason;
    result.signature = make_signature_from_vote(result, keys);
    return result;
}

quorum_vote_t make_checkpointing_vote(
        hf hf_version,
        crypto::hash const& block_hash,
        uint64_t block_height,
        uint16_t index_in_quorum,
        const service_node_keys& keys) {
    quorum_vote_t result = {};
    result.type = quorum_type::checkpointing;
    result.checkpoint.block_hash = block_hash;
    result.block_height = block_height;
    result.group = quorum_group::validator;
    result.index_in_group = index_in_quorum;
    result.signature = make_signature_from_vote(result, keys);
    return result;
}

cryptonote::checkpoint_t make_empty_service_node_checkpoint(
        crypto::hash const& block_hash, uint64_t height) {
    cryptonote::checkpoint_t result = {};
    result.type = cryptonote::checkpoint_type::service_node;
    result.height = height;
    result.block_hash = block_hash;
    return result;
}

bool verify_vote_age(
        const quorum_vote_t& vote,
        uint64_t latest_height,
        cryptonote::vote_verification_context& vvc) {
    bool result = true;
    bool height_in_buffer = false;
    if (latest_height > vote.block_height + VOTE_LIFETIME) {
        height_in_buffer = latest_height <=
                           vote.block_height + (VOTE_LIFETIME + VOTE_OR_TX_VERIFY_HEIGHT_BUFFER);
        log::info(
                logcat,
                "Received vote for height: {}, is older than: {} blocks and has been rejected.",
                vote.block_height,
                VOTE_LIFETIME);
        vvc.m_invalid_block_height = true;
    } else if (vote.block_height > latest_height) {
        height_in_buffer = vote.block_height <= latest_height + VOTE_OR_TX_VERIFY_HEIGHT_BUFFER;
        log::info(
                logcat,
                "Received vote for height: {}, is newer than: {} (latest block height) and has "
                "been rejected.",
                vote.block_height,
                latest_height);
        vvc.m_invalid_block_height = true;
    }

    if (vvc.m_invalid_block_height) {
        vvc.m_verification_failed = !height_in_buffer;
        result = false;
    }

    return result;
}

bool verify_vote_signature(
        hf hf_version,
        const quorum_vote_t& vote,
        cryptonote::vote_verification_context& vvc,
        const service_nodes::quorum& quorum) {
    bool result = true;
    if (vote.type > tools::enum_top<quorum_type>) {
        vvc.m_invalid_vote_type = true;
        result = false;
    }

    if (vote.group > quorum_group::worker || vote.group < quorum_group::validator) {
        vvc.m_incorrect_voting_group = true;
        result = false;
    }

    if (!result)
        return result;

    if (vote.group == quorum_group::validator)
        result = bounds_check_validator_index(quorum, vote.index_in_group, &vvc);
    else
        result = bounds_check_worker_index(quorum, vote.index_in_group, &vvc);

    if (!result)
        return result;

    crypto::public_key key{};
    crypto::hash hash{};

    switch (vote.type) {
        default: {
            log::info(logcat, "Unhandled vote type with value: {}", (int)vote.type);
            assert("Unhandled vote type" == 0);
            return false;
        };

        case quorum_type::obligations: {
            if (vote.group != quorum_group::validator) {
                log::info(
                        logcat,
                        "Vote received specifies incorrect voting group, expected vote from "
                        "validator");
                vvc.m_incorrect_voting_group = true;
                result = false;
            } else {
                key = quorum.validators[vote.index_in_group];
                hash = make_state_change_vote_hash(
                        vote.block_height, vote.state_change.worker_index, vote.state_change.state);
                result = bounds_check_worker_index(quorum, vote.state_change.worker_index, &vvc);
            }
        } break;

        case quorum_type::checkpointing: {
            if (vote.group != quorum_group::validator) {
                log::info(logcat, "Vote received specifies incorrect voting group");
                vvc.m_incorrect_voting_group = true;
                result = false;
            } else {
                key = quorum.validators[vote.index_in_group];
                hash = vote.checkpoint.block_hash;
            }
        } break;
    }

    if (!result)
        return result;

    result = crypto::check_signature(hash, key, vote.signature);
    if (result)
        log::debug(
                logcat,
                "Signature accepted for {} voter {}/{} voting for worker {} at height {}",
                vote.type,
                vote.index_in_group,
                key,
                (vote.type == quorum_type::obligations
                         ? " voting for worker " + std::to_string(vote.state_change.worker_index)
                         : ""),
                vote.block_height);
    else
        vvc.m_signature_not_valid = true;

    return result;
}

template <typename T>
static std::vector<pool_vote_entry>* find_vote_in_pool(
        std::vector<T>& pool, const quorum_vote_t& vote, bool create) {
    T typed_vote{vote};
    auto it = std::find(pool.begin(), pool.end(), typed_vote);
    if (it != pool.end())
        return &it->votes;
    if (!create)
        return nullptr;
    pool.push_back(std::move(typed_vote));
    return &pool.back().votes;
}

std::vector<pool_vote_entry>* voting_pool::find_vote_pool(
        const quorum_vote_t& find_vote, bool create_if_not_found) {
    switch (find_vote.type) {
        default:
            log::info(logcat, "Unhandled find_vote type with value: {}", (int)find_vote.type);
            assert("Unhandled find_vote type" == 0);
            return nullptr;

        case quorum_type::obligations:
            return find_vote_in_pool(m_obligations_pool, find_vote, create_if_not_found);

        case quorum_type::checkpointing:
            return find_vote_in_pool(m_checkpoint_pool, find_vote, create_if_not_found);
    }
}

void voting_pool::set_relayed(const std::vector<quorum_vote_t>& votes) {
    std::unique_lock lock{m_lock};
    const time_t now = time(NULL);

    for (const quorum_vote_t& find_vote : votes) {
        std::vector<pool_vote_entry>* vote_pool = find_vote_pool(find_vote);

        if (vote_pool)  // We found the group that this vote belongs to
        {
            auto vote = std::find_if(
                    vote_pool->begin(),
                    vote_pool->end(),
                    [&find_vote](pool_vote_entry const& entry) {
                        return (find_vote.index_in_group == entry.vote.index_in_group);
                    });

            if (vote != vote_pool->end()) {
                vote->time_last_sent_p2p = now;
            }
        }
    }
}

template <typename T>
static void append_relayable_votes(
        std::vector<quorum_vote_t>& result,
        const T& pool,
        const uint64_t max_last_sent,
        uint64_t min_height) {
    for (const auto& pool_entry : pool)
        for (const auto& vote_entry : pool_entry.votes)
            if (vote_entry.vote.block_height >= min_height &&
                vote_entry.time_last_sent_p2p <= max_last_sent)
                result.push_back(vote_entry.vote);
}

std::vector<quorum_vote_t> voting_pool::get_relayable_votes(
        uint64_t height, hf hf_version, bool quorum_relay) const {
    std::unique_lock lock{m_lock};

    // TODO(doyle): Rate-limiting: A better threshold value that follows suite with transaction
    // relay time back-off
    constexpr uint64_t TIME_BETWEEN_RELAY = 60 * 2;

    const uint64_t max_last_sent = static_cast<uint64_t>(time(nullptr)) - TIME_BETWEEN_RELAY;
    const uint64_t min_height = height > VOTE_LIFETIME ? height - VOTE_LIFETIME : 0;

    std::vector<quorum_vote_t> result;

    if (quorum_relay && hf_version < hf::hf14_blink)
        return result;  // no quorum relaying before HF14

    if (hf_version < hf::hf14_blink || quorum_relay)
        append_relayable_votes(result, m_obligations_pool, max_last_sent, min_height);

    if (hf_version < hf::hf14_blink || !quorum_relay)
        append_relayable_votes(result, m_checkpoint_pool, max_last_sent, min_height);

    return result;
}

// return: True if the vote was unique
static bool add_vote_to_pool_if_unique(
        std::vector<pool_vote_entry>& votes, quorum_vote_t const& vote) {
    auto vote_it = std::lower_bound(
            votes.begin(),
            votes.end(),
            vote,
            [](pool_vote_entry const& pool_entry, quorum_vote_t const& vote) {
                assert(pool_entry.vote.group == vote.group);
                return pool_entry.vote.index_in_group < vote.index_in_group;
            });

    if (vote_it == votes.end() || vote_it->vote.index_in_group != vote.index_in_group) {
        votes.insert(vote_it, {vote});
        return true;
    }

    return false;
}

std::vector<pool_vote_entry> voting_pool::add_pool_vote_if_unique(
        const quorum_vote_t& vote, cryptonote::vote_verification_context& vvc) {
    std::unique_lock lock{m_lock};
    auto* votes = find_vote_pool(vote, /*create_if_not_found=*/true);
    if (!votes)
        return {};

    vvc.m_added_to_pool = add_vote_to_pool_if_unique(*votes, vote);
    return *votes;
}

void voting_pool::remove_used_votes(std::vector<cryptonote::transaction> const& txs, hf version) {
    // TODO(doyle): Cull checkpoint votes
    std::unique_lock lock{m_lock};
    if (m_obligations_pool.empty())
        return;

    for (const auto& tx : txs) {
        if (tx.type != cryptonote::txtype::state_change)
            continue;

        cryptonote::tx_extra_service_node_state_change state_change;
        if (!get_service_node_state_change_from_tx_extra(tx.extra, state_change, version)) {
            log::error(logcat, "Could not get state change from tx, possibly corrupt tx");
            continue;
        }

        auto it = std::find(m_obligations_pool.begin(), m_obligations_pool.end(), state_change);

        if (it != m_obligations_pool.end())
            m_obligations_pool.erase(it);
    }
}

template <typename T>
static void cull_votes(std::vector<T>& vote_pool, uint64_t min_height, uint64_t max_height) {
    for (auto it = vote_pool.begin(); it != vote_pool.end();) {
        const T& pool_entry = *it;
        if (pool_entry.height < min_height || pool_entry.height > max_height)
            it = vote_pool.erase(it);
        else
            ++it;
    }
}

void voting_pool::remove_expired_votes(uint64_t height) {
    std::unique_lock lock{m_lock};
    uint64_t min_height = (height < VOTE_LIFETIME) ? 0 : height - VOTE_LIFETIME;
    cull_votes(m_obligations_pool, min_height, height);
    cull_votes(m_checkpoint_pool, min_height, height);
}

bool voting_pool::received_checkpoint_vote(uint64_t height, size_t index_in_quorum) const {
    auto pool_it = std::find_if(
            m_checkpoint_pool.begin(),
            m_checkpoint_pool.end(),
            [height](checkpoint_pool_entry const& entry) { return entry.height == height; });

    if (pool_it == m_checkpoint_pool.end())
        return false;

    for (auto it = pool_it->votes.begin(); it != pool_it->votes.end(); it++) {
        if (it->vote.index_in_group == index_in_quorum)
            return true;
    }

    return false;
}

KV_SERIALIZE_MAP_CODE_BEGIN(quorum_vote_t)
KV_SERIALIZE(version)
KV_SERIALIZE_ENUM(type)
KV_SERIALIZE(block_height)
KV_SERIALIZE_ENUM(group)
KV_SERIALIZE(index_in_group)
KV_SERIALIZE_VAL_POD_AS_BLOB(signature)
if (this_ref.type == quorum_type::checkpointing) {
    KV_SERIALIZE_VAL_POD_AS_BLOB_N(checkpoint.block_hash, "checkpoint")
} else {
    KV_SERIALIZE(state_change.worker_index)
    KV_SERIALIZE_ENUM(state_change.state)
}
KV_SERIALIZE_MAP_CODE_END()
}  // namespace service_nodes

namespace cryptonote {
KV_SERIALIZE_MAP_CODE_BEGIN(NOTIFY_NEW_SERVICE_NODE_VOTE::request)
KV_SERIALIZE(votes)
KV_SERIALIZE_MAP_CODE_END()
}  // namespace cryptonote
