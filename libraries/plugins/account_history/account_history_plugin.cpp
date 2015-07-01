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

#include <graphene/account_history/account_history_plugin.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/key_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>

#include <fc/thread/thread.hpp>

namespace graphene { namespace account_history {

namespace detail
{


class account_history_plugin_impl
{
   public:
      account_history_plugin_impl(account_history_plugin& _plugin)
         : _self( _plugin )
      { }
      virtual ~account_history_plugin_impl();

      flat_set<key_id_type> get_keys_for_account(
          const account_id_type& account_id );

      /** this method is called as a callback after a block is applied
       * and will process/index all operations that were applied in the block.
       */
      void update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      account_history_plugin& _self;
      flat_set<account_id_type> _tracked_accounts;
};

struct operation_get_impacted_accounts
{
   const operation_history_object& _op_history;
   const account_history_plugin&   _plugin;
   flat_set<account_id_type>&      _impacted;
   operation_get_impacted_accounts( const operation_history_object& oho, const account_history_plugin& ahp, flat_set<account_id_type>& impact )
      :_op_history(oho),_plugin(ahp),_impacted(impact)
   {}
   typedef void result_type;

   void add_authority( const authority& a )const
   {
      for( auto& item : a.auths )
      {
         if( item.first.type() == account_object_type )
            _impacted.insert( item.first );
      }
   }

   void operator()( const transfer_operation& o )const {
      _impacted.insert( o.to );
   }

   void operator()( const limit_order_create_operation& o )const { }
   void operator()( const limit_order_cancel_operation& o )const { }
   void operator()( const call_order_update_operation& o )const { }
   void operator()( const key_create_operation& o )const { }
   void operator()( const custom_operation& o )const { }

   void operator()( const account_create_operation& o )const {
      _impacted.insert( _op_history.result.get<object_id_type>() );
   }

   void operator()( const account_update_operation& o )const {
      if( o.owner )
      {
         add_authority( *o.owner );
      }
      if( o.active )
      {
         add_authority( *o.active );
      }
   }
   void operator()( const account_upgrade_operation& )const {}
   void operator()( const account_transfer_operation& o )const
   {
      _impacted.insert( o.new_owner );
   }

   void operator()( const account_whitelist_operation& o )const {
       _impacted.insert( o.account_to_list );
   }

   void operator()( const asset_create_operation& o )const { }

   void operator()( const asset_update_operation& o )const {
      if( o.new_issuer )
         _impacted.insert(*o.new_issuer);
   }
   void operator()( const asset_update_bitasset_operation& o )const {
   }
   void operator()( const asset_update_feed_producers_operation& o )const {
      for( auto id : o.new_feed_producers )
         _impacted.insert(id);
   }

   void operator()( const asset_issue_operation& o )const {
       _impacted.insert( o.issue_to_account );
   }

   void operator()( const asset_reserve_operation& o )const { }
   void operator()( const asset_global_settle_operation& o )const { }
   void operator()( const asset_settle_operation& o )const { }

   void operator()( const asset_fund_fee_pool_operation& o )const { }
   void operator()( const asset_publish_feed_operation& o )const { }
   void operator()( const delegate_create_operation& o )const { }

   void operator()( const withdraw_permission_create_operation& o )const{
      _impacted.insert(o.authorized_account);
   }
   void operator()( const withdraw_permission_claim_operation& o )const{
      _impacted.insert( o.withdraw_from_account );
   }
   void operator()( const withdraw_permission_update_operation& o )const{
      _impacted.insert( o.authorized_account );
   }
   void operator()( const withdraw_permission_delete_operation& o )const{
      _impacted.insert( o.authorized_account );
   }

   void operator()( const witness_create_operation& o )const {
      _impacted.insert(o.witness_account);
   }

   void operator()( const witness_withdraw_pay_operation& o )const { }

