/*
 * Copyright (c) 2018 Abit More, and contributors.
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
#include <graphene/chain/protocol/custom_authority.hpp>
#include <graphene/chain/protocol/operations.hpp>

namespace graphene { namespace chain {

share_type custom_authority_create_operation::calculate_fee( const fee_parameters_type& k )const
{
   share_type core_fee_required = k.basic_fee;

   if( enabled )
   {
      share_type unit_fee = k.price_per_k_unit;
      unit_fee *= (valid_to - valid_from).to_seconds();
      unit_fee *= auth.num_auths();
      uint64_t restriction_units = 0;
      for( const auto& restriction : restrictions )
      {
         restriction_units += restriction.get_units();
      }
      unit_fee *= restriction_units;
      unit_fee /= 1000;
      core_fee_required += unit_fee;
   }

   return core_fee_required;
}

void custom_authority_create_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee amount can not be negative" );

   FC_ASSERT( account != GRAPHENE_TEMP_ACCOUNT
              && account != GRAPHENE_COMMITTEE_ACCOUNT
              && account != GRAPHENE_WITNESS_ACCOUNT
              && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
              "Can not create custom authority for special accounts" );

   FC_ASSERT( valid_from < valid_to, "valid_from must be earlier than valid_to" );

   // Note: when adding new operation with hard fork, need to check more strictly in evaluator
   // TODO add code in evaluator
   FC_ASSERT( operation_type < operation::count(), "operation_type is too large" );

   // Note: allow auths to be empty
   //FC_ASSERT( auth.num_auths() > 0, "Can not set empty auth" );
   FC_ASSERT( auth.address_auths.size() == 0, "Address auth is not supported" );
   // Note: allow auths to be impossible
   //FC_ASSERT( !auth.is_impossible(), "cannot use an imposible authority threshold" );

   // Note: allow restrictions to be empty
   for( const auto& restriction : restrictions )
   {
      // recursively validate member index and argument type
      restriction.validate( operation_type );
   }
}

share_type custom_authority_update_operation::calculate_fee( const fee_parameters_type& k )const
{
   share_type core_fee_required = k.basic_fee;

   share_type unit_fee = k.price_per_k_unit;
   unit_fee *= delta_units;
   unit_fee /= 1000;

   return core_fee_required + unit_fee;
}

void custom_authority_update_operation::validate()const
{
   FC_ASSERT( fee.amount >= 0, "Fee amount can not be negative" );

   FC_ASSERT( account != GRAPHENE_TEMP_ACCOUNT
              && account != GRAPHENE_COMMITTEE_ACCOUNT
              && account != GRAPHENE_WITNESS_ACCOUNT
              && account != GRAPHENE_RELAXED_COMMITTEE_ACCOUNT,
              "Can not create custom authority for special accounts" );
/*
   FC_ASSERT( valid_from < valid_to, "valid_from must be earlier than valid_to" );

   // Note: when adding new operation with hard fork, need to check more strictly in evaluator
   // TODO add code in evaluator
   FC_ASSERT( operation_type < operation::count(), "operation type too large" );

   FC_ASSERT( auth.num_auths() > 0, "Can not set empty auth" );
   FC_ASSERT( auth.address_auths.size() == 0, "Address auth is not supported" );
   //FC_ASSERT( !auth.is_impossible(), "cannot use an imposible authority threshold" );

   // Note: allow restrictions to be empty
   for( const auto& restriction : restrictions )
   {
      // TODO recursively validate member index and argument type
      restriction.validate( operation_type );
   }
*/
}

void custom_authority_delete_operation::validate()const
{
}

} } // graphene::chain
