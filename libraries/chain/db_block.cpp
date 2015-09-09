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
#include <graphene/chain/db_with.hpp>

#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/operation_history_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/witness_object.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <graphene/chain/exceptions.hpp>

namespace graphene { namespace chain {

bool database::is_known_block( const block_id_type& id )const
{
   return _fork_db.is_known_block(id) || _block_id_to_block.contains(id);
}
/**
 * Only return true *if* the transaction has not expired or been invalidated. If this
 * method is called with a VERY old transaction we will return false, they should
 * query things by blocks if they are that old.
 */
bool database::is_known_transaction( const transaction_id_type& id )const
{
   const auto& trx_idx = get_index_type<transaction_index>().indices().get<by_trx_id>();
   return trx_idx.find( id ) != trx_idx.end();
}

block_id_type  database::get_block_id_for_num( uint32_t block_num )const
{ try {
   return _block_id_to_block.fetch_block_id( block_num );
} FC_CAPTURE_AND_RETHROW( (block_num) ) }

optional<signed_block> database::fetch_block_by_id( const block_id_type& id )const
{
   auto b = _fork_db.fetch_block( id );
   if( !b )
      return _block_id_to_block.fetch_optional(id);
   return b->data;
}

optional<signed_block> database::fetch_block_by_number( uint32_t num )const
{
   auto results = _fork_db.fetch_block_by_number(num);
   if( results.size() == 1 )
      return results[0]->data;
   else
      return _block_id_to_block.fetch_by_number(num);
   return optional<signed_block>();
}

const signed_transaction& database::get_recent_transaction(const transaction_id_type& trx_id) const
{
   auto& index = get_index_type<transaction_index>().indices().get<by_trx_id>();
   auto itr = index.find(trx_id);
   FC_ASSERT(itr != index.end());
   return itr->trx;
}

/**
 * Push block "may fail" in which case every partial change is unwound.  After
 * push block is successful the block is appended to the chain database on disk.
 *
 * @return true if we switched forks as a result of this push.
 */
bool database::push_block(const signed_block& new_block, uint32_t skip)
{
   idump((new_block.block_num())(new_block.id()));
   bool result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      detail::without_pending_transactions( *this, std::move(_pending_block.transactions),
      [&]()
      {
         result = _push_block(new_block);
      });
   });
   return result;
}

bool database::_push_block(const signed_block& new_block)
{ try {
   uint32_t skip = get_node_properties().skip_flags;
   if( !(skip&skip_fork_db) )
   {
      /// TODO: if the block is greater than the head block and before the next maitenance interval
      // verify that the block signer is in the current set of active witnesses.

      shared_ptr<fork_item> new_head = _fork_db.push_block(new_block);
      //If the head block from the longest chain does not build off of the current head, we need to switch forks.
      if( new_head->data.previous != head_block_id() )
      {
         //If the newly pushed block is the same height as head, we get head back in new_head
         //Only switch forks if new_head is actually higher than head
         if( new_head->data.block_num() > head_block_num() )
         {
            auto branches = _fork_db.fetch_branch_from(new_head->data.id(), _pending_block.previous);

            // pop blocks until we hit the forked block
            while( head_block_id() != branches.second.back()->data.previous )
               pop_block();

            // push all blocks on the new fork
            for( auto ritr = branches.first.rbegin(); ritr != branches.first.rend(); ++ritr )
            {
                optional<fc::exception> except;
                try {
                   undo_database::session session = _undo_db.start_undo_session();
                   apply_block( (*ritr)->data, skip );
                   _block_id_to_block.store( (*ritr)->id, (*ritr)->data );
                   session.commit();
                }
                catch ( const fc::exception& e ) { except = e; }
                if( except )
                {
                   // remove the rest of branches.first from the fork_db, those blocks are invalid
                   while( ritr != branches.first.rend() )
                   {
                      _fork_db.remove( (*ritr)->data.id() );
                      ++ritr;
                   }
                   _fork_db.set_head( branches.second.front() );

                   // pop all blocks from the bad fork
                   while( head_block_id() != branches.second.back()->data.previous )
                      pop_block();

                   // restore all blocks from the good fork
                   for( auto ritr = branches.second.rbegin(); ritr != branches.second.rend(); ++ritr )
                   {
                      auto session = _undo_db.start_undo_session();
                      apply_block( (*ritr)->data, skip );
                      _block_id_to_block.store( new_block.id(), (*ritr)->data );
                      session.commit();
                   }
                   throw *except;
                }
            }
            return true;
         }
         else return false;
      }
   }

   try {
      auto session = _undo_db.start_undo_session();
      apply_block(new_block, skip);
      _block_id_to_block.store(new_block.id(), new_block);
      session.commit();
   } catch ( const fc::exception& e ) {
      elog("Failed to push new block:\n${e}", ("e", e.to_detail_string()));
      _fork_db.remove(new_block.id());
      throw;
   }

   return false;
} FC_CAPTURE_AND_RETHROW( (new_block) ) }

