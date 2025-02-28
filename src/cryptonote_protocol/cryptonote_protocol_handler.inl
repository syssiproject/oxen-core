/// @file
/// @author rfree (current maintainer/user in monero.cc project - most of code is from CryptoNote)
/// @brief This is the original cryptonote protocol network-events handler, modified by us

// Copyright (c) 2018-2020, The Loki Project
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

// (may contain code and/or modifications by other developers)
// developer rfree: this code is caller of our new network code, and is modded; e.g. for rate limiting

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <list>
#include <ctime>
#include <chrono>
#include <fmt/core.h>

#include "cryptonote_protocol/cryptonote_protocol_handler.h"
#include "common/string_util.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_basic/hardfork.h"
#include "cryptonote_basic/verification_context.h"
#include "cryptonote_core/cryptonote_core.h"
#include "cryptonote_core/tx_pool.h"
#include "epee/net/network_throttle-detail.hpp"
#include "common/pruning.h"
#include "common/random.h"
#include "common/lock.h"
#include "common/util.h"
#include <fmt/format.h>
#include <fmt/color.h>

namespace cryptonote
{
  static auto logcat = log::Cat("net.cn");

  constexpr size_t BLOCK_QUEUE_NSPANS_THRESHOLD = 10; // chunks of N blocks
  constexpr size_t BLOCK_QUEUE_SIZE_THRESHOLD = 100*1024*1024; // bytes, i.e. 100 MB
  constexpr uint64_t BLOCK_QUEUE_FORCE_DOWNLOAD_NEAR_BLOCKS = 1000;
  constexpr auto REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY = 5s;
  constexpr auto REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD = 30s;
  constexpr auto IDLE_PEER_KICK_TIME = 10min;
  constexpr auto PASSIVE_PEER_KICK_TIME = 1min;
  constexpr auto DROP_ON_SYNC_WEDGE_THRESHOLD = 30s;
  constexpr auto LAST_ACTIVITY_STALL_THRESHOLD = 2s;

  using seconds_f = std::chrono::duration<double>;

  //-----------------------------------------------------------------------------------------------------------------------
  template<class t_core>
    t_cryptonote_protocol_handler<t_core>::t_cryptonote_protocol_handler(t_core& rcore, bool offline):m_core(rcore),
                                                                                                              m_p2p(&m_p2p_stub),
                                                                                                              m_syncronized_connections_count(0),
                                                                                                              m_synchronized(offline),
                                                                                                              m_stopping(false),
                                                                                                              m_no_sync(false)

