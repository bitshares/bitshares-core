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

#include <graphene/chain/database.hpp>

#include <graphene/chain/account_object.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/market_object.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {

/**
 * All margin positions are force closed at the swan price
 * Collateral received goes into a force-settlement fund
 * No new margin positions can be created for this asset
 * Force settlement happens without delay at the swan price, deducting from force-settlement fund
 * No more asset updates may be issued.
*/
void database::globally_settle_asset( const asset_object& mia, const price& settlement_price )
{ try {
   const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);
   FC_ASSERT( !bitasset.has_settlement(), "black swan already occurred, it should not happen again" );

   const asset_object& backing_asset = bitasset.options.short_backing_asset(*this);
   asset collateral_gathered = backing_asset.amount(0);

   const asset_dynamic_data_object& mia_dyn = mia.dynamic_asset_data_id(*this);
   auto original_mia_supply = mia_dyn.current_supply;

   const call_order_index& call_index = get_index_type<call_order_index>();
   const auto& call_price_index = call_index.indices().get<by_price>();

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   // cancel all call orders and accumulate it into collateral_gathered
   auto call_itr = call_price_index.lower_bound( price::min( bitasset.options.short_backing_asset, mia.id ) );
   auto call_end = call_price_index.upper_bound( price::max( bitasset.options.short_backing_asset, mia.id ) );
   asset pays;
   while( call_itr != call_end )
   {
      if( before_core_hardfork_342 )
      {
         pays = call_itr->get_debt() * settlement_price; // round down, in favor of call order

         // Be here, the call order can be paying nothing
         if( pays.amount == 0 && !bitasset.is_prediction_market ) // TODO remove this warning after hard fork core-342
            wlog( "Something for nothing issue (#184, variant E) occurred at block #${block}", ("block",head_block_num()) );
      }
      else
         pays = call_itr->get_debt().multiply_and_round_up( settlement_price ); // round up, in favor of global settlement fund

      if( pays > call_itr->get_collateral() )
         pays = call_itr->get_collateral();

      collateral_gathered += pays;
      const auto&  order = *call_itr;
      ++call_itr;
      FC_ASSERT( fill_call_order( order, pays, order.get_debt(), settlement_price, true ) ); // call order is maker
   }

   modify( bitasset, [&]( asset_bitasset_data_object& obj ){
           assert( collateral_gathered.asset_id == settlement_price.quote.asset_id );
           obj.settlement_price = mia.amount(original_mia_supply) / collateral_gathered; //settlement_price;
           obj.settlement_fund  = collateral_gathered.amount;
           });

   /// After all margin positions are closed, the current supply will be reported as 0, but
   /// that is a lie, the supply didn't change.   We need to capture the current supply before
   /// filling all call orders and then restore it afterward.   Then in the force settlement
   /// evaluator reduce the supply
   modify( mia_dyn, [&]( asset_dynamic_data_object& obj ){
           obj.current_supply = original_mia_supply;
         });

} FC_CAPTURE_AND_RETHROW( (mia)(settlement_price) ) }

void database::revive_bitasset( const asset_object& bitasset )
{ try {
   FC_ASSERT( bitasset.is_market_issued() );
   const asset_bitasset_data_object& bad = bitasset.bitasset_data(*this);
   FC_ASSERT( bad.has_settlement() );
   const asset_dynamic_data_object& bdd = bitasset.dynamic_asset_data_id(*this);
   FC_ASSERT( !bad.is_prediction_market );
   FC_ASSERT( !bad.current_feed.settlement_price.is_null() );

   if( bdd.current_supply > 0 )
   {
      // Create + execute a "bid" with 0 additional collateral
      const collateral_bid_object& pseudo_bid = create<collateral_bid_object>([&](collateral_bid_object& bid) {
         bid.bidder = bitasset.issuer;
         bid.inv_swan_price = asset(0, bad.options.short_backing_asset)
                              / asset(bdd.current_supply, bitasset.id);
      });
      execute_bid( pseudo_bid, bdd.current_supply, bad.settlement_fund, bad.current_feed );
   } else
      FC_ASSERT( bad.settlement_fund == 0 );

   _cancel_bids_and_revive_mpa( bitasset, bad );
} FC_CAPTURE_AND_RETHROW( (bitasset) ) }

void database::_cancel_bids_and_revive_mpa( const asset_object& bitasset, const asset_bitasset_data_object& bad )
{ try {
   FC_ASSERT( bitasset.is_market_issued() );
   FC_ASSERT( bad.has_settlement() );
   FC_ASSERT( !bad.is_prediction_market );

   // cancel remaining bids
   const auto& bid_idx = get_index_type< collateral_bid_index >().indices().get<by_price>();
   auto itr = bid_idx.lower_bound( boost::make_tuple( bitasset.id,
                                                      price::max( bad.options.short_backing_asset, bitasset.id ),
                                                      collateral_bid_id_type() ) );
   while( itr != bid_idx.end() && itr->inv_swan_price.quote.asset_id == bitasset.id )
   {
      const collateral_bid_object& bid = *itr;
      ++itr;
      cancel_bid( bid );
   }

   // revive
   modify( bad, [&]( asset_bitasset_data_object& obj ){
              obj.settlement_price = price();
              obj.settlement_fund = 0;
           });
} FC_CAPTURE_AND_RETHROW( (bitasset) ) }

void database::cancel_bid(const collateral_bid_object& bid, bool create_virtual_op)
{
   adjust_balance(bid.bidder, bid.inv_swan_price.base);

   if( create_virtual_op )
   {
      bid_collateral_operation vop;
      vop.bidder = bid.bidder;
      vop.additional_collateral = bid.inv_swan_price.base;
      vop.debt_covered = asset( 0, bid.inv_swan_price.quote.asset_id );
      push_applied_operation( vop );
   }
   remove(bid);
}

