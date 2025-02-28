// Copyright (c) 2014-2019, The Monero Project
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

#include <fmt/color.h>
#include <fmt/std.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <boost/algorithm/string.hpp>
#include <cstdio>
#include <fstream>

#include "blocks/blocks.h"
#include "bootstrap_file.h"
#include "bootstrap_serialization.h"
#include "common/command_line.h"
#include "common/exception.h"
#include "cryptonote_basic/cryptonote_format_utils.h"
#include "cryptonote_core/cryptonote_core.h"
#include "cryptonote_protocol/quorumnet.h"
#include "epee/misc_log_ex.h"
#include "logging/oxen_logger.h"
#include "serialization/binary_utils.h"

using namespace blockchain_utils;

namespace {
// CONFIG
bool opt_batch = true;
bool opt_verify = true;  // use add_new_block, which does verification before calling add_block
bool opt_resume = true;

// number of blocks per batch transaction
// adjustable through command-line argument according to available RAM
#if ARCH_WIDTH != 32
uint64_t db_batch_size = 20000;
#else
// set a lower default batch size, pending possible LMDB issue with large transaction size
uint64_t db_batch_size = 100;
#endif

// when verifying, use a smaller default batch size so progress is more
// frequently saved
uint64_t db_batch_size_verify = 5000;

std::string refresh_string = "\r                                    \r";

const command_line::arg_flag arg_recalculate_difficulty{
        "recalculate-difficulty",
        "Recalculate per-block difficulty starting from the height specified"};
}  // namespace

namespace po = boost::program_options;

using namespace cryptonote;

static auto logcat = log::Cat("bcutil");

// db_mode: safe, fast, fastest
int get_db_flags_from_mode(const std::string& db_mode) {
    int db_flags = 0;
    if (db_mode == "safe")
        db_flags = DBF_SAFE;
    else if (db_mode == "fast")
        db_flags = DBF_FAST;
    else if (db_mode == "fastest")
        db_flags = DBF_FASTEST;
    return db_flags;
}

int pop_blocks(cryptonote::core& core, int num_blocks) {
    bool use_batch = opt_batch;

    if (use_batch)
        core.blockchain.db().batch_start();

    try {
        core.blockchain.pop_blocks(num_blocks);
        if (use_batch) {
            core.blockchain.db().batch_stop();
            core.blockchain.db().show_stats();
        }
    } catch (const std::exception& e) {
        // There was an error, so don't commit pending data.
        // Destructor will abort write txn.
    }

    return num_blocks;
}

int check_flush(cryptonote::core& core, std::vector<block_complete_entry>& blocks, bool force) {
    if (blocks.empty())
        return 0;
    if (!force && blocks.size() < db_batch_size)
        return 0;

    // wait till we can verify a full HOH without extra, for speed
    uint64_t new_height = core.blockchain.db().height() + blocks.size();
    if (!force && new_height % HASH_OF_HASHES_STEP)
        return 0;

    std::vector<crypto::hash> hashes;
    for (const auto& b : blocks) {
        cryptonote::block block;
        if (!parse_and_validate_block_from_blob(b.block, block)) {
            log::error(logcat, "Failed to parse block: {}", get_blob_hash(b.block));
            core.cleanup_handle_incoming_blocks();
            return 1;
        }
        hashes.push_back(cryptonote::get_block_hash(block));
    }
    core.blockchain.prevalidate_block_hashes(core.blockchain.db().height(), hashes);

    // TODO(doyle): Checkpointing
    std::vector<block> pblocks;
    if (!core.prepare_handle_incoming_blocks(blocks, pblocks)) {
        log::error(logcat, "Failed to prepare to add blocks");
        return 1;
    }
    if (!pblocks.empty() && pblocks.size() != blocks.size()) {
        log::error(logcat, "Unexpected parsed blocks size");
        core.cleanup_handle_incoming_blocks();
        return 1;
    }

    size_t blockidx = 0;
    for (const block_complete_entry& block_entry : blocks) {
        // process transactions
        for (auto& tx_blob : block_entry.txs) {
            tx_verification_context tvc{};
            core.handle_incoming_tx(tx_blob, tvc, tx_pool_options::from_block());
            if (tvc.m_verifivation_failed) {
                log::error(
                        logcat,
                        "transaction verification failed, tx_id = {}",
                        get_blob_hash(tx_blob));
                core.cleanup_handle_incoming_blocks();
                return 1;
            }
        }

        // process block

        block_verification_context bvc{};

        core.handle_incoming_block(
                block_entry.block,
                pblocks.empty() ? NULL : &pblocks[blockidx++],
                bvc,
                nullptr /*checkpoint*/,
                false);  // <--- process block

        if (bvc.m_verifivation_failed) {
            log::error(
                    logcat, "Block verification failed, id = {}", get_blob_hash(block_entry.block));
            core.cleanup_handle_incoming_blocks();
            return 1;
        }
        if (bvc.m_marked_as_orphaned) {
            log::error(logcat, "Block received at sync phase was marked as orphaned");
            core.cleanup_handle_incoming_blocks();
            return 1;
        }

    }  // each download block
    if (!core.cleanup_handle_incoming_blocks())
        return 1;

    blocks.clear();
    return 0;
}

