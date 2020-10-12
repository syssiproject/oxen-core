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

#include "wallet_rpc_server_commands_defs.h"

namespace tools::wallet_rpc {

KV_SERIALIZE_MAP_CODE_BEGIN(EMPTY)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_BALANCE::request)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(address_indices)
  KV_SERIALIZE_OPT(all_accounts, false);
  KV_SERIALIZE_OPT(strict, false);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_BALANCE::per_subaddress_info)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(address_index)
  KV_SERIALIZE(address)
  KV_SERIALIZE(balance)
  KV_SERIALIZE(unlocked_balance)
  KV_SERIALIZE(label)
  KV_SERIALIZE(num_unspent_outputs)
  KV_SERIALIZE(blocks_to_unlock)
  KV_SERIALIZE(time_to_unlock)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_BALANCE::response)
  KV_SERIALIZE(balance)
  KV_SERIALIZE(unlocked_balance)
  KV_SERIALIZE(multisig_import_needed)
  KV_SERIALIZE(per_subaddress)
  KV_SERIALIZE(blocks_to_unlock)
  KV_SERIALIZE(time_to_unlock)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS::request)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(address_index)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS::address_info)
  KV_SERIALIZE(address)
  KV_SERIALIZE(label)
  KV_SERIALIZE(address_index)
  KV_SERIALIZE(used)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(addresses)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS_INDEX::request)
  KV_SERIALIZE(address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS_INDEX::response)
  KV_SERIALIZE(index)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CREATE_ADDRESS::request)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE_OPT(count, 1U)
  KV_SERIALIZE(label)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CREATE_ADDRESS::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(address_index)
  KV_SERIALIZE(addresses)
  KV_SERIALIZE(address_indices)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LABEL_ADDRESS::request)
  KV_SERIALIZE(index)
  KV_SERIALIZE(label)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ACCOUNTS::request)
  KV_SERIALIZE(tag)
  KV_SERIALIZE_OPT(strict_balances, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ACCOUNTS::subaddress_account_info)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(base_address)
  KV_SERIALIZE(balance)
  KV_SERIALIZE(unlocked_balance)
  KV_SERIALIZE(label)
  KV_SERIALIZE(tag)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ACCOUNTS::response)
  KV_SERIALIZE(total_balance)
  KV_SERIALIZE(total_unlocked_balance)
  KV_SERIALIZE(subaddress_accounts)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CREATE_ACCOUNT::request)
  KV_SERIALIZE(label)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CREATE_ACCOUNT::response)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LABEL_ACCOUNT::request)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(label)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ACCOUNT_TAGS::account_tag_info)
  KV_SERIALIZE(tag);
  KV_SERIALIZE(label);
  KV_SERIALIZE(accounts);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ACCOUNT_TAGS::response)
  KV_SERIALIZE(account_tags)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TAG_ACCOUNTS::request)
  KV_SERIALIZE(tag)
  KV_SERIALIZE(accounts)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(UNTAG_ACCOUNTS::request)
  KV_SERIALIZE(accounts)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_ACCOUNT_TAG_DESCRIPTION::request)
  KV_SERIALIZE(tag)
  KV_SERIALIZE(description)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_HEIGHT::response)
  KV_SERIALIZE(height)
  KV_SERIALIZE(immutable_height)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TRANSFER::request)
  KV_SERIALIZE(destinations)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(subaddr_indices)
  KV_SERIALIZE(priority)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay, false)
  KV_SERIALIZE_OPT(get_tx_hex, false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TRANSFER::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TRANSFER_SPLIT::request)
  KV_SERIALIZE(destinations)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(subaddr_indices)
  KV_SERIALIZE(priority)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(get_tx_keys)
  KV_SERIALIZE_OPT(do_not_relay, false)
  KV_SERIALIZE_OPT(get_tx_hex, false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TRANSFER_SPLIT::key_list)
  KV_SERIALIZE(keys)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(TRANSFER_SPLIT::response)
  KV_SERIALIZE(tx_hash_list)
  KV_SERIALIZE(tx_key_list)
  KV_SERIALIZE(amount_list)
  KV_SERIALIZE(fee_list)
  KV_SERIALIZE(tx_blob_list)
  KV_SERIALIZE(tx_metadata_list)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(DESCRIBE_TRANSFER::recipient)
  KV_SERIALIZE(address)
  KV_SERIALIZE(amount)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(DESCRIBE_TRANSFER::transfer_description)
  KV_SERIALIZE(amount_in)
  KV_SERIALIZE(amount_out)
  KV_SERIALIZE(ring_size)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(recipients)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(change_amount)
  KV_SERIALIZE(change_address)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(dummy_outputs)
  KV_SERIALIZE(extra)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(DESCRIBE_TRANSFER::request)
  KV_SERIALIZE(unsigned_txset)
  KV_SERIALIZE(multisig_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(DESCRIBE_TRANSFER::response)
  KV_SERIALIZE(desc)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN_TRANSFER::request)
  KV_SERIALIZE(unsigned_txset)
  KV_SERIALIZE_OPT(export_raw, false)
  KV_SERIALIZE_OPT(get_tx_keys, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN_TRANSFER::response)
  KV_SERIALIZE(signed_txset)
  KV_SERIALIZE(tx_hash_list)
  KV_SERIALIZE(tx_raw_list)
  KV_SERIALIZE(tx_key_list)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SUBMIT_TRANSFER::request)
  KV_SERIALIZE(tx_data_hex)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SUBMIT_TRANSFER::response)
  KV_SERIALIZE(tx_hash_list)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_DUST::request)
  KV_SERIALIZE(get_tx_keys)
  KV_SERIALIZE_OPT(do_not_relay, false)
  KV_SERIALIZE_OPT(get_tx_hex, false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_DUST::key_list)
  KV_SERIALIZE(keys)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_DUST::response)
  KV_SERIALIZE(tx_hash_list)
  KV_SERIALIZE(tx_key_list)
  KV_SERIALIZE(amount_list)
  KV_SERIALIZE(fee_list)
  KV_SERIALIZE(tx_blob_list)
  KV_SERIALIZE(tx_metadata_list)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_ALL::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(subaddr_indices)
  KV_SERIALIZE_OPT(subaddr_indices_all, false)
  KV_SERIALIZE(priority)
  KV_SERIALIZE_OPT(outputs, (uint64_t)1)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(get_tx_keys)
  KV_SERIALIZE(below_amount)
  KV_SERIALIZE_OPT(do_not_relay, false)
  KV_SERIALIZE_OPT(get_tx_hex, false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_ALL::key_list)
  KV_SERIALIZE(keys)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_ALL::response)
  KV_SERIALIZE(tx_hash_list)
  KV_SERIALIZE(tx_key_list)
  KV_SERIALIZE(amount_list)
  KV_SERIALIZE(fee_list)
  KV_SERIALIZE(tx_blob_list)
  KV_SERIALIZE(tx_metadata_list)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_SINGLE::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE(priority)
  KV_SERIALIZE_OPT(outputs, (uint64_t)1)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(get_tx_key)
  KV_SERIALIZE(key_image)
  KV_SERIALIZE_OPT(do_not_relay, false)
  KV_SERIALIZE_OPT(get_tx_hex, false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SWEEP_SINGLE::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(RELAY_TX::request)
  KV_SERIALIZE(hex)
  KV_SERIALIZE_OPT(blink, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(RELAY_TX::response)
  KV_SERIALIZE(tx_hash)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(payment_details)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(block_height)
  KV_SERIALIZE(unlock_time)
  KV_SERIALIZE(locked)
  KV_SERIALIZE(subaddr_index)
  KV_SERIALIZE(address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_PAYMENTS::request)
  KV_SERIALIZE(payment_id)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_PAYMENTS::response)
  KV_SERIALIZE(payments)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_BULK_PAYMENTS::request)
  KV_SERIALIZE(payment_ids)
  KV_SERIALIZE(min_block_height)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_BULK_PAYMENTS::response)
  KV_SERIALIZE(payments)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(transfer_details)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(spent)
  KV_SERIALIZE(global_index)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(subaddr_index)
  KV_SERIALIZE(key_image)
  KV_SERIALIZE(block_height)
  KV_SERIALIZE(frozen)
  KV_SERIALIZE(unlocked)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(INCOMING_TRANSFERS::request)
  KV_SERIALIZE(transfer_type)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(subaddr_indices)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(INCOMING_TRANSFERS::response)
  KV_SERIALIZE(transfers)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(QUERY_KEY::request)
  KV_SERIALIZE(key_type)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(QUERY_KEY::response)
  KV_SERIALIZE(key)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(MAKE_INTEGRATED_ADDRESS::request)
  KV_SERIALIZE(standard_address)
  KV_SERIALIZE(payment_id)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(MAKE_INTEGRATED_ADDRESS::response)
  KV_SERIALIZE(integrated_address)
  KV_SERIALIZE(payment_id)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SPLIT_INTEGRATED_ADDRESS::request)
  KV_SERIALIZE(integrated_address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SPLIT_INTEGRATED_ADDRESS::response)
  KV_SERIALIZE(standard_address)
  KV_SERIALIZE(payment_id)
  KV_SERIALIZE(is_subaddress)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(RESCAN_BLOCKCHAIN::request)
  KV_SERIALIZE_OPT(hard, false);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_TX_NOTES::request)
  KV_SERIALIZE(txids)
  KV_SERIALIZE(notes)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_NOTES::request)
  KV_SERIALIZE(txids)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_NOTES::response)
  KV_SERIALIZE(notes)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_ATTRIBUTE::request)
  KV_SERIALIZE(key)
  KV_SERIALIZE(value)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ATTRIBUTE::request)
  KV_SERIALIZE(key)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ATTRIBUTE::response)
  KV_SERIALIZE(value)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_KEY::request)
  KV_SERIALIZE(txid)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_KEY::response)
  KV_SERIALIZE(tx_key)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_TX_KEY::request)
  KV_SERIALIZE(txid)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_TX_KEY::response)
  KV_SERIALIZE(received)
  KV_SERIALIZE(in_pool)
  KV_SERIALIZE(confirmations)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_PROOF::request)
  KV_SERIALIZE(txid)
  KV_SERIALIZE(address)
  KV_SERIALIZE(message)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TX_PROOF::response)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_TX_PROOF::request)
  KV_SERIALIZE(txid)
  KV_SERIALIZE(address)
  KV_SERIALIZE(message)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_TX_PROOF::response)
  KV_SERIALIZE(good)
  KV_SERIALIZE(received)
  KV_SERIALIZE(in_pool)
  KV_SERIALIZE(confirmations)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_SPEND_PROOF::request)
  KV_SERIALIZE(txid)
  KV_SERIALIZE(message)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_SPEND_PROOF::response)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_SPEND_PROOF::request)
  KV_SERIALIZE(txid)
  KV_SERIALIZE(message)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_SPEND_PROOF::response)
  KV_SERIALIZE(good)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_RESERVE_PROOF::request)
  KV_SERIALIZE(all)
  KV_SERIALIZE(account_index)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(message)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_RESERVE_PROOF::response)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_RESERVE_PROOF::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE(message)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHECK_RESERVE_PROOF::response)
  KV_SERIALIZE(good)
  KV_SERIALIZE(total)
  KV_SERIALIZE(spent)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TRANSFERS::request)
  KV_SERIALIZE_OPT(in, true);
  KV_SERIALIZE_OPT(out, true);
  KV_SERIALIZE_OPT(stake, true);
  KV_SERIALIZE_OPT(pending, true);
  KV_SERIALIZE_OPT(failed, true);
  KV_SERIALIZE_OPT(pool, true);
  KV_SERIALIZE_OPT(coinbase, true);
  KV_SERIALIZE(filter_by_height);
  KV_SERIALIZE(min_height);
  KV_SERIALIZE_OPT(max_height, (uint64_t)CRYPTONOTE_MAX_BLOCK_NUMBER);
  KV_SERIALIZE(account_index);
  KV_SERIALIZE(subaddr_indices);
  KV_SERIALIZE_OPT(all_accounts, false);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TRANSFERS::response)
  KV_SERIALIZE(in);
  KV_SERIALIZE(out);
  KV_SERIALIZE(pending);
  KV_SERIALIZE(failed);
  KV_SERIALIZE(pool);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TRANSFERS_CSV::response)
  KV_SERIALIZE(csv);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TRANSFER_BY_TXID::request)
  KV_SERIALIZE(txid);
  KV_SERIALIZE_OPT(account_index, (uint32_t)0)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_TRANSFER_BY_TXID::response)
  KV_SERIALIZE(transfer);
  KV_SERIALIZE(transfers);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN::request)
  KV_SERIALIZE(data)
  KV_SERIALIZE_OPT(account_index, 0u)
  KV_SERIALIZE_OPT(address_index, 0u)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN::response)
  KV_SERIALIZE(signature);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(VERIFY::request)
  KV_SERIALIZE(data);
  KV_SERIALIZE(address);
  KV_SERIALIZE(signature);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(VERIFY::response)
  KV_SERIALIZE(good);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_OUTPUTS::request)
  KV_SERIALIZE(all)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_OUTPUTS::response)
  KV_SERIALIZE(outputs_data_hex);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_OUTPUTS::request)
  KV_SERIALIZE(outputs_data_hex);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_OUTPUTS::response)
  KV_SERIALIZE(num_imported);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_KEY_IMAGES::request)
  KV_SERIALIZE_OPT(requested_only, false);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_KEY_IMAGES::signed_key_image)
  KV_SERIALIZE(key_image);
  KV_SERIALIZE(signature);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_KEY_IMAGES::response)
  KV_SERIALIZE(offset);
  KV_SERIALIZE(signed_key_images);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_KEY_IMAGES::signed_key_image)
  KV_SERIALIZE(key_image);
  KV_SERIALIZE(signature);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_KEY_IMAGES::request)
  KV_SERIALIZE_OPT(offset, (uint32_t)0);
  KV_SERIALIZE(signed_key_images);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_KEY_IMAGES::response)
  KV_SERIALIZE(height)
  KV_SERIALIZE(spent)
  KV_SERIALIZE(unspent)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(uri_spec)
  KV_SERIALIZE(address);
  KV_SERIALIZE(payment_id);
  KV_SERIALIZE(amount);
  KV_SERIALIZE(tx_description);
  KV_SERIALIZE(recipient_name);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(MAKE_URI::response)
  KV_SERIALIZE(uri)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(PARSE_URI::request)
  KV_SERIALIZE(uri)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(PARSE_URI::response)
  KV_SERIALIZE(uri);
  KV_SERIALIZE(unknown_parameters);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(ADD_ADDRESS_BOOK_ENTRY::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE(description)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(ADD_ADDRESS_BOOK_ENTRY::response)
  KV_SERIALIZE(index);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EDIT_ADDRESS_BOOK_ENTRY::request)
  KV_SERIALIZE(index)
  KV_SERIALIZE(set_address)
  KV_SERIALIZE(address)
  KV_SERIALIZE(set_description)
  KV_SERIALIZE(description)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS_BOOK_ENTRY::request)
  KV_SERIALIZE(entries)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS_BOOK_ENTRY::entry)
  KV_SERIALIZE(index)
  KV_SERIALIZE(address)
  KV_SERIALIZE(description)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_ADDRESS_BOOK_ENTRY::response)
  KV_SERIALIZE(entries)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(DELETE_ADDRESS_BOOK_ENTRY::request)
  KV_SERIALIZE(index);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REFRESH::request)
  KV_SERIALIZE_OPT(start_height, (uint64_t) 0)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REFRESH::response)
  KV_SERIALIZE(blocks_fetched);
  KV_SERIALIZE(received_money);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(AUTO_REFRESH::request)
  KV_SERIALIZE_OPT(enable, true)
  KV_SERIALIZE_OPT(period, (uint32_t)0)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(START_MINING::request)
  KV_SERIALIZE(threads_count)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_LANGUAGES::response)
  KV_SERIALIZE(languages)
  KV_SERIALIZE(languages_local)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CREATE_WALLET::request)
  KV_SERIALIZE(filename)
  KV_SERIALIZE(password)
  KV_SERIALIZE(language)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(OPEN_WALLET::request)
  KV_SERIALIZE(filename)
  KV_SERIALIZE(password)
  KV_SERIALIZE_OPT(autosave_current, true)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CLOSE_WALLET::request)
  KV_SERIALIZE_OPT(autosave_current, true)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CHANGE_WALLET_PASSWORD::request)
  KV_SERIALIZE(old_password)
  KV_SERIALIZE(new_password)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GENERATE_FROM_KEYS::request)
  KV_SERIALIZE_OPT(restore_height, (uint64_t)0)
  KV_SERIALIZE(filename)
  KV_SERIALIZE(address)
  KV_SERIALIZE(spendkey)
  KV_SERIALIZE(viewkey)
  KV_SERIALIZE(password)
  KV_SERIALIZE_OPT(autosave_current, true)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GENERATE_FROM_KEYS::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(RESTORE_DETERMINISTIC_WALLET::request)
  KV_SERIALIZE_OPT(restore_height, (uint64_t)0)
  KV_SERIALIZE(filename)
  KV_SERIALIZE(seed)
  KV_SERIALIZE(seed_offset)
  KV_SERIALIZE(password)
  KV_SERIALIZE(language)
  KV_SERIALIZE_OPT(autosave_current, true)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(RESTORE_DETERMINISTIC_WALLET::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(seed)
  KV_SERIALIZE(info)
  KV_SERIALIZE(was_deprecated)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IS_MULTISIG::response)
  KV_SERIALIZE(multisig)
  KV_SERIALIZE(ready)
  KV_SERIALIZE(threshold)
  KV_SERIALIZE(total)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(PREPARE_MULTISIG::response)
  KV_SERIALIZE(multisig_info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(MAKE_MULTISIG::request)
  KV_SERIALIZE(multisig_info)
  KV_SERIALIZE(threshold)
  KV_SERIALIZE(password)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(MAKE_MULTISIG::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(multisig_info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXPORT_MULTISIG::response)
  KV_SERIALIZE(info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_MULTISIG::request)
  KV_SERIALIZE(info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(IMPORT_MULTISIG::response)
  KV_SERIALIZE(n_outputs)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(FINALIZE_MULTISIG::request)
  KV_SERIALIZE(password)
  KV_SERIALIZE(multisig_info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(FINALIZE_MULTISIG::response)
  KV_SERIALIZE(address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXCHANGE_MULTISIG_KEYS::request)
  KV_SERIALIZE(password)
  KV_SERIALIZE(multisig_info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(EXCHANGE_MULTISIG_KEYS::response)
  KV_SERIALIZE(address)
  KV_SERIALIZE(multisig_info)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN_MULTISIG::request)
  KV_SERIALIZE(tx_data_hex)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SIGN_MULTISIG::response)
  KV_SERIALIZE(tx_data_hex)
  KV_SERIALIZE(tx_hash_list)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SUBMIT_MULTISIG::request)
  KV_SERIALIZE(tx_data_hex)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SUBMIT_MULTISIG::response)
  KV_SERIALIZE(tx_hash_list)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(GET_VERSION::response)
  KV_SERIALIZE(version)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(STAKE::request)
  KV_SERIALIZE    (subaddr_indices);
  KV_SERIALIZE    (destination);
  KV_SERIALIZE    (amount);
  KV_SERIALIZE    (service_node_key);
  KV_SERIALIZE_OPT(priority,        (uint32_t)0);
  KV_SERIALIZE    (get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay,    false)
  KV_SERIALIZE_OPT(get_tx_hex,      false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(STAKE::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REGISTER_SERVICE_NODE::request)
  KV_SERIALIZE(register_service_node_str);
  KV_SERIALIZE(get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay,    false)
  KV_SERIALIZE_OPT(get_tx_hex,      false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REGISTER_SERVICE_NODE::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REQUEST_STAKE_UNLOCK::request)
  KV_SERIALIZE(service_node_key);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(REQUEST_STAKE_UNLOCK::response)
  KV_SERIALIZE(unlocked)
  KV_SERIALIZE(msg)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CAN_REQUEST_STAKE_UNLOCK::request)
  KV_SERIALIZE(service_node_key);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(CAN_REQUEST_STAKE_UNLOCK::response)
  KV_SERIALIZE(can_unlock)
  KV_SERIALIZE(msg)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(VALIDATE_ADDRESS::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE_OPT(any_net_type, false)
  KV_SERIALIZE_OPT(allow_openalias, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(VALIDATE_ADDRESS::response)
  KV_SERIALIZE(valid)
  KV_SERIALIZE(integrated)
  KV_SERIALIZE(subaddress)
  KV_SERIALIZE(nettype)
  KV_SERIALIZE(openalias_address)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_DAEMON::request)
  KV_SERIALIZE(address)
  KV_SERIALIZE(proxy)
  KV_SERIALIZE_OPT(trusted, false)
  KV_SERIALIZE(ssl_private_key_path)
  KV_SERIALIZE(ssl_certificate_path)
  KV_SERIALIZE(ssl_ca_file)
  KV_SERIALIZE_OPT(ssl_allow_any_cert, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_LOG_LEVEL::request)
  KV_SERIALIZE(level)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_LOG_CATEGORIES::request)
  KV_SERIALIZE(categories)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(SET_LOG_CATEGORIES::response)
  KV_SERIALIZE(categories)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_BUY_MAPPING::request)
  KV_SERIALIZE    (type);
  KV_SERIALIZE    (owner);
  KV_SERIALIZE    (backup_owner);
  KV_SERIALIZE    (name);
  KV_SERIALIZE    (value);
  KV_SERIALIZE_OPT(account_index,   (uint32_t)0);
  KV_SERIALIZE    (subaddr_indices);
  KV_SERIALIZE_OPT(priority,        (uint32_t)0);
  KV_SERIALIZE    (get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay,    false)
  KV_SERIALIZE_OPT(get_tx_hex,      false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_BUY_MAPPING::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_RENEW_MAPPING::request)
  KV_SERIALIZE    (type);
  KV_SERIALIZE    (name);
  KV_SERIALIZE_OPT(account_index,   (uint32_t)0);
  KV_SERIALIZE    (subaddr_indices);
  KV_SERIALIZE_OPT(priority,        (uint32_t)0);
  KV_SERIALIZE    (get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay,    false)
  KV_SERIALIZE_OPT(get_tx_hex,      false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_UPDATE_MAPPING::request)
  KV_SERIALIZE    (type);
  KV_SERIALIZE    (name);
  KV_SERIALIZE    (value);
  KV_SERIALIZE    (owner);
  KV_SERIALIZE    (backup_owner);
  KV_SERIALIZE    (signature);
  KV_SERIALIZE_OPT(account_index,   (uint32_t)0);
  KV_SERIALIZE    (subaddr_indices);
  KV_SERIALIZE_OPT(priority,        (uint32_t)0);
  KV_SERIALIZE    (get_tx_key)
  KV_SERIALIZE_OPT(do_not_relay,    false)
  KV_SERIALIZE_OPT(get_tx_hex,      false)
  KV_SERIALIZE_OPT(get_tx_metadata, false)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_UPDATE_MAPPING::response)
  KV_SERIALIZE(tx_hash)
  KV_SERIALIZE(tx_key)
  KV_SERIALIZE(amount)
  KV_SERIALIZE(fee)
  KV_SERIALIZE(tx_blob)
  KV_SERIALIZE(tx_metadata)
  KV_SERIALIZE(multisig_txset)
  KV_SERIALIZE(unsigned_txset)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_MAKE_UPDATE_SIGNATURE::request)
  KV_SERIALIZE(type);
  KV_SERIALIZE(name);
  KV_SERIALIZE(encrypted_value);
  KV_SERIALIZE(owner);
  KV_SERIALIZE(backup_owner);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_MAKE_UPDATE_SIGNATURE::response)
  KV_SERIALIZE(signature)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_HASH_NAME::request)
  KV_SERIALIZE(type);
  KV_SERIALIZE(name);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_HASH_NAME::response)
  KV_SERIALIZE(name)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_KNOWN_NAMES::request)
  KV_SERIALIZE(decrypt)
  KV_SERIALIZE(include_expired)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_KNOWN_NAMES::known_record)
  KV_SERIALIZE(type)
  KV_SERIALIZE(hashed)
  KV_SERIALIZE(name)
  KV_SERIALIZE(owner)
  KV_SERIALIZE(backup_owner)
  KV_SERIALIZE(encrypted_value)
  KV_SERIALIZE(value)
  KV_SERIALIZE(update_height)
  KV_SERIALIZE(expiration_height)
  KV_SERIALIZE(expired)
  KV_SERIALIZE(txid)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_KNOWN_NAMES::response)
  KV_SERIALIZE(known_names)
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_DECRYPT_VALUE::request)
  KV_SERIALIZE(name);
  KV_SERIALIZE(type);
  KV_SERIALIZE(encrypted_value);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_DECRYPT_VALUE::response)
  KV_SERIALIZE(value)
KV_SERIALIZE_MAP_CODE_END()

KV_SERIALIZE_MAP_CODE_BEGIN(LNS_ENCRYPT_VALUE::request)
  KV_SERIALIZE(name);
  KV_SERIALIZE(type);
  KV_SERIALIZE(value);
KV_SERIALIZE_MAP_CODE_END()


KV_SERIALIZE_MAP_CODE_BEGIN(LNS_ENCRYPT_VALUE::response)
  KV_SERIALIZE(encrypted_value)
KV_SERIALIZE_MAP_CODE_END()

}
