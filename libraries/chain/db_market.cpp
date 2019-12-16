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
#include <graphene/chain/is_authorized_asset.hpp>

#include <fc/uint128.hpp>

namespace graphene { namespace chain {

namespace detail {

   share_type calculate_percent(const share_type& value, uint16_t percent)
   {
      fc::uint128_t a(value.value);
      a *= percent;
      a /= GRAPHENE_100_PERCENT;
      FC_ASSERT( a <= GRAPHENE_MAX_SHARE_SUPPLY, "overflow when calculating percent" );
      return static_cast<int64_t>(a);
   }

} //detail

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

   stored_value collateral_gathered( bitasset.options.short_backing_asset );
   stored_value debt_pool;
   modify( bitasset, [&debt_pool]( asset_bitasset_data_object& obj ){
       debt_pool = std::move(obj.total_debt);
   });

   const auto& call_price_index = get_index_type<call_order_index>().indices().get<by_price>();

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   // cancel all call orders and accumulate it into collateral_gathered
   auto call_itr = call_price_index.lower_bound( price::min( bitasset.options.short_backing_asset, mia.id ) );
   auto call_end = call_price_index.upper_bound( price::max( bitasset.options.short_backing_asset, mia.id ) );
   asset pays;
   while( call_itr != call_end )
   {
      if( before_core_hardfork_342 )
         pays = call_itr->get_debt() * settlement_price; // round down, in favor of call order
      else
         pays = call_itr->get_debt().multiply_and_round_up( settlement_price ); // round up, in favor of global settlement fund

      if( pays > call_itr->get_collateral() )
         pays = call_itr->get_collateral();
      asset receives = call_itr->get_debt();

      const auto&  order = *call_itr;
      ++call_itr;
      stored_value remaining_collateral;
      modify( order, [&collateral_gathered,&debt_pool,&pays,&remaining_collateral] ( call_order_object& call ) {
         collateral_gathered += call.collateral.split(pays.amount);
         call.debt.burn( debt_pool.split(call.debt.get_amount()) );
         remaining_collateral = std::move(call.collateral);
      });

      // Update account statistics. We know that order.collateral_type() == pays.asset_id
      if( pays.asset_id == asset_id_type() )
         modify( get_account_stats_by_owner(order.borrower), [&remaining_collateral,&pays]( account_statistics_object& b ){
            b.total_core_in_orders -= pays.amount;
            b.total_core_in_orders -= remaining_collateral.get_amount();
         });

      if( remaining_collateral.get_amount() > 0 )
         add_balance( order.borrower, std::move(remaining_collateral) );

      push_applied_operation( fill_order_operation( order.id, order.borrower, pays, receives,
                                                    asset(0, pays.asset_id), settlement_price, true ) );
      remove( order );
   }