void database::execute_bid( const collateral_bid_object& bid, share_type debt_covered, share_type collateral_from_fund,
                            const price_feed& current_feed )
{
   const call_order_object& call_obj = create<call_order_object>( [&](call_order_object& call ){
         call.borrower = bid.bidder;
         call.collateral = bid.inv_swan_price.base.amount + collateral_from_fund;
         call.debt = debt_covered;
         call.call_price = price::call_price(asset(debt_covered, bid.inv_swan_price.quote.asset_id),
                                             asset(call.collateral, bid.inv_swan_price.base.asset_id),
                                             current_feed.maintenance_collateral_ratio);
      });

   // Note: CORE asset in collateral_bid_object is not counted in account_stats.total_core_in_orders
   if( bid.inv_swan_price.base.asset_id == asset_id_type() )
      modify( get_account_stats_by_owner(bid.bidder), [&](account_statistics_object& stats) {
         stats.total_core_in_orders += call_obj.collateral;
      });

   push_applied_operation( execute_bid_operation( bid.bidder, asset( call_obj.collateral, bid.inv_swan_price.base.asset_id ),
                                                  asset( debt_covered, bid.inv_swan_price.quote.asset_id ) ) );

   remove(bid);
}

void database::cancel_settle_order(const force_settlement_object& order, bool create_virtual_op)
{
   adjust_balance(order.owner, order.balance);

   if( create_virtual_op )
   {
      asset_settle_cancel_operation vop;
      vop.settlement = order.id;
      vop.account = order.owner;
      vop.amount = order.balance;
      push_applied_operation( vop );
   }
   remove(order);
}

void database::cancel_limit_order( const limit_order_object& order, bool create_virtual_op, bool skip_cancel_fee )
{
   // if need to create a virtual op, try deduct a cancellation fee here.
   // there are two scenarios when order is cancelled and need to create a virtual op:
   // 1. due to expiration: always deduct a fee if there is any fee deferred
   // 2. due to cull_small: deduct a fee after hard fork 604, but not before (will set skip_cancel_fee)
   const account_statistics_object* seller_acc_stats = nullptr;
   const asset_dynamic_data_object* fee_asset_dyn_data = nullptr;
   limit_order_cancel_operation vop;
   share_type deferred_fee = order.deferred_fee;
   asset deferred_paid_fee = order.deferred_paid_fee;
   if( create_virtual_op )
   {
      vop.order = order.id;
      vop.fee_paying_account = order.seller;
      // only deduct fee if not skipping fee, and there is any fee deferred
      if( !skip_cancel_fee && deferred_fee > 0 )
      {
         asset core_cancel_fee = current_fee_schedule().calculate_fee( vop );
         // cap the fee
         if( core_cancel_fee.amount > deferred_fee )
            core_cancel_fee.amount = deferred_fee;
         // if there is any CORE fee to deduct, redirect it to referral program
         if( core_cancel_fee.amount > 0 )
         {
            seller_acc_stats = &order.seller( *this ).statistics( *this );
            modify( *seller_acc_stats, [&]( account_statistics_object& obj ) {
               obj.pay_fee( core_cancel_fee.amount, get_global_properties().parameters.cashback_vesting_threshold );
            } );
            deferred_fee -= core_cancel_fee.amount;
            // handle originally paid fee if any:
            //    to_deduct = round_up( paid_fee * core_cancel_fee / deferred_core_fee_before_deduct )
            if( deferred_paid_fee.amount == 0 )
            {
               vop.fee = core_cancel_fee;
            }
            else
            {
               fc::uint128 fee128( deferred_paid_fee.amount.value );
               fee128 *= core_cancel_fee.amount.value;
               // to round up
               fee128 += order.deferred_fee.value;
               fee128 -= 1;
               fee128 /= order.deferred_fee.value;
               share_type cancel_fee_amount = fee128.to_uint64();
               // cancel_fee should be positive, pay it to asset's accumulated_fees
               fee_asset_dyn_data = &deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
               modify( *fee_asset_dyn_data, [&](asset_dynamic_data_object& addo) {
                  addo.accumulated_fees += cancel_fee_amount;
               });
               // cancel_fee should be no more than deferred_paid_fee
               deferred_paid_fee.amount -= cancel_fee_amount;
               vop.fee = asset( cancel_fee_amount, deferred_paid_fee.asset_id );
            }
         }
      }
   }

   // refund funds in order
   auto refunded = order.amount_for_sale();
   if( refunded.asset_id == asset_id_type() )
   {
      if( seller_acc_stats == nullptr )
         seller_acc_stats = &order.seller( *this ).statistics( *this );
      modify( *seller_acc_stats, [&]( account_statistics_object& obj ) {
         obj.total_core_in_orders -= refunded.amount;
      });
   }
   adjust_balance(order.seller, refunded);

   // refund fee
   // could be virtual op or real op here
   if( order.deferred_paid_fee.amount == 0 )
   {
      // be here, order.create_time <= HARDFORK_CORE_604_TIME, or fee paid in CORE, or no fee to refund.
      // if order was created before hard fork 604 then cancelled no matter before or after hard fork 604,
      //    see it as fee paid in CORE, deferred_fee should be refunded to order owner but not fee pool
      adjust_balance( order.seller, deferred_fee );
   }
   else // need to refund fee in originally paid asset
   {
      adjust_balance(order.seller, deferred_paid_fee);
      // be here, must have: fee_asset != CORE
      if( fee_asset_dyn_data == nullptr )
         fee_asset_dyn_data = &deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
      modify( *fee_asset_dyn_data, [&](asset_dynamic_data_object& addo) {
         addo.fee_pool += deferred_fee;
      });
   }

   if( create_virtual_op )
      push_applied_operation( vop );

   remove(order);
}

