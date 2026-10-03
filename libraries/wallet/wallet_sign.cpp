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
#include <graphene/chain/hardfork.hpp>

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
         data.pq_keys = _pq_keys;
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
      } catch (const fc::exception&) {
         // check if the string itself is a pubkey, if not, consider it as a label
         try {
            md.from = public_key_type( from );
         } catch (const fc::exception&) {
            md.from = self.get_public_key( from );
         }
      }
      // same as above, for destination key
      optional<pq_public_key_type> to_pq_key;
      try {
         account_object to_account = get_account(to);
         md.to = to_account.options.memo_key;
         // only an account can publish a KEM key; a bare pubkey or label has nowhere to put one
         to_pq_key = to_account.options.pq_memo_key.value;   // .value: gated wrapper, see fc::pq_gated
      } catch (const fc::exception&) {
         // check if the string itself is a pubkey, if not, consider it as a label
         try {
            md.to = public_key_type( to );
         } catch (const fc::exception&) {
            md.to = self.get_public_key( to );
         }
      }

      // try to get private key of from and sign, if that fails, try to sign with to
      try {
         encrypt_memo_message( md, get_private_key(md.from), to_pq_key, memo );
      } catch (const fc::exception&) {
         std::swap( md.from, md.to );
         // the roles are reversed here, so the recipient is now md.to -- whose KEM key we did
         // not look up. Fall back to the classical construction rather than encapsulating to
         // the wrong party's key.
         md.set_message(get_private_key(md.from), md.to, memo);
         std::swap( md.from, md.to );
      }
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
            clear_text = decrypt_memo_message( *memo, *my_key, memo->from );
         } else {
            auto my_key = wif_to_key(_keys.at(memo->from));
            FC_ASSERT(my_key, "Unable to recover private key to decrypt memo. Wallet may be corrupted.");
            clear_text = decrypt_memo_message( *memo, *my_key, memo->to );
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

   bool wallet_api_impl::verify_message( const string& message, const string& account, int32_t block,
                                         const string& msg_time, const fc::ecc::compact_signature& sig )
   {
      const account_object from_account = get_account( account );

      signed_message msg;
      msg.message = message;
      msg.meta.account = from_account.name;
      msg.meta.memo_key = from_account.options.memo_key;
      msg.meta.block = block;
      msg.meta.time = msg_time;
      msg.signature = sig;

      return verify_signed_message( msg );
   }

   bool wallet_api_impl::verify_signed_message( const signed_message& message )
   {
      if( !message.signature.valid() ) return false;

      const account_object from_account = get_account( message.meta.account );

      const fc::ecc::public_key signer( *message.signature, message.digest() );
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

   bool wallet_api_impl::is_pq_active() const
   {
      // Must mirror the chain-side gate exactly (see account_evaluator.cpp and
      // db_block.cpp): BOTH the hardfork time and the committee flag. Testing only the
      // flag makes the wallet serialize under pq_format::current while the chain still
      // validates under ::legacy, so every transaction carrying an authority hashes to a
      // different digest and is rejected as "Missing Active Authority". A running chain
      // cannot normally reach that state -- proposal_evaluator refuses to set the flag
      // before the hardfork -- but a genesis file can set it directly, which is exactly
      // how a PQ testnet tends to be configured.
      const auto& params = get_global_properties().parameters;
      return HARDFORK_PQ_0_PASSED( get_dynamic_global_properties().time )
             && params.extensions.value.pq_serialization_active.valid()
             && *params.extensions.value.pq_serialization_active;
   }

   fc::raw::pq_format wallet_api_impl::get_pq_format() const
   {
      return is_pq_active() ? fc::raw::pq_format::current : fc::raw::pq_format::legacy;
   }

   void wallet_api_impl::encrypt_memo_message( memo_data& md,
                                               const fc::ecc::private_key& from_priv,
                                               const optional<pq_public_key_type>& to_pq_key,
                                               const string& msg )
   {
      // is_pq_active() is not a nicety here. Before activation memo_data's ciphertext field is
      // stripped on the wire, and the KEM shared secret is half of the AES key, so a hybrid
      // memo written now would be confirmed on chain and permanently undecryptable -- by the
      // recipient, the sender, and everyone else.
      if( to_pq_key.valid() && is_pq_active() )
      {
         md.set_message_pq( from_priv, md.to, to_pq_key->data, msg );
         return;
      }
      md.set_message( from_priv, md.to, msg );
   }

   string wallet_api_impl::decrypt_memo_message( const memo_data& md,
                                                 const fc::ecc::private_key& my_key,
                                                 const public_key_type& other )const
   {
      if( !md.pq_ciphertext.valid() )
         return md.get_message( my_key, other );

      // ML-KEM implicit rejection (FIPS 203 s7.3) returns a pseudorandom shared secret for a
      // wrong secret key instead of reporting failure, so there is no way to identify the right
      // key except to try each and let the memo checksum decide. A wallet holds a handful of
      // these at most.
      for( const auto& k : _pq_keys )
      {
         if( k.first.algorithm != fc::pq_algorithm::ml_kem_768 )
            continue;
         try {
            auto priv = fc::pq_private_key::from_base58( k.second );
            return md.get_message_pq( my_key, other, priv.key_ );
         } catch( const fc::exception& ) {
            continue;
         }
      }
      FC_THROW( "This memo is encrypted to a post-quantum memo key that is not in this wallet. "
                "Import it with import_pq_key(), or restore from the backup taken when "
                "generate_pq_memo_key() was run." );
   }

   signed_transaction wallet_api_impl::generate_pq_memo_key( const string& account_name_or_id,
                                                             bool broadcast )
   { try {
      FC_ASSERT( !self.is_locked() );
      require_pq_active();
      const account_object account = get_account( account_name_or_id );

      auto kem = fc::pq_kem_generate( fc::pq_algorithm::ml_kem_768 );
      FC_ASSERT( kem.valid(), "ML-KEM key generation failed" );

      pq_public_key_type pub;
      pub.algorithm = fc::pq_algorithm::ml_kem_768;
      pub.data      = kem.pk;
      pub.validate();

      // Stored and persisted BEFORE the operation is broadcast, deliberately. The dangerous
      // ordering is the other one: if the account_update confirmed and the secret were then
      // lost, the account would advertise a key nobody can decrypt against, and every memo
      // sent to it afterwards would be unreadable forever.
      _pq_keys[ pub ] = fc::pq_private_key( fc::pq_algorithm::ml_kem_768, kem.sk, kem.pk )
                           .to_base58();
      save_wallet_file();

      account_options opts = account.options;
      opts.pq_memo_key = pub;

      account_update_operation op;
      op.account     = account.get_id();
      op.new_options = opts;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, _remote_db->get_global_properties().parameters.get_current_fees() );
      tx.validate();

      return sign_transaction( tx, broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_name_or_id)(broadcast) ) }

   void wallet_api_impl::require_pq_active() const
   {
      FC_ASSERT( is_pq_active(),
                 "Post-quantum features are not yet active. "
                 "The blockchain must pass the PQ hardfork time and the committee "
                 "must enable pq_serialization_active in chain_parameters." );
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
      set<pq_public_key_type> pq_keys = get_owned_required_pq_keys( tx, true );
      const auto pq_fmt = get_pq_format();
      sign_with_minimal_key_set( tx, approving_key_set, pq_keys, pq_fmt );

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

         // post-quantum: when the owned PQ keys alone satisfy the required
         // authorities, sign exclusively with them (an additional legacy
         // signature would be rejected as irrelevant by verify_authority)
         set<pq_public_key_type> pq_keys = get_owned_required_pq_keys( tx, true );
         const auto pq_fmt = get_pq_format();
         sign_with_minimal_key_set( tx, approving_key_set, pq_keys, pq_fmt );

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

   fc::pq_private_key wallet_api_impl::get_pq_private_key(const pq_public_key_type& id)const
   {
      auto it = _pq_keys.find(id);
      FC_ASSERT( it != _pq_keys.end(), "PQ key not found in wallet" );
      return fc::pq_private_key::from_base58( it->second );
   }

   set<pq_public_key_type> wallet_api_impl::query_required_pq_keys( const signed_transaction &tx )const
   {
      flat_set<account_id_type> required_active;
      flat_set<account_id_type> required_owner;
      vector<authority> other;
      tx.get_required_authorities( required_active, required_owner, other, false );

      set<pq_public_key_type> result;
      auto add_pq = [&result]( const authority& a ) {
         for( const auto& k : a.pq_key_auths )
            result.insert( k.first );
      };
      for( const auto& a : other )
         add_pq( a );
      if( !required_active.empty() || !required_owner.empty() )
      {
         vector<string> names_or_ids;
         names_or_ids.reserve( required_active.size() + required_owner.size() );
         for( const auto& id : required_active )
            names_or_ids.push_back( std::string( object_id_type( id ) ) );
         for( const auto& id : required_owner )
            names_or_ids.push_back( std::string( object_id_type( id ) ) );
         for( const auto& acct : _remote_db->get_accounts( names_or_ids, false ) )
         {
            if( !acct.valid() ) continue;
            // Only collect keys from the authority actually required. Collecting both
            // unconditionally means that for an ordinary operation (which needs just the
            // active authority) the wallet also signs with the owner PQ key -- and since
            // migrate_wallet installs a *different* key in each authority, that extra
            // signature satisfies nothing and the chain rejects the whole transaction as
            // tx_irrelevant_sig.
            if( std::find( required_active.begin(), required_active.end(), acct->id )
                  != required_active.end() )
               add_pq( acct->active );
            if( std::find( required_owner.begin(), required_owner.end(), acct->id )
                  != required_owner.end() )
               add_pq( acct->owner );
         }
      }
      return result;
   }

   set<pq_public_key_type> wallet_api_impl::get_owned_required_pq_keys( signed_transaction &tx,
         bool erase_existing_sigs )
   {
      set<pq_public_key_type> required = query_required_pq_keys( tx );
      set<pq_public_key_type> owned;
      for( const auto& k : required )
         if( _pq_keys.count( k ) > 0 )
            owned.insert( k );
      if( erase_existing_sigs )
         tx.pq_signatures.clear();
      return owned;
   }

   void wallet_api_impl::sign_with_minimal_key_set( signed_transaction& tx,
         const set<public_key_type>& approving_key_set,
         const set<pq_public_key_type>& pq_keys,
         fc::raw::pq_format pq_fmt )
   {
      // verify_authority rejects any signature that isn't needed to satisfy an authority
      // (tx_irrelevant_sig), for PQ signatures just as for classical ones -- and a PQ
      // signature is ~5 KB, so attaching an unnecessary one is both fatal and expensive.
      // query_required_pq_keys() returns every PQ key present in the relevant authorities
      // rather than a minimal set, so signing with all of them unconditionally would attach
      // irrelevant signatures whenever the classical keys already suffice on their own.
      const bool pq_alone = !pq_keys.empty()
                            && keys_satisfy_authorities( tx, {}, pq_keys );
      const bool classical_alone = !approving_key_set.empty()
                            && keys_satisfy_authorities( tx, approving_key_set, {} );

      // Prefer PQ-only where it suffices: the point of migrating is to stop relying on
      // secp256k1, so don't fall back to a classical-only signature just because it is
      // smaller. Only mix the two when neither kind satisfies the authorities alone.
      if( !pq_alone )
         for( const public_key_type& key : approving_key_set )
            tx.sign( get_private_key( key ), _chain_id, pq_fmt );

      if( pq_alone || !classical_alone )
         for( const pq_public_key_type& pq_key : pq_keys )
            tx.sign_pq( get_pq_private_key( pq_key ), _chain_id, pq_fmt );
   }

   bool wallet_api_impl::keys_satisfy_authorities( const signed_transaction& tx,
         const set<public_key_type>& owned_keys,
         const set<pq_public_key_type>& owned_pq )const
   {
      flat_set<account_id_type> required_active;
      flat_set<account_id_type> required_owner;
      vector<authority> other;
      tx.get_required_authorities( required_active, required_owner, other, false );

      auto satisfied = [&owned_keys, &owned_pq]( const authority& a ) {
         weight_type sum = 0;
         for( const auto& k : a.key_auths )
            if( owned_keys.count( k.first ) )
               sum += k.second;
         for( const auto& k : a.pq_key_auths )
            if( owned_pq.count( k.first ) )
               sum += k.second;
         return sum >= a.weight_threshold;
      };

      for( const auto& a : other )
         if( !satisfied( a ) )
            return false;

      if( !required_active.empty() || !required_owner.empty() )
      {
         vector<string> names_or_ids;
         names_or_ids.reserve( required_active.size() + required_owner.size() );
         for( const auto& id : required_active )
            names_or_ids.push_back( std::string( object_id_type( id ) ) );
         for( const auto& id : required_owner )
            names_or_ids.push_back( std::string( object_id_type( id ) ) );
         for( const auto& acct : _remote_db->get_accounts( names_or_ids, false ) )
         {
            if( !acct.valid() ) continue;
            bool active_ok = std::find( required_active.begin(), required_active.end(), acct->id )
                             == required_active.end()
                             || satisfied( acct->active ) || satisfied( acct->owner );
            bool owner_ok  = std::find( required_owner.begin(), required_owner.end(), acct->id )
                             == required_owner.end()
                             || satisfied( acct->owner );
            if( !active_ok || !owner_ok )
               return false;
         }
      }
      return true;
   }

   bool wallet_api_impl::import_pq_key(string account_name_or_id, string base58_key)
   {
   require_pq_active();
      fc::pq_private_key pq_priv = fc::pq_private_key::from_base58( base58_key );
      graphene::protocol::pq_public_key_type wif_pub_key( pq_priv.get_public_key() );

      account_object account = get_account( account_name_or_id );

      // make a list of all current PQ public keys for the named account
      flat_set<pq_public_key_type> all_keys_for_account;
      for( const auto& k : account.active.get_pq_keys() ) all_keys_for_account.insert( k );
      for( const auto& k : account.owner.get_pq_keys() ) all_keys_for_account.insert( k );
      // The memo key belongs to the account as much as the authority keys do, and import_key()
      // counts the classical one for exactly this reason. Without it, importing the key that
      // reads the account's own post-quantum memos was reported back as a key belonging to
      // somebody else.
      if( account.options.pq_memo_key.value.valid() )
         all_keys_for_account.insert( *account.options.pq_memo_key.value );

      _pq_keys[wif_pub_key] = base58_key;

      _wallet.update_account(account);

      return all_keys_for_account.find( wif_pub_key ) != all_keys_for_account.end();
   }

   string wallet_api_impl::generate_pq_key( const string& account_name_or_id,
                                           const string& owner_or_active_key_string,
                                           optional<uint8_t> algorithm )
   {
      require_pq_active();
      const account_object account = get_account( account_name_or_id );
      fc::ecc::private_key entropy_key;
      bool matched = false;
      string source_key = owner_or_active_key_string;
      // allow either the account name or a WIF/pubkey to identify the source key
      if( source_key.empty() || account_name_or_id == source_key )
      {
         vector<public_key_type> active_keys = account.active.get_keys();
         if( active_keys.size() == 1 && _keys.count( active_keys.front() ) )
            source_key = active_keys.front().operator std::string();
         else
         {
            vector<public_key_type> owner_keys = account.owner.get_keys();
            FC_ASSERT( owner_keys.size() == 1 && _keys.count( owner_keys.front() ),
                       "Could not uniquely determine the source key; pass it explicitly" );
            source_key = owner_keys.front().operator std::string();
         }
      }
      // accept a public key string
      auto it = _keys.find( public_key_type( source_key ) );
      if( it != _keys.end() )
      {
         fc::optional<fc::ecc::private_key> priv = wif_to_key( it->second );
         FC_ASSERT( priv, "Unable to recover private key" );
         entropy_key = *priv;
         matched = true;
      }
      else
      {
         // accept a WIF string directly
         fc::optional<fc::ecc::private_key> priv = wif_to_key( source_key );
         FC_ASSERT( priv, "Could not resolve the source key" );
         entropy_key = *priv;
         matched = true;
      }
      FC_ASSERT( matched );

      fc::pq_algorithm alg = fc::pq_algorithm::ml_dsa_65;
      if( algorithm.valid() )
         alg = (fc::pq_algorithm)(*algorithm);

      // entropy_key above only proves the caller controls a classical key for
      // this account (authorization gate); it must NOT be used to derive the
      // PQ key. A quantum adversary capable of recovering an ECDSA private
      // key from its (always-public) on-chain public key is exactly the
      // threat model PQC defends against, so a PQ key derived from that same
      // secret would give no additional security margin. Generate from
      // independent randomness instead.
      (void)entropy_key;
      fc::pq_private_key pq_priv = fc::pq_private_key::generate( alg );

      graphene::protocol::pq_public_key_type wif_pub_key( pq_priv.get_public_key() );
      _pq_keys[wif_pub_key] = pq_priv.to_base58();
      _wallet.update_account( account );
      return wif_pub_key.to_base58();
   }

   signed_transaction wallet_api_impl::migrate_wallet( const string& account_name_or_id,
         bool broadcast )
   {
   require_pq_active();
      const account_object account = get_account( account_name_or_id );

      authority new_active = account.active;
      authority new_owner  = account.owner;

      // source_key_str only proves the wallet controls the classical key being
      // migrated (authorization gate); the PQ key itself is generated from
      // independent randomness -- see generate_pq_key() for why deriving it
      // from the classical secret would defeat the point of PQC.
      auto add_pq_from = [this, &account]( authority& auth, const string& source_key_str ) {
         fc::optional<fc::ecc::private_key> priv = wif_to_key( source_key_str );
         FC_ASSERT( priv, "Could not recover private key" );
         fc::pq_private_key pq_priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
         pq_public_key_type pq_pub( pq_priv.get_public_key() );
         auth.pq_key_auths[ pq_pub ] = auth.weight_threshold;
         _pq_keys[ pq_pub ] = pq_priv.to_base58();
      };

      // Hybrid migration: the existing key_auths stay, and PQ keys are added alongside at
      // full weight, so either scheme can authorize.
      //
      // Be clear about what that is and is not. check_authority() sums weights and succeeds
      // as soon as the threshold is met, trying key_auths first, so an account in this state
      // remains fully controllable by its classical key alone. Against an adversary who can
      // break secp256k1 -- the adversary post-quantum cryptography exists for -- this adds
      // nothing. It is an OR, not an AND.
      //
      // It is still the right default, because it is reversible and cannot lock anyone out
      // while they verify their PQ key works. But an account is only quantum-resistant once
      // no classical key remains in its authority, which is what migrate_wallet_pq_only()
      // does. Nothing here should be read as protection on its own.
      for( const auto& k : new_active.key_auths )
         if( _keys.count( k.first ) )
            add_pq_from( new_active, _keys.at( k.first ) );
      for( const auto& k : new_owner.key_auths )
         if( _keys.count( k.first ) )
            add_pq_from( new_owner, _keys.at( k.first ) );

      account_update_operation op;
      op.account = account.id;
      op.fee = account_update_operation::fee_params_t().fee;
      op.owner  = new_owner;
      op.active = new_active;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, get_global_properties().parameters.get_current_fees() );
      tx.validate();
      return sign_transaction2( tx, vector<public_key_type>(), broadcast );
   }

   signed_transaction wallet_api_impl::migrate_wallet_pq_only( const string& account_name_or_id,
         bool broadcast )
   {
   require_pq_active();
      const account_object account = get_account( account_name_or_id );

      // Replace each authority outright rather than editing the existing one: the point is
      // that no classical key survives. An authority that still lists a key_auth meeting its
      // threshold is satisfiable without any post-quantum signature at all.
      auto pq_only = []( const authority& current,
                         std::map<pq_public_key_type, string>& store ) -> authority
      {
         authority result;
         result.weight_threshold = current.weight_threshold;
         // account_auths are kept: they delegate to another account, whose own authority is
         // checked recursively, so they do not themselves imply a classical key here.
         result.account_auths = current.account_auths;

         // One post-quantum key per classical key being retired, each carrying the weight
         // that key had, so the threshold arithmetic is unchanged.
         for( const auto& k : current.key_auths )
         {
            fc::pq_private_key pq_priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
            pq_public_key_type pq_pub( pq_priv.get_public_key() );
            result.pq_key_auths[ pq_pub ] = k.second;
            store[ pq_pub ] = pq_priv.to_base58();
         }
         // Any post-quantum keys already present are carried over.
         for( const auto& k : current.pq_key_auths )
            result.pq_key_auths[ k.first ] = k.second;

         return result;
      };

      authority new_active = pq_only( account.active, _pq_keys );
      authority new_owner  = pq_only( account.owner,  _pq_keys );

      FC_ASSERT( new_active.key_auths.empty() && new_owner.key_auths.empty(),
                 "internal error: a classical key survived a post-quantum-only migration" );
      FC_ASSERT( !new_active.pq_key_auths.empty() && !new_owner.pq_key_auths.empty(),
                 "refusing to write an authority with no post-quantum key: the account would "
                 "become unusable" );
      // Gegen die BESTEHENDE Autoritaet pruefen, nicht gegen die eben gebaute. pq_only()
      // legt ein frisches authority-Objekt an und uebertraegt address_auths gar nicht,
      // also war new_*.address_auths immer leer und diese Zusicherung konnte nie ausloesen.
      // Die Migration haette einem Konto mit Adress-Autoritaeten diese stillschweigend
      // entfernt -- genau das, was hier verhindert werden sollte.
      FC_ASSERT( account.active.address_auths.empty() && account.owner.address_auths.empty(),
                 "this account has address authorities, which are classical and which this "
                 "migration cannot carry over; they would be dropped silently. There is no "
                 "wallet command to replace them with post-quantum addresses yet, so such an "
                 "account has no migration path -- see PQ-MIGRATION.md" );

      account_update_operation op;
      op.account = account.id;
      op.fee = account_update_operation::fee_params_t().fee;
      op.owner  = new_owner;
      op.active = new_active;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, get_global_properties().parameters.get_current_fees() );
      tx.validate();

      // Signed with the classical key that still controls the account at this instant; from
      // the block this lands in, only the post-quantum keys can authorize it.
      return sign_transaction2( tx, vector<public_key_type>(), broadcast );
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

      _wallet.extra_keys[account.get_id()].insert(wif_pub_key);

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
         update_op.active_approvals_to_add.insert( get_account( name ).get_id() );
      for( const std::string& name : delta.active_approvals_to_remove )
         update_op.active_approvals_to_remove.insert( get_account( name ).get_id() );
      for( const std::string& name : delta.owner_approvals_to_add )
         update_op.owner_approvals_to_add.insert( get_account( name ).get_id() );
      for( const std::string& name : delta.owner_approvals_to_remove )
         update_op.owner_approvals_to_remove.insert( get_account( name ).get_id() );
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


   signed_transaction wallet_api_impl::migrate_address_auths_pq( const string& account_name_or_id,
                                                                 bool broadcast )
   { try {
      account_object account = get_account( account_name_or_id );

      FC_ASSERT( !account.active.address_auths.empty() || !account.owner.address_auths.empty(),
                 "this account has no address authorities, so there is nothing to migrate" );

      // Eine Adresse ist der Hash eines Schluessels. Welcher Schluessel das war, steht nirgends,
      // also laesst sich der Eintrag nicht umrechnen -- nur ersetzen. Jeder Eintrag bekommt
      // einen frischen ML-DSA-Schluessel mit demselben Gewicht, damit die Schwellenrechnung
      // unveraendert bleibt.
      auto replace = [this]( const authority& current ) -> authority
      {
         authority result = current;
         result.address_auths.clear();
         for( const auto& a : current.address_auths )
         {
            fc::pq_private_key pq_priv = fc::pq_private_key::generate( fc::pq_algorithm::ml_dsa_65 );
            pq_public_key_type pq_pub( pq_priv.get_public_key() );
            result.address_auths[ address( pq_pub ) ] = a.second;
            _pq_keys[ pq_pub ] = pq_priv.to_base58();
         }
         return result;
      };

      authority new_active = replace( account.active );
      authority new_owner  = replace( account.owner );

      FC_ASSERT( new_active.address_auths.size() == account.active.address_auths.size()
                 && new_owner.address_auths.size() == account.owner.address_auths.size(),
                 "internal error: an address authority was lost during migration" );

      account_update_operation op;
      op.account = account.id;
      op.fee = account_update_operation::fee_params_t().fee;
      op.owner  = new_owner;
      op.active = new_active;

      signed_transaction tx;
      tx.operations.push_back( op );
      set_operation_fees( tx, get_global_properties().parameters.get_current_fees() );
      tx.validate();

      // Mit dem Schluessel signiert, der das Konto in diesem Augenblick noch kontrolliert.
      return sign_transaction2( tx, vector<public_key_type>(), broadcast );
   } FC_CAPTURE_AND_RETHROW( (account_name_or_id)(broadcast) ) }
}

}} // namespace graphene::wallet::detail
