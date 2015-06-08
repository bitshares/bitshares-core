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
      void add_authority( key_id_type k, weight_type w )
      {
         auths[k] = w;
      }
      void add_authority( account_id_type k, weight_type w )
      {
         auths[k] = w;
      }
      void add_authority( relative_key_id_type k, weight_type w )
      {
         auths[k] = w;
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

      vector<key_id_type> get_keys() const
      {
         vector<key_id_type> result;
         result.reserve( auths.size() );
         for( const pair<object_id_type, weight_type>& item : auths )
         {
            if( item.first.type() == key_object_type )
                result.push_back( item.first );
         }
         return result;
      }

      uint32_t                             weight_threshold = 0;
      flat_map<object_id_type,weight_type> auths;
   };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::authority, (weight_threshold)(auths) )
FC_REFLECT_TYPENAME( graphene::chain::authority::classification )
FC_REFLECT_ENUM( graphene::chain::authority::classification, (owner)(active)(key) )