bool maybe_cull_small_order( database& db, const limit_order_object& order )
{
   /**
    *  There are times when the AMOUNT_FOR_SALE * SALE_PRICE == 0 which means that we
    *  have hit the limit where the seller is asking for nothing in return.  When this
    *  happens we must refund any balance back to the seller, it is too small to be
    *  sold at the sale price.
    *
    *  If the order is a taker order (as opposed to a maker order), so the price is
    *  set by the counterparty, this check is deferred until the order becomes unmatched
    *  (see #555) -- however, detecting this condition is the responsibility of the caller.
    */
   if( order.amount_to_receive().amount == 0 )
   {
      if( order.deferred_fee > 0 && db.head_block_time() <= HARDFORK_CORE_604_TIME )
      { // TODO remove this warning after hard fork core-604
         wlog( "At block ${n}, cancelling order without charging a fee: ${o}", ("n",db.head_block_num())("o",order) );
         db.cancel_limit_order( order, true, true );
      }
      else
         db.cancel_limit_order( order );
      return true;
   }
   return false;
}

bool database::apply_order_before_hardfork_625(const limit_order_object& new_order_object, bool allow_black_swan)
{
   auto order_id = new_order_object.id;
   const asset_object& sell_asset = get(new_order_object.amount_for_sale().asset_id);
   const asset_object& receive_asset = get(new_order_object.amount_to_receive().asset_id);

   // Possible optimization: We only need to check calls if both are true:
   //  - The new order is at the front of the book
   //  - The new order is below the call limit price
   bool called_some = check_call_orders(sell_asset, allow_black_swan, true); // the first time when checking, call order is maker
   called_some |= check_call_orders(receive_asset, allow_black_swan, true); // the other side, same as above
   if( called_some && !find_object(order_id) ) // then we were filled by call order
      return true;

   const auto& limit_price_idx = get_index_type<limit_order_index>().indices().get<by_price>();

   // TODO: it should be possible to simply check the NEXT/PREV iterator after new_order_object to
   // determine whether or not this order has "changed the book" in a way that requires us to
   // check orders. For now I just lookup the lower bound and check for equality... this is log(n) vs
   // constant time check. Potential optimization.

   auto max_price = ~new_order_object.sell_price;
   auto limit_itr = limit_price_idx.lower_bound(max_price.max());
   auto limit_end = limit_price_idx.upper_bound(max_price);

   bool finished = false;
   while( !finished && limit_itr != limit_end )
   {
      auto old_limit_itr = limit_itr;
      ++limit_itr;
      // match returns 2 when only the old order was fully filled. In this case, we keep matching; otherwise, we stop.
      finished = (match(new_order_object, *old_limit_itr, old_limit_itr->sell_price) != 2);
   }

   //Possible optimization: only check calls if the new order completely filled some old order
   //Do I need to check both assets?
   check_call_orders(sell_asset, allow_black_swan); // after the new limit order filled some orders on the book,
                                                    // if a call order matches another order, the call order is taker
   check_call_orders(receive_asset, allow_black_swan); // the other side, same as above

   const limit_order_object* updated_order_object = find< limit_order_object >( order_id );
   if( updated_order_object == nullptr )
      return true;
   if( head_block_time() <= HARDFORK_555_TIME )
      return false;
   // before #555 we would have done maybe_cull_small_order() logic as a result of fill_order() being called by match() above
   // however after #555 we need to get rid of small orders -- #555 hardfork defers logic that was done too eagerly before, and
   // this is the point it's deferred to.
   return maybe_cull_small_order( *this, *updated_order_object );
}

