#include <graphene/chain/balance_evaluator.hpp>

namespace graphene { namespace chain {

void_result balance_claim_evaluator::do_evaluate(const balance_claim_operation& op)
{
   database& d = db();
   balance = &op.balance_to_claim(d);

   FC_ASSERT(op.balance_owner_key == balance->owner ||
             pts_address(op.balance_owner_key, false, 56) == balance->owner ||
             pts_address(op.balance_owner_key, true, 56) == balance->owner ||
             pts_address(op.balance_owner_key, false, 0) == balance->owner ||
             pts_address(op.balance_owner_key, true, 0) == balance->owner,
             "balance_owner_key does not match balance's owner");
   if( !(d.get_node_properties().skip_flags & (database::skip_authority_check |
                                               database::skip_transaction_signatures)) )

   FC_ASSERT(op.total_claimed.asset_id == balance->asset_type());

   if( balance->is_vesting_balance() ) {
      if( !balance->vesting_policy->is_withdraw_allowed({balance->balance,
                                                        d.head_block_time(),
                                                        op.total_claimed}) )
         FC_THROW_EXCEPTION(invalid_claim_amount,
                            "Attempted to claim ${c} from a vesting balance with ${a} available",
                            ("c", op.total_claimed)("a", balance->available(d.head_block_time())));
      if( d.head_block_time() - balance->last_claim_date < fc::days(1) )
         FC_THROW_EXCEPTION(balance_claimed_too_often,
                            "Genesis vesting balances may not be claimed more than once per day.");
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
