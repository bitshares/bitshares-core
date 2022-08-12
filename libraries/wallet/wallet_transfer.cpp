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
#include "wallet_api_impl.hpp"
#include <graphene/wallet/wallet.hpp>

/***
 * Methods to handle transfers / exchange orders
 */

namespace graphene { namespace wallet { namespace detail {

   /***
    * @brief a helper method to create an htlc_hash from an algo/hash combination
    */
   htlc_hash wallet_api_impl::do_hash( const string& algorithm, const std::string& hash )
   {
      string name_upper;
      std::transform( algorithm.begin(), algorithm.end(), std::back_inserter(name_upper), ::toupper);
      if( name_upper == "RIPEMD160" )
         return fc::ripemd160( hash );
      if( name_upper == "SHA256" )
         return fc::sha256( hash );
      if( name_upper == "SHA1" )
         return fc::sha1( hash );
      if( name_upper == "HASH160" )
         return fc::hash160( hash );
      FC_THROW_EXCEPTION( fc::invalid_arg_exception, "Unknown algorithm '${a}'", ("a",algorithm) );
   }

   signed_transaction wallet_api_impl::transfer(string from, string to, string amount,
                               string asset_symbol, string memo, bool broadcast )
   { try {
      FC_ASSERT( !self.is_locked() );
      fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
      FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

      account_object from_account = get_account(from);
      account_object to_account = get_account(to);
      account_id_type from_id = from_account.id;
      account_id_type to_id = to_account.id;

      transfer_operation xfer_op;

      xfer_op.from = from_id;
      xfer_op.to = to_id;
      xfer_op.amount = asset_obj->amount_from_string(amount);

      if( memo.size() )
         {
            xfer_op.memo = memo_data();
            xfer_op.memo->from = from_account.options.memo_key;
            xfer_op.memo->to = to_account.options.memo_key;
            xfer_op.memo->set_message(get_private_key(from_account.options.memo_key),
                                      to_account.options.memo_key, memo);
         }

      signed_transaction tx;
      tx.operations.push_back(xfer_op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (from)(to)(amount)(asset_symbol)(memo)(broadcast) ) }

   signed_transaction wallet_api_impl::htlc_create( string source, string destination, string amount,
         string asset_symbol, string hash_algorithm, const std::string& preimage_hash, uint32_t preimage_size,
         const uint32_t claim_period_seconds, const std::string& memo, bool broadcast )
   {
      try
      {
         FC_ASSERT( !self.is_locked() );
         fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
         FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

         const account_object& from_acct = get_account(source);
         const account_object& to_acct = get_account(destination);
         htlc_create_operation create_op;
         create_op.from = get_account(source).id;
         create_op.to = get_account(destination).id;
         create_op.amount = asset_obj->amount_from_string(amount);
         create_op.claim_period_seconds = claim_period_seconds;
         create_op.preimage_hash = do_hash( hash_algorithm, preimage_hash );
         create_op.preimage_size = preimage_size;
         if (!memo.empty())
         {
            memo_data data;
            data.from = from_acct.options.memo_key;
            data.to = to_acct.options.memo_key;
            data.set_message( 
                  get_private_key(from_acct.options.memo_key), to_acct.options.memo_key, memo);
            create_op.extensions.value.memo = data;
         }

         signed_transaction tx;
         tx.operations.push_back(create_op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (source)(destination)(amount)(asset_symbol)(hash_algorithm)
            (preimage_hash)(preimage_size)(claim_period_seconds)(broadcast) )
   }

   signed_transaction wallet_api_impl::htlc_redeem( string htlc_id, string issuer,
         const std::vector<char>& preimage, bool broadcast )
   {
      try
      {
         FC_ASSERT( !self.is_locked() );
         fc::optional<htlc_object> htlc_obj = get_htlc(htlc_id);
         FC_ASSERT(htlc_obj, "Could not find HTLC matching ${htlc}", ("htlc", htlc_id));

         account_object issuer_obj = get_account(issuer);

         htlc_redeem_operation update_op;
         update_op.htlc_id = htlc_obj->id;
         update_op.redeemer = issuer_obj.id;
         update_op.preimage = preimage;

         signed_transaction tx;
         tx.operations.push_back(update_op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (htlc_id)(issuer)(preimage)(broadcast) )
   }

   signed_transaction wallet_api_impl::htlc_extend ( string htlc_id, string issuer, const uint32_t seconds_to_add,
         bool broadcast)
   {
      try
      {
         FC_ASSERT( !self.is_locked() );
         fc::optional<htlc_object> htlc_obj = get_htlc(htlc_id);
         FC_ASSERT(htlc_obj, "Could not find HTLC matching ${htlc}", ("htlc", htlc_id));

         account_object issuer_obj = get_account(issuer);

         htlc_extend_operation update_op;
         update_op.htlc_id = htlc_obj->id;
         update_op.update_issuer = issuer_obj.id;
         update_op.seconds_to_add = seconds_to_add;

         signed_transaction tx;
         tx.operations.push_back(update_op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (htlc_id)(issuer)(seconds_to_add)(broadcast) )
   }

   fc::optional<htlc_object> wallet_api_impl::get_htlc(string htlc_id) const
   {
      htlc_id_type id;
      fc::from_variant(htlc_id, id);
      auto obj = _remote_db->get_objects( { id }, {}).front();
      if ( !obj.is_null() )
      {
         return fc::optional<htlc_object>(obj.template as<htlc_object>(GRAPHENE_MAX_NESTED_OBJECTS));
      }
      return fc::optional<htlc_object>();
   }

   signed_transaction wallet_api_impl::sell_asset(string seller_account, string amount_to_sell,
         string symbol_to_sell, string min_to_receive, string symbol_to_receive,
         uint32_t timeout_sec, bool fill_or_kill, bool broadcast )
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
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   }

   signed_transaction wallet_api_impl::borrow_asset(string seller_name, string amount_to_borrow, 
         string asset_symbol, string amount_of_collateral, bool broadcast )
   {
      return borrow_asset_ext( seller_name, amount_to_borrow, asset_symbol, amount_of_collateral,
                              {}, broadcast );
   }

   signed_transaction wallet_api_impl::borrow_asset_ext( string seller_name, string amount_to_borrow, 
         string asset_symbol, string amount_of_collateral,
         call_order_update_operation::extensions_type extensions, bool broadcast )
   {
      account_object seller = get_account(seller_name);
      auto mia = get_asset(asset_symbol);
      FC_ASSERT(mia.is_market_issued());
      auto collateral = get_asset(get_object(*mia.bitasset_data_id).options.short_backing_asset);

      call_order_update_operation op;
      op.funding_account = seller.id;
      op.delta_debt   = mia.amount_from_string(amount_to_borrow);
      op.delta_collateral = collateral.amount_from_string(amount_of_collateral);
      op.extensions = extensions;

      signed_transaction trx;
      trx.operations = {op};
      set_operation_fees( trx, _remote_db->get_global_properties().parameters.get_current_fees());
      trx.validate();

      return sign_transaction(trx, broadcast);
   }

   signed_transaction wallet_api_impl::cancel_order(limit_order_id_type order_id, bool broadcast )
   { try {
         FC_ASSERT(!is_locked());
         signed_transaction trx;

         limit_order_cancel_operation op;
         op.fee_paying_account = get_object(order_id).seller;
         op.order = order_id;
         trx.operations = {op};
         set_operation_fees( trx, _remote_db->get_global_properties().parameters.get_current_fees());

         trx.validate();
         return sign_transaction(trx, broadcast);
   } FC_CAPTURE_AND_RETHROW((order_id)) }

   signed_transaction wallet_api_impl::withdraw_vesting( string witness_name, string amount, string asset_symbol,
         bool broadcast )
   { try {
      auto asset_obj = get_asset( asset_symbol );
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>(witness_name);
      if( !vbid )
      {
         witness_object wit = get_witness( witness_name );
         FC_ASSERT( wit.pay_vb );
         vbid = wit.pay_vb;
      }

      vesting_balance_object vbo = get_object( *vbid );
      vesting_balance_withdraw_operation vesting_balance_withdraw_op;

      vesting_balance_withdraw_op.vesting_balance = *vbid;
      vesting_balance_withdraw_op.owner = vbo.owner;
      vesting_balance_withdraw_op.amount = asset_obj.amount_from_string(amount);

      signed_transaction tx;
      tx.operations.push_back( vesting_balance_withdraw_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (witness_name)(amount) )
   }

}}} // namespace graphene::wallet::detail
