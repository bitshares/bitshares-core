#include <graphene/chain/protocol/chain_parameters.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>

namespace graphene { namespace chain {
   chain_parameters::chain_parameters() {
       current_fees = std::make_shared<fee_schedule>();
   }
}}