bool database::apply_order(const limit_order_object& new_order_object, bool allow_black_swan)
{
   auto order_id = new_order_object.id;
   asset_id_type sell_asset_id = new_order_object.sell_asset_id();
   asset_id_type recv_asset_id = new_order_object.receive_asset_id();

   // We only need to check if the new order will match with others if it is at the front of the book
   const auto& limit_price_idx = get_index_type<limit_order_index>().indices().get<by_price>();
   auto limit_itr = limit_price_idx.lower_bound( boost::make_tuple( new_order_object.sell_price, order_id ) );
   if( limit_itr != limit_price_idx.begin() )
   {
      --limit_itr;
      if( limit_itr->sell_asset_id() == sell_asset_id && limit_itr->receive_asset_id() == recv_asset_id )
         return false;
   }

   // this is the opposite side (on the book)
   auto max_price = ~new_order_object.sell_price;
   limit_itr = limit_price_idx.lower_bound( max_price.max() );
   auto limit_end = limit_price_idx.upper_bound( max_price );

   // Order matching should be in favor of the taker.
   // When a new limit order is created, e.g. an ask, need to check if it will match the highest bid.
   // We were checking call orders first. However, due to MSSR (maximum_short_squeeze_ratio),
   // effective price of call orders may be worse than limit orders, so we should also check limit orders here.

   // Question: will a new limit order trigger a black swan event?
   //
   // 1. as of writing, it's possible due to the call-order-and-limit-order overlapping issue:
   //       https://github.com/bitshares/bitshares-core/issues/606 .
   //    when it happens, a call order can be very big but don't match with the opposite,
   //    even when price feed is too far away, further than swan price,
   //    if the new limit order is in the same direction with the call orders, it can eat up all the opposite,
   //    then the call order will lose support and trigger a black swan event.
   // 2. after issue 606 is fixed, there will be no limit order on the opposite side "supporting" the call order,
   //    so a new order in the same direction with the call order won't trigger a black swan event.
   // 3. calling is one direction. if the new limit order is on the opposite direction,
   //    no matter if matches with the call, it won't trigger a black swan event.
   //    (if a match at MSSP caused a black swan event, it means the call order is already undercollateralized,
   //      which should trigger a black swan event earlier.)
   //
   // Since it won't trigger a black swan, no need to check here.

   // currently we don't do cross-market (triangle) matching.
   // the limit order will only match with a call order if meet all of these:
   // 1. it's buying collateral, which means sell_asset is the MIA, receive_asset is the backing asset.
   // 2. sell_asset is not a prediction market
   // 3. sell_asset is not globally settled
   // 4. sell_asset has a valid price feed
   // 5. the call order's collateral ratio is below or equals to MCR
   // 6. the limit order provided a good price

   bool to_check_call_orders = false;
   const asset_object& sell_asset = sell_asset_id( *this );
   const asset_bitasset_data_object* sell_abd = nullptr;
   price call_match_price;
   if( sell_asset.is_market_issued() )
   {
      sell_abd = &sell_asset.bitasset_data( *this );
      if( sell_abd->options.short_backing_asset == recv_asset_id
          && !sell_abd->is_prediction_market
          && !sell_abd->has_settlement()
          && !sell_abd->current_feed.settlement_price.is_null() )
      {
         call_match_price = ~sell_abd->current_feed.max_short_squeeze_price();
         if( ~new_order_object.sell_price <= call_match_price ) // new limit order price is good enough to match a call
            to_check_call_orders = true;
      }
   }

   bool finished = false; // whether the new order is gone
   if( to_check_call_orders )
   {
      // check limit orders first, match the ones with better price in comparison to call orders
      while( !finished && limit_itr != limit_end && limit_itr->sell_price > call_match_price )
      {
         auto old_limit_itr = limit_itr;
         ++limit_itr;
         // match returns 2 when only the old order was fully filled. In this case, we keep matching; otherwise, we stop.
         finished = ( match( new_order_object, *old_limit_itr, old_limit_itr->sell_price ) != 2 );
      }

      if( !finished )
      {
         // check if there are margin calls
         const auto& call_price_idx = get_index_type<call_order_index>().indices().get<by_price>();
         auto call_min = price::min( recv_asset_id, sell_asset_id );
         while( !finished )
         {
            // assume hard fork core-343 and core-625 will take place at same time, always check call order with least call_price
            auto call_itr = call_price_idx.lower_bound( call_min );
            if( call_itr == call_price_idx.end()
                  || call_itr->debt_type() != sell_asset_id
                  // feed protected https://github.com/cryptonomex/graphene/issues/436
                  || call_itr->call_price > ~sell_abd->current_feed.settlement_price )
               break;
            // assume hard fork core-338 and core-625 will take place at same time, not checking HARDFORK_CORE_338_TIME here.
            int match_result = match( new_order_object, *call_itr, call_match_price,
                                      sell_abd->current_feed.settlement_price,
                                      sell_abd->current_feed.maintenance_collateral_ratio );
            // match returns 1 or 3 when the new order was fully filled. In this case, we stop matching; otherwise keep matching.
            // since match can return 0 due to BSIP38 (hard fork core-834), we no longer only check if the result is 2.
            if( match_result == 1 || match_result == 3 )
               finished = true;
         }
      }
   }

   // still need to check limit orders
   while( !finished && limit_itr != limit_end )
   {
      auto old_limit_itr = limit_itr;
      ++limit_itr;
      // match returns 2 when only the old order was fully filled. In this case, we keep matching; otherwise, we stop.
      finished = ( match( new_order_object, *old_limit_itr, old_limit_itr->sell_price ) != 2 );
   }

   const limit_order_object* updated_order_object = find< limit_order_object >( order_id );
   if( updated_order_object == nullptr )
      return true;

   // before #555 we would have done maybe_cull_small_order() logic as a result of fill_order() being called by match() above
   // however after #555 we need to get rid of small orders -- #555 hardfork defers logic that was done too eagerly before, and
   // this is the point it's deferred to.
   return maybe_cull_small_order( *this, *updated_order_object );
}

/**
 *  Matches the two orders, the first parameter is taker, the second is maker.
 *
 *  @return a bit field indicating which orders were filled (and thus removed)
 *
 *  0 - no orders were matched
 *  1 - taker was filled
 *  2 - maker was filled
 *  3 - both were filled
 */
int database::match( const limit_order_object& usd, const limit_order_object& core, const price& match_price )
{
   FC_ASSERT( usd.sell_price.quote.asset_id == core.sell_price.base.asset_id );
   FC_ASSERT( usd.sell_price.base.asset_id  == core.sell_price.quote.asset_id );
   FC_ASSERT( usd.for_sale > 0 && core.for_sale > 0 );

   auto usd_for_sale = usd.amount_for_sale();
   auto core_for_sale = core.amount_for_sale();

   asset usd_pays, usd_receives, core_pays, core_receives;

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   bool cull_taker = false;
   if( usd_for_sale <= core_for_sale * match_price ) // rounding down here should be fine
   {
      usd_receives  = usd_for_sale * match_price; // round down, in favor of bigger order

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      if( usd_receives.amount == 0 && maint_time > HARDFORK_CORE_184_TIME )
         return 1;

      if( before_core_hardfork_342 )
         core_receives = usd_for_sale;
      else
      {
         // The remaining amount in order `usd` would be too small,
         //   so we should cull the order in fill_limit_order() below.
         // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
         //   so calling maybe_cull_small() will always cull it.
         core_receives = usd_receives.multiply_and_round_up( match_price );
         cull_taker = true;
      }
   }
   else
   {
      //This line once read: assert( core_for_sale < usd_for_sale * match_price );
      //This assert is not always true -- see trade_amount_equals_zero in operation_tests.cpp
      //Although usd_for_sale is greater than core_for_sale * match_price, core_for_sale == usd_for_sale * match_price
      //Removing the assert seems to be safe -- apparently no asset is created or destroyed.

      // The maker won't be paying something for nothing, since if it would, it would have been cancelled already.
      core_receives = core_for_sale * match_price; // round down, in favor of bigger order
      if( before_core_hardfork_342 )
         usd_receives = core_for_sale;
      else
         // The remaining amount in order `core` would be too small,
         //   so the order will be culled in fill_limit_order() below
         usd_receives = core_receives.multiply_and_round_up( match_price );
   }

   core_pays = usd_receives;
   usd_pays  = core_receives;

   if( before_core_hardfork_342 )
      FC_ASSERT( usd_pays == usd.amount_for_sale() ||
                 core_pays == core.amount_for_sale() );

   int result = 0;
   result |= fill_limit_order( usd, usd_pays, usd_receives, cull_taker, match_price, false ); // the first param is taker
   result |= fill_limit_order( core, core_pays, core_receives, true, match_price, true ) << 1; // the second param is maker
   FC_ASSERT( result != 0 );
   return result;
}