/**
 * Attempts to push the transaction into the pending queue
 *
 * When called to push a locally generated transaction, set the skip_block_size_check bit on the skip argument. This
 * will allow the transaction to be pushed even if it causes the pending block size to exceed the maximum block size.
 * Although the transaction will probably not propagate further now, as the peers are likely to have their pending
 * queues full as well, it will be kept in the queue to be propagated later when a new block flushes out the pending
 * queues.
 */
processed_transaction database::push_transaction( const signed_transaction& trx, uint32_t skip )
{ try {
   processed_transaction result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _push_transaction( trx );
   } );
   return result;
} FC_CAPTURE_AND_RETHROW( (trx) ) }

processed_transaction database::_push_transaction( const signed_transaction& trx )
{
   uint32_t skip = get_node_properties().skip_flags;
   // If this is the first transaction pushed after applying a block, start a new undo session.
   // This allows us to quickly rewind to the clean state of the head block, in case a new block arrives.
   if( !_pending_block_session ) _pending_block_session = _undo_db.start_undo_session();
   auto session = _undo_db.start_undo_session();
   auto processed_trx = _apply_transaction( trx );
   _pending_block.transactions.push_back(processed_trx);

   FC_ASSERT( (skip & skip_block_size_check) ||
              fc::raw::pack_size(_pending_block) <= get_global_properties().parameters.maximum_block_size );

   notify_changed_objects();
   // The transaction applied successfully. Merge its changes into the pending block session.
   session.merge();

   // notify anyone listening to pending transactions
   on_pending_transaction( trx );
   return processed_trx;
}

processed_transaction database::validate_transaction( const signed_transaction& trx )
{
   auto session = _undo_db.start_undo_session();
   return _apply_transaction( trx );
}

processed_transaction database::push_proposal(const proposal_object& proposal)
{
   transaction_evaluation_state eval_state(this);
   eval_state._is_proposed_trx = true;

   eval_state.operation_results.reserve(proposal.proposed_transaction.operations.size());
   processed_transaction ptrx(proposal.proposed_transaction);
   eval_state._trx = &ptrx;

   auto session = _undo_db.start_undo_session();
   for( auto& op : proposal.proposed_transaction.operations )
      eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
   remove(proposal);
   session.merge();

   ptrx.operation_results = std::move(eval_state.operation_results);
   return ptrx;
}

signed_block database::generate_block(
   fc::time_point_sec when,
   witness_id_type witness_id,
   const fc::ecc::private_key& block_signing_private_key,
   uint32_t skip /* = 0 */
   )
{
   signed_block result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _generate_block( when, witness_id, block_signing_private_key, true );
   } );
   return result;
}

