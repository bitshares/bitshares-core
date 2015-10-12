/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once
#include <graphene/chain/protocol/types.hpp>

namespace graphene { namespace chain {

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

      /// @note custom_nonce is for debugging only; do not set to a nonzero value in production
      void        set_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub, const string& msg, uint64_t custom_nonce = 0);

      std::string get_message(const fc::ecc::private_key& priv,
                              const fc::ecc::public_key& pub)const;
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

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::memo_message, (checksum)(text) )
FC_REFLECT( graphene::chain::memo_data, (from)(to)(nonce)(message) )
