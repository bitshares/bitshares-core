
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/protocol/vote.hpp>
#include <fc/variant.hpp>

namespace graphene { namespace chain {

vote_id_type get_next_vote_id( global_property_object& gpo, vote_id_type::vote_type type )
{
   return vote_id_type( type, gpo.next_available_vote_id++ );
}

} } // graphene::chain

namespace fc
{

void to_variant(const graphene::chain::vote_id_type& var, variant& vo)
{
   vo = string(var);
}

void from_variant(const variant& var, graphene::chain::vote_id_type& vo)
{
   vo = graphene::chain::vote_id_type(var.as_string());
}

} // fc
