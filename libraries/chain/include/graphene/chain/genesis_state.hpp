#pragma once

#include <graphene/chain/protocol/types.hpp>

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
      string symbol;
      string description;
      uint8_t precision;
      string issuer_name;
      share_type max_supply;
      uint16_t market_fee_percent;
      share_type max_market_fee;
      uint16_t issuer_permissions;
      uint16_t flags;

      struct initial_bitasset_options {
         uint32_t feed_lifetime_sec;
         uint8_t minimum_feeds;
         uint32_t force_settlement_delay_sec;
         uint16_t force_settlement_offset_percent;
         uint16_t maximum_force_settlement_volume;
         string backing_asset_symbol;

         struct initial_collateral_position {
            address owner;
            share_type collateral;
            share_type debt;
         };

         uint16_t maintenance_collateral_ratio;
         vector<initial_collateral_position> collateral_records;
      };
      optional<initial_bitasset_options> bitasset_opts;

      share_type initial_accumulated_fees;
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
   chain_parameters                         initial_parameters;
   vector<initial_account_type>             initial_accounts;
   vector<initial_asset_type>               initial_assets;
   vector<initial_balance_type>             initial_balances;
   vector<initial_vesting_balance_type>     initial_vesting_balances;
   int                                      initial_active_witnesses = GRAPHENE_DEFAULT_NUM_WITNESSES;
   vector<initial_witness_type>             initial_witness_candidates;
   vector<initial_committee_member_type>    initial_committee_candidates;
   vector<initial_worker_type>              initial_worker_candidates;
};
} } // namespace graphene::chain

FC_REFLECT(graphene::chain::genesis_state_type::initial_account_type, (name)(owner_key)(active_key)(is_lifetime_member))

FC_REFLECT(graphene::chain::genesis_state_type::initial_asset_type,
           (symbol)(description)(precision)(issuer_name)(max_supply)(market_fee_percent)
           (issuer_permissions)(flags)(bitasset_opts)(initial_accumulated_fees))

FC_REFLECT(graphene::chain::genesis_state_type::initial_asset_type::initial_bitasset_options,
           (feed_lifetime_sec)(minimum_feeds)(force_settlement_delay_sec)(force_settlement_offset_percent)
           (maximum_force_settlement_volume)(backing_asset_symbol)(maintenance_collateral_ratio)(collateral_records))

FC_REFLECT(graphene::chain::genesis_state_type::initial_asset_type::initial_bitasset_options::initial_collateral_position,
           (collateral)(debt))

FC_REFLECT(graphene::chain::genesis_state_type::initial_balance_type,
           (owner)(asset_symbol)(amount))

FC_REFLECT(graphene::chain::genesis_state_type::initial_vesting_balance_type,
           (owner)(asset_symbol)(amount)(begin_timestamp)(vesting_duration_seconds)(begin_balance))

FC_REFLECT(graphene::chain::genesis_state_type::initial_witness_type, (owner_name)(block_signing_key))

FC_REFLECT(graphene::chain::genesis_state_type::initial_committee_member_type, (owner_name))

FC_REFLECT(graphene::chain::genesis_state_type::initial_worker_type, (owner_name)(daily_pay))

FC_REFLECT(graphene::chain::genesis_state_type,
           (initial_timestamp)(initial_parameters)(initial_accounts)(initial_assets)(initial_balances)
           (initial_vesting_balances)(initial_active_witnesses)(initial_witness_candidates)
           (initial_committee_candidates)(initial_worker_candidates))
