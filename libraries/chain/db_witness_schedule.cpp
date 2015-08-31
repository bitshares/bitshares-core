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
#pragma once

#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/witness_object.hpp>

namespace graphene { namespace chain {

using boost::container::flat_set;

witness_id_type database::get_scheduled_witness( uint32_t slot_num )const
{
   //
   // Each witness gets an arbitration key H(time, witness_id).
   // The witness with the smallest key is selected to go first.
   //
   // As opposed to just using H(time) to determine an index into
   // an array of eligible witnesses, this has the following desirable
   // properties:
   //
   // - Avoid dynamic memory allocation
   // - Decreases (but does not eliminate) the probability that a
   // missed block will change the witness assigned to a future slot
   //
   // The hash function is xorshift* as given in
   // [1] https://en.wikipedia.org/wiki/Xorshift#Xorshift.2A
   //

   const flat_set< witness_id_type >& active_witnesses = get_global_properties().active_witnesses;
   uint32_t n = active_witnesses.size();
   uint64_t min_witness_separation;
   if( GRAPHENE_DEFAULT_MIN_WITNESS_COUNT < 5 && BOOST_UNLIKELY( n < 5 ) )
   {
      // special-case 0 and 1.
      // for 2 give a value which results in witnesses alternating slots
      // when there is no missed block
      // for 3-4 give values which don't lock in a single permutation
      switch( n )
      {
         case 0:
            assert(false);
         case 1:
            return *active_witnesses.begin();
         case 2:
         case 3:
            min_witness_separation = 1;
            break;
         case 4:
            min_witness_separation = 2;
            break;
      }
   }
   else
      min_witness_separation = (n/2)+1;

   uint64_t current_aslot = get_dynamic_global_properties().current_aslot + slot_num;

   if( slot_num == 0 ) // then return the witness that produced the last block
   {
      for( const witness_id_type& wit_id : active_witnesses )
      {
         const witness_object& wit = wit_id(*this);
         if( wit.last_aslot >= current_aslot )
            return wit_id;
      }
   }


   uint64_t start_of_current_round_aslot = current_aslot - (current_aslot % n);
   uint64_t first_ineligible_aslot = std::min( start_of_current_round_aslot, current_aslot - min_witness_separation );
   //
   // overflow analysis of above subtraction:
   //
   // we always have min_witness_separation <= n, so
   // if current_aslot < min_witness_separation it follows that
   // start_of_current_round_aslot == 0
   //
   // therefore result of above min() is 0 when subtraction overflows
   //

   first_ineligible_aslot = std::max( first_ineligible_aslot, uint64_t( 1 ) );

   uint64_t best_k = 0;
   witness_id_type best_wit;
   bool success = false;

   uint64_t now_hi = get_slot_time( slot_num ).sec_since_epoch();
   now_hi <<= 32;

   for( const witness_id_type& wit_id : active_witnesses )
   {
      const witness_object& wit = wit_id(*this);
      if( wit.last_aslot >= first_ineligible_aslot )
         continue;

      /// High performance random generator
      /// http://xorshift.di.unimi.it/
      uint64_t k = now_hi + uint64_t(wit_id)*2685821657736338717ULL;
      k ^= (k >> 12);
      k ^= (k << 25);
      k ^= (k >> 27);
      k *= 2685821657736338717ULL;
      if( k >= best_k )
      {
         best_k = k;
         best_wit = wit_id;
         success = true;
      }
   }

   // the above loop should choose at least 1 because
   //   at most K elements are susceptible to the filter,
   //   otherwise we have an inconsistent database (such as
   //   wit.last_aslot values that are non-unique or in the future)
   if( !success ) {
      edump((best_k)(slot_num)(first_ineligible_aslot)(current_aslot)(start_of_current_round_aslot)(min_witness_separation)(active_witnesses.size()));

      for( const witness_id_type& wit_id : active_witnesses )
      {
         const witness_object& wit = wit_id(*this);
         if( wit.last_aslot >= first_ineligible_aslot )
            idump((wit_id)(wit.last_aslot));
      }

      assert( success );
   }

   return best_wit;
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

uint32_t database::witness_participation_rate()const
{
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   return uint64_t(GRAPHENE_100_PERCENT) * dpo.recent_slots_filled.popcount() / 128;
}

} }
