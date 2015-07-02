/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#pragma once
#include <fc/io/varint.hpp>
#include <graphene/chain/types.hpp>

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
      uint32_t num_auths()const { return account_auths.size() + key_auths.size(); }
      void     clear() { account_auths.clear(); key_auths.clear(); }

      uint32_t                              weight_threshold = 0;
      flat_map<account_id_type,weight_type> account_auths;
      flat_map<public_key_type,weight_type> key_auths;
      /** needed for backward compatibility only */
      flat_map<address,weight_type>         address_auths;
   };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::authority, (weight_threshold)(account_auths)(key_auths)(address_auths) )
FC_REFLECT_TYPENAME( graphene::chain::authority::classification )
FC_REFLECT_ENUM( graphene::chain::authority::classification, (owner)(active)(key) )
