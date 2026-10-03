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
#include <graphene/protocol/address.hpp>

namespace graphene { namespace protocol {

   /**
    *  @class authority
    *  @brief Identifies a weighted set of keys and accounts that must approve operations.
    */
   struct authority
   {
      authority(){}
      template<class ...Args>
      authority(uint32_t threshhold, Args... auths)
         : weight_threshold(threshhold)
      {
         add_authorities(auths...);
      }

      enum classification
      {
         /** the key that is authorized to change owner, active, and voting keys */
         owner  = 0,
         /** the key that is able to perform normal operations */
         active = 1,
         key    = 2
      };
      void add_authority( const public_key_type& k, weight_type w )
      {
         key_auths[k] = w;
      }
      void add_authority( const address& k, weight_type w )
      {
         address_auths[k] = w;
      }
      void add_authority( const pq_public_key_type& k, weight_type w )
      {
         pq_key_auths[k] = w;
      }
      void add_authority( account_id_type k, weight_type w )
      {
         account_auths[k] = w;
      }
      bool is_impossible()const
      {
         uint64_t auth_weights = 0;
         for( const auto& item : account_auths ) auth_weights += item.second;
         for( const auto& item : key_auths ) auth_weights += item.second;
         for( const auto& item : pq_key_auths ) auth_weights += item.second;
         for( const auto& item : address_auths ) auth_weights += item.second;
         return auth_weights < weight_threshold;
      }

      template<typename AuthType>
      void add_authorities(AuthType k, weight_type w)
      {
         add_authority(k, w);
      }
      template<typename AuthType, class ...Args>
      void add_authorities(AuthType k, weight_type w, Args... auths)
      {
         add_authority(k, w);
         add_authorities(auths...);
      }

      vector<public_key_type> get_keys() const
      {
         vector<public_key_type> result;
         result.reserve( key_auths.size() );
         for( const auto& k : key_auths )
            result.push_back(k.first);
         return result;
      }
      vector<address> get_addresses() const
      {
         vector<address> result;
         result.reserve( address_auths.size() );
         for( const auto& k : address_auths )
            result.push_back(k.first);
         return result;
      }


      friend bool operator == ( const authority& a, const authority& b )
      {
         return (a.weight_threshold == b.weight_threshold) &&
                (a.account_auths == b.account_auths) &&
                (a.key_auths == b.key_auths) &&
                (a.pq_key_auths == b.pq_key_auths) &&
                (a.address_auths == b.address_auths); 
      }
      friend bool operator!= ( const authority& a, const authority& b ) { return !(a==b); }
      uint32_t num_auths()const { return account_auths.size() + key_auths.size() + pq_key_auths.size() + address_auths.size(); }
      void     clear() { account_auths.clear(); key_auths.clear(); pq_key_auths.clear(); address_auths.clear(); weight_threshold = 0; }

      vector<pq_public_key_type> get_pq_keys() const
      {
         vector<pq_public_key_type> result;
         result.reserve( pq_key_auths.size() );
         for( const auto& k : pq_key_auths )
            result.push_back(k.first);
         return result;
      }

      static authority null_authority()
      {
         return authority( 1, GRAPHENE_NULL_ACCOUNT, 1 );
      }

      uint32_t                              weight_threshold = 0;
      flat_map<account_id_type,weight_type> account_auths;
      flat_map<public_key_type,weight_type> key_auths;
      /** needed for backward compatibility only */
      flat_map<pq_public_key_type,weight_type> pq_key_auths;
      /** needed for backward compatibility only */
      flat_map<address,weight_type>         address_auths;
   };

/**
 * Add all account members of the given authority to the given flat_set.
 */
void add_authority_accounts(
   flat_set<account_id_type>& result,
   const authority& a
   );

} } // namespace graphene::protocol

FC_REFLECT( graphene::protocol::authority, (weight_threshold)(account_auths)(key_auths)(pq_key_auths)(address_auths) )
FC_REFLECT_ENUM( graphene::protocol::authority::classification, (owner)(active)(key) )


namespace fc { namespace raw {

// Non-template pack/unpack (defined in authority.cpp). Non-template wins
// over the generic reflected template in overload resolution.
void pack( datastream<size_t>& s, const graphene::protocol::authority& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( sha256::encoder& s, const graphene::protocol::authority& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void pack( datastream<char*>& s, const graphene::protocol::authority& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );
void unpack( datastream<const char*>& s, graphene::protocol::authority& v, uint32_t _max_depth = FC_PACK_MAX_DEPTH );

// Prevent implicit instantiation of 1-arg vector-returning pack/pack_size
// in other TUs — the instantiation in authority.cpp sees these declarations.
extern template std::vector<char> pack( const graphene::protocol::authority& v, uint32_t _max_depth );
extern template size_t pack_size( const graphene::protocol::authority& v );

} } // namespace fc::raw
GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::authority )
