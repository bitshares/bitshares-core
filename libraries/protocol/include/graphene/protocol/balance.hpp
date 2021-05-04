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
#include <graphene/protocol/base.hpp>
#include <graphene/protocol/asset.hpp>
#include <graphene/protocol/authority.hpp>

namespace graphene { namespace protocol { 

   /**
    * @brief Claim a balance in a @ref graphene::chain::balance_object
    *
    * This operation is used to claim the balance in a given @ref graphene::chain::balance_object.
    * If the balance object contains a
    * vesting balance, @ref total_claimed must not exceed @ref graphene::chain::balance_object::available
    * at the time of evaluation. If
    * the object contains a non-vesting balance, @ref total_claimed must be the full balance of the object.
    */
   struct balance_claim_operation : public base_operation
   {
      struct fee_parameters_type {};

      asset             fee;
      account_id_type   deposit_to_account;
      balance_id_type   balance_to_claim;
      public_key_type   balance_owner_key;
      asset             total_claimed;

      account_id_type fee_payer()const { return deposit_to_account; }
      share_type      calculate_fee(const fee_parameters_type& )const { return 0; }
      void            validate()const;
      void            get_required_authorities( vector<authority>& a )const
      {
         a.push_back( authority( 1, balance_owner_key, 1 ) );
      }
   };

} } // graphene::protocol

FC_REFLECT( graphene::protocol::balance_claim_operation::fee_parameters_type,  )
FC_REFLECT( graphene::protocol::balance_claim_operation,
            (fee)(deposit_to_account)(balance_to_claim)(balance_owner_key)(total_claimed) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::balance_claim_operation )