signed_block database::_generate_block(
   fc::time_point_sec when,
   witness_id_type witness_id,
   const fc::ecc::private_key& block_signing_private_key,
   bool retry_on_failure
   )
{
   try {
   uint32_t skip = get_node_properties().skip_flags;
   uint32_t slot_num = get_slot_at_time( when );
   FC_ASSERT( slot_num > 0 );
   witness_id_type scheduled_witness = get_scheduled_witness( slot_num );
   FC_ASSERT( scheduled_witness == witness_id );

   const auto& witness_obj = witness_id(*this);

   if( !(skip & skip_witness_signature) )
      FC_ASSERT( witness_obj.signing_key == block_signing_private_key.get_public_key() );

   _pending_block.timestamp = when;

   _pending_block.transaction_merkle_root = _pending_block.calculate_merkle_root();

   _pending_block.witness = witness_id;
   if( !(skip & skip_witness_signature) ) _pending_block.sign( block_signing_private_key );

   FC_ASSERT( fc::raw::pack_size(_pending_block) <= get_global_properties().parameters.maximum_block_size );
   signed_block tmp = _pending_block;
   tmp.transaction_merkle_root = tmp.calculate_merkle_root();
   _pending_block.transactions.clear();

   bool failed = false;
   try { push_block( tmp, skip ); }
   catch ( const undo_database_exception& e ) { throw; }
   catch ( const fc::exception& e )
   {
      if( !retry_on_failure )
      {
         failed = true;
      }
      else
      {
         wlog( "Reason for block production failure: ${e}", ("e",e) );
         throw;
      }
   }
   if( failed )
   {
      uint32_t failed_tx_count = 0;
      for( const auto& trx : tmp.transactions )
      {
         try
         {
            push_transaction( trx, skip );
         }
         catch ( const fc::exception& e )
         {
            wlog( "Transaction is no longer valid: ${trx}", ("trx",trx) );
            failed_tx_count++;
         }
      }
      if( failed_tx_count == 0 )
      {
         //
         // this is in generate_block() so this intensive logging
         // (dumping a whole block) should be rate-limited
         // to once per block production attempt
         //
         // TODO:  Turn this off again once #261 is resolved.
         //
         wlog( "Block creation failed even though all tx's are still valid.  Block: ${b}", ("b",tmp) );
      }
      return _generate_block( when, witness_id, block_signing_private_key, false );
   }
   return tmp;
} FC_CAPTURE_AND_RETHROW( (witness_id) ) }

/**
 * Removes the most recent block from the database and
 * undoes any changes it made.
 */
void database::pop_block()
{ try {
   _pending_block_session.reset();
   auto prev = _pending_block.previous;
   pop_undo();
   _block_id_to_block.remove( prev );
   _pending_block.previous  = head_block_id();
   _pending_block.timestamp = head_block_time();
   _fork_db.pop_block();
} FC_CAPTURE_AND_RETHROW() }

void database::clear_pending()
{ try {
   _pending_block.transactions.clear();
   _pending_block_session.reset();
} FC_CAPTURE_AND_RETHROW() }

uint32_t database::push_applied_operation( const operation& op )
{
   _applied_ops.emplace_back(op);
   auto& oh = _applied_ops.back();
   oh.block_num    = _current_block_num;
   oh.trx_in_block = _current_trx_in_block;
   oh.op_in_trx    = _current_op_in_trx;
   oh.virtual_op   = _current_virtual_op++;
   return _applied_ops.size() - 1;
}
void database::set_applied_operation_result( uint32_t op_id, const operation_result& result )
{
   assert( op_id < _applied_ops.size() );
   _applied_ops[op_id].result = result;
}

const vector<operation_history_object>& database::get_applied_operations() const
{
   return _applied_ops;
}

//////////////////// private methods ////////////////////

void database::apply_block( const signed_block& next_block, uint32_t skip )
{
   auto block_num = next_block.block_num();
   if( _checkpoints.size() && _checkpoints.rbegin()->second != block_id_type() )
   {
      auto itr = _checkpoints.find( block_num );
      if( itr != _checkpoints.end() )
         FC_ASSERT( next_block.id() == itr->second, "Block did not match checkpoint", ("checkpoint",*itr)("block_id",next_block.id()) );

      if( _checkpoints.rbegin()->first >= block_num )
         skip = ~0;// WE CAN SKIP ALMOST EVERYTHING
   }

   detail::with_skip_flags( *this, skip, [&]()
   {
      _apply_block( next_block );
   } );
   return;
}