int import_from_file(
        cryptonote::core& core, const fs::path& import_file_path, uint64_t block_stop = 0) {
    // Reset stats, in case we're using newly created db, accumulating stats
    // from addition of genesis block.
    // This aligns internal db counts with importer counts.
    core.blockchain.db().reset_stats();

    if (std::error_code ec; !fs::exists(import_file_path, ec)) {
        log::error(logcat, "bootstrap file not found: {}", import_file_path);
        return false;
    }

    uint64_t start_height = 1, seek_height;
    if (opt_resume)
        start_height = core.blockchain.get_current_blockchain_height();

    seek_height = start_height;
    BootstrapFile bootstrap;
    std::streampos pos;
    // BootstrapFile bootstrap(import_file_path);
    uint64_t total_source_blocks = bootstrap.count_blocks(import_file_path, pos, seek_height);
    log::info(
            logcat,
            "bootstrap file last block number: {} (zero-based height)  total blocks: {}",
            total_source_blocks - 1,
            total_source_blocks);

    if (total_source_blocks - 1 <= start_height) {
        return false;
    }

    std::cout << "\nPreparing to read blocks...\n\n";

    std::ifstream import_file{import_file_path, std::ios::binary};

    if (import_file.fail()) {
        log::error(logcat, "import_file.open() fail");
        return false;
    }

    // 4 byte magic + (currently) 1024 byte header structures
    bootstrap.seek_to_first_chunk(import_file);

    char buffer1[1024];
    char buffer_block[BUFFER_SIZE];
    block b;
    transaction tx;
    int quit = 0;
    uint64_t bytes_read;

    // Note that a new blockchain will start with block number 0 (total blocks: 1)
    // due to genesis block being added at initialization.

    if (!block_stop) {
        block_stop = total_source_blocks - 1;
    }

    // These are what we'll try to use, and they don't have to be a determination
    // from source and destination blockchains, but those are the defaults.
    log::info(logcat, "start block: {}  stop block: {}", start_height, block_stop);

    bool use_batch = opt_batch && !opt_verify;

    log::info(logcat, "Reading blockchain from bootstrap file...");
    std::cout << "\n";

    std::vector<block_complete_entry> blocks;

    uint64_t h = 0;
    uint64_t num_imported = 0;

    // Skip to start_height before we start adding.
    {
        bool q2 = false;
        import_file.seekg(pos);
        bytes_read = bootstrap.count_bytes(import_file, start_height - seek_height, h, q2);
        if (q2) {
            quit = 2;
            goto quitting;
        }
        h = start_height;
    }

    if (use_batch) {
        uint64_t bytes, h2;
        bool q2;
        pos = import_file.tellg();
        bytes = bootstrap.count_bytes(import_file, db_batch_size, h2, q2);
        if (import_file.eof())
            import_file.clear();
        import_file.seekg(pos);
        core.blockchain.db().batch_start(db_batch_size, bytes);
    }
    while (!quit) {
        uint32_t chunk_size;
        import_file.read(buffer1, sizeof(chunk_size));
        // TODO: bootstrap.read_chunk();
        if (!import_file) {
            std::cout << refresh_string;
            log::info(logcat, "End of file reached");
            quit = 1;
            break;
        }
        bytes_read += sizeof(chunk_size);

        try {
            serialization::parse_binary(std::string_view{buffer1, sizeof(chunk_size)}, chunk_size);
        } catch (const std::exception& e) {
            throw oxen::traced<std::runtime_error>(
                    "Error in deserialization of chunk size: "s + e.what());
        }
        log::debug(logcat, "chunk_size: {}", chunk_size);

        if (chunk_size > BUFFER_SIZE) {
            log::warning(
                    logcat, "WARNING: chunk_size {} > BUFFER_SIZE {}", chunk_size, BUFFER_SIZE);
            throw oxen::traced<std::runtime_error>("Aborting: chunk size exceeds buffer size");
        }
        if (chunk_size > CHUNK_SIZE_WARNING_THRESHOLD) {
            log::info(logcat, "NOTE: chunk_size {} > {}", chunk_size, CHUNK_SIZE_WARNING_THRESHOLD);
        } else if (chunk_size == 0) {
            log::error(logcat, "ERROR: chunk_size == 0");
            return 2;
        }
        import_file.read(buffer_block, chunk_size);
        if (!import_file) {
            if (import_file.eof()) {
                std::cout << refresh_string;
                log::info(logcat, "End of file reached - file was truncated");
                quit = 1;
                break;
            } else {
                log::error(
                        logcat,
                        "ERROR: unexpected end of file: bytes read before error: {} of chunk_size "
                        "{}",
                        import_file.gcount(),
                        chunk_size);
                return 2;
            }
        }
        bytes_read += chunk_size;
        log::debug(logcat, "Total bytes read: {}", bytes_read);

        if (h > block_stop) {
            std::cout << refresh_string << "block " << h - 1 << " / " << block_stop << "\n"
                      << std::endl;
            log::info(
                    logcat,
                    "Specified block number reached - stopping.  block: {}  total blocks: {}",
                    h - 1,
                    h);
            quit = 1;
            break;
        }

        try {
            bootstrap::block_package bp;
            try {
                serialization::parse_binary(std::string_view{buffer_block, chunk_size}, bp);
            } catch (const std::exception& e) {
                throw oxen::traced<std::runtime_error>(
                        "Error in deserialization of chunk"s + e.what());
            }

            int display_interval = 1000;
            int progress_interval = 10;
            // NOTE: use of NUM_BLOCKS_PER_CHUNK is a placeholder in case multi-block chunks are
            // later supported.
            for (size_t chunk_ind = 0; chunk_ind < NUM_BLOCKS_PER_CHUNK; ++chunk_ind) {
                ++h;
                if ((h - 1) % display_interval == 0) {
                    std::cout << refresh_string;
                    log::debug(logcat, "loading block number {}", h - 1);
                } else {
                    log::debug(logcat, "loading block number {}", h - 1);
                }
                b = bp.block;
                log::debug(logcat, "block prev_id: {}\n", b.prev_id);

                if ((h - 1) % progress_interval == 0) {
                    std::cout << refresh_string << "block " << h - 1 << " / " << block_stop << "\r"
                              << std::flush;
                }

                if (opt_verify) {
                    std::string block;
                    cryptonote::block_to_blob(bp.block, block);
                    std::vector<std::string> txs;
                    for (const auto& tx : bp.txs) {
                        txs.push_back(std::string());
                        cryptonote::tx_to_blob(tx, txs.back());
                    }
                    blocks.push_back({block, txs});
                    int ret = check_flush(core, blocks, false);
                    if (ret) {
                        quit = 2;  // make sure we don't commit partial block data
                        break;
                    }
                } else {
                    std::vector<std::pair<transaction, std::string>> txs;
                    std::vector<transaction> archived_txs;

                    archived_txs = bp.txs;

                    // tx number 1: coinbase tx
                    // tx number 2 onwards: archived_txs
                    for (const transaction& tx : archived_txs) {
                        // add blocks with verification.
                        // for Blockchain and blockchain_storage add_new_block().
                        // for add_block() method, without (much) processing.
                        // don't add coinbase transaction to txs.
                        //
                        // because add_block() calls
                        // add_transaction(blk_hash, blk.miner_tx) first, and
                        // then a for loop for the transactions in txs.
                        txs.push_back(std::make_pair(tx, tx_to_blob(tx)));
                    }

                    size_t block_weight;
                    difficulty_type cumulative_difficulty;
                    uint64_t coins_generated;

                    block_weight = bp.block_weight;
                    cumulative_difficulty = bp.cumulative_difficulty;
                    coins_generated = bp.coins_generated;

                    try {
                        uint64_t long_term_block_weight =
                                core.blockchain.get_next_long_term_block_weight(
                                        block_weight);
                        core.blockchain.db().add_block(
                                std::make_pair(b, block_to_blob(b)),
                                block_weight,
                                long_term_block_weight,
                                cumulative_difficulty,
                                coins_generated,
                                txs);
                    } catch (const std::exception& e) {
                        std::cout << refresh_string;
                        log::error(logcat, "Error adding block to blockchain: {}", e.what());
                        quit = 2;  // make sure we don't commit partial block data
                        break;
                    }

                    if (use_batch) {
                        if ((h - 1) % db_batch_size == 0) {
                            uint64_t bytes, h2;
                            bool q2;
                            std::cout << refresh_string;
                            // zero-based height
                            std::cout << "\n[- batch commit at height " << h - 1 << " -]\n";
                            core.blockchain.db().batch_stop();
                            pos = import_file.tellg();
                            bytes = bootstrap.count_bytes(import_file, db_batch_size, h2, q2);
                            import_file.seekg(pos);
                            core.blockchain.db().batch_start(
                                    db_batch_size, bytes);
                            std::cout << "\n";
                            core.blockchain.db().show_stats();
                        }
                    }
                }
                ++num_imported;
            }
        } catch (const std::exception& e) {
            std::cout << refresh_string;
            log::error(logcat, "exception while reading from file, height={}: {}", h, e.what());
            return 2;
        }
    }  // while

quitting:
    import_file.close();

    if (opt_verify) {
        int ret = check_flush(core, blocks, true);
        if (ret)
            return ret;
    }

    if (use_batch) {
        if (quit > 1) {
            // There was an error, so don't commit pending data.
            // Destructor will abort write txn.
        } else {
            core.blockchain.db().batch_stop();
        }
    }

    core.blockchain.db().show_stats();
    log::info(logcat, "Number of blocks imported: {}", num_imported);
    if (h > 0)
        // TODO: if there was an error, the last added block is probably at zero-based height h-2
        log::info(logcat, "Finished at block: {}  total blocks: {}", h - 1, h);

    std::cout << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    oxen::set_terminate_handler();
    TRY_ENTRY();

    epee::string_tools::set_module_name_and_folder(argv[0]);

    uint64_t num_blocks = 0;
    uint64_t block_stop = 0;
    std::string m_config_folder;
    std::string db_arg_str;

    tools::on_startup();

    auto opt_size = command_line::boost_option_sizes();

    po::options_description desc_cmd_only("Command line options", opt_size.first, opt_size.second);
    po::options_description desc_cmd_sett(
            "Command line options and settings options", opt_size.first, opt_size.second);
    const command_line::arg_descriptor<std::string> arg_input_file = {
            "input-file", "Specify input file"};
    const command_line::arg_descriptor<std::string> arg_log_level = {
            "log-level", "0-4 or categories", ""};
    const command_line::arg_descriptor<uint64_t> arg_block_stop = {
            "block-stop", "Stop at block number", block_stop};
    const command_line::arg_descriptor<uint64_t> arg_batch_size = {"batch-size", "", db_batch_size};
    const command_line::arg_descriptor<uint64_t> arg_pop_blocks = {
            "pop-blocks", "Remove blocks from end of blockchain", num_blocks};
    const command_line::arg_flag arg_count_blocks = {
            "count-blocks", "Count blocks in bootstrap file and exit"};
    const command_line::arg_flag arg_noverify = {
            "dangerous-unverified-import",
            "Blindly trust the import file and use potentially malicious blocks and transactions "
            "during import (only enable if you exported the file yourself)"};
    const command_line::arg_flag arg_batch = {"batch", "Batch transactions for faster import"};
    const command_line::arg_flag arg_resume = {
            "resume", "Resume from current height if output database already exists"};

    command_line::add_arg(desc_cmd_sett, arg_input_file);
    command_line::add_arg(desc_cmd_sett, arg_log_level);
    command_line::add_arg(desc_cmd_sett, arg_batch_size);
    command_line::add_arg(desc_cmd_sett, arg_block_stop);

    command_line::add_arg(desc_cmd_only, arg_count_blocks);
    command_line::add_arg(desc_cmd_only, arg_pop_blocks);
    command_line::add_arg(desc_cmd_only, command_line::arg_help);

    command_line::add_arg(desc_cmd_only, arg_recalculate_difficulty);

    // call add_options() directly for these arguments since
    // command_line helpers support only boolean switch, not boolean argument
    desc_cmd_sett.add_options()(
            arg_noverify.name.c_str(),
            arg_noverify.make_semantic(),
            arg_noverify.description.c_str())(
            arg_batch.name.c_str(), arg_batch.make_semantic(), arg_batch.description.c_str())(
            arg_resume.name.c_str(), arg_resume.make_semantic(), arg_resume.description.c_str());

    po::options_description desc_options("Allowed options");
    desc_options.add(desc_cmd_only).add(desc_cmd_sett);
    cryptonote::core::init_options(desc_options);

    po::variables_map vm;
    bool r = command_line::handle_error_helper(desc_options, [&]() {
        po::store(po::parse_command_line(argc, argv, desc_options), vm);
        po::notify(vm);
        return true;
    });
    if (!r)
        return 1;

    opt_verify = !command_line::get_arg(vm, arg_noverify);
    opt_batch = command_line::get_arg(vm, arg_batch);
    opt_resume = command_line::get_arg(vm, arg_resume);
    block_stop = command_line::get_arg(vm, arg_block_stop);
    db_batch_size = command_line::get_arg(vm, arg_batch_size);

    if (command_line::get_arg(vm, command_line::arg_help)) {
        std::cout << "Oxen '" << OXEN_RELEASE_NAME << "' (v" << OXEN_VERSION_FULL << ")\n\n";
        std::cout << desc_options << std::endl;
        return 1;
    }

    if (!opt_batch && !command_line::is_arg_defaulted(vm, arg_batch_size)) {
        std::cerr << "Error: batch-size set, but batch option not enabled\n";
        return 1;
    }
    if (!db_batch_size) {
        std::cerr << "Error: batch-size must be > 0\n";
        return 1;
    }
    if (opt_verify && command_line::is_arg_defaulted(vm, arg_batch_size)) {
        // usually want batch size default lower if verify on, so progress can be
        // frequently saved.
        //
        // currently, with Windows, default batch size is low, so ignore
        // default db_batch_size_verify unless it's even lower
        if (db_batch_size > db_batch_size_verify) {
            db_batch_size = db_batch_size_verify;
        }
    }

    auto net_type = command_line::get_network(vm);
    m_config_folder = command_line::get_arg(vm, cryptonote::arg_data_dir);
    auto log_file_path = m_config_folder + "oxen-blockchain-import.log";
    oxen::logging::init(log_file_path, command_line::get_arg(vm, arg_log_level));
    log::info(logcat, "Starting...");

    fs::path import_file_path;

    if (!command_line::is_arg_defaulted(vm, arg_input_file))
        import_file_path = tools::utf8_path(command_line::get_arg(vm, arg_input_file));
    else
        import_file_path =
                tools::utf8_path(m_config_folder) / fs::path(u8"export") / BLOCKCHAIN_RAW;

    if (command_line::get_arg(vm, arg_count_blocks)) {
        BootstrapFile bootstrap;
        bootstrap.count_blocks(import_file_path);
        return 0;
    }

    log::info(logcat, "database: LMDB");
    log::info(logcat, "verify:  {}", opt_verify);
    if (opt_batch) {
        log::info(logcat, "batch:   {} batch size: {}", opt_batch, db_batch_size);
    } else {
        log::info(logcat, "batch:   {}", opt_batch);
    }
    log::info(logcat, "resume:  {}", opt_resume);
    log::info(logcat, "nettype: {}", network_type_to_string(net_type));

    log::info(logcat, "bootstrap file path: {}", import_file_path);
    log::info(logcat, "database path:       {}", m_config_folder);

    if (!opt_verify) {
        log::warning(
                logcat,
                fg(fmt::terminal_color::red),
                "\n\
      Import is set to proceed WITHOUT VERIFICATION.\n\
      This is a DANGEROUS operation: if the file was tampered with in transit, or obtained from a malicious source,\n\
      you could end up with a compromised database. It is recommended to NOT use {}.\n\
      *****************************************************************************************\n\
      You have 90 seconds to press ^C or terminate this program before unverified import starts\n\
      *****************************************************************************************",
                arg_noverify.name);
        sleep(90);
    }

    // TODO: currently using cryptonote_protocol stub for this kind of test, use real validation of
    // relayed objects
    cryptonote::core core;

    try {

#if defined(PER_BLOCK_CHECKPOINT)
        const GetCheckpointsCallback& get_checkpoints = blocks::GetCheckpointsData;
#else
        const GetCheckpointsCallback& get_checkpoints = nullptr;
#endif

        quorumnet::init_core_callbacks();

        if (!core.init(vm, nullptr, get_checkpoints)) {
            std::cerr << "Failed to initialize core\n";
            return 1;
        }
        core.blockchain.db().set_batch_transactions(true);

        if (!command_line::is_arg_defaulted(vm, arg_pop_blocks)) {
            num_blocks = command_line::get_arg(vm, arg_pop_blocks);
            log::info(
                    logcat,
                    "height: {}",
                    core.blockchain.get_current_blockchain_height());
            pop_blocks(core, num_blocks);
            log::info(
                    logcat,
                    "height: {}",
                    core.blockchain.get_current_blockchain_height());
            return 0;
        }

        if (command_line::get_arg(vm, arg_recalculate_difficulty))
            core.blockchain.db().fixup(core.get_nettype());

        import_from_file(core, import_file_path, block_stop);

        // ensure db closed
        //   - transactions properly checked and handled
        //   - disk sync if needed
        //
        core.deinit();
    } catch (const DB_ERROR& e) {
        std::cout << std::string("Error loading blockchain db: ") + e.what() +
                             " -- shutting down now\n";
        core.deinit();
        return 1;
    }

    return 0;

    CATCH_ENTRY("Import error", 1);
}
