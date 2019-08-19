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

#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/ripemd160.hpp>

namespace graphene { namespace protocol {
   struct pts_address;

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
       address(){} ///< constructs empty / null address
       explicit address( const std::string& base58str );   ///< converts to binary, validates checksum
       address( const fc::ecc::public_key& pub ); ///< converts to binary
       explicit address( const fc::ecc::public_key_data& pub ); ///< converts to binary
       address( const pts_address& pub ); ///< converts to binary
       address( const public_key_type& pubkey );

       static bool is_valid( const std::string& base58str, const std::string& prefix = GRAPHENE_ADDRESS_PREFIX );

       explicit operator std::string()const; ///< converts to base58 + checksum

       fc::ripemd160 addr;
   };
   inline bool operator == ( const address& a, const address& b ) { return a.addr == b.addr; }
   inline bool operator != ( const address& a, const address& b ) { return a.addr != b.addr; }
   inline bool operator <  ( const address& a, const address& b ) { return a.addr <  b.addr; }

} } // namespace graphene::protocol

namespace fc
{
   void to_variant( const graphene::protocol::address& var,  fc::variant& vo, uint32_t max_depth = 1 );
   void from_variant( const fc::variant& var,  graphene::protocol::address& vo, uint32_t max_depth = 1 );
}

FC_REFLECT( graphene::protocol::address, (addr) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::address )
