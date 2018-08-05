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

      enum function_type
      {
         func_eq,
         func_ne,
         func_lt,
         func_le,
         func_gt,
         func_ge,
         func_any,
         func_none,
         func_attr,
         FUNCTION_TYPE_COUNT ///< Sentry value which contains the number of different function types
      };

      typedef static_variant <
         bool,
         int64_t,
         string,
         asset_id_type,
         account_id_type,

         flat_set< bool             >,
         flat_set< int64_t          >,
         flat_set< string           >,
         flat_set< asset_id_type    >,
         flat_set< account_id_type  >,

         attr_restriction_type

      > argument_type;

      unsigned_int               member;
      function_type              function;
      argument_type              argument;

      empty_extensions_type      extensions;
   };

   /**
    * @brief Create a new custom authority
    * @ingroup operations
    */
   struct custom_authority_create_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      account_id_type   account;

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };

   /**
    * @brief Create a new custom authority
    * @ingroup operations
    */
   struct custom_authority_update_operation : public base_operation
   {
      struct fee_parameters_type { uint64_t fee =  GRAPHENE_BLOCKCHAIN_PRECISION; };

      asset             fee;
      account_id_type   account;

      account_id_type fee_payer()const { return account; }
      void            validate()const;
   };


   /**
    * @brief Create a new custom authority
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

FC_REFLECT_ENUM( graphene::chain::operation_restriction::function_type,
                 (func_eq)
                 (func_ne)
                 (func_lt)
                 (func_le)
                 (func_gt)
                 (func_ge)
                 (func_any)
                 (func_none)
                 (func_attr)
                 (FUNCTION_TYPE_COUNT)
               )

FC_REFLECT( graphene::chain::operation_restriction,
            (member)
            (function)
            (argument)
            (extensions)
          )

FC_REFLECT( graphene::chain::custom_authority_create_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::custom_authority_update_operation::fee_parameters_type, (fee) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation::fee_parameters_type, (fee) )

FC_REFLECT( graphene::chain::custom_authority_create_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_update_operation, (fee)(account) )
FC_REFLECT( graphene::chain::custom_authority_delete_operation, (fee)(account) )