   const auto& add = mia.dynamic_asset_data_id(*this);
   modify( bitasset, [&add,&collateral_gathered]( asset_bitasset_data_object& obj ){
      obj.settlement_price = add.current_supply.get_value() / collateral_gathered.get_value();
      obj.settlement_fund  = std::move( collateral_gathered );
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

   if( bdd.current_supply.get_amount() > 0 )
   {
      // Create + execute a "bid" with 0 additional collateral
      const collateral_bid_object& pseudo_bid = create<collateral_bid_object>([&bitasset,&bad,&bdd](collateral_bid_object& bid) {
         bid.bidder = bitasset.issuer;
         bid.collateral_offered = stored_value( bad.options.short_backing_asset );
         bid.debt_covered = asset( bdd.current_supply.get_amount(), bitasset.id );
      });
      stored_value fund;
      modify( bad, [&fund] ( asset_bitasset_data_object& _bad ) { fund = std::move(_bad.settlement_fund); });
      execute_bid( pseudo_bid, bdd.current_supply.get_amount(), std::move(fund), bad.current_feed );
   }

   _cancel_bids_and_revive_mpa( bitasset, bad );
} FC_CAPTURE_AND_RETHROW( (bitasset) ) }

void database::_cancel_bids_and_revive_mpa( const asset_object& bitasset, const asset_bitasset_data_object& bad )
{ try {
   FC_ASSERT( bitasset.is_market_issued() );
   FC_ASSERT( bad.has_settlement() );
   FC_ASSERT( !bad.is_prediction_market );
   FC_ASSERT( bad.settlement_fund.get_amount() == 0 );

   // cancel remaining bids
   const auto& bid_idx = get_index_type< collateral_bid_index >().indices().get<by_price>();
   auto itr = bid_idx.lower_bound( boost::make_tuple( bitasset.id,
                                                      price::max( bad.options.short_backing_asset, bitasset.id ),
                                                      collateral_bid_id_type() ) );
   while( itr != bid_idx.end() && itr->debt_type() == bitasset.id )
   {
      const collateral_bid_object& bid = *itr;
      ++itr;
      cancel_bid( bid );
   }

   // revive
   modify( bad, []( asset_bitasset_data_object& obj ){ obj.settlement_price = price(); });
} FC_CAPTURE_AND_RETHROW( (bitasset) ) }

void database::cancel_bid(const collateral_bid_object& bid, bool create_virtual_op)
{
   stored_value collateral;
   modify( bid, [&collateral] ( collateral_bid_object& _bid ) {
      collateral = std::move(_bid.collateral_offered);
   });
   if( create_virtual_op )
   {
      bid_collateral_operation vop;
      vop.bidder = bid.bidder;
      vop.additional_collateral = collateral.get_value();
      vop.debt_covered = asset( 0, bid.debt_type() );
      push_applied_operation( vop );
   }
   add_balance( bid.bidder, std::move(collateral) );
   remove(bid);
}

void database::execute_bid( const collateral_bid_object& bid, share_type debt_covered,
                            stored_value&& collateral_from_fund, const price_feed& current_feed )
{
   stored_value collateral;
   modify( bid, [&collateral] ( collateral_bid_object& _bid ) {
      collateral = std::move(_bid.collateral_offered);
   });
   collateral += std::move(collateral_from_fund);
   stored_debt debt( bid.debt_covered.asset_id );
   modify( debt.get_asset()(*this).bitasset_data(*this), [&debt,debt_covered] ( asset_bitasset_data_object& bad ) {
      bad.total_debt += debt.issue( debt_covered );
   });
   const auto& call_obj = create<call_order_object>(
      [&bid,&collateral,&debt,&current_feed,this] ( call_order_object& call ) {
         call.borrower = bid.bidder;
         call.collateral = std::move(collateral);
         call.debt = std::move(debt);
         // don't calculate call_price after core-1270 hard fork
         if( get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_1270_TIME )
            // bid.inv_swan_price is in collateral / debt
            call.call_price = price( asset( 1, bid.collateral_offered.get_asset() ),
                                     asset( 1, bid.debt_covered.asset_id ) );
         else
            call.call_price = price::call_price( call.debt.get_value(), call.collateral.get_value(),
                                                 current_feed.maintenance_collateral_ratio );
      });

   // Note: CORE asset in collateral_bid_object is not counted in account_stats.total_core_in_orders
   if( collateral.get_asset() == asset_id_type() )
      modify( get_account_stats_by_owner(bid.bidder), [&call_obj] ( account_statistics_object& stats ) {
         stats.total_core_in_orders += call_obj.collateral.get_amount();
      });

   push_applied_operation( execute_bid_operation( bid.bidder, call_obj.collateral.get_value(),
                                                  asset( debt_covered, bid.debt_covered.asset_id ) ) );

   remove(bid);
}

void database::cancel_settle_order(const force_settlement_object& order, bool create_virtual_op)
{
   stored_value balance;
   modify( order, [&balance] ( force_settlement_object& fso ) { balance = std::move(fso.balance); });

   if( create_virtual_op )
   {
      asset_settle_cancel_operation vop;
      vop.settlement = order.id;
      vop.account = order.owner;
      vop.amount = balance.get_value();
      push_applied_operation( vop );
   }

   add_balance( order.owner, std::move(balance) );

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
   stored_value refunded;
   stored_value deferred_fee;
   stored_value deferred_paid_fee;
   modify( order, [&refunded,&deferred_fee,&deferred_paid_fee] ( limit_order_object& loo ) {
      refunded = std::move( loo.for_sale );
      deferred_fee = std::move( loo.deferred_fee );
      deferred_paid_fee = std::move( loo.deferred_paid_fee );
   });
   const bool have_deferred_fee = deferred_paid_fee.get_amount() != 0;
   if( create_virtual_op )
   {
      vop.order = order.id;
      vop.fee_paying_account = order.seller;
      // only deduct fee if not skipping fee, and there is any fee deferred
      if( !skip_cancel_fee && deferred_fee.get_amount() > 0 )
      {
         share_type core_cancel_fee_amount = current_fee_schedule().calculate_fee( vop ).amount;
         // cap the fee
         if( core_cancel_fee_amount > deferred_fee.get_amount() )
            core_cancel_fee_amount = deferred_fee.get_amount();
         // if there is any CORE fee to deduct, redirect it to referral program
         if( core_cancel_fee_amount > 0 )
         {
            // handle originally paid fee if any:
            //    to_deduct = round_up( paid_fee * core_cancel_fee / deferred_core_fee_before_deduct )
            if( !have_deferred_fee )
            {
               vop.fee = asset( core_cancel_fee_amount );
            }
            else
            {
               fc::uint128_t fee128( deferred_paid_fee.get_amount().value );
               fee128 *= core_cancel_fee_amount.value;
               // to round up
               fee128 += deferred_fee.get_amount().value;
               fee128 -= 1;
               fee128 /= deferred_fee.get_amount().value;
               // cancel_fee should be no more than deferred_paid_fee
               stored_value cancel_fee = deferred_paid_fee.split( static_cast<int64_t>(fee128) );
               vop.fee = cancel_fee.get_value();
               // cancel_fee should be positive, pay it to asset's accumulated_fees
               fee_asset_dyn_data = &deferred_paid_fee.get_asset()(*this).dynamic_asset_data_id(*this);
               modify( *fee_asset_dyn_data, [&cancel_fee](asset_dynamic_data_object& addo) {
                  addo.accumulated_fees += std::move( cancel_fee );
               });
            }

            stored_value core_cancel_fee = deferred_fee.split( core_cancel_fee_amount );
            seller_acc_stats = &order.seller( *this ).statistics( *this );
            modify( *seller_acc_stats, [&core_cancel_fee,this]( account_statistics_object& obj ) {
               obj.pay_fee( std::move(core_cancel_fee), get_global_properties().parameters.cashback_vesting_threshold );
            } );
         }
      }
   }

   // refund funds in order
   if( refunded.get_asset() == asset_id_type() )
   {
      if( seller_acc_stats == nullptr )
         seller_acc_stats = &order.seller( *this ).statistics( *this );
      modify( *seller_acc_stats, [&refunded]( account_statistics_object& obj ) {
         obj.total_core_in_orders -= refunded.get_amount();
      });
   }
   add_balance( order.seller, std::move(refunded) );

   // refund fee
   // could be virtual op or real op here
   if( !have_deferred_fee )
   {
      // be here, order.create_time <= HARDFORK_CORE_604_TIME, or fee paid in CORE, or no fee to refund.
      // if order was created before hard fork 604 then cancelled no matter before or after hard fork 604,
      //    see it as fee paid in CORE, deferred_fee should be refunded to order owner but not fee pool
      add_balance( order.seller, std::move(deferred_fee) );
   }
   else // need to refund fee in originally paid asset
   {
      add_balance( order.seller, std::move(deferred_paid_fee) );
      // be here, must have: fee_asset != CORE
      if( deferred_fee.get_amount() > 0 )
      {
         if( fee_asset_dyn_data == nullptr )
            fee_asset_dyn_data = &deferred_paid_fee.get_asset()(*this).dynamic_asset_data_id(*this);
         modify( *fee_asset_dyn_data, [&deferred_fee](asset_dynamic_data_object& addo) {
            addo.fee_pool += std::move(deferred_fee);
         });
      }
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
      if( order.deferred_fee.get_amount() > 0 && db.head_block_time() <= HARDFORK_CORE_604_TIME )
      {
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

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_1270 = ( maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue

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
         if( before_core_hardfork_1270 )
            call_match_price = ~sell_abd->current_feed.max_short_squeeze_price_before_hf_1270();
         else
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

      if( !finished && !before_core_hardfork_1270 ) // TODO refactor or cleanup duplicate code after core-1270 hard fork
      {
         // check if there are margin calls
         const auto& call_collateral_idx = get_index_type<call_order_index>().indices().get<by_collateral>();
         auto call_min = price::min( recv_asset_id, sell_asset_id );
         while( !finished )
         {
            // hard fork core-343 and core-625 took place at same time,
            // always check call order with least collateral ratio
            auto call_itr = call_collateral_idx.lower_bound( call_min );
            if( call_itr == call_collateral_idx.end()
                  || call_itr->debt_type() != sell_asset_id
                  // feed protected https://github.com/cryptonomex/graphene/issues/436
                  || call_itr->collateralization() > sell_abd->current_maintenance_collateralization )
               break;
            // hard fork core-338 and core-625 took place at same time, not checking HARDFORK_CORE_338_TIME here.
            int match_result = match( new_order_object, *call_itr, call_match_price,
                                      sell_abd->current_feed.settlement_price,
                                      sell_abd->current_feed.maintenance_collateral_ratio,
                                      sell_abd->current_maintenance_collateralization );
            // match returns 1 or 3 when the new order was fully filled. In this case, we stop matching; otherwise keep matching.
            // since match can return 0 due to BSIP38 (hard fork core-834), we no longer only check if the result is 2.
            if( match_result == 1 || match_result == 3 )
               finished = true;
         }
      }
      else if( !finished ) // and before core-1270 hard fork
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
                                      sell_abd->current_feed.maintenance_collateral_ratio,
                                      optional<price>() );
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
   FC_ASSERT( usd.for_sale.get_amount() > 0 && core.for_sale.get_amount() > 0 );

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
   stored_value transport_core;
   modify( core, [&transport_core,&core_pays] ( limit_order_object& loo ) {
      transport_core = loo.for_sale.split( core_pays.amount );
   });
   fc::optional<stored_value> transport_usd = stored_value(usd_pays.asset_id);
   result |= fill_limit_order( usd, usd_pays, std::move(transport_core), cull_taker, match_price, transport_usd, false ); // the first param is taker
   fc::optional<stored_value> ignore;
   result |= fill_limit_order( core, core_pays, std::move(*transport_usd), true, match_price, ignore, true ) << 1; // the second param is maker
   FC_ASSERT( result != 0 );
   return result;
}

int database::match( const limit_order_object& bid, const call_order_object& ask, const price& match_price,
                     const price& feed_price, const uint16_t maintenance_collateral_ratio,
                     const optional<price>& maintenance_collateralization )
{
   FC_ASSERT( bid.sell_asset_id() == ask.debt_type() );
   FC_ASSERT( bid.receive_asset_id() == ask.collateral_type() );
   FC_ASSERT( bid.for_sale.get_amount() > 0 && ask.debt.get_amount() > 0 && ask.collateral.get_amount() > 0 );

   bool cull_taker = false;

   asset usd_for_sale = bid.amount_for_sale();
   asset usd_to_buy   = asset( ask.get_max_debt_to_cover( match_price, feed_price, 
         maintenance_collateral_ratio,  maintenance_collateralization ), ask.debt_type() );

   asset call_pays, call_receives, order_pays, order_receives;
   if( usd_to_buy > usd_for_sale )
   {  // fill limit order
      order_receives  = usd_for_sale * match_price; // round down here, in favor of call order

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      if( order_receives.amount == 0 )
         return 1;

      // The remaining amount in the limit order would be too small,
      //   so we should cull the order in fill_limit_order() below.
      // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
      //   so calling maybe_cull_small() will always cull it.
      call_receives = order_receives.multiply_and_round_up( match_price );
      cull_taker = true;
   }
   else
   {  // fill call order
      call_receives  = usd_to_buy;
      order_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up here, in favor of limit order
   }

   call_pays  = order_receives;
   order_pays = call_receives;

   int result = 0;
   stored_value transport_debt;
   modify( ask, [&transport_debt,&call_pays] ( call_order_object& coo ) {
      transport_debt = coo.collateral.split( call_pays.amount );
   });
   fc::optional<stored_value> transport_collateral = stored_value(order_pays.asset_id);
   result |= fill_limit_order( bid, order_pays, std::move(transport_debt), cull_taker, match_price, transport_collateral, false ); // the limit order is taker
   fc::optional<stored_value> ignore;
   result |= fill_call_order( ask, call_pays, std::move(*transport_collateral), match_price, ignore, true ) << 1;      // the call order is maker
   // result can be 0 when call order has target_collateral_ratio option set.

   return result;
}


asset database::match( const call_order_object& call, 
                       const force_settlement_object& settle, 
                       const price& match_price,
                       asset max_settlement,
                       const price& fill_price )
{ try {
   FC_ASSERT(call.get_debt().asset_id == settle.balance.get_asset() );
   FC_ASSERT(call.debt.get_amount() > 0 && call.collateral.get_amount() > 0 && settle.balance.get_amount() > 0);

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   auto settle_for_sale = std::min(settle.balance.get_value(), max_settlement);
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
            call_pays.amount = 1;
         }
         else
         {
            if( call_receives.amount == settle.balance.get_amount() ) // the settle order is smaller
            {
               cancel_settle_order( settle );
            }
            // else do nothing: neither order will be completely filled, perhaps due to max_settlement too small

            return asset( 0, settle.balance.get_asset() );
         }
      }

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

            if( call_receives.amount == settle.balance.get_amount() ) // the settle order will be completely filled, assuming we need to cull it
               cull_settle_order = true;
            // else do nothing, since we can't cull the settle order

            call_receives = call_pays.multiply_and_round_up( match_price ); // round up here to mitigate rounding issue (core-342).
                                                                            // It is important to understand here that the newly
                                                                            // rounded up call_receives won't be greater than the
                                                                            // old call_receives.

            if( call_receives.amount == settle.balance.get_amount() ) // the settle order will be completely filled, no need to cull
               cull_settle_order = false;
            // else do nothing, since we still need to cull the settle order or still can't cull the settle order
         }
      }
   }

