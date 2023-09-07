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

bool database::check_for_blackswan( const asset_object& mia, bool enable_black_swan,
                                    const asset_bitasset_data_object* bitasset_ptr )
{
    if( !mia.is_market_issued() ) return false;

    const asset_bitasset_data_object& bitasset = bitasset_ptr ? *bitasset_ptr : mia.bitasset_data(*this);
    if( bitasset.is_globally_settled() ) return true; // already globally settled
    auto settle_price = bitasset.current_feed.settlement_price;
    if( settle_price.is_null() ) return false; // no feed

    asset_id_type debt_asset_id = bitasset.asset_id;

    auto maint_time = get_dynamic_global_properties().next_maintenance_time;
    bool before_core_hardfork_1270 = ( maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue
    bool after_core_hardfork_2481 = HARDFORK_CORE_2481_PASSED( maint_time ); // Match settle orders with margin calls

    // After core-2481 hard fork, if there are force-settlements, match call orders with them first
    if( after_core_hardfork_2481 )
    {
      const auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
      auto lower_itr = settlement_index.lower_bound( debt_asset_id );
      if( lower_itr != settlement_index.end() && lower_itr->balance.asset_id == debt_asset_id )
         return false;
    }

    // Find the call order with the least collateral ratio
    const call_order_object* call_ptr = find_least_collateralized_short( bitasset, false );
    if( !call_ptr ) // no call order
       return false;

    const limit_order_index& limit_index = get_index_type<limit_order_index>();
    const auto& limit_price_index = limit_index.indices().get<by_price>();

    // looking for limit orders selling the most USD for the least CORE
    auto highest_possible_bid = price::max( debt_asset_id, bitasset.options.short_backing_asset );
    // stop when limit orders are selling too little USD for too much CORE
    auto lowest_possible_bid  = price::min( debt_asset_id, bitasset.options.short_backing_asset );

    FC_ASSERT( highest_possible_bid.base.asset_id == lowest_possible_bid.base.asset_id );
    // NOTE limit_price_index is sorted from greatest to least
    auto limit_itr = limit_price_index.lower_bound( highest_possible_bid );
    auto limit_end = limit_price_index.upper_bound( lowest_possible_bid );

    price call_pays_price;
    if( limit_itr != limit_end )
    {
       call_pays_price = limit_itr->sell_price;
       if( after_core_hardfork_2481 )
       {
          // due to margin call fee, we check with MCPP (margin call pays price) here
          call_pays_price = call_pays_price * bitasset.get_margin_call_pays_ratio();
       }
    }

    using bsrm_type = bitasset_options::black_swan_response_type;
    const auto bsrm = bitasset.get_black_swan_response_method();

    // when BSRM is individual settlement, we loop multiple times
    bool settled_some = false;
    while( true )
    {
       settle_price = bitasset.current_feed.settlement_price;
       price highest = settle_price;
       // Due to #338, we won't check for black swan on incoming limit order, so need to check with MSSP here
       // * If BSRM is individual_settlement_to_fund, check with median_feed to decide whether to settle.
       // * If BSRM is no_settlement, check with current_feed to NOT trigger global settlement.
       // * If BSRM is global_settlement or individual_settlement_to_order, median_feed == current_feed.
       if( bsrm_type::individual_settlement_to_fund == bsrm )
          highest = bitasset.median_feed.max_short_squeeze_price();
       else if( !before_core_hardfork_1270 )
          highest = bitasset.current_feed.max_short_squeeze_price();
       else if( maint_time > HARDFORK_CORE_338_TIME )
          highest = bitasset.current_feed.max_short_squeeze_price_before_hf_1270();
       // else do nothing

       if( limit_itr != limit_end )
       {
          FC_ASSERT( highest.base.asset_id == limit_itr->sell_price.base.asset_id );
          if( bsrm_type::individual_settlement_to_fund != bsrm )
             highest = std::max( call_pays_price, highest );
          // for individual_settlement_to_fund, if call_pays_price < current_feed.max_short_squeeze_price(),
          // we don't match the least collateralized short with the limit order
          //    even if call_pays_price >= median_feed.max_short_squeeze_price()
          else if( call_pays_price >= bitasset.current_feed.max_short_squeeze_price() )
             highest = call_pays_price;
          // else highest is median_feed.max_short_squeeze_price()
       }

       // The variable `highest` after hf_338:
       // * if no limit order, it is expected to be the black swan price; if the call order with the least CR
       //   has CR below or equal to the black swan price, we trigger GS,
       // * if there exists at least one limit order and the price is higher, we use the limit order's price,
       //   which means we will match the margin call orders with the limit order first.
       //
       // However, there was a bug: after hf_bsip74 and before hf_2481, margin call fee was not considered
       // when calculating highest, which means some blackswans weren't got caught here.  Fortunately they got
       // caught by an additional check in check_call_orders().
       // This bug is fixed in hf_2481. Actually, after hf_2481,
       // * if there is a force settlement, we totally rely on the additional checks in check_call_orders(),
       // * if there is no force settlement, we check here with margin call fee in consideration.

       auto least_collateral = call_ptr->collateralization();
       // Note: strictly speaking, even when the call order's collateralization is lower than ~highest,
       //       if the matching limit order is smaller, due to rounding, it is still possible that the
       //       call order's collateralization would increase and become higher than ~highest after matched.
       //       However, for simplicity, we only compare the prices here.
       bool is_blackswan = after_core_hardfork_2481 ? ( ~least_collateral > highest )
                                                    : ( ~least_collateral >= highest );
       if( !is_blackswan )
          return settled_some;

       wdump( (*call_ptr) );
       elog( "Black Swan detected on asset ${symbol} (${id}) at block ${b}: \n"
             "   Least collateralized call: ${lc}  ${~lc}\n"
           //  "   Highest Bid:               ${hb}  ${~hb}\n"
             "   Settle Price:              ${~sp}  ${sp}\n"
             "   Max:                       ${~h}  ${h}\n",
            ("id",mia.id)("symbol",mia.symbol)("b",head_block_num())
            ("lc",least_collateral.to_real())("~lc",(~least_collateral).to_real())
          //  ("hb",limit_itr->sell_price.to_real())("~hb",(~limit_itr->sell_price).to_real())
            ("sp",settle_price.to_real())("~sp",(~settle_price).to_real())
            ("h",highest.to_real())("~h",(~highest).to_real()) );
       edump((enable_black_swan));
       FC_ASSERT( enable_black_swan,
                  "Black swan was detected during a margin update which is not allowed to trigger a blackswan" );

       if( bsrm_type::individual_settlement_to_fund == bsrm || bsrm_type::individual_settlement_to_order == bsrm )
       {
          individually_settle( bitasset, *call_ptr );
          call_ptr = find_least_collateralized_short( bitasset, true );
          if( !call_ptr ) // no call order
             return true;
          settled_some = true;
          continue;
       }
       // Global settlement or no settlement, but we should not be here if BSRM is no_settlement
       else if( after_core_hardfork_2481 )
       {
          if( bsrm_type::no_settlement == bsrm ) // this should not happen, be defensive here
             wlog( "Internal error: BSRM is no_settlement but undercollateralization occurred" );
          // After hf_2481, when a global settlement occurs,
          // * the margin calls (whose CR <= MCR) pay a premium (by MSSR-MCFR) and a margin call fee (by MCFR), and
          //   they are closed at the same price,
          // * the debt positions with CR > MCR do not pay premium or margin call fee, and they are closed at a same
          //   price too.
          // * The GS price would close the position with the least CR with no collateral left for the owner,
          //   but would close other positions with some collateral left (if any) for their owners.
          // * Both the premium and the margin call fee paid by the margin calls go to the asset owner, none will go
          //   to the global settlement fund, because
          //   - if a part of the premium or fees goes to the global settlement fund, it means there would be a
          //     difference in settlement prices, so traders are incentivized to create new debt in the last minute
          //     then settle after GS to earn free money,
          //   - if no premium or fees goes to the global settlement fund, it means debt asset holders would only
          //     settle for less after GS, so they are incentivized to settle before GS which helps avoid GS.
          globally_settle_asset(mia, ~least_collateral, true );
       }
       else if( maint_time > HARDFORK_CORE_338_TIME && ~least_collateral <= settle_price )
          // global settle at feed price if possible
          globally_settle_asset(mia, settle_price );
       else
          globally_settle_asset(mia, ~least_collateral );
       return true;
    }
}

/**
 * All margin positions are force closed at the swan price
 * Collateral received goes into a force-settlement fund
 * No new margin positions can be created for this asset
 * Force settlement happens without delay at the swan price, deducting from force-settlement fund
*/
void database::globally_settle_asset( const asset_object& mia, const price& settlement_price,
                                      bool check_margin_calls )
{
   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_1669 = ( maint_time <= HARDFORK_CORE_1669_TIME ); // whether to use call_price

   if( before_core_hardfork_1669 )
   {
      globally_settle_asset_impl( mia, settlement_price,
                                  get_index_type<call_order_index>().indices().get<by_price>(),
                                  check_margin_calls );
   }
   else
   {
      // Note: it is safe to iterate here even if there is no call order due to individual settlements
      globally_settle_asset_impl( mia, settlement_price,
                                  get_index_type<call_order_index>().indices().get<by_collateral>(),
                                  check_margin_calls );
   }
}

template<typename IndexType>
void database::globally_settle_asset_impl( const asset_object& mia,
                                           const price& settlement_price,
                                           const IndexType& call_index,
                                           bool check_margin_calls )
{ try {
   const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);
   // GCOVR_EXCL_START
   // Defensive code, normally it should not fail
   FC_ASSERT( !bitasset.is_globally_settled(), "black swan already occurred, it should not happen again" );
   // GCOVR_EXCL_STOP

   asset collateral_gathered( 0, bitasset.options.short_backing_asset );

   const asset_dynamic_data_object& mia_dyn = mia.dynamic_asset_data_id(*this);
   auto original_mia_supply = mia_dyn.current_supply;

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   // cancel all call orders and accumulate it into collateral_gathered
   auto call_itr = call_index.lower_bound( price::min( bitasset.options.short_backing_asset, bitasset.asset_id ) );
   auto call_end = call_index.upper_bound( price::max( bitasset.options.short_backing_asset, bitasset.asset_id ) );

   auto margin_end = call_end;
   bool is_margin_call = false;
   price call_pays_price = settlement_price;
   price fund_receives_price = settlement_price;
   if( check_margin_calls )
   {
      margin_end = call_index.upper_bound( bitasset.current_maintenance_collateralization );
      // Note: settlement_price is in debt / collateral, here the fund gets less collateral
      fund_receives_price = settlement_price * ratio_type( bitasset.current_feed.maximum_short_squeeze_ratio,
                                                           GRAPHENE_COLLATERAL_RATIO_DENOM );
      if( call_itr != margin_end )
         is_margin_call = true;
   }
   asset margin_call_fee( 0, bitasset.options.short_backing_asset );

   asset pays;
   while( call_itr != call_end )
   {
      if( is_margin_call && call_itr == margin_end )
      {
         is_margin_call = false;
         call_pays_price = fund_receives_price;
      }

      const call_order_object& order = *call_itr;
      ++call_itr;

      auto order_debt = order.get_debt();
      if( before_core_hardfork_342 )
         pays = order_debt * call_pays_price; // round down, in favor of call order
      else
         pays = order_debt.multiply_and_round_up( call_pays_price ); // round up in favor of global-settle fund

      if( pays > order.get_collateral() )
         pays = order.get_collateral();

      if( is_margin_call )
      {
         auto fund_receives = order_debt.multiply_and_round_up( fund_receives_price );
         if( fund_receives > pays )
            fund_receives = pays;
         margin_call_fee = pays - fund_receives;
         collateral_gathered += fund_receives;
      }
      else
      {
         margin_call_fee.amount = 0;
         collateral_gathered += pays;
      }

      // call order is maker
      FC_ASSERT( fill_call_order( order, pays, order_debt, fund_receives_price, true, margin_call_fee, false ),
                 "Internal error: unable to close margin call ${o}", ("o", order) );
   }

   // Remove the individual settlement order
   const limit_order_object* limit_ptr = find_settled_debt_order( bitasset.asset_id );
   if( limit_ptr )
      remove( *limit_ptr );

   // Move individual settlement fund to the GS fund
   collateral_gathered.amount += bitasset.individual_settlement_fund;

   modify( bitasset, [&mia,&original_mia_supply,&collateral_gathered]( asset_bitasset_data_object& obj ){
      obj.options.extensions.value.black_swan_response_method.reset(); // Update BSRM to GS
      obj.current_feed = obj.median_feed; // reset current feed price if was capped
      obj.individual_settlement_debt = 0;
      obj.individual_settlement_fund = 0;
      obj.settlement_price = mia.amount(original_mia_supply) / collateral_gathered;
      obj.settlement_fund  = collateral_gathered.amount;
   });

} FC_CAPTURE_AND_RETHROW( (mia)(settlement_price) ) } // GCOVR_EXCL_LINE

