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
#include <graphene/chain/vesting_balance_object.hpp>
#include <graphene/chain/worker_object.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

void refund_worker_type::pay_worker(share_type pay, database& db)
{
   total_burned += pay;
   db.modify(db.get(asset_id_type()).dynamic_data(db), [pay](asset_dynamic_data_object& d) {
      d.current_supply -= pay;
   });
}

void vesting_balance_worker_type::pay_worker(share_type pay, database& db)
{
   db.modify(balance(db), [&](vesting_balance_object& b) {
      b.deposit(db.head_block_time(), asset(pay));
   });
}

void vesting_balance_worker_type::initializer::init(database& db, const worker_object& obj, vesting_balance_worker_type& worker) const
{
   worker.balance = db.create<vesting_balance_object>([&](vesting_balance_object& b) {
         b.owner = obj.worker_account;
         b.balance = asset(0);

         cdd_vesting_policy policy;
         policy.vesting_seconds = fc::days(pay_vesting_period_days).to_seconds();
         policy.coin_seconds_earned = 0;
         policy.coin_seconds_earned_last_update = db.head_block_time();
         b.policy = policy;
   }).id;
}

} } // graphene::chain
