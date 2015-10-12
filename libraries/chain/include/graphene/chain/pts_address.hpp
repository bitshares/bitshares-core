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

#include <fc/array.hpp>
#include <string>

namespace fc { namespace ecc { class public_key; } }

namespace graphene { namespace chain {

   /**
    *  Implements address stringification and validation from PTS
    */
   struct pts_address
   {
       pts_address(); ///< constructs empty / null address
       pts_address( const std::string& base58str );   ///< converts to binary, validates checksum
       pts_address( const fc::ecc::public_key& pub, bool compressed = true, uint8_t version=56 ); ///< converts to binary

       uint8_t version()const { return addr.at(0); }
       bool is_valid()const;

       operator std::string()const; ///< converts to base58 + checksum

       fc::array<char,25> addr; ///< binary representation of address
   };

   inline bool operator == ( const pts_address& a, const pts_address& b ) { return a.addr == b.addr; }
   inline bool operator != ( const pts_address& a, const pts_address& b ) { return a.addr != b.addr; }
   inline bool operator <  ( const pts_address& a, const pts_address& b ) { return a.addr <  b.addr; }

} } // namespace graphene::chain

namespace std
{
   template<>
   struct hash<graphene::chain::pts_address> 
   {
       public:
         size_t operator()(const graphene::chain::pts_address &a) const 
         {
            size_t s;
            memcpy( (char*)&s, &a.addr.data[sizeof(a)-sizeof(s)], sizeof(s) );
            return s;
         }
   };
}

#include <fc/reflect/reflect.hpp>
FC_REFLECT( graphene::chain::pts_address, (addr) )

namespace fc 
{ 
   void to_variant( const graphene::chain::pts_address& var,  fc::variant& vo );
   void from_variant( const fc::variant& var,  graphene::chain::pts_address& vo );
}
