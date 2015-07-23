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
#include <fc/uint128.hpp>

#include <graphene/chain/protocol/chain_parameters.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/db/object.hpp>

namespace graphene { namespace chain {

   /**
    * @class global_property_object
    * @brief Maintains global state information (committee_member list, current fees)
    * @ingroup object
    * @ingroup implementation
    *
    * This is an implementation detail. The values here are set by committee_members to tune the blockchain parameters.
    */
   class global_property_object : public graphene::db::abstract_object<global_property_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_global_property_object_type;

         chain_parameters           parameters;
         optional<chain_parameters> pending_parameters;

         uint32_t                   next_available_vote_id = 0;
         vector<committee_member_id_type>   active_committee_members; // updated once per maintenance interval
         flat_set<witness_id_type>  active_witnesses; // updated once per maintenance interval
         // n.b. witness scheduling is done by witness_schedule object
         flat_set<account_id_type>  witness_accounts; // updated once per maintenance interval

         fc::sha256                 chain_id;
   };

   /**
    * @class dynamic_global_property_object
    * @brief Maintains global state information (committee_member list, current fees)
    * @ingroup object
    * @ingroup implementation
    *
    * This is an implementation detail. The values here are calculated during normal chain operations and reflect the
    * current values of global blockchain properties.
    */
   class dynamic_global_property_object : public abstract_object<dynamic_global_property_object>
   {
      public:
         static const uint8_t space_id = implementation_ids;
         static const uint8_t type_id  = impl_dynamic_global_property_object_type;

         secret_hash_type  random;
         uint32_t          head_block_number = 0;
         block_id_type     head_block_id;
         time_point_sec    time;
         witness_id_type   current_witness;
         time_point_sec    next_maintenance_time;
         time_point_sec    last_budget_time;
         share_type        witness_budget;
         uint32_t          accounts_registered_this_interval = 0;
         /**
          *  Every time a block is missed this increases by 2, every time a block is found it decreases by 1 it is
          *  never less than 0
          *
          *  If the recently_missed_count hits 2*UNDO_HISTORY then no ew blocks may be pushed.  
          */
         uint32_t          recently_missed_count = 0;

         /** if the interval changes then how we calculate witness participation will
          * also change.  Normally witness participation is defined as % of blocks
          * produced in the last round which is calculated by dividing the delta
          * time between block N and N-NUM_WITNESSES by the block interval to calculate
          * the number of blocks produced.
          */
         uint32_t          first_maintenance_block_with_current_interval = 0;
   };
}}

FC_REFLECT_DERIVED( graphene::chain::dynamic_global_property_object, (graphene::db::object),
                    (random)
                    (head_block_number)
                    (head_block_id)
                    (time)
                    (current_witness)
                    (next_maintenance_time)
                    (witness_budget)
                    (accounts_registered_this_interval)
                    (recently_missed_count)
                    (first_maintenance_block_with_current_interval)
                  )

FC_REFLECT_DERIVED( graphene::chain::global_property_object, (graphene::db::object),
                    (parameters)
                    (pending_parameters)
                    (next_available_vote_id)
                    (active_committee_members)
                    (active_witnesses)
                    (chain_id)
                  )