int database::match( const limit_order_object& bid, const call_order_object& ask, const price& match_price,
                     const price& feed_price, const uint16_t maintenance_collateral_ratio )
{
   FC_ASSERT( bid.sell_asset_id() == ask.debt_type() );
   FC_ASSERT( bid.receive_asset_id() == ask.collateral_type() );
   FC_ASSERT( bid.for_sale > 0 && ask.debt > 0 && ask.collateral > 0 );

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   // TODO remove when we're sure it's always false
   bool before_core_hardfork_184 = ( maint_time <= HARDFORK_CORE_184_TIME ); // something-for-nothing
   // TODO remove when we're sure it's always false
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding
   // TODO remove when we're sure it's always false
   bool before_core_hardfork_834 = ( maint_time <= HARDFORK_CORE_834_TIME ); // target collateral ratio option
   if( before_core_hardfork_184 )
      ilog( "match(limit,call) is called before hardfork core-184 at block #${block}", ("block",head_block_num()) );
   if( before_core_hardfork_342 )
      ilog( "match(limit,call) is called before hardfork core-342 at block #${block}", ("block",head_block_num()) );
   if( before_core_hardfork_834 )
      ilog( "match(limit,call) is called before hardfork core-834 at block #${block}", ("block",head_block_num()) );

   bool cull_taker = false;

   asset usd_for_sale = bid.amount_for_sale();
   // TODO if we're sure `before_core_hardfork_834` is always false, remove the check
   asset usd_to_buy   = ( before_core_hardfork_834 ?
                          ask.get_debt() :
                          asset( ask.get_max_debt_to_cover( match_price, feed_price, maintenance_collateral_ratio ),
                                 ask.debt_type() ) );

   asset call_pays, call_receives, order_pays, order_receives;
   if( usd_to_buy > usd_for_sale )
   {  // fill limit order
      order_receives  = usd_for_sale * match_price; // round down here, in favor of call order

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      // TODO remove hardfork check when we're sure it's always after hard fork (but keep the zero amount check)
      if( order_receives.amount == 0 && !before_core_hardfork_184 )
         return 1;

      if( before_core_hardfork_342 ) // TODO remove this "if" when we're sure it's always false (keep the code in else)
         call_receives = usd_for_sale;
      else
      {
         // The remaining amount in the limit order would be too small,
         //   so we should cull the order in fill_limit_order() below.
         // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
         //   so calling maybe_cull_small() will always cull it.
         call_receives = order_receives.multiply_and_round_up( match_price );
         cull_taker = true;
      }
   }
   else
   {  // fill call order
      call_receives  = usd_to_buy;
      if( before_core_hardfork_342 ) // TODO remove this "if" when we're sure it's always false (keep the code in else)
      {
         order_receives = usd_to_buy * match_price; // round down here, in favor of call order
         // TODO remove hardfork check when we're sure it's always after hard fork (but keep the zero amount check)
         if( order_receives.amount == 0 && !before_core_hardfork_184 )
            return 1;
      }
      else // has hardfork core-342
         order_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up here, in favor of limit order
   }

   call_pays  = order_receives;
   order_pays = call_receives;

   int result = 0;
   result |= fill_limit_order( bid, order_pays, order_receives, cull_taker, match_price, false ); // the limit order is taker
   result |= fill_call_order( ask, call_pays, call_receives, match_price, true ) << 1;      // the call order is maker
   // result can be 0 when call order has target_collateral_ratio option set.

   return result;
}


