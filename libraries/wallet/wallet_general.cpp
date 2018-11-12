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

#include "implementation/wallet_general_impl.cpp"

namespace graphene { namespace wallet {

bool wallet_api::is_locked()const
{
   return my->is_locked();
}

bool wallet_api::is_new()const
{
   return my->_wallet.cipher_keys.size() == 0;
}

void wallet_api::lock()
{ try {
   FC_ASSERT( !is_locked() );
   encrypt_keys();
   for( auto key : my->_keys )
      key.second = key_to_wif(fc::ecc::private_key());
   my->_keys.clear();
   my->_checksum = fc::sha512();
   my->self.lock_changed(true);
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::unlock(string password)
{ try {
   FC_ASSERT(password.size() > 0);
   auto pw = fc::sha512::hash(password.c_str(), password.size());
   vector<char> decrypted = fc::aes_decrypt(pw, my->_wallet.cipher_keys);
   auto pk = fc::raw::unpack<plain_keys>(decrypted);
   FC_ASSERT(pk.checksum == pw);
   my->_keys = std::move(pk.keys);
   my->_checksum = pk.checksum;
   my->self.lock_changed(false);
} FC_CAPTURE_AND_RETHROW() }

void wallet_api::set_password( string password )
{
   if( !is_new() )
      FC_ASSERT( !is_locked(), "The wallet must be unlocked before the password can be set" );
   my->_checksum = fc::sha512::hash( password.c_str(), password.size() );
   lock();
}

map<public_key_type, string> wallet_api::dump_private_keys()
{
   FC_ASSERT(!is_locked());
   return my->_keys;
}

namespace detail{
   string address_to_shorthash( const address& addr )
   {
      uint32_t x = addr.addr._hash[0];
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
}

bool wallet_api::import_key(string account_name_or_id, string wif_key)
{
   FC_ASSERT(!is_locked());
   // backup wallet
   fc::optional<fc::ecc::private_key> optional_private_key = wif_to_key(wif_key);
   if (!optional_private_key)
      FC_THROW("Invalid private key");
   string shorthash = detail::address_to_shorthash(optional_private_key->get_public_key());
   copy_wallet_file( "before-import-key-" + shorthash );

   if( my->import_key(account_name_or_id, wif_key) )
   {
      save_wallet_file();
      copy_wallet_file( "after-import-key-" + shorthash );
      return true;
   }
   return false;
}

map<string, bool> wallet_api::import_accounts( string filename, string password )
{
   FC_ASSERT( !is_locked() );
   FC_ASSERT( fc::exists( filename ) );

   const auto imported_keys = fc::json::from_file<exported_keys>( filename );

   const auto password_hash = fc::sha512::hash( password );
   FC_ASSERT( fc::sha512::hash( password_hash ) == imported_keys.password_checksum );

   map<string, bool> result;
   for( const auto& item : imported_keys.account_keys )
   {
       const auto import_this_account = [ & ]() -> bool
       {
           try
           {
               const account_object account = get_account( item.account_name );
               const auto& owner_keys = account.owner.get_keys();
               const auto& active_keys = account.active.get_keys();

               for( const auto& public_key : item.public_keys )
               {
                   if( std::find( owner_keys.begin(), owner_keys.end(), public_key ) != owner_keys.end() )
                       return true;

                   if( std::find( active_keys.begin(), active_keys.end(), public_key ) != active_keys.end() )
                       return true;
               }
           }
           catch( ... )
           {
           }

           return false;
       };

       const auto should_proceed = import_this_account();
       result[ item.account_name ] = should_proceed;

       if( should_proceed )
       {
           uint32_t import_successes = 0;
           uint32_t import_failures = 0;
           // TODO: First check that all private keys match public keys
           for( const auto& encrypted_key : item.encrypted_private_keys )
           {
               try
               {
                  const auto plain_text = fc::aes_decrypt( password_hash, encrypted_key );
                  const auto private_key = fc::raw::unpack<private_key_type>( plain_text );

                  import_key( item.account_name, string( graphene::utilities::key_to_wif( private_key ) ) );
                  ++import_successes;
               }
               catch( const fc::exception& e )
               {
                  elog( "Couldn't import key due to exception ${e}", ("e", e.to_detail_string()) );
                  ++import_failures;
               }
           }
           ilog( "successfully imported ${n} keys for account ${name}", ("n", import_successes)("name", item.account_name) );
           if( import_failures > 0 )
              elog( "failed to import ${n} keys for account ${name}", ("n", import_failures)("name", item.account_name) );
       }
   }

   return result;
}

bool wallet_api::import_account_keys( string filename, string password, string src_account_name, string dest_account_name )
{
   FC_ASSERT( !is_locked() );
   FC_ASSERT( fc::exists( filename ) );

   bool is_my_account = false;
   const auto accounts = list_my_accounts();
   for( const auto& account : accounts )
   {
       if( account.name == dest_account_name )
       {
           is_my_account = true;
           break;
       }
   }
   FC_ASSERT( is_my_account );

   const auto imported_keys = fc::json::from_file<exported_keys>( filename );

   const auto password_hash = fc::sha512::hash( password );
   FC_ASSERT( fc::sha512::hash( password_hash ) == imported_keys.password_checksum );

   bool found_account = false;
   for( const auto& item : imported_keys.account_keys )
   {
       if( item.account_name != src_account_name )
           continue;

       found_account = true;

       for( const auto& encrypted_key : item.encrypted_private_keys )
       {
           const auto plain_text = fc::aes_decrypt( password_hash, encrypted_key );
           const auto private_key = fc::raw::unpack<private_key_type>( plain_text );

           my->import_key( dest_account_name, string( graphene::utilities::key_to_wif( private_key ) ) );
       }

       return true;
   }
   save_wallet_file();

   FC_ASSERT( found_account );

   return false;
}

string wallet_api::normalize_brain_key(string s) const
{
   return detail::normalize_brain_key( s );
}

brain_key_info wallet_api::suggest_brain_key()const
{
   return graphene::wallet::utility::suggest_brain_key();
}

bool wallet_api::load_wallet_file( string wallet_filename )
{
   return my->load_wallet_file( wallet_filename );
}

void wallet_api::save_wallet_file( string wallet_filename )
{
   my->save_wallet_file( wallet_filename );
}

string wallet_api::get_private_key( public_key_type pubkey )const
{
   return key_to_wif( my->get_private_key( pubkey ) );
}

vector< signed_transaction > wallet_api::import_balance( string name_or_id, const vector<string>& wif_keys, bool broadcast )
{
   return my->import_balance( name_or_id, wif_keys, broadcast );
}

string wallet_api::get_wallet_filename() const
{
   return my->get_wallet_filename();
}

void wallet_api::set_wallet_filename(string wallet_filename)
{
   my->_wallet_filename = wallet_filename;
}

bool wallet_api::copy_wallet_file(string destination_filename)
{
   return my->copy_wallet_file(destination_filename);
}

void wallet_api::quit()
{
    my->quit();
}

string wallet_api::serialize_transaction( signed_transaction tx )const
{
   return fc::to_hex(fc::raw::pack(tx));
}

pair<transaction_id_type,signed_transaction> wallet_api::broadcast_transaction(signed_transaction tx)
{
   return my->broadcast_transaction(tx);
}

signed_transaction wallet_api::sign_transaction(signed_transaction tx, bool broadcast /* = false */)
{ try {
   return my->sign_transaction( tx, broadcast);
} FC_CAPTURE_AND_RETHROW( (tx) ) }

std::map<string,std::function<string(fc::variant,const fc::variants&)> >
wallet_api::get_result_formatters() const
{
   return my->get_result_formatters();
}

fc::ecc::private_key wallet_api::derive_private_key(const std::string& prefix_string, int sequence_number) const
{
   return detail::derive_private_key( prefix_string, sequence_number );
}

}} // graphene::wallet
