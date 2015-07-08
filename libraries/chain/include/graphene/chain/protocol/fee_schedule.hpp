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

      /**
       *  @note must be sorted by fee_parameters.which() and have no duplicates
       */
      flat_set<fee_parameters> parameters;
      uint32_t                 scale; ///< fee * scale / GRAPHENE_100_PERCENT
   };

   typedef fee_schedule fee_schedule_type;

} } // graphene::chain 

FC_REFLECT_TYPENAME( graphene::chain::fee_parameters )
FC_REFLECT( graphene::chain::fee_schedule, (parameters) )

