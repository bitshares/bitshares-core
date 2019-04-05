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

#include <graphene/chain/protocol/market.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {
void_result limit_order_create_evaluator::do_evaluate(const limit_order_create_operation& op)
{ try {
   const database& d = db();

   FC_ASSERT( op.expiration >= d.head_block_time() );

   _seller        = this->fee_paying_account;
   _sell_asset    = &op.amount_to_sell.asset_id(d);
   _receive_asset = &op.min_to_receive.asset_id(d);

   if( _sell_asset->options.whitelist_markets.size() )
      FC_ASSERT( _sell_asset->options.whitelist_markets.find(_receive_asset->id) 
            != _sell_asset->options.whitelist_markets.end(),
            "This market has not been whitelisted." );
   if( _sell_asset->options.blacklist_markets.size() )
      FC_ASSERT( _sell_asset->options.blacklist_markets.find(_receive_asset->id) 
            == _sell_asset->options.blacklist_markets.end(),
            "This market has been blacklisted." );

   FC_ASSERT( is_authorized_asset( d, *_seller, *_sell_asset ) );
   FC_ASSERT( is_authorized_asset( d, *_seller, *_receive_asset ) );

   FC_ASSERT( d.get_balance( *_seller, *_sell_asset ) >= op.amount_to_sell, "insufficient balance",
              ("balance",d.get_balance(*_seller,*_sell_asset))("amount_to_sell",op.amount_to_sell) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void limit_order_create_evaluator::convert_fee()
{
   if( db().head_block_time() <= HARDFORK_CORE_604_TIME )
      generic_evaluator::convert_fee();
   else
      if( !trx_state->skip_fee )
      {
         if( fee_asset->get_id() != asset_id_type() )
         {
            db().modify(*fee_asset_dyn_data, [this](asset_dynamic_data_object& d) {
               d.fee_pool -= core_fee_paid;
            });
         }
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

object_id_type limit_order_create_evaluator::do_apply(const limit_order_create_operation& op)
{ try {
   const auto& seller_stats = _seller->statistics(db());
   db().modify(seller_stats, [&](account_statistics_object& bal) {
         if( op.amount_to_sell.asset_id == asset_id_type() )
         {
            bal.total_core_in_orders += op.amount_to_sell.amount;
         }
   });

   db().adjust_balance(op.seller, -op.amount_to_sell);

   const auto& new_order_object = db().create<limit_order_object>([&](limit_order_object& obj){
       obj.seller   = _seller->id;
       obj.for_sale = op.amount_to_sell.amount;
       obj.sell_price = op.get_price();
       obj.expiration = op.expiration;
       obj.deferred_fee = _deferred_fee;
       obj.deferred_paid_fee = _deferred_paid_fee;
   });
   limit_order_id_type order_id = new_order_object.id; // save this because we may remove the object by filling it
   bool filled;
   if( db().get_dynamic_global_properties().next_maintenance_time <= HARDFORK_CORE_625_TIME )
      filled = db().apply_order_before_hardfork_625( new_order_object );
   else
      filled = db().apply_order( new_order_object );

   FC_ASSERT( !op.fill_or_kill || filled );

   return order_id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result limit_order_cancel_evaluator::do_evaluate(const limit_order_cancel_operation& o)
{ try {
   database& d = db();

   _order = &o.order(d);
   FC_ASSERT( _order->seller == o.fee_paying_account );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

asset limit_order_cancel_evaluator::do_apply(const limit_order_cancel_operation& o)
{ try {
   database& d = db();

   auto base_asset = _order->sell_price.base.asset_id;
   auto quote_asset = _order->sell_price.quote.asset_id;
   auto refunded = _order->amount_for_sale();

   d.cancel_limit_order(*_order, false /* don't create a virtual op*/);

   if( d.get_dynamic_global_properties().next_maintenance_time <= HARDFORK_CORE_606_TIME )
   {
      // Possible optimization: order can be called by canceling a limit order iff the canceled order was at the top of the book.
      // Do I need to check calls in both assets?
      d.check_call_orders(base_asset(d));
      d.check_call_orders(quote_asset(d));
   }

   return refunded;
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result call_order_update_evaluator::do_evaluate(const call_order_update_operation& o)
{ try {
   database& d = db();

   auto next_maintenance_time = d.get_dynamic_global_properties().next_maintenance_time;

   // TODO: remove this check and the assertion after hf_834
   if( next_maintenance_time <= HARDFORK_CORE_834_TIME )
      FC_ASSERT( !o.extensions.value.target_collateral_ratio.valid(),
                 "Can not set target_collateral_ratio in call_order_update_operation before hardfork 834." );

   _paying_account = &o.funding_account(d);
   _debt_asset     = &o.delta_debt.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );

   _dynamic_data_obj = &_debt_asset->dynamic_asset_data_id(d);

   /***
    * We have softfork code already in production to prevent exceeding MAX_SUPPLY between 2018-12-21 until HF 1465.
    * But we must allow this in replays until 2018-12-21. The HF 1465 code will correct the problem.
    * After HF 1465, we MAY be able to remove the cleanup code IF it never executes. We MAY be able to clean
    * up the softfork code IF it never executes. We MAY be able to turn the hardfork code into regular code IF
    * noone ever attempted this before HF 1465.
    */
   if (next_maintenance_time <= SOFTFORK_CORE_1465_TIME)
   {
      if ( _dynamic_data_obj->current_supply + o.delta_debt.amount > _debt_asset->options.max_supply )
         ilog("Issue 1465... Borrowing and exceeding MAX_SUPPLY. Will be corrected at hardfork time.");
   }
   else
   {
      FC_ASSERT( _dynamic_data_obj->current_supply + o.delta_debt.amount <= _debt_asset->options.max_supply,
            "Borrowing this quantity would exceed MAX_SUPPLY" );
   }
   
   FC_ASSERT( _dynamic_data_obj->current_supply + o.delta_debt.amount >= 0,
         "This transaction would bring current supply below zero.");

   _bitasset_data  = &_debt_asset->bitasset_data(d);

   /// if there is a settlement for this asset, then no further margin positions may be taken and
   /// all existing margin positions should have been closed va database::globally_settle_asset
   FC_ASSERT( !_bitasset_data->has_settlement(), "Cannot update debt position when the asset has been globally settled" );

   FC_ASSERT( o.delta_collateral.asset_id == _bitasset_data->options.short_backing_asset,
              "Collateral asset type should be same as backing asset of debt asset" );

   if( _bitasset_data->is_prediction_market )
      FC_ASSERT( o.delta_collateral.amount == o.delta_debt.amount,
                 "Debt amount and collateral amount should be same when updating debt position in a prediction market" );
   else if( _bitasset_data->current_feed.settlement_price.is_null() )
      FC_THROW_EXCEPTION(insufficient_feeds, "Cannot borrow asset with no price feed.");

   // Note: there was code here checking whether the account has enough balance to increase delta collateral,
   //       which is now removed since the check is implicitly done later by `adjust_balance()` in `do_apply()`.

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


object_id_type call_order_update_evaluator::do_apply(const call_order_update_operation& o)
{ try {
   database& d = db();

   if( o.delta_debt.amount != 0 )
   {
      d.adjust_balance( o.funding_account, o.delta_debt );

      // Deduct the debt paid from the total supply of the debt asset.
      d.modify(*_dynamic_data_obj, [&](asset_dynamic_data_object& dynamic_asset) {
         dynamic_asset.current_supply += o.delta_debt.amount;
      });
   }

   if( o.delta_collateral.amount != 0 )
   {
      d.adjust_balance( o.funding_account, -o.delta_collateral  );

      // Adjust the total core in orders accodingly
      if( o.delta_collateral.asset_id == asset_id_type() )
      {
         d.modify(_paying_account->statistics(d), [&](account_statistics_object& stats) {
               stats.total_core_in_orders += o.delta_collateral.amount;
         });
      }
   }

   const auto next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_1270 = ( next_maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue

   auto& call_idx = d.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(o.funding_account, o.delta_debt.asset_id) );
   const call_order_object* call_obj = nullptr;
   call_order_id_type call_order_id;

   optional<price> old_collateralization;
   optional<share_type> old_debt;

   if( itr == call_idx.end() ) // creating new debt position
   {
      FC_ASSERT( o.delta_collateral.amount > 0, "Delta collateral amount of new debt position should be positive" );
      FC_ASSERT( o.delta_debt.amount > 0, "Delta debt amount of new debt position should be positive" );

      call_obj = &d.create<call_order_object>( [&o,this,before_core_hardfork_1270]( call_order_object& call ){
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
      call_order_id = call_obj->id;
   }
   else // updating existing debt position
   {
      call_obj = &*itr;
      auto new_collateral = call_obj->collateral + o.delta_collateral.amount;
      auto new_debt = call_obj->debt + o.delta_debt.amount;
      call_order_id = call_obj->id;

      if( new_debt == 0 )
      {
         FC_ASSERT( new_collateral == 0, "Should claim all collateral when closing debt position" );
         d.remove( *call_obj );
         return call_order_id;
      }

      FC_ASSERT( new_collateral > 0 && new_debt > 0,
                 "Both collateral and debt should be positive after updated a debt position if not to close it" );

      old_collateralization = call_obj->collateralization();
      old_debt = call_obj->debt;

      d.modify( *call_obj, [&o,new_debt,new_collateral,this,before_core_hardfork_1270]( call_order_object& call ){
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

   // then we must check for margin calls and other issues
   if( !_bitasset_data->is_prediction_market )
   {
      // check to see if the order needs to be margin called now, but don't allow black swans and require there to be
      // limit orders available that could be used to fill the order.
      // Note: due to https://github.com/bitshares/bitshares-core/issues/649, before core-343 hard fork,
      //       the first call order may be unable to be updated if the second one is undercollateralized.
      if( d.check_call_orders( *_debt_asset, false, false, _bitasset_data ) ) // don't allow black swan, not for new limit order
      {
         call_obj = d.find(call_order_id);
         // before hard fork core-583: if we filled at least one call order, we are OK if we totally filled.
         // after hard fork core-583: we want to allow increasing collateral
         //   Note: increasing collateral won't get the call order itself matched (instantly margin called)
         //   if there is at least a call order get matched but didn't cause a black swan event,
         //   current order must have got matched. in this case, it's OK if it's totally filled.
         GRAPHENE_ASSERT(
            !call_obj,
            call_order_update_unfilled_margin_call,
            "Updating call order would trigger a margin call that cannot be fully filled"
            );
      }
      else
      {
         call_obj = d.find(call_order_id);
         // we know no black swan event has occurred
         FC_ASSERT( call_obj, "no margin call was executed and yet the call object was deleted" );
         if( d.head_block_time() <= HARDFORK_CORE_583_TIME ) // TODO remove after hard fork core-583
         {
            // We didn't fill any call orders.  This may be because we
            // aren't in margin call territory, or it may be because there
            // were no matching orders.  In the latter case, we throw.
            GRAPHENE_ASSERT(
               // we know core-583 hard fork is before core-1270 hard fork, it's ok to use call_price here
               ~call_obj->call_price < _bitasset_data->current_feed.settlement_price,
               call_order_update_unfilled_margin_call,
               "Updating call order would trigger a margin call that cannot be fully filled",
               // we know core-583 hard fork is before core-1270 hard fork, it's ok to use call_price here
               ("a", ~call_obj->call_price )("b", _bitasset_data->current_feed.settlement_price)
               );
         }
         else // after hard fork, always allow call order to be updated if collateral ratio is increased and debt is not increased
         {
            // We didn't fill any call orders.  This may be because we
            // aren't in margin call territory, or it may be because there
            // were no matching orders. In the latter case,
            // if collateral ratio is not increased or debt is increased, we throw.
            // be here, we know no margin call was executed,
            // so call_obj's collateral ratio should be set only by op
            FC_ASSERT( ( !before_core_hardfork_1270
                            && call_obj->collateralization() > _bitasset_data->current_maintenance_collateralization )
                       || ( before_core_hardfork_1270 && ~call_obj->call_price < _bitasset_data->current_feed.settlement_price )
                       || ( old_collateralization.valid() && call_obj->debt <= *old_debt
                                                          && call_obj->collateralization() > *old_collateralization ),
               "Can only increase collateral ratio without increasing debt if would trigger a margin call that "
               "cannot be fully filled",
               ("old_debt", old_debt)
               ("new_debt", call_obj->debt)
               ("old_collateralization", old_collateralization)
               ("new_collateralization", call_obj->collateralization() )
               );
         }
      }
   }

   return call_order_id;
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result bid_collateral_evaluator::do_evaluate(const bid_collateral_operation& o)
{ try {
   database& d = db();

   FC_ASSERT( d.head_block_time() > HARDFORK_CORE_216_TIME, "Not yet!" );

   _paying_account = &o.bidder(d);
   _debt_asset     = &o.debt_covered.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );

   _bitasset_data  = &_debt_asset->bitasset_data(d);

   FC_ASSERT( _bitasset_data->has_settlement() );

   FC_ASSERT( o.additional_collateral.asset_id == _bitasset_data->options.short_backing_asset );

   FC_ASSERT( !_bitasset_data->is_prediction_market, "Cannot bid on a prediction market!" );

   if( o.additional_collateral.amount > 0 )
   {
      FC_ASSERT( d.get_balance(*_paying_account, _bitasset_data->options.short_backing_asset(d)) >= o.additional_collateral,
                 "Cannot bid ${c} collateral when payer only has ${b}", ("c", o.additional_collateral.amount)
                 ("b", d.get_balance(*_paying_account, o.additional_collateral.asset_id(d)).amount) );
   }

   const collateral_bid_index& bids = d.get_index_type<collateral_bid_index>();
   const auto& index = bids.indices().get<by_account>();
   const auto& bid = index.find( boost::make_tuple( o.debt_covered.asset_id, o.bidder ) );
   if( bid != index.end() )
      _bid = &(*bid);
   else
       FC_ASSERT( o.debt_covered.amount > 0, "Can't find bid to cancel?!");

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result bid_collateral_evaluator::do_apply(const bid_collateral_operation& o)
{ try {
   database& d = db();

   if( _bid )
      d.cancel_bid( *_bid, false );

   if( o.debt_covered.amount == 0 ) return void_result();

   d.adjust_balance( o.bidder, -o.additional_collateral  );

   _bid = &d.create<collateral_bid_object>([&]( collateral_bid_object& bid ) {
      bid.bidder = o.bidder;
      bid.inv_swan_price = o.additional_collateral / o.debt_covered;
   });

   // Note: CORE asset in collateral_bid_object is not counted in account_stats.total_core_in_orders

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

} } // graphene::chain
