/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/database.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/restriction_predicate.hpp>

namespace graphene { namespace chain {

bool proposal_object::is_authorized_to_execute( database& db ) const
{
   transaction_evaluation_state dry_run_eval( &db );

   try {
      bool allow_non_immediate_owner = ( db.head_block_time() >= HARDFORK_CORE_584_TIME );
      verify_authority( proposed_transaction.operations,
                        available_key_approvals,
                        [&db]( account_id_type id ){ return &id( db ).active; },
                        [&db]( account_id_type id ){ return &id( db ).owner;  },
                        [&db]( account_id_type id, const operation& op, rejected_predicate_map* rejects ){
                           return db.get_viable_custom_authorities(id, op, rejects); },
                        allow_non_immediate_owner,
                        MUST_IGNORE_CUSTOM_OP_REQD_AUTHS( db.head_block_time() ),
                        db.get_global_properties().parameters.max_authority_depth,
                        true, /* allow committee */
                        available_active_approvals,
                        available_owner_approvals );
   } 
   catch ( const fc::exception& e )
   {
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

void required_approval_index::insert_or_remove_delta( proposal_id_type p,
                                                      const flat_set<account_id_type>& before,
                                                      const flat_set<account_id_type>& after )
{
    auto b = before.begin();
    auto a = after.begin();
    while( b != before.end() || a != after.end() )
    {
       if( a == after.end() || (b != before.end() && *b < *a) )
       {
           remove( *b, p );
           ++b;
       }
       else if( b == before.end() || (a != after.end() && *a < *b) )
       {
           _account_to_proposals[*a].insert( p );
           ++a;
       }
       else // *a == *b
       {
           ++a;
           ++b;
       }
    }
}

void required_approval_index::about_to_modify( const object& before )
{
    const proposal_object& p = static_cast<const proposal_object&>(before);
    available_active_before_modify = p.available_active_approvals;
    available_owner_before_modify  = p.available_owner_approvals;
}

void required_approval_index::object_modified( const object& after )
{
    const proposal_object& p = static_cast<const proposal_object&>(after);
    insert_or_remove_delta( p.id, available_active_before_modify, p.available_active_approvals );
    insert_or_remove_delta( p.id, available_owner_before_modify,  p.available_owner_approvals );
}

} } // graphene::chain

FC_REFLECT_DERIVED_NO_TYPENAME( graphene::chain::proposal_object, (graphene::chain::object),
                    (expiration_time)(review_period_time)(proposed_transaction)(required_active_approvals)
                    (available_active_approvals)(required_owner_approvals)(available_owner_approvals)
                    (available_key_approvals)(proposer)(fail_reason) )

GRAPHENE_IMPLEMENT_EXTERNAL_SERIALIZATION( graphene::chain::proposal_object )