asset database::match( const call_order_object& call, 
                       const force_settlement_object& settle, 
                       const price& match_price,
                       asset max_settlement,
                       const price& fill_price )
{ try {
   FC_ASSERT(call.get_debt().asset_id == settle.balance.asset_id );
   FC_ASSERT(call.debt > 0 && call.collateral > 0 && settle.balance.amount > 0);

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   auto settle_for_sale = std::min(settle.balance, max_settlement);
   auto call_debt = call.get_debt();

   asset call_receives   = std::min(settle_for_sale, call_debt);
   asset call_pays       = call_receives * match_price; // round down here, in favor of call order, for first check
                                                        // TODO possible optimization: check need to round up or down first

   // Be here, the call order may be paying nothing.
   bool cull_settle_order = false; // whether need to cancel dust settle order
   if( call_pays.amount == 0 )
   {
      if( maint_time > HARDFORK_CORE_184_TIME )
      {
         if( call_receives == call_debt ) // the call order is smaller than or equal to the settle order
         {
            wlog( "Something for nothing issue (#184, variant C-1) handled at block #${block}", ("block",head_block_num()) );
            call_pays.amount = 1;
         }
         else
         {
            if( call_receives == settle.balance ) // the settle order is smaller
            {
               wlog( "Something for nothing issue (#184, variant C-2) handled at block #${block}", ("block",head_block_num()) );
               cancel_settle_order( settle );
            }
            // else do nothing: neither order will be completely filled, perhaps due to max_settlement too small

            return asset( 0, settle.balance.asset_id );
         }
      }
      else // TODO remove this warning after hard fork core-184
         wlog( "Something for nothing issue (#184, variant C) occurred at block #${block}", ("block",head_block_num()) );
   }
   else // the call order is not paying nothing, but still possible it's paying more than minimum required due to rounding
   {
      if( !before_core_hardfork_342 )
      {
         if( call_receives == call_debt ) // the call order is smaller than or equal to the settle order
         {
            call_pays = call_receives.multiply_and_round_up( match_price ); // round up here, in favor of settle order
            // be here, we should have: call_pays <= call_collateral
         }
         else
         {
            // be here, call_pays has been rounded down

            // be here, we should have: call_pays <= call_collateral

            if( call_receives == settle.balance ) // the settle order will be completely filled, assuming we need to cull it
               cull_settle_order = true;
            // else do nothing, since we can't cull the settle order

            call_receives = call_pays.multiply_and_round_up( match_price ); // round up here to mitigate rounding issue (core-342).
                                                                            // It is important to understand here that the newly
                                                                            // rounded up call_receives won't be greater than the
                                                                            // old call_receives.

            if( call_receives == settle.balance ) // the settle order will be completely filled, no need to cull
               cull_settle_order = false;
            // else do nothing, since we still need to cull the settle order or still can't cull the settle order
         }
      }
   }

   asset settle_pays     = call_receives;
   asset settle_receives = call_pays;

   /**
    *  If the least collateralized call position lacks sufficient
    *  collateral to cover at the match price then this indicates a black 
    *  swan event according to the price feed, but only the market 
    *  can trigger a black swan.  So now we must cancel the forced settlement
    *  object.
    */
   if( before_core_hardfork_342 )
   {
      auto call_collateral = call.get_collateral();
      if( call_pays == call_collateral ) // TODO remove warning after hard fork core-342
         wlog( "Incorrectly captured black swan event at block #${block}", ("block",head_block_num()) );
      GRAPHENE_ASSERT( call_pays < call_collateral, black_swan_exception, "" );

      assert( settle_pays == settle_for_sale || call_receives == call.get_debt() );
   }
   // else do nothing, since black swan event won't happen, and the assertion is no longer true

   fill_call_order( call, call_pays, call_receives, fill_price, true ); // call order is maker
   fill_settle_order( settle, settle_pays, settle_receives, fill_price, false ); // force settlement order is taker

   if( cull_settle_order )
      cancel_settle_order( settle );

   return call_receives;
} FC_CAPTURE_AND_RETHROW( (call)(settle)(match_price)(max_settlement) ) }

bool database::fill_limit_order( const limit_order_object& order, const asset& pays, const asset& receives, bool cull_if_small,
                           const price& fill_price, const bool is_maker )
{ try {
   cull_if_small |= (head_block_time() < HARDFORK_555_TIME);

   FC_ASSERT( order.amount_for_sale().asset_id == pays.asset_id );
   FC_ASSERT( pays.asset_id != receives.asset_id );

   const account_object& seller = order.seller(*this);
   const asset_object& recv_asset = receives.asset_id(*this);

   auto issuer_fees = pay_market_fees( recv_asset, receives );
   pay_order( seller, receives - issuer_fees, pays );

   assert( pays.asset_id != receives.asset_id );
   push_applied_operation( fill_order_operation( order.id, order.seller, pays, receives, issuer_fees, fill_price, is_maker ) );

   // conditional because cheap integer comparison may allow us to avoid two expensive modify() and object lookups
   if( order.deferred_fee > 0 )
   {
      modify( seller.statistics(*this), [&]( account_statistics_object& statistics )
      {
         statistics.pay_fee( order.deferred_fee, get_global_properties().parameters.cashback_vesting_threshold );
      } );
   }

   if( order.deferred_paid_fee.amount > 0 ) // implies head_block_time() > HARDFORK_CORE_604_TIME
   {
      const auto& fee_asset_dyn_data = order.deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
      modify( fee_asset_dyn_data, [&](asset_dynamic_data_object& addo) {
         addo.accumulated_fees += order.deferred_paid_fee.amount;
      });
   }

   if( pays == order.amount_for_sale() )
   {
      remove( order );
      return true;
   }
   else
   {
      modify( order, [&]( limit_order_object& b ) {
                             b.for_sale -= pays.amount;
                             b.deferred_fee = 0;
                             b.deferred_paid_fee.amount = 0;
                          });
      if( cull_if_small )
         return maybe_cull_small_order( *this, order );
      return false;
   }
} FC_CAPTURE_AND_RETHROW( (order)(pays)(receives) ) }


bool database::fill_call_order( const call_order_object& order, const asset& pays, const asset& receives,
                                const price& fill_price, const bool is_maker )
{ try {
   FC_ASSERT( order.debt_type() == receives.asset_id );
   FC_ASSERT( order.collateral_type() == pays.asset_id );
   FC_ASSERT( order.collateral >= pays.amount );

   // TODO pass in mia and bitasset_data for better performance
   const asset_object& mia = receives.asset_id(*this);
   FC_ASSERT( mia.is_market_issued() );

   optional<asset> collateral_freed;
   modify( order, [&]( call_order_object& o ){
            o.debt       -= receives.amount;
            o.collateral -= pays.amount;
            if( o.debt == 0 )
            {
              collateral_freed = o.get_collateral();
              o.collateral = 0;
            }
            else if( get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_343_TIME )
              o.call_price = price::call_price( o.get_debt(), o.get_collateral(),
                                mia.bitasset_data(*this).current_feed.maintenance_collateral_ratio );
      });

   // update current supply
   const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);

   modify( mia_ddo, [&receives]( asset_dynamic_data_object& ao ){
         ao.current_supply -= receives.amount;
      });

   // Adjust balance
   if( collateral_freed.valid() )
      adjust_balance( order.borrower, *collateral_freed );

   // Update account statistics. We know that order.collateral_type() == pays.asset_id
   if( pays.asset_id == asset_id_type() )
   {
      modify( get_account_stats_by_owner(order.borrower), [&collateral_freed,&pays]( account_statistics_object& b ){
         b.total_core_in_orders -= pays.amount;
         if( collateral_freed.valid() )
            b.total_core_in_orders -= collateral_freed->amount;
      });
   }

   push_applied_operation( fill_order_operation( order.id, order.borrower, pays, receives,
                                                 asset(0, pays.asset_id), fill_price, is_maker ) );

   if( collateral_freed.valid() )
      remove( order );

   return collateral_freed.valid();
} FC_CAPTURE_AND_RETHROW( (order)(pays)(receives) ) }

