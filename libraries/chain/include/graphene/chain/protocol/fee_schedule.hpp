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
