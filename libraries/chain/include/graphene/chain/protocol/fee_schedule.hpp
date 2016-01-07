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
#include <graphene/chain/protocol/operations.hpp>

namespace graphene { namespace chain {

   template<typename T> struct transform_to_fee_parameters;
   template<typename ...T>
   struct transform_to_fee_parameters<fc::static_variant<T...>>
   {
      typedef fc::static_variant< typename T::fee_parameters_type... > type;
   };
   typedef transform_to_fee_parameters<operation>::type fee_parameters;

   /**
    *  @brief contains all of the parameters necessary to calculate the fee for any operation
    */
   struct fee_schedule
   {
      fee_schedule();

      static fee_schedule get_default();

      /**
       *  Finds the appropriate fee parameter struct for the operation
       *  and then calculates the appropriate fee.
       */
      asset calculate_fee( const operation& op, const price& core_exchange_rate = price::unit_price() )const;
      asset set_fee( operation& op, const price& core_exchange_rate = price::unit_price() )const;

      void zero_all_fees();

      /**
       *  Validates all of the parameters are present and accounted for.
       */
      void validate()const;

      template<typename Operation>
      const typename Operation::fee_parameters_type& get()const
      {
         auto itr = parameters.find( typename Operation::fee_parameters_type() );
         FC_ASSERT( itr != parameters.end() );
         return itr->template get<typename Operation::fee_parameters_type>();
      }
      template<typename Operation>
      typename Operation::fee_parameters_type& get()
      {
         auto itr = parameters.find( typename Operation::fee_parameters_type() );
         FC_ASSERT( itr != parameters.end() );
         return itr->template get<typename Operation::fee_parameters_type>();
      }

      /**
       *  @note must be sorted by fee_parameters.which() and have no duplicates
       */
      flat_set<fee_parameters> parameters;
      uint32_t                 scale = GRAPHENE_100_PERCENT; ///< fee * scale / GRAPHENE_100_PERCENT
   };

   typedef fee_schedule fee_schedule_type;

} } // graphene::chain

FC_REFLECT_TYPENAME( graphene::chain::fee_parameters )
FC_REFLECT( graphene::chain::fee_schedule, (parameters)(scale) )
