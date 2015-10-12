
#include <graphene/chain/genesis_state.hpp>

// these are required to serialize a genesis_state
#include <fc/smart_ref_impl.hpp>   // required for gcc in release mode
#include <graphene/chain/protocol/fee_schedule.hpp>

namespace graphene { namespace chain {

chain_id_type genesis_state_type::compute_chain_id() const
{
   return initial_chain_id;
}

} } // graphene::chain
