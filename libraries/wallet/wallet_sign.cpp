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

#include <fc/crypto/aes.hpp>

#include "wallet_api_impl.hpp"
#include <graphene/wallet/wallet.hpp>

/***
 * These methods handle signing and keys
 */

namespace graphene { namespace wallet { namespace detail {

   string address_to_shorthash( const graphene::protocol::address& addr )
   {
      uint32_t x = addr.addr._hash[0].value();
      static const char hd[] = "0123456789abcdef";
      string result;

      result += hd[(x >> 0x1c) & 0x0f];
      result += hd[(x >> 0x18) & 0x0f];
      result += hd[(x >> 0x14) & 0x0f];
      result += hd[(x >> 0x10) & 0x0f];
      result += hd[(x >> 0x0c) & 0x0f];
      result += hd[(x >> 0x08) & 0x0f];
      result += hd[(x >> 0x04) & 0x0f];
      result += hd[(x        ) & 0x0f];

      return result;
   }

   fc::ecc::private_key derive_private_key( const std::string& prefix_string, int sequence_number )
   {
      std::string sequence_string = std::to_string(sequence_number);
      fc::sha512 h = fc::sha512::hash(prefix_string + " " + sequence_string);
      fc::ecc::private_key derived_key = fc::ecc::private_key::regenerate(fc::sha256::hash(h));
      return derived_key;
   }

   string normalize_brain_key( string s )
   {
      size_t i = 0, n = s.length();
      std::string result;
      char c;
      result.reserve( n );

      bool preceded_by_whitespace = false;
      bool non_empty = false;
      while( i < n )
      {
         c = s[i++];
         switch( c )
         {
         case ' ':  case '\t': case '\r': case '\n': case '\v': case '\f':
            preceded_by_whitespace = true;
            continue;

         case 'a': c = 'A'; break;
         case 'b': c = 'B'; break;
         case 'c': c = 'C'; break;
         case 'd': c = 'D'; break;
         case 'e': c = 'E'; break;
         case 'f': c = 'F'; break;
         case 'g': c = 'G'; break;
         case 'h': c = 'H'; break;
         case 'i': c = 'I'; break;
         case 'j': c = 'J'; break;
         case 'k': c = 'K'; break;
         case 'l': c = 'L'; break;
         case 'm': c = 'M'; break;
         case 'n': c = 'N'; break;
         case 'o': c = 'O'; break;
         case 'p': c = 'P'; break;
         case 'q': c = 'Q'; break;
         case 'r': c = 'R'; break;
         case 's': c = 'S'; break;
         case 't': c = 'T'; break;
         case 'u': c = 'U'; break;
         case 'v': c = 'V'; break;
         case 'w': c = 'W'; break;
         case 'x': c = 'X'; break;
         case 'y': c = 'Y'; break;
         case 'z': c = 'Z'; break;

         default:
            break;
         }
         if( preceded_by_whitespace && non_empty )
            result.push_back(' ');
         result.push_back(c);
         preceded_by_whitespace = false;
         non_empty = true;
      }
      return result;
   }

   void wallet_api_impl::encrypt_keys()
   {
      if( !is_locked() )
      {
         plain_keys data;
         data.keys = _keys;
         data.checksum = _checksum;
         auto plain_txt = fc::raw::pack(data);
         _wallet.cipher_keys = fc::aes_encrypt( data.checksum, plain_txt );
      }
   }

   memo_data wallet_api_impl::sign_memo(string from, string to, string memo)
   {
      FC_ASSERT( !self.is_locked() );

      memo_data md = memo_data();

      // get account memo key, if that fails, try a pubkey
      try {
         account_object from_account = get_account(from);
         md.from = from_account.options.memo_key;
      } catch (const fc::exception& e) {
         md.from =  self.get_public_key( from );
      }
      // same as above, for destination key
      try {
         account_object to_account = get_account(to);
         md.to = to_account.options.memo_key;
      } catch (const fc::exception& e) {
         md.to = self.get_public_key( to );
      }

      md.set_message(get_private_key(md.from), md.to, memo);
      return md;
   }

