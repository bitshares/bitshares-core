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
#include <graphene/chain/db_with.hpp>

#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/credit_offer_object.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/htlc_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/ticket_object.hpp>
#include <graphene/chain/transaction_history_object.hpp>
#include <graphene/chain/withdraw_permission_object.hpp>
#include <graphene/chain/witness_object.hpp>

#include <graphene/protocol/fee_schedule.hpp>

namespace graphene { namespace chain {

void database::update_global_dynamic_data( const signed_block& b, const uint32_t missed_blocks )
{
   const dynamic_global_property_object& _dgp = get_dynamic_global_properties();

   // dynamic global properties updating
   modify( _dgp, [&b,this,missed_blocks]( dynamic_global_property_object& dgp ){
      const uint32_t block_num = b.block_num();
      if( BOOST_UNLIKELY( block_num == 1 ) )
         dgp.recently_missed_count = 0;
      else if( !_checkpoints.empty() && _checkpoints.rbegin()->first >= block_num )
         dgp.recently_missed_count = 0;
      else if( missed_blocks )
         dgp.recently_missed_count += GRAPHENE_RECENTLY_MISSED_COUNT_INCREMENT*missed_blocks;
      else if( dgp.recently_missed_count > GRAPHENE_RECENTLY_MISSED_COUNT_INCREMENT )
         dgp.recently_missed_count -= GRAPHENE_RECENTLY_MISSED_COUNT_DECREMENT;
      else if( dgp.recently_missed_count > 0 )
         dgp.recently_missed_count--;

      dgp.head_block_number = block_num;
      dgp.head_block_id = b.id();
      dgp.time = b.timestamp;
      dgp.current_witness = b.witness;
      dgp.recent_slots_filled = (
           (dgp.recent_slots_filled << 1)
           + 1) << missed_blocks;
      dgp.current_aslot += missed_blocks+1;
   });

   if( 0 == (get_node_properties().skip_flags & skip_undo_history_check) )
   {
      GRAPHENE_ASSERT( _dgp.head_block_number - _dgp.last_irreversible_block_num  < GRAPHENE_MAX_UNDO_HISTORY, undo_database_exception,
                 "The database does not have enough undo history to support a blockchain with so many missed blocks. "
                 "Please add a checkpoint if you would like to continue applying blocks beyond this point.",
                 ("last_irreversible_block_num",_dgp.last_irreversible_block_num)("head", _dgp.head_block_number)
                 ("recently_missed",_dgp.recently_missed_count)("max_undo",GRAPHENE_MAX_UNDO_HISTORY) );
   }

   _undo_db.set_max_size( _dgp.head_block_number - _dgp.last_irreversible_block_num + 1 );
   _fork_db.set_max_size( _dgp.head_block_number - _dgp.last_irreversible_block_num + 1 );
}

void database::update_signing_witness(const witness_object& signing_witness, const signed_block& new_block)
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();
   uint64_t new_block_aslot = dpo.current_aslot + get_slot_at_time( new_block.timestamp );

   share_type witness_pay = std::min( gpo.parameters.witness_pay_per_block, dpo.witness_budget );

   modify( dpo, [&]( dynamic_global_property_object& _dpo )
   {
      _dpo.witness_budget -= witness_pay;
   } );

   deposit_witness_pay( signing_witness, witness_pay );