void database::individually_settle( const asset_bitasset_data_object& bitasset, const call_order_object& order )
{
   FC_ASSERT( bitasset.asset_id == order.debt_type(), "Internal error: asset type mismatch" );

   using bsrm_type = bitasset_options::black_swan_response_type;
   const auto bsrm = bitasset.get_black_swan_response_method();
   FC_ASSERT( bsrm_type::individual_settlement_to_fund == bsrm || bsrm_type::individual_settlement_to_order == bsrm,
              "Internal error: Invalid BSRM" );

   auto order_debt = order.get_debt();
   auto order_collateral = order.get_collateral();
   auto fund_receives_price = (~order.collateralization()) / bitasset.get_margin_call_pays_ratio();
   auto fund_receives = order_debt.multiply_and_round_up( fund_receives_price );
   if( fund_receives.amount > order.collateral ) // should not happen, just be defensive
      fund_receives.amount = order.collateral;

   auto margin_call_fee = order_collateral - fund_receives;

   modify( bitasset, [&order,&fund_receives]( asset_bitasset_data_object& obj ){
      obj.individual_settlement_debt += order.debt;
      obj.individual_settlement_fund += fund_receives.amount;
   });

   if( bsrm_type::individual_settlement_to_order == bsrm ) // settle to order
   {
      const auto& head_time = head_block_time();
      bool after_core_hardfork_2591 = HARDFORK_CORE_2591_PASSED( head_time ); // Tighter peg (fill debt order at MCOP)

      const limit_order_object* limit_ptr = find_settled_debt_order( bitasset.asset_id );
      if( limit_ptr )
      {
         modify( *limit_ptr, [after_core_hardfork_2591,&bitasset]( limit_order_object& obj ) {
            // TODO fix duplicate code
            bool sell_all = true;
            if( after_core_hardfork_2591 )
            {
               obj.sell_price = ~bitasset.get_margin_call_order_price();
               asset settled_debt( bitasset.individual_settlement_debt, obj.receive_asset_id() );
               try
               {
                  obj.for_sale = settled_debt.multiply_and_round_up( obj.sell_price ).amount; // may overflow
                  // Note: the "=" below is for the consistency of order matching logic
                  if( obj.for_sale <= bitasset.individual_settlement_fund )
                     sell_all = false;
               }
               catch( fc::exception& e ) // catch the overflow
               {
                  // do nothing
                  dlog( e.to_detail_string() );
               }
            }
            if( sell_all )
            {
               obj.for_sale = bitasset.individual_settlement_fund;
               obj.sell_price = ~bitasset.get_individual_settlement_price();
            }
         } );
      }
      else
      {
         create< limit_order_object >( [&order_debt,&fund_receives]( limit_order_object& obj ) {
            obj.expiration = time_point_sec::maximum();
            obj.seller = GRAPHENE_NULL_ACCOUNT;
            obj.for_sale = fund_receives.amount;
            obj.sell_price = fund_receives / order_debt;
            obj.is_settled_debt = true;
         } );
      }
      // Note: CORE asset in settled debt is not counted in account_stats.total_core_in_orders
   }

   // call order is maker
   FC_ASSERT( fill_call_order( order, order_collateral, order_debt,
                               fund_receives_price, true, margin_call_fee, false ),
              "Internal error: unable to close margin call ${o}", ("o", order) );

   // Update current feed if needed
   if( bsrm_type::individual_settlement_to_fund == bsrm )
      update_bitasset_current_feed( bitasset, true );

}

void database::revive_bitasset( const asset_object& bitasset, const asset_bitasset_data_object& bad )
{ try {
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( bitasset.is_market_issued() );
   FC_ASSERT( bitasset.id == bad.asset_id );
   FC_ASSERT( bad.is_globally_settled() );
   FC_ASSERT( !bad.is_prediction_market );
   FC_ASSERT( !bad.current_feed.settlement_price.is_null() );
   // GCOVR_EXCL_STOP

   const asset_dynamic_data_object& bdd = bitasset.dynamic_asset_data_id(*this);
   if( bdd.current_supply > 0 )
   {
      // Create + execute a "bid" with 0 additional collateral
      const collateral_bid_object& pseudo_bid = create<collateral_bid_object>(
               [&bitasset,&bad,&bdd](collateral_bid_object& bid) {
         bid.bidder = bitasset.issuer;
         bid.inv_swan_price = asset(0, bad.options.short_backing_asset)
                              / asset(bdd.current_supply, bad.asset_id);
      });
      execute_bid( pseudo_bid, bdd.current_supply, bad.settlement_fund, bad.current_feed );
   } else
      FC_ASSERT( bad.settlement_fund == 0 );

   _cancel_bids_and_revive_mpa( bitasset, bad );
} FC_CAPTURE_AND_RETHROW( (bitasset) ) } // GCOVR_EXCL_LINE

void database::_cancel_bids_and_revive_mpa( const asset_object& bitasset, const asset_bitasset_data_object& bad )
{ try {
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( bitasset.is_market_issued() );
   FC_ASSERT( bad.is_globally_settled() );
   FC_ASSERT( !bad.is_prediction_market );
   // GCOVR_EXCL_STOP

   // cancel remaining bids
   const auto& bid_idx = get_index_type< collateral_bid_index >().indices().get<by_price>();
   auto itr = bid_idx.lower_bound( bad.asset_id );
   const auto end = bid_idx.upper_bound( bad.asset_id );
   while( itr != end )
   {
      const collateral_bid_object& bid = *itr;
      ++itr;
      cancel_bid( bid );
   }

   // revive
   modify( bad, []( asset_bitasset_data_object& obj ){
              obj.settlement_price = price();
              obj.settlement_fund = 0;
           });
} FC_CAPTURE_AND_RETHROW( (bitasset) ) } // GCOVR_EXCL_LINE

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

void database::execute_bid( const collateral_bid_object& bid, share_type debt_covered,
                            share_type collateral_from_fund, const price_feed& current_feed )
{
   const call_order_object& call_obj = create<call_order_object>(
               [&bid, &debt_covered, &collateral_from_fund, &current_feed, this](call_order_object& call ){
         call.borrower = bid.bidder;
         call.collateral = bid.inv_swan_price.base.amount + collateral_from_fund;
         call.debt = debt_covered;
         // don't calculate call_price after core-1270 hard fork
         if( get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_1270_TIME )
            // bid.inv_swan_price is in collateral / debt
            call.call_price = price( asset( 1, bid.inv_swan_price.base.asset_id ),
                                     asset( 1, bid.inv_swan_price.quote.asset_id ) );
         else
            call.call_price = price::call_price( asset(debt_covered, bid.inv_swan_price.quote.asset_id),
                                                 asset(call.collateral, bid.inv_swan_price.base.asset_id),
                                                 current_feed.maintenance_collateral_ratio );
      });

   // Note: CORE asset in collateral_bid_object is not counted in account_stats.total_core_in_orders
   if( bid.inv_swan_price.base.asset_id == asset_id_type() )
      modify( get_account_stats_by_owner(bid.bidder), [&call_obj](account_statistics_object& stats) {
         stats.total_core_in_orders += call_obj.collateral;
      });

   push_applied_operation( execute_bid_operation( bid.bidder,
                                                  asset( debt_covered, bid.inv_swan_price.quote.asset_id ),
                                                  asset( call_obj.collateral, bid.inv_swan_price.base.asset_id ) ) );

   remove(bid);
}

void database::cancel_settle_order( const force_settlement_object& order )
{
   adjust_balance(order.owner, order.balance);

   push_applied_operation( asset_settle_cancel_operation( order.get_id(), order.owner, order.balance ) );

   remove(order);
}