   asset settle_pays     = call_receives;

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
      GRAPHENE_ASSERT( call_pays < call_collateral, black_swan_exception, "" );

      assert( settle_pays == settle_for_sale || call_receives == call.get_debt() );
   }
   // else do nothing, since black swan event won't happen, and the assertion is no longer true

   stored_value transport;
   modify( settle, [&transport,&settle_pays] ( force_settlement_object& fso ) {
      transport = fso.balance.split( settle_pays.amount );
   });
   fc::optional<stored_value> transport_debt = stored_value();
   fill_call_order( call, call_pays, std::move(transport), fill_price, transport_debt, true ); // call order is maker
   fill_settle_order( settle, settle_pays, std::move(*transport_debt), fill_price ); // force settlement order is always taker

   if( cull_settle_order )
      cancel_settle_order( settle );

   return call_receives;
} FC_CAPTURE_AND_RETHROW( (call)(settle)(match_price)(max_settlement) ) }

bool database::fill_limit_order( const limit_order_object& order, const asset& pays, stored_value&& receives,
                                 bool cull_if_small, const price& fill_price,
                                 fc::optional<stored_value>& transport, const bool is_maker )
{ try {
   const auto received = receives.get_value();

   cull_if_small |= (head_block_time() < HARDFORK_555_TIME);

   FC_ASSERT( order.amount_for_sale().asset_id == pays.asset_id );
   FC_ASSERT( pays.asset_id != received.asset_id );

   const account_object& seller = order.seller(*this);
   const asset_object& recv_asset = received.asset_id(*this);

   auto issuer_fees = pay_market_fees( &seller, recv_asset, std::move(receives) );
   add_balance( seller.get_id(), std::move(receives) );

   push_applied_operation( fill_order_operation( order.id, order.seller, pays, received, issuer_fees, fill_price, is_maker ) );

   if( pays.asset_id == asset_id_type() )
      modify( seller.statistics(*this), [&pays]( account_statistics_object& b ){
         b.total_core_in_orders -= pays.amount;
      });

   stored_value deferred_fee;
   stored_value deferred_paid_fee;
   modify( order, [&pays,&transport,&deferred_fee,&deferred_paid_fee] ( limit_order_object& loo ) {
      deferred_fee = std::move(loo.deferred_fee);
      deferred_paid_fee = std::move(loo.deferred_paid_fee);
      if( transport.valid() )
         *transport = loo.for_sale.split( pays.amount );
   });
   // conditional because cheap integer comparison may allow us to avoid two expensive modify() and object lookups
   if( deferred_fee.get_amount() > 0 )
   {
      modify( seller.statistics(*this), [&deferred_fee,this]( account_statistics_object& statistics )
      {
         statistics.pay_fee( std::move(deferred_fee), get_global_properties().parameters.cashback_vesting_threshold );
      } );
   }

   if( deferred_paid_fee.get_amount() > 0 ) // implies head_block_time() > HARDFORK_CORE_604_TIME
   {
      const auto& fee_asset_dyn_data = order.deferred_paid_fee.get_asset()(*this).dynamic_asset_data_id(*this);
      modify( fee_asset_dyn_data, [&deferred_paid_fee](asset_dynamic_data_object& addo) {
         addo.accumulated_fees += std::move(deferred_paid_fee);
      });
   }

   if( order.for_sale.get_amount() == 0 )
   {
      remove( order );
      return true;
   }

   if( cull_if_small )
      return maybe_cull_small_order( *this, order );

   return false;
} FC_CAPTURE_AND_RETHROW( (order)(pays)(receives.get_value()) ) }


