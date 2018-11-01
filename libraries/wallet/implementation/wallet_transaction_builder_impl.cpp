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

namespace graphene { namespace wallet {

namespace detail {

pair<transaction_id_type,signed_transaction> wallet_api_impl::broadcast_transaction(signed_transaction tx)
{
      try {
         _remote_net_broadcast->broadcast_transaction(tx);
      }
      catch (const fc::exception& e) {
         elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()));
         throw;
      }
      return std::make_pair(tx.id(),tx);
}

signed_transaction wallet_api_impl::sign_transaction(signed_transaction tx, bool broadcast)
{
   set<public_key_type> pks = _remote_db->get_potential_signatures( tx );
   flat_set<public_key_type> owned_keys;
   owned_keys.reserve( pks.size() );
   std::copy_if( pks.begin(), pks.end(), std::inserter(owned_keys, owned_keys.end()),
                  [this](const public_key_type& pk){ return _keys.find(pk) != _keys.end(); } );
   tx.clear_signatures();
   set<public_key_type> approving_key_set = _remote_db->get_required_signatures( tx, owned_keys );

   auto dyn_props = get_dynamic_global_properties();
   tx.set_reference_block( dyn_props.head_block_id );

   // first, some bookkeeping, expire old items from _recently_generated_transactions
   // since transactions include the head block id, we just need the index for keeping transactions unique
   // when there are multiple transactions in the same block.  choose a time period that should be at
   // least one block long, even in the worst case.  2 minutes ought to be plenty.
   fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
   auto oldest_transaction_record_iter = _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
   auto begin_iter = _recently_generated_transactions.get<timestamp_index>().begin();
   _recently_generated_transactions.get<timestamp_index>().erase(begin_iter, oldest_transaction_record_iter);

   uint32_t expiration_time_offset = 0;
   for (;;)
   {
      tx.set_expiration( dyn_props.time + fc::seconds(30 + expiration_time_offset) );
      tx.clear_signatures();

      for( const public_key_type& key : approving_key_set )
         tx.sign( get_private_key(key), _chain_id );

      graphene::chain::transaction_id_type this_transaction_id = tx.id();
      auto iter = _recently_generated_transactions.find(this_transaction_id);
      if (iter == _recently_generated_transactions.end())
      {
         // we haven't generated this transaction before, the usual case
         recently_generated_transaction_record this_transaction_record;
         this_transaction_record.generation_time = dyn_props.time;
         this_transaction_record.transaction_id = this_transaction_id;
         _recently_generated_transactions.insert(this_transaction_record);
         break;
      }

      // else we've generated a dupe, increment expiration time and re-sign it
      ++expiration_time_offset;
   }

   if( broadcast )
   {
      try
      {
         _remote_net_broadcast->broadcast_transaction( tx );
      }
      catch (const fc::exception& e)
      {
         elog("Caught exception while broadcasting tx ${id}:  ${e}", ("id", tx.id().str())("e", e.to_detail_string()) );
         throw;
      }
   }

   return tx;
}

signed_transaction wallet_api_impl::propose_builder_transaction2(
   transaction_handle_type handle,
   string account_name_or_id,
   time_point_sec expiration,
   uint32_t review_period_seconds, bool broadcast)
{
   FC_ASSERT(_builder_transactions.count(handle));
   proposal_create_operation op;
   op.fee_paying_account = get_account(account_name_or_id).get_id();
   op.expiration_time = expiration;
   signed_transaction& trx = _builder_transactions[handle];
   std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                  [](const operation& op) -> op_wrapper { return op; });
   if( review_period_seconds )
      op.review_period_seconds = review_period_seconds;
   trx.operations = {op};
   _remote_db->get_global_properties().parameters.current_fees->set_fee( trx.operations.front() );

   return trx = sign_transaction(trx, broadcast);
}

void wallet_api_impl::remove_builder_transaction(transaction_handle_type handle)
{
   _builder_transactions.erase(handle);
}

signed_transaction wallet_api_impl::propose_builder_transaction(
   transaction_handle_type handle,
   time_point_sec expiration,
   uint32_t review_period_seconds, bool broadcast)
{
   FC_ASSERT(_builder_transactions.count(handle));
   proposal_create_operation op;
   op.expiration_time = expiration;
   signed_transaction& trx = _builder_transactions[handle];
   std::transform(trx.operations.begin(), trx.operations.end(), std::back_inserter(op.proposed_ops),
                  [](const operation& op) -> op_wrapper { return op; });
   if( review_period_seconds )
      op.review_period_seconds = review_period_seconds;
   trx.operations = {op};
   _remote_db->get_global_properties().parameters.current_fees->set_fee( trx.operations.front() );

   return trx = sign_transaction(trx, broadcast);
}

transaction_handle_type wallet_api_impl::begin_builder_transaction()
{
   int trx_handle = _builder_transactions.empty()? 0
                                                   : (--_builder_transactions.end())->first + 1;
   _builder_transactions[trx_handle];
   return trx_handle;
}
void wallet_api_impl::add_operation_to_builder_transaction(transaction_handle_type transaction_handle, const operation& op)
{
   FC_ASSERT(_builder_transactions.count(transaction_handle));
   _builder_transactions[transaction_handle].operations.emplace_back(op);
}
void wallet_api_impl::replace_operation_in_builder_transaction(transaction_handle_type handle,
                                                uint32_t operation_index,
                                                const operation& new_op)
{
   FC_ASSERT(_builder_transactions.count(handle));
   signed_transaction& trx = _builder_transactions[handle];
   FC_ASSERT( operation_index < trx.operations.size());
   trx.operations[operation_index] = new_op;
}
asset wallet_api_impl::set_fees_on_builder_transaction(transaction_handle_type handle, string fee_asset)
{
   FC_ASSERT(_builder_transactions.count(handle));

   auto fee_asset_obj = get_asset(fee_asset);
   asset total_fee = fee_asset_obj.amount(0);

   auto gprops = _remote_db->get_global_properties().parameters;
   if( fee_asset_obj.get_id() != asset_id_type() )
   {
      for( auto& op : _builder_transactions[handle].operations )
         total_fee += gprops.current_fees->set_fee( op, fee_asset_obj.options.core_exchange_rate );

      FC_ASSERT((total_fee * fee_asset_obj.options.core_exchange_rate).amount <=
                  get_object<asset_dynamic_data_object>(fee_asset_obj.dynamic_asset_data_id).fee_pool,
                  "Cannot pay fees in ${asset}, as this asset's fee pool is insufficiently funded.",
                  ("asset", fee_asset_obj.symbol));
   } else {
      for( auto& op : _builder_transactions[handle].operations )
         total_fee += gprops.current_fees->set_fee( op );
   }

   return total_fee;
}
transaction wallet_api_impl::preview_builder_transaction(transaction_handle_type handle)
{
   FC_ASSERT(_builder_transactions.count(handle));
   return _builder_transactions[handle];
}
signed_transaction wallet_api_impl::sign_builder_transaction(transaction_handle_type transaction_handle, bool broadcast)
{
   FC_ASSERT(_builder_transactions.count(transaction_handle));

   return _builder_transactions[transaction_handle] = sign_transaction(_builder_transactions[transaction_handle], broadcast);
}

}}} // graphene::wallet::detail
