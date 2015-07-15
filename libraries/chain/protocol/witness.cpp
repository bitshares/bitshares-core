/* Copyright (C) Cryptonomex, Inc - All Rights Reserved **/
#include <graphene/chain/protocol/witness.hpp>

namespace graphene { namespace chain {

void witness_create_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(url.size() < GRAPHENE_MAX_URL_LENGTH );
}

} } // graphene::chain
