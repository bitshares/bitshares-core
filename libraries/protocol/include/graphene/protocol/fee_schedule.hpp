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
#include <graphene/protocol/operations.hpp>

namespace graphene { namespace protocol {

   template<typename T> struct transform_to_fee_parameters;
   template<typename ...T>
   struct transform_to_fee_parameters<fc::static_variant<T...>>
   {
      using type = fc::static_variant< typename T::fee_parameters_type... >;
   };
   using fee_parameters = transform_to_fee_parameters<operation>::type;

   template<typename Operation>
   class fee_helper {
     public:
      const typename Operation::fee_parameters_type& get(const fee_parameters::flat_set_type& parameters)const
      {
         auto itr = parameters.find( typename Operation::fee_parameters_type() );
         if( itr != parameters.end() )
            return itr->template get<typename Operation::fee_parameters_type>();

         static typename Operation::fee_parameters_type dummy;
         return dummy;
      }
      typename Operation::fee_parameters_type& mutable_get(fee_parameters::flat_set_type& parameters)const
      {
         auto itr = parameters.find( typename Operation::fee_parameters_type() );
         if( itr != parameters.end() )
            return itr->template get<typename Operation::fee_parameters_type >();
         
         static typename Operation::fee_parameters_type dummy = get(parameters);
         parameters.insert(dummy);
         return dummy;
      }
   };

   template<>
   class fee_helper<bid_collateral_operation> {
     public:
      const bid_collateral_operation::fee_parameters_type& get(const fee_parameters::flat_set_type& parameters)const
      {
         auto itr = parameters.find( bid_collateral_operation::fee_parameters_type() );
         if ( itr != parameters.end() )
            return itr->get<bid_collateral_operation::fee_parameters_type>();

         static bid_collateral_operation::fee_parameters_type bid_collateral_dummy;
         bid_collateral_dummy.fee = fee_helper<call_order_update_operation>().get(parameters).fee;
         return bid_collateral_dummy;
      }
   };

   template<>
   class fee_helper<asset_update_issuer_operation> {
     public:
      const asset_update_issuer_operation::fee_parameters_type& get(const fee_parameters::flat_set_type& parameters)const
      {
         auto itr = parameters.find( asset_update_issuer_operation::fee_parameters_type() );
         if ( itr != parameters.end() )
            return itr->get<asset_update_issuer_operation::fee_parameters_type>();

         static asset_update_issuer_operation::fee_parameters_type dummy;
         dummy.fee = fee_helper<asset_update_operation>().get(parameters).fee;
         return dummy;
      }
   };

   template<>
   class fee_helper<asset_claim_pool_operation> {
     public:
      const asset_claim_pool_operation::fee_parameters_type& get(const fee_parameters::flat_set_type& parameters)const
      {
         auto itr = parameters.find( asset_claim_pool_operation::fee_parameters_type() );
         if ( itr != parameters.end() )
            return itr->get<asset_claim_pool_operation::fee_parameters_type>();

         static asset_claim_pool_operation::fee_parameters_type asset_claim_pool_dummy;
         asset_claim_pool_dummy.fee = fee_helper<asset_fund_fee_pool_operation>().get(parameters).fee;
         return asset_claim_pool_dummy;
      }
   };



   /**
    *  @brief contains all of the parameters necessary to calculate the fee for any operation
    */
   struct fee_schedule
   {
      fee_schedule();

      static fee_schedule get_default();

      /**
       *  Finds the appropriate fee parameter struct for the operation
       *  and then calculates the appropriate fee in CORE asset.
       */
      asset calculate_fee( const operation& op )const;
      /**
       *  Finds the appropriate fee parameter struct for the operation
       *  and then calculates the appropriate fee in an asset specified
       *  implicitly by core_exchange_rate.
       */
      asset calculate_fee( const operation& op, const price& core_exchange_rate )const;
      /**
       *  Updates the operation with appropriate fee and returns the fee.
       */
      asset set_fee( operation& op, const price& core_exchange_rate = price::unit_price() )const;

      void zero_all_fees();

      /**
       *  Validates all of the parameters are present and accounted for.
       */
      void validate()const {}

      template<typename Operation>
      const typename Operation::fee_parameters_type& get()const
      {
         return fee_helper<Operation>().get(parameters);
      }
      template<typename Operation>
      typename Operation::fee_parameters_type& mutable_get()
      {
         return fee_helper<Operation>().mutable_get(parameters);
      }
      template<typename Operation>
      bool exists()const
      {
         auto itr = parameters.find(typename Operation::fee_parameters_type());
         return itr != parameters.end();
      }

      /**
       *  @note must be sorted by fee_parameters.which() and have no duplicates
       */
      fee_parameters::flat_set_type parameters;
      uint32_t                 scale = GRAPHENE_100_PERCENT; ///< fee * scale / GRAPHENE_100_PERCENT
      private:
      static void set_fee_parameters(fee_schedule& sched);
   };

   typedef fee_schedule fee_schedule_type;

} } // graphene::protocol

FC_REFLECT_TYPENAME( graphene::protocol::fee_parameters )
FC_REFLECT( graphene::protocol::fee_schedule, (parameters)(scale) )

GRAPHENE_DECLARE_EXTERNAL_SERIALIZATION( graphene::protocol::fee_schedule )
