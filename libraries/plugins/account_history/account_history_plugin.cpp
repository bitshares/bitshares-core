/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#include <graphene/account_history/account_history_plugin.hpp>

#include <graphene/chain/impacted.hpp>

#include <graphene/chain/account_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/config.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/utilities/boost_program_options.hpp>

#include <fc/thread/thread.hpp>

namespace graphene { namespace account_history {

namespace detail
{


class account_history_plugin_impl
{
   public:
      explicit account_history_plugin_impl(account_history_plugin& _plugin)
         : _self( _plugin )
      { }

   private:
      /** this method is called as a callback after a block is applied
       * and will process/index all operations that were applied in the block.
       */
      void update_account_histories( const signed_block& b );

      graphene::chain::database& database()
      {
         return _self.database();
      }

      friend class graphene::account_history::account_history_plugin;

      account_history_plugin& _self;
      flat_set<account_id_type> _tracked_accounts;
      flat_set<account_id_type> _extended_history_accounts;
      flat_set<account_id_type> _extended_history_registrars;
      bool _partial_operations = false;
      primary_index< operation_history_index >* _oho_index;
      uint64_t _max_ops_per_account = -1;
      uint64_t _extended_max_ops_per_account = -1;
      uint32_t _min_blocks_to_keep = 30000;
      uint64_t _max_ops_per_acc_by_min_blocks = 1000;

      uint64_t get_max_ops_to_keep( const account_id_type& account_id );

      /** add one history record, then check and remove the earliest history record(s) */
      void add_account_history( const account_id_type& account_id, const operation_history_object& op );

      void remove_old_histories( const account_statistics_object& stats_obj,
                                 const account_history_object& latest_aho,
                                 const operation_history_object& latest_op );

      void init_program_options(const boost::program_options::variables_map& options);
};

void account_history_plugin_impl::update_account_histories( const signed_block& b )
{
   graphene::chain::database& db = database();
   const vector<optional< operation_history_object > >& hist = db.get_applied_operations();
   bool is_first = true;
   auto skip_oho_id = [&is_first,&db,this]() {
      if( is_first && db._undo_db.enabled() ) // this ensures that the current id is rolled back on undo
      {
         db.remove( db.create<operation_history_object>( []( operation_history_object& obj) {} ) );
         is_first = false;
      }
      else
         _oho_index->use_next_id();
   };

   for( const optional< operation_history_object >& o_op : hist )
   {
      optional<operation_history_object> oho;

      auto create_oho = [&]() {
         is_first = false;
         return optional<operation_history_object>( db.create<operation_history_object>( [&]( operation_history_object& h )
         {
            if( o_op.valid() )
            {
               h.op           = o_op->op;
               h.result       = o_op->result;
               h.block_num    = o_op->block_num;
               h.trx_in_block = o_op->trx_in_block;
               h.op_in_trx    = o_op->op_in_trx;
               h.virtual_op   = o_op->virtual_op;
               h.is_virtual   = o_op->is_virtual;
               h.block_time   = o_op->block_time;
            }
         } ) );
      };

      if( !o_op.valid() || ( _max_ops_per_account == 0 && _partial_operations ) )
      {
         // Note: the 2nd and 3rd checks above are for better performance, when the db is not clean,
         //       they will break consistency of account_stats.total_ops and removed_ops and most_recent_op
         skip_oho_id();
         continue;
      }
      else if( !_partial_operations )
         // add to the operation history index
         oho = create_oho();

      const operation_history_object& op = *o_op;

      // get the set of accounts this operation applies to
      flat_set<account_id_type> impacted;
      vector<authority> other;
      // fee payer is added here
      operation_get_required_authorities( op.op, impacted, impacted, other,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ) );

      if( op.op.is_type< account_create_operation >() )
         impacted.insert( op.result.get<object_id_type>() );

      // https://github.com/bitshares/bitshares-core/issues/265
      if( HARDFORK_CORE_265_PASSED(b.timestamp) || !op.op.is_type< account_create_operation >() )
      {
         operation_get_impacted_accounts( op.op, impacted,
                                          MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ) );
      }

      if( op.result.is_type<extendable_operation_result>() )
      {
         const auto& op_result = op.result.get<extendable_operation_result>();
         if( op_result.value.impacted_accounts.valid() )
         {
            for( const auto& a : *op_result.value.impacted_accounts )
               impacted.insert( a );
         }
      }

      for( auto& a : other )
         for( auto& item : a.account_auths )
            impacted.insert( item.first );

      // be here, either _max_ops_per_account > 0, or _partial_operations == false, or both
      // if _partial_operations == false, oho should have been created above
      // so the only case should be checked here is:
      //    whether need to create oho if _max_ops_per_account > 0 and _partial_operations == true

