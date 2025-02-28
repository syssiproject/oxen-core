// Copyright (c) 2014-2018, The Monero Project
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

#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include <exception>

#include <oxen/log.hpp>
#include "cryptonote_config.h"
#include "cryptonote_basic/difficulty.h"
#include "networks.h"

#define DIFFICULTY_LAG 15

int main(int argc, char* argv[]) {
    auto logcat = oxen::log::Cat("difficulty");
    if (argc < 2) {
        std::cerr << "Wrong arguments\n";
        return 1;
    }
    try {
        std::vector<uint64_t> timestamps, cumulative_difficulties;
        std::fstream data(argv[1], std::fstream::in);
        data.exceptions(std::fstream::badbit);
        data.clear(data.rdstate());
        uint64_t timestamp, difficulty, cumulative_difficulty = 0;
        size_t n = 0;
        uint64_t target_block_time_s =
                std::chrono::duration_cast<std::chrono::seconds>(
                        cryptonote::get_config(cryptonote::network_type::MAINNET).TARGET_BLOCK_TIME)
                        .count();
        while (data >> timestamp >> difficulty) {
            size_t begin, end;
            if (n < (cryptonote::old::DIFFICULTY_WINDOW + 1) + DIFFICULTY_LAG) {
                begin = 0;
                end = std::min(n, (size_t)(cryptonote::old::DIFFICULTY_WINDOW + 1));
            } else {
                end = n - DIFFICULTY_LAG;
                begin = end - (cryptonote::old::DIFFICULTY_WINDOW + 1);
            }
            uint64_t res = cryptonote::next_difficulty_v2(
                    std::vector<uint64_t>(timestamps.begin() + begin, timestamps.begin() + end),
                    std::vector<uint64_t>(
                            cumulative_difficulties.begin() + begin,
                            cumulative_difficulties.begin() + end),
                    target_block_time_s,
                    cryptonote::difficulty_calc_mode::normal);
            if (res != difficulty) {
                std::cerr << "Wrong difficulty for block " << n << "\nExpected: " << difficulty
                          << "\nFound: " << res << "\n";
                return 1;
            }
            timestamps.push_back(timestamp);
            cumulative_difficulties.push_back(cumulative_difficulty += difficulty);
            ++n;
        }
        if (!data.eof()) {
            data.clear(std::fstream::badbit);
        }
    } catch (const std::exception& ex) {
        oxen::log::error(logcat, "Exception at [{}]: {}", "main", ex.what());
        return 1;
    }
    return 0;
}