void database::cancel_limit_order( const limit_order_object& order, bool create_virtual_op, bool skip_cancel_fee )
{
   // if need to create a virtual op, try deduct a cancellation fee here.
   // there are two scenarios when order is cancelled and need to create a virtual op:
   // 1. due to expiration: always deduct a fee if there is any fee deferred
   // 2. due to cull_small: deduct a fee after hard fork 604, but not before (will set skip_cancel_fee)
   const account_statistics_object* seller_acc_stats = nullptr;
   const asset_dynamic_data_object* deferred_fee_asset_dyn_data = nullptr;
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
            seller_acc_stats = &get_account_stats_by_owner( order.seller );
            modify( *seller_acc_stats, [&core_cancel_fee, this]( account_statistics_object& obj ) {
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
               fc::uint128_t fee128( deferred_paid_fee.amount.value );
               fee128 *= core_cancel_fee.amount.value;
               // to round up
               fee128 += order.deferred_fee.value;
               fee128 -= 1;
               fee128 /= order.deferred_fee.value;
               share_type cancel_fee_amount = static_cast<int64_t>(fee128);
               // cancel_fee should be positive, pay it to asset's accumulated_fees
               deferred_fee_asset_dyn_data = &deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
               modify( *deferred_fee_asset_dyn_data, [&cancel_fee_amount](asset_dynamic_data_object& addo) {
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
      if( !seller_acc_stats )
         seller_acc_stats = &get_account_stats_by_owner( order.seller );
      modify( *seller_acc_stats, [&refunded]( account_statistics_object& obj ) {
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
      if( !deferred_fee_asset_dyn_data )
         deferred_fee_asset_dyn_data = &deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
      modify( *deferred_fee_asset_dyn_data, [&deferred_fee](asset_dynamic_data_object& addo) {
         addo.fee_pool += deferred_fee;
      });
   }

   if( create_virtual_op )
   {
      auto op_id = push_applied_operation( vop );
      set_applied_operation_result( op_id, refunded );
   }

   cleanup_and_remove_limit_order( order );
}

void database::cleanup_and_remove_limit_order( const limit_order_object& order )
{
   // Unlink the linked take profit order if it exists
   if( order.take_profit_order_id.valid() )
   {
      const auto& take_profit_order = (*order.take_profit_order_id)(*this);
      modify( take_profit_order, []( limit_order_object& loo ) {
         loo.take_profit_order_id.reset();
      });
   }

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
      {
         db.cancel_limit_order( order, true, true );
      }
      else
         db.cancel_limit_order( order );
      return true;
   }
   return false;
}

// Note: optimizations have been done in apply_order(...)
bool database::apply_order_before_hardfork_625(const limit_order_object& new_order_object)
{
   auto order_id = new_order_object.id;
   const asset_object& sell_asset = get(new_order_object.amount_for_sale().asset_id);
   const asset_object& receive_asset = get(new_order_object.amount_to_receive().asset_id);

   // Possible optimization: We only need to check calls if both are true:
   //  - The new order is at the front of the book
   //  - The new order is below the call limit price
   bool called_some = check_call_orders(sell_asset, true, true); // the first time when checking, call order is maker
   bool called_some_else = check_call_orders(receive_asset, true, true); // the other side, same as above
   if( ( called_some || called_some_else ) && !find_object(order_id) ) // then we were filled by call order
      return true;

   const auto& limit_price_idx = get_index_type<limit_order_index>().indices().get<by_price>();

   // it should be possible to simply check the NEXT/PREV iterator after new_order_object to
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
      finished = ( match(new_order_object, *old_limit_itr, old_limit_itr->sell_price)
                   != match_result_type::only_maker_filled );
   }

   // Possible optimization: only check calls if the new order completely filled some old order.
   // Do I need to check both assets?
   check_call_orders(sell_asset); // after the new limit order filled some orders on the book,
                                  // if a call order matches another order, the call order is taker
   check_call_orders(receive_asset); // the other side, same as above

   const limit_order_object* updated_order_object = find< limit_order_object >( order_id );
   if( !updated_order_object )
      return true;
   if( head_block_time() <= HARDFORK_555_TIME )
      return false;
   // before #555 we would have done maybe_cull_small_order() logic as a result of fill_order()
   // being called by match() above
   // however after #555 we need to get rid of small orders -- #555 hardfork defers logic that
   // was done too eagerly before, and
   // this is the point it's deferred to.
   return maybe_cull_small_order( *this, *updated_order_object );
}

/***
 * @brief apply a new limit_order_object to the market, matching with existing limit orders or
 *    margin call orders where possible, leaving remainder on the book if not fully matched.
 * @detail Called from limit_order_create_evaluator::do_apply() in market_evaluator.cpp in
 *    response to a limit_order_create operation.  If we're not at the front of the book, we
 *    return false early and do nothing else, since there's nothing we can match.  If we are at
 *    the front of the book, then we first look for matching limit orders that are more
 *    favorable than the margin call price, then we search through active margin calls, then
 *    finaly the remaining limit orders, until we either fully consume the order or can no
 *    longer match and must leave the remainder on the book.
 * @return Returns true if limit order is completely consumed by matching, else false if it
 *    remains on the book.
 * @param new_order_object the new limit order (read only ref, though the corresponding db
 *    object is modified as we match and deleted if filled completely)
 */
bool database::apply_order(const limit_order_object& new_order_object)
{
   auto order_id = new_order_object.id;
   asset_id_type sell_asset_id = new_order_object.sell_asset_id();
   asset_id_type recv_asset_id = new_order_object.receive_asset_id();

   // We only need to check if the new order will match with others if it is at the front of the book
   const auto& limit_price_idx = get_index_type<limit_order_index>().indices().get<by_price>();
   auto limit_itr = limit_price_idx.iterator_to( new_order_object );
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
   price call_match_price;  // Price at which margin calls sit on the books. Prior to BSIP-74 this price is
                            // same as the MSSP. After, it is the MCOP, which may deviate from MSSP due to MCFR.
   price call_pays_price;   // Price margin call actually relinquishes collateral at. Equals the MSSP and it may
                            // differ from call_match_price if there is a Margin Call Fee.
   if( sell_asset.is_market_issued() )
   {
      sell_abd = &sell_asset.bitasset_data( *this );
      if( sell_abd->options.short_backing_asset == recv_asset_id
          && !sell_abd->is_prediction_market
          && !sell_abd->is_globally_settled()
          && !sell_abd->current_feed.settlement_price.is_null() )
      {
         if( before_core_hardfork_1270 ) {
            call_match_price = ~sell_abd->current_feed.max_short_squeeze_price_before_hf_1270();
            call_pays_price = call_match_price;
         } else {
            call_match_price = ~sell_abd->get_margin_call_order_price();
            call_pays_price = ~sell_abd->current_feed.max_short_squeeze_price();
         }
         if( ~new_order_object.sell_price <= call_match_price ) // If new limit order price is good enough to
            to_check_call_orders = true;                        // match a call, then check if there are calls.
      }
   }

   bool finished = false; // whether the new order is gone
   bool feed_price_updated = false; // whether current_feed.settlement_price has been updated
   if( to_check_call_orders )
   {
      // check limit orders first, match the ones with better price in comparison to call orders
      auto limit_itr_after_call = limit_price_idx.lower_bound( call_match_price );
      while( !finished && limit_itr != limit_itr_after_call )
      {
         const limit_order_object& matching_limit_order = *limit_itr;
         ++limit_itr;
         // match returns 2 when only the old order was fully filled.
         // In this case, we keep matching; otherwise, we stop.
         finished = ( match( new_order_object, matching_limit_order, matching_limit_order.sell_price )
                      != match_result_type::only_maker_filled );
      }

      auto call_min = price::min( recv_asset_id, sell_asset_id );
      if( !finished && !before_core_hardfork_1270 ) // TODO refactor or cleanup duplicate code after core-1270 hf
      {
         // check if there are margin calls
         // Note: it is safe to iterate here even if there is no call order due to individual settlements
         const auto& call_collateral_idx = get_index_type<call_order_index>().indices().get<by_collateral>();
         // Note: when BSRM is no_settlement, current_feed can change after filled a call order,
         //       so we recalculate inside the loop
         using bsrm_type = bitasset_options::black_swan_response_type;
         auto bsrm = sell_abd->get_black_swan_response_method();
         bool update_call_price = ( bsrm_type::no_settlement == bsrm && sell_abd->is_current_feed_price_capped() );
         auto old_current_feed_price = sell_abd->current_feed.settlement_price;
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
            const auto match_result = match( new_order_object, *call_itr, call_match_price,
                                             *sell_abd, call_pays_price );
            // match returns 1 or 3 when the new order was fully filled.
            // In this case, we stop matching; otherwise keep matching.
            // since match can return 0 due to BSIP38 (hf core-834), we no longer only check if the result is 2.
            if( match_result_type::only_taker_filled == match_result
                  || match_result_type::both_filled == match_result )
               finished = true;
            else if( update_call_price )
            {
               call_match_price = ~sell_abd->get_margin_call_order_price();
               call_pays_price = ~sell_abd->current_feed.max_short_squeeze_price();
               update_call_price = sell_abd->is_current_feed_price_capped();
               // Since current feed price (in debt/collateral) can only decrease after updated, if there still
               // exists a call order in margin call territory, it would be on the top of the order book,
               // so no need to check if the current limit (buy) order would match another limit (sell) order atm.
               // On the other hand, the current limit order is on the top of the other side of the order book.
            }
         } // while !finished
         if( bsrm_type::no_settlement == bsrm && sell_abd->current_feed.settlement_price != old_current_feed_price )
            feed_price_updated = true;
      } // if after core-1270 hf
      else if( !finished ) // and before core-1270 hard fork
      {
         // check if there are margin calls
         const auto& call_price_idx = get_index_type<call_order_index>().indices().get<by_price>();
         while( !finished )
         {
            // assume hard fork core-343 and core-625 will take place at same time,
            // always check call order with least call_price
            auto call_itr = call_price_idx.lower_bound( call_min );
            if( call_itr == call_price_idx.end()
                  || call_itr->debt_type() != sell_asset_id
                  // feed protected https://github.com/cryptonomex/graphene/issues/436
                  || call_itr->call_price > ~sell_abd->current_feed.settlement_price )
               break;
            // assume hard fork core-338 and core-625 will take place at same time,
            // not checking HARDFORK_CORE_338_TIME here.
            const auto match_result = match( new_order_object, *call_itr, call_match_price, *sell_abd );
            // match returns 1 or 3 when the new order was fully filled.
            // In this case, we stop matching; otherwise keep matching.
            // since match can return 0 due to BSIP38 (hard fork core-834),
            // we no longer only check if the result is 2.
            if( match_result_type::only_taker_filled == match_result
                  || match_result_type::both_filled == match_result )
               finished = true;
         } // while !finished
      } // if before core-1270 hf
   } // if to check call

   // still need to check limit orders
   while( !finished && limit_itr != limit_end )
   {
      const limit_order_object& matching_limit_order = *limit_itr;
      ++limit_itr;
      // match returns 2 when only the old order was fully filled. In this case, we keep matching; otherwise, we stop.
      finished = ( match( new_order_object, matching_limit_order, matching_limit_order.sell_price )
                   != match_result_type::only_maker_filled );
   }

   bool limit_order_is_gone = true;
   const limit_order_object* updated_order_object = find< limit_order_object >( order_id );
   if( updated_order_object )
      // before #555 we would have done maybe_cull_small_order() logic as a result of fill_order()
      // being called by match() above
      // however after #555 we need to get rid of small orders -- #555 hardfork defers logic that
      // was done too eagerly before, and
      // this is the point it's deferred to.
      limit_order_is_gone = maybe_cull_small_order( *this, *updated_order_object );

   if( limit_order_is_gone && feed_price_updated )
   {
      // If current_feed got updated, and the new limit order is gone,
      // it is possible that other limit orders are able to get filled,
      // so we need to call check_call_orders()
      check_call_orders( sell_asset, true, false, sell_abd );
   }

   return limit_order_is_gone;
}

void database::apply_force_settlement( const force_settlement_object& new_settlement,
                                       const asset_bitasset_data_object& bitasset,
                                       const asset_object& asset_obj )
{
   // Defensive checks
   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( HARDFORK_CORE_2481_PASSED( maint_time ), "Internal error: hard fork core-2481 not passed" );
   FC_ASSERT( new_settlement.balance.asset_id == bitasset.asset_id, "Internal error: asset type mismatch" );
   FC_ASSERT( !bitasset.is_prediction_market, "Internal error: asset is a prediction market" );
   FC_ASSERT( !bitasset.is_globally_settled(), "Internal error: asset is globally settled already" );
   FC_ASSERT( !bitasset.current_feed.settlement_price.is_null(), "Internal error: no sufficient price feeds" );
   // GCOVR_EXCL_STOP

   auto head_time = head_block_time();
   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   auto new_obj_id = new_settlement.id;

   // Price at which margin calls sit on the books.
   // It is the MCOP, which may deviate from MSSP due to MCFR.
   price call_match_price = bitasset.get_margin_call_order_price();
   // Price margin call actually relinquishes collateral at. Equals the MSSP and it may
   // differ from call_match_price if there is a Margin Call Fee.
   price call_pays_price = bitasset.current_feed.max_short_squeeze_price();

   // Note: when BSRM is no_settlement, current_feed can change after filled a call order,
   //       so we recalculate inside the loop
   using bsrm_type = bitasset_options::black_swan_response_type;
   auto bsrm = bitasset.get_black_swan_response_method();
   bool update_call_price = ( bsrm_type::no_settlement == bsrm && bitasset.is_current_feed_price_capped() );

   bool finished = false; // whether the new order is gone

   // check if there are margin calls
   // Note: it is safe to iterate here even if there is no call order due to individual settlements
   const auto& call_collateral_idx = get_index_type<call_order_index>().indices().get<by_collateral>();
   auto call_min = price::min( bitasset.options.short_backing_asset, new_settlement.balance.asset_id );
   while( !finished )
   {
      // always check call order with the least collateral ratio
      auto call_itr = call_collateral_idx.lower_bound( call_min );
      // Note: we don't precalculate an iterator with upper_bound() before entering the loop,
      //       because the upper bound can change after a call order got filled
      if( call_itr == call_collateral_idx.end()
            || call_itr->debt_type() != new_settlement.balance.asset_id
            // feed protected https://github.com/cryptonomex/graphene/issues/436
            || call_itr->collateralization() > bitasset.current_maintenance_collateralization )
         break;
      // TCR applies here
      auto settle_price = after_core_hardfork_2582 ? bitasset.median_feed.settlement_price
                                                   : bitasset.current_feed.settlement_price;
      asset max_debt_to_cover( call_itr->get_max_debt_to_cover( call_pays_price,
                                                       settle_price,
                                                       bitasset.current_feed.maintenance_collateral_ratio,
                                                       bitasset.current_maintenance_collateralization ),
                               new_settlement.balance.asset_id );

      match( new_settlement, *call_itr, call_pays_price, bitasset, max_debt_to_cover, call_match_price, true );

      // Check whether the new order is gone
      finished = ( nullptr == find_object( new_obj_id ) );

      if( update_call_price )
      {
         // when current_feed is updated, it is possible that there are limit orders able to get filled,
         // so we need to call check_call_orders(), but skip matching call orders with force settlements
         check_call_orders( asset_obj, true, false, &bitasset, false, true );
         if( !finished )
         {
            call_match_price = bitasset.get_margin_call_order_price();
            call_pays_price = bitasset.current_feed.max_short_squeeze_price();
            update_call_price = bitasset.is_current_feed_price_capped();
         }
      }
   }

}

/// Helper function
static database::match_result_type get_match_result( bool taker_filled, bool maker_filled )
{
   int8_t result = 0;
   if( maker_filled )
      result += static_cast<int8_t>( database::match_result_type::only_maker_filled );
   if( taker_filled )
      result += static_cast<int8_t>( database::match_result_type::only_taker_filled );
   return static_cast<database::match_result_type>( result );
}

/**
 *  Matches the two orders, the first parameter is taker, the second is maker.
 *
 *  @return which orders were filled (and thus removed)
 */
database::match_result_type database::match( const limit_order_object& taker, const limit_order_object& maker,
                                             const price& match_price )
{
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( taker.sell_price.quote.asset_id == maker.sell_price.base.asset_id );
   FC_ASSERT( taker.sell_price.base.asset_id  == maker.sell_price.quote.asset_id );
   FC_ASSERT( taker.for_sale > 0 && maker.for_sale > 0 );
   // GCOVR_EXCL_STOP

   return maker.is_settled_debt ? match_limit_settled_debt( taker, maker, match_price )
                                : match_limit_normal_limit( taker, maker, match_price );
}

/// Match a normal limit order with another normal limit order
database::match_result_type database::match_limit_normal_limit( const limit_order_object& taker,
                               const limit_order_object& maker, const price& match_price )
{
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( !maker.is_settled_debt, "Internal error: maker is settled debt" );
   FC_ASSERT( !taker.is_settled_debt, "Internal error: taker is settled debt" );
   // GCOVR_EXCL_STOP

   auto taker_for_sale = taker.amount_for_sale();
   auto maker_for_sale = maker.amount_for_sale();

   asset taker_pays;
   asset taker_receives;
   asset maker_pays;
   asset maker_receives;

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   bool cull_taker = false;
   if( taker_for_sale <= ( maker_for_sale * match_price ) ) // rounding down here should be fine
   {
      taker_receives  = taker_for_sale * match_price; // round down, in favor of bigger order

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      if( taker_receives.amount == 0 && maint_time > HARDFORK_CORE_184_TIME )
         return match_result_type::only_taker_filled;

      if( before_core_hardfork_342 )
         maker_receives = taker_for_sale;
      else
      {
         // The remaining amount in order `taker` would be too small,
         //   so we should cull the order in fill_limit_order() below.
         // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
         //   so calling maybe_cull_small() will always cull it.
         maker_receives = taker_receives.multiply_and_round_up( match_price );
         cull_taker = true;
      }
   }
   else
   {
      //This line once read: assert( maker_for_sale < taker_for_sale * match_price ); // check
      //This assert is not always true -- see trade_amount_equals_zero in operation_tests.cpp
      //Although taker_for_sale is greater than maker_for_sale * match_price,
      //         maker_for_sale == taker_for_sale * match_price
      //Removing the assert seems to be safe -- apparently no asset is created or destroyed.

      // The maker won't be paying something for nothing, since if it would, it would have been cancelled already.
      maker_receives = maker_for_sale * match_price; // round down, in favor of bigger order
      if( before_core_hardfork_342 )
         taker_receives = maker_for_sale;
      else
         // The remaining amount in order `maker` would be too small,
         //   so the order will be culled in fill_limit_order() below
         taker_receives = maker_receives.multiply_and_round_up( match_price );
   }

   maker_pays = taker_receives;
   taker_pays  = maker_receives;

   if( before_core_hardfork_342 )
      FC_ASSERT( taker_pays == taker.amount_for_sale() ||
                 maker_pays == maker.amount_for_sale() );

   // the first param of match() is taker
   bool taker_filled = fill_limit_order( taker, taker_pays, taker_receives, cull_taker, match_price, false );
   // the second param of match() is maker
   bool maker_filled = fill_limit_order( maker, maker_pays, maker_receives, true, match_price, true );

   match_result_type result = get_match_result( taker_filled, maker_filled );
   FC_ASSERT( result != match_result_type::none_filled );
   return result;
}

/// When matching a limit order against settled debt, the maker actually behaviors like a call order
database::match_result_type database::match_limit_settled_debt( const limit_order_object& taker,
                               const limit_order_object& maker, const price& match_price )
{
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( maker.is_settled_debt, "Internal error: maker is not settled debt" );
   FC_ASSERT( !taker.is_settled_debt, "Internal error: taker is settled debt" );
   // GCOVR_EXCL_STOP

   bool cull_taker = false;
   bool maker_filled = false;

   const auto& mia = maker.receive_asset_id()(*this);
   const auto& bitasset = mia.bitasset_data(*this);

   auto usd_for_sale = taker.amount_for_sale();
   auto usd_to_buy = asset( bitasset.individual_settlement_debt, maker.receive_asset_id() );

   asset call_receives;
   asset order_receives;
   if( usd_to_buy > usd_for_sale )
   {  // fill taker limit order
      order_receives  = usd_for_sale * match_price; // round down here, in favor of "call order"

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      if( order_receives.amount == 0 )
         return match_result_type::only_taker_filled;

      // The remaining amount in the limit order could be too small,
      //   so we should cull the order in fill_limit_order() below.
      // If the order would receive 0 even at `match_price`, it would receive 0 at its own price,
      //   so calling maybe_cull_small() will always cull it.
      call_receives = order_receives.multiply_and_round_up( match_price );
      cull_taker = true;
   }
   else
   {  // fill maker "call order"
      call_receives  = usd_to_buy;
      order_receives = maker.amount_for_sale();
      maker_filled = true;
   }

   // seller, pays, receives, ...
   bool taker_filled = fill_limit_order( taker, call_receives, order_receives, cull_taker, match_price, false );

   const auto& head_time = head_block_time();
   bool after_core_hardfork_2591 = HARDFORK_CORE_2591_PASSED( head_time ); // Tighter peg (fill debt order at MCOP)

   asset call_pays = order_receives;
   if( maker_filled ) // Regardless of hf core-2591
      call_pays.amount = bitasset.individual_settlement_fund;
   else if( maker.for_sale != bitasset.individual_settlement_fund ) // implies hf core-2591
      call_pays = call_receives * bitasset.get_individual_settlement_price(); // round down, in favor of "call order"
   if( call_pays < order_receives ) // be defensive, maybe unnecessary
   { // GCOVR_EXCL_START
      wlog( "Unexpected scene: call_pays < order_receives" );
      call_pays = order_receives;
   } // GCOVR_EXCL_STOP
   asset collateral_fee = call_pays - order_receives;

   // Reduce current supply, and accumulate collateral fees
   const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);
   modify( mia_ddo, [&call_receives,&collateral_fee]( asset_dynamic_data_object& ao ){
      ao.current_supply -= call_receives.amount;
      ao.accumulated_collateral_fees += collateral_fee.amount;
   });

   // Push fill_order vitual operation
   // id, seller, pays, receives, ...
   push_applied_operation( fill_order_operation( maker.id, maker.seller, call_pays, call_receives,
                                                 collateral_fee, match_price, true ) );

   // Update bitasset data
   modify( bitasset, [&call_receives,&call_pays]( asset_bitasset_data_object& obj ){
      obj.individual_settlement_debt -= call_receives.amount;
      obj.individual_settlement_fund -= call_pays.amount;
   });

   // Update the maker order
   // Note: CORE asset in settled debt is not counted in account_stats.total_core_in_orders
   if( maker_filled )
      remove( maker );
   else
   {
      modify( maker, [after_core_hardfork_2591,&bitasset]( limit_order_object& obj ) {
         if( after_core_hardfork_2591 )
         {
            // Note: for simplicity, only update price when necessary
            asset settled_debt( bitasset.individual_settlement_debt, obj.receive_asset_id() );
            obj.for_sale = settled_debt.multiply_and_round_up( obj.sell_price ).amount;
            if( obj.for_sale > bitasset.individual_settlement_fund ) // be defensive, maybe unnecessary
            { // GCOVR_EXCL_START
               wlog( "Unexpected scene: obj.for_sale > bitasset.individual_settlement_fund" );
               obj.for_sale = bitasset.individual_settlement_fund;
               obj.sell_price = ~bitasset.get_individual_settlement_price();
            } // GCOVR_EXCL_STOP
         }
         else
         {
            obj.for_sale = bitasset.individual_settlement_fund;
            obj.sell_price = ~bitasset.get_individual_settlement_price();
         }
         // Note: filled_amount is not updated, but it should be fine
      });
      // Note:
      // After the price is updated, it is possible that the order can be matched with another order on the order
      // book, which may then be matched with more other orders. For simplicity, we don't do more matching here.
   }

   match_result_type result = get_match_result( taker_filled, maker_filled );
   return result;
}

/// When matching a settled debt order against a limit order, the taker actually behaviors like a call order
// TODO fix duplicate code
database::match_result_type database::match_settled_debt_limit( const limit_order_object& taker,
                               const limit_order_object& maker, const price& match_price )
{
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( !maker.is_settled_debt, "Internal error: maker is settled debt" );
   FC_ASSERT( taker.is_settled_debt, "Internal error: taker is not settled debt" );
   // GCOVR_EXCL_STOP

   bool taker_filled = false;

   const auto& mia = taker.receive_asset_id()(*this);
   const auto& bitasset = mia.bitasset_data(*this);

   auto usd_for_sale = maker.amount_for_sale();
   auto usd_to_buy = asset( bitasset.individual_settlement_debt, taker.receive_asset_id() );

   asset call_receives;
   asset order_receives;
   if( usd_to_buy > usd_for_sale )
   {  // fill maker limit order
      order_receives = usd_for_sale * match_price; // round down here, in favor of call order

      // Be here, the limit order won't be paying something for nothing, since if it would, it would have
      //   been cancelled elsewhere already (a maker limit order won't be paying something for nothing).

      call_receives = order_receives.multiply_and_round_up( match_price );
   }
   else
   {  // fill taker "call order"
      call_receives = usd_to_buy;
      order_receives = call_receives.multiply_and_round_up( match_price ); // round up here, in favor of limit order
      taker_filled = true;
   }

   asset call_pays = order_receives;
   if( taker_filled )
      call_pays.amount = bitasset.individual_settlement_fund;
   else if( taker.for_sale != bitasset.individual_settlement_fund )
      call_pays = call_receives * bitasset.get_individual_settlement_price(); // round down, in favor of "call order"
   if( call_pays < order_receives ) // be defensive, maybe unnecessary
   { // GCOVR_EXCL_START
      wlog( "Unexpected scene: call_pays < order_receives" );
      call_pays = order_receives;
   } // GCOVR_EXCL_STOP
   asset collateral_fee = call_pays - order_receives;

   // Reduce current supply, and accumulate collateral fees
   const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);
   modify( mia_ddo, [&call_receives,&collateral_fee]( asset_dynamic_data_object& ao ){
      ao.current_supply -= call_receives.amount;
      ao.accumulated_collateral_fees += collateral_fee.amount;
   });

   // Push fill_order vitual operation
   // id, seller, pays, receives, ...
   push_applied_operation( fill_order_operation( taker.id, taker.seller, call_pays, call_receives,
                                                 collateral_fee, match_price, false ) );

   // Update bitasset data
   modify( bitasset, [&call_receives,&call_pays]( asset_bitasset_data_object& obj ){
      obj.individual_settlement_debt -= call_receives.amount;
      obj.individual_settlement_fund -= call_pays.amount;
   });

   // Update the taker order
   // Note: CORE asset in settled debt is not counted in account_stats.total_core_in_orders
   if( taker_filled )
      remove( taker );
   else
   {
      modify( taker, [&bitasset]( limit_order_object& obj ) {
         // Note: for simplicity, only update price when necessary
         asset settled_debt( bitasset.individual_settlement_debt, obj.receive_asset_id() );
         obj.for_sale = settled_debt.multiply_and_round_up( obj.sell_price ).amount;
         if( obj.for_sale > bitasset.individual_settlement_fund ) // be defensive, maybe unnecessary
         { // GCOVR_EXCL_START
            wlog( "Unexpected scene: obj.for_sale > bitasset.individual_settlement_fund" );
            obj.for_sale = bitasset.individual_settlement_fund;
            obj.sell_price = ~bitasset.get_individual_settlement_price();
         } // GCOVR_EXCL_STOP
         // Note: filled_amount is not updated, but it should be fine
      });
   }

   // seller, pays, receives, ...
   bool maker_filled = fill_limit_order( maker, call_receives, order_receives, true, match_price, true );

   match_result_type result = get_match_result( taker_filled, maker_filled );
   return result;
}


