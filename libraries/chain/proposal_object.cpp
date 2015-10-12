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
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/proposal_object.hpp>

namespace graphene { namespace chain {

bool proposal_object::is_authorized_to_execute(database& db) const
{
   transaction_evaluation_state dry_run_eval(&db);

   try {
      verify_authority( proposed_transaction.operations, 
                        available_key_approvals,
                        [&]( account_id_type id ){ return &id(db).active; },
                        [&]( account_id_type id ){ return &id(db).owner;  },
                        db.get_global_properties().parameters.max_authority_depth,
                        true, /* allow committeee */
                        available_active_approvals,
                        available_owner_approvals );
   } 
   catch ( const fc::exception& e )
   {
      //idump((available_active_approvals));
      //wlog((e.to_detail_string()));
      return false;
   }
   return true;
}


void required_approval_index::object_inserted( const object& obj )
{
    assert( dynamic_cast<const proposal_object*>(&obj) );
    const proposal_object& p = static_cast<const proposal_object&>(obj);

    for( const auto& a : p.required_active_approvals )
       _account_to_proposals[a].insert( p.id );
    for( const auto& a : p.required_owner_approvals )
       _account_to_proposals[a].insert( p.id );
    for( const auto& a : p.available_active_approvals )
       _account_to_proposals[a].insert( p.id );
    for( const auto& a : p.available_owner_approvals )
       _account_to_proposals[a].insert( p.id );
}

void required_approval_index::remove( account_id_type a, proposal_id_type p )
{
    auto itr = _account_to_proposals.find(a);
    if( itr != _account_to_proposals.end() )
    {
        itr->second.erase( p );
        if( itr->second.empty() )
            _account_to_proposals.erase( itr->first );
    }
}

void required_approval_index::object_removed( const object& obj )
{
    assert( dynamic_cast<const proposal_object*>(&obj) );
    const proposal_object& p = static_cast<const proposal_object&>(obj);

    for( const auto& a : p.required_active_approvals )
       remove( a, p.id );
    for( const auto& a : p.required_owner_approvals )
       remove( a, p.id );
    for( const auto& a : p.available_active_approvals )
       remove( a, p.id );
    for( const auto& a : p.available_owner_approvals )
       remove( a, p.id );
}

} } // graphene::chain