bool database::fill_call_order( const call_order_object& order, const asset& pays, stored_value&& receives,
                                const price& fill_price, fc::optional<stored_value>& transport,
                                const bool is_maker )
{ try {
   const asset received = receives.get_value();

   FC_ASSERT( order.debt_type() == receives.get_asset() );
   FC_ASSERT( order.collateral_type() == pays.asset_id );
   FC_ASSERT( !transport.valid() || order.collateral.get_amount() >= pays.amount );

   // TODO pass in mia and bitasset_data for better performance
   const asset_object& mia = receives.get_asset()(*this);
   FC_ASSERT( mia.is_market_issued() );

   // update current supply
   const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);
   modify( mia_ddo, [&receives]( asset_dynamic_data_object& ao ){
      ao.current_supply.burn( std::move(receives) );
   });

   stored_value debt_to_burn;
   const auto& bdo = (*mia.bitasset_data_id)(*this);
   modify( bdo, [&received,&debt_to_burn] ( asset_bitasset_data_object& _bdo ) {
       debt_to_burn = _bdo.total_debt.split( received.amount );
   });

   stored_value collateral_freed( order.collateral_type() );
   modify( order, [&bdo,&pays,&debt_to_burn,&transport,&collateral_freed,this]( call_order_object& o ){
            o.debt.burn( std::move(debt_to_burn) );
            if( transport.valid() )
               transport = o.collateral.split( pays.amount );
            if( o.debt.get_amount() == 0 )
              collateral_freed = std::move( o.collateral );
            else
            {
               auto maint_time = get_dynamic_global_properties().next_maintenance_time;
               // update call_price after core-343 hard fork,
               // but don't update call_price after core-1270 hard fork
               if( maint_time <= HARDFORK_CORE_1270_TIME && maint_time > HARDFORK_CORE_343_TIME )
               {
                  o.call_price = price::call_price( o.get_debt(), o.get_collateral(),
                                                    bdo.current_feed.maintenance_collateral_ratio );
               }
            }
      });

   // Update account statistics. We know that order.collateral_type() == pays.asset_id
   if( pays.asset_id == asset_id_type() )
   {
      modify( get_account_stats_by_owner(order.borrower), [&collateral_freed,&pays]( account_statistics_object& b ){
         b.total_core_in_orders -= pays.amount;
         b.total_core_in_orders -= collateral_freed.get_amount();
      });
   }

   // Adjust balance
   add_balance( order.borrower, std::move(collateral_freed) );

   push_applied_operation( fill_order_operation( order.id, order.borrower, pays, received,
                                                 asset(0, pays.asset_id), fill_price, is_maker ) );

   bool filled = order.collateral.get_amount() == 0;
   if( filled )
      remove( order );

   return filled;
} FC_CAPTURE_AND_RETHROW( (order)(pays)(transport) ) }

