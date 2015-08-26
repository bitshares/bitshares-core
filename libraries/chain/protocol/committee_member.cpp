/* Copyright (C) Cryptonomex, Inc - All Rights Reserved **/
#include <graphene/chain/protocol/committee_member.hpp>

namespace graphene { namespace chain {

void committee_member_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

void committee_member_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0 );
   if( new_url.valid() )
      FC_ASSERT(new_url->size() < GRAPHENE_MAX_URL_LENGTH );
}

void committee_member_update_global_parameters_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
   new_parameters.validate();
}

} } // graphene::chain
