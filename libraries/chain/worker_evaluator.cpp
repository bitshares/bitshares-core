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
#include <graphene/chain/worker_evaluator.hpp>
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/account_object.hpp>

namespace graphene { namespace chain {

object_id_type worker_create_evaluator::do_evaluate(const worker_create_evaluator::operation_type& o)
{ try {
   database& d = db();

   FC_ASSERT(d.get(o.owner).is_lifetime_member());
   FC_ASSERT(o.work_begin_date >= d.head_block_time());

   return object_id_type();
} FC_CAPTURE_AND_RETHROW((o)) }

object_id_type worker_create_evaluator::do_apply(const worker_create_evaluator::operation_type& o)
{ try {
   database& d = db();
   vote_id_type for_id, against_id;
   d.modify(d.get_global_properties(), [&for_id, &against_id](global_property_object& p) {
      for_id = p.get_next_vote_id(vote_id_type::worker);
      against_id = p.get_next_vote_id(vote_id_type::worker);
   });

   return d.create<worker_object>([&](worker_object& w) {
      w.worker_account = o.owner;
      w.daily_pay = o.daily_pay;
      w.work_begin_date = o.work_begin_date;
      w.work_end_date = o.work_end_date;
      w.vote_for = for_id;
      w.vote_against = against_id;
      w.worker.set_which(o.initializer.which());
      w.worker.visit(worker_initialize_visitor(w, o.initializer, d));
   }).id;
} FC_CAPTURE_AND_RETHROW((o)) }

} } // graphene::chain
