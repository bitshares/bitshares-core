/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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
#include <graphene/protocol/memo.hpp>
#include <boost/endian/conversion.hpp>
#include <fc/crypto/aes.hpp>
#include <fc/crypto/hex.hpp>
#include <fc/crypto/pqc.hpp>
#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

namespace {

/**
 * The AES key for a memo.
 *
 * With no KEM secret this is the original construction, sha512(nonce || ecdh_secret), and it
 * must stay byte-identical forever: every memo ever written to the chain is encrypted under
 * it, and a change here makes all of them permanently unreadable.
 *
 * With a KEM secret the ML-KEM shared secret is appended, so recovering the plaintext needs
 * both secp256k1 ECDH and ML-KEM broken, not either one. Hybrid rather than KEM-only is the
 * point: no weaker than today's memo if this KEM implementation turns out flawed, and no
 * weaker than the KEM once secp256k1 falls.
 */
fc::sha512 memo_aes_key( uint64_t nonce, const fc::sha512& ecdh_secret,
                         const std::vector<char>* kem_secret )
{
   if( nullptr == kem_secret )
      return fc::sha512::hash( fc::to_string(nonce) + ecdh_secret.str() );
   return fc::sha512::hash( fc::to_string(nonce) + ecdh_secret.str() + fc::to_hex( *kem_secret ) );
}

/// Shared by both encrypt paths so they cannot drift: the nonce is the IV half of the
/// key/IV pair, so a nonce repeated between two memos under the same shared secret would
/// leak the xor of their plaintexts.
uint64_t generate_memo_nonce()
{
   uint64_t entropy = fc::sha224::hash(fc::ecc::private_key::generate())._hash[0].value();
   constexpr uint64_t half_size = 32;
   constexpr uint64_t high_bits = 0xff00000000000000ULL;
   constexpr uint64_t  low_bits = 0x00ffffffffffffffULL;
   entropy <<= half_size;
   entropy &= high_bits;
   return ((uint64_t)(fc::time_point::now().time_since_epoch().count()) & low_bits) | entropy;
}

constexpr fc::pq_algorithm memo_kem_algorithm = fc::pq_algorithm::ml_kem_768;

} // namespace

void memo_data::set_message(const fc::ecc::private_key& priv, const fc::ecc::public_key& pub,
                            const string& msg, uint64_t custom_nonce)
{
   if( priv != fc::ecc::private_key() && public_key_type(pub) != public_key_type() )
   {
      from = priv.get_public_key();
      to = pub;
      nonce = ( 0 == custom_nonce ) ? generate_memo_nonce() : custom_nonce;
      auto secret = priv.get_shared_secret(pub);
      auto nonce_plus_secret = memo_aes_key( nonce, secret, nullptr );
      string text = memo_message((uint32_t)digest_type::hash(msg)._hash[0].value(), msg).serialize();
      message = fc::aes_encrypt( nonce_plus_secret, vector<char>(text.begin(), text.end()) );
   }
   else
   {
      auto text = memo_message(0, msg).serialize();
      message = vector<char>(text.begin(), text.end());
   }
}

string memo_data::get_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub)const
{
   if( from != public_key_type() )
   {
      auto secret = priv.get_shared_secret(pub);
      auto nonce_plus_secret = memo_aes_key( nonce, secret, nullptr );
      auto plain_text = fc::aes_decrypt( nonce_plus_secret, message );
      auto result = memo_message::deserialize(string(plain_text.begin(), plain_text.end()));
      FC_ASSERT( result.checksum == (uint32_t)digest_type::hash(result.text)._hash[0].value() );
      return result.text;
   }
   else
   {
      return memo_message::deserialize(string(message.begin(), message.end())).text;
   }
}

void memo_data::set_message_pq(const fc::ecc::private_key& priv, const fc::ecc::public_key& pub,
                               const std::vector<char>& to_pq_key,
                               const string& msg, uint64_t custom_nonce)
{ try {
   // There is deliberately no public-memo branch here, unlike set_message: an unencrypted memo
   // has nothing to protect, and silently writing one when the caller asked for post-quantum
   // protection would be the worst possible failure mode.
   FC_ASSERT( priv != fc::ecc::private_key() && public_key_type(pub) != public_key_type(),
              "A post-quantum memo still needs both classical memo keys; the construction is "
              "hybrid so that it is never weaker than an ordinary memo." );

   const auto expected_pk_size = fc::pqc_sizes::public_key_size( memo_kem_algorithm );
   FC_ASSERT( to_pq_key.size() == expected_pk_size,
              "Recipient ML-KEM public key must be ${e} bytes, got ${g}.",
              ("e", expected_pk_size)("g", to_pq_key.size()) );

   auto kem = fc::pq_kem_encapsulate( memo_kem_algorithm, to_pq_key );
   FC_ASSERT( kem.valid && !kem.shared_secret.empty(), "ML-KEM encapsulation failed." );

   from = priv.get_public_key();
   to = pub;
   nonce = ( 0 == custom_nonce ) ? generate_memo_nonce() : custom_nonce;

   auto secret = priv.get_shared_secret(pub);
   auto key = memo_aes_key( nonce, secret, &kem.shared_secret );
   string text = memo_message((uint32_t)digest_type::hash(msg)._hash[0].value(), msg).serialize();
   message = fc::aes_encrypt( key, vector<char>(text.begin(), text.end()) );
   pq_ciphertext = kem.ciphertext;
} FC_CAPTURE_AND_RETHROW() }

