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
#include <graphene/wallet/wallet_api_impl.hpp>
#include <graphene/wallet/wallet.hpp>

namespace graphene { namespace wallet { namespace detail {

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

signed_transaction wallet_api_impl::htlc_create( string source, string destination, string amount, string asset_symbol,
         string hash_algorithm, const std::string& preimage_hash, uint32_t preimage_size,
         const uint32_t claim_period_seconds, bool broadcast )
   {
      try
      {
         FC_ASSERT( !self.is_locked() );
         fc::optional<asset_object> asset_obj = get_asset(asset_symbol);
         FC_ASSERT(asset_obj, "Could not find asset matching ${asset}", ("asset", asset_symbol));

         htlc_create_operation create_op;
         create_op.from = get_account(source).id;
         create_op.to = get_account(destination).id;
         create_op.amount = asset_obj->amount_from_string(amount);
         create_op.claim_period_seconds = claim_period_seconds;
         create_op.preimage_hash = do_hash( hash_algorithm, preimage_hash );
         create_op.preimage_size = preimage_size;

         signed_transaction tx;
         tx.operations.push_back(create_op);
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         return sign_transaction(tx, broadcast);
      } FC_CAPTURE_AND_RETHROW( (source)(destination)(amount)(asset_symbol)(hash_algorithm)
            (preimage_hash)(preimage_size)(claim_period_seconds)(broadcast) )
   }

   signed_transaction wallet_api_impl::htlc_redeem( string htlc_id, string issuer, const std::vector<char>& preimage, 
         bool broadcast )
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

}}} // namespace graphene::wallet::detail
