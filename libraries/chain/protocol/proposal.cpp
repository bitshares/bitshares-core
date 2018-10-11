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
#include <graphene/chain/protocol/operations.hpp>
#include <graphene/chain/protocol/fee_schedule.hpp>
#include <fc/smart_ref_impl.hpp>

namespace graphene { namespace chain {

proposal_create_operation proposal_create_operation::committee_proposal(const chain_parameters& global_params, fc::time_point_sec head_block_time )
{
   // TODO move this method to unit tests as it is not useful
   proposal_create_operation op;
   op.expiration_time = head_block_time + global_params.maximum_proposal_lifetime;
   op.review_period_seconds = global_params.committee_proposal_review_period;
   return op;
}

void proposal_create_operation::validate() const
{
   FC_ASSERT( !proposed_ops.empty() );
   for( const auto& op : proposed_ops ) operation_validate( op.op );
}

share_type proposal_create_operation::calculate_fee(const fee_parameters_type& k) const
{
   return k.fee + calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte );
}

void proposal_update_operation::validate() const
{
   FC_ASSERT(fee.amount >= 0);
   FC_ASSERT(!(active_approvals_to_add.empty() && active_approvals_to_remove.empty() &&
               owner_approvals_to_add.empty() && owner_approvals_to_remove.empty() &&
               key_approvals_to_add.empty() && key_approvals_to_remove.empty()));
   for( auto a : active_approvals_to_add )
   {
      FC_ASSERT(active_approvals_to_remove.find(a) == active_approvals_to_remove.end(),
                "Cannot add and remove approval at the same time.");
   }
   for( auto a : owner_approvals_to_add )
   {
      FC_ASSERT(owner_approvals_to_remove.find(a) == owner_approvals_to_remove.end(),
                "Cannot add and remove approval at the same time.");
   }
   flat_set<public_key_type> remove_keys( key_approvals_to_remove.begin(), key_approvals_to_remove.end() );
   for( auto a : key_approvals_to_add )
   {
      FC_ASSERT(remove_keys.find(a) == remove_keys.end(),
                "Cannot add and remove approval at the same time.");
   }
   check_key_set( key_approvals_to_add );
   check_key_set( key_approvals_to_remove );
}

void proposal_delete_operation::validate() const
{
   FC_ASSERT( fee.amount >= 0 );
}

share_type proposal_update_operation::calculate_fee(const fee_parameters_type& k) const
{
   return k.fee + calculate_data_fee( fc::raw::pack_size(*this), k.price_per_kbyte );
}

void proposal_update_operation::get_required_authorities( vector<const authority*>& o )const
{
   if( key_approvals_to_add.empty() && key_approvals_to_remove.empty() ) return;

   _auth.weight_threshold = key_approvals_to_add.size() + key_approvals_to_remove.size(); // we know they're disjoint
   vector<std::pair<public_key_type,weight_type>> temp;
   temp.reserve( _auth.weight_threshold );
   for( const auto& k : key_approvals_to_add )
      temp.emplace_back( k, 1 );
   for( const auto& k : key_approvals_to_remove )
      temp.emplace_back( k, 1 );
   std::sort( temp.begin(), temp.end(), compare_entries_by_address );
   _auth.key_auths = std::move( temp );

   o.push_back( &_auth );
}

void proposal_update_operation::get_required_active_authorities( flat_set<account_id_type>& a )const
{
   for( const auto& i : active_approvals_to_add )    a.insert(i);
   for( const auto& i : active_approvals_to_remove ) a.insert(i);
}

void proposal_update_operation::get_required_owner_authorities( flat_set<account_id_type>& a )const
{
   for( const auto& i : owner_approvals_to_add )    a.insert(i);
   for( const auto& i : owner_approvals_to_remove ) a.insert(i);
}

} } // graphene::chain
