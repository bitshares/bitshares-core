/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/market_object.hpp>

#include <graphene/chain/market_evaluator.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/protocol/market.hpp>
#include <graphene/protocol/fee_schedule.hpp>

namespace graphene { namespace chain {
void_result limit_order_create_evaluator::do_evaluate(const limit_order_create_operation& op)
{ try {
   const database& d = db();

   if( op.extensions.value.on_fill.valid() )
   {
      FC_ASSERT( HARDFORK_CORE_2535_PASSED( d.head_block_time() ) ,
                 "The on_fill extension is not allowed until the core-2535 hardfork");
      FC_ASSERT( 1 == op.extensions.value.on_fill->size(),
                 "The on_fill action list must contain only one action until expanded in a future hardfork" );
      const auto& take_profit_action = op.extensions.value.on_fill->front().get<create_take_profit_order_action>();
      FC_ASSERT( d.find( take_profit_action.fee_asset_id ), "Fee asset does not exist" );
   }

   FC_ASSERT( op.expiration >= d.head_block_time() );

   _seller        = this->fee_paying_account;
   _sell_asset    = &op.amount_to_sell.asset_id(d);
   _receive_asset = &op.min_to_receive.asset_id(d);

   if( _sell_asset->options.whitelist_markets.size() )
   {
      GRAPHENE_ASSERT( _sell_asset->options.whitelist_markets.find(_receive_asset->get_id())
                          != _sell_asset->options.whitelist_markets.end(),
                       limit_order_create_market_not_whitelisted,
                       "This market has not been whitelisted by the selling asset", );
   }
   if( _sell_asset->options.blacklist_markets.size() )
   {
      GRAPHENE_ASSERT( _sell_asset->options.blacklist_markets.find(_receive_asset->get_id())
                          == _sell_asset->options.blacklist_markets.end(),
                       limit_order_create_market_blacklisted,
                       "This market has been blacklisted by the selling asset", );
   }

   GRAPHENE_ASSERT( is_authorized_asset( d, *_seller, *_sell_asset ),
                    limit_order_create_selling_asset_unauthorized,
                    "The account is not allowed to transact the selling asset", );

   GRAPHENE_ASSERT( is_authorized_asset( d, *_seller, *_receive_asset ),
                    limit_order_create_receiving_asset_unauthorized,
                    "The account is not allowed to transact the receiving asset", );

   GRAPHENE_ASSERT( d.get_balance( *_seller, *_sell_asset ) >= op.amount_to_sell,
                    limit_order_create_insufficient_balance,
                    "insufficient balance",
                    ("balance",d.get_balance(*_seller,*_sell_asset))("amount_to_sell",op.amount_to_sell) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void limit_order_create_evaluator::convert_fee()
{
   if( db().head_block_time() <= HARDFORK_CORE_604_TIME )
      generic_evaluator::convert_fee();
   else if( fee_asset->get_id() != asset_id_type() )
   {
      db().modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& d) {
         d.fee_pool -= core_fee_paid;
      });
   }
}

void limit_order_create_evaluator::pay_fee()
{
   if( db().head_block_time() <= HARDFORK_445_TIME )
      generic_evaluator::pay_fee();
   else
   {
      _deferred_fee = core_fee_paid;
      if( db().head_block_time() > HARDFORK_CORE_604_TIME && fee_asset->get_id() != asset_id_type() )
         _deferred_paid_fee = fee_from_account;
   }
}

object_id_type limit_order_create_evaluator::do_apply(const limit_order_create_operation& op) const
{ try {
   if( op.amount_to_sell.asset_id == asset_id_type() )
   {
      db().modify( _seller->statistics(db()), [&op](account_statistics_object& bal) {
         bal.total_core_in_orders += op.amount_to_sell.amount;
      });
   }

   db().adjust_balance(op.seller, -op.amount_to_sell);

   const auto& new_order_object = db().create<limit_order_object>([this,&op](limit_order_object& obj){
       obj.seller   = _seller->id;
       obj.for_sale = op.amount_to_sell.amount;
       obj.sell_price = op.get_price();
       obj.expiration = op.expiration;
       obj.deferred_fee = _deferred_fee;
       obj.deferred_paid_fee = _deferred_paid_fee;
       if( op.extensions.value.on_fill.valid() )
          obj.on_fill = *op.extensions.value.on_fill;
   });
   object_id_type order_id = new_order_object.id; // save this because we may remove the object by filling it
   bool filled;
   if( db().get_dynamic_global_properties().next_maintenance_time <= HARDFORK_CORE_625_TIME )
      filled = db().apply_order_before_hardfork_625( new_order_object );
   else
      filled = db().apply_order( new_order_object );

   GRAPHENE_ASSERT( !op.fill_or_kill || filled,
                    limit_order_create_kill_unfilled,
                    "Killing limit order ${op} due to unable to fill",
                    ("op",op) );

   return order_id;
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void limit_order_update_evaluator::convert_fee()
{
   if( fee_asset->get_id() != asset_id_type() )
   {
      db().modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& addo) {
         addo.fee_pool -= core_fee_paid;
      });
   }
}

void limit_order_update_evaluator::pay_fee()
{
   _deferred_fee = core_fee_paid;
   if( fee_asset->get_id() != asset_id_type() )
      _deferred_paid_fee = fee_from_account;
}

void_result limit_order_update_evaluator::do_evaluate(const limit_order_update_operation& o)
{ try {
   const database& d = db();
   FC_ASSERT( HARDFORK_CORE_1604_PASSED( d.head_block_time() ) , "Operation has not activated yet");

   if( o.on_fill.valid() )
   {
      // Note: Assuming that HF core-1604 and HF core-2535 will take place at the same time,
      //       no check for HF core-2535 here.
      FC_ASSERT( o.on_fill->size() <= 1,
                 "The on_fill action list must contain zero or one action until expanded in a future hardfork" );
      if( !o.on_fill->empty() )
      {
         const auto& take_profit_action = o.on_fill->front().get<create_take_profit_order_action>();
         FC_ASSERT( d.find( take_profit_action.fee_asset_id ), "Fee asset does not exist" );
      }
   }

   _order = d.find( o.order );

   GRAPHENE_ASSERT( _order != nullptr,
                    limit_order_update_nonexist_order,
                    "Limit order ${oid} does not exist, cannot update",
                    ("oid", o.order) );

   // Check this is my order
   GRAPHENE_ASSERT( o.seller == _order->seller,
                    limit_order_update_owner_mismatch,
                    "Limit order ${oid} is owned by someone else, cannot update",
                    ("oid", o.order) );

   // Check new price is compatible and appropriate
   if (o.new_price) {
      auto base_id = o.new_price->base.asset_id;
      auto quote_id = o.new_price->quote.asset_id;
      FC_ASSERT(base_id == _order->sell_price.base.asset_id && quote_id == _order->sell_price.quote.asset_id,
                "Cannot update limit order with incompatible price");

      // Do not allow inappropriate price manipulation
      auto max_amount_for_sale = std::max( _order->for_sale, _order->sell_price.base.amount );
      if( o.delta_amount_to_sell )
         max_amount_for_sale += o.delta_amount_to_sell->amount;
      FC_ASSERT( o.new_price->base.amount <= max_amount_for_sale,
                 "The base amount in the new price cannot be greater than the estimated maximum amount for sale" );

   }

   // Check delta asset is compatible
   if (o.delta_amount_to_sell) {
      const auto& delta = *o.delta_amount_to_sell;
      FC_ASSERT(delta.asset_id == _order->sell_price.base.asset_id,
                "Cannot update limit order with incompatible asset");
      if (delta.amount < 0)
          FC_ASSERT(_order->for_sale > -delta.amount,
                    "Cannot deduct all or more from order than order contains");
      // Note: if the delta amount is positive, account balance will be checked when calling adjust_balance()
   }

   // Check dust
   if (o.new_price || (o.delta_amount_to_sell && o.delta_amount_to_sell->amount < 0)) {
      auto new_price = o.new_price? *o.new_price : _order->sell_price;
      auto new_amount = _order->amount_for_sale();
      if (o.delta_amount_to_sell)
          new_amount += *o.delta_amount_to_sell;
      auto new_amount_to_receive = new_amount * new_price;

      FC_ASSERT( new_amount_to_receive.amount > 0,
                 "Cannot update limit order: order becomes too small; cancel order instead" );
   }

   // Check expiration is in the future
   if (o.new_expiration)
      FC_ASSERT( *o.new_expiration >= d.head_block_time(), "Cannot update limit order to expire in the past" );

   // Check asset authorization
   // TODO refactor to fix duplicate code (see limit_order_create)
   const auto sell_asset_id = _order->sell_asset_id();
   const auto receive_asset_id = _order->receive_asset_id();
   const auto& sell_asset = sell_asset_id(d);
   const auto& receive_asset = receive_asset_id(d);
   const auto& seller = *this->fee_paying_account;

   if( !sell_asset.options.whitelist_markets.empty() )
   {
      FC_ASSERT( sell_asset.options.whitelist_markets.find( receive_asset_id )
                          != sell_asset.options.whitelist_markets.end(),
                 "This market has not been whitelisted by the selling asset" );
   }
   if( !sell_asset.options.blacklist_markets.empty() )
   {
      FC_ASSERT( sell_asset.options.blacklist_markets.find( receive_asset_id )
                          == sell_asset.options.blacklist_markets.end(),
                 "This market has been blacklisted by the selling asset" );
   }

   FC_ASSERT( is_authorized_asset( d, seller, sell_asset ),
              "The account is not allowed to transact the selling asset" );

   FC_ASSERT( is_authorized_asset( d, seller, receive_asset ),
              "The account is not allowed to transact the receiving asset" );

   return {};
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void limit_order_update_evaluator::process_deferred_fee()
{
   // Attempt to deduct a possibly discounted order cancellation fee, and refund the remainder.
   // Only deduct fee if there is any fee deferred.
   // TODO fix duplicate code (see database::cancel_limit_order())
   if( _order->deferred_fee <= 0 )
      return;

   database& d = db();

   share_type deferred_fee = _order->deferred_fee;
   asset deferred_paid_fee = _order->deferred_paid_fee;
   const asset_dynamic_data_object* deferred_fee_asset_dyn_data = nullptr;
   const auto& current_fees = d.current_fee_schedule();
   asset core_cancel_fee = current_fees.calculate_fee( limit_order_cancel_operation() );
   if( core_cancel_fee.amount > 0 )
   {
      // maybe-discounted cancel_fee calculation:
      //   limit_order_cancel_fee * limit_order_update_fee / limit_order_create_fee
      asset core_create_fee = current_fees.calculate_fee( limit_order_create_operation() );
      fc::uint128_t fee128( core_cancel_fee.amount.value );
      if( core_create_fee.amount > 0 )
      {
         asset core_update_fee = current_fees.calculate_fee( limit_order_update_operation() );
         fee128 *= core_update_fee.amount.value;
         fee128 /= core_create_fee.amount.value;
      }
      // cap the fee
      if( fee128 > static_cast<uint64_t>( deferred_fee.value ) )
         fee128 = deferred_fee.value;
      core_cancel_fee.amount = static_cast<int64_t>( fee128 );
   }

   // if there is any CORE fee to deduct, redirect it to referral program
   if( core_cancel_fee.amount > 0 )
   {
      if( !_seller_acc_stats )
         _seller_acc_stats = &d.get_account_stats_by_owner( _order->seller );
      d.modify( *_seller_acc_stats, [&core_cancel_fee, &d]( account_statistics_object& obj ) {
         obj.pay_fee( core_cancel_fee.amount, d.get_global_properties().parameters.cashback_vesting_threshold );
      } );
      deferred_fee -= core_cancel_fee.amount;
      // handle originally paid fee if any:
      //    to_deduct = round_up( paid_fee * core_cancel_fee / deferred_core_fee_before_deduct )
      if( deferred_paid_fee.amount > 0 )
      {
         fc::uint128_t fee128( deferred_paid_fee.amount.value );
         fee128 *= core_cancel_fee.amount.value;
         // to round up
         fee128 += _order->deferred_fee.value;
         fee128 -= 1;
         fee128 /= _order->deferred_fee.value;
         share_type cancel_fee_amount = static_cast<int64_t>(fee128);
         // cancel_fee should be positive, pay it to asset's accumulated_fees
         deferred_fee_asset_dyn_data = &deferred_paid_fee.asset_id(d).dynamic_asset_data_id(d);
         d.modify( *deferred_fee_asset_dyn_data, [&cancel_fee_amount](asset_dynamic_data_object& addo) {
            addo.accumulated_fees += cancel_fee_amount;
         });
         // cancel_fee should be no more than deferred_paid_fee
         deferred_paid_fee.amount -= cancel_fee_amount;
      }
   }

   // refund fee
   if( 0 == _order->deferred_paid_fee.amount )
   {
      // be here, order.create_time <= HARDFORK_CORE_604_TIME, or fee paid in CORE, or no fee to refund.
      // if order was created before hard fork 604,
      //    see it as fee paid in CORE, deferred_fee should be refunded to order owner but not fee pool
      d.adjust_balance( _order->seller, deferred_fee );
   }
   else // need to refund fee in originally paid asset
   {
      d.adjust_balance( _order->seller, deferred_paid_fee );
      // be here, must have: fee_asset != CORE
      if( !deferred_fee_asset_dyn_data )
         deferred_fee_asset_dyn_data = &deferred_paid_fee.asset_id(d).dynamic_asset_data_id(d);
      d.modify( *deferred_fee_asset_dyn_data, [&deferred_fee](asset_dynamic_data_object& addo) {
         addo.fee_pool += deferred_fee;
      });
   }

}

bool limit_order_update_evaluator::is_linked_tp_order_compatible(const limit_order_update_operation& o) const
{
   if( !o.on_fill ) // there is no change to on_fill, so do nothing
      return true;
   bool is_compatible = true;
   if( o.on_fill->empty() )
   {
      if( !_order->on_fill.empty() ) // This order's on_fill is being removed
      {
         // Two scenarios:
         // 1. The linked order is generated by this order, now this order's on_fill is being removed, so unlink
         // 2. This order is generated by the linked order, and "repeat" was true, now this order's on_fill is
         //      being removed, so it becomes incompatible with the linked order, so unlink
         is_compatible = false;
      }
      // else there is no change, nothing to do here
   }
   else // o.on_fill is not empty
   {
      if( _order->on_fill.empty() )
      {
         // It means this order was generated by the linked order, and the linked order's "repeat" is false.
         // We are adding on_fill to this order, so it becomes incompatible with the linked order, so unlink.
         is_compatible = false;
      }
      else // Not empty
      {
         // Two scenarios:
         // 1. Both order's "repeat" are true
         // 2. This order's "repeat" was false, and the linked order was generated by this order
         //
         // Either way, if "spread_percent" or "repeat" in on_fill changed, unlink the linked take profit order.
         const auto& old_take_profit_action = _order->get_take_profit_action();
         const auto& new_take_profit_action = o.on_fill->front().get<create_take_profit_order_action>();
         if( old_take_profit_action.spread_percent != new_take_profit_action.spread_percent
             || old_take_profit_action.repeat != new_take_profit_action.repeat )
         {
            is_compatible = false;
         }
      } // whether order's on_fill is empty (both handled)
   } // whether o.on_fill is empty (both handled)
   return is_compatible;
};

void_result limit_order_update_evaluator::do_apply(const limit_order_update_operation& o)
{ try {
   database& d = db();

   // Adjust account balance
   if( o.delta_amount_to_sell )
   {
      d.adjust_balance( o.seller, -*o.delta_amount_to_sell );
      if( o.delta_amount_to_sell->asset_id == asset_id_type() )
      {
         _seller_acc_stats = &d.get_account_stats_by_owner( o.seller );
         d.modify( *_seller_acc_stats, [&o]( account_statistics_object& bal ) {
            bal.total_core_in_orders += o.delta_amount_to_sell->amount;
         });
      }
   }

   // Process deferred fee in the order.
   process_deferred_fee();

   // Process linked take profit order
   bool unlink = false;
   if( _order->take_profit_order_id.valid() )
   {
      // If price changed, unless it is triggered by on_fill of the linked take profit order,
      //   unlink the linked take profit order.
      if( !trx_state->skip_limit_order_price_check
            && o.new_price.valid() && *o.new_price != _order->sell_price )
      {
         unlink = true;
      }
      // If on_fill changed and the order became incompatible with the linked order, unlink
      else
         unlink = !is_linked_tp_order_compatible( o );

      // Now update database
      if( unlink )
      {
         const auto& take_profit_order = (*_order->take_profit_order_id)(d);
         d.modify( take_profit_order, []( limit_order_object& loo ) {
            loo.take_profit_order_id.reset();
         });
      }
   }

   // Update order
   d.modify(*_order, [&o,this,unlink](limit_order_object& loo) {
      if (o.new_price)
         loo.sell_price = *o.new_price;
      if (o.delta_amount_to_sell)
         loo.for_sale += o.delta_amount_to_sell->amount;
      if (o.new_expiration)
         loo.expiration = *o.new_expiration;
      loo.deferred_fee = _deferred_fee;
      loo.deferred_paid_fee = _deferred_paid_fee;
      if( o.on_fill )
         loo.on_fill= *o.on_fill;
      if( unlink )
         loo.take_profit_order_id.reset();
   });

   // Perform order matching if necessary
   d.apply_order(*_order);

   return {};
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result limit_order_cancel_evaluator::do_evaluate(const limit_order_cancel_operation& o)
{ try {
   const database& d = db();

   _order = d.find( o.order );

   GRAPHENE_ASSERT( _order != nullptr,
                    limit_order_cancel_nonexist_order,
                    "Limit order ${oid} does not exist",
                    ("oid", o.order) );

   GRAPHENE_ASSERT( _order->seller == o.fee_paying_account,
                    limit_order_cancel_owner_mismatch,
                    "Limit order ${oid} is owned by someone else",
                    ("oid", o.order) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

asset limit_order_cancel_evaluator::do_apply(const limit_order_cancel_operation& o) const
{ try {
   database& d = db();

   auto base_asset = _order->sell_price.base.asset_id;
   auto quote_asset = _order->sell_price.quote.asset_id;
   auto refunded = _order->amount_for_sale();

   d.cancel_limit_order( *_order, false ); // don't create a virtual op

   if( d.get_dynamic_global_properties().next_maintenance_time <= HARDFORK_CORE_606_TIME )
   {
      // Possible optimization:
      // order can be called by canceling a limit order if the canceled order was at the top of the book.
      // Do I need to check calls in both assets?
      d.check_call_orders(base_asset(d));
      d.check_call_orders(quote_asset(d));
   }

   return refunded;
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result call_order_update_evaluator::do_evaluate(const call_order_update_operation& o)
{ try {
   const database& d = db();

   auto next_maintenance_time = d.get_dynamic_global_properties().next_maintenance_time;

   // Note: funding_account is the fee payer thus exists in the database
   _debt_asset     = &o.delta_debt.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );

   FC_ASSERT( o.delta_debt.amount <= 0 || _debt_asset->can_create_new_supply(), "Can not create new supply" );

   _dynamic_data_obj = &_debt_asset->dynamic_asset_data_id(d);

   /***
    * There are instances of assets exceeding max_supply before hf 1465, therefore this code must remain.
    */
   if (next_maintenance_time > HARDFORK_CORE_1465_TIME)
   {
      FC_ASSERT( _dynamic_data_obj->current_supply + o.delta_debt.amount <= _debt_asset->options.max_supply,
            "Borrowing this quantity would exceed MAX_SUPPLY" );
   }

   FC_ASSERT( _dynamic_data_obj->current_supply + o.delta_debt.amount >= 0,
         "This transaction would bring current supply below zero.");

   _bitasset_data  = &_debt_asset->bitasset_data(d);

   /// if there is a settlement for this asset, then no further margin positions may be taken and
   /// all existing margin positions should have been closed va database::globally_settle_asset
   FC_ASSERT( !_bitasset_data->is_globally_settled(),
              "Cannot update debt position when the asset has been globally settled" );

   FC_ASSERT( o.delta_collateral.asset_id == _bitasset_data->options.short_backing_asset,
              "Collateral asset type should be same as backing asset of debt asset" );

   auto& call_idx = d.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(o.funding_account, o.delta_debt.asset_id) );
   if( itr != call_idx.end() ) // updating or closing debt position
   {
      call_ptr = &(*itr);
      new_collateral = call_ptr->collateral + o.delta_collateral.amount;
      new_debt = call_ptr->debt + o.delta_debt.amount;
      if( new_debt == 0 )
      {
         FC_ASSERT( new_collateral == 0, "Should claim all collateral when closing debt position" );
          _closing_order = true;
      }
      else
      {
         FC_ASSERT( new_collateral > 0 && new_debt > 0,
                    "Both collateral and debt should be positive after updated a debt position if not to close it" );
      }
   }
   else // creating new debt position
   {
      FC_ASSERT( o.delta_collateral.amount > 0, "Delta collateral amount of new debt position should be positive" );
      FC_ASSERT( o.delta_debt.amount > 0, "Delta debt amount of new debt position should be positive" );
   }

   if( _bitasset_data->is_prediction_market )
      FC_ASSERT( o.delta_collateral.amount == o.delta_debt.amount,
                 "Debt amount and collateral amount should be same when updating debt position in a prediction "
                 "market" );
   else if( _bitasset_data->current_feed.settlement_price.is_null()
            && !( HARDFORK_CORE_2467_PASSED( next_maintenance_time ) && _closing_order ) )
      FC_THROW_EXCEPTION(insufficient_feeds, "Cannot borrow asset with no price feed.");

   // Since hard fork core-973, check asset authorization limitations
   if( HARDFORK_CORE_973_PASSED(d.head_block_time()) )
   {
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, *_debt_asset ),
                 "The account is not allowed to transact the debt asset" );
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _bitasset_data->options.short_backing_asset(d) ),
                 "The account is not allowed to transact the collateral asset" );
   }

   // Note: there was code here checking whether the account has enough balance to increase delta collateral,
   //       which is now removed since the check is implicitly done later by `adjust_balance()` in `do_apply()`.

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE


object_id_type call_order_update_evaluator::do_apply(const call_order_update_operation& o)
{ try {
   database& d = db();

   if( o.delta_debt.amount != 0 )
   {
      d.adjust_balance( o.funding_account, o.delta_debt );

      // Deduct the debt paid from the total supply of the debt asset.
      d.modify(*_dynamic_data_obj, [&o](asset_dynamic_data_object& dynamic_asset) {
         dynamic_asset.current_supply += o.delta_debt.amount;
      });
   }

   if( o.delta_collateral.amount != 0 )
   {
      d.adjust_balance( o.funding_account, -o.delta_collateral  );

      // Adjust the total core in orders accodingly
      if( o.delta_collateral.asset_id == asset_id_type() )
      {
         d.modify( d.get_account_stats_by_owner( o.funding_account ), [&o](account_statistics_object& stats) {
               stats.total_core_in_orders += o.delta_collateral.amount;
         });
      }
   }

   if( _closing_order ) // closing the debt position
   {
      auto call_order_id = call_ptr->id;

      d.remove( *call_ptr );

      // Update current_feed if needed
      const auto bsrm = _bitasset_data->get_black_swan_response_method();
      if( bitasset_options::black_swan_response_type::no_settlement == bsrm )
      {
         auto old_feed_price = _bitasset_data->current_feed.settlement_price;
         d.update_bitasset_current_feed( *_bitasset_data, true );
         if( !_bitasset_data->current_feed.settlement_price.is_null()
               && _bitasset_data->current_feed.settlement_price != old_feed_price )
         {
            d.check_call_orders( *_debt_asset, true, false, _bitasset_data );
         }
      }

      return call_order_id;
   }

   const auto next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_1270 = ( next_maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue

   optional<price> old_collateralization;
   optional<share_type> old_debt;

   if( !call_ptr ) // creating new debt position
   {
      call_ptr = &d.create<call_order_object>( [&o,this,before_core_hardfork_1270]( call_order_object& call ){
         call.borrower = o.funding_account;
         call.collateral = o.delta_collateral.amount;
         call.debt = o.delta_debt.amount;
         if( before_core_hardfork_1270 ) // before core-1270 hard fork, calculate call_price here and cache it
            call.call_price = price::call_price( o.delta_debt, o.delta_collateral,
                                                 _bitasset_data->current_feed.maintenance_collateral_ratio );
         else // after core-1270 hard fork, set call_price to 1
            call.call_price = price( asset( 1, o.delta_collateral.asset_id ), asset( 1, o.delta_debt.asset_id ) );
         call.target_collateral_ratio = o.extensions.value.target_collateral_ratio;
      });
   }
   else // updating existing debt position
   {
      old_collateralization = call_ptr->collateralization();
      old_debt = call_ptr->debt;

      d.modify( *call_ptr, [&o,this,before_core_hardfork_1270]( call_order_object& call ){
         call.collateral = new_collateral;
         call.debt       = new_debt;
         if( before_core_hardfork_1270 ) // don't update call_price after core-1270 hard fork
         {
            call.call_price  =  price::call_price( call.get_debt(), call.get_collateral(),
                                                   _bitasset_data->current_feed.maintenance_collateral_ratio );
         }
         call.target_collateral_ratio = o.extensions.value.target_collateral_ratio;
      });
   }

   object_id_type call_order_id = call_ptr->id;

   if( _bitasset_data->is_prediction_market )
      return call_order_id;

   // then we must check for margin calls and other issues

   // After hf core-2481, we do not allow new position's CR to be <= ~max_short_squeeze_price, because
   // * if there is no force settlement order, it would trigger a blackswan event instantly,
   // * if there is a force settlement order, they will match at the call order's CR, but it is not fair for the
   //   force settlement order.
   auto call_collateralization = call_ptr->collateralization();
   bool increasing_cr = ( old_collateralization.valid() && call_ptr->debt <= *old_debt
                                                        && call_collateralization > *old_collateralization );
   if( HARDFORK_CORE_2481_PASSED( next_maint_time ) )
   {
      // Note: if it is to increase CR and is not increasing debt amount, it is allowed,
      //       because it implies BSRM == no_settlement
      FC_ASSERT( increasing_cr
                 || call_collateralization >= ~( _bitasset_data->median_feed.max_short_squeeze_price() ),
                 "Could not create a debt position which would trigger a blackswan event instantly, "
                 "unless it is to increase collateral ratio of an existing debt position and "
                 "is not increasing its debt amount" );
   }
   // Update current_feed if needed
   const auto bsrm = _bitasset_data->get_black_swan_response_method();
   if( bitasset_options::black_swan_response_type::no_settlement == bsrm )
      d.update_bitasset_current_feed( *_bitasset_data, true );

   // check to see if the order needs to be margin called now, but don't allow black swans and require there to be
   // limit orders available that could be used to fill the order.
   // Note: due to https://github.com/bitshares/bitshares-core/issues/649, before core-343 hard fork,
   //       the first call order may be unable to be updated if the second one is undercollateralized.
   // Note: check call orders, don't allow black swan, not for new limit order
   bool called_some = d.check_call_orders( *_debt_asset, false, false, _bitasset_data );
   call_ptr = d.find<call_order_object>(call_order_id);
   if( called_some )
   {
      // before hard fork core-583: if we filled at least one call order, we are OK if we totally filled.
      // after hard fork core-583: we want to allow increasing collateral
      //   Note: increasing collateral won't get the call order itself matched (instantly margin called)
      //   if there is at least a call order get matched but didn't cause a black swan event,
      //   current order must have got matched. in this case, it's OK if it's totally filled.
      // after hard fork core-2467: when BSRM is no_settlement, it is possible that other call orders are matched
      //   in check_call_orders, also possible that increasing CR will get the call order itself matched
      if( !HARDFORK_CORE_2467_PASSED( next_maint_time ) ) // before core-2467 hf
      {
         GRAPHENE_ASSERT( !call_ptr, call_order_update_unfilled_margin_call,
                          "Updating call order would trigger a margin call that cannot be fully filled" );
      }
      // after core-2467 hf
      else
      {
         // if the call order is totally filled, it is OK,
         // if it is increasing CR, it is always ok, no matter if it or another another call order is called,
         // otherwise, the remaining call order's CR need to be > ICR
         // TODO: perhaps it makes sense to allow more cases, e.g.
         //       - when a position has ICR > CR > MCR, allow the owner to sell some collateral to increase CR
         //       - allow owners to sell collateral at price < MSSP (need to update code elsewhere)
         FC_ASSERT( !call_ptr || increasing_cr
                    || call_ptr->collateralization() > _bitasset_data->current_initial_collateralization,
                    "Could not create a debt position which would trigger a margin call instantly, "
                    "unless the debt position is fully filled, or it is to increase collateral ratio of "
                    "an existing debt position and is not increasing its debt amount, "
                    "or the remaining debt position's collateral ratio is above required "
                    "initial collateral ratio (ICR)" );
      }
   }
   else
   {
      // we know no black swan event has occurred
      FC_ASSERT( call_ptr, "no margin call was executed and yet the call object was deleted" );
      // this HF must remain as-is, as the assert inside the "if" was triggered during push_proposal()
      if( d.head_block_time() <= HARDFORK_CORE_583_TIME )
      {
         // We didn't fill any call orders.  This may be because we
         // aren't in margin call territory, or it may be because there
         // were no matching orders.  In the latter case, we throw.
         GRAPHENE_ASSERT(
            // we know core-583 hard fork is before core-1270 hard fork, it's ok to use call_price here
            ~call_ptr->call_price < _bitasset_data->current_feed.settlement_price,
            call_order_update_unfilled_margin_call,
            "Updating call order would trigger a margin call that cannot be fully filled",
            // we know core-583 hard fork is before core-1270 hard fork, it's ok to use call_price here
            ("a", ~call_ptr->call_price )("b", _bitasset_data->current_feed.settlement_price)
            );
      }
      else // after hard fork core-583, always allow call order to be updated if collateral ratio
           // is increased and debt is not increased
      {
         // We didn't fill any call orders.  This may be because we
         // aren't in margin call territory, or it may be because there
         // were no matching orders. In the latter case,
         // if collateral ratio is not increased or debt is increased, we throw.
         // be here, we know no margin call was executed,
         // so call_obj's collateral ratio should be set only by op
         // ------
         // Before BSIP77, CR of the new/updated position is required to be above MCR.
         // After BSIP77, CR of the new/updated position is required to be above max(ICR,MCR).
         // The `current_initial_collateralization` variable has been initialized according to the logic,
         // so we directly use it here.
         bool ok = increasing_cr;
         if( !ok )
            ok = before_core_hardfork_1270 ?
                         ( ~call_ptr->call_price < _bitasset_data->current_feed.settlement_price )
                       : ( call_collateralization > _bitasset_data->current_initial_collateralization );
         FC_ASSERT( ok,
            "Can only increase collateral ratio without increasing debt when the debt position's "
            "collateral ratio is lower than or equal to required initial collateral ratio (ICR), "
            "if not to trigger a margin call immediately",
            ("old_debt", old_debt)
            ("new_debt", call_ptr->debt)
            ("old_collateralization", old_collateralization)
            ("new_collateralization", call_collateralization)
            );
      }
   }

   return call_order_id;
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result bid_collateral_evaluator::do_evaluate(const bid_collateral_operation& o)
{ try {
   const database& d = db();

   // TODO cleanup: remove the assertion and related test cases after hardfork
   FC_ASSERT( d.head_block_time() > HARDFORK_CORE_216_TIME, "Not yet!" );

   // Note: bidder is the fee payer thus exists in the database
   _debt_asset     = &o.debt_covered.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );

   const fc::time_point_sec next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;
   // Note: due to old bugs, an asset can have the flag set before the hardfork, so we need the hardfork check here
   // TODO review after hardfork to see if we can remove the check
   if( HARDFORK_CORE_2281_PASSED( next_maint_time ) )
      FC_ASSERT( _debt_asset->can_bid_collateral(), "Collateral bidding is disabled for this asset" );

   _bitasset_data  = &_debt_asset->bitasset_data(d);

   FC_ASSERT( _bitasset_data->is_globally_settled(), "Cannot bid since the asset is not globally settled" );

   FC_ASSERT( o.additional_collateral.asset_id == _bitasset_data->options.short_backing_asset );

   FC_ASSERT( !_bitasset_data->is_prediction_market, "Cannot bid on a prediction market!" );

   const collateral_bid_index& bids = d.get_index_type<collateral_bid_index>();
   const auto& index = bids.indices().get<by_account>();
   const auto& bid = index.find( boost::make_tuple( o.debt_covered.asset_id, o.bidder ) );
   if( bid != index.end() )
      _bid = &(*bid);
   else
       FC_ASSERT( o.debt_covered.amount > 0, "Can't find bid to cancel?!");

   if( o.additional_collateral.amount > 0 )
   {
      auto collateral_balance = d.get_balance( o.bidder, _bitasset_data->options.short_backing_asset );
      if( _bid && d.head_block_time() >= HARDFORK_CORE_1692_TIME ) // TODO: see if HF check can be removed after HF
      {
         asset delta = o.additional_collateral - _bid->get_additional_collateral();
         FC_ASSERT( collateral_balance >= delta,
                    "Cannot increase bid from ${oc} to ${nc} collateral when payer only has ${b}",
                    ("oc", _bid->get_additional_collateral().amount)("nc", o.additional_collateral.amount)
                    ("b", collateral_balance.amount) );
      } else
         FC_ASSERT( collateral_balance >= o.additional_collateral,
                    "Cannot bid ${c} collateral when payer only has ${b}", ("c", o.additional_collateral.amount)
                    ("b", collateral_balance.amount) );
   }

   // Since hard fork core-973, check asset authorization limitations
   if( HARDFORK_CORE_973_PASSED(d.head_block_time()) )
   {
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, *_debt_asset ),
                 "The account is not allowed to transact the debt asset" );
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _bitasset_data->options.short_backing_asset(d) ),
                 "The account is not allowed to transact the collateral asset" );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE


void_result bid_collateral_evaluator::do_apply(const bid_collateral_operation& o) const
{ try {
   database& d = db();

   if( _bid )
      d.cancel_bid( *_bid, false );

   if( o.debt_covered.amount == 0 ) return void_result();

   d.adjust_balance( o.bidder, -o.additional_collateral  );

   d.create<collateral_bid_object>([&o]( collateral_bid_object& bid ) {
      bid.bidder = o.bidder;
      bid.inv_swan_price = o.additional_collateral / o.debt_covered;
   });

   // Note: CORE asset in collateral_bid_object is not counted in account_stats.total_core_in_orders

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

} } // graphene::chain