      // for each operation this account applies to that is in the config link it into the history
      if( _tracked_accounts.size() == 0 ) // tracking all accounts
      {
         // if tracking all accounts, when impacted is not empty (although it will always be),
         //    still need to create oho if _max_ops_per_account > 0 and _partial_operations == true
         //    so always need to create oho if not done
         if (!impacted.empty() && !oho.valid()) { oho = create_oho(); }

         if( _max_ops_per_account > 0 )
         {
            // Note: the check above is for better performance, when the db is not clean,
            //       it breaks consistency of account_stats.total_ops and removed_ops and most_recent_op,
            //       but it ensures it's safe to remove old entries in add_account_history(...)
            for( auto& account_id : impacted )
            {
               // we don't do index_account_keys here anymore, because
               // that indexing now happens in observers' post_evaluate()

               // add history
               add_account_history( account_id, *oho );
            }
         }
      }
      else // tracking a subset of accounts
      {
         // whether need to create oho if _max_ops_per_account > 0 and _partial_operations == true ?
         // the answer: only need to create oho if a tracked account is impacted and need to save history

         if( _max_ops_per_account > 0 )
         {
            // Note: the check above is for better performance, when the db is not clean,
            //       it breaks consistency of account_stats.total_ops and removed_ops and most_recent_op,
            //       but it ensures it's safe to remove old entries in add_account_history(...)
            for( auto account_id : _tracked_accounts )
            {
               if( impacted.find( account_id ) != impacted.end() )
               {
                  if (!oho.valid()) { oho = create_oho(); }
                  // add history
                  add_account_history( account_id, *oho );
               }
            }
         }
      }
      if (_partial_operations && ! oho.valid())
         skip_oho_id();
   }
}

void account_history_plugin_impl::add_account_history( const account_id_type& account_id,
                                                       const operation_history_object& op )
{
   graphene::chain::database& db = database();
   const auto& stats_obj = db.get_account_stats_by_owner( account_id );
   // add new entry
   const auto& aho = db.create<account_history_object>( [&account_id,&op,&stats_obj](account_history_object& obj){
       obj.operation_id = op.id;
       obj.account = account_id;
       obj.sequence = stats_obj.total_ops + 1;
       obj.next = stats_obj.most_recent_op;
   });
   db.modify( stats_obj, [&aho]( account_statistics_object& obj ){
       obj.most_recent_op = aho.id;
       obj.total_ops = aho.sequence;
   });
   // Remove the earliest account history entries if too many.
   remove_old_histories( stats_obj, aho, op );
}

uint64_t account_history_plugin_impl::get_max_ops_to_keep( const account_id_type& account_id )
{
   const graphene::chain::database& db = database();
   // Amount of history to keep depends on if account is in the "extended history" list
   bool extended_hist = ( _extended_history_accounts.find( account_id ) != _extended_history_accounts.end() );
   if( !extended_hist && !_extended_history_registrars.empty() )
   {
      const account_id_type& registrar_id = account_id(db).registrar;
      extended_hist = ( _extended_history_registrars.find( registrar_id ) != _extended_history_registrars.end() );
   }
   // _max_ops_per_account is guaranteed to be non-zero outside; max_ops_to_keep
   // will likewise be non-zero, and also non-negative (it is unsigned).
   auto max_ops_to_keep = _max_ops_per_account;
   if( extended_hist && _extended_max_ops_per_account > max_ops_to_keep )
   {
      max_ops_to_keep = _extended_max_ops_per_account;
   }
   if( 0 == max_ops_to_keep )
      return 1;
   return max_ops_to_keep;
}

// Remove the earliest account history entries if too many.
void account_history_plugin_impl::remove_old_histories( const account_statistics_object& stats_obj,
                                                        const account_history_object& latest_aho,
                                                        const operation_history_object& latest_op )
{
   graphene::chain::database& db = database();
   const account_id_type& account_id = latest_aho.account;
   auto max_ops_to_keep = get_max_ops_to_keep( account_id );

   while( stats_obj.total_ops - stats_obj.removed_ops > max_ops_to_keep )
   {
      // look for the earliest entry
      const auto& his_idx = db.get_index_type<account_history_index>();
      const auto& by_seq_idx = his_idx.indices().get<by_seq>();
      auto itr = by_seq_idx.lower_bound( account_id );
      // make sure don't remove the one just added
      if( itr == by_seq_idx.end() || itr->account != account_id || itr->id == latest_aho.id )
         return;

      // if found, check whether to remove
      const auto remove_op_id = itr->operation_id;
      const auto& remove_op = remove_op_id(db);
      if( remove_op.block_num + _min_blocks_to_keep > latest_op.block_num
          && stats_obj.total_ops - stats_obj.removed_ops <= _max_ops_per_acc_by_min_blocks )
         return;

      // remove the entry, and adjust account stats object
      const auto itr_remove = itr;
      ++itr;
      db.remove( *itr_remove );
      db.modify( stats_obj, [&]( account_statistics_object& obj ){
          obj.removed_ops = obj.removed_ops + 1;
      });
      // modify previous node's next pointer
      // this should be always true, but just have a check here
      if( itr != by_seq_idx.end() && itr->account == account_id )
      {
         db.modify( *itr, [&]( account_history_object& obj ){
            obj.next = account_history_id_type();
         });
      }
      // else need to modify the head pointer, but it shouldn't be true

      // remove the operation history entry (1.11.x) if configured and no reference left
      if( _partial_operations )
      {
         // check for references
         const auto& by_opid_idx = his_idx.indices().get<by_opid>();
         if( by_opid_idx.find( remove_op_id ) == by_opid_idx.end() )
         {
            // if no reference, remove
            db.remove( remove_op );
         }
      }
   }
}

} // end namespace detail