bool database::fill_settle_order( const force_settlement_object& settle, const asset& pays, const asset& receives,
                                  const price& fill_price, const bool is_maker )
{ try {
   bool filled = false;

   auto issuer_fees = pay_market_fees(get(receives.asset_id), receives);

   if( pays < settle.balance )
   {
      modify(settle, [&pays](force_settlement_object& s) {
         s.balance -= pays;
      });
      filled = false;
   } else {
      filled = true;
   }
   adjust_balance(settle.owner, receives - issuer_fees);

   assert( pays.asset_id != receives.asset_id );
   push_applied_operation( fill_order_operation( settle.id, settle.owner, pays, receives, issuer_fees, fill_price, is_maker ) );

   if (filled)
      remove(settle);

   return filled;
} FC_CAPTURE_AND_RETHROW( (settle)(pays)(receives) ) }

/**
 *  Starting with the least collateralized orders, fill them if their
 *  call price is above the max(lowest bid,call_limit).
 *
 *  This method will return true if it filled a short or limit
 *
 *  @param mia - the market issued asset that should be called.
 *  @param enable_black_swan - when adjusting collateral, triggering a black swan is invalid and will throw
 *                             if enable_black_swan is not set to true.
 *  @param for_new_limit_order - true if this function is called when matching call orders with a new limit order
 *  @param bitasset_ptr - an optional pointer to the bitasset_data object of the asset
 *
 *  @return true if a margin call was executed.
 */
