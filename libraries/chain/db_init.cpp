/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/delegate_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>
#include <graphene/chain/worker_object.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/asset_evaluator.hpp>
#include <graphene/chain/assert_evaluator.hpp>
#include <graphene/chain/custom_evaluator.hpp>
#include <graphene/chain/delegate_evaluator.hpp>
#include <graphene/chain/global_parameters_evaluator.hpp>
#include <graphene/chain/limit_order_evaluator.hpp>
#include <graphene/chain/proposal_evaluator.hpp>
#include <graphene/chain/call_order_evaluator.hpp>
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/vesting_balance_evaluator.hpp>
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/witness_evaluator.hpp>
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/chain/balance_evaluator.hpp>

#include <fc/uint128.hpp>
#include <fc/crypto/digest.hpp>

#include <boost/algorithm/string.hpp>

namespace graphene { namespace chain {

void database::initialize_evaluators()
{
   _operation_evaluators.resize(255);
   register_evaluator<account_create_evaluator>();
   register_evaluator<account_update_evaluator>();
   register_evaluator<account_upgrade_evaluator>();
   register_evaluator<account_whitelist_evaluator>();
   register_evaluator<delegate_create_evaluator>();
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
   register_evaluator<transfer_evaluator>();
   register_evaluator<override_transfer_evaluator>();
   register_evaluator<asset_fund_fee_pool_evaluator>();
   register_evaluator<asset_publish_feeds_evaluator>();
   register_evaluator<proposal_create_evaluator>();
   register_evaluator<proposal_update_evaluator>();
   register_evaluator<proposal_delete_evaluator>();
   register_evaluator<global_parameters_update_evaluator>();
   register_evaluator<witness_create_evaluator>();
   register_evaluator<witness_withdraw_pay_evaluator>();
   register_evaluator<vesting_balance_create_evaluator>();
   register_evaluator<vesting_balance_withdraw_evaluator>();
   register_evaluator<withdraw_permission_create_evaluator>();
   register_evaluator<withdraw_permission_claim_evaluator>();
   register_evaluator<withdraw_permission_update_evaluator>();
   register_evaluator<withdraw_permission_delete_evaluator>();
   register_evaluator<worker_create_evaluator>();
   register_evaluator<balance_claim_evaluator>();
}

void database::initialize_indexes()
{
   reset_indexes();

   //Protocol object indexes
   add_index< primary_index<asset_index> >();
   add_index< primary_index<force_settlement_index> >();

   auto acnt_index = add_index< primary_index<account_index> >();
   acnt_index->add_secondary_index<account_member_index>();
   acnt_index->add_secondary_index<account_referrer_index>();

   add_index< primary_index<delegate_index> >();
   add_index< primary_index<witness_index> >();
   add_index< primary_index<limit_order_index > >();
   add_index< primary_index<call_order_index > >();

   auto prop_index = add_index< primary_index<proposal_index > >();
   prop_index->add_secondary_index<required_approval_index>();

   add_index< primary_index<withdraw_permission_index > >();
   //add_index< primary_index<vesting_balance_index> >();
   add_index< primary_index<simple_index<vesting_balance_object>> >();
   add_index< primary_index<worker_index> >();
   add_index< primary_index<balance_index> >();

   //Implementation object indexes
   add_index< primary_index<transaction_index                             > >();
   add_index< primary_index<account_balance_index                         > >();
   add_index< primary_index<asset_bitasset_data_index                     > >();
   add_index< primary_index<simple_index<global_property_object          >> >();
   add_index< primary_index<simple_index<dynamic_global_property_object  >> >();
   add_index< primary_index<simple_index<account_statistics_object       >> >();
   add_index< primary_index<simple_index<asset_dynamic_data_object       >> >();
   add_index< primary_index<flat_index<  block_summary_object            >> >();
   add_index< primary_index<simple_index<witness_schedule_object         >> >();
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
         n.statistics = create<account_statistics_object>( [&](account_statistics_object& b){}).id;
      });
   FC_ASSERT(committee_account.get_id() == GRAPHENE_COMMITTEE_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "witness-account";
       a.statistics = create<account_statistics_object>([](account_statistics_object&){}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_WITNESS_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_WITNESS_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "relaxed-committee-account";
       a.statistics = create<account_statistics_object>([](account_statistics_object&){}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_RELAXED_COMMITTEE_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_RELAXED_COMMITTEE_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "null-account";
       a.statistics = create<account_statistics_object>([](account_statistics_object&){}).id;
       a.owner.weight_threshold = 1;
       a.active.weight_threshold = 1;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_NULL_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = 0;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT;
   }).get_id() == GRAPHENE_NULL_ACCOUNT);
   FC_ASSERT(create<account_object>([this](account_object& a) {
       a.name = "temp-account";
       a.statistics = create<account_statistics_object>([](account_statistics_object&){}).id;
       a.owner.weight_threshold = 0;
       a.active.weight_threshold = 0;
       a.registrar = a.lifetime_referrer = a.referrer = GRAPHENE_TEMP_ACCOUNT;
       a.membership_expiration_date = time_point_sec::maximum();
       a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
       a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
   }).get_id() == GRAPHENE_TEMP_ACCOUNT);

