
#pragma once
#include <graphene/chain/protocol/base.hpp>

namespace graphene { namespace chain { 

   /**
    * @brief provides a generic way to add higher level protocols on top of witness consensus
    * @ingroup operations
    *
    * There is no validation for this operation other than that required auths are valid and a fee
    * is paid that is appropriate for the data contained.
    */
   struct custom_operation : public base_operation
   {
      struct fee_parameters_type { 
         uint64_t fee = GRAPHENE_BLOCKCHAIN_PRECISION; 
         uint32_t price_per_kbyte = 10;
      };

      asset                     fee;
      account_id_type           payer;
      flat_set<account_id_type> required_auths;
      uint16_t                  id = 0;
      vector<char>              data;

      account_id_type   fee_payer()const { return payer; }
      void              validate()const;
      share_type        calculate_fee(const fee_parameters_type& k)const;
   };

} } // namespace graphene::chain

FC_REFLECT( graphene::chain::custom_operation::fee_parameters_type, (fee)(price_per_kbyte) )
FC_REFLECT( graphene::chain::custom_operation, (fee)(payer)(required_auths)(id)(data) )