   string wallet_api_impl::read_memo(const memo_data& md)
   {
      FC_ASSERT(!is_locked());
      std::string clear_text;

      const memo_data *memo = &md;

      try {
         FC_ASSERT( _keys.count(memo->to) > 0 || _keys.count(memo->from) > 0,
                    "Memo is encrypted to a key ${to} or ${from} not in this wallet.",
                    ("to", memo->to)("from",memo->from) );
         if( _keys.count(memo->to) > 0 ) {
            auto my_key = wif_to_key(_keys.at(memo->to));
            FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
            clear_text = memo->get_message(*my_key, memo->from);
         } else {
            auto my_key = wif_to_key(_keys.at(memo->from));
            FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
            clear_text = memo->get_message(*my_key, memo->to);
         }
      } catch (const fc::exception& e) {
         elog("Error when decrypting memo: ${e}", ("e", e.to_detail_string()));
      }

      return clear_text;
   }

   signed_message wallet_api_impl::sign_message(string signer, string message)
   {
      FC_ASSERT( !self.is_locked() );

      const account_object from_account = get_account(signer);
      auto dynamic_props = get_dynamic_global_properties();

      signed_message msg;
      msg.message = message;
      msg.meta.account = from_account.name;
      msg.meta.memo_key = from_account.options.memo_key;
      msg.meta.block = dynamic_props.head_block_number;
      msg.meta.time = dynamic_props.time.to_iso_string() + "Z";
      msg.signature = get_private_key( from_account.options.memo_key ).sign_compact( msg.digest() );
      return msg;
   }

   bool wallet_api_impl::verify_message( const string& message, const string& account, int block, const string& time,
                        const compact_signature& sig )
   {
      const account_object from_account = get_account( account );

      signed_message msg;
      msg.message = message;
      msg.meta.account = from_account.name;
      msg.meta.memo_key = from_account.options.memo_key;
      msg.meta.block = block;
      msg.meta.time = time;
      msg.signature = sig;

      return verify_signed_message( msg );
   }

   bool wallet_api_impl::verify_signed_message( const signed_message& message )
   {
      if( !message.signature.valid() ) return false;

      const account_object from_account = get_account( message.meta.account );

      const public_key signer( *message.signature, message.digest() );
      if( !( message.meta.memo_key == signer ) ) return false;
      FC_ASSERT( from_account.options.memo_key == signer,
                 "Message was signed by contained key, but it doesn't belong to the contained account!" );

      return true;
   }

   /* meta contains lines of the form "key=value".
    * Returns the value for the corresponding key, throws if key is not present. */
   static string meta_extract( const string& meta, const string& key )
   {
      FC_ASSERT( meta.size() > key.size(), "Key '${k}' not found!", ("k",key) );
      size_t start;
      if( meta.substr( 0, key.size() ) == key && meta[key.size()] == '=' )
         start = 0;
      else
      {
         start = meta.find( "\n" + key + "=" );
         FC_ASSERT( start != string::npos, "Key '${k}' not found!", ("k",key) );
         ++start;
      }
      start += key.size() + 1;
      size_t lf = meta.find( "\n", start );
      if( lf == string::npos ) lf = meta.size();
      return meta.substr( start, lf - start );
   }

   bool wallet_api_impl::verify_encapsulated_message( const string& message )
   {
      signed_message msg;
      size_t begin_p = message.find( ENC_HEADER );
      FC_ASSERT( begin_p != string::npos, "BEGIN MESSAGE line not found!" );
      size_t meta_p = message.find( ENC_META, begin_p );
      FC_ASSERT( meta_p != string::npos, "BEGIN META line not found!" );
      FC_ASSERT( meta_p >= begin_p + ENC_HEADER.size() + 1, "Missing message!?" );
      size_t sig_p = message.find( ENC_SIG, meta_p );
      FC_ASSERT( sig_p != string::npos, "BEGIN SIGNATURE line not found!" );
      FC_ASSERT( sig_p >= meta_p + ENC_META.size(), "Missing metadata?!" );
      size_t end_p = message.find( ENC_FOOTER, meta_p );
      FC_ASSERT( end_p != string::npos, "END MESSAGE line not found!" );
      FC_ASSERT( end_p >= sig_p + ENC_SIG.size() + 1, "Missing signature?!" );

      msg.message = message.substr( begin_p + ENC_HEADER.size(), meta_p - begin_p - ENC_HEADER.size() - 1 );
      const string meta = message.substr( meta_p + ENC_META.size(), sig_p - meta_p - ENC_META.size() );
      const string sig = message.substr( sig_p + ENC_SIG.size(), end_p - sig_p - ENC_SIG.size() - 1 );

      msg.meta.account = meta_extract( meta, "account" );
      msg.meta.memo_key = public_key_type( meta_extract( meta, "memokey" ) );
      msg.meta.block = boost::lexical_cast<uint32_t>( meta_extract( meta, "block" ) );
      msg.meta.time = meta_extract( meta, "timestamp" );
      msg.signature = variant(sig).as< fc::ecc::compact_signature >( 5 );

      return verify_signed_message( msg );
   }

