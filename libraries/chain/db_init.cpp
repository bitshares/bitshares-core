/*
 * Copyright (c) 2017 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <graphene/chain/database.hpp>
#include <graphene/chain/fba_accumulator_id.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/budget_record_object.hpp>
#include <graphene/chain/buyback_object.hpp>
#include <graphene/chain/chain_property_object.hpp>
#include <graphene/chain/committee_member_object.hpp>
#include <graphene/chain/confidential_object.hpp>
#include <graphene/chain/fba_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/special_authority_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/asset_evaluator.hpp>
#include <graphene/chain/assert_evaluator.hpp>
#include <graphene/chain/balance_evaluator.hpp>
#include <graphene/chain/committee_member_evaluator.hpp>
#include <graphene/chain/confidential_evaluator.hpp>
#include <graphene/chain/custom_evaluator.hpp>
#include <graphene/chain/market_evaluator.hpp>
#include <graphene/chain/proposal_evaluator.hpp>
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/vesting_balance_evaluator.hpp>
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/witness_evaluator.hpp>
#include <graphene/chain/worker_evaluator.hpp>

#include <graphene/chain/protocol/fee_schedule.hpp>

#include <fc/smart_ref_impl.hpp>
#include <fc/uint128.hpp>
#include <fc/crypto/digest.hpp>

#include <boost/algorithm/string.hpp>

namespace graphene { namespace chain {

// C++ requires that static class variables declared and initialized
// in headers must also have a definition in a single source file,
// else linker errors will occur [1].
//
// The purpose of this source file is to collect such definitions in
// a single place.
//
// [1] http://stackoverflow.com/questions/8016780/undefined-reference-to-static-constexpr-char

const uint8_t account_object::space_id;
const uint8_t account_object::type_id;

const uint8_t asset_object::space_id;
const uint8_t asset_object::type_id;

const uint8_t block_summary_object::space_id;
const uint8_t block_summary_object::type_id;

const uint8_t call_order_object::space_id;
const uint8_t call_order_object::type_id;

const uint8_t committee_member_object::space_id;
const uint8_t committee_member_object::type_id;

const uint8_t force_settlement_object::space_id;
const uint8_t force_settlement_object::type_id;

const uint8_t global_property_object::space_id;
const uint8_t global_property_object::type_id;

const uint8_t limit_order_object::space_id;
const uint8_t limit_order_object::type_id;

const uint8_t operation_history_object::space_id;
const uint8_t operation_history_object::type_id;

const uint8_t proposal_object::space_id;
const uint8_t proposal_object::type_id;

const uint8_t transaction_object::space_id;
const uint8_t transaction_object::type_id;

const uint8_t vesting_balance_object::space_id;
const uint8_t vesting_balance_object::type_id;

const uint8_t withdraw_permission_object::space_id;
const uint8_t withdraw_permission_object::type_id;

const uint8_t witness_object::space_id;
const uint8_t witness_object::type_id;

const uint8_t worker_object::space_id;
const uint8_t worker_object::type_id;


void database::initialize_evaluators()
{
   _operation_evaluators.resize(255);
   register_evaluator<account_create_evaluator>();
   register_evaluator<account_update_evaluator>();
   register_evaluator<account_upgrade_evaluator>();
   register_evaluator<account_whitelist_evaluator>();
   register_evaluator<committee_member_create_evaluator>();
   register_evaluator<committee_member_update_evaluator>();
   register_evaluator<committee_member_update_global_parameters_evaluator>();
   register_evaluator<custom_evaluator>();
   register_evaluator<asset_create_evaluator>();
   register_evaluator<asset_issue_evaluator>();
   register_evaluator<asset_reserve_evaluator>();
   register_evaluator<asset_update_evaluator>();
   register_evaluator<asset_update_bitasset_evaluator>();
   register_evaluator<asset_update_feed_producers_evaluator>();
   register_evaluator<asset_settle_evaluator>();
   register_evaluator<asset_global_settle_evaluator>();
   register_evaluator<assert_evaluator>();
   register_evaluator<limit_order_create_evaluator>();
   register_evaluator<limit_order_cancel_evaluator>();
   register_evaluator<call_order_update_evaluator>();
   register_evaluator<bid_collateral_evaluator>();
   register_evaluator<transfer_evaluator>();
   register_evaluator<override_transfer_evaluator>();
   register_evaluator<asset_fund_fee_pool_evaluator>();
   register_evaluator<asset_publish_feeds_evaluator>();
   register_evaluator<proposal_create_evaluator>();
   register_evaluator<proposal_update_evaluator>();
   register_evaluator<proposal_delete_evaluator>();
   register_evaluator<vesting_balance_create_evaluator>();
   register_evaluator<vesting_balance_withdraw_evaluator>();
   register_evaluator<witness_create_evaluator>();
   register_evaluator<witness_update_evaluator>();
   register_evaluator<withdraw_permission_create_evaluator>();
   register_evaluator<withdraw_permission_claim_evaluator>();
   register_evaluator<withdraw_permission_update_evaluator>();
   register_evaluator<withdraw_permission_delete_evaluator>();
   register_evaluator<worker_create_evaluator>();
   register_evaluator<balance_claim_evaluator>();
   register_evaluator<transfer_to_blind_evaluator>();
   register_evaluator<transfer_from_blind_evaluator>();
   register_evaluator<blind_transfer_evaluator>();
   register_evaluator<asset_claim_fees_evaluator>();
   register_evaluator<asset_update_issuer_evaluator>();
   register_evaluator<asset_claim_pool_evaluator>();
}

void database::initialize_indexes()
{
   reset_indexes();
   _undo_db.set_max_size( GRAPHENE_MIN_UNDO_HISTORY );

   //Protocol object indexes
   add_index< primary_index<asset_index> >();
   add_index< primary_index<force_settlement_index> >();

   auto acnt_index = add_index< primary_index<account_index> >();
   acnt_index->add_secondary_index<account_member_index>();
   acnt_index->add_secondary_index<account_referrer_index>();

   add_index< primary_index<committee_member_index> >();
   add_index< primary_index<witness_index> >();
   add_index< primary_index<limit_order_index > >();
   add_index< primary_index<call_order_index > >();

   auto prop_index = add_index< primary_index<proposal_index > >();
   prop_index->add_secondary_index<required_approval_index>();

   add_index< primary_index<withdraw_permission_index > >();
   add_index< primary_index<vesting_balance_index> >();
   add_index< primary_index<worker_index> >();
   add_index< primary_index<balance_index> >();
   add_index< primary_index<blinded_balance_index> >();

   //Implementation object indexes
   add_index< primary_index<transaction_index                             > >();
   add_index< primary_index<account_balance_index                         > >();
   add_index< primary_index<asset_bitasset_data_index                     > >();
   add_index< primary_index<simple_index<global_property_object          >> >();
   add_index< primary_index<simple_index<dynamic_global_property_object  >> >();
   add_index< primary_index<simple_index<account_statistics_object       >> >();
   add_index< primary_index<simple_index<asset_dynamic_data_object       >> >();
   add_index< primary_index<simple_index<block_summary_object            >> >();
   add_index< primary_index<simple_index<chain_property_object          > > >();
   add_index< primary_index<simple_index<witness_schedule_object        > > >();
   add_index< primary_index<simple_index<budget_record_object           > > >();
   add_index< primary_index< special_authority_index                      > >();
   add_index< primary_index< buyback_index                                > >();
   add_index< primary_index<collateral_bid_index                          > >();

   add_index< primary_index< simple_index< fba_accumulator_object       > > >();
}

void database::init_genesis(const genesis_state_type& genesis_state)
{ try {
   FC_ASSERT( genesis_state.initial_timestamp != time_point_sec(), "Must initialize genesis timestamp." );
   FC_ASSERT( genesis_state.initial_timestamp.sec_since_epoch() % GRAPHENE_DEFAULT_BLOCK_INTERVAL == 0,
              "Genesis timestamp must be divisible by GRAPHENE_DEFAULT_BLOCK_INTERVAL." );
   FC_ASSERT(genesis_state.initial_witness_candidates.size() > 0,
             "Cannot start a chain with zero witnesses.");
   FC_ASSERT(genesis_state.initial_active_witnesses <= genesis_state.initial_witness_candidates.size(),
             "initial_active_witnesses is larger than the number of candidate witnesses.");

   _undo_db.disable();
   struct auth_inhibitor {
      auth_inhibitor(database& db) : db(db), old_flags(db.node_properties().skip_flags)
      { db.node_properties().skip_flags |= skip_authority_check; }
      ~auth_inhibitor()
      { db.node_properties().skip_flags = old_flags; }
   private:
      database& db;
      uint32_t old_flags;
   } inhibitor(*this);

   transaction_evaluation_state genesis_eval_state(this);

   // Create blockchain accounts
   fc::ecc::private_key null_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("null_key")));
   create<account_balance_object>([](account_balance_object& b) {
      b.balance = GRAPHENE_MAX_SHARE_SUPPLY;
   });
   const account_object& committee_account =
      create<account_object>( [&](account_object& n) {
         n.membership_expiration_date = time_point_sec::maximum();
         n.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
         n.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
         n.owner.weight_threshold = 1;
         n.active.weight_threshold = 1;
         n.name = "committee-account";
         n.statistics = create<account_statistics_object>( [&](account_statistics_object& s){ s.owner = n.id; }).id;
      });
   FC_ASSERT(committee_account.get_id() == GRAPHENE_COMMITTEE_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "witness-account";
       a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_WITNESS_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_WITNESS_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "relaxed-committee-account";
       a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_RELAXED_COMMITTEE_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_RELAXED_COMMITTEE_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "null-account";
       a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_NULL_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = 0;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT;
   }).get_id() == GRAPHENE_NULL_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "temp-account";
       a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
       a.owner.weight_threshold = 0;
       a.active.weight_threshold = 0;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_TEMP_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_TEMP_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "proxy-to-self";
       a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_NULL_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = 0;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT;
   }).get_id() == GRAPHENE_PROXY_TO_SELF_ACCOUNT);

   // Create more special accounts
   while( true )
   {
      uint64_t id = get_index<account_object>().get_next_id().instance();
      if( id >= genesis_state.immutable_parameters.num_special_accounts )
         break;
      const account_object& acct = create<account_object>([&](account_object& a) {
          a.name = "special-account-" + std::to_string(id);
          a.statistics = create<account_statistics_object>([&](account_statistics_object& s){s.owner = a.id;}).id;
          a.owner.weight_threshold = 1;
          a.active.weight_threshold = 1;
          a.registrar = a.lifetime_referrer = a.referrer = account_id_type(id);
          a.membership_expiration_date = time_point_sec::maximum();
          a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
          a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
      });
      FC_ASSERT( acct.get_id() == account_id_type(id) );
      remove( acct );
   }

   // Create core asset
   const asset_dynamic_data_object& dyn_asset =
      create<asset_dynamic_data_object>([&](asset_dynamic_data_object& a) {
         a.current_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      });
   const asset_object& core_asset =
     create<asset_object>( [&]( asset_object& a ) {
         a.symbol = GRAPHENE_SYMBOL;
         a.options.max_supply = genesis_state.max_core_supply;
         a.precision = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS;
         a.options.flags = 0;
         a.options.issuer_permissions = 0;
         a.issuer = GRAPHENE_NULL_ACCOUNT;
         a.options.core_exchange_rate.base.amount = 1;
         a.options.core_exchange_rate.base.asset_id = asset_id_type(0);
         a.options.core_exchange_rate.quote.amount = 1;
         a.options.core_exchange_rate.quote.asset_id = asset_id_type(0);
         a.dynamic_asset_data_id = dyn_asset.id;
      });
   assert( asset_id_type(core_asset.id) == asset().asset_id );
   assert( get_balance(account_id_type(), asset_id_type()) == asset(dyn_asset.current_supply) );
   // Create more special assets
   while( true )
   {
      uint64_t id = get_index<asset_object>().get_next_id().instance();
      if( id >= genesis_state.immutable_parameters.num_special_assets )
         break;
      const asset_dynamic_data_object& dyn_asset =
         create<asset_dynamic_data_object>([&](asset_dynamic_data_object& a) {
            a.current_supply = 0;
         });
      const asset_object& asset_obj = create<asset_object>( [&]( asset_object& a ) {
         a.symbol = "SPECIAL" + std::to_string( id );
         a.options.max_supply = 0;
         a.precision = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS;
         a.options.flags = 0;
         a.options.issuer_permissions = 0;
         a.issuer = GRAPHENE_NULL_ACCOUNT;
         a.options.core_exchange_rate.base.amount = 1;
         a.options.core_exchange_rate.base.asset_id = asset_id_type(0);
         a.options.core_exchange_rate.quote.amount = 1;
         a.options.core_exchange_rate.quote.asset_id = asset_id_type(0);
         a.dynamic_asset_data_id = dyn_asset.id;
      });
      FC_ASSERT( asset_obj.get_id() == asset_id_type(id) );
      remove( asset_obj );
   }

   chain_id_type chain_id = genesis_state.compute_chain_id();

   // Create global properties
   create<global_property_object>([&](global_property_object& p) {
       p.parameters = genesis_state.initial_parameters;
       // Set fees to zero initially, so that genesis initialization needs not pay them
       // We'll fix it at the end of the function
       p.parameters.current_fees->zero_all_fees();

   });
   create<dynamic_global_property_object>([&](dynamic_global_property_object& p) {
      p.time = genesis_state.initial_timestamp;
      p.dynamic_flags = 0;
      p.witness_budget = 0;
      p.recent_slots_filled = fc::uint128::max_value();
   });

   FC_ASSERT( (genesis_state.immutable_parameters.min_witness_count & 1) == 1, "min_witness_count must be odd" );
   FC_ASSERT( (genesis_state.immutable_parameters.min_committee_member_count & 1) == 1, "min_committee_member_count must be odd" );

   create<chain_property_object>([&](chain_property_object& p)
   {
      p.chain_id = chain_id;
      p.immutable_parameters = genesis_state.immutable_parameters;
   } );
   for (uint32_t i = 0; i <= 0x10000; i++)
      create<block_summary_object>( [&]( block_summary_object&) {});

   // Create initial accounts
   for( const auto& account : genesis_state.initial_accounts )
   {
      account_create_operation cop;
      cop.name = account.name;
      cop.registrar = GRAPHENE_TEMP_ACCOUNT;
      cop.owner = authority(1, account.owner_key, 1);
      if( account.active_key == public_key_type() )
      {
         cop.active = cop.owner;
         cop.options.memo_key = account.owner_key;
      }
      else
      {
         cop.active = authority(1, account.active_key, 1);
         cop.options.memo_key = account.active_key;
      }
      account_id_type account_id(apply_operation(genesis_eval_state, cop).get<object_id_type>());

      if( account.is_lifetime_member )
      {
          account_upgrade_operation op;
          op.account_to_upgrade = account_id;
          op.upgrade_to_lifetime_member = true;
          apply_operation(genesis_eval_state, op);
      }
   }

   // Helper function to get account ID by name
   const auto& accounts_by_name = get_index_type<account_index>().indices().get<by_name>();
   auto get_account_id = [&accounts_by_name](const string& name) {
      auto itr = accounts_by_name.find(name);
      FC_ASSERT(itr != accounts_by_name.end(),
                "Unable to find account '${acct}'. Did you forget to add a record for it to initial_accounts?",
                ("acct", name));
      return itr->get_id();
   };

   // Helper function to get asset ID by symbol
   const auto& assets_by_symbol = get_index_type<asset_index>().indices().get<by_symbol>();
   const auto get_asset_id = [&assets_by_symbol](const string& symbol) {
      auto itr = assets_by_symbol.find(symbol);
      FC_ASSERT(itr != assets_by_symbol.end(),
                "Unable to find asset '${sym}'. Did you forget to add a record for it to initial_assets?",
                ("sym", symbol));
      return itr->get_id();
   };

   map<asset_id_type, share_type> total_supplies;
   map<asset_id_type, share_type> total_debts;

   // Create initial assets
   for( const genesis_state_type::initial_asset_type& asset : genesis_state.initial_assets )
   {
      asset_id_type new_asset_id = get_index_type<asset_index>().get_next_id();
      total_supplies[ new_asset_id ] = 0;

      asset_dynamic_data_id_type dynamic_data_id;
      optional<asset_bitasset_data_id_type> bitasset_data_id;
      if( asset.is_bitasset )
      {
         int collateral_holder_number = 0;
         total_debts[ new_asset_id ] = 0;
         for( const auto& collateral_rec : asset.collateral_records )
         {
            account_create_operation cop;
            cop.name = asset.symbol + "-collateral-holder-" + std::to_string(collateral_holder_number);
            boost::algorithm::to_lower(cop.name);
            cop.registrar = GRAPHENE_TEMP_ACCOUNT;
            cop.owner = authority(1, collateral_rec.owner, 1);
            cop.active = cop.owner;
            account_id_type owner_account_id = apply_operation(genesis_eval_state, cop).get<object_id_type>();

            modify( owner_account_id(*this).statistics(*this), [&]( account_statistics_object& o ) {
                    o.total_core_in_orders = collateral_rec.collateral;
                    });

            create<call_order_object>([&](call_order_object& c) {
               c.borrower = owner_account_id;
               c.collateral = collateral_rec.collateral;
               c.debt = collateral_rec.debt;
               c.call_price = price::call_price(chain::asset(c.debt, new_asset_id),
                                                chain::asset(c.collateral, core_asset.id),
                                                GRAPHENE_DEFAULT_MAINTENANCE_COLLATERAL_RATIO);
            });

            total_supplies[ asset_id_type(0) ] += collateral_rec.collateral;
            total_debts[ new_asset_id ] += collateral_rec.debt;
            ++collateral_holder_number;
         }

         bitasset_data_id = create<asset_bitasset_data_object>([&](asset_bitasset_data_object& b) {
            b.options.short_backing_asset = core_asset.id;
            b.options.minimum_feeds = GRAPHENE_DEFAULT_MINIMUM_FEEDS;
         }).id;
      }

      dynamic_data_id = create<asset_dynamic_data_object>([&](asset_dynamic_data_object& d) {
         d.accumulated_fees = asset.accumulated_fees;
      }).id;

      total_supplies[ new_asset_id ] += asset.accumulated_fees;

      create<asset_object>([&](asset_object& a) {
         a.symbol = asset.symbol;
         a.options.description = asset.description;
         a.precision = asset.precision;
         string issuer_name = asset.issuer_name;
         a.issuer = get_account_id(issuer_name);
         a.options.max_supply = asset.max_supply;
         a.options.flags = witness_fed_asset;
         a.options.issuer_permissions = charge_market_fee | override_authority | white_list | transfer_restricted | disable_confidential |
                                       ( asset.is_bitasset ? disable_force_settle | global_settle | witness_fed_asset | committee_fed_asset : 0 );
         a.dynamic_asset_data_id = dynamic_data_id;
         a.bitasset_data_id = bitasset_data_id;
      });
   }

   // Create initial balances
   share_type total_allocation;
   for( const auto& handout : genesis_state.initial_balances )
   {
      const auto asset_id = get_asset_id(handout.asset_symbol);
      create<balance_object>([&handout,total_allocation,asset_id](balance_object& b) {
         b.balance = asset(handout.amount, asset_id);
         b.owner = handout.owner;
      });

      total_supplies[ asset_id ] += handout.amount;
   }

   // Create initial vesting balances
   for( const genesis_state_type::initial_vesting_balance_type& vest : genesis_state.initial_vesting_balances )
   {
      const auto asset_id = get_asset_id(vest.asset_symbol);
      create<balance_object>([&](balance_object& b) {
         b.owner = vest.owner;
         b.balance = asset(vest.amount, asset_id);

         linear_vesting_policy policy;
         policy.begin_timestamp = vest.begin_timestamp;
         policy.vesting_cliff_seconds = 0;
         policy.vesting_duration_seconds = vest.vesting_duration_seconds;
         policy.begin_balance = vest.begin_balance;

         b.vesting_policy = std::move(policy);
      });

      total_supplies[ asset_id ] += vest.amount;
   }

   if( total_supplies[ asset_id_type(0) ] > 0 )
   {
       adjust_balance(GRAPHENE_COMMITTEE_ACCOUNT, -get_balance(GRAPHENE_COMMITTEE_ACCOUNT,{}));
   }
   else
   {
       total_supplies[ asset_id_type(0) ] = GRAPHENE_MAX_SHARE_SUPPLY;
   }

   const auto& idx = get_index_type<asset_index>().indices().get<by_symbol>();
   auto it = idx.begin();
   bool has_imbalanced_assets = false;

   while( it != idx.end() )
   {
      if( it->bitasset_data_id.valid() )
      {
         auto supply_itr = total_supplies.find( it->id );
         auto debt_itr = total_debts.find( it->id );
         FC_ASSERT( supply_itr != total_supplies.end() );
         FC_ASSERT( debt_itr != total_debts.end() );
         if( supply_itr->second != debt_itr->second )
         {
            has_imbalanced_assets = true;
            elog( "Genesis for asset ${aname} is not balanced\n"
                  "   Debt is ${debt}\n"
                  "   Supply is ${supply}\n",
                  ("debt", debt_itr->second)
                  ("supply", supply_itr->second)
                );
         }
      }
      ++it;
   }
   FC_ASSERT( !has_imbalanced_assets );

   // Save tallied supplies
   for( const auto& item : total_supplies )
   {
       const auto asset_id = item.first;
       const auto total_supply = item.second;

       modify( get( asset_id ), [ & ]( asset_object& asset ) {
           modify( get( asset.dynamic_asset_data_id ), [ & ]( asset_dynamic_data_object& asset_data ) {
               asset_data.current_supply = total_supply;
           } );
       } );
   }

   // Create special witness account
   const witness_object& wit = create<witness_object>([&](witness_object& w) {});
   FC_ASSERT( wit.id == GRAPHENE_NULL_WITNESS );
   remove(wit);

   // Create initial witnesses
   std::for_each(genesis_state.initial_witness_candidates.begin(), genesis_state.initial_witness_candidates.end(),
                 [&](const genesis_state_type::initial_witness_type& witness) {
      witness_create_operation op;
      op.witness_account = get_account_id(witness.owner_name);
      op.block_signing_key = witness.block_signing_key;
      apply_operation(genesis_eval_state, op);
   });

   // Create initial committee members
   std::for_each(genesis_state.initial_committee_candidates.begin(), genesis_state.initial_committee_candidates.end(),
                 [&](const genesis_state_type::initial_committee_member_type& member) {
      committee_member_create_operation op;
      op.committee_member_account = get_account_id(member.owner_name);
      apply_operation(genesis_eval_state, op);
   });

   // Create initial workers
   std::for_each(genesis_state.initial_worker_candidates.begin(), genesis_state.initial_worker_candidates.end(),
                  [&](const genesis_state_type::initial_worker_type& worker)
   {
       worker_create_operation op;
       op.owner = get_account_id(worker.owner_name);
       op.work_begin_date = genesis_state.initial_timestamp;
       op.work_end_date = time_point_sec::maximum();
       op.daily_pay = worker.daily_pay;
       op.name = "Genesis-Worker-" + worker.owner_name;
       op.initializer = vesting_balance_worker_initializer{uint16_t(0)};

       apply_operation(genesis_eval_state, std::move(op));
   });

   // Set active witnesses
   modify(get_global_properties(), [&](global_property_object& p) {
      for( uint32_t i = 1; i <= genesis_state.initial_active_witnesses; ++i )
      {
         p.active_witnesses.insert(witness_id_type(i));
      }
   });

   // Enable fees
   modify(get_global_properties(), [&genesis_state](global_property_object& p) {
      p.parameters.current_fees = genesis_state.initial_parameters.current_fees;
   });

   // Create witness scheduler
   create<witness_schedule_object>([&]( witness_schedule_object& wso )
   {
      for( const witness_id_type& wid : get_global_properties().active_witnesses )
         wso.current_shuffled_witnesses.push_back( wid );
   });

   // Create FBA counters
   create<fba_accumulator_object>([&]( fba_accumulator_object& acc )
   {
      FC_ASSERT( acc.id == fba_accumulator_id_type( fba_accumulator_id_transfer_to_blind ) );
      acc.accumulated_fba_fees = 0;
#ifdef GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET
      acc.designated_asset = GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET;
#endif
   });

   create<fba_accumulator_object>([&]( fba_accumulator_object& acc )
   {
      FC_ASSERT( acc.id == fba_accumulator_id_type( fba_accumulator_id_blind_transfer ) );
      acc.accumulated_fba_fees = 0;
#ifdef GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET
      acc.designated_asset = GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET;
#endif
   });

   create<fba_accumulator_object>([&]( fba_accumulator_object& acc )
   {
      FC_ASSERT( acc.id == fba_accumulator_id_type( fba_accumulator_id_transfer_from_blind ) );
      acc.accumulated_fba_fees = 0;
#ifdef GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET
      acc.designated_asset = GRAPHENE_FBA_STEALTH_DESIGNATED_ASSET;
#endif
   });

   FC_ASSERT( get_index<fba_accumulator_object>().get_next_id() == fba_accumulator_id_type( fba_accumulator_id_count ) );

   debug_dump();

   _undo_db.enable();
} FC_CAPTURE_AND_RETHROW() }

} }
