/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/protocol/base.hpp>
#include <graphene/chain/protocol/restriction.hpp>

namespace graphene { namespace chain {

   /**
    * @brief Create a new custom authority
    * @ingroup operations
    */
   struct custom_authority_create_operation : public base_operation
   {
      struct fee_parameters_type
      {
         uint64_t basic_fee        = GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_k_unit = 100; ///< units = valid seconds * items in auth * items in restrictions
      };

      asset                           fee; // TODO: defer fee to expiration / update / removal ?
      account_id_type                 account;
      uint32_t                        auth_id;
      bool                            enabled;
      time_point_sec                  valid_from;
      time_point_sec                  valid_to;
      unsigned_int                    operation_type;
      authority                       auth;
      vector<restriction>             restrictions;

      empty_extensions_type           extensions;

      account_id_type fee_payer()const { return account; }
      void            validate()const;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };

   /**
    * @brief Update a custom authority
    * @ingroup operations
    */
   struct custom_authority_update_operation : public base_operation
   {
      struct fee_parameters_type
      {
         uint64_t basic_fee        = GRAPHENE_BLOCKCHAIN_PRECISION;
         uint32_t price_per_k_unit = 100; ///< units = valid seconds * items in auth * items in restrictions
      };

      asset             fee;
      account_id_type   account;
      uint64_t          delta_units; // to calculate fee, it will be validated in evaluator
                                     // Note: if start was in the past, when updating, used fee should be deducted

      account_id_type fee_payer()const { return account; }
      void            validate()const;
      share_type      calculate_fee(const fee_parameters_type& k)const;
   };


   /**
    * @brief Delete a custom authority
    * @ingroup operations
    */
   struct custom_authority_delete_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      account_id_type   account;

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

} } // graphene::chain

FC_REFLECT( graphene::chain::custom_authority_create_operation::fee_parameters_type, (basic_fee)(price_per_k_unit) )
FC_REFLECT( graphene::chain::custom_authority_update_operation::fee_parameters_type, (basic_fee)(price_per_k_unit) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::custom_authority_create_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_update_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation, (fee)(account) )
