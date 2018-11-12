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
