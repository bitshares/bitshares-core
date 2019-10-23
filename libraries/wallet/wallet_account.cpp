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
#include <boost/range/algorithm_ext/erase.hpp>
#include <boost/range/algorithm/unique.hpp>
#include <boost/range/algorithm/sort.hpp>

#include "wallet_api_impl.hpp"
#include <graphene/wallet/wallet.hpp>
#include <graphene/protocol/pts_address.hpp>

/****
 * Wallet API methods to handle accounts
 */

namespace graphene { namespace wallet { namespace detail {

   std::string wallet_api_impl::account_id_to_string(account_id_type id) const
   {
      std::string account_id = fc::to_string(id.space_id)
                               + "." + fc::to_string(id.type_id)
                               + "." + fc::to_string(id.instance.value);
      return account_id;
   }

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

   void wallet_api_impl::claim_registered_account(const graphene::chain::account_object& account)
   {
      bool import_keys = false;
      auto it = _wallet.pending_account_registrations.find( account.name );
      FC_ASSERT( it != _wallet.pending_account_registrations.end() );
      for (const std::string& wif_key : it->second) {
         if( !import_key( account.name, wif_key ) )
         {
            // somebody else beat our pending registration, there is
            //    nothing we can do except log it and move on
            elog( "account ${name} registered by someone else first!",
                  ("name", account.name) );
            // might as well remove it from pending regs,
            //    because there is now no way this registration
            //    can become valid (even in the extremely rare
            //    possibility of migrating to a fork where the
            //    name is available, the user can always
            //    manually re-register)
         } else {
            import_keys = true;
         }
      }
      _wallet.pending_account_registrations.erase( it );

      if( import_keys ) {
         save_wallet_file();
      }
   }

   // after a witness registration succeeds, this saves the private key in the wallet permanently
   //
   void wallet_api_impl::claim_registered_witness(const std::string& witness_name)
   {
      auto iter = _wallet.pending_witness_registrations.find(witness_name);
      FC_ASSERT(iter != _wallet.pending_witness_registrations.end());
      std::string wif_key = iter->second;

      // get the list key id this key is registered with in the chain
      fc::optional<fc::ecc::private_key> witness_private_key = wif_to_key(wif_key);
      FC_ASSERT(witness_private_key);

      auto pub_key = witness_private_key->get_public_key();
      _keys[pub_key] = wif_key;
      _wallet.pending_witness_registrations.erase(iter);
   }

   account_object wallet_api_impl::get_account(account_id_type id) const
   {
      std::string account_id = account_id_to_string(id);

      auto rec = _remote_db->get_accounts({account_id}, {}).front();
      FC_ASSERT(rec);
      return *rec;
   }

   account_object wallet_api_impl::get_account(string account_name_or_id) const
   {
      FC_ASSERT( account_name_or_id.size() > 0 );

      if( auto id = maybe_id<account_id_type>(account_name_or_id) )
      {
         // It's an ID
         return get_account(*id);
      } else {
         auto rec = _remote_db->lookup_account_names({account_name_or_id}).front();
         FC_ASSERT( rec && rec->name == account_name_or_id );
         return *rec;
      }
   }

   account_id_type wallet_api_impl::get_account_id(string account_name_or_id) const
   {
      return get_account(account_name_or_id).get_id();
   }

   signed_transaction wallet_api_impl::create_account_with_private_key(fc::ecc::private_key owner_privkey,
         string account_name, string registrar_account, string referrer_account, bool broadcast,
         bool save_wallet )
   { try {
         int active_key_index = find_first_unused_derived_key_index(owner_privkey);
         fc::ecc::private_key active_privkey = derive_private_key( key_to_wif(owner_privkey), active_key_index);

         int memo_key_index = find_first_unused_derived_key_index(active_privkey);
         fc::ecc::private_key memo_privkey = derive_private_key( key_to_wif(active_privkey), memo_key_index);

         graphene::chain::public_key_type owner_pubkey = owner_privkey.get_public_key();
         graphene::chain::public_key_type active_pubkey = active_privkey.get_public_key();
         graphene::chain::public_key_type memo_pubkey = memo_privkey.get_public_key();

         account_create_operation account_create_op;

         // TODO:  process when pay_from_account is ID

         account_object registrar_account_object = get_account( registrar_account );

         account_id_type registrar_account_id = registrar_account_object.id;

         account_object referrer_account_object = get_account( referrer_account );
         account_create_op.referrer = referrer_account_object.id;
         account_create_op.referrer_percent = referrer_account_object.referrer_rewards_percentage;

         account_create_op.registrar = registrar_account_id;
         account_create_op.name = account_name;
         account_create_op.owner = authority(1, owner_pubkey, 1);
         account_create_op.active = authority(1, active_pubkey, 1);
         account_create_op.options.memo_key = memo_pubkey;

         // current_fee_schedule()
         // find_account(pay_from_account)

         // account_create_op.fee = account_create_op.calculate_fee(db.current_fee_schedule());

         signed_transaction tx;
         tx.operations.push_back( account_create_op );
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
         tx.validate();

         // we do not insert owner_privkey here because
         //    it is intended to only be used for key recovery
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( active_privkey ));
         _wallet.pending_account_registrations[account_name].push_back(key_to_wif( memo_privkey ));
         if( save_wallet )
            save_wallet_file();
         return sign_transaction(tx, broadcast);
   } FC_CAPTURE_AND_RETHROW( (account_name)(registrar_account)(referrer_account)(broadcast) ) }

