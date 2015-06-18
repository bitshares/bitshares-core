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
#include <graphene/chain/key_object.hpp>
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
#include <graphene/chain/custom_evaluator.hpp>
#include <graphene/chain/delegate_evaluator.hpp>
#include <graphene/chain/global_parameters_evaluator.hpp>
#include <graphene/chain/key_evaluator.hpp>
#include <graphene/chain/limit_order_evaluator.hpp>
#include <graphene/chain/proposal_evaluator.hpp>
#include <graphene/chain/call_order_evaluator.hpp>
#include <graphene/chain/transfer_evaluator.hpp>
#include <graphene/chain/vesting_balance_evaluator.hpp>
#include <graphene/chain/withdraw_permission_evaluator.hpp>
#include <graphene/chain/witness_evaluator.hpp>
#include <graphene/chain/worker_evaluator.hpp>

#include <fc/uint128.hpp>

#include <fc/crypto/digest.hpp>

namespace graphene { namespace chain {

void database::initialize_evaluators()
{
   _operation_evaluators.resize(255);
   register_evaluator<key_create_evaluator>();
   register_evaluator<account_create_evaluator>();
   register_evaluator<account_update_evaluator>();
   register_evaluator<account_upgrade_evaluator>();
   register_evaluator<account_whitelist_evaluator>();
   register_evaluator<delegate_create_evaluator>();
   register_evaluator<custom_evaluator>();
   register_evaluator<asset_create_evaluator>();
   register_evaluator<asset_issue_evaluator>();
   register_evaluator<asset_burn_evaluator>();
   register_evaluator<asset_update_evaluator>();
   register_evaluator<asset_update_bitasset_evaluator>();
   register_evaluator<asset_update_feed_producers_evaluator>();
   register_evaluator<asset_settle_evaluator>();
   register_evaluator<asset_global_settle_evaluator>();
   register_evaluator<limit_order_create_evaluator>();
   register_evaluator<limit_order_cancel_evaluator>();
   register_evaluator<call_order_update_evaluator>();
   register_evaluator<transfer_evaluator>();
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
}

void database::initialize_indexes()
{
   reset_indexes();

   //Protocol object indexes
   add_index< primary_index<asset_index> >();
   add_index< primary_index<force_settlement_index> >();
   add_index< primary_index<account_index> >();
   add_index< primary_index<simple_index<key_object>> >();
   add_index< primary_index<simple_index<delegate_object>> >();
   add_index< primary_index<simple_index<witness_object>> >();
   add_index< primary_index<limit_order_index > >();
   add_index< primary_index<call_order_index > >();
   add_index< primary_index<proposal_index > >();
   add_index< primary_index<withdraw_permission_index > >();
   add_index< primary_index<simple_index<vesting_balance_object> > >();
   add_index< primary_index<worker_index> >();

   //Implementation object indexes
   add_index< primary_index<transaction_index                             > >();
   add_index< primary_index<account_balance_index                         > >();
   add_index< primary_index<asset_bitasset_data_index                     > >();
   add_index< primary_index<simple_index< global_property_object         >> >();
   add_index< primary_index<simple_index< dynamic_global_property_object >> >();
   add_index< primary_index<simple_index< account_statistics_object      >> >();
   add_index< primary_index<simple_index< asset_dynamic_data_object      >> >();
   add_index< primary_index<flat_index<   block_summary_object           >> >();
   add_index< primary_index< simple_index< witness_schedule_object       > > >();
}

void database::init_genesis(const genesis_state_type& genesis_state)
{ try {
   _undo_db.disable();

   fc::ecc::private_key genesis_private_key = fc::ecc::private_key::regenerate(fc::sha256::hash(string("genesis")));
   const key_object& genesis_key =
      create<key_object>( [&genesis_private_key](key_object& k) {
         k.key_data = public_key_type(genesis_private_key.get_public_key());
      });
   const account_statistics_object& genesis_statistics =
      create<account_statistics_object>( [&](account_statistics_object& b){
      });
   create<account_balance_object>( [](account_balance_object& b) {
      b.balance = GRAPHENE_INITIAL_SUPPLY;
   });
   const account_object& genesis_account =
      create<account_object>( [&](account_object& n) {
         n.membership_expiration_date = time_point_sec::maximum();
         n.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
         n.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
         n.name = "genesis";
         n.owner.add_authority(genesis_key.get_id(), 1);
         n.owner.weight_threshold = 1;
         n.active = n.owner;
         n.options.memo_key = genesis_key.id;
         n.statistics = genesis_statistics.id;
      });

   vector<delegate_id_type> init_delegates;
   vector<witness_id_type> init_witnesses;
   flat_set<witness_id_type> init_witness_set;

   auto delegates_and_witnesses = std::max(GRAPHENE_MIN_WITNESS_COUNT, GRAPHENE_MIN_DELEGATE_COUNT);
   for( int i = 0; i < delegates_and_witnesses; ++i )
   {
      const account_statistics_object& stats_obj =
         create<account_statistics_object>( [&](account_statistics_object&){
         });
      const account_object& delegate_account =
         create<account_object>( [&](account_object& a) {
            a.active = a.owner = genesis_account.owner;
            a.referrer = account_id_type(i);
            a.registrar = account_id_type(i);
            a.lifetime_referrer = account_id_type(i);
            a.network_fee_percentage = GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
            a.lifetime_referrer_fee_percentage = GRAPHENE_100_PERCENT - GRAPHENE_DEFAULT_NETWORK_PERCENT_OF_FEE;
            a.membership_expiration_date = fc::time_point_sec::maximum();
            a.name = string("init") + fc::to_string(i);
            a.statistics = stats_obj.id;
         });
      const delegate_object& init_delegate = create<delegate_object>( [&](delegate_object& d) {
         d.delegate_account = delegate_account.id;
         d.vote_id = i * 2;
      });
      init_delegates.push_back(init_delegate.id);

      const witness_object& init_witness = create<witness_object>( [&](witness_object& d) {
            d.witness_account = delegate_account.id;
            d.vote_id = i * 2 + 1;
            secret_hash_type::encoder enc;
            fc::raw::pack( enc, genesis_private_key );
            fc::raw::pack( enc, d.last_secret );
            d.next_secret = secret_hash_type::hash(enc.result());
      });
      init_witnesses.push_back(init_witness.id);
      init_witness_set.insert(init_witness.id);

   }
   create<block_summary_object>( [&](block_summary_object& p) {
   });

   const witness_schedule_object& wso =
   create<witness_schedule_object>( [&]( witness_schedule_object& _wso )
   {
      memset( _wso.rng_seed.begin(), 0, _wso.rng_seed.size() );

      witness_scheduler_rng rng( _wso.rng_seed.begin(), GRAPHENE_NEAR_SCHEDULE_CTR_IV );

      _wso.scheduler = witness_scheduler();
      _wso.scheduler._min_token_count = init_witnesses.size() / 2;
      _wso.scheduler.update( init_witness_set );

      for( size_t i=0; i<init_witnesses.size(); i++ )
         _wso.scheduler.produce_schedule( rng );

      _wso.last_scheduling_block = 0;
   } ) ;
   assert( wso.id == witness_schedule_id_type() );

   const global_property_object& properties =
      create<global_property_object>( [&](global_property_object& p) {
         p.active_delegates = init_delegates;
         for( const witness_id_type& wit : init_witnesses )
            p.active_witnesses.insert( wit );
         p.next_available_vote_id = delegates_and_witnesses * 2;
         p.chain_id = fc::digest(genesis_state);
      });
   (void)properties;

   create<dynamic_global_property_object>( [&](dynamic_global_property_object& p) {
      p.time = fc::time_point_sec( GRAPHENE_GENESIS_TIMESTAMP );
      });

   const asset_dynamic_data_object& dyn_asset =
      create<asset_dynamic_data_object>( [&]( asset_dynamic_data_object& a ) {
         a.current_supply = GRAPHENE_INITIAL_SUPPLY;
      });

   const asset_object& core_asset =
     create<asset_object>( [&]( asset_object& a ) {
         a.symbol = GRAPHENE_SYMBOL;
         a.options.max_supply = GRAPHENE_INITIAL_SUPPLY;
         a.precision = GRAPHENE_BLOCKCHAIN_PRECISION_DIGITS;
         a.options.flags = 0;
         a.options.issuer_permissions = 0;
         a.issuer = genesis_account.id;
         a.options.core_exchange_rate.base.amount = 1;
         a.options.core_exchange_rate.base.asset_id = 0;
         a.options.core_exchange_rate.quote.amount = 1;
         a.options.core_exchange_rate.quote.asset_id = 0;
         a.dynamic_asset_data_id = dyn_asset.id;
      });
   assert( asset_id_type(core_asset.id) == asset().asset_id );
   assert( get_balance(account_id_type(), asset_id_type()) == asset(dyn_asset.current_supply) );
   (void)core_asset;

   if( !genesis_state.allocation_targets.empty() )
   {
      share_type total_allocation = 0;
      for( const auto& handout : genesis_state.allocation_targets )
         total_allocation += handout.weight;

      auto mangle_to_name = [](const fc::static_variant<public_key_type, address>& key) {
         string addr = string(key.which() == std::decay<decltype(key)>::type::tag<address>::value? key.get<address>()
                                                                                                 : key.get<public_key_type>());
         string result = "import";
         string key_string = string(addr).substr(sizeof(GRAPHENE_ADDRESS_PREFIX)-1);
         for( char c : key_string )
         {
            if( isupper(c) )
               result += string("-") + char(tolower(c));
            else
               result += c;
         }
         return result;
      };

      fc::time_point start_time = fc::time_point::now();

      for( const auto& handout : genesis_state.allocation_targets )
      {
         asset amount(handout.weight);
         amount.amount = ((fc::uint128(amount.amount.value) * GRAPHENE_INITIAL_SUPPLY)/total_allocation.value).to_uint64();
         if( amount.amount == 0 )
         {
            wlog("Skipping zero allocation to ${k}", ("k", handout.name));
            continue;
         }

         signed_transaction trx;
         trx.operations.emplace_back(key_create_operation({asset(), genesis_account.id, handout.addr}));
         relative_key_id_type key_id(0);
         authority account_authority(1, key_id, 1);
         account_create_operation cop;
         cop.name = handout.name;
         cop.registrar = account_id_type(1);
         cop.active = account_authority;
         cop.owner = account_authority;
         cop.options.memo_key = key_id;
         trx.operations.push_back(cop);
         trx.validate();
         auto ptrx = apply_transaction(trx, ~0);
         trx = signed_transaction();
         account_id_type account_id(ptrx.operation_results.back().get<object_id_type>());
         trx.operations.emplace_back(transfer_operation({  asset(),
                                                           genesis_account.id,
                                                           account_id,
                                                           amount,
                                                           memo_data()//vector<char>()
                                                        }));
         trx.validate();
         apply_transaction(trx, ~0);
      }

      asset leftovers = get_balance(account_id_type(), asset_id_type());
      if( leftovers.amount > 0 )
      {
         modify(*get_index_type<account_balance_index>().indices().get<by_balance>().find(boost::make_tuple(account_id_type(), asset_id_type())),
                [](account_balance_object& b) {
            b.adjust_balance(-b.get_balance());
         });
         modify(core_asset.dynamic_asset_data_id(*this), [&leftovers](asset_dynamic_data_object& d) {
            d.accumulated_fees += leftovers.amount;
         });
      }

      fc::microseconds duration = fc::time_point::now() - start_time;
      ilog("Finished allocating to ${n} accounts in ${t} milliseconds.",
           ("n", genesis_state.allocation_targets.size())("t", duration.count() / 1000));
   }

   _undo_db.enable();
} FC_LOG_AND_RETHROW() }

} }