bool database::check_call_orders( const asset_object& mia, bool enable_black_swan, bool for_new_limit_order,
                                  const asset_bitasset_data_object* bitasset_ptr )
{ try {
    const auto& dyn_prop = get_dynamic_global_properties();
    auto maint_time = dyn_prop.next_maintenance_time;
    if( for_new_limit_order )
       FC_ASSERT( maint_time <= HARDFORK_CORE_625_TIME ); // `for_new_limit_order` is only true before HF 338 / 625

    if( !mia.is_market_issued() ) return false;

    const asset_bitasset_data_object& bitasset = ( bitasset_ptr ? *bitasset_ptr : mia.bitasset_data(*this) );

    if( check_for_blackswan( mia, enable_black_swan, &bitasset ) )
       return false;

    if( bitasset.is_prediction_market ) return false;
    if( bitasset.current_feed.settlement_price.is_null() ) return false;

    const limit_order_index& limit_index = get_index_type<limit_order_index>();
    const auto& limit_price_index = limit_index.indices().get<by_price>();

    // looking for limit orders selling the most USD for the least CORE
    auto max_price = price::max( mia.id, bitasset.options.short_backing_asset );
    // stop when limit orders are selling too little USD for too much CORE
    auto min_price = bitasset.current_feed.max_short_squeeze_price();

    assert( max_price.base.asset_id == min_price.base.asset_id );
    // NOTE limit_price_index is sorted from greatest to least
    auto limit_itr = limit_price_index.lower_bound( max_price );
    auto limit_end = limit_price_index.upper_bound( min_price );

    if( limit_itr == limit_end )
       return false;

    const call_order_index& call_index = get_index_type<call_order_index>();
    const auto& call_price_index = call_index.indices().get<by_price>();

    auto call_min = price::min( bitasset.options.short_backing_asset, mia.id );
    auto call_max = price::max( bitasset.options.short_backing_asset, mia.id );
    auto call_itr = call_price_index.lower_bound( call_min );
    auto call_end = call_price_index.upper_bound( call_max );

    bool filled_limit = false;
    bool margin_called = false;

    auto head_time = head_block_time();
    auto head_num = head_block_num();

    bool before_hardfork_615 = ( head_time < HARDFORK_615_TIME );
    bool after_hardfork_436 = ( head_time > HARDFORK_436_TIME );

    bool before_core_hardfork_184 = ( maint_time <= HARDFORK_CORE_184_TIME ); // something-for-nothing
    bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding
    bool before_core_hardfork_343 = ( maint_time <= HARDFORK_CORE_343_TIME ); // update call_price after partially filled
    bool before_core_hardfork_453 = ( maint_time <= HARDFORK_CORE_453_TIME ); // multiple matching issue
    bool before_core_hardfork_606 = ( maint_time <= HARDFORK_CORE_606_TIME ); // feed always trigger call
    bool before_core_hardfork_834 = ( maint_time <= HARDFORK_CORE_834_TIME ); // target collateral ratio option

    while( !check_for_blackswan( mia, enable_black_swan, &bitasset ) // TODO perhaps improve performance by passing in iterators
           && call_itr != call_end
           && limit_itr != limit_end )
    {
       bool  filled_call      = false;

       const call_order_object& call_order = *call_itr;

       // Feed protected (don't call if CR>MCR) https://github.com/cryptonomex/graphene/issues/436
       if( after_hardfork_436 && bitasset.current_feed.settlement_price > ~call_order.call_price )
          return margin_called;

       const limit_order_object& limit_order = *limit_itr;
       price match_price  = limit_order.sell_price;
       // There was a check `match_price.validate();` here, which is removed now because it always passes

       // Old rule: margin calls can only buy high https://github.com/bitshares/bitshares-core/issues/606
       if( before_core_hardfork_606 && match_price > ~call_order.call_price )
          return margin_called;

       margin_called = true;

       auto usd_to_buy = call_order.get_debt();
       if( usd_to_buy * match_price > call_order.get_collateral() )
       {
          elog( "black swan detected on asset ${symbol} (${id}) at block ${b}",
                ("id",mia.id)("symbol",mia.symbol)("b",head_num) );
          edump((enable_black_swan));
          FC_ASSERT( enable_black_swan );
          globally_settle_asset(mia, bitasset.current_feed.settlement_price );
          return true;
       }

       if( !before_core_hardfork_834 )
          usd_to_buy.amount = call_order.get_max_debt_to_cover( match_price,
                                                               bitasset.current_feed.settlement_price,
                                                               bitasset.current_feed.maintenance_collateral_ratio );

       asset usd_for_sale = limit_order.amount_for_sale();
       asset call_pays, call_receives, order_pays, order_receives;
       if( usd_to_buy > usd_for_sale )
       {  // fill order
          order_receives  = usd_for_sale * match_price; // round down, in favor of call order

          // Be here, the limit order won't be paying something for nothing, since if it would, it would have
          //   been cancelled elsewhere already (a maker limit order won't be paying something for nothing):
          // * after hard fork core-625, the limit order will be always a maker if entered this function;
          // * before hard fork core-625,
          //   * when the limit order is a taker, it could be paying something for nothing only when
          //     the call order is smaller and is too small
          //   * when the limit order is a maker, it won't be paying something for nothing
          if( order_receives.amount == 0 ) // TODO this should not happen. remove the warning after confirmed
          {
             if( before_core_hardfork_184 )
                wlog( "Something for nothing issue (#184, variant D-1) occurred at block #${block}", ("block",head_num) );
             else
                wlog( "Something for nothing issue (#184, variant D-2) occurred at block #${block}", ("block",head_num) );
          }

          if( before_core_hardfork_342 )
             call_receives = usd_for_sale;
          else
             // The remaining amount in the limit order would be too small,
             //   so we should cull the order in fill_limit_order() below.
             // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
             //   so calling maybe_cull_small() will always cull it.
             call_receives = order_receives.multiply_and_round_up( match_price );

          filled_limit = true;

       } else { // fill call
          call_receives  = usd_to_buy;

          if( before_core_hardfork_342 )
          {
             order_receives = usd_to_buy * match_price; // round down, in favor of call order

             // Be here, the limit order would be paying something for nothing
             if( order_receives.amount == 0 ) // TODO remove warning after hard fork core-342
                wlog( "Something for nothing issue (#184, variant D) occurred at block #${block}", ("block",head_num) );
          }
          else
             order_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up, in favor of limit order

          filled_call    = true; // this is safe, since BSIP38 (hard fork core-834) depends on BSIP31 (hard fork core-343)

          if( usd_to_buy == usd_for_sale )
             filled_limit = true;
          else if( filled_limit && maint_time <= HARDFORK_CORE_453_TIME ) // TODO remove warning after hard fork core-453
          {
             wlog( "Multiple limit match problem (issue 453) occurred at block #${block}", ("block",head_num) );
             if( before_hardfork_615 )
                _issue_453_affected_assets.insert( bitasset.asset_id );
          }
       }

       call_pays  = order_receives;
       order_pays = call_receives;

       if( filled_call && before_core_hardfork_343 )
          ++call_itr;
       // when for_new_limit_order is true, the call order is maker, otherwise the call order is taker
       fill_call_order( call_order, call_pays, call_receives, match_price, for_new_limit_order );
       if( !before_core_hardfork_343 )
          call_itr = call_price_index.lower_bound( call_min );

       auto next_limit_itr = std::next( limit_itr );
       // when for_new_limit_order is true, the limit order is taker, otherwise the limit order is maker
       bool really_filled = fill_limit_order( limit_order, order_pays, order_receives, true, match_price, !for_new_limit_order );
       if( really_filled || ( filled_limit && before_core_hardfork_453 ) )
          limit_itr = next_limit_itr;

    } // while call_itr != call_end

    return margin_called;
} FC_CAPTURE_AND_RETHROW() }

void database::pay_order( const account_object& receiver, const asset& receives, const asset& pays )
{
   const auto& balances = receiver.statistics(*this);
   modify( balances, [&]( account_statistics_object& b ){
         if( pays.asset_id == asset_id_type() )
         {
            b.total_core_in_orders -= pays.amount;
         }
   });
   adjust_balance(receiver.get_id(), receives);
}

asset database::calculate_market_fee( const asset_object& trade_asset, const asset& trade_amount )
{
   assert( trade_asset.id == trade_amount.asset_id );

   if( !trade_asset.charges_market_fees() )
      return trade_asset.amount(0);
   if( trade_asset.options.market_fee_percent == 0 )
      return trade_asset.amount(0);

   fc::uint128 a(trade_amount.amount.value);
   a *= trade_asset.options.market_fee_percent;
   a /= GRAPHENE_100_PERCENT;
   asset percent_fee = trade_asset.amount(a.to_uint64());

   if( percent_fee.amount > trade_asset.options.max_market_fee )
      percent_fee.amount = trade_asset.options.max_market_fee;

   return percent_fee;
}

asset database::pay_market_fees( const asset_object& recv_asset, const asset& receives )
{
   auto issuer_fees = calculate_market_fee( recv_asset, receives );
   assert(issuer_fees <= receives );

   //Don't dirty undo state if not actually collecting any fees
   if( issuer_fees.amount > 0 )
   {
      const auto& recv_dyn_data = recv_asset.dynamic_asset_data_id(*this);
      modify( recv_dyn_data, [&]( asset_dynamic_data_object& obj ){
                   //idump((issuer_fees));
         obj.accumulated_fees += issuer_fees.amount;
      });
   }

   return issuer_fees;
}

} }
