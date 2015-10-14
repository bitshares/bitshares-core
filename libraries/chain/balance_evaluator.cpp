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
#include <graphene/chain/balance_evaluator.hpp>

namespace graphene { namespace chain {

void_result balance_claim_evaluator::do_evaluate(const balance_claim_operation& op)
{
   database& d = db();
   balance = &op.balance_to_claim(d);

   GRAPHENE_ASSERT(
             op.balance_owner_key == balance->owner ||
             pts_address(op.balance_owner_key, false, 56) == balance->owner ||
             pts_address(op.balance_owner_key, true, 56) == balance->owner ||
             pts_address(op.balance_owner_key, false, 0) == balance->owner ||
             pts_address(op.balance_owner_key, true, 0) == balance->owner,
             balance_claim_owner_mismatch,
             "Balance owner key was specified as '${op}' but balance's actual owner is '${bal}'",
             ("op", op.balance_owner_key)
             ("bal", balance->owner)
             );
   if( !(d.get_node_properties().skip_flags & (database::skip_authority_check |
                                               database::skip_transaction_signatures)) )

   FC_ASSERT(op.total_claimed.asset_id == balance->asset_type());

   if( balance->is_vesting_balance() )
   {
      GRAPHENE_ASSERT(
         balance->vesting_policy->is_withdraw_allowed(
            { balance->balance,
              d.head_block_time(),
              op.total_claimed } ),
         balance_claim_invalid_claim_amount,
         "Attempted to claim ${c} from a vesting balance with ${a} available",
         ("c", op.total_claimed)("a", balance->available(d.head_block_time()))
         );
      GRAPHENE_ASSERT(
         d.head_block_time() - balance->last_claim_date >= fc::days(1),
         balance_claim_claimed_too_often,
         "Genesis vesting balances may not be claimed more than once per day."
         );
      return {};
   }

   FC_ASSERT(op.total_claimed == balance->balance);
   return {};
}

/**
 * @note the fee is always 0 for this particular operation because once the
 * balance is claimed it frees up memory and it cannot be used to spam the network
 */
void_result balance_claim_evaluator::do_apply(const balance_claim_operation& op)
{
   database& d = db();

   if( balance->is_vesting_balance() && op.total_claimed < balance->balance )
      d.modify(*balance, [&](balance_object& b) {
         b.vesting_policy->on_withdraw({b.balance, d.head_block_time(), op.total_claimed});
         b.balance -= op.total_claimed;
         b.last_claim_date = d.head_block_time();
      });
   else
      d.remove(*balance);

   d.adjust_balance(op.deposit_to_account, op.total_claimed);
   return {};
}
} } // namespace graphene::chain
