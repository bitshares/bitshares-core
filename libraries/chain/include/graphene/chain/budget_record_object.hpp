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
#pragma once
#include <graphene/chain/protocol/types.hpp>
#include <graphene/db/object.hpp>
#include <graphene/db/generic_index.hpp>

namespace graphene { namespace chain {

struct budget_record
{
   uint64_t time_since_last_budget = 0;

   // sources of budget
   share_type from_initial_reserve = 0;
   share_type from_accumulated_fees = 0;
   share_type from_unused_witness_budget = 0;

   // witness budget requested by the committee
   share_type requested_witness_budget = 0;

   // funds that can be released from reserve at maximum rate
   share_type total_budget = 0;

   // sinks of budget, should sum up to total_budget
   share_type witness_budget = 0;
   share_type worker_budget = 0;

   // unused budget
   share_type leftover_worker_funds = 0;

   // change in supply due to budget operations
   share_type supply_delta = 0;
};

class budget_record_object;

class budget_record_object : public graphene::db::abstract_object<budget_record_object>
{
   public:
      static const uint8_t space_id = implementation_ids;
      static const uint8_t type_id = impl_budget_record_object_type;

      fc::time_point_sec time;
      budget_record record;
};

} }

FC_REFLECT(
   graphene::chain::budget_record,
   (time_since_last_budget)
   (from_initial_reserve)
   (from_accumulated_fees)
   (from_unused_witness_budget)
   (requested_witness_budget)
   (total_budget)
   (witness_budget)
   (worker_budget)
   (leftover_worker_funds)
   (supply_delta)
)

FC_REFLECT_DERIVED(
   graphene::chain::budget_record_object,
   (graphene::db::object),
   (time)
   (record)
)
