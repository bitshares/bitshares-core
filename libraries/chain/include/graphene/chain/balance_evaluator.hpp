#pragma once

#include <graphene/chain/database.hpp>
#include <graphene/chain/transaction.hpp>
#include <graphene/chain/transaction_evaluation_state.hpp>
#include <graphene/chain/balance_object.hpp>
#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {

class balance_claim_evaluator : public evaluator<balance_claim_evaluator>
{
public:
   typedef balance_claim_operation operation_type;

   const balance_object* balance = nullptr;
   asset amount_withdrawn;

   asset do_evaluate(const balance_claim_operation& op)
   {
      database& d = db();
      balance = &op.balance_to_claim(d);

      FC_ASSERT(trx_state->signed_by( balance->owner, true /*maybe pts*/ ));
      FC_ASSERT(op.total_claimed.asset_id == balance->asset_type());

      if( balance->vesting_policy.valid() ) {
         FC_ASSERT(op.total_claimed.amount == 0);
         return amount_withdrawn = balance->vesting_policy->get_allowed_withdraw({balance->balance,
                                                                                  d.head_block_time(),
                                                                                  {}});
      }

      FC_ASSERT(op.total_claimed == balance->balance);
      return amount_withdrawn = op.total_claimed;
   }

   /**
    * @note the fee is always 0 for this particular operation because once the
    * balance is claimed it frees up memory and it cannot be used to spam the network
    */
   asset do_apply(const balance_claim_operation& op)
   {
      database& d = db();

      if( balance->vesting_policy.valid() && amount_withdrawn < balance->balance )
         d.modify(*balance, [&](balance_object& b) {
            b.vesting_policy->on_withdraw({b.balance, d.head_block_time(), amount_withdrawn});
            b.balance -= amount_withdrawn;
         });
      else
         d.remove(*balance);

      d.adjust_balance(op.deposit_to_account, amount_withdrawn);
      return amount_withdrawn;
   }
};

} } // graphene::chain