  {
  }
  //-----------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::init(const boost::program_options::variables_map& vm)
  {
    m_sync_timer = std::chrono::steady_clock::now();
    m_last_add_end_time = std::chrono::steady_clock::now();
    m_sync_spans_downloaded = 0;
    m_sync_old_spans_downloaded = 0;
    m_sync_bad_spans_downloaded = 0;
    m_sync_download_chain_size = 0;
    m_sync_download_objects_size = 0;

    m_block_download_max_size = command_line::get_arg(vm, cryptonote::arg_block_download_max_size);

    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::deinit()
  {
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::set_p2p_endpoint(nodetool::i_p2p_endpoint<connection_context>* p2p)
  {
    if(p2p)
      m_p2p = p2p;
    else
      m_p2p = &m_p2p_stub;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::on_callback(cryptonote_connection_context& context)
  {
    log::debug(logcat, "callback fired");
    CHECK_AND_ASSERT_MES_CC( context.m_callback_request_count > 0, false, "false callback fired, but context.m_callback_request_count={}", context.m_callback_request_count.load());
    --context.m_callback_request_count;

    if(context.m_state == cryptonote_connection_context::state_synchronizing)
    {
      NOTIFY_REQUEST_CHAIN::request r{};
      context.m_needed_objects.clear();
      m_core.blockchain.get_short_chain_history(r.block_ids);
      log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_CHAIN: m_block_ids.size()={}", r.block_ids.size());
      post_notify<NOTIFY_REQUEST_CHAIN>(r, context);


      log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting chain", cryptonote::get_protocol_state_string(context.m_state));
    }
    else if(context.m_state == cryptonote_connection_context::state_standby)
    {
      context.m_state = cryptonote_connection_context::state_synchronizing;
      try_add_next_blocks(context);
    }


    if (context.m_need_blink_sync)
    {
      NOTIFY_REQUEST_BLOCK_BLINKS::request r{};
      auto curr_height = m_core.blockchain.get_current_blockchain_height();
      auto my_blink_hashes = m_core.mempool.get_blink_checksums();
      const uint64_t immutable_height = m_core.blockchain.get_immutable_height();
      // Delete any irrelevant heights > 0 (the mempool) and <= the immutable height
      context.m_blink_state.erase(context.m_blink_state.lower_bound(1), context.m_blink_state.lower_bound(immutable_height + 1));

      // We can't validate blinks yet if we are syncing and haven't synced enough blocks to look
      // up the blink quorum.  Set a cutoff at current height plus 10 because blink quorums are
      // defined by 35 and 30 blocks ago, so even if we are 10 blocks behind the blink quorum will
      // still be 20-25 blocks old which means we can form it and it is likely to be checkpointed.
      const uint64_t future_height_limit = curr_height + 10;

      // m_blink_state: HEIGHT => {CHECKSUM, NEEDED}
      for (auto &i : context.m_blink_state)
      {
        if (!i.second.second) continue;

        if (i.first > future_height_limit) continue;

        // We thought we needed it when we last got some data; check whether we still do:
        auto my_it = my_blink_hashes.find(i.first);
        if (my_it == my_blink_hashes.end() || i.second.first != my_it->second)
          r.heights.push_back(i.first);
        else
          i.second.second = false; // checksum is now equal, don't need it anymore
      }

      context.m_need_blink_sync = false;
      if (!r.heights.empty())
      {
        log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_BLOCK_BLINKS: requesting blink tx lists for {} blocks", r.heights.size());
        post_notify<NOTIFY_REQUEST_BLOCK_BLINKS>(r, context);
        log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting block blinks", cryptonote::get_protocol_state_string(context.m_state));
      }
    }

    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::log_connections()
  {
    std::stringstream ss;
    ss.precision(1);

    double down_sum = 0.0;
    double down_curr_sum = 0.0;
    double up_sum = 0.0;
    double up_curr_sum = 0.0;

    ss << std::setw(30) << std::left << "Remote Host"
      << std::setw(20) << "Peer id"
      << std::setw(30) << "Recv/Sent (inactive,sec)"
      << std::setw(25) << "State"
      << std::setw(20) << "Livetime(sec)"
      << std::setw(12) << "Down (kB/s)"
      << std::setw(14) << "Down(now)"
      << std::setw(10) << "Up (kB/s)"
      << std::setw(13) << "Up(now)"
      << "\n";

    m_p2p->for_each_connection([&](const connection_context& cntxt, nodetool::peerid_type peer_id)
    {
      bool local_ip = cntxt.m_remote_address.is_local();
      const auto now = std::chrono::steady_clock::now();
      seconds_f connection_time{now - cntxt.m_started};
      ss << std::setw(30) << std::left << std::string(cntxt.m_is_income ? " [INC]":"[OUT]") +
        cntxt.m_remote_address.str()
        << std::setw(20) << "{:016x}"_format(peer_id)
        << std::setw(30) << std::to_string(cntxt.m_recv_cnt) + "(" + std::to_string(tools::to_seconds(now - cntxt.m_last_recv)) + ")" +
                      "/" + std::to_string(cntxt.m_send_cnt) + "(" + std::to_string(tools::to_seconds(now - cntxt.m_last_send)) + ")"
        << std::setw(25) << get_protocol_state_string(cntxt.m_state)
        << std::setw(20) << std::to_string(tools::to_seconds(connection_time))
        << std::setw(12) << std::fixed << (connection_time < 1s ? 0.0 : cntxt.m_recv_cnt / connection_time.count() / 1024)
        << std::setw(14) << std::fixed << cntxt.m_current_speed_down / 1024
        << std::setw(10) << std::fixed << (connection_time < 1s ? 0.0 : cntxt.m_send_cnt / connection_time.count() / 1024)
        << std::setw(13) << std::fixed << cntxt.m_current_speed_up / 1024
        << (local_ip ? "[LAN]" : "")
        << std::left << (cntxt.m_remote_address.is_loopback() ? "[LOCALHOST]" : "") // 127.0.0.1
        << "\n";

      if (connection_time >= 1s)
      {
        down_sum += (cntxt.m_recv_cnt / connection_time.count() / 1024);
        up_sum += (cntxt.m_send_cnt / connection_time.count() / 1024);
      }

      down_curr_sum += (cntxt.m_current_speed_down / 1024);
      up_curr_sum += (cntxt.m_current_speed_up / 1024);

      return true;
    });
    ss << "\n"
      << std::setw(125) << " "
      << std::setw(12) << down_sum
      << std::setw(14) << down_curr_sum
      << std::setw(10) << up_sum
      << std::setw(13) << up_curr_sum
      << "\n";
    log::warning(logcat, "Connections:\n{}", ss.str());
  }
  //------------------------------------------------------------------------------------------------------------------------
  // Returns a list of connection_info objects describing each open p2p connection
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  std::list<connection_info> t_cryptonote_protocol_handler<t_core>::get_connections()
  {
    std::list<connection_info> connections;

    m_p2p->for_each_connection([&](const connection_context& cntxt, nodetool::peerid_type peer_id)
    {
      connection_info cnx;
      auto now = std::chrono::steady_clock::now();

      cnx.incoming = cntxt.m_is_income ? true : false;

      cnx.address = cntxt.m_remote_address.str();
      cnx.host = cntxt.m_remote_address.host_str();
      cnx.ip = "";
      cnx.port = "";
      if (cntxt.m_remote_address.get_type_id() == epee::net_utils::ipv4_network_address::get_type_id())
      {
        cnx.ip = cnx.host;
        cnx.port = std::to_string(cntxt.m_remote_address.as<epee::net_utils::ipv4_network_address>().port());
      }
      cnx.peer_id = "{:016x}"_format(peer_id);
      
      cnx.live_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - cntxt.m_started);
      cnx.recv_idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - std::max(cntxt.m_started, cntxt.m_last_recv));
      cnx.send_idle_time = std::chrono::duration_cast<std::chrono::milliseconds>(now - std::max(cntxt.m_started, cntxt.m_last_send));

      cnx.recv_count = cntxt.m_recv_cnt;
      cnx.send_count = cntxt.m_send_cnt;

      cnx.state = get_protocol_state_string(cntxt.m_state);

      cnx.localhost = cntxt.m_remote_address.is_loopback();
      cnx.local_ip = cntxt.m_remote_address.is_local();

      seconds_f connection_time{std::chrono::steady_clock::now() - cntxt.m_started};
      if (connection_time < 1s)
      {
        cnx.avg_download = 0;
        cnx.avg_upload = 0;
      }

      else
      {
        cnx.avg_download = cntxt.m_recv_cnt / connection_time.count();
        cnx.avg_upload = cntxt.m_send_cnt / connection_time.count();
      }

      cnx.current_download = cntxt.m_current_speed_down;
      cnx.current_upload = cntxt.m_current_speed_up;

      cnx.connection_id = tools::hex_guts(cntxt.m_connection_id);

      cnx.height = cntxt.m_remote_blockchain_height;
      cnx.pruning_seed = cntxt.m_pruning_seed;
      cnx.address_type = (uint8_t)cntxt.m_remote_address.get_type_id();

      connections.push_back(cnx);

      return true;
    });

    return connections;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::process_payload_sync_data(CORE_SYNC_DATA&& hshd, cryptonote_connection_context& context, bool is_initial)
  {
    if(context.m_state == cryptonote_connection_context::state_before_handshake && !is_initial)
      return true;

    if(context.m_state == cryptonote_connection_context::state_synchronizing)
      return true;

    // if the peer advertises a top block version, reject if it's not what it should be
    if (hshd.current_height > 0)
    {
      auto nettype = m_core.get_nettype();
      const auto version = get_network_version(nettype, hshd.current_height - 1);
      if (version != hshd.top_version)
      {
        if (version < hshd.top_version && version == get_network_version(nettype, m_core.blockchain.get_current_blockchain_height()))
          log::warning(logcat, fg(fmt::terminal_color::red), "{} peer claims higher version than we think ({} for {} instead of {}) 0 we may be forked from the network and a software upgrade may be needed", context, (unsigned)hshd.top_version, (hshd.current_height - 1), (unsigned)version);
        return false;
      }
    }

    // reject weird pruning schemes
    if (hshd.pruning_seed)
    {
      const uint32_t log_stripes = tools::get_pruning_log_stripes(hshd.pruning_seed);
      if (log_stripes != PRUNING_LOG_STRIPES || tools::get_pruning_stripe(hshd.pruning_seed) > (1u << log_stripes))
      {
        log::warning(logcat, "{} peer claim unexpected pruning seed {}, disconnecting", context, epee::string_tools::to_string_hex(hshd.pruning_seed));
        return false;
      }
    }

    context.m_remote_blockchain_height = hshd.current_height;
    context.m_pruning_seed = hshd.pruning_seed;
    if constexpr (PRUNING_DEBUG_SPOOF_SEED) {
      context.m_pruning_seed = tools::make_pruning_seed(1 + (context.m_remote_address.as<epee::net_utils::ipv4_network_address>().ip()) % (1 << PRUNING_LOG_STRIPES), PRUNING_LOG_STRIPES);
      log::info(logcat, "{}New connection posing as pruning seed {:08x}", context, context.m_pruning_seed);
    }

    // No chain synchronization over hidden networks (tor, i2p, etc.)
    if(context.m_remote_address.get_zone() != epee::net_utils::zone::public_)
    {
      context.m_state = cryptonote_connection_context::state_normal;
      return true;
    }

    auto curr_height = m_core.blockchain.get_current_blockchain_height();

    context.m_need_blink_sync = false;
    // Check for any blink txes being advertised that we don't know about
    if (is_hard_fork_at_least(m_core.get_nettype(), feature::BLINK, curr_height))
    {
      if (hshd.blink_blocks.size() != hshd.blink_hash.size())
      {
        log::warning(logcat, "{} peer sent illegal mismatched blink heights/hashes; disconnecting", context);
        return false;
      }
      else if (hshd.blink_blocks.size() > 1000)
      {
        log::warning(logcat, "{} peer sent too many post-checkpoint blink blocks; disconnecting", context);
        return false;
      }

      // Peer sends us HEIGHT -> HASH pairs, where the HASH is the xor'ed tx hashes of all blink
      // txes mined at the given HEIGHT.  If the HASH is different than our hash for the same height
      // *and* different than the last height the peer sent then we will request the blink txes for
      // that height.

      const uint64_t immutable_height = m_core.blockchain.get_immutable_height();
      // Delete any irrelevant heights > 0 (the mempool) and <= the immutable height
      context.m_blink_state.erase(context.m_blink_state.lower_bound(1), context.m_blink_state.lower_bound(immutable_height + 1));
      auto our_blink_hashes = m_core.mempool.get_blink_checksums();
      uint64_t last_height;
      log::debug(logcat, "Peer sent {} blink hashes", hshd.blink_blocks.size());
      for (size_t i = 0; i < hshd.blink_blocks.size(); i++) {
        auto &height = hshd.blink_blocks[i];
        if (i == 0 || height > last_height)
          last_height = height;
        else {
          log::warning(logcat, "{} peer sent blink tx heights out of order, which is not valid; disconnecting", context);
          return false;
        }

        if (height > 0 && (height < immutable_height || height >= curr_height))
        {
          // We're either past the immutable height (in which case we don't care about the blink
          // signatures), or we don't know about the advertised block yet (we'll get the blink info
          // when we get the block).  Skip it but don't disconnect because this isn't invalid.
          continue;
        }
        auto &hash = hshd.blink_hash[i];

        auto it = our_blink_hashes.find(height);
        if (it != our_blink_hashes.end() && it->second == hash)
        { // Matches our hash already, great
          context.m_blink_state.erase(height);
          continue;
        }

        auto ctx_it = context.m_blink_state.lower_bound(height);
        if (ctx_it == context.m_blink_state.end() || ctx_it->first != height) // Height not found in peer context
          context.m_blink_state.emplace_hint(ctx_it, height, std::make_pair(hash, true));
        else if (ctx_it->second.first != hash) // Hash changed, update and request
        {
          ctx_it->second.first = hash;
          ctx_it->second.second = true;
        }
        else
          continue;

        context.m_need_blink_sync = true;
      }

      if (context.m_need_blink_sync)
        log::debug(logcat, "{}Need to synchronized blink signatures", context);
    }

    uint64_t target = m_core.get_target_blockchain_height();
    if (target == 0)
      target = curr_height;

    bool have_block = m_core.blockchain.have_block(hshd.top_id);

    if (!have_block && hshd.current_height > target)
    {
      /* As I don't know if accessing hshd from core could be a good practice,
      I prefer pushing target height to the core at the same time it is pushed to the user.
      Nz. */
      int64_t diff = static_cast<int64_t>(hshd.current_height) - static_cast<int64_t>(curr_height);
      uint64_t abs_diff = std::abs(diff);
      std::string sync_msg = "{}Sync data returned a new top block candidate: {} -> {} [Your node is {} blocks ({} {})]\nSYNCHRONIZATION started"_format(
              context, curr_height, hshd.current_height, abs_diff, tools::get_human_readable_timespan(abs_diff*m_core.get_net_config().TARGET_BLOCK_TIME), (0 <= diff ? "behind" : "ahead"));
      if (is_initial)
        log::info(globallogcat, fg(fmt::terminal_color::cyan), "{}", sync_msg);
      else
        log::debug(globallogcat, "{}", sync_msg);

      m_period_start_time = m_sync_start_time = std::chrono::steady_clock::now();
      m_sync_start_height = curr_height;

      if (hshd.current_height >= curr_height + 5) // don't switch to unsafe mode just for a few blocks
      {
        m_core.safesyncmode(false);
      }
      if (m_core.get_target_blockchain_height() == 0) // only when sync starts
      {
        m_sync_timer = std::chrono::steady_clock::now();
        m_last_add_end_time = std::chrono::steady_clock::now();
        m_sync_spans_downloaded = 0;
        m_sync_old_spans_downloaded = 0;
        m_sync_bad_spans_downloaded = 0;
        m_sync_download_chain_size = 0;
        m_sync_download_objects_size = 0;
      }
      m_core.set_target_blockchain_height((hshd.current_height));
    }

    if (m_no_sync)
    {
      context.m_state = cryptonote_connection_context::state_normal;
      return true;
    }

    if(have_block)
    {
      context.m_state = cryptonote_connection_context::state_normal;
      if(is_initial  && hshd.current_height >= target && target == m_core.blockchain.get_current_blockchain_height())
        on_connection_synchronized();
    }
    else
    {
      context.m_state = cryptonote_connection_context::state_synchronizing;
    }

    if (context.m_need_blink_sync || context.m_state == cryptonote_connection_context::state_synchronizing)
    {
      log::debug(logcat, "{}Remote blockchain height: {}, id: {}", context, hshd.current_height, hshd.top_id);
      //let the socket to send response to handshake, but request callback, to let send request data after response
      log::debug(logcat, "requesting callback");
      ++context.m_callback_request_count;
      m_p2p->request_callback(context);
      log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting callback", cryptonote::get_protocol_state_string(context.m_state));
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::get_payload_sync_data(CORE_SYNC_DATA& hshd)
  {
    std::tie(hshd.current_height, hshd.top_id) = m_core.blockchain.get_tail_id();
    hshd.top_version = get_network_version(m_core.get_nettype(), hshd.current_height);
    hshd.cumulative_difficulty = m_core.blockchain.db().get_block_cumulative_difficulty(hshd.current_height);
    hshd.current_height +=1;
    hshd.pruning_seed = m_core.blockchain.get_blockchain_pruning_seed();
    auto our_blink_hashes = m_core.mempool.get_blink_checksums();
    hshd.blink_blocks.reserve(our_blink_hashes.size());
    hshd.blink_hash.reserve(our_blink_hashes.size());
    for (auto &h : our_blink_hashes)
    {
        hshd.blink_blocks.push_back(h.first);
        hshd.blink_hash.push_back(h.second);
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
    template<class t_core>
    bool t_cryptonote_protocol_handler<t_core>::get_payload_sync_data(std::string& data)
  {
    CORE_SYNC_DATA hsd{};
    get_payload_sync_data(hsd);
    epee::serialization::store_t_to_binary(hsd, data);
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_notify_new_fluffy_block(int command, NOTIFY_NEW_FLUFFY_BLOCK::request& arg, cryptonote_connection_context& context)
  {
    if(log::Cat("net.p2p,msg")->should_log(log::Level::info))
    {
      crypto::hash hash;
      cryptonote::block b;
      bool ret = cryptonote::parse_and_validate_block_from_blob(arg.b.block, b, &hash);
      if (ret)
        log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_NEW_FLUFFY_BLOCK {} (height {}, {} txes)", hash, arg.current_blockchain_height, arg.b.txs.size());
    }

    if(context.m_state != cryptonote_connection_context::state_normal)
      return 1;
    if(!is_synchronized() || m_no_sync) // can happen if a peer connection goes to normal but another thread still hasn't finished adding queued blocks
    {
      log::debug(logcat, "{}Received new block while syncing, ignored", context);
      return 1;
    }
    
    m_core.miner.pause();
      
    block new_block;
    if(parse_and_validate_block_from_blob(arg.b.block, new_block))
    {
      // This is a second notification, we must have asked for some missing tx
      if(!context.m_requested_objects.empty())
      {
        // What we asked for != to what we received ..
        if(context.m_requested_objects.size() != arg.b.txs.size())
        {
          log::error(logcat, "NOTIFY_NEW_FLUFFY_BLOCK -> request/response mismatch, block = {}, requested = {}, received = {}, dropping connection",
                  get_blob_hash(arg.b.block), context.m_requested_objects.size(), new_block.tx_hashes.size());
          drop_connection(context, false, false);
          m_core.miner.resume();
          return 1;
        }
      }      
      
      std::vector<std::string> have_tx;
      
      // Instead of requesting missing transactions by hash like BTC, 
      // we do it by index (thanks to a suggestion from moneromooo) because
      // we're way cooler .. and also because they're smaller than hashes.
      // 
      // Also, remember to pepper some whitespace changes around to bother
      // moneromooo ... only because I <3 him. 
      std::vector<uint64_t> need_tx_indices;
        
      transaction tx;
      crypto::hash tx_hash;

      for(auto& tx_blob: arg.b.txs)
      {
        if(parse_and_validate_tx_from_blob(tx_blob, tx))
        {
          try
          {
            if(!get_transaction_hash(tx, tx_hash))
            {
              log::info(logcat, "NOTIFY_NEW_FLUFFY_BLOCK: get_transaction_hash failed, dropping connection");
              drop_connection(context, false, false);
              m_core.miner.resume();
              return 1;
            }
          }
          catch(...)
          {
            log::info(logcat, "NOTIFY_NEW_FLUFFY_BLOCK: get_transaction_hash failed, exception thrown, dropping connection");
            drop_connection(context, false, false);
            m_core.miner.resume();
            return 1;
          }
          
          // hijacking m_requested objects in connection context to patch up
          // a possible DOS vector pointed out by @monero-moo where peers keep
          // sending (0...n-1) transactions.
          // If requested objects is not empty, then we must have asked for 
          // some missing transacionts, make sure that they're all there.
          //
          // Can I safely re-use this field? I think so, but someone check me!
          if(!context.m_requested_objects.empty()) 
          {
            auto req_tx_it = context.m_requested_objects.find(tx_hash);
            if(req_tx_it == context.m_requested_objects.end())
            {
              log::error(logcat, "Peer sent wrong transaction (NOTIFY_NEW_FLUFFY_BLOCK): transaction with id = {} wasn't requested, dropping connection", tx_hash);
              drop_connection(context, false, false);
              m_core.miner.resume();
              return 1;
            }
            
            context.m_requested_objects.erase(req_tx_it);
          }          
          
          // we might already have the tx that the peer
          // sent in our pool, so don't verify again..
          if(!m_core.mempool.have_tx(tx_hash))
          {
            log::debug(logcat, "Incoming tx {} not in pool, adding", tx_hash);
            cryptonote::tx_verification_context tvc{};
            if(!m_core.handle_incoming_tx(tx_blob, tvc, tx_pool_options::from_block()) || tvc.m_verifivation_failed)
            {
              log::info(logcat, "Block verification failed: transaction verification failed, dropping connection");
              drop_connection(context, false, false);
              m_core.miner.resume();
              return 1;
            }
            
            //
            // future todo: 
            // tx should only not be added to pool if verification failed, but
            // maybe in the future could not be added for other reasons 
            // according to monero-moo so keep track of these separately ..
            //
          }
        }
        else
        {
          log::error(logcat, "sent wrong tx: failed to parse and validate transaction: {}, dropping connection", oxenc::to_hex(tx_blob));
          drop_connection(context, false, false);
          m_core.miner.resume();
          return 1;
        }
      }
      
      // The initial size equality check could have been fooled if the sender
      // gave us the number of transactions we asked for, but not the right 
      // ones. This check make sure the transactions we asked for were the
      // ones we received.
      if(context.m_requested_objects.size())
      {
        log::error(logcat, "NOTIFY_NEW_FLUFFY_BLOCK: peer sent the number of transaction requested, but not the actual transactions requested, context.m_requested_objects.size() = {}, dropping connection", context.m_requested_objects.size());
        
        drop_connection(context, false, false);
        m_core.miner.resume();
        return 1;
      }      
      
      size_t tx_idx = 0;
      for(auto& tx_hash: new_block.tx_hashes)
      {
        std::string txblob;
        if(m_core.mempool.get_transaction(tx_hash, txblob))
        {
          have_tx.push_back(txblob);
        }
        else
        {
          std::vector<crypto::hash> tx_ids;
          std::vector<transaction> txes;
          std::unordered_set<crypto::hash> missing;
          tx_ids.push_back(tx_hash);
          if (m_core.blockchain.get_transactions(tx_ids, txes, &missing) && missing.empty())
          {
            if (txes.size() == 1)
            {
              have_tx.push_back(tx_to_blob(txes.front()));
            }
            else
            {
              log::error(logcat, "1 tx requested, none not found, but {} returned", txes.size());
              m_core.miner.resume();
              return 1;
            }
          }
          else
          {
            log::debug(logcat, "Tx {} not found in pool", tx_hash);
            need_tx_indices.push_back(tx_idx);
          }
        }
        
        ++tx_idx;
      }
        
      if(!need_tx_indices.empty()) // drats, we don't have everything..
      {
        // request non-mempool txs
        log::debug(logcat, "We are missing {} txes for this fluffy block", need_tx_indices.size());
        for (auto txidx: need_tx_indices)
          log::debug(logcat, "  tx {}", new_block.tx_hashes[txidx]);
        NOTIFY_REQUEST_FLUFFY_MISSING_TX::request missing_tx_req;
        missing_tx_req.block_hash = get_block_hash(new_block);
        missing_tx_req.current_blockchain_height = arg.current_blockchain_height;
        missing_tx_req.missing_tx_indices = std::move(need_tx_indices);
        
        m_core.miner.resume();
        log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_FLUFFY_MISSING_TX: missing_tx_indices.size()={}", missing_tx_req.missing_tx_indices.size());
        post_notify<NOTIFY_REQUEST_FLUFFY_MISSING_TX>(missing_tx_req, context);
      }
      else // whoo-hoo we've got em all ..
      {
        log::debug(logcat, "We have all needed txes for this fluffy block");

        block_complete_entry b = {};
        b.block                = arg.b.block;
        b.txs                  = have_tx;

        std::vector<block_complete_entry> blocks;
        blocks.push_back(b);


        std::vector<block> pblocks;
        if (!m_core.prepare_handle_incoming_blocks(blocks, pblocks))
        {
          log::warning(logcat, "Failure in prepare_handle_incoming_blocks");
          m_core.miner.resume();
          return 1;
        }

        block_verification_context bvc{};
        m_core.handle_incoming_block(arg.b.block, pblocks.empty() ? NULL : &pblocks[0], bvc, nullptr /*checkpoint*/); // got block from handle_notify_new_block
        if (!m_core.cleanup_handle_incoming_blocks(true))
        {
          log::warning(logcat, "Failure in cleanup_handle_incoming_blocks");
          m_core.miner.resume();
          return 1;
        }
        m_core.miner.resume();

        if( bvc.m_verifivation_failed )
        {
          log::warning(logcat, "Block verification failed, dropping connection");
          drop_connection(context, true, false);
          return 1;
        }
        if( bvc.m_added_to_main_chain )
        {
          //TODO: Add here announce protocol usage
          NOTIFY_NEW_FLUFFY_BLOCK::request reg_arg{};
          reg_arg.current_blockchain_height = arg.current_blockchain_height;
          reg_arg.b = b;
          relay_block(reg_arg, context);
        }
        else if( bvc.m_marked_as_orphaned )
        {
          context.m_needed_objects.clear();
          context.m_state = cryptonote_connection_context::state_synchronizing;
          NOTIFY_REQUEST_CHAIN::request r{};
          m_core.blockchain.get_short_chain_history(r.block_ids);
          log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_CHAIN: m_block_ids.size()={}", r.block_ids.size());
          post_notify<NOTIFY_REQUEST_CHAIN>(r, context);
          log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting chain", cryptonote::get_protocol_state_string(context.m_state));
        }            
      }
    } 
    else
    {
      log::error(logcat, "sent wrong block: failed to parse and validate block: {}, dropping connection", oxenc::to_hex(arg.b.block));
        
      m_core.miner.resume();
      drop_connection(context, false, false);
        
      return 1;     
    }
        
    return 1;
  }  
  //------------------------------------------------------------------------------------------------------------------------  
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_uptime_proof(int command, NOTIFY_BTENCODED_UPTIME_PROOF::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_BTENCODED_UPTIME_PROOF");
    // NOTE: Don't relay your own uptime proof, otherwise we have the following situation

    // Node1 sends uptime ->
    // Node2 receives uptime and relays it back to Node1 for acknowledgement ->
    // Node1 receives it, handle_uptime_proof returns true to acknowledge, Node1 tries to resend to the same peers again

    // Instead, if we receive our own uptime proof, then acknowledge but don't
    // send on. If the we are missing an uptime proof it will have been
    // submitted automatically by the daemon itself instead of
    // using my own proof relayed by other nodes.

    (void)context;
    bool my_uptime_proof_confirmation = false;

    if (m_core.handle_uptime_proof(arg, my_uptime_proof_confirmation))
    {
      if (!my_uptime_proof_confirmation)
      {
        // NOTE: The default exclude context contains the peer who sent us this
        // uptime proof, we want to ensure we relay it back so they know that the
        // peer they relayed to received their uptime and confirm it, so send in an
        // empty context so we don't omit the source peer from the relay back.
        cryptonote_connection_context empty_context = {};
        relay_uptime_proof(arg, empty_context);
      }
    }
    return 1;
  }

  //------------------------------------------------------------------------------------------------------------------------  
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_notify_new_service_node_vote(int command, NOTIFY_NEW_SERVICE_NODE_VOTE::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_NEW_SERVICE_NODE_VOTE ({} txes)", arg.votes.size());

    if(context.m_state != cryptonote_connection_context::state_normal)
      return 1;

    if(!is_synchronized() || m_no_sync)
    {
      log::debug(logcat, "{}Received new service node vote while syncing, ignored", context);
      return 1;
    }

    for(auto it = arg.votes.begin(); it != arg.votes.end();)
    {
      cryptonote::vote_verification_context vvc = {};
      m_core.add_service_node_vote(*it, vvc);

      if (vvc.m_verification_failed)
      {
        log::info(logcat, "Vote type: {}, verification failed, dropping connection", it->type);
        drop_connection(context, false /*add_fail*/, false /*flush_all_spans i.e. delete cached block data from this peer*/);
        return 1;
      }

      if (vvc.m_added_to_pool)
      {
        it++;
      }
      else
      {
        it = arg.votes.erase(it);
      }
    }

    if (arg.votes.size())
      relay_service_node_votes(arg, context);

    return 1;
  }

  //------------------------------------------------------------------------------------------------------------------------  
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_request_fluffy_missing_tx(int command, NOTIFY_REQUEST_FLUFFY_MISSING_TX::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_REQUEST_FLUFFY_MISSING_TX ({} txes), block hash {}", arg.missing_tx_indices.size(), arg.block_hash);
    
    std::vector<std::pair<std::string, block>> local_blocks;
    std::vector<std::string> local_txs;

    block b;
    if (!m_core.blockchain.get_block_by_hash(arg.block_hash, b))
    {
      log::error(logcat, "failed to find block: {}, dropping connection", arg.block_hash);
      drop_connection(context, false, false);
      return 1;
    }

    std::vector<crypto::hash> txids;
    NOTIFY_NEW_FLUFFY_BLOCK::request fluffy_response;
    fluffy_response.b.block = t_serializable_object_to_blob(b);
    fluffy_response.current_blockchain_height = arg.current_blockchain_height;

    // NOTE: Dupe index check
    {
      std::unordered_set<uint64_t> requested_index_set;
      requested_index_set.reserve(16); // typical maximum number of txs per block

      for (uint64_t requested_index : arg.missing_tx_indices)
      {
        if (!requested_index_set.insert(requested_index).second)
        {
          log::error(logcat, "Failed to handle request NOTIFY_REQUEST_FLUFFY_MISSING_TX, request is asking for the same tx index more than once, tx index = {}, block tx count {}, block_height = {}, dropping connection", requested_index, b.tx_hashes.size(), arg.current_blockchain_height);

          drop_connection(context, false, false);
          return 1;
        }
      }
    }

    for(auto& tx_idx: arg.missing_tx_indices)
    {
      if(tx_idx < b.tx_hashes.size())
      {
        log::debug(logcat, "  tx {}", b.tx_hashes[tx_idx]);
        txids.push_back(b.tx_hashes[tx_idx]);
      }
      else
      {
        log::error(logcat, "Failed to handle request NOTIFY_REQUEST_FLUFFY_MISSING_TX, request is asking for a tx whose index is out of bounds, tx index = {}, block tx count {}, block_height = {}, dropping connection ", tx_idx, b.tx_hashes.size(), arg.current_blockchain_height);
        
        drop_connection(context, false, false);
        return 1;
      }
    }

    std::vector<cryptonote::transaction> txs;
    std::unordered_set<crypto::hash> missed;
    if (!m_core.blockchain.get_transactions(txids, txs, &missed))
    {
      log::error(logcat, "Failed to handle request NOTIFY_REQUEST_FLUFFY_MISSING_TX, failed to get requested transactions");
      drop_connection(context, false, false);
      return 1;
    }
    if (!missed.empty() || txs.size() != txids.size())
    {
      log::error(logcat, "Failed to handle request NOTIFY_REQUEST_FLUFFY_MISSING_TX, {} requested transactions not found, dropping connection", missed.size());
      drop_connection(context, false, false);
      return 1;
    }

    for(auto& tx: txs)
    {
      fluffy_response.b.txs.push_back(t_serializable_object_to_blob(tx));
    }

    log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_RESPONSE_FLUFFY_MISSING_TX: txs.size()={}, rsp.current_blockchain_height={}", fluffy_response.b.txs.size(), fluffy_response.current_blockchain_height);
           
    post_notify<NOTIFY_NEW_FLUFFY_BLOCK>(fluffy_response, context);    
    return 1;        
  }
  //------------------------------------------------------------------------------------------------------------------------  
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_notify_new_transactions(int command, NOTIFY_NEW_TRANSACTIONS::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_NEW_TRANSACTIONS ({} txes w/ {} blinks)", arg.txs.size(), arg.blinks.size());
    for (const auto &blob: arg.txs)
      if(logcat->should_log(log::Level::info))
      {
        cryptonote::transaction tx;
        crypto::hash hash;
        bool ret = cryptonote::parse_and_validate_tx_from_blob(blob, tx, hash);
        if (ret)
          log::info(log::Cat("net.p2p.msg"), "Including transaction {}", hash);
      }

    if(context.m_state != cryptonote_connection_context::state_normal)
      return 1;

    // while syncing, core will lock for a long time, so we ignore those txes as they aren't really
    // needed anyway, and avoid a long block before replying.  (Not for .requested though: in that
    // case we specifically asked for these txes).
    bool syncing = !is_synchronized();
    if((syncing && !arg.requested) || m_no_sync)
    {
      log::debug(logcat, "{}Received new tx while syncing, ignored", context);
      return 1;
    }

    bool bad_blinks = false;
    auto parsed_blinks = m_core.parse_incoming_blinks(arg.blinks);
    auto &blinks = parsed_blinks.first;
    std::unordered_set<crypto::hash> blink_approved;
    for (auto &b : blinks)
      if (b->approved())
        blink_approved.insert(b->get_txhash());
      else
        bad_blinks = true;

    bool all_okay;
    {
      auto lock = m_core.incoming_tx_lock();

      const auto txpool_opts = tx_pool_options::from_peer();
      auto parsed_txs = m_core.parse_incoming_txs(arg.txs, txpool_opts);
      for (auto &txi : parsed_txs)
        if (blink_approved.count(txi.tx_hash))
          txi.approved_blink = true;

      uint64_t blink_rollback_height = 0;
      all_okay = m_core.handle_parsed_txs(parsed_txs, txpool_opts, &blink_rollback_height);

      // Even if !all_okay (which means we want to drop the connection) we may still have added some
      // incoming txs and so still need to finish handling/relaying them
      std::vector<std::string> newtxs;
      newtxs.reserve(arg.txs.size());
      auto &unknown_txs = parsed_blinks.second;
      for (size_t i = 0; i < arg.txs.size(); ++i)
      {
        if (parsed_txs[i].tvc.m_should_be_relayed)
          newtxs.push_back(std::move(arg.txs[i]));

        if (parsed_txs[i].tvc.m_added_to_pool || parsed_txs[i].already_have)
          unknown_txs.erase(parsed_txs[i].tx_hash);
      }
      arg.txs = std::move(newtxs);

      // Attempt to add any blinks signatures we received, but with unknown txs removed (where unknown
      // means previously unknown and didn't just get added to the mempool).  (Don't bother worrying
      // about approved because add_blinks() already does that).
      blinks.erase(std::remove_if(blinks.begin(), blinks.end(), [&](const auto &b) { return unknown_txs.count(b->get_txhash()) > 0; }), blinks.end());
      m_core.add_blinks(blinks);

      if (blink_rollback_height > 0)
      {
        log::debug(logcat, "after handling parsed txes we need to rollback to height: {}", blink_rollback_height);
        // We need to clear back to and including block at height blink_rollback_height (so that the
        // new blockchain "height", i.e. of current top_block_height+1, is blink_rollback_height).
        auto &blockchain = m_core.blockchain;
        auto locks = tools::unique_locks(blockchain, m_core.mempool);

        uint64_t height    = blockchain.get_current_blockchain_height(),
                 immutable = blockchain.get_immutable_height();
        if (immutable >= blink_rollback_height)
        {
          log::warning(logcat, "blink rollback specified a block at or before the immutable height; we can only roll back to the immutable height.");
          blink_rollback_height = immutable + 1;
        }
        if (blink_rollback_height < height)
          m_core.blockchain.blink_rollback(blink_rollback_height);
        else
          log::debug(logcat, "Nothing to roll back");
      }
    }

    // If this is a response to a request for txes that we sent (.requested) then don't relay this
    // on to our peers because they probably already have it: we just missed it somehow.
    if(arg.txs.size() && !arg.requested)
    {
      //TODO: add announce usage here
      relay_transactions(arg, context);
    }

    // If we're still syncing (which implies this was a requested tx list) then it's quite possible
    // we got sent some mempool or future block blinks that we can't handle yet, which is fine (and
    // so don't drop the connection).
    if (!syncing && (!all_okay || bad_blinks))
    {
      log::debug(logcat, "{} verification(s) failed, dropping connection", (!all_okay && bad_blinks ? "Tx and Blink" : !all_okay ? "Tx" : "Blink"));
      drop_connection(context, false, false);
    }

    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_request_get_blocks(int command, NOTIFY_REQUEST_GET_BLOCKS::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_REQUEST_GET_BLOCKS ({} blocks)", arg.blocks.size());
    if (arg.blocks.size() > CURRENCY_PROTOCOL_MAX_OBJECT_REQUEST_COUNT)
    {
      log::error(logcat, "Requested blocks count is too big ({}) expected not more than {}", arg.blocks.size(), CURRENCY_PROTOCOL_MAX_OBJECT_REQUEST_COUNT);
      drop_connection(context, false, false);
      return 1;
    }

    NOTIFY_RESPONSE_GET_BLOCKS::request rsp;
    if(!m_core.blockchain.handle_get_blocks(arg, rsp))
    {
      log::error(logcat, "failed to handle request NOTIFY_REQUEST_GET_BLOCKS, dropping connection");
      drop_connection(context, false, false);
      return 1;
    }
    log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_RESPONSE_GET_BLOCKS: blocks.size()={}, rsp.m_current_blockchain_height={}, missed_ids.size()={}", rsp.blocks.size(), rsp.current_blockchain_height, rsp.missed_ids.size());
    post_notify<NOTIFY_RESPONSE_GET_BLOCKS>(rsp, context);
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------


  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_response_get_blocks(int command, NOTIFY_RESPONSE_GET_BLOCKS::request& arg, cryptonote_connection_context& context)
  {
    log::debug(logcat, "Received NOTIFY_RESPONSE_GET_BLOCKS ({} blocks)", arg.blocks.size());
    log::debug(log::Cat("net.p2p.msg"), "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "received blocks", cryptonote::get_protocol_state_string(context.m_state));

    auto request_time = *context.m_last_request_time;
    context.m_last_request_time.reset();

    // calculate size of request
    size_t blocks_size = 0, others_size = 0;

    for (const auto &element : arg.blocks) {
      blocks_size += element.block.size();
      for (const auto &tx : element.txs)
        blocks_size += tx.size();
      others_size += element.checkpoint.size();
      for (const auto &blink : element.blinks)
        others_size += sizeof(blink.tx_hash) + sizeof(blink.height) + blink.quorum.size() + blink.position.size() + blink.signature.size() * sizeof(crypto::signature);
    }

    size_t size = blocks_size + others_size;
    for (const auto &element : arg.missed_ids)
      size += element.size();

    size += sizeof(arg.current_blockchain_height);
    {
      std::lock_guard lock{m_buffer_mutex};
      m_avg_buffer.push_back(size);
    }
    ++m_sync_spans_downloaded;
    m_sync_download_objects_size += size;
    log::debug(logcat, "{} downloaded {} bytes worth of blocks", context, size);

    if(context.m_last_response_height > arg.current_blockchain_height)
    {
      log::error(logcat, "sent wrong NOTIFY_GET_BLOCKS: arg.m_current_blockchain_height={} < m_last_response_height={}, dropping connection", arg.current_blockchain_height, context.m_last_response_height);
      drop_connection(context, false, false);
      ++m_sync_bad_spans_downloaded;
      return 1;
    }

    context.m_remote_blockchain_height = arg.current_blockchain_height;
    if (context.m_remote_blockchain_height > m_core.get_target_blockchain_height())
      m_core.set_target_blockchain_height(context.m_remote_blockchain_height);

    std::vector<crypto::hash> block_hashes;
    block_hashes.reserve(arg.blocks.size());
    const auto now = std::chrono::steady_clock::now();
    uint64_t start_height = std::numeric_limits<uint64_t>::max();
    cryptonote::block b;
    for(const block_complete_entry& block_entry: arg.blocks)
    {
      if (m_stopping)
      {
        return 1;
      }

      crypto::hash block_hash;
      if(!parse_and_validate_block_from_blob(block_entry.block, b, block_hash))
      {
        log::error(logcat, "sent wrong block: failed to parse and validate block: {}, dropping connection", oxenc::to_hex(block_entry.block));
        drop_connection(context, false, false);
        ++m_sync_bad_spans_downloaded;
        return 1;
      }

      if (start_height == std::numeric_limits<uint64_t>::max())
        start_height = b.get_height();

      auto req_it = context.m_requested_objects.find(block_hash);
      if(req_it == context.m_requested_objects.end())
      {
        log::error(logcat, "sent wrong NOTIFY_RESPONSE_GET_BLOCKS: block with id={} wasn't requested, dropping connection",
                get_blob_hash(block_entry.block));
        drop_connection(context, false, false);
        ++m_sync_bad_spans_downloaded;
        return 1;
      }
      if(b.tx_hashes.size() != block_entry.txs.size())
      {
        log::error(logcat, "sent wrong NOTIFY_RESPONSE_GET_BLOCKS: block with id={}, tx_hashes.size()= {} mismatch with block_complete_entry.m_txs.size()= {}, dropping connection",
                get_blob_hash(block_entry.block), b.tx_hashes.size(), block_entry.txs.size());
        drop_connection(context, false, false);
        ++m_sync_bad_spans_downloaded;
        return 1;
      }

      context.m_requested_objects.erase(req_it);
      block_hashes.push_back(block_hash);
    }

    if(!context.m_requested_objects.empty())
    {
      log::error(logcat, "{}returned not all requested objects (context.m_requested_objects.size()={}), dropping connection", context, context.m_requested_objects.size());
      drop_connection(context, false, false);
      ++m_sync_bad_spans_downloaded;
      return 1;
    }

    {
      log::debug(globallogcat, fg(fmt::terminal_color::yellow), "{} Got NEW BLOCKS inside of {}: size: {}, blocks: {} - {} (pruning seed {})", context, __FUNCTION__, arg.blocks.size(), start_height, (start_height + arg.blocks.size() - 1), epee::string_tools::to_string_hex(context.m_pruning_seed));

      // add that new span to the block queue
      seconds_f dt = now - request_time;
      const double rate = size / dt.count();
      log::debug(logcat, "{} adding span: {} at height {}, {} seconds, {} kB/s, size now {} MB", context, arg.blocks.size(), start_height, dt.count(), (rate/1024), (m_block_queue.get_data_size() + blocks_size) / 1048576.f);
      m_block_queue.add_blocks(start_height, arg.blocks, context.m_connection_id, rate, blocks_size);

      const crypto::hash last_block_hash = cryptonote::get_block_hash(b);
      context.m_last_known_hash = last_block_hash;

      if (!m_core.get_test_drop_download() || !m_core.get_test_drop_download_height()) { // DISCARD BLOCKS for testing
        return 1;
      }
    }

    try_add_next_blocks(context);
    return 1;
  }

  // Get an estimate for the remaining sync time from given current to target blockchain height, in seconds
  template<class t_core>
  uint64_t t_cryptonote_protocol_handler<t_core>::get_estimated_remaining_sync_seconds(uint64_t current_blockchain_height, uint64_t target_blockchain_height)
  {
    // The average sync speed varies so much, even averaged over quite long time periods like 10 minutes,
    // that using some sliding window would be difficult to implement without often leading to bad estimates.
    // The simplest strategy - always average sync speed over the maximum available interval i.e. since sync
    // started at all (from "m_sync_start_time" and "m_sync_start_height") - gives already useful results
    // and seems to be quite robust. Some quite special cases like "Internet connection suddenly becoming
    // much faster after syncing already a long time, and staying fast" are not well supported however.

    if (target_blockchain_height <= current_blockchain_height)
    {
      // Syncing stuck, or other special circumstance: Avoid errors, simply give back 0
      return 0;
    }

    auto now = std::chrono::steady_clock::now();
    seconds_f sync_time = now - m_sync_start_time;
    cryptonote::network_type nettype = m_core.get_nettype();

    // Don't simply use remaining number of blocks for the estimate but "sync weight" as provided by
    // "cumulative_block_sync_weight" which knows about strongly varying Monero mainnet block sizes
    uint64_t synced_weight = tools::cumulative_block_sync_weight(nettype, m_sync_start_height, current_blockchain_height - m_sync_start_height);
    uint64_t remaining_weight = tools::cumulative_block_sync_weight(nettype, current_blockchain_height, target_blockchain_height - current_blockchain_height);
    return (uint64_t)((sync_time.count() / synced_weight) * remaining_weight);
  }

  // Return a textual remaining sync time estimate, or the empty string if waiting period not yet over
  template<class t_core>
  std::string t_cryptonote_protocol_handler<t_core>::get_periodic_sync_estimate(uint64_t current_blockchain_height, uint64_t target_blockchain_height)
  {
    std::string text;
    const auto now = std::chrono::steady_clock::now();
    auto period_sync_time = now - m_period_start_time;
    if (period_sync_time > 30s)
    {
      // Period is over, time to report another estimate
      uint64_t remaining_seconds = get_estimated_remaining_sync_seconds(current_blockchain_height, target_blockchain_height);
      text = tools::get_human_readable_timespan(std::chrono::seconds(remaining_seconds));

      // Start the new period
      m_period_start_time = now;
    }
    return text;
  }

  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::try_add_next_blocks(cryptonote_connection_context& context)
  {
    bool force_next_span = false;

    {
      // We try to lock the sync lock. If we can, it means no other thread is
      // currently adding blocks, so we do that for as long as we can from the
      // block queue. Then, we go back to download.
      const std::unique_lock sync{m_sync_lock, std::try_to_lock};
      if (!sync)
      {
        log::debug(logcat, "{}Failed to lock m_sync_lock, going back to download", context);
        goto skip;
      }
      log::debug(logcat, "{} lock m_sync_lock, adding blocks to chain...", context);
      log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "adding blocks", cryptonote::get_protocol_state_string(context.m_state));

      {
        m_core.miner.pause();
        bool starting = true;
        OXEN_DEFER
        {
          m_core.miner.resume();
          if (!starting) m_last_add_end_time = std::chrono::steady_clock::now();
        };

        while (1)
        {
          const uint64_t previous_height = m_core.blockchain.get_current_blockchain_height();
          uint64_t start_height;
          std::vector<cryptonote::block_complete_entry> blocks;
          boost::uuids::uuid span_connection_id;
          if (!m_block_queue.get_next_span(start_height, blocks, span_connection_id))
          {
            log::debug(logcat, "{} no next span found, going back to download", context);
            break;
          }

          if (blocks.empty())
          {
            log::error(logcat, "{}Next span has no blocks", context);
            m_block_queue.remove_spans(span_connection_id, start_height);
            continue;
          }

          log::debug(logcat, "{} next span in the queue has blocks {}-{}, we need {}", context, start_height, (start_height + blocks.size() - 1), previous_height);

          block new_block;
          crypto::hash last_block_hash;
          if (!parse_and_validate_block_from_blob(blocks.back().block, new_block, last_block_hash))
          {
            log::error(logcat, "{}Failed to parse block, but it should already have been parsed", context);
            m_block_queue.remove_spans(span_connection_id, start_height);
            continue;
          }
          if (m_core.blockchain.have_block(last_block_hash))
          {
            const uint64_t subchain_height = start_height + blocks.size();
            log::debug(logcat, "{}{} - {}, blockchain height {}", context, "These are old blocks, ignoring: blocks ", start_height, (subchain_height-1), m_core.blockchain.get_current_blockchain_height());
            m_block_queue.remove_spans(span_connection_id, start_height);
            ++m_sync_old_spans_downloaded;
            continue;
          }
          if (!parse_and_validate_block_from_blob(blocks.front().block, new_block))
          {
            log::error(logcat, "{}Failed to parse block, but it should already have been parsed", context);
            m_block_queue.remove_spans(span_connection_id, start_height);
            continue;
          }

          bool parent_known = m_core.blockchain.have_block(new_block.prev_id);
          if (!parent_known)
          {
            // it could be:
            //  - later in the current chain
            //  - later in an alt chain
            //  - orphan
            // if it was requested, then it'll be resolved later, otherwise it's an orphan
            bool parent_requested = m_block_queue.requested(new_block.prev_id);
            if (!parent_requested)
            {
              // we might be able to ask for that block directly, as we now can request out of order,
              // otherwise we continue out of order, unless this block is the one we need, in which
              // case we request block hashes, though it might be safer to disconnect ?
              if (start_height > previous_height)
              {
                if (should_drop_connection(context, get_next_needed_pruning_stripe().first))
                {
                  log::debug(logcat, "{}Got block with unknown parent which was not requested, but peer does not have that block - dropping connection", context);
                  if (!context.m_is_income)
                    m_p2p->add_used_stripe_peer(context);
                  drop_connection(context, false, true);
                  return 1;
                }
                log::debug(logcat, "{}Got block with unknown parent which was not requested, but peer does not have that block - back to download", context);

                goto skip;
              }

              // this can happen if a connection was sicced onto a late span, if it did not have those blocks,
              // since we don't know that at the sic time
              log::error(logcat, "Got block with unknown parent which was not requested - querying block hashes");
              m_block_queue.remove_spans(span_connection_id, start_height);
              context.m_needed_objects.clear();
              context.m_last_response_height = 0;
              goto skip;
            }

            // parent was requested, so we wait for it to be retrieved
            log::debug(logcat, "{} parent was requested, we'll get back to it", context);
            break;
          }

          const auto start = std::chrono::steady_clock::now();

          if (starting)
          {
            starting = false;
            auto elapsed = std::chrono::steady_clock::now() - m_last_add_end_time;
            log::debug(logcat, "Restarting adding block after idle for {} seconds", tools::friendly_duration(elapsed));
          }

          std::vector<block> pblocks;
          if (!m_core.prepare_handle_incoming_blocks(blocks, pblocks))
          {
            log::error(logcat, "Failure in prepare_handle_incoming_blocks");
            return 1;
          }

          {
            bool remove_spans = false;
            OXEN_DEFER
            {
              if (!m_core.cleanup_handle_incoming_blocks())
                log::warning(logcat, "Failure in cleanup_handle_incoming_blocks");

              // in case the peer had dropped beforehand, remove the span anyway so other threads can wake up and get it
              if (remove_spans)
                m_block_queue.remove_spans(span_connection_id, start_height);
            };

            if (!pblocks.empty() && pblocks.size() != blocks.size())
            {
              log::error(logcat, "Internal error: blocks.size() != block_entry.txs.size()");
              return 1;
            }

            auto block_process_time_full = 0ns;
            auto transactions_process_time_full = 0ns;
            size_t num_txs = 0, blockidx = 0;
            for(const block_complete_entry& block_entry: blocks)
            {
              if (m_stopping)
                return 1;

              // process transactions
              auto transactions_process_start = std::chrono::steady_clock::now();
              num_txs += block_entry.txs.size();
              auto parsed_txs = m_core.handle_incoming_txs(block_entry.txs, tx_pool_options::from_block());

              for (size_t i = 0; i < parsed_txs.size(); ++i)
              {
                if (parsed_txs[i].tvc.m_verifivation_failed)
                {
                  if (!m_p2p->for_connection(span_connection_id, [&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool{
                    cryptonote::transaction tx;
                    parse_and_validate_tx_from_blob(block_entry.txs[i], tx); // must succeed if we got here
                    log::error(logcat, "transaction verification failed on NOTIFY_RESPONSE_GET_BLOCKS, tx_id = {}, dropping connection",
                            cryptonote::get_transaction_hash(tx));
                    drop_connection(context, false, true);
                    return 1;
                  }))
                    log::error(logcat, "span connection id not found");

                  remove_spans = true;
                  return 1;
                }
              }
              transactions_process_time_full += std::chrono::steady_clock::now() - transactions_process_start;

              //
              // NOTE: Checkpoint parsing
              //
              checkpoint_t checkpoint_allocated_on_stack_;
              checkpoint_t *checkpoint = nullptr;
              if (block_entry.checkpoint.size())
              {
                // TODO(doyle): It's wasteful to have to parse the checkpoint to
                // figure out the height when at some point during the syncing
                // step we know exactly what height the block entries are for

                if (!t_serializable_object_from_blob(checkpoint_allocated_on_stack_, block_entry.checkpoint))
                {
                  log::error(logcat, "Checkpoint blob available but failed to parse");
                  return false;
                }

                checkpoint = &checkpoint_allocated_on_stack_;
              }

              // process block

              auto block_process_start = std::chrono::steady_clock::now();
              block_verification_context bvc{};

              m_core.handle_incoming_block(block_entry.block, pblocks.empty() ? NULL : &pblocks[blockidx], bvc, checkpoint, false); // <--- process block

              if (bvc.m_verifivation_failed || bvc.m_marked_as_orphaned)
              {
                if (!m_p2p->for_connection(span_connection_id, [&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool{
                    if (bvc.m_verifivation_failed)
                        log::info(logcat, "Block verification failed, dropping connection");
                    else
                        log::info(logcat, "Block received at sync phase was marked as orphaned, dropping connection");

                  drop_connection(context, true, true);
                  return 1;
                }))
                  log::error(logcat, "span connection id not found");

                remove_spans = true;
                return 1;
              }

              block_process_time_full += std::chrono::steady_clock::now() - block_process_start;
              ++blockidx;

            } // each download block

            remove_spans = true;
            log::debug(logcat, "{}Block process time ({} blocks, {} txs): {} ({}/{})",
                context,
                blocks.size(),
                num_txs,
                tools::friendly_duration(block_process_time_full + transactions_process_time_full),
                tools::friendly_duration(transactions_process_time_full),
                tools::friendly_duration(block_process_time_full));
          }

          const uint64_t current_blockchain_height = m_core.blockchain.get_current_blockchain_height();
          if (current_blockchain_height > previous_height)
          {
            const uint64_t target_blockchain_height = m_core.get_target_blockchain_height();
            seconds_f dt = std::chrono::steady_clock::now() - start;
            std::string progress_message = "";
            if (current_blockchain_height < target_blockchain_height)
            {
              uint64_t completion_percent = (current_blockchain_height * 100 / target_blockchain_height);
              if (completion_percent == 100) // never show 100% if not actually up to date
                completion_percent = 99;
              progress_message = " (" + std::to_string(completion_percent) + "%, "
                  + std::to_string(target_blockchain_height - current_blockchain_height) + " left";
              std::string time_message = get_periodic_sync_estimate(current_blockchain_height, target_blockchain_height);
              if (!time_message.empty())
              {
                uint64_t total_blocks_to_sync = target_blockchain_height - m_sync_start_height;
                uint64_t total_blocks_synced = current_blockchain_height - m_sync_start_height;
                progress_message += ", " + std::to_string(total_blocks_synced * 100 / total_blocks_to_sync) + "% of total synced";
                progress_message += ", estimated " + time_message + " left";
              }
              progress_message += ")";
            }
            const uint32_t previous_stripe = tools::get_pruning_stripe(previous_height, target_blockchain_height, PRUNING_LOG_STRIPES);
            const uint32_t current_stripe = tools::get_pruning_stripe(current_blockchain_height, target_blockchain_height, PRUNING_LOG_STRIPES);
            std::string timing_message;
            if (logcat->should_log(log::Level::info))
                timing_message =
                        " ({:.3f} sec, {:.3f} blocks/sec), {:.3f} MB queued in {} spans, stripe {} -> {}{}"_format(
                                dt.count(),
                                (current_blockchain_height - previous_height) / dt.count(),
                                m_block_queue.get_data_size() / 1000000.f,
                                m_block_queue.get_num_filled_spans(),
                                previous_stripe,
                                current_stripe,
                                logcat->should_log(log::Level::debug)
                                        ? ": {}"_format(m_block_queue.get_overview(
                                                  current_blockchain_height))
                                        : "");
            else {
                auto speed = (current_blockchain_height - previous_height) / dt.count();
                timing_message = " ({} blocks/sec)"_format(
                        speed >= 100 ? "{:.0f}"_format(speed) : "{:.1f}"_format(speed));
            }
            log::info(globallogcat, fg(fmt::terminal_color::yellow), "Synced {}/{} {} {}", current_blockchain_height, target_blockchain_height, progress_message, timing_message);
            if (previous_stripe != current_stripe)
              notify_new_stripe(context, current_stripe);
          }
        }
      }

      log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "stopping adding blocks", cryptonote::get_protocol_state_string(context.m_state));

      if (should_download_next_span(context, false))
      {
        force_next_span = true;
      }
      else if (should_drop_connection(context, get_next_needed_pruning_stripe().first))
      {
        if (!context.m_is_income)
        {
          m_p2p->add_used_stripe_peer(context);
          drop_connection(context, false, false);
        }
        return 1;
      }
    }

skip:
    if (!request_missing_objects(context, true, force_next_span))
    {
      log::error(logcat, "Failed to request missing objects, dropping connection");
      drop_connection(context, false, false);
      return 1;
    }
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::notify_new_stripe(cryptonote_connection_context& cntxt, uint32_t stripe)
  {
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool
    {
      if (cntxt.m_connection_id == context.m_connection_id)
        return true;
      if (context.m_state == cryptonote_connection_context::state_normal)
      {
        const uint32_t peer_stripe = tools::get_pruning_stripe(context.m_pruning_seed);
        if (stripe && peer_stripe && peer_stripe != stripe)
          return true;
        context.m_state = cryptonote_connection_context::state_synchronizing;
        log::debug(logcat, "requesting callback");
        ++context.m_callback_request_count;
        m_p2p->request_callback(context);
        log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting callback", cryptonote::get_protocol_state_string(context.m_state));
      }
      return true;
    });
  }
  //------------------------------------------------------------------------------------------------------------------------
  // Tells the other end to send us the given txes (typically with attached blink data) as if they
  // are new transactions.
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_request_get_txs(int command, NOTIFY_REQUEST_GET_TXS::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_REQUEST_GET_TXS ({} txs)", arg.txs.size());

    if (arg.txs.size() > CURRENCY_PROTOCOL_MAX_TXS_REQUEST_COUNT)
    {
      log::error(logcat, "Requested txs count is too big ({}) expected not mroe than {}", arg.txs.size(), CURRENCY_PROTOCOL_MAX_TXS_REQUEST_COUNT);
      drop_connection(context, false, false);
      return 1;
    }

    NOTIFY_NEW_TRANSACTIONS::request rsp;
    rsp.requested = true;
    if(!m_core.blockchain.handle_get_txs(arg, rsp))
    {
      log::error(logcat, "failed to handle request NOTIFY_REQUEST_GET_TXS, dropping connection");
      drop_connection(context, false, false);
      return 1;
    }
    log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_NEW_TRANSACTIONS: requested=true, txs[{}], blinks[{}]", rsp.txs.size(), rsp.blinks.size());
    post_notify<NOTIFY_NEW_TRANSACTIONS>(rsp, context);
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::on_idle()
  {
    m_idle_peer_kicker.do_call([this] { return kick_idle_peers(); });
    m_standby_checker.do_call([this] { return check_standby_peers(); });
    m_sync_search_checker.do_call([this] { return update_sync_search(); });
    return m_core.on_idle();
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::kick_idle_peers()
  {
    log::trace(logcat, "Checking for idle peers...");
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool
    {
      if (context.m_state == cryptonote_connection_context::state_synchronizing && context.m_last_request_time)
      {
        const auto dt = std::chrono::steady_clock::now() - *context.m_last_request_time;
        if (dt > IDLE_PEER_KICK_TIME)
        {
          log::info(logcat, "{} kicking idle peer, last update {} seconds ago", context, seconds_f{dt}.count());
          log::debug(logcat, "requesting callback");
          context.m_last_request_time.reset();
          context.m_state = cryptonote_connection_context::state_standby; // we'll go back to adding, then (if we can't), download
          ++context.m_callback_request_count;
          m_p2p->request_callback(context);
        }
      }
      return true;
    });
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::update_sync_search()
  {
    const uint64_t target = m_core.get_target_blockchain_height();
    const uint64_t height = m_core.blockchain.get_current_blockchain_height();
    if (target > height) // if we're not synced yet, don't do it
      return true;

    log::trace(logcat, "Checking for outgoing syncing peers...");
    unsigned n_syncing = 0, n_synced = 0;
    boost::uuids::uuid last_synced_peer_id(boost::uuids::nil_uuid());
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool
    {
      if (!peer_id || context.m_is_income) // only consider connected outgoing peers
        return true;
      if (context.m_state == cryptonote_connection_context::state_synchronizing)
        ++n_syncing;
      if (context.m_state == cryptonote_connection_context::state_normal)
      {
        ++n_synced;
        if (!context.m_anchor)
          last_synced_peer_id = context.m_connection_id;
      }
      return true;
    });
    log::trace(logcat, "{} syncing, {} synced", n_syncing, n_synced);

    // if we're at max out peers, and not enough are syncing
    if (n_synced + n_syncing >= m_max_out_peers && n_syncing < p2p::DEFAULT_SYNC_SEARCH_CONNECTIONS_COUNT && last_synced_peer_id != boost::uuids::nil_uuid())
    {
      if (!m_p2p->for_connection(last_synced_peer_id, [&](cryptonote_connection_context& ctx, nodetool::peerid_type peer_id)->bool{
        log::debug(logcat, "{}dropping synced peer, {} syncing, {} synced", ctx, n_syncing, n_synced);
        drop_connection(ctx, false, false);
        return true;
      }))
        log::debug(logcat, "Failed to find peer we wanted to drop");
    }

    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::check_standby_peers()
  {
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool
    {
      if (context.m_state == cryptonote_connection_context::state_standby)
      {
        log::debug(logcat, "requesting callback");
        ++context.m_callback_request_count;
        m_p2p->request_callback(context);
      }
      return true;
    });
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_request_chain(int command, NOTIFY_REQUEST_CHAIN::request& arg, cryptonote_connection_context& context)
  {
    log::debug(logcat, "Received NOTIFY_REQUEST_CHAIN ({} blocks)", arg.block_ids.size());
    NOTIFY_RESPONSE_CHAIN_ENTRY::request r;
    if(!m_core.blockchain.find_blockchain_supplement(arg.block_ids, r))
    {
      log::error(logcat, "Failed to handle NOTIFY_REQUEST_CHAIN.");
      drop_connection(context, false, false);
      return 1;
    }
    log::debug(logcat, "-->>NOTIFY_RESPONSE_CHAIN_ENTRY: m_start_height={}, m_total_height={}, m_block_ids.size()={}", r.start_height, r.total_height, r.m_block_ids.size());
    post_notify<NOTIFY_RESPONSE_CHAIN_ENTRY>(r, context);
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::should_download_next_span(cryptonote_connection_context& context, bool standby)
  {
    std::chrono::steady_clock::time_point request_time;
    boost::uuids::uuid connection_id;
    std::pair<uint64_t, uint64_t> span;
    bool filled;

    const uint64_t blockchain_height = m_core.blockchain.get_current_blockchain_height();
    if (context.m_remote_blockchain_height <= blockchain_height)
      return false;
    const auto now = std::chrono::steady_clock::now();
    const bool has_next_block = tools::has_unpruned_block(blockchain_height, context.m_remote_blockchain_height, context.m_pruning_seed);
    if (has_next_block)
    {
      if (!m_block_queue.has_next_span(blockchain_height, filled, request_time, connection_id))
      {
        log::debug(logcat, "{} we should download it as no peer reserved it", context);
        return true;
      }
      if (!filled)
      {
        const auto dt = now - request_time;
        if (dt >= REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD)
        {
          log::debug(logcat, "{} we should download it as it's not been received yet after {}", context, seconds_f{dt}.count());
          return true;
        }

        // in standby, be ready to double download early since we're idling anyway
        // let the fastest peer trigger first
        long threshold;
        const double dl_speed = context.m_max_speed_down;
        if (standby && dt >= REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY && dl_speed > 0)
        {
          bool download = false;
          if (m_p2p->for_connection(connection_id, [&](cryptonote_connection_context& ctx, nodetool::peerid_type peer_id)->bool{
            const auto last_activity = std::min(now - ctx.m_last_recv, dt);
            const bool stalled = last_activity > LAST_ACTIVITY_STALL_THRESHOLD;
            if (stalled)
            {
              log::debug(logcat, "{} we should download it as the downloading peer is stalling for {} seconds", context, seconds_f{last_activity}.count());
              download = true;
              return true;
            }

            // estimate the standby peer can give us 80% of its max speed
            // and let it download if that speed is > N times as fast as the current one
            // N starts at 10 after REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY,
            // decreases to 1.25 at REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD,
            // so that at times goes without the download being done, a retry becomes easier
            const float max_multiplier = 10.f;
            const float min_multiplier = 1.25f;
            float multiplier = max_multiplier;
            if (dt >= REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY)
            {
              multiplier = max_multiplier - (
                      (max_multiplier - min_multiplier)
                      *
                      (
                        seconds_f{               dt                     - REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY}
                          /
                        seconds_f{REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD - REQUEST_NEXT_SCHEDULED_SPAN_THRESHOLD_STANDBY}
                      )
                    );
              multiplier = std::min(max_multiplier, std::max(min_multiplier, multiplier));
            }
            if (dl_speed * .8f > ctx.m_current_speed_down * multiplier)
            {
              log::debug(logcat, "{} we should download it as we are substantially faster ({} vs {}, multiplier {} after {} seconds)", context, dl_speed, ctx.m_current_speed_down, multiplier, seconds_f{dt}.count());
              download = true;
              return true;
            }
            return true;
          }))
          {
            if (download)
              return true;
          }
          else
          {
            log::warning(logcat, "{} we should download it as the downloading peer is unexpectedly not known to us", context);
            return true;
          }
        }
      }
    }

    return false;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::should_drop_connection(cryptonote_connection_context& context, uint32_t next_stripe)
  {
    if (context.m_anchor)
    {
      log::debug(logcat, "{}This is an anchor peer, not dropping", context);
      return false;
    }
    if (context.m_pruning_seed == 0)
    {
      log::debug(logcat, "{}This peer is not striped, not dropping", context);
      return false;
    }

    const uint32_t peer_stripe = tools::get_pruning_stripe(context.m_pruning_seed);
    if (next_stripe == peer_stripe)
    {
      log::debug(logcat, "{}This peer has needed stripe {}, not dropping", context, peer_stripe);
      return false;
    }

    if (!context.m_needed_objects.empty())
    {
      const uint64_t next_available_block_height = context.m_last_response_height - context.m_needed_objects.size() + 1;
      if (tools::has_unpruned_block(next_available_block_height, context.m_remote_blockchain_height, context.m_pruning_seed))
      {
        log::debug(logcat, "{}This peer has unpruned next block at height {}, not dropping", context, next_available_block_height);
        return false;
      }
    }

    if (next_stripe > 0)
    {
      unsigned int n_out_peers = 0, n_peers_on_next_stripe = 0;
      m_p2p->for_each_connection([&](cryptonote_connection_context& ctx, nodetool::peerid_type peer_id)->bool{
        if (!ctx.m_is_income)
          ++n_out_peers;
        if (ctx.m_state >= cryptonote_connection_context::state_synchronizing && tools::get_pruning_stripe(ctx.m_pruning_seed) == next_stripe)
          ++n_peers_on_next_stripe;
        return true;
      });
      const uint32_t distance = (peer_stripe + (1<<PRUNING_LOG_STRIPES) - next_stripe) % (1<<PRUNING_LOG_STRIPES);
      if ((n_out_peers >= m_max_out_peers && n_peers_on_next_stripe == 0) || (distance > 1 && n_peers_on_next_stripe <= 2) || distance > 2)
      {
        log::debug(logcat, "{}we want seed {}, and either {} is at max out peers ({}) or distance {} from {} to {} is too large and we have only {} peers on next seed, dropping connection to make space", context, next_stripe, n_out_peers, m_max_out_peers.load(), distance, next_stripe, peer_stripe, n_peers_on_next_stripe);
        return true;
      }
    }
    log::debug(logcat, "{}End of checks, not dropping", context);
    return false;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::skip_unneeded_hashes(cryptonote_connection_context& context, bool check_block_queue) const
  {
    // take out blocks we already have
    size_t skip = 0;
    while (skip < context.m_needed_objects.size() && (m_core.blockchain.have_block(context.m_needed_objects[skip]) || (check_block_queue && m_block_queue.have(context.m_needed_objects[skip]))))
    {
      // if we're popping the last hash, record it so we can ask again from that hash,
      // this prevents never being able to progress on peers we get old hash lists from
      if (skip + 1 == context.m_needed_objects.size())
        context.m_last_known_hash = context.m_needed_objects[skip];
      ++skip;
    }
    if (skip > 0)
    {
      log::debug(logcat, "{}skipping {}/{} blocks", context, skip, context.m_needed_objects.size());
      context.m_needed_objects = std::vector<crypto::hash>(context.m_needed_objects.begin() + skip, context.m_needed_objects.end());
    }
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::request_missing_objects(cryptonote_connection_context& context, bool check_having_blocks, bool force_next_span)
  {
    // flush stale spans
    std::set<boost::uuids::uuid> live_connections;
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool{
      live_connections.insert(context.m_connection_id);
      return true;
    });
    m_block_queue.flush_stale_spans(live_connections);

    // if we don't need to get next span, and the block queue is full enough, wait a bit
    bool start_from_current_chain = false;
    if (!force_next_span)
    {
      do
      {
        size_t nspans = m_block_queue.get_num_filled_spans();
        size_t size = m_block_queue.get_data_size();
        const uint64_t bc_height = m_core.blockchain.get_current_blockchain_height();
        const auto next_needed_pruning_stripe = get_next_needed_pruning_stripe();
        const uint32_t add_stripe = tools::get_pruning_stripe(bc_height, context.m_remote_blockchain_height, PRUNING_LOG_STRIPES);
        const uint32_t peer_stripe = tools::get_pruning_stripe(context.m_pruning_seed);
        const size_t block_queue_size_threshold = m_block_download_max_size ? m_block_download_max_size : BLOCK_QUEUE_SIZE_THRESHOLD;
        bool queue_proceed = nspans < BLOCK_QUEUE_NSPANS_THRESHOLD || size < block_queue_size_threshold;
        // get rid of blocks we already requested, or already have
        skip_unneeded_hashes(context, true);
        uint64_t next_needed_height = m_block_queue.get_next_needed_height(bc_height);
        uint64_t next_block_height;
        if (context.m_needed_objects.empty())
          next_block_height = next_needed_height;
        else
          next_block_height = context.m_last_response_height - context.m_needed_objects.size() + 1;
        bool stripe_proceed_main = (add_stripe == 0 || peer_stripe == 0 || add_stripe == peer_stripe) && (next_block_height < bc_height + BLOCK_QUEUE_FORCE_DOWNLOAD_NEAR_BLOCKS || next_needed_height < bc_height + BLOCK_QUEUE_FORCE_DOWNLOAD_NEAR_BLOCKS);
        bool stripe_proceed_secondary = tools::has_unpruned_block(next_block_height, context.m_remote_blockchain_height, context.m_pruning_seed);
        bool proceed = stripe_proceed_main || (queue_proceed && stripe_proceed_secondary);
        if (!stripe_proceed_main && !stripe_proceed_secondary && should_drop_connection(context, tools::get_pruning_stripe(next_block_height, context.m_remote_blockchain_height, PRUNING_LOG_STRIPES)))
        {
          if (!context.m_is_income)
            m_p2p->add_used_stripe_peer(context);
          return false; // drop outgoing connections
        }

        log::debug(logcat, "{}proceed {} (queue {}, stripe {}/{}), {}-{} needed, bc add stripe {}, we have {}), bc_height {}", context, proceed, queue_proceed, stripe_proceed_main, stripe_proceed_secondary, next_needed_pruning_stripe.first, next_needed_pruning_stripe.second, add_stripe, peer_stripe, bc_height);
        log::debug(logcat, "{}  - next_block_height {}, seed {}, next_needed_height {}", context, next_block_height, epee::string_tools::to_string_hex(context.m_pruning_seed), next_needed_height);
        log::debug(logcat, "{}  - last_response_height {}, m_needed_objects size {}", context, context.m_last_response_height, context.m_needed_objects.size());

        // if we're waiting for next span, try to get it before unblocking threads below,
        // or a runaway downloading of future spans might happen
        if (stripe_proceed_main && should_download_next_span(context, true))
        {
          log::debug(logcat, "{} we should try for that next span too, we think we could get it faster, resuming", context);
          force_next_span = true;
          log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "resuming", cryptonote::get_protocol_state_string(context.m_state));
          break;
        }

        if (proceed)
        {
          if (context.m_state != cryptonote_connection_context::state_standby)
          {
            log::debug(logcat, "{}{} and {}, resuming", context, "Block queue is ", nspans, size);
            log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "resuming", cryptonote::get_protocol_state_string(context.m_state));
          }
          break;
        }

        // this one triggers if all threads are in standby, which should not happen,
        // but happened at least once, so we unblock at least one thread if so
        std::unique_lock sync{m_sync_lock, std::try_to_lock};
        if (sync)
        {
          bool filled = false;
          std::chrono::steady_clock::time_point time;
          boost::uuids::uuid connection_id;
          if (m_block_queue.has_next_span(m_core.blockchain.get_current_blockchain_height(), filled, time, connection_id) && filled)
          {
            log::debug(logcat, "{}No other thread is adding blocks, and next span needed is ready, resuming", context);
            log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "resuming", cryptonote::get_protocol_state_string(context.m_state));
            context.m_state = cryptonote_connection_context::state_standby;
            ++context.m_callback_request_count;
            m_p2p->request_callback(context);
            return true;
          }
          else
          {
            sync.unlock();

            // if this has gone on for too long, drop incoming connection to guard against some wedge state
            if (!context.m_is_income)
            {
              auto ns = std::chrono::steady_clock::now() - m_last_add_end_time;
              if (ns >= DROP_ON_SYNC_WEDGE_THRESHOLD)
              {
                log::debug(logcat, "{}Block addition seems to have wedged, dropping connection", context);
                return false;
              }
            }
          }
        }

        if (context.m_state != cryptonote_connection_context::state_standby)
        {
          if (!queue_proceed)
            log::debug(logcat, "{}{} and {}, pausing", context, "Block queue is ", nspans, size);
          else if (!stripe_proceed_main && !stripe_proceed_secondary)
            log::debug(logcat, "{}We do not have the stripe required to download another block, pausing", context);
          context.m_state = cryptonote_connection_context::state_standby;
          log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "pausing", cryptonote::get_protocol_state_string(context.m_state));
        }

        return true;
      } while(0);
      context.m_state = cryptonote_connection_context::state_synchronizing;
    }

    log::debug(logcat, "{} request_missing_objects: check {}, force_next_span {}, m_needed_objects {} lrh {}, chain {}, pruning seed {}", context, check_having_blocks, force_next_span, context.m_needed_objects.size(), context.m_last_response_height, m_core.blockchain.get_current_blockchain_height(), epee::string_tools::to_string_hex(context.m_pruning_seed));
    if(context.m_needed_objects.size() || force_next_span)
    {
      //we know objects that we need, request this objects
      NOTIFY_REQUEST_GET_BLOCKS::request req;
      bool is_next = false;
      size_t count = 0;
      const size_t count_limit = m_core.get_block_sync_size(m_core.blockchain.get_current_blockchain_height());
      std::pair<uint64_t, uint64_t> span = std::make_pair(0, 0);
      if (force_next_span)
      {
        if (span.second == 0)
        {
          std::vector<crypto::hash> hashes;
          boost::uuids::uuid span_connection_id;
          span = m_block_queue.get_next_span_if_scheduled(hashes, span_connection_id);
          if (span.second > 0)
          {
            is_next = true;
            for (const auto &hash: hashes)
            {
              req.blocks.push_back(hash);
              context.m_requested_objects.insert(hash);
            }
            m_block_queue.reset_next_span_time();
          }
        }
      }
      if (span.second == 0)
      {
        log::debug(logcat, "{} span size is 0", context);
        if (context.m_last_response_height + 1 < context.m_needed_objects.size())
        {
          log::error(logcat, "{} ERROR: inconsistent context: lrh {}, nos {}", context, context.m_last_response_height, context.m_needed_objects.size());
          context.m_needed_objects.clear();
          context.m_last_response_height = 0;
          goto skip;
        }
        skip_unneeded_hashes(context, false);

        const uint64_t first_block_height = context.m_last_response_height - context.m_needed_objects.size() + 1;
        span = m_block_queue.reserve_span(first_block_height, context.m_last_response_height, count_limit, context.m_connection_id, context.m_pruning_seed, context.m_remote_blockchain_height, context.m_needed_objects);
        log::debug(logcat, "{} span from {}: {}/{}", context, first_block_height, span.first, span.second);
        if (span.second > 0)
        {
          const uint32_t stripe = tools::get_pruning_stripe(span.first, context.m_remote_blockchain_height, PRUNING_LOG_STRIPES);
          if (context.m_pruning_seed && stripe != tools::get_pruning_stripe(context.m_pruning_seed))
          {
            log::debug(logcat, "{} starting early on next seed ({}  with stripe {}, context seed {})", context, span.first, stripe, epee::string_tools::to_string_hex(context.m_pruning_seed));
          }
        }
      }
      if (span.second == 0 && !force_next_span)
      {
        log::debug(logcat, "{} still no span reserved, we may be in the corner case of next span scheduled and everything else scheduled/filled", context);
        std::vector<crypto::hash> hashes;
        boost::uuids::uuid span_connection_id;
        span = m_block_queue.get_next_span_if_scheduled(hashes, span_connection_id);
        if (span.second > 0 && !tools::has_unpruned_block(span.first, context.m_remote_blockchain_height, context.m_pruning_seed))
          span = std::make_pair(0, 0);
        if (span.second > 0)
        {
          is_next = true;
          for (const auto &hash: hashes)
          {
            req.blocks.push_back(hash);
            ++count;
            context.m_requested_objects.insert(hash);
            // that's atrocious O(n) wise, but this is rare
            auto i = std::find(context.m_needed_objects.begin(), context.m_needed_objects.end(), hash);
            if (i != context.m_needed_objects.end())
              context.m_needed_objects.erase(i);
          }
        }
      }
      log::debug(logcat, "{} span: {}/{} ({} - {})", context, span.first, span.second, span.first, (span.first + span.second - 1));
      if (span.second > 0)
      {
        if (!is_next)
        {
          const uint64_t first_context_block_height = context.m_last_response_height - context.m_needed_objects.size() + 1;
          uint64_t skip = span.first - first_context_block_height;
          if (skip > context.m_needed_objects.size())
          {
            log::error(logcat, "ERROR: skip {}, m_needed_objects {}, first_context_block_height{}", skip, context.m_needed_objects.size(), first_context_block_height);
            return false;
          }
          if (skip > 0)
            context.m_needed_objects = std::vector<crypto::hash>(context.m_needed_objects.begin() + skip, context.m_needed_objects.end());
          if (context.m_needed_objects.size() < span.second)
          {
            log::error(logcat, "ERROR: span {}/{}, m_needed_objects {}", span.first, span.second, context.m_needed_objects.size());
            return false;
          }

          for (size_t n = 0; n < span.second; ++n)
          {
            req.blocks.push_back(context.m_needed_objects[n]);
            ++count;
            context.m_requested_objects.insert(context.m_needed_objects[n]);
          }
          context.m_needed_objects = std::vector<crypto::hash>(context.m_needed_objects.begin() + span.second, context.m_needed_objects.end());
        }

        context.m_last_request_time = std::chrono::steady_clock::now();
        log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_GET_OBJECTS: blocks.size()={}, requested blocks count={} / {} from {}, first hash {}", req.blocks.size(), count, count_limit, span.first, req.blocks.front());

        post_notify<NOTIFY_REQUEST_GET_BLOCKS>(req, context);
        log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting objects", cryptonote::get_protocol_state_string(context.m_state));
        return true;
      }

      // if we're still around, we might be at a point where the peer is pruned, so we could either
      // drop it to make space for other peers, or ask for a span further down the line
      const uint32_t next_stripe = get_next_needed_pruning_stripe().first;
      const uint32_t peer_stripe = tools::get_pruning_stripe(context.m_pruning_seed);
      if (next_stripe && peer_stripe && next_stripe != peer_stripe)
      {
        // at this point, we have to either close the connection, or start getting blocks past the
        // current point, or become dormant
        log::debug(logcat, "{}this peer is pruned at seed {}, next stripe needed is {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), next_stripe);
        if (!context.m_is_income)
        {
          if (should_drop_connection(context, next_stripe))
          {
            m_p2p->add_used_stripe_peer(context);
            return false; // drop outgoing connections
          }
        }
        // we'll get back stuck waiting for the go ahead
        context.m_state = cryptonote_connection_context::state_normal;
        log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "Nothing to do for now, switching to normal state", cryptonote::get_protocol_state_string(context.m_state));
        return true;
      }
    }

skip:
    context.m_needed_objects.clear();

    // we might have been called from the "received chain entry" handler, and end up
    // here because we can't use any of those blocks (maybe because all of them are
    // actually already requested). In this case, if we can add blocks instead, do so
    if (m_core.blockchain.get_current_blockchain_height() < m_core.get_target_blockchain_height())
    {
      const std::unique_lock sync{m_sync_lock, std::try_to_lock};
      if (sync)
      {
        uint64_t start_height;
        std::vector<cryptonote::block_complete_entry> blocks;
        boost::uuids::uuid span_connection_id;
        bool filled = false;
        if (m_block_queue.get_next_span(start_height, blocks, span_connection_id, filled) && filled)
        {
          log::debug(logcat, "{}No other thread is adding blocks, resuming", context);
          log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "will try to add blocks next", cryptonote::get_protocol_state_string(context.m_state));
          context.m_state = cryptonote_connection_context::state_standby;
          ++context.m_callback_request_count;
          m_p2p->request_callback(context);
          return true;
        }
      }
    }

    if(context.m_last_response_height < context.m_remote_blockchain_height-1)
    {//we have to fetch more objects ids, request blockchain entry

      NOTIFY_REQUEST_CHAIN::request r{};
      m_core.blockchain.get_short_chain_history(r.block_ids);
      CHECK_AND_ASSERT_MES(!r.block_ids.empty(), false, "Short chain history is empty");

      if (!start_from_current_chain)
      {
        // we'll want to start off from where we are on that peer, which may not be added yet
        if (context.m_last_known_hash && r.block_ids.front() != context.m_last_known_hash)
          r.block_ids.push_front(context.m_last_known_hash);
      }

      context.m_last_request_time = std::chrono::steady_clock::now();
      log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_CHAIN: m_block_ids.size()={}, start_from_current_chain {}", r.block_ids.size(), start_from_current_chain);
      post_notify<NOTIFY_REQUEST_CHAIN>(r, context);
      log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting chain", cryptonote::get_protocol_state_string(context.m_state));
    }else
    {
      CHECK_AND_ASSERT_MES(context.m_last_response_height == context.m_remote_blockchain_height-1
                           && !context.m_needed_objects.size()
                           && !context.m_requested_objects.size(), false,
                           "request_missing_blocks final condition failed!"
                           "\nm_last_response_height={}"
                           "\nm_remote_blockchain_height="
                           "\nm_needed_objects.size()="
                           "\nm_requested_objects.size()="
                           "\non connection [{}]",
                           context.m_last_response_height,
                           context.m_remote_blockchain_height,
                           context.m_needed_objects.size(),
                           context.m_requested_objects.size(),
                           epee::net_utils::print_connection_context_short(context));

      context.m_state = cryptonote_connection_context::state_normal;
      if (context.m_remote_blockchain_height >= m_core.get_target_blockchain_height())
      {
        if (m_core.blockchain.get_current_blockchain_height() >= m_core.get_target_blockchain_height())
        {
          log::info(globallogcat, fg(fmt::terminal_color::green), "SYNCHRONIZED OK");
          on_connection_synchronized();
        }
      }
      else
      {
        log::info(logcat, "{} we've reached this peer's blockchain height", context);
      }
    }
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::on_connection_synchronized()
  {
    bool val_expected = false;
    uint64_t current_blockchain_height = m_core.blockchain.get_current_blockchain_height();
    if(!m_core.blockchain.is_within_compiled_block_hash_area(current_blockchain_height) && m_synchronized.compare_exchange_strong(val_expected, true))
    {
      if ((current_blockchain_height > m_sync_start_height) && (m_sync_spans_downloaded > 0))
      {
        uint64_t synced_blocks = current_blockchain_height - m_sync_start_height;
        // Report only after syncing an "interesting" number of blocks:
        if (synced_blocks > 20)
        {
          auto synced_seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - m_sync_start_time);
          if (synced_seconds == 0s)
            synced_seconds = 1s;
          float blocks_per_second = synced_blocks / (float)synced_seconds.count();
          log::info(globallogcat, fg(fmt::terminal_color::yellow), "Synced {} blocks in {} ({} blocks per second)", synced_blocks, tools::get_human_readable_timespan(synced_seconds), blocks_per_second);
        }
      }
      log::info(globallogcat, fg(fmt::terminal_color::yellow) | fmt::emphasis::bold, R"(
**********************************************************************
You are now synchronized with the network.
**********************************************************************)");
      if (logcat->should_log(log::Level::info))
      {
        const std::chrono::duration<double> sync_time{std::chrono::steady_clock::now() - m_sync_timer};
        log::info(logcat, fg(fmt::terminal_color::yellow), "Sync time: {:.1f} min, {:.1f} + {:.1f} MB downloaded, {:.2f}% old spans, {:.2f}% bad spans",
            sync_time.count()/60.0,
            m_sync_download_objects_size / 1000.0 / 1000.0,
            m_sync_download_chain_size / 1000.0 / 1000.0,
            100.0 * m_sync_old_spans_downloaded / m_sync_spans_downloaded,
            100.0 * m_sync_bad_spans_downloaded / m_sync_spans_downloaded);
      }
      m_core.on_synchronized();
    }
    m_core.safesyncmode(true);
    m_p2p->clear_used_stripe_peers();
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  size_t t_cryptonote_protocol_handler<t_core>::get_synchronizing_connections_count()
  {
    size_t count = 0;
    m_p2p->for_each_connection([&](cryptonote_connection_context& context, nodetool::peerid_type peer_id)->bool{
      if(context.m_state == cryptonote_connection_context::state_synchronizing)
        ++count;
      return true;
    });
    return count;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_response_chain_entry(int command, NOTIFY_RESPONSE_CHAIN_ENTRY::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_RESPONSE_CHAIN_ENTRY: m_block_ids.size()={}, m_start_height={}, m_total_height={}", arg.m_block_ids.size(), arg.start_height, arg.total_height);
    log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "received chain", cryptonote::get_protocol_state_string(context.m_state));

    context.m_last_request_time.reset();

    m_sync_download_chain_size += arg.m_block_ids.size() * sizeof(crypto::hash);

    if(!arg.m_block_ids.size())
    {
      log::error(logcat, "sent empty m_block_ids, dropping connection");
      drop_connection(context, true, false);
      return 1;
    }
    if (arg.total_height < arg.m_block_ids.size() || arg.start_height > arg.total_height - arg.m_block_ids.size())
    {
      log::error(logcat, "sent invalid start/nblocks/height, dropping connection");
      drop_connection(context, true, false);
      return 1;
    }
    log::debug(logcat, "{}first block hash {}, last {}", context, arg.m_block_ids.front(), arg.m_block_ids.back());

    if (arg.total_height >= MAX_BLOCK_NUMBER || arg.m_block_ids.size() >= MAX_BLOCK_NUMBER)
    {
      log::error(logcat, "sent wrong NOTIFY_RESPONSE_CHAIN_ENTRY, with total_height={} and block_ids={}", arg.total_height, arg.m_block_ids.size());
      drop_connection(context, false, false);
      return 1;
    }
    context.m_remote_blockchain_height = arg.total_height;
    context.m_last_response_height = arg.start_height + arg.m_block_ids.size()-1;
    if(context.m_last_response_height > context.m_remote_blockchain_height)
    {
      log::error(logcat, "sent wrong NOTIFY_RESPONSE_CHAIN_ENTRY, with m_total_height={}, m_start_height= {}, m_block_ids.size()={}", arg.total_height, arg.start_height, arg.m_block_ids.size());
      drop_connection(context, false, false);
      return 1;
    }

    uint64_t n_use_blocks = m_core.blockchain.prevalidate_block_hashes(arg.start_height, arg.m_block_ids);
    if (n_use_blocks + HASH_OF_HASHES_STEP <= arg.m_block_ids.size())
    {
      log::error(logcat, "Most blocks are invalid, dropping connection");
      drop_connection(context, true, false);
      return 1;
    }

    context.m_needed_objects.clear();
    uint64_t added = 0;
    for(auto& bl_id: arg.m_block_ids)
    {
      context.m_needed_objects.push_back(bl_id);
      if (++added == n_use_blocks)
        break;
    }
    context.m_last_response_height -= arg.m_block_ids.size() - n_use_blocks;

    if (!request_missing_objects(context, false))
    {
      log::error(logcat, "Failed to request missing objects, dropping connection");
      drop_connection(context, false, false);
      return 1;
    }

    if (arg.total_height > m_core.get_target_blockchain_height())
      m_core.set_target_blockchain_height(arg.total_height);

    return 1;
  }

  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_request_block_blinks(int command, NOTIFY_REQUEST_BLOCK_BLINKS::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_REQUEST_BLOCK_BLINKS: heights.size()={}", arg.heights.size());
    NOTIFY_RESPONSE_BLOCK_BLINKS::request r;

    r.txs = m_core.mempool.get_mined_blinks({arg.heights.begin(), arg.heights.end()});

    log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_RESPONSE_BLOCK_BLINKS: txs.size()={}", r.txs.size());
    post_notify<NOTIFY_RESPONSE_BLOCK_BLINKS>(r, context);
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  int t_cryptonote_protocol_handler<t_core>::handle_response_block_blinks(int command, NOTIFY_RESPONSE_BLOCK_BLINKS::request& arg, cryptonote_connection_context& context)
  {
    log::info(log::Cat("net.p2p.msg"), "Received NOTIFY_RESPONSE_BLOCK_BLINKS: txs.size()={}", arg.txs.size());

    m_core.mempool.keep_missing_blinks(arg.txs);
    if (arg.txs.empty())
    {
      log::debug(logcat, "NOTIFY_RESPONSE_BLOCKS_BLINKS included only blink txes we already knew about");
      return 1;
    }

    NOTIFY_REQUEST_GET_TXS::request req;
    while (!arg.txs.empty())
    {
      if (arg.txs.size() <= CURRENCY_PROTOCOL_MAX_TXS_REQUEST_COUNT)
        req.txs = std::move(arg.txs);
      else
      {
        req.txs = {arg.txs.end() - CURRENCY_PROTOCOL_MAX_TXS_REQUEST_COUNT, arg.txs.end()};
        arg.txs.resize(arg.txs.size() - CURRENCY_PROTOCOL_MAX_TXS_REQUEST_COUNT);
      }

      log::info(log::Cat("net.p2p.msg"), "-->>NOTIFY_REQUEST_GET_TXS: requesting for tx & blink data, txs.size()={}", req.txs.size());
      post_notify<NOTIFY_REQUEST_GET_TXS>(req, context);
    }
    log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "requesting missing blink txs", cryptonote::get_protocol_state_string(context.m_state));
    return 1;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::relay_block(NOTIFY_NEW_FLUFFY_BLOCK::request& arg, cryptonote_connection_context& exclude_context)
  {
    // sort peers between fluffy ones and others
    std::vector<std::pair<epee::net_utils::zone, boost::uuids::uuid>> fluffyConnections;
    m_p2p->for_each_connection([&exclude_context, &fluffyConnections](connection_context& context, nodetool::peerid_type peer_id)
    {
      if (peer_id && exclude_context.m_connection_id != context.m_connection_id && context.m_remote_address.get_zone() == epee::net_utils::zone::public_)
      {
        log::debug(logcat, "{}PEER FLUFFY BLOCKS - RELAYING THIN/COMPACT WHATEVER BLOCK", context);
        fluffyConnections.push_back({context.m_remote_address.get_zone(), context.m_connection_id});
      }
      return true;
    });

    std::string fluffyBlob;
    if (arg.b.txs.size())
    {
      epee::serialization::store_t_to_binary(arg, fluffyBlob);
    }
    else
    {
      // NOTE: We should never ideally hit this case. If we do, some developer
      // at the calling site passed in the full block information

      // relay_block is only meant to send the header, tx blobs should be
      // requested subsequently in handle notify fluffy transactions
      log::debug(logcat, "relay_block called with argument that contains TX blobs, this is the non-expected case");
      NOTIFY_NEW_FLUFFY_BLOCK::request arg_without_tx_blobs = {};
      arg_without_tx_blobs.current_blockchain_height        = arg.current_blockchain_height;
      arg_without_tx_blobs.b.block                          = arg.b.block;
      epee::serialization::store_t_to_binary(arg_without_tx_blobs, fluffyBlob);
    }

    m_p2p->relay_notify_to_list(NOTIFY_NEW_FLUFFY_BLOCK::ID, epee::strspan<uint8_t>(fluffyBlob), std::move(fluffyConnections));
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::relay_uptime_proof(NOTIFY_BTENCODED_UPTIME_PROOF::request& arg, cryptonote_connection_context& exclude_context)
  {
    bool result = relay_to_synchronized_peers<NOTIFY_BTENCODED_UPTIME_PROOF>(arg, exclude_context);
    return result;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::relay_service_node_votes(NOTIFY_NEW_SERVICE_NODE_VOTE::request& arg, cryptonote_connection_context& exclude_context)
  {
    bool result = relay_to_synchronized_peers<NOTIFY_NEW_SERVICE_NODE_VOTE>(arg, exclude_context);
    if (result)
      m_core.set_service_node_votes_relayed(arg.votes);
    return result;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::relay_transactions(NOTIFY_NEW_TRANSACTIONS::request& arg, cryptonote_connection_context& exclude_context)
  {
    // no check for success, so tell core they're relayed unconditionally and snag a copy of the
    // hash so that we can look up any associated blink data we should include.
    std::vector<crypto::hash> relayed_txes;
    relayed_txes.reserve(arg.txs.size());
    for (auto &tx_blob : arg.txs)
    {
      if (auto hash = m_core.on_transaction_relayed(tx_blob))
        relayed_txes.push_back(hash);
    }

    // Rebuild arg.blinks from blink data that we have because we don't necessarily have the same
    // blink data that got sent to us (we may have additional blink info, or may have rejected some
    // of the incoming blink data).
    arg.blinks.clear();
    if (is_hard_fork_at_least(m_core.get_nettype(), feature::BLINK, m_core.blockchain.get_current_blockchain_height()))
    {
      auto &pool = m_core.mempool;
      auto lock = pool.blink_shared_lock();
      for (auto &hash : relayed_txes)
      {
        if (auto blink = pool.get_blink(hash))
        {
          arg.blinks.emplace_back();
          auto l = blink->shared_lock();
          blink->fill_serialization_data(arg.blinks.back());
        }
      }
    }

    // no check for success, so tell core they're relayed unconditionally
    m_p2p->send_txs(std::move(arg.txs), exclude_context.m_remote_address.get_zone(), exclude_context.m_connection_id, m_core.pad_transactions());
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  std::string t_cryptonote_protocol_handler<t_core>::get_peers_overview() const
  {
    std::ostringstream ss;
    const auto now = std::chrono::steady_clock::now();
    m_p2p->for_each_connection([&](const connection_context &ctx, nodetool::peerid_type peer_id) {
      const uint32_t stripe = tools::get_pruning_stripe(ctx.m_pruning_seed);
      char state_char = cryptonote::get_protocol_state_char(ctx.m_state);
      ss << stripe + state_char;
      if (ctx.m_last_request_time)
        ss << ((now - *ctx.m_last_request_time > IDLE_PEER_KICK_TIME) ? "!" : "?");
      ss << " ";
      return true;
    });
    return ss.str();
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  std::pair<uint32_t, uint32_t> t_cryptonote_protocol_handler<t_core>::get_next_needed_pruning_stripe() const
  {
    const uint64_t want_height_from_blockchain = m_core.blockchain.get_current_blockchain_height();
    const uint64_t want_height_from_block_queue = m_block_queue.get_next_needed_height(want_height_from_blockchain);
    const uint64_t want_height = std::max(want_height_from_blockchain, want_height_from_block_queue);
    uint64_t blockchain_height = m_core.get_target_blockchain_height();
    // if we don't know the remote chain size yet, assume infinitely large so we get the right stripe if we're not near the tip
    if (blockchain_height == 0)
      blockchain_height = MAX_BLOCK_NUMBER;
    const uint32_t next_pruning_stripe = tools::get_pruning_stripe(want_height, blockchain_height, PRUNING_LOG_STRIPES);
    if (next_pruning_stripe == 0)
      return std::make_pair(0, 0);
    // if we already have a few peers on this stripe, but none on next one, try next one
    unsigned int n_next = 0, n_subsequent = 0, n_others = 0;
    const uint32_t subsequent_pruning_stripe = 1 + next_pruning_stripe % (1<<PRUNING_LOG_STRIPES);
    m_p2p->for_each_connection([&](const connection_context &context, nodetool::peerid_type peer_id) {
      if (context.m_state >= cryptonote_connection_context::state_synchronizing)
      {
        if (context.m_pruning_seed == 0 || tools::get_pruning_stripe(context.m_pruning_seed) == next_pruning_stripe)
          ++n_next;
        else if (tools::get_pruning_stripe(context.m_pruning_seed) == subsequent_pruning_stripe)
          ++n_subsequent;
        else
          ++n_others;
      }
      return true;
    });
    const bool use_next = (n_next > m_max_out_peers / 2 && n_subsequent <= 1) || (n_next > 2 && n_subsequent == 0);
    const uint32_t ret_stripe = use_next ? subsequent_pruning_stripe: next_pruning_stripe;
    const std::string po = get_peers_overview();
    log::debug(logcat, "get_next_needed_pruning_stripe: want height {} ({} from blockchain, {} from block queue), stripe {} ({}/{} on it and {} on {}, {} others) -> {} (+{}), current peers {}", want_height, want_height_from_blockchain, want_height_from_block_queue, next_pruning_stripe, n_next, m_max_out_peers.load(), n_subsequent, subsequent_pruning_stripe, n_others, ret_stripe, (ret_stripe - next_pruning_stripe + (1 << PRUNING_LOG_STRIPES)) % (1 << PRUNING_LOG_STRIPES), po);
    return std::make_pair(next_pruning_stripe, ret_stripe);
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  bool t_cryptonote_protocol_handler<t_core>::needs_new_sync_connections() const
  {
    const uint64_t target = m_core.get_target_blockchain_height();
    const uint64_t height = m_core.blockchain.get_current_blockchain_height();
    if (target && target <= height)
      return false;
    size_t n_out_peers = 0;
    m_p2p->for_each_connection([&](cryptonote_connection_context& ctx, nodetool::peerid_type peer_id)->bool{
      if (!ctx.m_is_income)
        ++n_out_peers;
      return true;
    });
    if (n_out_peers >= m_max_out_peers)
      return false;
    return true;
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::drop_connection(cryptonote_connection_context &context, bool add_fail, bool flush_all_spans)
  {
    log::debug(logcat, "{}dropping connection id {} (pruning seed {}), add_fail {}, flush_all_spans {}", context, boost::lexical_cast<std::string>(context.m_connection_id), epee::string_tools::to_string_hex(context.m_pruning_seed), add_fail, flush_all_spans);

    if (add_fail)
      m_p2p->add_host_fail(context.m_remote_address);

    m_block_queue.flush_spans(context.m_connection_id, flush_all_spans);

    // If this is the first drop_connection attempt then give the peer a second chance to sort
    // itself out: it might have send an invalid block because of a blink conflict, and we want it
    // to be able to get our blinks and do a rollback, but if we close instantly it might not get
    // them before we close the connection and so might never learn of the problem.
    if (context.m_drop_count >= 1)
    {
      log::debug(logcat, "{}{} a second chance before dropping", context, "giving connect id ", boost::lexical_cast<std::string>(context.m_connection_id));
      ++context.m_drop_count;
    }
    else
      m_p2p->drop_connection(context);
  }
  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::on_connection_close(cryptonote_connection_context &context)
  {
    uint64_t target = 0;
    m_p2p->for_each_connection([&](const connection_context& cntxt, nodetool::peerid_type peer_id) {
      if (cntxt.m_state >= cryptonote_connection_context::state_synchronizing && cntxt.m_connection_id != context.m_connection_id)
        target = std::max(target, cntxt.m_remote_blockchain_height);
      return true;
    });
    const uint64_t previous_target = m_core.get_target_blockchain_height();
    if (target < previous_target)
    {
      log::info(logcat, "Target height decreasing from {} to {}", previous_target, target);
      m_core.set_target_blockchain_height(target);
      if (target == 0 && context.m_state > cryptonote_connection_context::state_before_handshake && !m_stopping)
        log::warning(logcat, fg(fmt::terminal_color::yellow), "oxend is now disconnected from the network");
    }

    m_block_queue.flush_spans(context.m_connection_id, false);
    log::debug(logcat, "{}[{}] state: {} in state {}", context, epee::string_tools::to_string_hex(context.m_pruning_seed), "closed", cryptonote::get_protocol_state_string(context.m_state));
  }

  //------------------------------------------------------------------------------------------------------------------------
  template<class t_core>
  void t_cryptonote_protocol_handler<t_core>::stop()
  {
    m_stopping = true;
    m_core.stop();
  }
} // namespace

