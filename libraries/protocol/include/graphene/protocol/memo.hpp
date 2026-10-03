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
#pragma once
#include <graphene/protocol/types.hpp>

namespace graphene { namespace protocol {

   /**
    *  @brief defines the keys used to derive the shared secret
    *
    *  Because account authorities and keys can change at any time, each memo must
    *  capture the specific keys used to derive the shared secret.  In order to read
    *  the cipher message you will need one of the two private keys.
    *
    *  If @ref from == @ref to and @ref from == 0 then no encryption is used, the memo is public.
    *  If @ref from == @ref to and @ref from != 0 then invalid memo data
    *
    */
   struct memo_data
   {
      public_key_type from;
      public_key_type to;
      /**
       * 64 bit nonce format:
       * [  8 bits | 56 bits   ]
       * [ entropy | timestamp ]
       * Timestamp is number of microseconds since the epoch
       * Entropy is a byte taken from the hash of a new private key
       *
       * This format is not mandated or verified; it is chosen to ensure uniqueness of key-IV pairs only. This should
       * be unique with high probability as long as the generating host has a high-resolution clock OR a strong source
       * of entropy for generating private keys.
       */
      uint64_t nonce = 0;
      /**
       * This field contains the AES encrypted packed @ref memo_message
       */
      vector<char> message;

      /**
       * ML-KEM (FIPS 203) ciphertext, present only on memos encrypted with post-quantum
       * protection and only under the post-quantum serialization format.
       *
       * The recipient recovers the KEM shared secret from this and folds it into the AES
       * key alongside the ECDH secret, so the memo is readable only by someone holding
       * both the classical memo key and the post-quantum one. That is deliberate: the
       * construction is no weaker than today's memo even if this KEM implementation turns
       * out to be flawed, and no weaker than the KEM even once secp256k1 falls.
       *
       * Memos matter more than most fields here for post-quantum purposes. A signature
       * only has to resist forgery until it is confirmed, but a memo encrypted today is
       * recorded on chain permanently, so traffic captured now becomes readable the moment
       * secp256k1 breaks. Nothing done later can undo that -- which is why this exists.
       */
      fc::pq_gated< optional< vector<char> > > pq_ciphertext;

      /// @note custom_nonce is for debugging only; do not set to a nonzero value in production
      void        set_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub, const string& msg, uint64_t custom_nonce = 0);

      std::string get_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub)const;

      /**
       * Encrypt with hybrid classical + post-quantum key agreement.
       *
       * Performs the usual ECDH against @p pub and additionally encapsulates to @p to_pq_key,
       * an ML-KEM public key belonging to the recipient. Both secrets are folded into the AES
       * key, and the KEM ciphertext is carried in @ref pq_ciphertext.
       *
       * @param priv      the sender's classical memo private key
       * @param pub       the recipient's classical memo public key
       * @param to_pq_key the recipient's ML-KEM public key, from their account options
       * @param msg       the plaintext
       * @param custom_nonce debugging only; leave zero in production
       */
      void set_message_pq(const fc::ecc::private_key& priv,
                          const fc::ecc::public_key& pub,
                          const std::vector<char>& to_pq_key,
                          const string& msg, uint64_t custom_nonce = 0);

      /**
       * Decrypt a memo written by @ref set_message_pq.
       *
       * @param priv       the recipient's (or sender's) classical memo private key
       * @param pub        the other party's classical memo public key
       * @param pq_priv_key the recipient's ML-KEM secret key
       */
      std::string get_message_pq(const fc::ecc::private_key& priv,
                                 const fc::ecc::public_key& pub,
                                 const std::vector<char>& pq_priv_key)const;
   };

   /**
    * @brief defines a message and checksum to enable validation of successful decryption
    *
    * When encrypting/decrypting a checksum is required to determine whether or not
    * decryption was successful.
    */
   struct memo_message
   {
      memo_message(){}
      memo_message( uint32_t checksum, const std::string& text )
      :checksum(checksum),text(text){}

      uint32_t    checksum = 0;
      std::string text;

      string serialize() const;
      static memo_message deserialize(const string& serial);
   };

} } // namespace graphene::protocol

FC_REFLECT( graphene::protocol::memo_message, (checksum)(text) )
// pq_ciphertext is reflected so JSON and the API see it, but it is NOT part of the
// reflected binary packing: fc::raw::pack/unpack for memo_data are written out by hand in
// memo.cpp so the field only reaches the wire under the post-quantum format. memo_data is
// embedded in transfer_operation and has been on chain since genesis; emitting a new field
// unconditionally would make every historical memo fail to deserialize.
FC_REFLECT( graphene::protocol::memo_data, (from)(to)(nonce)(message)(pq_ciphertext) )

namespace fc { namespace raw {
   void pack( datastream<size_t>& s, const graphene::protocol::memo_data& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( sha256::encoder& s, const graphene::protocol::memo_data& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void pack( datastream<char*>& s, const graphene::protocol::memo_data& v,
              uint32_t _max_depth = FC_PACK_MAX_DEPTH );
   void unpack( datastream<const char*>& s, graphene::protocol::memo_data& v,
                uint32_t _max_depth = FC_PACK_MAX_DEPTH );

   // Prevent implicit instantiation of 1-arg vector-returning pack/pack_size in other TUs --
   // the instantiation in memo.cpp sees these declarations.
   extern template std::vector<char> pack( const graphene::protocol::memo_data& v,
                                           uint32_t _max_depth );
   extern template size_t pack_size( const graphene::protocol::memo_data& v );
} } // namespace fc::raw

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::memo_message )
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::memo_data )
