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

// needed to serialize witness_scheduler
#include <fc/container/deque.hpp>
#include <fc/uint128.hpp>

#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/witness_scheduler.hpp>
#include <graphene/chain/witness_scheduler_rng.hpp>

namespace graphene { namespace chain {

typedef hash_ctr_rng<
   /* HashClass  = */ fc::sha256,
   /* SeedLength = */ GRAPHENE_RNG_SEED_LENGTH
   > witness_scheduler_rng;

typedef generic_witness_scheduler<
   /* WitnessID  = */ witness_id_type,
   /* RNG        = */ witness_scheduler_rng,
   /* CountType  = */ decltype( chain_parameters::maximum_witness_count ),
   /* OffsetType = */ uint32_t,
   /* debug      = */ true
   > witness_scheduler;

typedef generic_far_future_witness_scheduler<
   /* WitnessID  = */ witness_id_type,
   /* RNG        = */ witness_scheduler_rng,
   /* CountType  = */ decltype( chain_parameters::maximum_witness_count ),
   /* OffsetType = */ uint32_t,
   /* debug      = */ true
   > far_future_witness_scheduler;

class witness_schedule_object : public abstract_object<witness_schedule_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id = impl_witness_schedule_object_type;

      witness_scheduler scheduler;
      uint32_t last_scheduling_block;
      uint64_t slots_since_genesis = 0;
      fc::array< char, sizeof(secret_hash_type) > rng_seed;

      /**
       * Not necessary for consensus, but used for figuring out the participation rate.
       * The nth bit is 0 if the nth slot was unfilled, else it is 1.
       */
      fc::uint128 recent_slots_filled;
};

} }

FC_REFLECT( graphene::chain::witness_scheduler,
            (_turns)
            (_tokens)
            (_min_token_count)
            (_ineligible_waiting_for_token)
            (_ineligible_no_turn)
            (_eligible)
            (_schedule)
            (_lame_duck)
            )

FC_REFLECT_DERIVED( graphene::chain::witness_schedule_object, (graphene::chain::object),
                    (scheduler)
                    (last_scheduling_block)
                    (slots_since_genesis)
                    (rng_seed)
                    (recent_slots_filled)
                    )
