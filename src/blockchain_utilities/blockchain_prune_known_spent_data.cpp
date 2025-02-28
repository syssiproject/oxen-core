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

#ifdef _WIN32
#define __STDC_FORMAT_MACROS  // NOTE(oxen): Explicitly define the PRIu64 macro on Mingw
#endif

#include "blockchain_db/blockchain_db.h"
#include "blockchain_objects.h"
#include "cryptonote_core/cryptonote_core.h"
#include "serialization/crypto.h"
#include "version.h"

#include <common/exception.hpp>
#include <common/command_line.h>

namespace po = boost::program_options;
using namespace cryptonote;

static auto logcat = log::Cat("bcutil");

static std::map<uint64_t, uint64_t> load_outputs(const fs::path& filename) {
    std::map<uint64_t, uint64_t> outputs;
    uint64_t amount = std::numeric_limits<uint64_t>::max();

    FILE* f =
#ifdef _WIN32
            _wfopen(filename.c_str(), L"r");
#else
            fopen(filename.c_str(), "r");
#endif

    if (!f) {
        log::error(logcat, "Failed to load outputs from {}: {}", filename, strerror(errno));
        return {};
    }
    while (1) {
        char s[256];
        if (!fgets(s, sizeof(s), f))
            break;
        if (feof(f))
            break;
        const size_t len = strlen(s);
        if (len > 0 && s[len - 1] == '\n')
            s[len - 1] = 0;
        if (!s[0])
            continue;
        std::pair<uint64_t, uint64_t> output;
        uint64_t offset, num_offsets;
        if (sscanf(s, "@%" PRIu64, &amount) == 1) {
            continue;
        }
        if (amount == std::numeric_limits<uint64_t>::max()) {
            log::error(logcat, "Bad format in {}", filename);
            continue;
        }
        if (sscanf(s, "%" PRIu64 "*%" PRIu64, &offset, &num_offsets) == 2 &&
            num_offsets < std::numeric_limits<uint64_t>::max() - offset) {
            outputs[amount] += num_offsets;
        } else if (sscanf(s, "%" PRIu64, &offset) == 1) {
            outputs[amount] += 1;
        } else {
            log::error(logcat, "Bad format in {}", filename);
            continue;
        }
    }
    fclose(f);
    return outputs;
}