   signed_transaction wallet_api_impl::add_transaction_signature( signed_transaction tx, 
         bool broadcast )
   {
      set<public_key_type> approving_key_set = get_owned_required_keys(tx, false);

      if ( ( ( tx.ref_block_num == 0 && tx.ref_block_prefix == 0 ) ||
             tx.expiration == fc::time_point_sec() ) &&
           tx.signatures.empty() )
      {
         auto dyn_props = get_dynamic_global_properties();
         auto parameters = get_global_properties().parameters;
         fc::time_point_sec now = dyn_props.time;
         tx.set_reference_block( dyn_props.head_block_id );
         tx.set_expiration( now + parameters.maximum_time_until_expiration );
      }
      for ( const public_key_type &key : approving_key_set )
         tx.sign( get_private_key( key ), _chain_id );

      if ( broadcast )
      {
         try
         {
            _remote_net_broadcast->broadcast_transaction( tx );
         }
         catch ( const fc::exception &e )
         {
            elog( "Caught exception while broadcasting tx ${id}:  ${e}",
                  ( "id", tx.id().str() )( "e", e.to_detail_string() ) );
            FC_THROW( "Caught exception while broadcasting tx" );
         }
      }

      return tx;
   }

   signed_transaction wallet_api_impl::sign_transaction( signed_transaction tx, bool broadcast )
   {
      return sign_transaction2(tx, {}, broadcast);
   }

   signed_transaction wallet_api_impl::sign_transaction2( signed_transaction tx,
                                                         const vector<public_key_type>& signing_keys, bool broadcast)
   {
      set<public_key_type> approving_key_set = get_owned_required_keys(tx);

      // Add any explicit keys to the approving_key_set
      for (const public_key_type& explicit_key : signing_keys) {
         approving_key_set.insert(explicit_key);
      }

      auto dyn_props = get_dynamic_global_properties();
      tx.set_reference_block( dyn_props.head_block_id );

      // first, some bookkeeping, expire old items from _recently_generated_transactions
      // since transactions include the head block id, we just need the index for keeping transactions unique
      // when there are multiple transactions in the same block.  choose a time period that should be at
      // least one block long, even in the worst case.  2 minutes ought to be plenty.
      fc::time_point_sec oldest_transaction_ids_to_track(dyn_props.time - fc::minutes(2));
      auto oldest_transaction_record_iter =
            _recently_generated_transactions.get<timestamp_index>().lower_bound(oldest_transaction_ids_to_track);
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
            elog("Caught exception while broadcasting tx ${id}:  ${e}",
                 ("id", tx.id().str())("e", e.to_detail_string()) );
            throw;
         }
      }

      return tx;
   }

   fc::ecc::private_key wallet_api_impl::get_private_key(const public_key_type& id)const
   {
      auto it = _keys.find(id);
      FC_ASSERT( it != _keys.end() );

      fc::optional< fc::ecc::private_key > privkey = wif_to_key( it->second );
      FC_ASSERT( privkey );
      return *privkey;
   }

   fc::ecc::private_key wallet_api_impl::get_private_key_for_account(const account_object& account)const
   {
      vector<public_key_type> active_keys = account.active.get_keys();
      if (active_keys.size() != 1)
         FC_THROW("Expecting a simple authority with one active key");
      return get_private_key(active_keys.front());
   }