   modify( signing_witness, [&]( witness_object& _wit )
   {
      _wit.last_aslot = new_block_aslot;
      _wit.last_confirmed_block_num = new_block.block_num();
   } );
}

void database::update_last_irreversible_block()
{
   const global_property_object& gpo = get_global_properties();
   const dynamic_global_property_object& dpo = get_dynamic_global_properties();

   // TODO for better performance, move this to db_maint, because only need to do it once per maintenance interval
   vector< const witness_object* > wit_objs;
   wit_objs.reserve( gpo.active_witnesses.size() );
   for( const witness_id_type& wid : gpo.active_witnesses )
      wit_objs.push_back( &(wid(*this)) );

   static_assert( GRAPHENE_IRREVERSIBLE_THRESHOLD > 0, "irreversible threshold must be nonzero" );

   // 1 1 1 2 2 2 2 2 2 2 -> 2     .3*10 = 3
   // 1 1 1 1 1 1 1 2 2 2 -> 1
   // 3 3 3 3 3 3 3 3 3 3 -> 3
   // 3 3 3 4 4 4 4 4 4 4 -> 4

   size_t offset = ((GRAPHENE_100_PERCENT - GRAPHENE_IRREVERSIBLE_THRESHOLD) * wit_objs.size() / GRAPHENE_100_PERCENT);

   std::nth_element( wit_objs.begin(), wit_objs.begin() + offset, wit_objs.end(),
      []( const witness_object* a, const witness_object* b )
      {
         return a->last_confirmed_block_num < b->last_confirmed_block_num;
      } );

   uint32_t new_last_irreversible_block_num = wit_objs[offset]->last_confirmed_block_num;

   if( new_last_irreversible_block_num > dpo.last_irreversible_block_num )
   {
      modify( dpo, [&]( dynamic_global_property_object& _dpo )
      {
         _dpo.last_irreversible_block_num = new_last_irreversible_block_num;
      } );
   }
}

void database::clear_expired_transactions()
{ try {
   //Look for expired transactions in the deduplication list, and remove them.
   //Transactions must have expired by at least two forking windows in order to be removed.
   auto& transaction_idx = static_cast<transaction_index&>(get_mutable_index(implementation_ids,
                                                                             impl_transaction_history_object_type));
   const auto& dedupe_index = transaction_idx.indices().get<by_expiration>();
   while( (!dedupe_index.empty()) && (head_block_time() > dedupe_index.begin()->trx.expiration) )
      transaction_idx.remove(*dedupe_index.begin());
} FC_CAPTURE_AND_RETHROW() } // GCOVR_EXCL_LINE

void database::clear_expired_proposals()
{
   const auto& proposal_expiration_index = get_index_type<proposal_index>().indices().get<by_expiration>();
   while( !proposal_expiration_index.empty() && proposal_expiration_index.begin()->expiration_time <= head_block_time() )
   {
      const proposal_object& proposal = *proposal_expiration_index.begin();
      processed_transaction result;
      try {
         if( proposal.is_authorized_to_execute(*this) )
         {
            result = push_proposal(proposal);
            //TODO: Do something with result so plugins can process it.
            continue;
         }
      } catch( const fc::exception& e ) {
         elog("Failed to apply proposed transaction on its expiration. Deleting it.\n${proposal}\n${error}",
              ("proposal", proposal)("error", e.to_detail_string()));
      }
      remove(proposal);
   }
}

// Helper function to check whether we need to udpate current_feed.settlement_price.
static optional<price> get_derived_current_feed_price( const database& db,
                                                       const asset_bitasset_data_object& bitasset )
{
   optional<price> result;
   // check for null first
   if( bitasset.median_feed.settlement_price.is_null() )
   {
      if( bitasset.current_feed.settlement_price.is_null() )
         return result;
      else
         return bitasset.median_feed.settlement_price;
   }

   using bsrm_type = bitasset_options::black_swan_response_type;
   const auto bsrm = bitasset.get_black_swan_response_method();
   if( bsrm_type::no_settlement == bsrm )
   {
      // Find the call order with the least collateral ratio
      const call_order_object* call_ptr = db.find_least_collateralized_short( bitasset, true );
      if( call_ptr )
      {
         // GS if : call_ptr->collateralization() < ~( bitasset.median_feed.max_short_squeeze_price() )
         auto least_collateral = call_ptr->collateralization();
         auto lowest_callable_feed_price = (~least_collateral) / ratio_type( GRAPHENE_COLLATERAL_RATIO_DENOM,
                                              bitasset.current_feed.maximum_short_squeeze_ratio );
         result = std::max( bitasset.median_feed.settlement_price, lowest_callable_feed_price );
      }
      else // there is no call order of this bitasset
         result = bitasset.median_feed.settlement_price;
   }
   else if( bsrm_type::individual_settlement_to_fund == bsrm && bitasset.individual_settlement_debt > 0 )
   {
      // Check whether to cap
      price fund_price = asset( bitasset.individual_settlement_debt, bitasset.asset_id )
                       / asset( bitasset.individual_settlement_fund, bitasset.options.short_backing_asset );
      auto lowest_callable_feed_price = fund_price * bitasset.get_margin_call_order_ratio();
      result = std::max( bitasset.median_feed.settlement_price, lowest_callable_feed_price );
   }
   else // should not cap
      result = bitasset.median_feed.settlement_price;

   // Check whether it's necessary to update
   if( result.valid() && (*result) == bitasset.current_feed.settlement_price )
      result.reset();
   return result;
}

// Helper function to update the limit order which is the individual settlement fund of the specified asset
static void update_settled_debt_order( database& db, const asset_bitasset_data_object& bitasset )
{
   // To avoid unexpected price fluctuations, do not update the order if no sufficient price feeds
   if( bitasset.current_feed.settlement_price.is_null() )
      return;

   const limit_order_object* limit_ptr = db.find_settled_debt_order( bitasset.asset_id );
   if( !limit_ptr )
      return;

   bool sell_all = true;
   share_type for_sale;

   // Note: bitasset.get_margin_call_order_price() is in debt/collateral
   price sell_price = ~bitasset.get_margin_call_order_price();
   asset settled_debt( bitasset.individual_settlement_debt, limit_ptr->receive_asset_id() );
   try
   {
      for_sale = settled_debt.multiply_and_round_up( sell_price ).amount; // may overflow
      if( for_sale <= bitasset.individual_settlement_fund ) // "=" is for the consistency of order matching logic
         sell_all = false;
   }
   catch( const fc::exception& e ) // catch the overflow
   {
      // do nothing
      dlog( e.to_detail_string() );
   }

   // TODO Potential optimization: to avoid unnecessary database update, check before update
   db.modify( *limit_ptr, [sell_all, &sell_price, &for_sale, &bitasset]( limit_order_object& obj )
   {
      if( sell_all )
      {
         obj.for_sale = bitasset.individual_settlement_fund;
         obj.sell_price = ~bitasset.get_individual_settlement_price();
      }
      else
      {
         obj.for_sale = for_sale;
         obj.sell_price = sell_price;
      }
   } );
}

void database::update_bitasset_current_feed( const asset_bitasset_data_object& bitasset, bool skip_median_update )
{
   // For better performance, if nothing to update, we return
   optional<price> new_current_feed_price;
   using bsrm_type = bitasset_options::black_swan_response_type;
   const auto bsrm = bitasset.get_black_swan_response_method();
   if( skip_median_update )
   {
      if( bsrm_type::no_settlement != bsrm && bsrm_type::individual_settlement_to_fund != bsrm )
      {
         // it's possible that current_feed was capped thus we still need to update it
         if( bitasset.current_feed.settlement_price == bitasset.median_feed.settlement_price )
            return;
         new_current_feed_price = bitasset.median_feed.settlement_price;
      }
      else
      {
         new_current_feed_price = get_derived_current_feed_price( *this, bitasset );
         if( !new_current_feed_price.valid() )
            return;
      }
   }

   const auto& head_time = head_block_time();

   // We need to update the database
   modify( bitasset, [this, skip_median_update, &head_time, &new_current_feed_price, &bsrm]
                     ( asset_bitasset_data_object& abdo )
   {
      if( !skip_median_update )
      {
         const auto& maint_time = get_dynamic_global_properties().next_maintenance_time;
         abdo.update_median_feeds( head_time, maint_time );
         abdo.current_feed = abdo.median_feed;
         if( bsrm_type::no_settlement == bsrm || bsrm_type::individual_settlement_to_fund == bsrm )
            new_current_feed_price = get_derived_current_feed_price( *this, abdo );
      }
      if( new_current_feed_price.valid() )
         abdo.current_feed.settlement_price = *new_current_feed_price;
   } );

   // Update individual settlement order price
   if( !skip_median_update
         && bsrm_type::individual_settlement_to_order == bsrm
         && HARDFORK_CORE_2591_PASSED( head_time ) ) // Tighter peg (fill individual settlement order at MCOP)
   {
      update_settled_debt_order( *this, bitasset );
   }
}

void database::clear_expired_orders()
{ try {
         //Cancel expired limit orders
         auto head_time = head_block_time();
         auto maint_time = get_dynamic_global_properties().next_maintenance_time;

         bool before_core_hardfork_606 = ( maint_time <= HARDFORK_CORE_606_TIME ); // feed always trigger call

         auto& limit_index = get_index_type<limit_order_index>().indices().get<by_expiration>();
         while( !limit_index.empty() && limit_index.begin()->expiration <= head_time )
         {
            const limit_order_object& order = *limit_index.begin();
            auto base_asset = order.sell_price.base.asset_id;
            auto quote_asset = order.sell_price.quote.asset_id;
            cancel_limit_order( order );
            if( before_core_hardfork_606 )
            {
               // check call orders
               // Comments below are copied from limit_order_cancel_evaluator::do_apply(...)
               // Possible optimization: order can be called by cancelling a limit order
               //   if the canceled order was at the top of the book.
               // Do I need to check calls in both assets?
               check_call_orders( base_asset( *this ) );
               check_call_orders( quote_asset( *this ) );
            }
         }
} FC_CAPTURE_AND_RETHROW() } // GCOVR_EXCL_LINE

void database::clear_expired_force_settlements()
{ try {
   // Process expired force settlement orders

   // TODO Possible performance optimization. Looping through all assets is not ideal.
   //      - One idea is to check time first, if any expired settlement found, check asset.
   //        However, due to max_settlement_volume, this does not work, i.e. time meets but have to
   //        skip due to volume limit.
   //      - Instead, maintain some data e.g. (whether_force_settle_volome_meets, first_settle_time)
   //        in bitasset_data object and index by them, then we could process here faster.
   //        Note: due to rounding, even when settled < max_volume, it is still possible that we have to skip
   const auto& settlement_index = get_index_type<force_settlement_index>().indices().get<by_expiration>();
   if( settlement_index.empty() )
      return;

   const auto& head_time = head_block_time();
   const auto& maint_time = get_dynamic_global_properties().next_maintenance_time;

   const bool before_core_hardfork_184 = ( maint_time <= HARDFORK_CORE_184_TIME ); // something-for-nothing
   const bool before_core_hardfork_342 = ( maint_time <= HARDFORK_CORE_342_TIME ); // better rounding

   asset_id_type current_asset = settlement_index.begin()->settlement_asset_id();
   const asset_object* mia_object_ptr = &get(current_asset);
   const asset_bitasset_data_object* mia_ptr = &mia_object_ptr->bitasset_data(*this);

   asset max_settlement_volume;
   price settlement_fill_price;
   price settlement_price;
   bool current_asset_finished = false;

   auto next_asset = [&current_asset, &mia_object_ptr, &mia_ptr, &current_asset_finished, &settlement_index, this] {
      const auto bound = settlement_index.upper_bound(current_asset);
      if( bound == settlement_index.end() )
         return false;
      current_asset = bound->settlement_asset_id();
      mia_object_ptr = &get(current_asset);
      mia_ptr = &mia_object_ptr->bitasset_data(*this);
      current_asset_finished = false;
      return true;
   };

   // At each iteration, we either consume the current order and remove it, or we move to the next asset
   for( auto itr = settlement_index.lower_bound(current_asset);
        itr != settlement_index.end();
        itr = settlement_index.lower_bound(current_asset) )
   {
      const force_settlement_object& settle_order = *itr;
      auto settle_order_id = settle_order.id;

      if( current_asset != settle_order.settlement_asset_id() )
      {
         current_asset = settle_order.settlement_asset_id();
         mia_object_ptr = &get(current_asset);
         mia_ptr = &mia_object_ptr->bitasset_data(*this);
         // Note: we did not reset current_asset_finished to false here, it is OK,
         //       because current_asset should not have changed if current_asset_finished is true
      }
      const asset_object& mia_object = *mia_object_ptr;
      const asset_bitasset_data_object& mia = *mia_ptr;

      if( mia.is_globally_settled() )
      {
         ilog( "Canceling a force settlement because of black swan" );
         cancel_settle_order( settle_order );
         continue;
      }

      // Has this order not reached its settlement date?
      if( settle_order.settlement_date > head_time )
      {
         if( next_asset() )
            continue;
         break;
      }
      // Can we still settle in this asset?
      if( mia.current_feed.settlement_price.is_null() )
      {
         ilog("Canceling a force settlement in ${asset} because settlement price is null",
              ("asset", mia_object.symbol));
         cancel_settle_order(settle_order);
         continue;
      }
      if( GRAPHENE_100_PERCENT == mia.options.force_settlement_offset_percent ) // settle something for nothing
      {
         ilog( "Canceling a force settlement in ${asset} because settlement offset is 100%",
               ("asset", mia_object.symbol));
         cancel_settle_order(settle_order);
         continue;
      }
      // Note: although current supply would decrease during filling the settle orders,
      //       we always calculate with the initial value
      if( max_settlement_volume.asset_id != current_asset )
         max_settlement_volume = mia_object.amount( mia.max_force_settlement_volume(
                                                           mia_object.dynamic_data(*this).current_supply ) );
      // When current_asset_finished is true, this would be the 2nd time processing the same order.
      // In this case, we move to the next asset.
      if( mia.force_settled_volume >= max_settlement_volume.amount || current_asset_finished )
      {
         if( next_asset() )
            continue;
         break;
      }

      if( settlement_fill_price.base.asset_id != current_asset ) // only calculate once per asset
         settlement_fill_price = mia.current_feed.settlement_price
                                 / ratio_type( GRAPHENE_100_PERCENT - mia.options.force_settlement_offset_percent,
                                               GRAPHENE_100_PERCENT );

      if( before_core_hardfork_342 )
      {
         auto& pays = settle_order.balance;
         auto receives = (settle_order.balance * mia.current_feed.settlement_price);
         receives.amount = static_cast<uint64_t>( ( fc::uint128_t(receives.amount.value) *
                             (GRAPHENE_100_PERCENT - mia.options.force_settlement_offset_percent) ) /
                             GRAPHENE_100_PERCENT );
         assert(receives <= settle_order.balance * mia.current_feed.settlement_price);
         settlement_price = pays / receives;
      }
      else if( settlement_price.base.asset_id != current_asset ) // only calculate once per asset
         settlement_price = settlement_fill_price;

      asset settled = mia_object.amount(mia.force_settled_volume);
      // Match against the least collateralized short until the settlement is finished or we reach max settlements
      while( settled < max_settlement_volume && find_object(settle_order_id) )
      {
         if( 0 == settle_order.balance.amount )
         {
            wlog( "0 settlement detected" );
            cancel_settle_order( settle_order );
            break;
         }

         const call_order_object* call_ptr = find_least_collateralized_short( mia, true );
         // Note: there can be no debt position due to individual settlements
         if( !call_ptr ) // no debt position
         {
            wlog( "No debt position found when processing force settlement ${o}", ("o",settle_order) );
            cancel_settle_order( settle_order );
            break;
         }

         try {
            asset max_settlement = max_settlement_volume - settled;

            asset new_settled = match( settle_order, *call_ptr, settlement_price, mia,
                                       max_settlement, settlement_fill_price );
            if( !before_core_hardfork_184 && new_settled.amount == 0 ) // unable to fill this settle order
            {
               // current asset is finished when the settle order hasn't been cancelled
               current_asset_finished = ( nullptr != find_object( settle_order_id ) );
               break;
            }
            settled += new_settled;
            // before hard fork core-342, `new_settled > 0` is always true, we'll have:
            // * call order is completely filled (thus call_ptr will change in next loop), or
            // * settle order is completely filled (thus find_object(settle_order_id) will be false so will
            //   break out), or
            // * reached max_settlement_volume limit (thus new_settled == max_settlement so will break out).
            //
            // after hard fork core-342, if new_settled > 0, we'll have:
            // * call order is completely filled (thus call_ptr will change in next loop), or
            // * settle order is completely filled (thus find_object(settle_order_id) will be false so will
            //   break out), or
            // * reached max_settlement_volume limit, but it's possible that new_settled < max_settlement,
            //   in this case, new_settled will be zero in next iteration of the loop, so no need to check here.
         }
         catch ( const black_swan_exception& e ) {
            wlog( "Cancelling a settle_order since it may trigger a black swan: ${o}, ${e}",
                  ("o", settle_order)("e", e.to_detail_string()) );
            cancel_settle_order( settle_order );
            break;
         }
      }
      if( mia.force_settled_volume != settled.amount )
      {
         modify(mia, [&settled](asset_bitasset_data_object& b) {
            b.force_settled_volume = settled.amount;
         });
      }
   }
} FC_CAPTURE_AND_RETHROW() } // GCOVR_EXCL_LINE

void database::update_expired_feeds()
{
   const auto head_time = head_block_time();
   bool after_hardfork_615 = ( head_time >= HARDFORK_615_TIME );
   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   const auto& idx = get_index_type<asset_bitasset_data_index>().indices().get<by_feed_expiration>();
   auto itr = idx.begin();
   while( itr != idx.end() && itr->feed_is_expired( head_time ) )
   {
      const asset_bitasset_data_object& b = *itr;
      ++itr; // not always process begin() because old code skipped updating some assets before hf 615

      // update feeds, check margin calls
      if( !( after_hardfork_615 || b.feed_is_expired_before_hf_615( head_time ) ) )
         continue;

      auto old_current_feed = b.current_feed;
      auto old_median_feed = b.median_feed;
      const asset_object& asset_obj = b.asset_id( *this );
      update_bitasset_current_feed( b );
      // Note: we don't try to revive the bitasset here if it was GSed // TODO probably we should do it

      if( !b.current_feed.settlement_price.is_null()
            && !b.current_feed.margin_call_params_equal( old_current_feed ) )
      {
         check_call_orders( asset_obj, true, false, &b, true );
      }
      else if( after_core_hardfork_2582 && !b.median_feed.settlement_price.is_null()
            && !b.median_feed.margin_call_params_equal( old_median_feed ) )
      {
         check_call_orders( asset_obj, true, false, &b, true );
      }
      // update CER
      if( b.need_to_update_cer() )
      {
         modify( b, []( asset_bitasset_data_object& abdo )
         {
            abdo.asset_cer_updated = false;
            abdo.feed_cer_updated = false;
         });
         if( asset_obj.options.core_exchange_rate != b.current_feed.core_exchange_rate )
         {
            modify( asset_obj, [&b]( asset_object& ao )
            {
               ao.options.core_exchange_rate = b.current_feed.core_exchange_rate;
            });
         }
      }
   } // for each asset whose feed is expired

   // process assets affected by bitshares-core issue 453 before hard fork 615
   if( !after_hardfork_615 )
   {
      for( asset_id_type a : _issue_453_affected_assets )
      {
         check_call_orders( a(*this) );
      }
   }
}

void database::update_core_exchange_rates()
{
   const auto& idx = get_index_type<asset_bitasset_data_index>().indices().get<by_cer_update>();
   if( idx.begin() != idx.end() )
   {
      for( auto itr = idx.rbegin(); itr->need_to_update_cer(); itr = idx.rbegin() )
      {
         const asset_bitasset_data_object& b = *itr;
         const asset_object& a = b.asset_id( *this );
         if( a.options.core_exchange_rate != b.current_feed.core_exchange_rate )
         {
            modify( a, [&b]( asset_object& ao )
            {
               ao.options.core_exchange_rate = b.current_feed.core_exchange_rate;
            });
         }
         modify( b, []( asset_bitasset_data_object& abdo )
         {
            abdo.asset_cer_updated = false;
            abdo.feed_cer_updated = false;
         });
      }
   }
}

void database::update_maintenance_flag( bool new_maintenance_flag )
{
   modify( get_dynamic_global_properties(), [&]( dynamic_global_property_object& dpo )
   {
      auto maintenance_flag = dynamic_global_property_object::maintenance_flag;
      dpo.dynamic_flags =
           (dpo.dynamic_flags & (uint32_t)(~maintenance_flag))
         | (new_maintenance_flag ? (uint32_t)maintenance_flag : 0U);
   } );
   return;
}

void database::update_withdraw_permissions()
{
   auto& permit_index = get_index_type<withdraw_permission_index>().indices().get<by_expiration>();
   while( !permit_index.empty() && permit_index.begin()->expiration <= head_block_time() )
      remove(*permit_index.begin());
}

void database::clear_expired_htlcs()
{
   const auto& htlc_idx = get_index_type<htlc_index>().indices().get<by_expiration>();
   while ( htlc_idx.begin() != htlc_idx.end()
         && htlc_idx.begin()->conditions.time_lock.expiration <= head_block_time() )
   {
      const htlc_object& obj = *htlc_idx.begin();
      const auto amount = asset(obj.transfer.amount, obj.transfer.asset_id);
      adjust_balance( obj.transfer.from, amount );
      // notify related parties
      htlc_refund_operation vop( obj.get_id(), obj.transfer.from, obj.transfer.to, amount,
         obj.conditions.hash_lock.preimage_hash, obj.conditions.hash_lock.preimage_size );
      push_applied_operation( vop );
      remove( obj );
   }
}

generic_operation_result database::process_tickets()
{
   const auto maint_time = get_dynamic_global_properties().next_maintenance_time;
   ticket_version version = ( HARDFORK_CORE_2262_PASSED(maint_time) ? ticket_v2 : ticket_v1 );

   generic_operation_result result;
   share_type total_delta_pob;
   share_type total_delta_inactive;
   auto& idx = get_index_type<ticket_index>().indices().get<by_next_update>();
   while( !idx.empty() && idx.begin()->next_auto_update_time <= head_block_time() )
   {
      const ticket_object& ticket = *idx.begin();
      const auto& stat = get_account_stats_by_owner( ticket.account );
      if( ticket.status == withdrawing && ticket.current_type == liquid )
      {
         adjust_balance( ticket.account, ticket.amount );
         // Note: amount.asset_id is checked when creating the ticket, so no check here
         modify( stat, [&ticket](account_statistics_object& aso) {
            aso.total_core_pol -= ticket.amount.amount;
            aso.total_pol_value -= ticket.value;
         });
         result.removed_objects.insert( ticket.id );
         remove( ticket );
      }
      else
      {
         ticket_type old_type = ticket.current_type;
         share_type old_value = ticket.value;
         modify( ticket, [version]( ticket_object& o ) {
            o.auto_update( version );
         });
         result.updated_objects.insert( ticket.id );

         share_type delta_inactive_amount;
         share_type delta_forever_amount;
         share_type delta_forever_value;
         share_type delta_other_amount;
         share_type delta_other_value;

         if( old_type == lock_forever ) // It implies that the new type is lock_forever too
         {
            if( ticket.value == 0 )
            {
               total_delta_pob -= ticket.amount.amount;
               total_delta_inactive += ticket.amount.amount;
               delta_inactive_amount = ticket.amount.amount;
               delta_forever_amount = -ticket.amount.amount;
            }
            delta_forever_value = ticket.value - old_value;
         }
         else // old_type != lock_forever
         {
            if( ticket.current_type == lock_forever )
            {
               total_delta_pob += ticket.amount.amount;
               delta_forever_amount = ticket.amount.amount;
               delta_forever_value = ticket.value;
               delta_other_amount = -ticket.amount.amount;
               delta_other_value = -old_value;
            }
            else // ticket.current_type != lock_forever
            {
               delta_other_value = ticket.value - old_value;
            }
         }

         // Note: amount.asset_id is checked when creating the ticket, so no check here
         modify( stat, [delta_inactive_amount,delta_forever_amount,delta_forever_value,
                        delta_other_amount,delta_other_value](account_statistics_object& aso) {
            aso.total_core_inactive += delta_inactive_amount;
            aso.total_core_pob += delta_forever_amount;
            aso.total_core_pol += delta_other_amount;
            aso.total_pob_value += delta_forever_value;
            aso.total_pol_value += delta_other_value;
         });

      }
      // TODO if a lock_forever ticket lost all the value, remove it
   }

   // TODO merge stable tickets with the same account and the same type

   // Update global data
   if( total_delta_pob != 0 || total_delta_inactive != 0 )
   {
      modify( get_dynamic_global_properties(),
              [total_delta_pob,total_delta_inactive]( dynamic_global_property_object& dgp ) {
         dgp.total_pob += total_delta_pob;
         dgp.total_inactive += total_delta_inactive;
      });
   }

   return result;
}

void database::update_credit_offers_and_deals()
{
   const auto head_time = head_block_time();

   // Auto-disable offers
   const auto& offer_idx = get_index_type<credit_offer_index>().indices().get<by_auto_disable_time>();
   auto offer_itr = offer_idx.lower_bound( true );
   auto offer_itr_end = offer_idx.upper_bound( boost::make_tuple( true, head_time ) );
   while( offer_itr != offer_itr_end )
   {
      const credit_offer_object& offer = *offer_itr;
      ++offer_itr;
      modify( offer, []( credit_offer_object& obj ) {
         obj.enabled = false;
      });
   }

   // Auto-process deals
   const auto& deal_idx = get_index_type<credit_deal_index>().indices().get<by_latest_repay_time>();
   const auto& deal_summary_idx = get_index_type<credit_deal_summary_index>().indices().get<by_offer_borrower>();
   auto deal_itr_end = deal_idx.upper_bound( head_time );
   for( auto deal_itr = deal_idx.begin(); deal_itr != deal_itr_end; deal_itr = deal_idx.begin() )
   {
      const credit_deal_object& deal = *deal_itr;

      // Process automatic repayment
      // Note: an automatic repayment may fail, in which case we consider the credit deal past due without repayment
      using repay_type = credit_deal_auto_repayment_type;
      if( static_cast<uint8_t>(repay_type::no_auto_repayment) != deal.auto_repay )
      {
         credit_deal_repay_operation op;
         op.account = deal.borrower;
         op.deal_id = deal.get_id();
         // Amounts
         // Note: the result can be larger than 64 bit
         auto required_fee = ( ( ( fc::uint128_t( deal.debt_amount.value ) * deal.fee_rate )
                                 + GRAPHENE_FEE_RATE_DENOM ) - 1 ) / GRAPHENE_FEE_RATE_DENOM; // Round up
         fc::uint128_t total_required = required_fee + deal.debt_amount.value;
         auto balance = get_balance( deal.borrower, deal.debt_asset );
         if( static_cast<uint8_t>(repay_type::only_full_repayment) == deal.auto_repay
               || fc::uint128_t( balance.amount.value ) >= total_required )
         { // if only full repayment or account balance is sufficient
            op.repay_amount = asset( deal.debt_amount, deal.debt_asset );
            op.credit_fee = asset( static_cast<int64_t>( required_fee ), deal.debt_asset );
         }
         else // Allow partial repayment
         {
            fc::uint128_t debt_to_repay = ( fc::uint128_t( balance.amount.value ) * GRAPHENE_FEE_RATE_DENOM )
                                          / ( GRAPHENE_FEE_RATE_DENOM + deal.fee_rate ); // Round down
            fc::uint128_t collateral_to_release = ( debt_to_repay * deal.collateral_amount.value )
                                                  / deal.debt_amount.value; // Round down
            debt_to_repay = ( ( ( collateral_to_release * deal.debt_amount.value ) + deal.collateral_amount.value )
                              - 1 ) / deal.collateral_amount.value; // Round up
            fc::uint128_t fee_to_pay = ( ( ( debt_to_repay * deal.fee_rate )
                                           + GRAPHENE_FEE_RATE_DENOM ) - 1 ) / GRAPHENE_FEE_RATE_DENOM; // Round up
            op.repay_amount = asset( static_cast<int64_t>( debt_to_repay ), deal.debt_asset );
            op.credit_fee = asset( static_cast<int64_t>( fee_to_pay ), deal.debt_asset );
         }

         auto deal_copy = deal; // Make a copy for logging

         transaction_evaluation_state eval_state(this);
         eval_state.skip_fee_schedule_check = true;

         try
         {
            try_push_virtual_operation( eval_state, op );
         }
         catch( const fc::exception& e )
         {
            // We can in fact get here,
            // e.g. if the debt asset issuer blacklisted the account, or account balance is insufficient
            wlog( "Automatic repayment ${op} for credit deal ${credit_deal} failed at block ${n}; "
                  "account balance was ${balance}; exception was ${e}",
                  ("op", op)("credit_deal", deal_copy)
                  ("n", head_block_num())("balance", balance)("e", e.to_detail_string()) );
         }

         if( !find( op.deal_id ) ) // The credit deal is fully repaid
            continue;
      }

      // Update offer
      // Note: offer balance can be zero after updated. TODO remove zero-balance offers after a period
      const credit_offer_object& offer = deal.offer_id(*this);
      modify( offer, [&deal]( credit_offer_object& obj ){
         obj.total_balance -= deal.debt_amount;
      });

      // Process deal summary
      auto summ_itr = deal_summary_idx.find( boost::make_tuple( deal.offer_id, deal.borrower ) );
      if( summ_itr == deal_summary_idx.end() ) // This should not happen, just be defensive here
      {
         // We do not do FC_ASSERT or FC_THROW here to avoid halting the chain
         elog( "Error: unable to find the credit deal summary object for credit deal ${d}",
               ("d", deal) );
      }
      else
      {
         const credit_deal_summary_object& summ_obj = *summ_itr;
         if( summ_obj.total_debt_amount == deal.debt_amount )
         {
            remove( summ_obj );
         }
         else
         {
            modify( summ_obj, [&deal]( credit_deal_summary_object& obj ){
               obj.total_debt_amount -= deal.debt_amount;
            });
         }
      }

      // Adjust balance
      adjust_balance( deal.offer_owner, asset( deal.collateral_amount, deal.collateral_asset ) );

      // Notify related parties
      push_applied_operation( credit_deal_expired_operation (
                                    deal.get_id(), deal.offer_id, deal.offer_owner, deal.borrower,
                                    asset( deal.debt_amount, deal.debt_asset ),
                                    asset( deal.collateral_amount, deal.collateral_asset ),
                                    deal.fee_rate ) );

      // Remove the deal
      remove( deal );
   }
}

} }