void database::fill_settle_order( const force_settlement_object& settle, const asset& paid,
                                  stored_value&& receives, const price& fill_price )
{ try {
   const auto received = receives.get_value();

   auto issuer_fees = pay_market_fees( nullptr, get(receives.get_asset()), std::move(receives) );

   add_balance( settle.owner, std::move(receives) );

   assert( paid.asset_id != receives.get_asset() );
   push_applied_operation( fill_order_operation( settle.id, settle.owner, paid, received, issuer_fees, fill_price, false ) );

   if( settle.balance.get_amount() == 0 )
      remove(settle);
} FC_CAPTURE_AND_RETHROW( (settle)(paid)(receives) ) }

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

    bool before_core_hardfork_1270 = ( maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue

    // looking for limit orders selling the most USD for the least CORE
    auto max_price = price::max( mia.id, bitasset.options.short_backing_asset );
    // stop when limit orders are selling too little USD for too much CORE
    auto min_price = ( before_core_hardfork_1270 ? bitasset.current_feed.max_short_squeeze_price_before_hf_1270()
                                                 : bitasset.current_feed.max_short_squeeze_price() );

    // NOTE limit_price_index is sorted from greatest to least
    auto limit_itr = limit_price_index.lower_bound( max_price );
    auto limit_end = limit_price_index.upper_bound( min_price );

    if( limit_itr == limit_end )
       return false;

    const call_order_index& call_index = get_index_type<call_order_index>();
    const auto& call_price_index = call_index.indices().get<by_price>();
    const auto& call_collateral_index = call_index.indices().get<by_collateral>();

    auto call_min = price::min( bitasset.options.short_backing_asset, mia.id );
    auto call_max = price::max( bitasset.options.short_backing_asset, mia.id );

    auto call_price_itr = call_price_index.begin();
    auto call_price_end = call_price_itr;
    auto call_collateral_itr = call_collateral_index.begin();
    auto call_collateral_end = call_collateral_itr;

    if( before_core_hardfork_1270 )
    {
       call_price_itr = call_price_index.lower_bound( call_min );
       call_price_end = call_price_index.upper_bound( call_max );
    }
    else
    {
       call_collateral_itr = call_collateral_index.lower_bound( call_min );
       call_collateral_end = call_collateral_index.upper_bound( call_max );
    }

    bool filled_limit = false;
    bool margin_called = false;

    auto head_time = head_block_time();
    auto head_num = head_block_num();

    bool before_hardfork_615 = ( head_time < HARDFORK_615_TIME );
    bool after_hardfork_436 = ( head_time > HARDFORK_436_TIME );

    bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding
    bool before_core_hardfork_343 = ( maint_time <= HARDFORK_CORE_343_TIME ); // update call_price after partially filled
    bool before_core_hardfork_453 = ( maint_time <= HARDFORK_CORE_453_TIME ); // multiple matching issue
    bool before_core_hardfork_606 = ( maint_time <= HARDFORK_CORE_606_TIME ); // feed always trigger call
    bool before_core_hardfork_834 = ( maint_time <= HARDFORK_CORE_834_TIME ); // target collateral ratio option

    while( !check_for_blackswan( mia, enable_black_swan, &bitasset ) // TODO perhaps improve performance by passing in iterators
           && limit_itr != limit_end
           && ( ( !before_core_hardfork_1270 && call_collateral_itr != call_collateral_end )
              || ( before_core_hardfork_1270 && call_price_itr != call_price_end ) ) )
    {
       bool  filled_call      = false;

       const call_order_object& call_order = ( before_core_hardfork_1270 ? *call_price_itr : *call_collateral_itr );

       // Feed protected (don't call if CR>MCR) https://github.com/cryptonomex/graphene/issues/436
       if( ( !before_core_hardfork_1270 && bitasset.current_maintenance_collateralization < call_order.collateralization() )
             || ( before_core_hardfork_1270
                   && after_hardfork_436 && bitasset.current_feed.settlement_price > ~call_order.call_price ) )
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

       if( !before_core_hardfork_1270 )
       {
          usd_to_buy.amount = call_order.get_max_debt_to_cover( match_price,
                                                                bitasset.current_feed.settlement_price,
                                                                bitasset.current_feed.maintenance_collateral_ratio,
                                                                bitasset.current_maintenance_collateralization );
       }
       else if( !before_core_hardfork_834 )
       {
          usd_to_buy.amount = call_order.get_max_debt_to_cover( match_price,
                                                                bitasset.current_feed.settlement_price,
                                                                bitasset.current_feed.maintenance_collateral_ratio );
       }

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
          }
          else
             order_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up, in favor of limit order

          filled_call    = true; // this is safe, since BSIP38 (hard fork core-834) depends on BSIP31 (hard fork core-343)

          if( usd_to_buy == usd_for_sale )
             filled_limit = true;
          else if( filled_limit && maint_time <= HARDFORK_CORE_453_TIME )
          {
             //NOTE: Multiple limit match problem (see issue 453, yes this happened)
             if( before_hardfork_615 )
                _issue_453_affected_assets.insert( bitasset.asset_id );
          }
       }

       call_pays  = order_receives;
       order_pays = call_receives;

       if( filled_call && before_core_hardfork_343 )
          ++call_price_itr;
       stored_value transport;
       modify( limit_order, [&transport,&order_pays] ( limit_order_object& loo ) {
          transport = loo.for_sale.split(order_pays.amount);
       });
       // when for_new_limit_order is true, the call order is maker, otherwise the call order is taker
       fc::optional<stored_value> transport_debt = stored_value();
       fill_call_order( call_order, call_pays, std::move(transport), match_price, transport_debt,
                        for_new_limit_order );
       if( !before_core_hardfork_1270 )
          call_collateral_itr = call_collateral_index.lower_bound( call_min );
       else if( !before_core_hardfork_343 )
          call_price_itr = call_price_index.lower_bound( call_min );

       auto next_limit_itr = std::next( limit_itr );
       // when for_new_limit_order is true, the limit order is taker, otherwise the limit order is maker
       fc::optional<stored_value> ignore;
       bool really_filled = fill_limit_order( limit_order, order_pays, std::move(*transport_debt), true,
                                              match_price, ignore, !for_new_limit_order );
       if( really_filled || ( filled_limit && before_core_hardfork_453 ) )
          limit_itr = next_limit_itr;

    } // while call_itr != call_end

    return margin_called;
} FC_CAPTURE_AND_RETHROW() }

