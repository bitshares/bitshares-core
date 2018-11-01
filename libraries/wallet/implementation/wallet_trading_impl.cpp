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

signed_transaction wallet_api_impl::bid_collateral(string bidder_name,
                                    string debt_amount, string debt_symbol,
                                    string additional_collateral,
                                    bool broadcast )
{ try {
   optional<asset_object> debt_asset = find_asset(debt_symbol);
   if (!debt_asset)
      FC_THROW("No asset with that symbol exists!");
   const asset_object& collateral = get_asset(get_object(*debt_asset->bitasset_data_id).options.short_backing_asset);

   bid_collateral_operation op;
   op.bidder = get_account_id(bidder_name);
   op.debt_covered = debt_asset->amount_from_string(debt_amount);
   op.additional_collateral = collateral.amount_from_string(additional_collateral);

   signed_transaction tx;
   tx.operations.push_back( op );
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (bidder_name)(debt_amount)(debt_symbol)(additional_collateral)(broadcast) ) }

signed_transaction wallet_api_impl::sell_asset(string seller_account,
                                 string amount_to_sell,
                                 string symbol_to_sell,
                                 string min_to_receive,
                                 string symbol_to_receive,
                                 uint32_t timeout_sec,
                                 bool   fill_or_kill,
                                 bool   broadcast)
   {
      account_object seller   = get_account( seller_account );

      limit_order_create_operation op;
      op.seller = seller.id;
      op.amount_to_sell = get_asset(symbol_to_sell).amount_from_string(amount_to_sell);
      op.min_to_receive = get_asset(symbol_to_receive).amount_from_string(min_to_receive);
      if( timeout_sec )
         op.expiration = fc::time_point::now() + fc::seconds(timeout_sec);
      op.fill_or_kill = fill_or_kill;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.current_fees);
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction wallet_api_impl::borrow_asset(string seller_name, string amount_to_borrow, string asset_symbol,
                                       string amount_of_collateral, bool broadcast)
   {
      account_object seller = get_account(seller_name);
      asset_object mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      asset_object collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.current_fees);
      trx.validate();

      return sign_transaction(trx, broadcast);
   }

   signed_transaction wallet_api_impl::borrow_asset_ext( string seller_name, string amount_to_borrow, string asset_symbol,
                                        string amount_of_collateral,
                                        call_order_update_operation::extensions_type extensions,
                                        bool broadcast)
   {
      account_object seller = get_account(seller_name);
      asset_object mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      asset_object collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);
      op.extensions = extensions;

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.current_fees);
      trx.validate();

      return sign_transaction(trx, broadcast);
   }

   signed_transaction wallet_api_impl::cancel_order(object_id_type order_id, bool broadcast)
   { try {
         FC_ASSERT(!is_locked());
         FC_ASSERT(order_id.space() == protocol_ids, "Invalid order ID ${id}", ("id", order_id));
         signed_transaction trx;

         limit_order_cancel_operation op;
         op.fee_paying_account = get_object<limit_order_object>(order_id).seller;
         op.order = order_id;
         trx.operations = {op};
         set_operation_fees( trx, _remote_db->get_global_properties().parameters.current_fees);

         trx.validate();
         return sign_transaction(trx, broadcast);
   } FC_CAPTURE_AND_RETHROW((order_id)) }

}}} // graphene::wallet::detail