   // Create core asset
   const asset_dynamic_data_object& dyn_asset =
      create<asset_dynamic_data_object>([&](asset_dynamic_data_object& a) {
         a.current_supply = GRAPHENE_MAX_SHARE_SUPPLY;
      });
   const asset_object& core_asset =
     create<asset_object>( [&]( asset_object& a ) {
         a.symbol = GRAPHENE_SYMBOL;
         a.options.max_supply = GRAPHENE_MAX_SHARE_SUPPLY;
         a.precision = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS;
         a.options.flags = 0;
         a.options.issuer_permissions = 0;
         a.issuer = committee_account.id;
         a.options.core_exchange_rate.base.amount = 1;
         a.options.core_exchange_rate.base.asset_id = 0;
         a.options.core_exchange_rate.quote.amount = 1;
         a.options.core_exchange_rate.quote.asset_id = 0;
         a.dynamic_asset_data_id = dyn_asset.id;
      });
   assert( asset_id_type(core_asset.id) == asset().asset_id );
   assert( get_balance(account_id_type(), asset_id_type()) == asset(dyn_asset.current_supply) );
   (void)core_asset;

   // Create global properties
   create<global_property_object>([&](global_property_object& p) {
       p.chain_id = fc::digest(genesis_state);
       p.parameters = genesis_state.initial_parameters;
       // Set fees to zero initially, so that genesis initialization needs not pay them
       // We'll fix it at the end of the function
       p.parameters.current_fees.set_all_fees(0);

   });
   create<dynamic_global_property_object>([&](dynamic_global_property_object& p) {
      p.time = genesis_state.initial_timestamp;
      p.witness_budget = 0;
   });
   create<block_summary_object>([&](block_summary_object&) {});

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
   auto get_asset_id = [&assets_by_symbol](const string& symbol) {
      auto itr = assets_by_symbol.find(symbol);
      FC_ASSERT(itr != assets_by_symbol.end(),
                "Unable to find asset '${sym}'. Did you forget to add a record for it to initial_assets?",
                ("sym", symbol));
      return itr->get_id();
   };

   // Create initial assets
   for( const genesis_state_type::initial_asset_type& asset : genesis_state.initial_assets )
   {
      asset_dynamic_data_id_type dynamic_data_id;
      optional<asset_bitasset_data_id_type> bitasset_data_id;
      if( asset.bitasset_options.valid() )
      {
         share_type total_allocated;
         asset_id_type new_asset_id = get_index_type<asset_index>().get_next_id();
         asset_id_type collateral_asset_id = get_asset_id(asset.bitasset_options->backing_asset_symbol);

         int collateral_holder_number = 0;
         for( const auto& collateral_rec : asset.bitasset_options->collateral_records )
         {
            account_create_operation cop;
            cop.name = asset.symbol + "-collateral-holder-" + std::to_string(collateral_holder_number);
            boost::algorithm::to_lower(cop.name);
            cop.registrar = GRAPHENE_TEMP_ACCOUNT;
            cop.owner = authority(1, collateral_rec.owner, 1);
            cop.active = cop.owner;
            account_id_type owner_account_id = apply_operation(genesis_eval_state, cop).get<object_id_type>();

            create<call_order_object>([&](call_order_object& c) {
               c.borrower = owner_account_id;
               c.collateral = collateral_rec.collateral;
               c.debt = collateral_rec.debt;
               c.call_price = price::call_price(chain::asset(c.debt, new_asset_id),
                                                chain::asset(c.collateral, collateral_asset_id),
                                                asset.bitasset_options->maintenance_collateral_ratio);
            });

            total_allocated += collateral_rec.debt;
         }

         bitasset_data_id = create<asset_bitasset_data_object>([&](asset_bitasset_data_object& b) {
            b.options.feed_lifetime_sec = asset.bitasset_options->feed_lifetime_sec;
            b.options.minimum_feeds = asset.bitasset_options->minimum_feeds;
            b.options.force_settlement_delay_sec = asset.bitasset_options->force_settlement_delay_sec;
            b.options.force_settlement_offset_percent = asset.bitasset_options->force_settlement_offset_percent;
            b.options.maximum_force_settlement_volume = asset.bitasset_options->maximum_force_settlement_volume;
            b.options.short_backing_asset = get_asset_id(asset.bitasset_options->backing_asset_symbol);
         }).id;

         dynamic_data_id = create<asset_dynamic_data_object>([&](asset_dynamic_data_object& d) {
            d.current_supply = total_allocated;
            d.accumulated_fees = asset.initial_accumulated_fees;
         }).id;
      } else
         dynamic_data_id = create<asset_dynamic_data_object>([&](asset_dynamic_data_object& d) {
            d.accumulated_fees = asset.initial_accumulated_fees;
         }).id;

      create<asset_object>([&](asset_object& a) {
         a.symbol = asset.symbol;
         a.options.description = asset.description;
         a.precision = asset.precision;
         a.issuer = get_account_id(asset.issuer_name);
         a.options.max_supply = asset.max_supply;
         a.options.market_fee_percent = asset.market_fee_percent;
         a.options.max_market_fee = asset.max_market_fee;
         a.options.issuer_permissions = asset.issuer_permissions;
         a.options.flags = asset.flags;

         a.dynamic_asset_data_id = dynamic_data_id;
         a.bitasset_data_id = bitasset_data_id;
      });
   }