   // imports the private key into the wallet, and associate it in some way (?) with the
   // given account name.
   // @returns true if the key matches a current active/owner/memo key for the named
   //          account, false otherwise (but it is stored either way)
   bool wallet_api_impl::import_key(string account_name_or_id, string wif_key)
   {
      fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
      if (!optional_private_key)
         FC_THROW("Invalid private key");
      graphene::chain::public_key_type wif_pub_key = optional_private_key->get_public_key();

      account_object account = get_account( account_name_or_id );

      // make a list of all current public keys for the named account
      flat_set<public_key_type> all_keys_for_account;
      std::vector<public_key_type> active_keys = account.active.get_keys();
      std::vector<public_key_type> owner_keys = account.owner.get_keys();
      std::copy(active_keys.begin(), active_keys.end(),
            std::inserter(all_keys_for_account, all_keys_for_account.end()));
      std::copy(owner_keys.begin(), owner_keys.end(),
            std::inserter(all_keys_for_account, all_keys_for_account.end()));
      all_keys_for_account.insert(account.options.memo_key);

      _keys[wif_pub_key] = wif_key;

      _wallet.update_account(account);

      _wallet.extra_keys[account.id].insert(wif_pub_key);

      return all_keys_for_account.find(wif_pub_key) != all_keys_for_account.end();
   }

   /**
    * Get the required public keys to sign the transaction which had been
    * owned by us
    *
    * NOTE, if `erase_existing_sigs` set to true, the original trasaction's
    * signatures will be erased
    *
    * @param tx           The transaction to be signed
    * @param erase_existing_sigs
    *        The transaction could have been partially signed already,
    *        if set to false, the corresponding public key of existing
    *        signatures won't be returned.
    *        If set to true, the existing signatures will be erased and
    *        all required keys returned.
   */
   set<public_key_type> wallet_api_impl::get_owned_required_keys( signed_transaction &tx,
         bool erase_existing_sigs )
   {
      set<public_key_type> pks = _remote_db->get_potential_signatures( tx );
      flat_set<public_key_type> owned_keys;
      owned_keys.reserve( pks.size() );
      std::copy_if( pks.begin(), pks.end(),
                    std::inserter( owned_keys, owned_keys.end() ),
                    [this]( const public_key_type &pk ) {
                       return _keys.find( pk ) != _keys.end();
                    } );

      if ( erase_existing_sigs )
         tx.signatures.clear();

      return _remote_db->get_required_signatures( tx, owned_keys );
   }

   flat_set<public_key_type> wallet_api_impl::get_transaction_signers(const signed_transaction &tx) const
   {
      return tx.get_signature_keys(_chain_id);
   }

   signed_transaction wallet_api_impl::approve_proposal( const string& fee_paying_account, const string& proposal_id,
         const approval_delta& delta, bool broadcast )
   {
      proposal_update_operation update_op;

      update_op.fee_paying_account = get_account(fee_paying_account).id;
      update_op.proposal = fc::variant(proposal_id, 1).as<proposal_id_type>( 1 );
      // make sure the proposal exists
      get_object( update_op.proposal );

      for( const std::string& name : delta.active_approvals_to_add )
         update_op.active_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.active_approvals_to_remove )
         update_op.active_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_add )
         update_op.owner_approvals_to_add.insert( get_account( name ).id );
      for( const std::string& name : delta.owner_approvals_to_remove )
         update_op.owner_approvals_to_remove.insert( get_account( name ).id );
      for( const std::string& k : delta.key_approvals_to_add )
         update_op.key_approvals_to_add.insert( public_key_type( k ) );
      for( const std::string& k : delta.key_approvals_to_remove )
         update_op.key_approvals_to_remove.insert( public_key_type( k ) );

      signed_transaction tx;
      tx.operations.push_back(update_op);
      set_operation_fees(tx, get_global_properties().parameters.get_current_fees());
      tx.validate();
      return sign_transaction(tx, broadcast);
   }

}}} // namespace graphene::wallet::detail
