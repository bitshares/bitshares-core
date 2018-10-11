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
#include <graphene/chain/protocol/types.hpp>

namespace graphene { namespace chain {

   /**
    *  @class authority
    *  @brief Identifies a weighted set of keys and accounts that must approve operations.
    */
   struct authority
   {
      authority(){}
      explicit authority( const address& k );
      template<class ...Args>
      authority(uint32_t threshhold, Args... auths) : weight_threshold(threshhold)
      {
         add_authorities(auths...);
      }

      void add_authority( const public_key_type& k, weight_type w );
      void add_authority( account_id_type k, weight_type w );

      bool is_impossible()const;

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

      friend bool operator == ( const authority& a, const authority& b );

      void clear();

      void validate()const;

      static const authority& null_authority();

      uint32_t num_auths()const;

      uint32_t                              weight_threshold = 0;
      flat_map<account_id_type,weight_type> account_auths;
      vector<std::pair<public_key_type,weight_type>> key_auths;
      /** Only used by collateral-holder-accounts from genesis, always empty on-chain */
      flat_map<address,weight_type>         address_auths;
   };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::authority, (weight_threshold)(account_auths)(key_auths)(address_auths) )