database::match_result_type database::match( const limit_order_object& bid, const call_order_object& ask,
                     const price& match_price,
                     const asset_bitasset_data_object& bitasset,
                     const price& call_pays_price )
{
   FC_ASSERT( bid.sell_asset_id() == ask.debt_type() );
   FC_ASSERT( bid.receive_asset_id() == ask.collateral_type() );
   FC_ASSERT( bid.for_sale > 0 && ask.debt > 0 && ask.collateral > 0 );

   bool cull_taker = false;

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_1270 = ( maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue
   bool after_core_hardfork_2481 = HARDFORK_CORE_2481_PASSED( maint_time ); // Match settle orders with margin calls

   auto head_time = head_block_time();
   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   const auto& feed_price = after_core_hardfork_2582 ? bitasset.median_feed.settlement_price
                                                     : bitasset.current_feed.settlement_price;
   const auto& maintenance_collateral_ratio = bitasset.current_feed.maintenance_collateral_ratio;
   optional<price> maintenance_collateralization;
   if( !before_core_hardfork_1270 )
      maintenance_collateralization = bitasset.current_maintenance_collateralization;

   asset usd_for_sale = bid.amount_for_sale();
   asset usd_to_buy( ask.get_max_debt_to_cover( call_pays_price, feed_price, maintenance_collateral_ratio,
                                                maintenance_collateralization ),
                     ask.debt_type() );

   asset call_pays;
   asset call_receives;
   asset order_pays;
   asset order_receives;
   if( usd_to_buy > usd_for_sale )
   {  // fill limit order
      order_receives  = usd_for_sale * match_price; // round down here, in favor of call order

      // Be here, it's possible that taker is paying something for nothing due to partially filled in last loop.
      // In this case, we see it as filled and cancel it later
      if( order_receives.amount == 0 )
         return match_result_type::only_taker_filled;

      call_receives = order_receives.multiply_and_round_up( match_price );
      if( after_core_hardfork_2481 )
         call_pays = call_receives * call_pays_price; // calculate with updated call_receives
      else
         // TODO add tests about CR change
         call_pays = usd_for_sale * call_pays_price; // (same as match_price until BSIP-74)

      // The remaining amount (if any) in the limit order would be too small,
      //   so we should cull the order in fill_limit_order() below.
      // The order would receive 0 even at `match_price`, so it would receive 0 at its own price,
      //   so calling maybe_cull_small() will always cull it.
      cull_taker = true;
   }
   else
   {  // fill call order
      call_receives  = usd_to_buy;
      order_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up here, in favor of limit order
      call_pays      = usd_to_buy.multiply_and_round_up( call_pays_price );
      // Note: here we don't re-assign call_receives with (orders_receives * match_price) to receive more
      //       debt asset, it means the call order could be receiving a bit too much less than its value.
      //       It is a sad thing for the call order, but it is the rule -- when a call order is margin called,
      //       it does not get more than it borrowed.
      //       On the other hand, if the call order is not being closed (due to TCR),
      //       it means get_max_debt_to_cover() did not return a perfect result, probably we can improve it.
   }
   order_pays = call_receives;

   // Compute margin call fee (BSIP74). Difference between what the call order pays and the limit order
   // receives is the margin call fee that is paid by the call order owner to the asset issuer.
   // Margin call fee should equal = X*MCFR/settle_price, to within rounding error.
   FC_ASSERT(call_pays >= order_receives);
   const asset margin_call_fee = call_pays - order_receives;

   bool taker_filled = fill_limit_order( bid, order_pays, order_receives, cull_taker, match_price, false );
   bool maker_filled = fill_call_order( ask, call_pays, call_receives, match_price, true, margin_call_fee );

   // Update current_feed after filled call order if needed
   if( bitasset_options::black_swan_response_type::no_settlement == bitasset.get_black_swan_response_method() )
      update_bitasset_current_feed( bitasset, true );

   // Note: result can be none_filled when call order has target_collateral_ratio option set.
   match_result_type result = get_match_result( taker_filled, maker_filled );
   return result;
}


asset database::match( const force_settlement_object& settle,
                       const call_order_object& call,
                       const price& match_price,
                       const asset_bitasset_data_object& bitasset,
                       const asset& max_settlement,
                       const price& fill_price,
                       bool is_margin_call )
{
   return match_impl( settle, call, match_price, bitasset, max_settlement, fill_price, is_margin_call, true );
}

asset database::match( const call_order_object& call,
                       const force_settlement_object& settle,
                       const price& match_price,
                       const asset_bitasset_data_object& bitasset,
                       const asset& max_settlement,
                       const price& fill_price )
{
   return match_impl( settle, call, match_price, bitasset, max_settlement, fill_price, true, false );
}

asset database::match_impl( const force_settlement_object& settle,
                            const call_order_object& call,
                            const price& p_match_price,
                            const asset_bitasset_data_object& bitasset,
                            const asset& max_settlement,
                            const price& p_fill_price,
                            bool is_margin_call,
                            bool settle_is_taker )
{ try {
   FC_ASSERT(call.get_debt().asset_id == settle.balance.asset_id );
   FC_ASSERT(call.debt > 0 && call.collateral > 0 && settle.balance.amount > 0);

   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   auto settle_for_sale = std::min(settle.balance, max_settlement);
   auto call_debt = call.get_debt();
   auto call_collateral = call.get_collateral();

   price match_price = p_match_price;
   price fill_price = p_fill_price;

   asset call_receives   = std::min(settle_for_sale, call_debt);
   asset call_pays       = call_receives * match_price; // round down here, in favor of call order, for first check
                                                        // TODO possible optimization: check need to round up
                                                        //      or down first

   // Note: when is_margin_call == true, the call order is being margin called,
   //       match_price is the price that the call order pays,
   //       fill_price is the price that the settle order receives,
   //       the difference is the margin-call fee

   asset settle_receives = call_pays;
   asset settle_pays     = call_receives;

   // Be here, the call order may be paying nothing.
   bool cull_settle_order = false; // whether need to cancel dust settle order
   if( maint_time > HARDFORK_CORE_184_TIME && call_pays.amount == 0 )
   {
      if( call_receives == call_debt ) // the call order is smaller than or equal to the settle order
      {
         call_pays.amount = 1;
         settle_receives.amount = 1; // Note: no margin-call fee in this case even if is_margin_call
      }
      else if( call_receives == settle.balance ) // the settle order is smaller
      {
         cancel_settle_order( settle );
         // If the settle order is canceled, we just return, since nothing else can be done
         return asset( 0, call_debt.asset_id );
      }
      // be here, neither order will be completely filled, perhaps due to max_settlement too small
      else if( !is_margin_call )
      {
         // If the call order is not being margin called, we simply return and continue outside
         return asset( 0, call_debt.asset_id );
      }
      else
      {
         // Be here, the call order is being margin called, and it is not being fully covered due to TCR,
         // and the settle order is big enough.
         // So the call order is considered as the smaller one, and we should round up call_pays.
         // We have ( call_receives == max_settlement == call_order.get_max_debt_to_cover() ).
         // It is guaranteed by call_order.get_max_debt_to_cover() that rounding up call_pays
         // would not reduce CR of the call order, but would push it to be above MCR.
         call_pays.amount = 1;
         settle_receives.amount = 1; // Note: no margin-call fee in this case
      }
   } // end : if after the core-184 hf and call_pays.amount == 0
   else if( !before_core_hardfork_342 && call_pays.amount != 0 )
   {
      auto margin_call_pays_ratio = bitasset.get_margin_call_pays_ratio();
      // be here, the call order is not paying nothing,
      // but it is still possible that the settle order is paying more than minimum required due to rounding
      if( call_receives == call_debt ) // the call order is smaller than or equal to the settle order
      {
         call_pays = call_receives.multiply_and_round_up( match_price ); // round up here, in favor of settle order
         if( is_margin_call ) // implies hf core-2481
         {
            if( call_pays.amount > call.collateral ) // CR too low
            {
               call_pays.amount = call.collateral;
               match_price = call_debt / call_collateral;
               fill_price = match_price / margin_call_pays_ratio;
            }
            settle_receives = call_receives.multiply_and_round_up( fill_price );
         }
         else // be here, we should have: call_pays <= call_collateral
         {
            settle_receives = call_pays; // Note: fill_price is not used in calculation when is_margin_call is false
         }
      }
      else // the call order is not completely filled, due to max_settlement too small or settle order too small
      {
         // be here, call_pays has been rounded down
         if( !is_margin_call )
         {
            // it was correct to round down call_pays.
            // round up here to mitigate rounding issues (hf core-342).
            // It is important to understand the math that the newly rounded-up call_receives won't be greater than
            // the old call_receives. And rounding up here would NOT make CR lower.
            call_receives = call_pays.multiply_and_round_up( match_price );
         }
         // the call order is a margin call, implies hf core-2481
         else if( settle_pays == max_settlement ) // the settle order is larger, but the call order has TCR
         {
            // Note: here settle_pays == call_receives
            call_pays = call_receives.multiply_and_round_up( match_price ); // round up, in favor of settle order
            settle_receives = call_receives.multiply_and_round_up( fill_price ); // round up
            // Note: here we do NOT stabilize call_receives since it is done in get_max_debt_to_cover(),
            //       and it is already the maximum value
         }
         else // the call order is a margin call, and the settle order is smaller
         {
            // It was correct to round down call_pays. However, it is not the final result.
            // For margin calls, due to margin call fee, it is fairer to calculate with fill_price first
            const auto& calculate = [&settle_receives,&settle_pays,&fill_price,&call_receives,&call_pays,&match_price]
            {
               settle_receives  = settle_pays * fill_price; // round down here, in favor of call order
               if( settle_receives.amount != 0 )
               {
                  // round up to mitigate rounding issues (hf core-342)
                  call_receives = settle_receives.multiply_and_round_up( fill_price );
                  // round down
                  call_pays = call_receives * match_price;
               }
            };

            calculate();
            if( settle_receives.amount == 0 )
            {
               cancel_settle_order( settle );
               // If the settle order is canceled, we just return, since nothing else can be done
               return asset( 0, call_debt.asset_id );
            }

            // check whether the call order can be filled at match_price
            bool cap_price = false;
            if( call_pays.amount >= call.collateral ) // CR too low, normally won't be true, just be defensive here
               cap_price = true;
            else
            {
               auto new_collateral = call_collateral - call_pays;
               auto new_debt = call_debt - call_receives; // the result is positive due to math
               if( ( new_collateral / new_debt ) < call.collateralization() ) // if CR would decrease
                  cap_price = true;
            }

            if( cap_price ) // match_price is not good, update match price and fill price, then calculate again
            {
               match_price = call_debt / call_collateral;
               fill_price = match_price / margin_call_pays_ratio;
               calculate();
               if( settle_receives.amount == 0 )
               {
                  // Note: when it is a margin call, max_settlement is max_debt_to_cover.
                  //       if need to cap price here, max_debt_to_cover should be equal to call_debt.
                  //       if call pays 0, it means the settle order is really small.
                  cancel_settle_order( settle );
                  // If the settle order is canceled, we just return, since nothing else can be done
                  return asset( 0, call_debt.asset_id );
               }
            }
         } // end : if is_margin_call, else ...

         // be here, we should have: call_pays <= call_collateral

         // if the settle order is too small, mark it to be culled
         if( settle_pays == settle.balance && call_receives != settle.balance )
            cull_settle_order = true;
         // else do nothing, since we can't cull the settle order, or it is already fully filled

         settle_pays = call_receives;
      }
   } // end : if after the core-342 hf and call_pays.amount != 0
   // else : before the core-184 hf or the core-342 hf, do nothing

   /**
    *  If the least collateralized call position lacks sufficient
    *  collateral to cover at the match price then this indicates a black
    *  swan event according to the price feed, but only the market
    *  can trigger a black swan.  So now we must cancel the forced settlement
    *  object.
    */
   if( before_core_hardfork_342 )
   {
      GRAPHENE_ASSERT( call_pays < call_collateral, black_swan_exception, "" );

      assert( settle_pays == settle_for_sale || call_receives == call.get_debt() );
   }
   // else do nothing, since black swan event won't happen, and the assertion is no longer true

   asset margin_call_fee = call_pays - settle_receives;

   fill_call_order( call, call_pays, call_receives, fill_price, settle_is_taker, margin_call_fee );
   // do not pay force-settlement fee if the call is being margin called
   fill_settle_order( settle, settle_pays, settle_receives, fill_price, !settle_is_taker, !is_margin_call );

   // Update current_feed after filled call order if needed
   if( bitasset_options::black_swan_response_type::no_settlement == bitasset.get_black_swan_response_method() )
      update_bitasset_current_feed( bitasset, true );

   if( cull_settle_order )
      cancel_settle_order( settle );

   return call_receives;
} FC_CAPTURE_AND_RETHROW( (p_match_price)(max_settlement)(p_fill_price) // GCOVR_EXCL_LINE
                          (is_margin_call)(settle_is_taker) ) } // GCOVR_EXCL_LINE

optional<limit_order_id_type> database::process_limit_order_on_fill( const limit_order_object& order,
                                                                     const asset& order_receives )
{
   optional<limit_order_id_type> result;
   if( order.on_fill.empty() )
      return result;

   const auto& take_profit_action = order.get_take_profit_action();

   fc::uint128_t amount128( order_receives.amount.value );
   amount128 *= take_profit_action.size_percent;
   amount128 += (GRAPHENE_100_PERCENT - 1); // Round up
   amount128 /= GRAPHENE_100_PERCENT;
   // GCOVR_EXCL_START
   // Defensive code, should not happen
   if( amount128 <= 0 )
      return result;
   // GCOVR_EXCL_STOP

   asset for_sale( static_cast<int64_t>( amount128 ), order_receives.asset_id );

   if( order.take_profit_order_id.valid() ) // Update existing take profit order
   {
      limit_order_update_operation op;
      op.seller = order.seller;
      op.order = *order.take_profit_order_id;
      op.delta_amount_to_sell = for_sale;

      if( ( time_point_sec::maximum() - take_profit_action.expiration_seconds ) > head_block_time() )
         op.new_expiration = head_block_time() + take_profit_action.expiration_seconds;
      else
         op.new_expiration = time_point_sec::maximum();

      try
      {
         if( take_profit_action.fee_asset_id == asset_id_type() )
            op.fee = current_fee_schedule().calculate_fee( op );
         else
            op.fee = current_fee_schedule().calculate_fee( op,
                        take_profit_action.fee_asset_id(*this).options.core_exchange_rate ); // This may throw

         if( *order.take_profit_order_id > order.get_id() ) //The linked take profit order was generated by this order
         {
            // Update order price
            const auto& take_profit_order = (*order.take_profit_order_id)(*this);
            for_sale.amount += take_profit_order.for_sale;
            auto sell_price = (~order.sell_price) * ratio_type( GRAPHENE_100_PERCENT,
                                 int32_t(GRAPHENE_100_PERCENT) + take_profit_action.spread_percent );
            auto new_min_to_receive = for_sale.multiply_and_round_up( sell_price ); // This may throw
            op.new_price = for_sale / new_min_to_receive;
         }
         // else do not update order price

         // GCOVR_EXCL_START
         // Defensive code, should not fail
         FC_ASSERT( !op.new_price || ( ~(*op.new_price) > order.sell_price ),
                    "Internal error: the take profit order should not match the current order" );
         // GCOVR_EXCL_STOP

         transaction_evaluation_state eval_state(this);
         eval_state.skip_limit_order_price_check = true;

         try_push_virtual_operation( eval_state, op );
      }
      catch( const fc::exception& e )
      {
         // We can in fact get here
         // e.g. if the selling or receiving asset issuer blacklisted the account,
         //      or no sufficient balance to pay fees, or undo sessions nested too deeply
         wlog( "At block ${n}, failed to process on_fill for limit order ${order}, "
               "automatic action (maybe incomplete) was ${op}, exception was ${e}",
               ("op", operation(op))("order", order)
               ("n", head_block_num())("e", e.to_detail_string()) );
      }
   }
   else // Create a new take profit order
   {
      limit_order_create_operation op;
      op.seller = order.seller;
      op.amount_to_sell = for_sale;
      if( ( time_point_sec::maximum() - take_profit_action.expiration_seconds ) > head_block_time() )
         op.expiration = head_block_time() + take_profit_action.expiration_seconds;
      else
         op.expiration = time_point_sec::maximum();
      if( take_profit_action.repeat )
         op.extensions.value.on_fill = order.on_fill;

      try
      {
         if( take_profit_action.fee_asset_id == asset_id_type() )
            op.fee = current_fee_schedule().calculate_fee( op );
         else
            op.fee = current_fee_schedule().calculate_fee( op,
                        take_profit_action.fee_asset_id(*this).options.core_exchange_rate ); // This may throw

         auto sell_price = (~order.sell_price) * ratio_type( GRAPHENE_100_PERCENT,
                              int32_t(GRAPHENE_100_PERCENT) + take_profit_action.spread_percent );
         op.min_to_receive = for_sale.multiply_and_round_up( sell_price ); // This may throw

         // GCOVR_EXCL_START
         // Defensive code, should not fail
         FC_ASSERT( ~op.get_price() > order.sell_price,
                    "Internal error: the take profit order should not match the current order" );
         // GCOVR_EXCL_STOP

         transaction_evaluation_state eval_state(this);

         auto op_result = try_push_virtual_operation( eval_state, op );
         result = limit_order_id_type( op_result.get<object_id_type>() );
      }
      catch( const fc::exception& e )
      {
         // We can in fact get here
         // e.g. if the selling or receiving asset issuer blacklisted the account,
         //      or no sufficient balance to pay fees, or undo sessions nested too deeply
         wlog( "At block ${n}, failed to process on_fill for limit order ${order}, "
               "automatic action (maybe incomplete) was ${op}, exception was ${e}",
               ("op", operation(op))("order", order)
               ("n", head_block_num())("e", e.to_detail_string()) );
      }
   }

   return result;
}

bool database::fill_limit_order( const limit_order_object& order, const asset& pays, const asset& receives,
                                 bool cull_if_small, const price& fill_price, const bool is_maker)
{ try {
   if( head_block_time() < HARDFORK_555_TIME )
      cull_if_small = true;

   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( order.amount_for_sale().asset_id == pays.asset_id );
   FC_ASSERT( pays.asset_id != receives.asset_id );
   // GCOVR_EXCL_STOP

   const account_object& seller = order.seller(*this);

   const auto issuer_fees = pay_market_fees(&seller, receives.asset_id(*this), receives, is_maker);

   auto order_receives = receives - issuer_fees;
   pay_order( seller, order_receives, pays );

   push_applied_operation( fill_order_operation( order.id, order.seller, pays, receives,
                                                 issuer_fees, fill_price, is_maker ) );

   // BSIP85: Maker order creation fee discount, https://github.com/bitshares/bsips/blob/master/bsip-0085.md
   //   if the order creation fee was paid in BTS,
   //     return round_down(deferred_fee * maker_fee_discount_percent) to the owner,
   //     then process the remaining deferred fee as before;
   //   if the order creation fee was paid in another asset,
   //     return round_down(deferred_paid_fee * maker_fee_discount_percent) to the owner,
   //     return round_down(deferred_fee * maker_fee_discount_percent) to the fee pool of the asset,
   //     then process the remaining deferred fee and deferred paid fee as before.
   const uint16_t maker_discount_percent = get_global_properties().parameters.get_maker_fee_discount_percent();

   // Save local copies for calculation
   share_type deferred_fee = order.deferred_fee;
   share_type deferred_paid_fee = order.deferred_paid_fee.amount;

   // conditional because cheap integer comparison may allow us to avoid two expensive modify() and object lookups
   if( order.deferred_paid_fee.amount > 0 ) // implies head_block_time() > HARDFORK_CORE_604_TIME
   {
      share_type fee_pool_refund = 0;
      if( is_maker && maker_discount_percent > 0 )
      {
         share_type refund = detail::calculate_percent( deferred_paid_fee, maker_discount_percent );
         // Note: it's possible that the deferred_paid_fee is very small,
         //       which can result in a zero refund due to rounding issue,
         //       in this case, no refund to the fee pool
         if( refund > 0 )
         {
            FC_ASSERT( refund <= deferred_paid_fee, "Internal error" );
            adjust_balance( order.seller, asset(refund, order.deferred_paid_fee.asset_id) );
            deferred_paid_fee -= refund;

            // deferred_fee might be positive too
            FC_ASSERT( deferred_fee > 0, "Internal error" );
            fee_pool_refund = detail::calculate_percent( deferred_fee, maker_discount_percent );
            FC_ASSERT( fee_pool_refund <= deferred_fee, "Internal error" );
            deferred_fee -= fee_pool_refund;
         }
      }

      const auto& fee_asset_dyn_data = order.deferred_paid_fee.asset_id(*this).dynamic_asset_data_id(*this);
      modify( fee_asset_dyn_data, [deferred_paid_fee,fee_pool_refund](asset_dynamic_data_object& addo) {
         addo.accumulated_fees += deferred_paid_fee;
         addo.fee_pool += fee_pool_refund;
      });
   }

   if( order.deferred_fee > 0 )
   {
      if( order.deferred_paid_fee.amount <= 0 // paid in CORE, or before HF 604
            && is_maker && maker_discount_percent > 0 )
      {
         share_type refund = detail::calculate_percent( deferred_fee, maker_discount_percent );
         if( refund > 0 )
         {
            FC_ASSERT( refund <= deferred_fee, "Internal error" );
            adjust_balance( order.seller, asset(refund, asset_id_type()) );
            deferred_fee -= refund;
         }
      }
      // else do nothing here, because we have already processed it above, or no need to process

      if( deferred_fee > 0 )
      {
         modify( seller.statistics(*this), [deferred_fee,this]( account_statistics_object& statistics )
         {
            statistics.pay_fee( deferred_fee, get_global_properties().parameters.cashback_vesting_threshold );
         } );
      }
   }

   // Process on_fill for order_receives
   optional<limit_order_id_type> new_take_profit_order_id = process_limit_order_on_fill( order, order_receives );

   // If this order is fully filled
   if( pays == order.amount_for_sale() )
   {
      cleanup_and_remove_limit_order( order );
      return true;
   }

   // This order is partially filled
   if( new_take_profit_order_id.valid() ) // A new take profit order is created, link this order to it
   {
      modify( (*new_take_profit_order_id)(*this), [&order]( limit_order_object& loo ) {
         loo.take_profit_order_id = order.get_id();
      });
   }
   modify( order, [&pays,&new_take_profit_order_id]( limit_order_object& b ) {
      b.for_sale -= pays.amount;
      b.filled_amount += pays.amount.value;
      b.deferred_fee = 0;
      b.deferred_paid_fee.amount = 0;
      if( new_take_profit_order_id.valid() ) // A new take profit order is created, link it to this order
         b.take_profit_order_id = *new_take_profit_order_id;
   });
   if( cull_if_small )
      return maybe_cull_small_order( *this, order );
   return false;
} FC_CAPTURE_AND_RETHROW( (pays)(receives) ) } // GCOVR_EXCL_LINE

/***
 * @brief fill a call order in the specified amounts
 * @param order the call order
 * @param pays What the call order will give to the other party (collateral)
 * @param receives what the call order will receive from the other party (debt)
 * @param fill_price the price at which the call order will execute
 * @param is_maker TRUE if the call order is the maker, FALSE if it is the taker
 * @param margin_call_fee Margin call fees paid in collateral asset
 * @returns TRUE if the call order was completely filled
 */
bool database::fill_call_order( const call_order_object& order, const asset& pays, const asset& receives,
      const price& fill_price, const bool is_maker, const asset& margin_call_fee, bool reduce_current_supply )
{ try {
   FC_ASSERT( order.debt_type() == receives.asset_id );
   FC_ASSERT( order.collateral_type() == pays.asset_id );
   FC_ASSERT( order.collateral >= pays.amount );

   // TODO pass in mia and bitasset_data for better performance
   const asset_object& mia = receives.asset_id(*this);
   FC_ASSERT( mia.is_market_issued() );
   const asset_bitasset_data_object& bitasset = mia.bitasset_data(*this);

   optional<asset> collateral_freed;
   // adjust the order
   modify( order, [&]( call_order_object& o ) {
         o.debt       -= receives.amount;
         o.collateral -= pays.amount;
         if( o.debt == 0 ) // is the whole debt paid?
         {
            collateral_freed = o.get_collateral();
            o.collateral = 0;
         }
         else // the debt was not completely paid
         {
            auto maint_time = get_dynamic_global_properties().next_maintenance_time;
            // update call_price after core-343 hard fork,
            // but don't update call_price after core-1270 hard fork
            if( maint_time <= HARDFORK_CORE_1270_TIME && maint_time > HARDFORK_CORE_343_TIME )
            {
               o.call_price = price::call_price( o.get_debt(), o.get_collateral(),
                     bitasset.current_feed.maintenance_collateral_ratio );
            }
         }
      });

   // update current supply
   if( reduce_current_supply )
   {
      const asset_dynamic_data_object& mia_ddo = mia.dynamic_asset_data_id(*this);
      modify( mia_ddo, [&receives]( asset_dynamic_data_object& ao ){
         ao.current_supply -= receives.amount;
      });
   }

   // If the whole debt is paid, adjust borrower's collateral balance
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

   // BSIP74: Accumulate the collateral-denominated fee
   if (margin_call_fee.amount.value != 0)
      mia.accumulate_fee(*this, margin_call_fee);

   // virtual operation for account history
   push_applied_operation( fill_order_operation( order.id, order.borrower, pays, receives,
         margin_call_fee, fill_price, is_maker ) );

   // Call order completely filled, remove it
   if( collateral_freed.valid() )
      remove( order );

   return collateral_freed.valid();
} FC_CAPTURE_AND_RETHROW( (pays)(receives) ) } // GCOVR_EXCL_LINE

/***
 * @brief fullfill a settle order in the specified amounts
 *
 * @details Called from database::match(), this coordinates exchange of debt asset X held in the
 *    settle order for collateral asset Y held in a call order, and routes fees.  Note that we
 *    don't touch the call order directly, as match() handles this via a separate call to
 *    fill_call_order().  We are told exactly how much X and Y to exchange, based on details of
 *    order matching determined higher up the call chain. Thus it is possible that the settle
 *    order is not completely satisfied at the conclusion of this function.
 *
 * @param settle the force_settlement object
 * @param pays the quantity of market-issued debt asset X which the settler will yield in this
 *    round (may be less than the full amount indicated in settle object)
 * @param receives the quantity of collateral asset Y which the settler will receive in
 *    exchange for X
 * @param fill_price the price at which the settle order will execute (not used - passed through
 *    to virtual operation)
 * @param is_maker TRUE if the settle order is the maker, FALSE if it is the taker (passed
 *    through to virtual operation)
 * @returns TRUE if the settle order was completely filled, FALSE if only partially filled
 */
bool database::fill_settle_order( const force_settlement_object& settle, const asset& pays, const asset& receives,
                                  const price& fill_price, bool is_maker, bool pay_force_settle_fee )
{ try {
   bool filled = false;

   const account_object* settle_owner_ptr = nullptr;
   // The owner of the settle order pays market fees to the issuer of the collateral asset.
   // After HF core-1780, these fees are shared to the referral program, which is flagged to
   // pay_market_fees by setting settle_owner_ptr non-null.
   //
   // TODO Check whether the HF check can be removed after the HF.
   //      Note: even if logically it can be removed, perhaps the removal will lead to a small performance
   //            loss. Needs testing.
   if( head_block_time() >= HARDFORK_CORE_1780_TIME )
      settle_owner_ptr = &settle.owner(*this);
   // Compute and pay the market fees:
   asset market_fees = pay_market_fees( settle_owner_ptr, get(receives.asset_id), receives, is_maker );

   // Issuer of the settled smartcoin asset lays claim to a force-settlement fee (BSIP87), but
   // note that fee is denominated in collateral asset, not the debt asset.  Asset object of
   // debt asset is passed to the pay function so it knows where to put the fee. Note that
   // amount of collateral asset upon which fee is assessed is reduced by market_fees already
   // paid to prevent the total fee exceeding total collateral.
   asset force_settle_fees = pay_force_settle_fee
                             ? pay_force_settle_fees( get(pays.asset_id), receives - market_fees )
                             : asset( 0, receives.asset_id );

   auto total_collateral_denominated_fees = market_fees + force_settle_fees;

   // If we don't consume entire settle order:
   if( pays < settle.balance )
   {
      modify(settle, [&pays](force_settlement_object& s) {
         s.balance -= pays;
      });
   } else {
      filled = true;
   }
   // Give released collateral not already taken as fees to settle order owner:
   adjust_balance(settle.owner, receives - total_collateral_denominated_fees);

   assert( pays.asset_id != receives.asset_id );
   push_applied_operation( fill_order_operation( settle.id, settle.owner, pays, receives,
                                                 total_collateral_denominated_fees, fill_price, is_maker ) );

   if (filled)
      remove(settle);

   return filled;

} FC_CAPTURE_AND_RETHROW( (pays)(receives) ) } // GCOVR_EXCL_LINE

/**
 *  Starting with the least collateralized orders, fill them if their
 *  call price is above the max(lowest bid,call_limit).
 *
 *  This method will return true if it filled a short or limit
 *
 *  @param mia - the market issued asset that should be called.
 *  @param enable_black_swan - when adjusting collateral, triggering a black swan is invalid and will throw
 *                             if enable_black_swan is not set to true.
 *  @param for_new_limit_order - true if this function is called when matching call orders with a new
 *     limit order. (Only relevent before hardfork 625. apply_order_before_hardfork_625() is only
 *     function that calls this with for_new_limit_order true.)
 *  @param bitasset_ptr - an optional pointer to the bitasset_data object of the asset
 *  @param mute_exceptions - whether to mute exceptions in a special case
 *  @param skip_matching_settle_orders - whether to skip matching call orders with force settlements
 *
 *  @return true if a margin call was executed.
 */
bool database::check_call_orders( const asset_object& mia, bool enable_black_swan, bool for_new_limit_order,
                                  const asset_bitasset_data_object* bitasset_ptr,
                                  bool mute_exceptions, bool skip_matching_settle_orders )
{ try {
    const auto& dyn_prop = get_dynamic_global_properties();
    auto maint_time = dyn_prop.next_maintenance_time;
    if( for_new_limit_order )
       FC_ASSERT( maint_time <= HARDFORK_CORE_625_TIME ); // `for_new_limit_order` is only true before HF 338 / 625

    if( !mia.is_market_issued() ) return false;

    const asset_bitasset_data_object& bitasset = ( bitasset_ptr ? *bitasset_ptr : mia.bitasset_data(*this) );

    // price feeds can cause black swans in prediction markets
    // The hardfork check may be able to be removed after the hardfork date
    // if check_for_blackswan never triggered a black swan on a prediction market.
    // NOTE: check_for_blackswan returning true does not always mean a black
    // swan was triggered.
    if ( maint_time >= HARDFORK_CORE_460_TIME && bitasset.is_prediction_market )
       return false;

    using bsrm_type = bitasset_options::black_swan_response_type;
    const auto bsrm = bitasset.get_black_swan_response_method();

    // Only check for black swan here if BSRM is not individual settlement
    if( bsrm_type::individual_settlement_to_fund != bsrm
          && bsrm_type::individual_settlement_to_order != bsrm
          && check_for_blackswan( mia, enable_black_swan, &bitasset ) )
       return false;

    if( bitasset.is_prediction_market ) return false;
    if( bitasset.current_feed.settlement_price.is_null() ) return false;

    const limit_order_index& limit_index = get_index_type<limit_order_index>();
    const auto& limit_price_index = limit_index.indices().get<by_price>();

    bool before_core_hardfork_1270 = ( maint_time <= HARDFORK_CORE_1270_TIME ); // call price caching issue
    bool after_core_hardfork_2481 = HARDFORK_CORE_2481_PASSED( maint_time ); // Match settle orders with margin calls

    // Looking for limit orders selling the most USD for the least CORE.
    auto max_price = price::max( bitasset.asset_id, bitasset.options.short_backing_asset );
    // Stop when limit orders are selling too little USD for too much CORE.
    // Note that since BSIP74, margin calls offer somewhat less CORE per USD
    // if the issuer claims a Margin Call Fee.
    auto min_price = before_core_hardfork_1270 ?
                         bitasset.current_feed.max_short_squeeze_price_before_hf_1270()
                       : bitasset.get_margin_call_order_price();

    // NOTE limit_price_index is sorted from greatest to least
    auto limit_itr = limit_price_index.lower_bound( max_price );
    auto limit_end = limit_price_index.upper_bound( min_price );

    // Before the core-2481 hf, only check limit orders
    if( !after_core_hardfork_2481 && limit_itr == limit_end )
       return false;

    const call_order_index& call_index = get_index_type<call_order_index>();
    const auto& call_price_index = call_index.indices().get<by_price>();
    // Note: it is safe to iterate here even if there is no call order due to individual settlements
    const auto& call_collateral_index = call_index.indices().get<by_collateral>();

    auto call_min = price::min( bitasset.options.short_backing_asset, bitasset.asset_id );
    auto call_max = price::max( bitasset.options.short_backing_asset, bitasset.asset_id );

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
    bool margin_called = false;         // toggles true once/if we actually execute a margin call

    auto head_time = head_block_time();
    auto head_num = head_block_num();

    bool before_hardfork_615 = ( head_time < HARDFORK_615_TIME );
    bool after_hardfork_436 = ( head_time > HARDFORK_436_TIME );

    bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding
    bool before_core_hardfork_343 = ( maint_time <= HARDFORK_CORE_343_TIME ); // update call_price on partial fill
    bool before_core_hardfork_453 = ( maint_time <= HARDFORK_CORE_453_TIME ); // multiple matching issue
    bool before_core_hardfork_606 = ( maint_time <= HARDFORK_CORE_606_TIME ); // feed always trigger call
    bool before_core_hardfork_834 = ( maint_time <= HARDFORK_CORE_834_TIME ); // target collateral ratio option

    bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

    auto has_call_order = [ before_core_hardfork_1270,
                            &call_collateral_itr,&call_collateral_end,
                            &call_price_itr,&call_price_end ]()
    {
       return before_core_hardfork_1270 ? ( call_price_itr != call_price_end )
                                        : ( call_collateral_itr != call_collateral_end );
    };

    bool update_current_feed = ( bsrm_type::no_settlement == bsrm && bitasset.is_current_feed_price_capped() );

    const auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();

    while( has_call_order() )
    {
      // check for blackswan first // TODO perhaps improve performance by passing in iterators
      bool settled_some = check_for_blackswan( mia, enable_black_swan, &bitasset );
      if( bitasset.is_globally_settled() )
         return margin_called;

      if( settled_some ) // which implies that BSRM is individual settlement to fund or to order
      {
         call_collateral_itr = call_collateral_index.lower_bound( call_min );
         if( call_collateral_itr == call_collateral_end ) // no call order left
         {
            check_settled_debt_order( bitasset );
            return true;
         }
         margin_called = true;
         if( bsrm_type::individual_settlement_to_fund == bsrm )
            limit_end = limit_price_index.upper_bound( bitasset.get_margin_call_order_price() );
      }

      // be here, there exists at least one call order
      const call_order_object& call_order = ( before_core_hardfork_1270 ? *call_price_itr : *call_collateral_itr );

      // Feed protected (don't call if CR>MCR) https://github.com/cryptonomex/graphene/issues/436
      bool feed_protected = before_core_hardfork_1270 ?
                            ( after_hardfork_436 && bitasset.current_feed.settlement_price > ~call_order.call_price )
                          : ( bitasset.current_maintenance_collateralization < call_order.collateralization() );
      if( feed_protected )
      {
         check_settled_debt_order( bitasset );
         return margin_called;
      }

      // match call orders with limit orders
      if( limit_itr != limit_end )
      {
         const limit_order_object& limit_order = *limit_itr;

         price match_price  = limit_order.sell_price;
         // There was a check `match_price.validate();` here, which is removed now because it always passes

         // Old rule: margin calls can only buy high https://github.com/bitshares/bitshares-core/issues/606
         if( before_core_hardfork_606 && match_price > ~call_order.call_price )
            return margin_called;

         margin_called = true;

         price call_pays_price = match_price * bitasset.get_margin_call_pays_ratio();
         // Since BSIP74, the call "pays" a bit more collateral per debt than the match price, with the
         // excess being kept by the asset issuer as a margin call fee. In what follows, we use
         // call_pays_price for the black swan check, and for the TCR, but we still use the match_price,
         // of course, to determine what the limit order receives.  Note margin_call_pays_ratio() returns
         // 1/1 if margin_call_fee_ratio is unset (i.e. before BSIP74), so hardfork check is implicit.

         // Although we checked for black swan above, we do one more check to ensure the call order can
         // pay the amount of collateral which we intend to take from it (including margin call fee).
         // TODO refactor code for better performance and readability, perhaps extract the new logic to a new
         //      function and call it after hf_1270, hf_bsip74 or hf_2481.
         auto usd_to_buy = call_order.get_debt();
         if( !after_core_hardfork_2481 && ( usd_to_buy * call_pays_price ) > call_order.get_collateral() )
         {
            // Trigger black swan
            elog( "black swan detected on asset ${symbol} (${id}) at block ${b}",
                  ("id",bitasset.asset_id)("symbol",mia.symbol)("b",head_num) );
            edump((enable_black_swan));
            FC_ASSERT( enable_black_swan );
            globally_settle_asset(mia, bitasset.current_feed.settlement_price );
            return true;
         }

         if( !before_core_hardfork_1270 )
         {
            auto settle_price = after_core_hardfork_2582 ? bitasset.median_feed.settlement_price
                                                         : bitasset.current_feed.settlement_price;
            usd_to_buy.amount = call_order.get_max_debt_to_cover( call_pays_price,
                                                                settle_price,
                                                                bitasset.current_feed.maintenance_collateral_ratio,
                                                                bitasset.current_maintenance_collateralization );
         }
         else if( !before_core_hardfork_834 )
         {
            usd_to_buy.amount = call_order.get_max_debt_to_cover( call_pays_price,
                                                                bitasset.current_feed.settlement_price,
                                                                bitasset.current_feed.maintenance_collateral_ratio );
         }

         asset usd_for_sale = limit_order.amount_for_sale();
         asset call_pays, call_receives, limit_pays, limit_receives;

         struct UndercollateralizationException {};
         try { // throws UndercollateralizationException if the call order is undercollateralized

            bool filled_call = false;

            if( usd_to_buy > usd_for_sale )
            {  // fill order
               limit_receives  = usd_for_sale * match_price; // round down, in favor of call order

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
                  call_receives = limit_receives.multiply_and_round_up( match_price );

               if( !after_core_hardfork_2481 )
                  // TODO add tests about CR change
                  call_pays = usd_for_sale * call_pays_price; // (same as match_price until BSIP-74)
               else
               {
                  call_pays = call_receives * call_pays_price; // calculate with updated call_receives
                  if( call_pays.amount >= call_order.collateral )
                     throw UndercollateralizationException();
                  auto new_collateral = call_order.get_collateral() - call_pays;
                  auto new_debt = call_order.get_debt() - call_receives; // the result is positive due to math
                  if( ( new_collateral / new_debt ) < call_order.collateralization() ) // if CR would decrease
                     throw UndercollateralizationException();
               }

               filled_limit = true;

            } else { // fill call, could be partial fill due to TCR
               call_receives  = usd_to_buy;

               if( before_core_hardfork_342 )
               {
                  limit_receives = usd_to_buy * match_price; // round down, in favor of call order
                  call_pays = limit_receives;
               } else {
                  call_pays      = usd_to_buy.multiply_and_round_up( call_pays_price ); // BSIP74; excess is fee.
                  // Note: Due to different rounding, this could potentialy be
                  //       one satoshi more than the blackswan check above
                  if( call_pays.amount > call_order.collateral )
                  {
                     if( after_core_hardfork_2481 )
                        throw UndercollateralizationException();
                     if( mute_exceptions )
                        call_pays.amount = call_order.collateral;
                  }
                  // Note: if it is a partial fill due to TCR, the math guarantees that the new CR will be higher
                  //       than the old CR, so no additional check for potential blackswan here

                  limit_receives = usd_to_buy.multiply_and_round_up( match_price ); // round up, favors limit order
                  if( limit_receives.amount > call_order.collateral ) // implies !after_hf_2481
                     limit_receives.amount = call_order.collateral;
                  // Note: here we don't re-assign call_receives with (orders_receives * match_price) to receive more
                  //       debt asset, it means the call order could be receiving a bit too much less than its value.
                  //       It is a sad thing for the call order, but it is the rule
                  //       -- when a call order is margin called, it does not get more than it borrowed.
                  //       On the other hand, if the call order is not being closed (due to TCR),
                  //       it means get_max_debt_to_cover() did not return a perfect result, maybe we can improve it.
               }

               filled_call = true; // this is safe, since BSIP38 (hard fork core-834) depends on BSIP31 (hf core-343)

               if( usd_to_buy == usd_for_sale )
                  filled_limit = true;
               else if( filled_limit && before_hardfork_615 )
                  //NOTE: Multiple limit match problem (see issue 453, yes this happened)
                  _issue_453_affected_assets.insert( bitasset.asset_id );
            }
            limit_pays = call_receives;

            // BSIP74: Margin call fee
            FC_ASSERT(call_pays >= limit_receives);
            const asset margin_call_fee = call_pays - limit_receives;

            if( filled_call && before_core_hardfork_343 )
               ++call_price_itr;

            // when for_new_limit_order is true, the call order is maker, otherwise the call order is taker
            fill_call_order( call_order, call_pays, call_receives, match_price, for_new_limit_order, margin_call_fee);

            // Update current_feed after filled call order if needed
            if( update_current_feed )
            {
               update_bitasset_current_feed( bitasset, true );
               limit_end = limit_price_index.upper_bound( bitasset.get_margin_call_order_price() );
               update_current_feed = bitasset.is_current_feed_price_capped();
            }

            if( !before_core_hardfork_1270 )
               call_collateral_itr = call_collateral_index.lower_bound( call_min );
            else if( !before_core_hardfork_343 )
               call_price_itr = call_price_index.lower_bound( call_min );

            auto next_limit_itr = std::next( limit_itr );
            // when for_new_limit_order is true, the limit order is taker, otherwise the limit order is maker
            bool really_filled = fill_limit_order( limit_order, limit_pays, limit_receives, true,
                                                   match_price, !for_new_limit_order );
            if( really_filled || ( filled_limit && before_core_hardfork_453 ) )
               limit_itr = next_limit_itr;

            continue; // check for blackswan again

         } catch( const UndercollateralizationException& ) {
            // Nothing to do here
         }
      } // if there is a matching limit order

      // be here, it is unable to fill a limit order due to undercollateralization (and there is a force settlement),
      //          or there is no matching limit order due to MSSR, or no limit order at all

      // If no need to process force settlements, we return
      // Note: before core-2481/2467 hf, or BSRM is no_settlement and processing a new force settlement
      if( skip_matching_settle_orders || !after_core_hardfork_2481 )
         return margin_called;

      // If no force settlements, we return
      // Note: there is no matching limit order due to MSSR, or no limit order at all,
      //       in either case, the settled debt order can't be matched
      auto settle_itr = settlement_index.lower_bound( bitasset.asset_id );
      if( settle_itr == settlement_index.end() || settle_itr->balance.asset_id != bitasset.asset_id )
         return margin_called;

      // Check margin calls against force settlements
      // Note: we always need to recheck limit orders after processed call-settle match,
      //       in case when the least collateralized short was undercollateralized.
      if( match_force_settlements( bitasset ) )
      {
         margin_called = true;
         call_collateral_itr = call_collateral_index.lower_bound( call_min );
         if( update_current_feed )
         {
            // Note: we do not call update_bitasset_current_feed() here,
            //       because it's called in match_impl() in match() in match_force_settlements()
            limit_end = limit_price_index.upper_bound( bitasset.get_margin_call_order_price() );
            update_current_feed = bitasset.is_current_feed_price_capped();
         }
      }
      // else : no more force settlements, or feed protected, both will be handled in the next loop
   } // while there exists a call order
   check_settled_debt_order( bitasset );
   return margin_called;
} FC_CAPTURE_AND_RETHROW() } // GCOVR_EXCL_LINE

bool database::match_force_settlements( const asset_bitasset_data_object& bitasset )
{
   // Defensive checks
   auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   // GCOVR_EXCL_START
   // Defensive code, normally none of these should fail
   FC_ASSERT( HARDFORK_CORE_2481_PASSED( maint_time ), "Internal error: hard fork core-2481 not passed" );
   FC_ASSERT( !bitasset.is_prediction_market, "Internal error: asset is a prediction market" );
   FC_ASSERT( !bitasset.is_globally_settled(), "Internal error: asset is globally settled already" );
   FC_ASSERT( !bitasset.current_feed.settlement_price.is_null(), "Internal error: no sufficient price feeds" );
   // GCOVR_EXCL_STOP

   auto head_time = head_block_time();
   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   const auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
   auto settle_itr = settlement_index.lower_bound( bitasset.asset_id );
   auto settle_end = settlement_index.upper_bound( bitasset.asset_id );

   // Note: it is safe to iterate here even if there is no call order due to individual settlements
   const auto& call_collateral_index = get_index_type<call_order_index>().indices().get<by_collateral>();
   auto call_min = price::min( bitasset.options.short_backing_asset, bitasset.asset_id );
   auto call_max = price::max( bitasset.options.short_backing_asset, bitasset.asset_id );
   auto call_itr = call_collateral_index.lower_bound( call_min );
   auto call_end = call_collateral_index.upper_bound( call_max );

   // Price at which margin calls sit on the books.
   // It is the MCOP, which may deviate from MSSP due to MCFR.
   // It is in debt/collateral .
   price call_match_price = bitasset.get_margin_call_order_price();
   // Price margin call actually relinquishes collateral at. Equals the MSSP and it may
   // differ from call_match_price if there is a Margin Call Fee.
   // It is in debt/collateral .
   price call_pays_price = bitasset.current_feed.max_short_squeeze_price();

   while( settle_itr != settle_end && call_itr != call_end )
   {
      const force_settlement_object& settle_order = *settle_itr;
      const call_order_object& call_order = *call_itr;

      // Feed protected (don't call if CR>MCR) https://github.com/cryptonomex/graphene/issues/436
      if( bitasset.current_maintenance_collateralization < call_order.collateralization() )
         return false;

      // TCR applies here
      auto settle_price = after_core_hardfork_2582 ? bitasset.median_feed.settlement_price
                                                   : bitasset.current_feed.settlement_price;
      asset max_debt_to_cover( call_order.get_max_debt_to_cover( call_pays_price,
                                                       settle_price,
                                                       bitasset.current_feed.maintenance_collateral_ratio,
                                                       bitasset.current_maintenance_collateralization ),
                               bitasset.asset_id );

      // Note: if the call order's CR is too low, it is probably unable to fill at call_pays_price.
      //       In this case, the call order pays at its CR, the settle order may receive less due to margin call fee.
      //       It is processed inside the function.
      auto result = match( call_order, settle_order, call_pays_price, bitasset, max_debt_to_cover, call_match_price );

      // if result.amount > 0, it means the call order got updated or removed
      // in this case, we need to check limit orders first, so we return
      if( result.amount > 0 )
         return true;
      // else : result.amount == 0, it means the settle order got canceled directly and the call order did not change

      settle_itr = settlement_index.lower_bound( bitasset.asset_id );
      call_itr = call_collateral_index.lower_bound( call_min );
   }
   return false;
}

void database::check_settled_debt_order( const asset_bitasset_data_object& bitasset )
{
   const auto& head_time = head_block_time();
   bool after_core_hardfork_2591 = HARDFORK_CORE_2591_PASSED( head_time ); // Tighter peg (fill debt order at MCOP)
   if( !after_core_hardfork_2591 )
      return;

   using bsrm_type = bitasset_options::black_swan_response_type;
   const auto bsrm = bitasset.get_black_swan_response_method();
   if( bsrm_type::individual_settlement_to_order != bsrm )
      return;

   const limit_order_object* limit_ptr = find_settled_debt_order( bitasset.asset_id );
   if( !limit_ptr )
      return;

   const limit_order_index& limit_index = get_index_type<limit_order_index>();
   const auto& limit_price_index = limit_index.indices().get<by_price>();

   // Looking for limit orders selling the most USD for the least CORE.
   auto max_price = price::max( bitasset.asset_id, bitasset.options.short_backing_asset );
   // Stop when limit orders are selling too little USD for too much CORE.
   auto min_price = ~limit_ptr->sell_price;

   // NOTE limit_price_index is sorted from greatest to least
   auto limit_itr = limit_price_index.lower_bound( max_price );
   auto limit_end = limit_price_index.upper_bound( min_price );

   bool finished = false; // whether the settled debt order is gone
   while( !finished && limit_itr != limit_end )
   {
      const limit_order_object& matching_limit_order = *limit_itr;
      ++limit_itr;
      price old_price = limit_ptr->sell_price;
      finished = ( match_settled_debt_limit( *limit_ptr, matching_limit_order, matching_limit_order.sell_price )
                   != match_result_type::only_maker_filled );
      if( !finished && old_price != limit_ptr->sell_price )
         limit_end = limit_price_index.upper_bound( ~limit_ptr->sell_price );
   }
}

void database::pay_order( const account_object& receiver, const asset& receives, const asset& pays )
{
   if( pays.asset_id == asset_id_type() )
   {
      const auto& stats = receiver.statistics(*this);
      modify( stats, [&pays]( account_statistics_object& b ){
         b.total_core_in_orders -= pays.amount;
      });
   }
   adjust_balance(receiver.get_id(), receives);
}

asset database::calculate_market_fee( const asset_object& trade_asset, const asset& trade_amount,
                                      const bool& is_maker )const
{
   assert( trade_asset.id == trade_amount.asset_id );

   if( !trade_asset.charges_market_fees() )
      return trade_asset.amount(0);
   // Optimization: The fee is zero if the order is a maker, and the maker fee percent is 0%
   if( is_maker && trade_asset.options.market_fee_percent == 0 )
      return trade_asset.amount(0);

   // Optimization: The fee is zero if the order is a taker, and the taker fee percent is 0%
   const optional<uint16_t>& taker_fee_percent = trade_asset.options.extensions.value.taker_fee_percent;
   if(!is_maker && taker_fee_percent.valid() && *taker_fee_percent == 0)
      return trade_asset.amount(0);

   uint16_t fee_percent;
   if (is_maker) {
      // Maker orders are charged the maker fee percent
      fee_percent = trade_asset.options.market_fee_percent;
   } else {
      // Taker orders are charged the taker fee percent if they are valid.  Otherwise, the maker fee percent.
      fee_percent = taker_fee_percent.valid() ? *taker_fee_percent : trade_asset.options.market_fee_percent;
   }

   auto value = detail::calculate_percent(trade_amount.amount, fee_percent);
   asset percent_fee = trade_asset.amount(value);

   if( percent_fee.amount > trade_asset.options.max_market_fee )
      percent_fee.amount = trade_asset.options.max_market_fee;

   return percent_fee;
}


asset database::pay_market_fees(const account_object* seller, const asset_object& recv_asset, const asset& receives,
                                const bool& is_maker, const optional<asset>& calculated_market_fees )
{
   const auto market_fees = ( calculated_market_fees.valid() ? *calculated_market_fees
                                    : calculate_market_fee( recv_asset, receives, is_maker ) );
   auto issuer_fees = market_fees;
   FC_ASSERT( issuer_fees <= receives, "Market fee shouldn't be greater than receives");
   //Don't dirty undo state if not actually collecting any fees
   if ( issuer_fees.amount > 0 )
   {
      // Share market fees to the network
      const uint16_t network_percent = get_global_properties().parameters.get_market_fee_network_percent();
      if( network_percent > 0 )
      {
         const auto network_fees_amt = detail::calculate_percent( issuer_fees.amount, network_percent );
         FC_ASSERT( network_fees_amt <= issuer_fees.amount,
                    "Fee shared to the network shouldn't be greater than total market fee" );
         if( network_fees_amt > 0 )
         {
            const asset network_fees = recv_asset.amount( network_fees_amt );
            deposit_market_fee_vesting_balance( GRAPHENE_COMMITTEE_ACCOUNT, network_fees );
            issuer_fees -= network_fees;
         }
      }
   }

   // Process the remaining fees
   if ( issuer_fees.amount > 0 )
   {
      // calculate and pay rewards
      asset reward = recv_asset.amount(0);

      auto is_rewards_allowed = [&recv_asset, seller]() {
         if ( !seller )
            return false;
         const auto &white_list = recv_asset.options.extensions.value.whitelist_market_fee_sharing;
         return ( !white_list || (*white_list).empty()
               || ( (*white_list).find(seller->registrar) != (*white_list).end() ) );
      };

      if ( is_rewards_allowed() )
      {
         const auto reward_percent = recv_asset.options.extensions.value.reward_percent;
         if ( reward_percent.valid() && (*reward_percent) > 0 )
         {
            const auto reward_value = detail::calculate_percent(issuer_fees.amount, *reward_percent);
            if ( reward_value > 0 && is_authorized_asset(*this, seller->registrar(*this), recv_asset) )
            {
               reward = recv_asset.amount(reward_value);
               // TODO after hf_1774, remove the `if` check, keep the code in `else`
               if( head_block_time() < HARDFORK_1774_TIME ){
                  FC_ASSERT( reward < issuer_fees, "Market reward should be less than issuer fees");
               }
               else{
                  FC_ASSERT( reward <= issuer_fees, "Market reward should not be greater than issuer fees");
               }
               // cut referrer percent from reward
               auto registrar_reward = reward;

               auto registrar = seller->registrar;
               auto referrer = seller->referrer;

               // After HF core-1800, for funds going to temp-account, redirect to committee-account
               if( head_block_time() >= HARDFORK_CORE_1800_TIME )
               {
                  if( registrar == GRAPHENE_TEMP_ACCOUNT )
                     registrar = GRAPHENE_COMMITTEE_ACCOUNT;
                  if( referrer == GRAPHENE_TEMP_ACCOUNT )
                     referrer = GRAPHENE_COMMITTEE_ACCOUNT;
               }

               if( referrer != registrar )
               {
                  const auto referrer_rewards_value = detail::calculate_percent( reward.amount,
                                                                 seller->referrer_rewards_percentage );

                  if ( referrer_rewards_value > 0 && is_authorized_asset(*this, referrer(*this), recv_asset) )
                  {
                     FC_ASSERT ( referrer_rewards_value <= reward.amount.value,
                                 "Referrer reward shouldn't be greater than total reward" );
                     const asset referrer_reward = recv_asset.amount(referrer_rewards_value);
                     registrar_reward -= referrer_reward;
                     deposit_market_fee_vesting_balance(referrer, referrer_reward);
                  }
               }
               if( registrar_reward.amount > 0 )
                  deposit_market_fee_vesting_balance(registrar, registrar_reward);
            }
         }
      }

      if( issuer_fees.amount > reward.amount )
      {
         const auto& recv_dyn_data = recv_asset.dynamic_asset_data_id(*this);
         modify( recv_dyn_data, [&issuer_fees, &reward]( asset_dynamic_data_object& obj ){
            obj.accumulated_fees += issuer_fees.amount - reward.amount;
         });
      }
   }

   return market_fees;
}

/***
 * @brief Calculate force-settlement fee and give it to issuer of the settled asset
 * @param collecting_asset the smart asset object which should receive the fee
 * @param collat_receives the amount of collateral the settler would expect to receive absent this fee
 *     (fee is computed as a percentage of this amount)
 * @return asset denoting the amount of fee collected
 */
asset database::pay_force_settle_fees(const asset_object& collecting_asset, const asset& collat_receives)
{
   FC_ASSERT( collecting_asset.get_id() != collat_receives.asset_id );

   const bitasset_options& collecting_bitasset_opts = collecting_asset.bitasset_data(*this).options;

   if( !collecting_bitasset_opts.extensions.value.force_settle_fee_percent.valid()
         || *collecting_bitasset_opts.extensions.value.force_settle_fee_percent == 0 )
      return asset{ 0, collat_receives.asset_id };

   auto value = detail::calculate_percent(collat_receives.amount,
                                          *collecting_bitasset_opts.extensions.value.force_settle_fee_percent);
   asset settle_fee( value, collat_receives.asset_id );

   // Deposit fee in asset's dynamic data object:
   if( value > 0) {
      collecting_asset.accumulate_fee(*this, settle_fee);
   }
   return settle_fee;
}

} }
