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
#include <graphene/chain/protocol/authority.hpp>
#include <utility>

namespace graphene { namespace chain {

   authority::authority( const address& k )
      : weight_threshold( 1 )
   {
      address_auths[k] = 1;
   }

   void authority::add_authority( const public_key_type& k, weight_type w )
   {
      key_auths.emplace_back( k, w );
      if( key_auths.size() > 1 ) // TODO: eliminate sorting after hardfork
         std::sort( key_auths.begin(), key_auths.end(), compare_entries_by_address );
   }

   void authority::add_authority( account_id_type k, weight_type w )
   {
      account_auths[k] = w;
   }

   bool authority::is_impossible()const
   {
      uint64_t auth_weights = 0;
      for( const auto& item : account_auths ) auth_weights += item.second;
      for( const auto& item : key_auths ) auth_weights += item.second;
      for( const auto& item : address_auths ) auth_weights += item.second;
      return auth_weights < weight_threshold;
   }

   uint32_t authority::num_auths()const
   {
      return account_auths.size() + key_auths.size() + address_auths.size();
   }

   void authority::clear()
   {
      account_auths.clear();
      key_auths.clear();
      address_auths.clear();
   }

   const authority& authority::null_authority()
   {
      static authority null_auth( 1, GRAPHENE_NULL_ACCOUNT, 1 );
      return null_auth;
   }

   void authority::validate()const
   {
      FC_ASSERT( !is_impossible(), "Weight threshold ${w} cannot be met!", ("w", weight_threshold) );
      if( key_auths.size() > 1 )
      {
         address prev_addr( key_auths[0].first );
         for( size_t i = 1; i < key_auths.size(); i++ )
         {
            address current_addr( key_auths[i].first );
            FC_ASSERT( prev_addr < current_addr, "Invalid order in key authorities!" );
            prev_addr = current_addr;
         }
      }
   }

   bool operator == ( const authority& a, const authority& b )
   {
      return (a.weight_threshold == b.weight_threshold) &&
             (a.account_auths == b.account_auths) &&
             (a.key_auths == b.key_auths) &&
             (a.address_auths == b.address_auths);
   }

} } // graphene::chain