void database::_apply_block( const signed_block& next_block )
{ try {
   uint32_t skip = get_node_properties().skip_flags;
   _applied_ops.clear();

   FC_ASSERT( (skip & skip_merkle_check) || next_block.transaction_merkle_root == next_block.calculate_merkle_root(), "", ("next_block.transaction_merkle_root",next_block.transaction_merkle_root)("calc",next_block.calculate_merkle_root())("next_block",next_block)("id",next_block.id()) );

   const witness_object& signing_witness = validate_block_header(skip, next_block);
   const auto& global_props = get_global_properties();
   const auto& dynamic_global_props = get<dynamic_global_property_object>(dynamic_global_property_id_type());
   bool maint_needed = (dynamic_global_props.next_maintenance_time <= next_block.timestamp);

   _current_block_num    = next_block.block_num();
   _current_trx_in_block = 0;

   for( const auto& trx : next_block.transactions )
   {
      /* We do not need to push the undo state for each transaction
       * because they either all apply and are valid or the
       * entire block fails to apply.  We only need an "undo" state
       * for transactions when validating broadcast transactions or
       * when building a block.
       */
      apply_transaction( trx, skip | skip_transaction_signatures );
      ++_current_trx_in_block;
   }

   update_global_dynamic_data(next_block);
   update_signing_witness(signing_witness, next_block);

   auto current_block_interval = global_props.parameters.block_interval;

   // Are we at the maintenance interval?
   if( maint_needed )
      // This will update _pending_block.timestamp if the block interval has changed
      perform_chain_maintenance(next_block, global_props);

   create_block_summary(next_block);
   clear_expired_transactions();
   clear_expired_proposals();
   clear_expired_orders();
   update_expired_feeds();
   update_withdraw_permissions();

   // n.b., update_maintenance_flag() happens this late
   // because get_slot_time() / get_slot_at_time() is needed above
   // TODO:  figure out if we could collapse this function into
   // update_global_dynamic_data() as perhaps these methods only need
   // to be called for header validation?
   update_maintenance_flag( maint_needed );

   // notify observers that the block has been applied
   applied_block( next_block ); //emit
   _applied_ops.clear();

   notify_changed_objects();

   update_pending_block(next_block, current_block_interval);
} FC_CAPTURE_AND_RETHROW( (next_block.block_num()) )  }

void database::notify_changed_objects()
{ try {
   if( _undo_db.enabled() ) 
   {
      const auto& head_undo = _undo_db.head();
      vector<object_id_type> changed_ids;  changed_ids.reserve(head_undo.old_values.size());
      for( const auto& item : head_undo.old_values ) changed_ids.push_back(item.first);
      for( const auto& item : head_undo.new_ids ) changed_ids.push_back(item);
      vector<const object*> removed;
      removed.reserve( head_undo.removed.size() );
      for( const auto& item : head_undo.removed )
      {
         changed_ids.push_back( item.first );
         removed.emplace_back( item.second.get() );
      }
      changed_objects(changed_ids);
   }
} FC_CAPTURE_AND_RETHROW() }

processed_transaction database::apply_transaction(const signed_transaction& trx, uint32_t skip)
{
   processed_transaction result;
   detail::with_skip_flags( *this, skip, [&]()
   {
      result = _apply_transaction(trx);
   });
   return result;
}

