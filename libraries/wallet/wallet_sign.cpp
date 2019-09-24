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
         FC_ASSERT( _keys.count(memo->to) || _keys.count(memo->from),
                    "Memo is encrypted to a key ${to} or ${from} not in this wallet.",
                    ("to", memo->to)("from",memo->from) );
         if( _keys.count(memo->to) ) {
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

}}} // namespace graphene::wallet::detail
