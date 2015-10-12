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
#include <graphene/chain/protocol/types.hpp>

namespace graphene { namespace chain {

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
      void add_authority( account_id_type k, weight_type w )
      {
         account_auths[k] = w;
      }
      bool is_impossible()const
      {
         uint64_t auth_weights = 0;
         for( const auto& item : account_auths ) auth_weights += item.second;
         for( const auto& item : key_auths ) auth_weights += item.second;
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
                (a.address_auths == b.address_auths); 
      }
      uint32_t num_auths()const { return account_auths.size() + key_auths.size() + address_auths.size(); }
      void     clear() { account_auths.clear(); key_auths.clear(); }

      uint32_t                              weight_threshold = 0;
      flat_map<account_id_type,weight_type> account_auths;
      flat_map<public_key_type,weight_type> key_auths;
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

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::authority, (weight_threshold)(account_auths)(key_auths)(address_auths) )
FC_REFLECT_TYPENAME( graphene::chain::authority::classification )
FC_REFLECT_ENUM( graphene::chain::authority::classification, (owner)(active)(key) )