share_type database::calculate_market_fee( const asset_object& trade_asset, const share_type trade_amount )
{
   if( !trade_asset.charges_market_fees() )
      return 0;
   if( trade_asset.options.market_fee_percent == 0 )
      return 0;

   auto percent_fee = detail::calculate_percent(trade_amount, trade_asset.options.market_fee_percent);

   if( percent_fee > trade_asset.options.max_market_fee )
      percent_fee = trade_asset.options.max_market_fee;

   return percent_fee;
}

asset database::pay_market_fees(const account_object* seller, const asset_object& recv_asset, stored_value&& receives )
{
   stored_value issuer_fees = receives.split( calculate_market_fee( recv_asset, receives.get_amount() ) );
   const asset result = issuer_fees.get_value();
   //Don't dirty undo state if not actually collecting any fees
   if ( issuer_fees.get_amount() > 0 )
   {
      // calculate and pay rewards

      auto is_rewards_allowed = [&recv_asset, seller]() {
         if (seller == nullptr)
            return false;
         const auto &white_list = recv_asset.options.extensions.value.whitelist_market_fee_sharing;
         return ( !white_list || (*white_list).empty() 
               || ( (*white_list).find(seller->registrar) != (*white_list).end() ) );
      };

      if ( is_rewards_allowed() )
      {
         const auto reward_percent = recv_asset.options.extensions.value.reward_percent;
         if ( reward_percent && *reward_percent )
         {
            const auto reward_value = detail::calculate_percent( issuer_fees.get_amount(), *reward_percent );
            if ( reward_value > 0 && is_authorized_asset(*this, seller->registrar(*this), recv_asset) )
            {
               // cut referrer percent from reward
               auto registrar_reward = issuer_fees.split(reward_value);
               if( seller->referrer != seller->registrar )
               {
                  const auto referrer_rewards_value = detail::calculate_percent( registrar_reward.get_amount(),
                                                                                 seller->referrer_rewards_percentage );

                  if ( referrer_rewards_value > 0 && is_authorized_asset(*this, seller->referrer(*this), recv_asset) )
                  {
                     auto referrer_reward = registrar_reward.split( referrer_rewards_value );
                     deposit_market_fee_vesting_balance(seller->referrer, std::move(referrer_reward) );
                  }
               }
               deposit_market_fee_vesting_balance( seller->registrar, std::move(registrar_reward) );
            }
         }
      }

      const auto& recv_dyn_data = recv_asset.dynamic_asset_data_id(*this);
      modify( recv_dyn_data, [&issuer_fees]( asset_dynamic_data_object& obj ){
         obj.accumulated_fees += std::move(issuer_fees);
      });
   }

   return result;
}

} }
