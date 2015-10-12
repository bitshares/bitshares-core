/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
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

#include <graphene/chain/config.hpp>
#include <graphene/chain/pts_address.hpp>

#include <fc/array.hpp>
#include <fc/crypto/ripemd160.hpp>

namespace fc { namespace ecc {
    class public_key;
    typedef fc::array<char,33>  public_key_data;
} } // fc::ecc

namespace graphene { namespace chain {

   struct public_key_type;

   /**
    *  @brief a 160 bit hash of a public key
    *
    *  An address can be converted to or from a base58 string with 32 bit checksum.
    *
    *  An address is calculated as ripemd160( sha512( compressed_ecc_public_key ) )
    *
    *  When converted to a string, checksum calculated as the first 4 bytes ripemd160( address ) is
    *  appended to the binary address before converting to base58.
    */
   class address
   {
      public:
       address(); ///< constructs empty / null address
       explicit address( const std::string& base58str );   ///< converts to binary, validates checksum
       address( const fc::ecc::public_key& pub ); ///< converts to binary
       explicit address( const fc::ecc::public_key_data& pub ); ///< converts to binary
       address( const pts_address& pub ); ///< converts to binary
       address( const public_key_type& pubkey );

       static bool is_valid( const std::string& base58str, const std::string& prefix = GRAPHENE_ADDRESS_PREFIX );

       explicit operator std::string()const; ///< converts to base58 + checksum

       friend size_t hash_value( const address& v ) { 
          const void* tmp = static_cast<const void*>(v.addr._hash+2);

          const size_t* tmp2 = reinterpret_cast<const size_t*>(tmp);
          return *tmp2;
       }
       fc::ripemd160 addr;
   };
   inline bool operator == ( const address& a, const address& b ) { return a.addr == b.addr; }
   inline bool operator != ( const address& a, const address& b ) { return a.addr != b.addr; }
   inline bool operator <  ( const address& a, const address& b ) { return a.addr <  b.addr; }

} } // namespace graphene::chain

namespace fc
{
   void to_variant( const graphene::chain::address& var,  fc::variant& vo );
   void from_variant( const fc::variant& var,  graphene::chain::address& vo );
}

namespace std
{
   template<>
   struct hash<graphene::chain::address>
   {
       public:
         size_t operator()(const graphene::chain::address &a) const
         {
            return (uint64_t(a.addr._hash[0])<<32) | uint64_t( a.addr._hash[0] );
         }
   };
}

#include <fc/reflect/reflect.hpp>
FC_REFLECT( graphene::chain::address, (addr) )