account_history_plugin::account_history_plugin(graphene::app::application& app) :
   plugin(app),
   my( std::make_unique<detail::account_history_plugin_impl>(*this) )
{
   // Nothing else to do
}

account_history_plugin::~account_history_plugin() = default;

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
         ("track-account", boost::program_options::value<std::vector<std::string>>()->composing()->multitoken(),
          "Account ID to track history for (may specify multiple times; if unset will track all accounts)")
         ("partial-operations", boost::program_options::value<bool>(),
          "Keep only those operations in memory that are related to account history tracking")
         ("max-ops-per-account", boost::program_options::value<uint64_t>(),
          "Maximum number of operations per account that will be kept in memory. "
          "Note that the actual number may be higher due to the min-blocks-to-keep option.")
         ("extended-max-ops-per-account", boost::program_options::value<uint64_t>(),
          "Maximum number of operations to keep for accounts for which extended history is kept")
         ("extended-history-by-account",
          boost::program_options::value<std::vector<std::string>>()->composing()->multitoken(),
          "Track longer history for these accounts (may specify multiple times)")
         ("extended-history-by-registrar",
          boost::program_options::value<std::vector<std::string>>()->composing()->multitoken(),
          "Track longer history for accounts with this registrar (may specify multiple times)")
         ("min-blocks-to-keep", boost::program_options::value<uint32_t>(),
          "Operations which are in the latest X blocks will be kept in memory. "
          "Note that this may exceed the limit defined by the max-ops-per-account option, "
          "but will be limited by the max-ops-per-acc-by-min-blocks option.")
         ("max-ops-per-acc-by-min-blocks", boost::program_options::value<uint64_t>(),
          "A potential higher limit on the maximum number of operations per account to be kept in memory "
          "when the min-blocks-to-keep option exceeds the limit defined by the max-ops-per-account option. "
          "If this is less than max-ops-per-account, max-ops-per-account will be used.")
         ;
   cfg.add(cli);
}

void account_history_plugin::plugin_initialize(const boost::program_options::variables_map& options)
{
   my->init_program_options( options );

   database().applied_block.connect( [&]( const signed_block& b){ my->update_account_histories(b); } );
   my->_oho_index = database().add_index< primary_index< operation_history_index > >();
   database().add_index< primary_index< account_history_index > >();
}

void detail::account_history_plugin_impl::init_program_options(const boost::program_options::variables_map& options)
{
   LOAD_VALUE_SET(options, "track-account", _tracked_accounts, graphene::chain::account_id_type);

   utilities::get_program_option( options, "partial-operations", _partial_operations );
   utilities::get_program_option( options, "max-ops-per-account", _max_ops_per_account );
   utilities::get_program_option( options, "extended-max-ops-per-account", _extended_max_ops_per_account );
   if( _extended_max_ops_per_account < _max_ops_per_account )
      _extended_max_ops_per_account = _max_ops_per_account;

   LOAD_VALUE_SET(options, "extended-history-by-account", _extended_history_accounts,
                  graphene::chain::account_id_type);
   LOAD_VALUE_SET(options, "extended-history-by-registrar", _extended_history_registrars,
                  graphene::chain::account_id_type);

   utilities::get_program_option( options, "min-blocks-to-keep", _min_blocks_to_keep );
   utilities::get_program_option( options, "max-ops-per-acc-by-min-blocks", _max_ops_per_acc_by_min_blocks );
   if( _max_ops_per_acc_by_min_blocks < _max_ops_per_account )
      _max_ops_per_acc_by_min_blocks = _max_ops_per_account;
}

void account_history_plugin::plugin_startup()
{
}

flat_set<account_id_type> account_history_plugin::tracked_accounts() const
{
   return my->_tracked_accounts;
}

} }
