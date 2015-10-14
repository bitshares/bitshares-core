/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#include <graphene/chain/protocol/chain_parameters.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/immutable_chain_parameters.hpp>

#include <fc/crypto/sha256.hpp>

#include <string>
#include <vector>

namespace graphene { namespace chain {
using std::string;
using std::vector;

struct genesis_state_type {
   struct initial_account_type {
      initial_account_type(const string& name = string(),
                           const public_key_type& owner_key = public_key_type(),
                           const public_key_type& active_key = public_key_type(),
                           bool is_lifetime_member = false)
         : name(name),
           owner_key(owner_key),
           active_key(active_key == public_key_type()? owner_key : active_key),
           is_lifetime_member(is_lifetime_member)
      {}
      string name;
      public_key_type owner_key;
      public_key_type active_key;
      bool is_lifetime_member = false;
   };
   struct initial_asset_type {
      struct initial_collateral_position {
         address owner;
         share_type collateral;
         share_type debt;
      };

      string symbol;
      string issuer_name;

      string description;
      uint8_t precision = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS;

      share_type max_supply;
      share_type accumulated_fees;

      bool is_bitasset = false;
      vector<initial_collateral_position> collateral_records;
   };
   struct initial_balance_type {
      address owner;
      string asset_symbol;
      share_type amount;
   };
   struct initial_vesting_balance_type {
      address owner;
      string asset_symbol;
      share_type amount;
      time_point_sec begin_timestamp;
      uint32_t vesting_duration_seconds = 0;
      share_type begin_balance;
   };
   struct initial_witness_type {
      /// Must correspond to one of the initial accounts
      string owner_name;
      public_key_type block_signing_key;
   };
   struct initial_committee_member_type {
      /// Must correspond to one of the initial accounts
      string owner_name;
   };
   struct initial_worker_type {
      /// Must correspond to one of the initial accounts
      string owner_name;
      share_type daily_pay;
   };

   time_point_sec                           initial_timestamp;
   share_type                               max_core_supply = GRAPHENE_MAX_SHARE_SUPPLY;
   chain_parameters                         initial_parameters;
   immutable_chain_parameters               immutable_parameters;
   vector<initial_account_type>             initial_accounts;
   vector<initial_asset_type>               initial_assets;
   vector<initial_balance_type>             initial_balances;
   vector<initial_vesting_balance_type>     initial_vesting_balances;
   uint64_t                                 initial_active_witnesses = GRAPHENE_DEFAULT_MIN_WITNESS_COUNT;
   vector<initial_witness_type>             initial_witness_candidates;
   vector<initial_committee_member_type>    initial_committee_candidates;
   vector<initial_worker_type>              initial_worker_candidates;

   /**
    * Temporary, will be moved elsewhere.
    */
   chain_id_type                            initial_chain_id;

   /**
    * Get the chain_id corresponding to this genesis state.
    *
    * This is the SHA256 serialization of the genesis_state.
    */
   chain_id_type compute_chain_id() const;
};

} } // namespace graphene::chain

FC_REFLECT(graphene::chain::genesis_state_type::initial_account_type, (name)(owner_key)(active_key)(is_lifetime_member))

FC_REFLECT(graphene::chain::genesis_state_type::initial_asset_type,
           (symbol)(issuer_name)(description)(precision)(max_supply)(accumulated_fees)(is_bitasset)(collateral_records))

FC_REFLECT(graphene::chain::genesis_state_type::initial_asset_type::initial_collateral_position,
           (owner)(collateral)(debt))

FC_REFLECT(graphene::chain::genesis_state_type::initial_balance_type,
           (owner)(asset_symbol)(amount))

FC_REFLECT(graphene::chain::genesis_state_type::initial_vesting_balance_type,
           (owner)(asset_symbol)(amount)(begin_timestamp)(vesting_duration_seconds)(begin_balance))

FC_REFLECT(graphene::chain::genesis_state_type::initial_witness_type, (owner_name)(block_signing_key))

FC_REFLECT(graphene::chain::genesis_state_type::initial_committee_member_type, (owner_name))

FC_REFLECT(graphene::chain::genesis_state_type::initial_worker_type, (owner_name)(daily_pay))

FC_REFLECT(graphene::chain::genesis_state_type,
           (initial_timestamp)(max_core_supply)(initial_parameters)(initial_accounts)(initial_assets)(initial_balances)
           (initial_vesting_balances)(initial_active_witnesses)(initial_witness_candidates)
           (initial_committee_candidates)(initial_worker_candidates)
           (initial_chain_id)
           (immutable_parameters))