   signed_transaction wallet_api_impl::whitelist_account(string authorizing_account, string account_to_list, 
         account_whitelist_operation::account_listing new_listing_status, bool broadcast )
   { try {
      account_whitelist_operation whitelist_op;
      whitelist_op.authorizing_account = get_account_id(authorizing_account);
      whitelist_op.account_to_list = get_account_id(account_to_list);
      whitelist_op.new_listing = new_listing_status;

      signed_transaction tx;
      tx.operations.push_back( whitelist_op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees());
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (authorizing_account)(account_to_list)(new_listing_status)(broadcast) ) }

   vector< vesting_balance_object_with_info > wallet_api_impl::get_vesting_balances( string account_name )
   { try {
      fc::optional<vesting_balance_id_type> vbid = maybe_id<vesting_balance_id_type>( account_name );
      std::vector<vesting_balance_object_with_info> result;
      fc::time_point_sec now = _remote_db->get_dynamic_global_properties().time;

      if( vbid )
      {
         result.emplace_back( get_object(*vbid), now );
         return result;
      }

      vector< vesting_balance_object > vbos = _remote_db->get_vesting_balances( account_name );
      if( vbos.size() == 0 )
         return result;

      for( const vesting_balance_object& vbo : vbos )
         result.emplace_back( vbo, now );

      return result;
   } FC_CAPTURE_AND_RETHROW( (account_name) )
   }

   vector< signed_transaction > wallet_api_impl::import_balance( string name_or_id, 
         const vector<string>& wif_keys, bool broadcast )
   { try {
      FC_ASSERT(!is_locked());
      const dynamic_global_property_object& dpo = _remote_db->get_dynamic_global_properties();
      account_object claimer = get_account( name_or_id );
      uint32_t max_ops_per_tx = 30;

      map< address, private_key_type > keys;  // local index of address -> private key
      vector< address > addrs;
      bool has_wildcard = false;
      addrs.reserve( wif_keys.size() );
      for( const string& wif_key : wif_keys )
      {
         if( wif_key == "*" )
         {
            if( has_wildcard )
               continue;
            for( const public_key_type& pub : _wallet.extra_keys[ claimer.id ] )
            {
               addrs.push_back( pub );
               auto it = _keys.find( pub );
               if( it != _keys.end() )
               {
                  fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
                  FC_ASSERT( privkey );
                  keys[ addrs.back() ] = *privkey;
               }
               else
               {
                  wlog( "Somehow _keys has no private key for extra_keys public key ${k}", ("k", pub) );
               }
            }
            has_wildcard = true;
         }
         else
         {
            optional< private_key_type > key = wif_to_key( wif_key );
            FC_ASSERT( key.valid(), "Invalid private key" );
            fc::ecc::public_key pk = key->get_public_key();
            addrs.push_back( pk );
            keys[addrs.back()] = *key;
            // see chain/balance_evaluator.cpp
            addrs.push_back( pts_address( pk, false, 56 ) );
            keys[addrs.back()] = *key;
            addrs.push_back( pts_address( pk, true, 56 ) );
            keys[addrs.back()] = *key;
            addrs.push_back( pts_address( pk, false, 0 ) );
            keys[addrs.back()] = *key;
            addrs.push_back( pts_address( pk, true, 0 ) );
            keys[addrs.back()] = *key;
         }
      }

      vector< balance_object > balances = _remote_db->get_balance_objects( addrs );
      addrs.clear();

      set<asset_id_type> bal_types;
      for( auto b : balances ) bal_types.insert( b.balance.asset_id );

      struct claim_tx
      {
         vector< balance_claim_operation > ops;
         set< address > addrs;
      };
      vector< claim_tx > claim_txs;

      for( const asset_id_type& a : bal_types )
      {
         balance_claim_operation op;
         op.deposit_to_account = claimer.id;
         for( const balance_object& b : balances )
         {
            if( b.balance.asset_id == a )
            {
               op.total_claimed = b.available( dpo.time );
               if( op.total_claimed.amount == 0 )
                  continue;
               op.balance_to_claim = b.id;
               op.balance_owner_key = keys[b.owner].get_public_key();
               if( (claim_txs.empty()) || (claim_txs.back().ops.size() >= max_ops_per_tx) )
                  claim_txs.emplace_back();
               claim_txs.back().ops.push_back(op);
               claim_txs.back().addrs.insert(b.owner);
            }
         }
      }

      vector< signed_transaction > result;

      for( const claim_tx& ctx : claim_txs )
      {
         signed_transaction tx;
         tx.operations.reserve( ctx.ops.size() );
         for( const balance_claim_operation& op : ctx.ops )
            tx.operations.emplace_back( op );
         set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
         tx.validate();
         signed_transaction signed_tx = sign_transaction( tx, false );
         for( const address& addr : ctx.addrs )
            signed_tx.sign( keys[addr], _chain_id );
         // if the key for a balance object was the same as a key for the account we're importing it into,
         // we may end up with duplicate signatures, so remove those
         boost::erase(signed_tx.signatures, boost::unique<boost::return_found_end>(boost::sort(signed_tx.signatures)));
         result.push_back( signed_tx );
         if( broadcast )
            _remote_net_broadcast->broadcast_transaction(signed_tx);
      }

      return result;
   } FC_CAPTURE_AND_RETHROW( (name_or_id) ) }

   vector<flat_set<account_id_type>> wallet_api_impl::get_key_references(const vector<public_key_type> &keys) const
   {
       return _remote_db->get_key_references(keys);
   }

}}} // namespace graphene::wallet::detail
