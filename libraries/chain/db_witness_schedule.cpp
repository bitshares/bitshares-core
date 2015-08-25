/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/chain/database.hpp>

#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/witness_schedule_object.hpp>

namespace graphene { namespace chain {

pair<witness_id_type, bool> database::get_scheduled_witness(uint32_t slot_num)const
{
   if( slot_num == 0 )
      return pair<witness_id_type, bool>(witness_id_type(), false);

   const witness_schedule_object& wso = witness_schedule_id_type()(*this);

   // ask the near scheduler who goes in the given slot
   witness_id_type wid;
   bool slot_is_near = wso.scheduler.get_slot(slot_num-1, wid);
   if( ! slot_is_near )
   {
      // if the near scheduler doesn't know, we have to extend it to
      //   a far scheduler.
      // n.b. instantiating it is slow, but block gaps long enough to
      //   need it are likely pretty rare.

      witness_scheduler_rng far_rng(wso.rng_seed.begin(), GRAPHENE_FAR_SCHEDULE_CTR_IV);

      far_future_witness_scheduler far_scheduler =
         far_future_witness_scheduler(wso.scheduler, far_rng);
      if( !far_scheduler.get_slot(slot_num-1, wid) )
      {
         // no scheduled witness -- somebody set up us the bomb
         // n.b. this code path is impossible, the present
         // implementation of far_future_witness_scheduler
         // returns true unconditionally
         assert( false );
      }
   }
   return pair<witness_id_type, bool>(wid, slot_is_near);
}

fc::time_point_sec database::get_slot_time(uint32_t slot_num)const
{
   if( slot_num == 0 )
      return fc::time_point_sec();

   auto interval = block_interval();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();

   if( head_block_num() == 0 )
   {
      // n.b. first block is at genesis_time plus one block interval
      fc::time_point_sec genesis_time = dpo.time;
      return genesis_time + slot_num * interval;
   }

   auto head_block_abs_slot = head_block_time().sec_since_epoch() / interval;
   fc::time_point_sec head_slot_time(head_block_abs_slot * interval);

   const global_property_object& gpo = get_global_properties();

   // "slot 0" is head_slot_time
   // "slot 1" is head_slot_time,
   //   plus maint interval if head block is a maint block
   //   plus block interval if head block is not a maint block
   return head_slot_time
        + (slot_num +
           (
            (dpo.dynamic_flags & dynamic_global_property_object::maintenance_flag)
            ? gpo.parameters.maintenance_skip_slots : 0
           )
          ) * interval
        ;
}

uint32_t database::get_slot_at_time(fc::time_point_sec when)const
{
   fc::time_point_sec first_slot_time = get_slot_time( 1 );
   if( when < first_slot_time )
      return 0;
   return (when - first_slot_time).to_seconds() / block_interval() + 1;
}

vector<witness_id_type> database::get_near_witness_schedule()const
{
   const witness_schedule_object& wso = witness_schedule_id_type()(*this);

   vector<witness_id_type> result;
   result.reserve(wso.scheduler.size());
   uint32_t slot_num = 0;
   witness_id_type wid;

   while( wso.scheduler.get_slot(slot_num++, wid) )
      result.emplace_back(wid);

   return result;
}

void database::update_witness_schedule(const signed_block& next_block)
{
   auto start = fc::time_point::now();
   const global_property_object& gpo = get_global_properties();
   const witness_schedule_object& wso = get(witness_schedule_id_type());
   uint32_t schedule_needs_filled = gpo.active_witnesses.size();
   uint32_t schedule_slot = get_slot_at_time(next_block.timestamp);

   // We shouldn't be able to generate _pending_block with timestamp
   // in the past, and incoming blocks from the network with timestamp
   // in the past shouldn't be able to make it this far without
   // triggering FC_ASSERT elsewhere

   assert( schedule_slot > 0 );
   witness_id_type first_witness;
   bool slot_is_near = wso.scheduler.get_slot( schedule_slot-1, first_witness );

   witness_id_type wit;

   //const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   
   assert( witness_scheduler_rng::seed_length == wso.rng_seed.size() );

   modify(wso, [&](witness_schedule_object& _wso)
   {
      _wso.slots_since_genesis += schedule_slot;
      witness_scheduler_rng rng(wso.rng_seed.data, _wso.slots_since_genesis);

      _wso.scheduler._min_token_count = std::max(int(gpo.active_witnesses.size()) / 2, 1);

      if( slot_is_near )
      {
         uint32_t drain = schedule_slot;
         while( drain > 0 )
         {
            if( _wso.scheduler.size() == 0 )
               break;
            _wso.scheduler.consume_schedule();
            --drain;
         }
      }
      else
      {
         _wso.scheduler.reset_schedule( first_witness );
      }
      while( !_wso.scheduler.get_slot(schedule_needs_filled, wit) )
      {
         auto random = fc::ripemd160::hash( next_block.timestamp );
         if( _wso.scheduler.produce_schedule(rng) & emit_turn )
            memcpy(_wso.rng_seed.begin(), random.data(), random.data_size());
      }
      _wso.last_scheduling_block = next_block.block_num();
      _wso.recent_slots_filled = (
           (_wso.recent_slots_filled << 1)
           + 1) << (schedule_slot - 1);
   });
   auto end = fc::time_point::now();
   static uint64_t total_time = 0;
   static uint64_t calls = 0;
   total_time += (end - start).count();
   if( ++calls % 1000 == 0 )
      idump( ( double(total_time/1000000.0)/calls) );
}

uint32_t database::witness_participation_rate()const
{
   const witness_schedule_object& wso = get(witness_schedule_id_type());
   return uint64_t(GRAPHENE_100_PERCENT) * wso.recent_slots_filled.popcount() / 128;
}

} }
