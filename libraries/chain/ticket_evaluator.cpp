/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/chain/ticket_object.hpp>

#include <graphene/chain/ticket_evaluator.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>

#include <graphene/protocol/ticket.hpp>

namespace graphene { namespace chain {

void_result ticket_create_evaluator::do_evaluate(const ticket_create_operation& op)
{ try {
   const database& d = db();
   const auto block_time = d.head_block_time();

   FC_ASSERT( HARDFORK_CORE_2103_PASSED(block_time), "Not allowed until hardfork 2103" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

object_id_type ticket_create_evaluator::do_apply(const ticket_create_operation& op)
{ try {
   database& d = db();
   const auto block_time = d.head_block_time();
   const auto maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   ticket_version version = ( HARDFORK_CORE_2262_PASSED(maint_time) ? ticket_v2 : ticket_v1 );

   d.adjust_balance( op.account, -op.amount );

   const auto& new_ticket_object = d.create<ticket_object>([&op,block_time,version](ticket_object& obj){
      obj.init_new( block_time, op.account, op.target_type, op.amount, version );
   });

   // Note: amount.asset_id is checked in validate(), so no check here
   d.modify( d.get_account_stats_by_owner( op.account ), [&op,&new_ticket_object](account_statistics_object& aso) {
      aso.total_core_pol += op.amount.amount;
      aso.total_pol_value += new_ticket_object.value;
   });

   return new_ticket_object.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result ticket_update_evaluator::do_evaluate(const ticket_update_operation& op)
{ try {
   database& d = db();

   _ticket = &op.ticket(d);

   FC_ASSERT( _ticket->account == op.account, "Ticket is not owned by the account" );

   FC_ASSERT( _ticket->current_type != lock_forever, "Can not to update a ticket that is locked forever" );

   FC_ASSERT( static_cast<uint64_t>(_ticket->target_type) != op.target_type, "Target type does not change" );

   if( op.amount_for_new_target.valid() )
   {
      // Note: amount.asset_id is checked in validate(), so no check here
      FC_ASSERT( *op.amount_for_new_target <= _ticket->amount, "Insufficient amount in ticket to be updated" );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_operation_result ticket_update_evaluator::do_apply(const ticket_update_operation& op)
{ try {
   database& d = db();
   const auto block_time = d.head_block_time();
   const auto maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   ticket_version version = ( HARDFORK_CORE_2262_PASSED(maint_time) ? ticket_v2 : ticket_v1 );

   generic_operation_result result;

   share_type old_value = _ticket->value;
   share_type delta_value;

   // To partially update the ticket, aka splitting
   if ( op.amount_for_new_target.valid() && *op.amount_for_new_target < _ticket->amount )
   {
      const auto& new_ticket_object = d.create<ticket_object>([&op,this,block_time,version](ticket_object& obj){
         obj.init_split( block_time, *_ticket, op.target_type, *op.amount_for_new_target, version );
      });

      result.new_objects.insert( new_ticket_object.id );

      d.modify( *_ticket, [&op,version](ticket_object& obj){
         obj.adjust_amount( -(*op.amount_for_new_target), version );
      });
      delta_value = new_ticket_object.value + _ticket->value - old_value;
   }
   else // To update the whole ticket
   {
      d.modify( *_ticket, [&op,block_time,version](ticket_object& obj){
         obj.update_target_type( block_time, op.target_type, version );
      });
      delta_value = _ticket->value - old_value;
   }
   result.updated_objects.insert( _ticket->id );

   if( delta_value != 0 )
   {
      const auto& stat = d.get_account_stats_by_owner( op.account );
      d.modify( stat, [delta_value](account_statistics_object& aso) {
         aso.total_pol_value += delta_value;
      });
   }

   // Do auto-update now.
   // Note: calling process_tickets() here won't affect other tickets,
   //       since head_block_time is not updated after last call,
   //       even when called via a proposal this time or last time
   generic_operation_result process_result = d.process_tickets();
   result.removed_objects.insert( process_result.removed_objects.begin(), process_result.removed_objects.end() );
   result.updated_objects.insert( process_result.updated_objects.begin(), process_result.updated_objects.end() );
   for( const auto& id : result.new_objects )
      result.updated_objects.erase( id );
   for( const auto& id : result.removed_objects )
      result.updated_objects.erase( id );

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