string memo_data::get_message_pq(const fc::ecc::private_key& priv, const fc::ecc::public_key& pub,
                                 const std::vector<char>& pq_priv_key)const
{ try {
   FC_ASSERT( pq_ciphertext.valid(),
              "This memo carries no ML-KEM ciphertext; read it with get_message instead." );
   FC_ASSERT( from != public_key_type(), "A post-quantum memo always records its sender key." );

   auto kem_secret = fc::pq_kem_decapsulate( memo_kem_algorithm, pq_priv_key, *pq_ciphertext );
   FC_ASSERT( !kem_secret.empty(), "ML-KEM decapsulation failed." );

   auto secret = priv.get_shared_secret(pub);
   auto key = memo_aes_key( nonce, secret, &kem_secret );
   auto plain_text = fc::aes_decrypt( key, message );
   auto result = memo_message::deserialize(string(plain_text.begin(), plain_text.end()));
   // ML-KEM implicit rejection (FIPS 203 s7.3) returns a pseudorandom shared secret for a wrong
   // secret key rather than reporting an error, so a wrong KEM key does not surface above -- it
   // surfaces here, as a checksum mismatch, exactly like a wrong classical key does.
   FC_ASSERT( result.checksum == (uint32_t)digest_type::hash(result.text)._hash[0].value(),
              "Memo checksum mismatch: wrong classical key, wrong ML-KEM key, or corrupt memo." );
   return result.text;
} FC_CAPTURE_AND_RETHROW() }

string memo_message::serialize() const
{
   auto serial_checksum = string(sizeof(checksum), ' ');
   (uint32_t&)(*serial_checksum.data()) = boost::endian::native_to_little(checksum);
   return serial_checksum + text;
}

memo_message memo_message::deserialize(const string& serial)
{
   memo_message result;
   FC_ASSERT( serial.size() >= sizeof(result.checksum) );
   result.checksum = boost::endian::little_to_native((uint32_t&)(*serial.data()));
   result.text = serial.substr(sizeof(result.checksum));
   return result;
}

} } // graphene::protocol

namespace fc { namespace raw {

namespace detail {

// memo_data has been on chain inside transfer_operation since genesis, so the post-quantum
// ciphertext must not appear in the legacy encoding. Gate it the way authority gates
// pq_key_auths, and append rather than insert, so legacy bytes stay byte-identical to what a
// pre-PQ node produces.
template<typename Stream>
void pack_memo_data_impl( Stream& s, const graphene::protocol::memo_data& v, uint32_t _max_depth )
{
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::pack( s, v.from, _max_depth );
   fc::raw::pack( s, v.to, _max_depth );
   fc::raw::pack( s, v.nonce, _max_depth );
   fc::raw::pack( s, v.message, _max_depth );
   fc::raw::pack( s, v.pq_ciphertext, _max_depth );   // gates itself; see fc::pq_gated
}

template<typename Stream>
void unpack_memo_data_impl( Stream& s, graphene::protocol::memo_data& v, uint32_t _max_depth )
{ try {
   FC_ASSERT( _max_depth > 0 );
   --_max_depth;
   fc::raw::unpack( s, v.from, _max_depth );
   fc::raw::unpack( s, v.to, _max_depth );
   fc::raw::unpack( s, v.nonce, _max_depth );
   fc::raw::unpack( s, v.message, _max_depth );
   fc::raw::unpack( s, v.pq_ciphertext, _max_depth );
} FC_RETHROW_EXCEPTIONS( warn, "error unpacking memo_data" ) }

} // namespace detail

void pack( datastream<size_t>& s, const graphene::protocol::memo_data& v, uint32_t d )
   { detail::pack_memo_data_impl( s, v, d ); }
void pack( sha256::encoder& s, const graphene::protocol::memo_data& v, uint32_t d )
   { detail::pack_memo_data_impl( s, v, d ); }
void pack( datastream<char*>& s, const graphene::protocol::memo_data& v, uint32_t d )
   { detail::pack_memo_data_impl( s, v, d ); }
void unpack( datastream<const char*>& s, graphene::protocol::memo_data& v, uint32_t d )
   { detail::unpack_memo_data_impl( s, v, d ); }

template std::vector<char> pack( const graphene::protocol::memo_data& v, uint32_t _max_depth );
template size_t pack_size( const graphene::protocol::memo_data& v );

} } // namespace fc::raw

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::memo_message )
GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::protocol::memo_data )
