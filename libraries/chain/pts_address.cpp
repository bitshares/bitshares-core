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
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/pts_address.hpp>

#include <fc/crypto/base58.hpp>
#include <fc/crypto/elliptic.hpp>
#include <fc/crypto/ripemd160.hpp>
#include <algorithm>

namespace graphene { namespace chain {

   pts_address::pts_address()
   {
      memset( addr.data, 0, sizeof(addr.data) );
   }

   pts_address::pts_address( const std::string& base58str )
   {
      std::vector<char> v = fc::from_base58( fc::string(base58str) );
      if( v.size() )
         memcpy( addr.data, v.data(), std::min<size_t>( v.size(), sizeof(addr) ) );

      if( !is_valid() )
      {
         FC_THROW_EXCEPTION( invalid_pts_address, "invalid pts_address ${a}", ("a", base58str) );
      }
   }

   pts_address::pts_address( const fc::ecc::public_key& pub, bool compressed, uint8_t version )
   {
       fc::sha256 sha2;
       if( compressed )
       {
           auto dat = pub.serialize();
           sha2     = fc::sha256::hash(dat.data, sizeof(dat) );
       }
       else
       {
           auto dat = pub.serialize_ecc_point();
           sha2     = fc::sha256::hash(dat.data, sizeof(dat) );
       }
       auto rep      = fc::ripemd160::hash((char*)&sha2,sizeof(sha2));
       addr.data[0]  = version;
       memcpy( addr.data+1, (char*)&rep, sizeof(rep) );
       auto check    = fc::sha256::hash( addr.data, sizeof(rep)+1 );
       check = fc::sha256::hash(check);
       memcpy( addr.data+1+sizeof(rep), (char*)&check, 4 );
   }

   /**
    *  Checks the address to verify it has a
    *  valid checksum
    */
   bool pts_address::is_valid()const
   {
       auto check    = fc::sha256::hash( addr.data, sizeof(fc::ripemd160)+1 );
       check = fc::sha256::hash(check);
       return memcmp( addr.data+1+sizeof(fc::ripemd160), (char*)&check, 4 ) == 0;
   }

   pts_address::operator std::string()const
   {
        return fc::to_base58( addr.data, sizeof(addr) );
   }

} } // namespace graphene

namespace fc
{
   void to_variant( const graphene::chain::pts_address& var,  variant& vo )
   {
        vo = std::string(var);
   }
   void from_variant( const variant& var,  graphene::chain::pts_address& vo )
   {
        vo = graphene::chain::pts_address( var.as_string() );
   }
}
