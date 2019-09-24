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

signed_transaction wallet_api_impl::register_account(string name, public_key_type owner,
      public_key_type active, string  registrar_account, string  referrer_account,
      uint32_t referrer_percent, bool broadcast )
{ try {
   FC_ASSERT( !self.is_locked() );
   FC_ASSERT( is_valid_name(name) );
   account_create_operation account_create_op;

   // #449 referrer_percent is on 0-100 scale, if user has larger
   // number it means their script is using GRAPHENE_100_PERCENT scale
   // instead of 0-100 scale.
   FC_ASSERT( referrer_percent <= 100 );
   // TODO:  process when pay_from_account is ID

   account_object registrar_account_object =
         this->get_account( registrar_account );
   FC_ASSERT( registrar_account_object.is_lifetime_member() );

   account_id_type registrar_account_id = registrar_account_object.id;

   account_object referrer_account_object =
         this->get_account( referrer_account );
   account_create_op.referrer = referrer_account_object.id;
   account_create_op.referrer_percent = uint16_t( referrer_percent * GRAPHENE_1_PERCENT );

   account_create_op.registrar = registrar_account_id;
   account_create_op.name = name;
   account_create_op.owner = authority(1, owner, 1);
   account_create_op.active = authority(1, active, 1);
   account_create_op.options.memo_key = active;

   signed_transaction tx;
   tx.operations.push_back( account_create_op );
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
   tx.validate();

   return sign_transaction(tx, broadcast);
} FC_CAPTURE_AND_RETHROW( (name)(owner)(active)(registrar_account)
                           (referrer_account)(referrer_percent)(broadcast) ) }

signed_transaction wallet_api_impl::upgrade_account(string name, bool broadcast)
{ try {
   FC_ASSERT( !self.is_locked() );
   account_object account_obj = get_account(name);
   FC_ASSERT( !account_obj.is_lifetime_member() );

   signed_transaction tx;
   account_upgrade_operation op;
   op.account_to_upgrade = account_obj.get_id();
   op.upgrade_to_lifetime_member = true;
   tx.operations = {op};
   set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
   tx.validate();

   return sign_transaction( tx, broadcast );
} FC_CAPTURE_AND_RETHROW( (name) ) }

signed_transaction wallet_api_impl::create_account_with_brain_key(string brain_key,
                                                   string account_name,
                                                   string registrar_account,
                                                   string referrer_account,
                                                   bool broadcast,
                                                   bool save_wallet )
{ try {
   FC_ASSERT( !self.is_locked() );
   string normalized_brain_key = normalize_brain_key( brain_key );
   // TODO:  scan blockchain for accounts that exist with same brain key
   fc::ecc::private_key owner_privkey = derive_private_key( normalized_brain_key, 0 );
   return create_account_with_private_key(owner_privkey, account_name, registrar_account,
                                          referrer_account, broadcast, save_wallet);
} FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account) ) }

signed_transaction wallet_api_impl::account_store_map(string account, string catalog, bool remove,
      flat_map<string, optional<string>> key_values, bool broadcast)
{
   try
   {
      FC_ASSERT( !self.is_locked() );

      account_id_type account_id = get_account(account).id;

      custom_operation op;
      account_storage_map store;
      store.catalog = catalog;
      store.remove = remove;
      store.key_values = key_values;

      custom_plugin_operation custom_plugin_op(store);
      auto packed = fc::raw::pack(custom_plugin_op);

      op.payer = account_id;
      op.data = packed;

      signed_transaction tx;
      tx.operations.push_back(op);
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction(tx, broadcast);

   } FC_CAPTURE_AND_RETHROW( (account)(remove)(catalog)(key_values)(broadcast) )
}

}}} // namespace graphene::wallet::detail