   // Create initial balances
   share_type total_allocation;
   for( const auto& handout : genesis_state.initial_balances )
   {
      create<balance_object>([&handout,&assets_by_symbol,total_allocation](balance_object& b) {
         b.balance = asset(handout.amount, assets_by_symbol.find(handout.asset_symbol)->get_id());
         b.owner = handout.owner;
      });
      total_allocation += handout.amount;
   }

   // Create initial vesting balances
   for( const genesis_state_type::initial_vesting_balance_type& vest : genesis_state.initial_vesting_balances )
   {
      create<balance_object>([&](balance_object& b) {
         b.owner = vest.owner;
         b.balance = asset(vest.amount, assets_by_symbol.find(vest.asset_symbol)->get_id());

         linear_vesting_policy policy;
         policy.begin_timestamp = vest.begin_timestamp;
         policy.vesting_cliff_seconds = 0;
         policy.vesting_duration_seconds = vest.vesting_duration_seconds;
         policy.begin_balance = vest.begin_balance;

         b.vesting_policy = std::move(policy);
      });
      total_allocation += vest.amount;
   }

   // Set current supply based on allocations, if they happened
   if( total_allocation > 0 )
   {
      modify(dyn_asset, [total_allocation](asset_dynamic_data_object& d) {
         d.current_supply = total_allocation;
      });
      adjust_balance(GRAPHENE_COMMITTEE_ACCOUNT, -get_balance(GRAPHENE_COMMITTEE_ACCOUNT,{}));
   }

   // Create initial witnesses
   std::for_each(genesis_state.initial_witness_candidates.begin(), genesis_state.initial_witness_candidates.end(),
                 [&](const genesis_state_type::initial_witness_type& witness) {
      witness_create_operation op;
      op.witness_account = get_account_id(witness.owner_name);
      op.block_signing_key = witness.block_signing_key;
      op.initial_secret = secret_hash_type::hash( secret_hash_type() );
      apply_operation(genesis_eval_state, op);
   });

   // Create initial committee members
   std::for_each(genesis_state.initial_committee_candidates.begin(), genesis_state.initial_committee_candidates.end(),
                 [&](const genesis_state_type::initial_committee_member_type& member) {
      delegate_create_operation op;
      op.delegate_account = get_account_id(member.owner_name);
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
       op.initializer = vesting_balance_worker_type::initializer(0);

       apply_operation(genesis_eval_state, std::move(op));
   });

   // Set active witnesses
   modify(get_global_properties(), [&](global_property_object& p) {
      for( int i = 0; i < genesis_state.initial_active_witnesses; ++i )
      {
         p.active_witnesses.insert(i);
         p.witness_accounts.insert(get(witness_id_type(i)).witness_account);
      }
   });

   // Initialize witness schedule
#ifndef NDEBUG
   const witness_schedule_object& wso =
#endif
   create<witness_schedule_object>([&](witness_schedule_object& _wso)
   {
      memset(_wso.rng_seed.begin(), 0, _wso.rng_seed.size());

      witness_scheduler_rng rng(_wso.rng_seed.begin(), GRAPHENE_NEAR_SCHEDULE_CTR_IV);

      auto init_witnesses = get_global_properties().active_witnesses;

      _wso.scheduler = witness_scheduler();
      _wso.scheduler._min_token_count = std::max(int(init_witnesses.size()) / 2, 1);
      _wso.scheduler.update(init_witnesses);

      for( size_t i=0; i<init_witnesses.size(); ++i )
         _wso.scheduler.produce_schedule(rng);

      _wso.last_scheduling_block = 0;
   });
   assert( wso.id == witness_schedule_id_type() );

   // Enable fees
   modify(get_global_properties(), [&genesis_state](global_property_object& p) {
      p.parameters.current_fees = genesis_state.initial_parameters.current_fees;
   });

   _undo_db.enable();
} FC_CAPTURE_AND_RETHROW() }

} }