processed_transaction database::_apply_transaction(const signed_transaction& trx)
{ try {
   uint32_t skip = get_node_properties().skip_flags;
   trx.validate();
   auto& trx_idx = get_mutable_index_type<transaction_index>();
   const chain_id_type& chain_id = get_chain_id();
   auto trx_id = trx.id();
   FC_ASSERT( (skip & skip_transaction_dupe_check) ||
              trx_idx.indices().get<by_trx_id>().find(trx_id) == trx_idx.indices().get<by_trx_id>().end() );
   transaction_evaluation_state eval_state(this);
   const chain_parameters& chain_parameters = get_global_properties().parameters;
   eval_state._trx = &trx;

   if( !(skip & (skip_transaction_signatures | skip_authority_check) ) )
   {
      auto get_active = [&]( account_id_type id ) { return &id(*this).active; };
      auto get_owner  = [&]( account_id_type id ) { return &id(*this).owner;  };
      trx.verify_authority( chain_id, get_active, get_owner, get_global_properties().parameters.max_authority_depth );
   }

   //Skip all manner of expiration and TaPoS checking if we're on block 1; It's impossible that the transaction is
   //expired, and TaPoS makes no sense as no blocks exist.
   if( BOOST_LIKELY(head_block_num() > 0) )
   {
      if( !(skip & skip_tapos_check) )
      {
         const auto& tapos_block_summary = block_summary_id_type( trx.ref_block_num )(*this);

         //Verify TaPoS block summary has correct ID prefix, and that this block's time is not past the expiration
         FC_ASSERT( trx.ref_block_prefix == tapos_block_summary.block_id._hash[1] );
      }

      FC_ASSERT( trx.expiration <= _pending_block.timestamp + chain_parameters.maximum_time_until_expiration, "",
                 ("trx.expiration",trx.expiration)("_pending_block.timestamp",_pending_block.timestamp)("max_til_exp",chain_parameters.maximum_time_until_expiration));
      FC_ASSERT( _pending_block.timestamp <= trx.expiration, "", ("pending.timestamp",_pending_block.timestamp)("trx.exp",trx.expiration) );
   }

   //Insert transaction into unique transactions database.
   if( !(skip & skip_transaction_dupe_check) )
   {
      create<transaction_object>([&](transaction_object& transaction) {
         transaction.trx_id = trx_id;
         transaction.trx = trx;
      });
   }

   eval_state.operation_results.reserve(trx.operations.size());

   //Finally process the operations
   processed_transaction ptrx(trx);
   _current_op_in_trx = 0;
   for( const auto& op : ptrx.operations )
   {
      eval_state.operation_results.emplace_back(apply_operation(eval_state, op));
      ++_current_op_in_trx;
   }
   ptrx.operation_results = std::move(eval_state.operation_results);

   //Make sure the temp account has no non-zero balances
   const auto& index = get_index_type<account_balance_index>().indices().get<by_account>();
   auto range = index.equal_range(GRAPHENE_TEMP_ACCOUNT);
   std::for_each(range.first, range.second, [](const account_balance_object& b) { FC_ASSERT(b.balance == 0); });

   return ptrx;
} FC_CAPTURE_AND_RETHROW( (trx) ) }

operation_result database::apply_operation(transaction_evaluation_state& eval_state, const operation& op)
{ try {
   int i_which = op.which();
   uint64_t u_which = uint64_t( i_which );
   if( i_which < 0 )
      assert( "Negative operation tag" && false );
   if( u_which >= _operation_evaluators.size() )
      assert( "No registered evaluator for this operation" && false );
   unique_ptr<op_evaluator>& eval = _operation_evaluators[ u_which ];
   if( !eval )
      assert( "No registered evaluator for this operation" && false );
   auto op_id = push_applied_operation( op );
   auto result = eval->evaluate( eval_state, op, true );
   set_applied_operation_result( op_id, result );
   return result;
} FC_CAPTURE_AND_RETHROW(  ) }

const witness_object& database::validate_block_header( uint32_t skip, const signed_block& next_block )const
{
   FC_ASSERT( _pending_block.previous == next_block.previous, "", ("pending.prev",_pending_block.previous)("next.prev",next_block.previous) );
   FC_ASSERT( _pending_block.timestamp <= next_block.timestamp, "", ("_pending_block.timestamp",_pending_block.timestamp)("next",next_block.timestamp)("blocknum",next_block.block_num()) );
   const witness_object& witness = next_block.witness(*this);

   if( !(skip&skip_witness_signature) ) 
      FC_ASSERT( next_block.validate_signee( witness.signing_key ) );

   if( !skip_witness_schedule_check )
   {
      uint32_t slot_num = get_slot_at_time( next_block.timestamp );
      FC_ASSERT( slot_num > 0 );

      witness_id_type scheduled_witness = get_scheduled_witness( slot_num );
      FC_ASSERT( next_block.witness == scheduled_witness );
   }

   return witness;
}

void database::create_block_summary(const signed_block& next_block)
{
   block_summary_id_type sid(next_block.block_num() & 0xffff );
   modify( sid(*this), [&](block_summary_object& p) {
         p.block_id = next_block.id();
   });
}

void database::add_checkpoints( const flat_map<uint32_t,block_id_type>& checkpts )
{
   for( const auto& i : checkpts )
      _checkpoints[i.first] = i.second;
}

} }
