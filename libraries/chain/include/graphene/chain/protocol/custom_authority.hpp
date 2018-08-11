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

namespace graphene { namespace chain {

/*
   template<typename T> struct transform_to_fee_parameters;
   template<typename ...T>
   struct transform_to_fee_parameters<fc::static_variant<T...>>
   {
      typedef fc::static_variant< typename T::fee_parameters_type... > type;
   };
   typedef transform_to_fee_parameters<operation>::type fee_parameters;
*/

   /**
    * @ingroup operations
    *
    * Defines the set of valid operation restritions as a discriminated union type.
    */
   /// @{
/*   template<typename T> struct transform_to_operation_restrictions;
   template<typename ...T>
   struct transform_to_operation_restrictions<fc::static_variant<T...>>
   {
      typedef fc::static_variant< typename T::operation_restriction_type... > type;
   };
   typedef transform_to_fee_parameters<operation>::type operation_restrictions;
*/   /// @}

//         vector<operation_restriction>   restrictions;

   struct operation_restriction;
   typedef vector<operation_restriction> attr_restriction_type;

   struct operation_restriction
   {

      enum member_modifier_type
      {
         mmod_none,
         mmod_size,
         mmod_pack_size,
         MEMBER_MODIFIER_TYPE_COUNT ///< Sentry value which contains the number of different types
      };

      enum function_type
      {
         func_eq,
         func_ne,
         func_lt,
         func_le,
         func_gt,
         func_ge,
         func_in,
         func_not_in,
         func_has_all,
         func_has_none,
         //func_is_valid,  // -> size() == 1
         //func_not_valid, // -> size() == 0
         FUNCTION_TYPE_COUNT ///< Sentry value which contains the number of different types
      };

      #define GRAPHENE_OP_RESTRICTION_ARGUMENTS_VARIADIC \
         void_t, \
         bool, \
         int64_t, \
         string, \
         time_point_sec, \
         public_key_type, \
         account_id_type, \
         asset_id_type, \
         force_settlement_id_type, \
         committee_member_id_type, \
         witness_id_type, \
         limit_order_id_type, \
         call_order_id_type, \
         custom_id_type, \
         proposal_id_type, \
         withdraw_permission_id_type, \
         vesting_balance_id_type, \
         worker_id_type, \
         balance_id_type, \
         flat_set< bool                        >, \
         flat_set< int64_t                     >, \
         flat_set< string                      >, \
         flat_set< time_point_sec              >, \
         flat_set< public_key_type             >, \
         flat_set< account_id_type             >, \
         flat_set< asset_id_type               >, \
         flat_set< force_settlement_id_type    >, \
         flat_set< committee_member_id_type    >, \
         flat_set< witness_id_type             >, \
         flat_set< limit_order_id_type         >, \
         flat_set< call_order_id_type          >, \
         flat_set< custom_id_type              >, \
         flat_set< proposal_id_type            >, \
         flat_set< withdraw_permission_id_type >, \
         flat_set< vesting_balance_id_type     >, \
         flat_set< worker_id_type              >, \
         flat_set< balance_id_type             >, \
         attr_restriction_type

      typedef static_variant < GRAPHENE_OP_RESTRICTION_ARGUMENTS_VARIADIC > argument_type;

      unsigned_int               member;          // index, use unsigned_int to save space TODO jsonify to actual name
      unsigned_int               member_modifier; // index, use unsigned_int to save space TODO jsonify to actual name
      unsigned_int               function;        // index, use unsigned_int to save space TODO jsonify to actual name
      argument_type              argument;

      empty_extensions_type      extensions;

      uint64_t get_units()const;
      void validate( unsigned_int op_type )const;
   };

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
      vector<operation_restriction>   restrictions;

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

FC_REFLECT_ENUM( graphene::chain::operation_restriction::member_modifier_type,
                 (mmod_none)
                 (mmod_size)
                 (mmod_pack_size)
                 (MEMBER_MODIFIER_TYPE_COUNT)
               )

FC_REFLECT_ENUM( graphene::chain::operation_restriction::function_type,
                 (func_eq)
                 (func_ne)
                 (func_lt)
                 (func_le)
                 (func_gt)
                 (func_ge)
                 (func_in)
                 (func_not_in)
                 (func_has_all)
                 (func_has_none)
                 //(func_is_valid)
                 //(func_not_valid)
                 (FUNCTION_TYPE_COUNT)
               )

FC_REFLECT( graphene::chain::operation_restriction,
            (member)
            (member_modifier)
            (function)
            (argument)
            (extensions)
          )

FC_REFLECT( graphene::chain::custom_authority_create_operation::fee_parameters_type, (basic_fee)(price_per_k_unit) )
FC_REFLECT( graphene::chain::custom_authority_update_operation::fee_parameters_type, (basic_fee)(price_per_k_unit) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::custom_authority_create_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_update_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation, (fee)(account) )