int main(int argc, char* argv[]) {
    oxen::set_terminate_handler();
    TRY_ENTRY();

    epee::string_tools::set_module_name_and_folder(argv[0]);

    uint32_t log_level = 0;

    tools::on_startup();

    auto opt_size = command_line::boost_option_sizes();

    po::options_description desc_cmd_only("Command line options", opt_size.first, opt_size.second);
    po::options_description desc_cmd_sett(
            "Command line options and settings options", opt_size.first, opt_size.second);
    const command_line::arg_descriptor<std::string> arg_log_level = {
            "log-level", "0-4 or categories", ""};
    const command_line::arg_flag arg_verbose{"verbose", "Verbose output"};
    const command_line::arg_flag arg_dry_run{"dry-run", "Do not actually prune"};
    const command_line::arg_descriptor<std::string> arg_input = {
            "input", "Path to the known spent outputs file"};

    command_line::add_arg(desc_cmd_sett, cryptonote::arg_data_dir);
    command_line::add_network_args(desc_cmd_sett);
    command_line::add_arg(desc_cmd_sett, arg_log_level);
    command_line::add_arg(desc_cmd_sett, arg_verbose);
    command_line::add_arg(desc_cmd_sett, arg_dry_run);
    command_line::add_arg(desc_cmd_sett, arg_input);
    command_line::add_arg(desc_cmd_only, command_line::arg_help);

    po::options_description desc_options("Allowed options");
    desc_options.add(desc_cmd_only).add(desc_cmd_sett);

    po::variables_map vm;
    bool r = command_line::handle_error_helper(desc_options, [&]() {
        auto parser = po::command_line_parser(argc, argv).options(desc_options);
        po::store(parser.run(), vm);
        po::notify(vm);
        return true;
    });
    if (!r)
        return 1;

    if (command_line::get_arg(vm, command_line::arg_help)) {
        std::cout << "Oxen '" << OXEN_RELEASE_NAME << "' (v" << OXEN_VERSION_FULL << ")\n\n";
        std::cout << desc_options << std::endl;
        return 1;
    }

    mlog_configure(mlog_get_default_log_path("oxen-blockchain-prune-known-spent-data.log"), true);
    if (!command_line::is_arg_defaulted(vm, arg_log_level))
        mlog_set_log(command_line::get_arg(vm, arg_log_level).c_str());
    else
        mlog_set_log(std::string(std::to_string(log_level) + ",bcutil:INFO").c_str());

    log::warning(logcat, "Starting...");

    auto net_type = command_line::get_network(vm);
    bool opt_verbose = command_line::get_arg(vm, arg_verbose);
    bool opt_dry_run = command_line::get_arg(vm, arg_dry_run);

    const auto input = tools::utf8_path(command_line::get_arg(vm, arg_input));

    log::warning(logcat, "Initializing source blockchain (BlockchainDB)");
    blockchain_objects_t blockchain_objects = {};
    Blockchain* core_storage = &blockchain_objects.m_blockchain;
    BlockchainDB* db = new_db();
    if (db == NULL) {
        log::error(logcat, "Failed to initialize a database");
        throw oxen::traced<std::runtime_error>("Failed to initialize a database");
    }

    const fs::path filename =
            tools::utf8_path(command_line::get_arg(vm, cryptonote::arg_data_dir)) /
            db->get_db_name();
    log::warning(logcat, "Loading blockchain from folder {} ...", filename);

    try {
        db->open(filename, core_storage->nettype(), 0);
    } catch (const std::exception& e) {
        log::warning(logcat, "Error opening database: {}", e.what());
        return 1;
    }
    r = core_storage->init(db, nullptr /*ons_db*/, net_type);

    CHECK_AND_ASSERT_MES(r, 1, "Failed to initialize source blockchain storage");
    log::warning(logcat, "Source blockchain storage initialized OK");

    std::map<uint64_t, uint64_t> known_spent_outputs;
    if (input.empty()) {
        std::map<uint64_t, std::pair<uint64_t, uint64_t>> outputs;

        log::warning(logcat, "Scanning for known spent data...");
        db->for_all_transactions(
                [&](const crypto::hash& txid, const cryptonote::transaction& tx) {
                    const bool miner_tx = tx.is_miner_tx();
                    for (const auto& in : tx.vin)
                        if (const auto* txin = std::get_if<txin_to_key>(&in);
                            txin && txin->amount != 0)
                            outputs[txin->amount].second++;

                    for (const auto& out : tx.vout) {
                        uint64_t amount = out.amount;
                        if (miner_tx && tx.version >= txversion::v2_ringct)
                            amount = 0;
                        if (amount == 0)
                            continue;
                        if (!std::holds_alternative<txout_to_key>(out.target))
                            continue;

                        outputs[amount].first++;
                    }
                    return true;
                },
                true);

        for (const auto& i : outputs) {
            known_spent_outputs[i.first] = i.second.second;
        }
    } else {
        log::warning(logcat, "Loading known spent data...");
        known_spent_outputs = load_outputs(input);
    }

    log::warning(logcat, "Pruning known spent data...");

    bool stop_requested = false;
    tools::signal_handler::install([&stop_requested](int type) { stop_requested = true; });

    db->batch_start();

    size_t num_total_outputs = 0, num_prunable_outputs = 0, num_known_spent_outputs = 0,
           num_eligible_outputs = 0, num_eligible_known_spent_outputs = 0;
    for (auto i = known_spent_outputs.begin(); i != known_spent_outputs.end(); ++i) {
        uint64_t num_outputs = db->get_num_outputs(i->first);
        num_total_outputs += num_outputs;
        num_known_spent_outputs += i->second;
        if (i->first == 0) {
            if (opt_verbose)
                log::info(
                        logcat, "Ignoring output value {}, with {} outputs", i->first, num_outputs);
            continue;
        }
        num_eligible_outputs += num_outputs;
        num_eligible_known_spent_outputs += i->second;
        if (opt_verbose)
            log::info(logcat, "{}: {}/{}", i->first, i->second, num_outputs);
        if (num_outputs > i->second)
            continue;
        if (num_outputs && num_outputs < i->second) {
            log::error(
                    logcat,
                    "More outputs are spent than known for amount {}, not touching",
                    i->first);
            continue;
        }
        if (opt_verbose)
            log::info(logcat, "Pruning data for {} outputs", num_outputs);
        if (!opt_dry_run)
            db->prune_outputs(i->first);
        num_prunable_outputs += i->second;
    }

    db->batch_stop();

    log::info(logcat, "Total outputs: {}", num_total_outputs);
    log::info(logcat, "Known spent outputs: {}", num_known_spent_outputs);
    log::info(logcat, "Eligible outputs: {}", num_eligible_outputs);
    log::info(logcat, "Eligible known spent outputs: {}", num_eligible_known_spent_outputs);
    log::info(logcat, "Prunable outputs: {}", num_prunable_outputs);

    log::warning(logcat, "Blockchain known spent data pruned OK");
    core_storage->deinit();
    return 0;

    CATCH_ENTRY("Error", 1);
}