   void operator()( const proposal_create_operation& o )const {
       for( auto op : o.proposed_ops )
          op.op.visit( operation_get_required_auths( _impacted, _impacted ) );
   }

   void operator()( const proposal_update_operation& o )const { }
   void operator()( const proposal_delete_operation& o )const { }

   void operator()( const fill_order_operation& o )const {
      _impacted.insert( o.account_id );
   }

   void operator()(const global_parameters_update_operation& )const {
      _impacted.insert( account_id_type() );
   }

   void operator()( const vesting_balance_create_operation& o )const
   {
      _impacted.insert( o.creator );
      _impacted.insert( o.owner );
   }

   void operator()( const vesting_balance_withdraw_operation& o )const
   {
      _impacted.insert( o.owner );
   }

   void operator()( const worker_create_operation& )const {}
   void operator()( const assert_operation& )const {}
   void operator()( const balance_claim_operation& )const {}
   void operator()( const override_transfer_operation& )const {}
};


account_history_plugin_impl::~account_history_plugin_impl()
{
   return;
}



void account_history_plugin_impl::update_account_histories( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<operation_history_object>& hist = db.get_applied_operations();
   for( auto op : hist )
   {
      // add to the operation history index
      const auto& oho = db.create<operation_history_object>( [&]( operation_history_object& h ){
                                h = op;
                        });

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      op.op.visit( operation_get_required_auths( impacted, impacted ) );
      op.op.visit( operation_get_impacted_accounts( oho, _self, impacted ) );

      // for each operation this account applies to that is in the config link it into the history
      if( _tracked_accounts.size() == 0 )
      {
         for( auto& account_id : impacted )
         {
            // we don't do index_account_keys here anymore, because
            // that indexing now happens in observers' post_evaluate()

            // add history
            const auto& stats_obj = account_id(db).statistics(db);
            const auto& ath = db.create<account_transaction_history_object>( [&]( account_transaction_history_object& obj ){
                obj.operation_id = oho.id;
                obj.next = stats_obj.most_recent_op;
            });
            db.modify( stats_obj, [&]( account_statistics_object& obj ){
                obj.most_recent_op = ath.id;
            });
         }
      }
      else
      {
         for( auto account_id : _tracked_accounts )
         {
            if( impacted.find( account_id ) != impacted.end() )
            {
               // add history
               const auto& stats_obj = account_id(db).statistics(db);
               const auto& ath = db.create<account_transaction_history_object>( [&]( account_transaction_history_object& obj ){
                   obj.operation_id = oho.id;
                   obj.next = stats_obj.most_recent_op;
               });
               db.modify( stats_obj, [&]( account_statistics_object& obj ){
                   obj.most_recent_op = ath.id;
               });
            }
         }
      }
   }
}
} // end namespace detail

account_history_plugin::account_history_plugin() :
   my( new detail::account_history_plugin_impl(*this) )
{
}

account_history_plugin::~account_history_plugin()
{
}

std::string account_history_plugin::plugin_name()const
{
   return "account_history";
}

void account_history_plugin::plugin_set_program_options(
   boost::program_options::options_description& cli,
   boost::program_options::options_description& cfg
   )
{
   cli.add_options()
         ("track-account", boost::program_options::value<std::vector<std::string>>()->composing()->multitoken(), "Account ID to track history for (may specify multiple times)")
         ;
   cfg.add(cli);
}

void account_history_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   database().applied_block.connect( [&]( const signed_block& b){ my->update_account_histories(b); } );
   database().add_index< primary_index< simple_index< operation_history_object > > >();
   database().add_index< primary_index< simple_index< account_transaction_history_object > > >();

   LOAD_VALUE_SET(options, "tracked-accounts", my->_tracked_accounts, graphene::chain::account_id_type);
}

void account_history_plugin::plugin_startup()
{
}

flat_set<account_id_type> account_history_plugin::tracked_accounts() const
{
   return my->_tracked_accounts;
}

} }
