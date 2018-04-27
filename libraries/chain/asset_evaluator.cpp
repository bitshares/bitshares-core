/*
 * Copyright (c) 2015-2018 Cryptonomex, Inc., and contributors.
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
#include <graphene/chain/asset_evaluator.hpp>
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/market_object.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <functional>

#include <locale>

namespace graphene { namespace chain {

void_result asset_create_evaluator::do_evaluate( const asset_create_operation& op )
{ try {

   database& d = db();

   const auto& chain_parameters = d.get_global_properties().parameters;
   FC_ASSERT( op.common_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   FC_ASSERT( op.common_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );

   // Check that all authorities do exist
   for( auto id : op.common_options.whitelist_authorities )
      d.get_object(id);
   for( auto id : op.common_options.blacklist_authorities )
      d.get_object(id);

   auto& asset_indx = d.get_index_type<asset_index>().indices().get<by_symbol>();
   auto asset_symbol_itr = asset_indx.find( op.symbol );
   FC_ASSERT( asset_symbol_itr == asset_indx.end() );

   if( d.head_block_time() > HARDFORK_385_TIME )
   {
      auto dotpos = op.symbol.rfind( '.' );
      if( dotpos != std::string::npos )
      {
         auto prefix = op.symbol.substr( 0, dotpos );
         auto asset_symbol_itr = asset_indx.find( prefix );
         FC_ASSERT( asset_symbol_itr != asset_indx.end(), "Asset ${s} may only be created by issuer of ${p}, but ${p} has not been registered",
                    ("s",op.symbol)("p",prefix) );
         FC_ASSERT( asset_symbol_itr->issuer == op.issuer, "Asset ${s} may only be created by issuer of ${p}, ${i}",
                    ("s",op.symbol)("p",prefix)("i", op.issuer(d).name) );
      }

      if(d.head_block_time() <= HARDFORK_CORE_620_TIME ) { // TODO: remove this check after hf_620
         static const std::locale& loc = std::locale::classic();
         FC_ASSERT(isalpha(op.symbol.back(), loc), "Asset ${s} must end with alpha character before hardfork 620", ("s",op.symbol));
      }
   }
   else
   {
      auto dotpos = op.symbol.find( '.' );
      if( dotpos != std::string::npos )
          wlog( "Asset ${s} has a name which requires hardfork 385", ("s",op.symbol) );
   }

   if( op.bitasset_opts )
   {
      const asset_object& backing = op.bitasset_opts->short_backing_asset(d);
      if( backing.is_market_issued() )
      {
         const asset_bitasset_data_object& backing_bitasset_data = backing.bitasset_data(d);
         const asset_object& backing_backing = backing_bitasset_data.options.short_backing_asset(d);
         FC_ASSERT( !backing_backing.is_market_issued(),
                    "May not create a bitasset backed by a bitasset backed by a bitasset." );
         FC_ASSERT( op.issuer != GRAPHENE_COMMITTEE_ACCOUNT || backing_backing.get_id() == asset_id_type(),
                    "May not create a blockchain-controlled market asset which is not backed by CORE.");
      } else
         FC_ASSERT( op.issuer != GRAPHENE_COMMITTEE_ACCOUNT || backing.get_id() == asset_id_type(),
                    "May not create a blockchain-controlled market asset which is not backed by CORE.");
      FC_ASSERT( op.bitasset_opts->feed_lifetime_sec > chain_parameters.block_interval &&
                 op.bitasset_opts->force_settlement_delay_sec > chain_parameters.block_interval );
   }
   if( op.is_prediction_market )
   {
      FC_ASSERT( op.bitasset_opts );
      FC_ASSERT( op.precision == op.bitasset_opts->short_backing_asset(d).precision );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void asset_create_evaluator::pay_fee()
{
   fee_is_odd = core_fee_paid.value & 1;
   core_fee_paid -= core_fee_paid.value/2;
   generic_evaluator::pay_fee();
}

object_id_type asset_create_evaluator::do_apply( const asset_create_operation& op )
{ try {
   bool hf_429 = fee_is_odd && db().head_block_time() > HARDFORK_CORE_429_TIME;

   const asset_dynamic_data_object& dyn_asset =
      db().create<asset_dynamic_data_object>( [&]( asset_dynamic_data_object& a ) {
         a.current_supply = 0;
         a.fee_pool = core_fee_paid - (hf_429 ? 1 : 0);
      });
   if( fee_is_odd && !hf_429 )
   {
      const auto& core_dd = db().get<asset_object>( asset_id_type() ).dynamic_data( db() );
      db().modify( core_dd, [=]( asset_dynamic_data_object& dd ) {
         dd.current_supply++;
      });
   }

   asset_bitasset_data_id_type bit_asset_id;
   if( op.bitasset_opts.valid() )
      bit_asset_id = db().create<asset_bitasset_data_object>( [&]( asset_bitasset_data_object& a ) {
            a.options = *op.bitasset_opts;
            a.is_prediction_market = op.is_prediction_market;
         }).id;

   auto next_asset_id = db().get_index_type<asset_index>().get_next_id();

   const asset_object& new_asset =
     db().create<asset_object>( [&]( asset_object& a ) {
         a.issuer = op.issuer;
         a.symbol = op.symbol;
         a.precision = op.precision;
         a.options = op.common_options;
         if( a.options.core_exchange_rate.base.asset_id.instance.value == 0 )
            a.options.core_exchange_rate.quote.asset_id = next_asset_id;
         else
            a.options.core_exchange_rate.base.asset_id = next_asset_id;
         a.dynamic_asset_data_id = dyn_asset.id;
         if( op.bitasset_opts.valid() )
            a.bitasset_data_id = bit_asset_id;
      });
   assert( new_asset.id == next_asset_id );

   return new_asset.id;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result asset_issue_evaluator::do_evaluate( const asset_issue_operation& o )
{ try {
   const database& d = db();

   const asset_object& a = o.asset_to_issue.asset_id(d);
   FC_ASSERT( o.issuer == a.issuer );
   FC_ASSERT( !a.is_market_issued(), "Cannot manually issue a market-issued asset." );

   to_account = &o.issue_to_account(d);
   FC_ASSERT( is_authorized_asset( d, *to_account, a ) );

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply + o.asset_to_issue.amount) <= a.options.max_supply );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_issue_evaluator::do_apply( const asset_issue_operation& o )
{ try {
   db().adjust_balance( o.issue_to_account, o.asset_to_issue );

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ){
        data.current_supply += o.asset_to_issue.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_reserve_evaluator::do_evaluate( const asset_reserve_operation& o )
{ try {
   const database& d = db();

   const asset_object& a = o.amount_to_reserve.asset_id(d);
   GRAPHENE_ASSERT(
      !a.is_market_issued(),
      asset_reserve_invalid_on_mia,
      "Cannot reserve ${sym} because it is a market-issued asset",
      ("sym", a.symbol)
   );

   from_account = &o.payer(d);
   FC_ASSERT( is_authorized_asset( d, *from_account, a ) );

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply - o.amount_to_reserve.amount) >= 0 );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_reserve_evaluator::do_apply( const asset_reserve_operation& o )
{ try {
   db().adjust_balance( o.payer, -o.amount_to_reserve );

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ){
        data.current_supply -= o.amount_to_reserve.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_fund_fee_pool_evaluator::do_evaluate(const asset_fund_fee_pool_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_id(d);

   asset_dyn_data = &a.dynamic_asset_data_id(d);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_fund_fee_pool_evaluator::do_apply(const asset_fund_fee_pool_operation& o)
{ try {
   db().adjust_balance(o.from_account, -o.amount);

   db().modify( *asset_dyn_data, [&]( asset_dynamic_data_object& data ) {
      data.fee_pool += o.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

static void validate_new_issuer( const database& d, const asset_object& a, account_id_type new_issuer )
{ try {
   FC_ASSERT(d.find_object(new_issuer));
   if( a.is_market_issued() && new_issuer == GRAPHENE_COMMITTEE_ACCOUNT )
   {
      const asset_object& backing = a.bitasset_data(d).options.short_backing_asset(d);
      if( backing.is_market_issued() )
      {
         const asset_object& backing_backing = backing.bitasset_data(d).options.short_backing_asset(d);
         FC_ASSERT( backing_backing.get_id() == asset_id_type(),
                    "May not create a blockchain-controlled market asset which is not backed by CORE.");
      } else
         FC_ASSERT( backing.get_id() == asset_id_type(),
                    "May not create a blockchain-controlled market asset which is not backed by CORE.");
   }
} FC_CAPTURE_AND_RETHROW( (a)(new_issuer) ) }

void_result asset_update_evaluator::do_evaluate(const asset_update_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_to_update(d);
   auto a_copy = a;
   a_copy.options = o.new_options;
   a_copy.validate();

   if( o.new_issuer )
   {
      FC_ASSERT( d.head_block_time() < HARDFORK_CORE_199_TIME,
                 "Since Hardfork #199, updating issuer requires the use of asset_update_issuer_operation.");
      validate_new_issuer( d, a, *o.new_issuer );
   }

   if( (d.head_block_time() < HARDFORK_572_TIME) || (a.dynamic_asset_data_id(d).current_supply != 0) )
   {
      // new issuer_permissions must be subset of old issuer permissions
      FC_ASSERT(!(o.new_options.issuer_permissions & ~a.options.issuer_permissions),
                "Cannot reinstate previously revoked issuer permissions on an asset.");
   }

   // changed flags must be subset of old issuer permissions
   FC_ASSERT(!((o.new_options.flags ^ a.options.flags) & ~a.options.issuer_permissions),
             "Flag change is forbidden by issuer permissions");

   asset_to_update = &a;
   FC_ASSERT( o.issuer == a.issuer,
              "Incorrect issuer for asset! (${o.issuer} != ${a.issuer})",
              ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   const auto& chain_parameters = d.get_global_properties().parameters;

   FC_ASSERT( o.new_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.whitelist_authorities )
      d.get_object(id);
   FC_ASSERT( o.new_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.blacklist_authorities )
      d.get_object(id);

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) }

void_result asset_update_evaluator::do_apply(const asset_update_operation& o)
{ try {
   database& d = db();

   // If we are now disabling force settlements, cancel all open force settlement orders
   if( (o.new_options.flags & disable_force_settle) && asset_to_update->can_force_settle() )
   {
      const auto& idx = d.get_index_type<force_settlement_index>().indices().get<by_expiration>();
      // Funky iteration code because we're removing objects as we go. We have to re-initialize itr every loop instead
      // of simply incrementing it.
      for( auto itr = idx.lower_bound(o.asset_to_update);
           itr != idx.end() && itr->settlement_asset_id() == o.asset_to_update;
           itr = idx.lower_bound(o.asset_to_update) )
         d.cancel_settle_order(*itr);
   }

   d.modify(*asset_to_update, [&](asset_object& a) {
      if( o.new_issuer )
         a.issuer = *o.new_issuer;
      a.options = o.new_options;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_update_issuer_evaluator::do_evaluate(const asset_update_issuer_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_to_update(d);

   validate_new_issuer( d, a, o.new_issuer );

   asset_to_update = &a;
   FC_ASSERT( o.issuer == a.issuer,
              "Incorrect issuer for asset! (${o.issuer} != ${a.issuer})",
              ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   if( d.head_block_time() < HARDFORK_CORE_199_TIME )
   {
      // TODO: remove after HARDFORK_CORE_199_TIME has passed
      FC_ASSERT(false, "Not allowed until hardfork 199");
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) }

void_result asset_update_issuer_evaluator::do_apply(const asset_update_issuer_operation& o)
{ try {
   database& d = db();
   d.modify(*asset_to_update, [&](asset_object& a) {
      a.issuer = o.new_issuer;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_update_bitasset_evaluator::do_evaluate(const asset_update_bitasset_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update BitAsset-specific settings on a non-BitAsset.");

   const asset_bitasset_data_object& b = a.bitasset_data(d);
   FC_ASSERT( !b.has_settlement(), "Cannot update a bitasset after a settlement has executed" );
   if( o.new_options.short_backing_asset != b.options.short_backing_asset )
   {
      FC_ASSERT(a.dynamic_asset_data_id(d).current_supply == 0);
      FC_ASSERT(d.find_object(o.new_options.short_backing_asset));

      if( a.issuer == GRAPHENE_COMMITTEE_ACCOUNT )
      {
         const asset_object& backing = a.bitasset_data(d).options.short_backing_asset(d);
         if( backing.is_market_issued() )
         {
            const asset_object& backing_backing = backing.bitasset_data(d).options.short_backing_asset(d);
            FC_ASSERT( backing_backing.get_id() == asset_id_type(),
                       "May not create a blockchain-controlled market asset which is not backed by CORE.");
         } else
            FC_ASSERT( backing.get_id() == asset_id_type(),
                       "May not create a blockchain-controlled market asset which is not backed by CORE.");
      }
   }

   bitasset_to_update = &b;
   FC_ASSERT( o.issuer == a.issuer, "", ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_update_bitasset_evaluator::do_apply(const asset_update_bitasset_operation& op)
{
   try {
      // If the minimum number of feeds to calculate a median has changed, we need to recalculate the median
      bool should_update_feeds = false;
      if( op.new_options.minimum_feeds != bitasset_to_update->options.minimum_feeds )
         should_update_feeds = true;

      // feeds must be reset if the backing asset is changed
      bool backing_asset_changed = false;
      bool is_witness_or_committee_fed = false;
      if (op.new_options.short_backing_asset != bitasset_to_update->options.short_backing_asset)
      {
         backing_asset_changed = true;
         const asset_object& base_asset = op.asset_to_update(db());
         if ( (base_asset.options.flags & witness_fed_asset)  || (base_asset.options.flags & committee_fed_asset) )
            is_witness_or_committee_fed = true;
      }

      // now do the actual modifications to the database object
      db().modify(*bitasset_to_update, [&](asset_bitasset_data_object& bdo) {
         bdo.options = op.new_options;

         // are we modifying the underlying? If so, reset the feeds
         if (backing_asset_changed) {
            if ( is_witness_or_committee_fed ) {
               bdo.feeds.clear();
            } else {
               // for non-witness-feeding and non-committe-feeding assets, modify all feeds
               // published by producers to null, since we can't simply remove them. For more information:
               // https://github.com/bitshares/bitshares-core/pull/832#issuecomment-384112633
               for(auto itr = bdo.feeds.rbegin(); itr != bdo.feeds.rend(); ++itr) {
                  //TODO: how do we "nullify" the price feeds?
               }
            }
         }

         if( should_update_feeds )
            bdo.update_median_feeds(db().head_block_time());
      });

      return void_result();
   } FC_CAPTURE_AND_RETHROW( (op) )
}

void_result asset_update_feed_producers_evaluator::do_evaluate(const asset_update_feed_producers_evaluator::operation_type& o)
{ try {
   database& d = db();

   FC_ASSERT( o.new_feed_producers.size() <= d.get_global_properties().parameters.maximum_asset_feed_publishers );
   for( auto id : o.new_feed_producers )
      d.get_object(id);

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update feed producers on a non-BitAsset.");
   FC_ASSERT(!(a.options.flags & committee_fed_asset), "Cannot set feed producers on a committee-fed asset.");
   FC_ASSERT(!(a.options.flags & witness_fed_asset), "Cannot set feed producers on a witness-fed asset.");

   const asset_bitasset_data_object& b = a.bitasset_data(d);
   bitasset_to_update = &b;
   FC_ASSERT( a.issuer == o.issuer );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_update_feed_producers_evaluator::do_apply(const asset_update_feed_producers_evaluator::operation_type& o)
{ try {
   db().modify(*bitasset_to_update, [&](asset_bitasset_data_object& a) {
      //This is tricky because I have a set of publishers coming in, but a map of publisher to feed is stored.
      //I need to update the map such that the keys match the new publishers, but not munge the old price feeds from
      //publishers who are being kept.
      //First, remove any old publishers who are no longer publishers
      for( auto itr = a.feeds.begin(); itr != a.feeds.end(); )
      {
         if( !o.new_feed_producers.count(itr->first) )
            itr = a.feeds.erase(itr);
         else
            ++itr;
      }
      //Now, add any new publishers
      for( auto itr = o.new_feed_producers.begin(); itr != o.new_feed_producers.end(); ++itr )
         if( !a.feeds.count(*itr) )
            a.feeds[*itr];
      a.update_median_feeds(db().head_block_time());
   });
   db().check_call_orders( o.asset_to_update(db()) );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_global_settle_evaluator::do_evaluate(const asset_global_settle_evaluator::operation_type& op)
{ try {
   const database& d = db();
   asset_to_settle = &op.asset_to_settle(d);
   FC_ASSERT(asset_to_settle->is_market_issued());
   FC_ASSERT(asset_to_settle->can_global_settle());
   FC_ASSERT(asset_to_settle->issuer == op.issuer );
   FC_ASSERT(asset_to_settle->dynamic_data(d).current_supply > 0);

   const asset_bitasset_data_object& _bitasset_data  = asset_to_settle->bitasset_data(d);
   // if there is a settlement for this asset, then no further global settle may be taken
   FC_ASSERT( !_bitasset_data.has_settlement(), "This asset has settlement, cannot global settle again" );

   const auto& idx = d.get_index_type<call_order_index>().indices().get<by_collateral>();
   assert( !idx.empty() );
   auto itr = idx.lower_bound(boost::make_tuple(price::min(asset_to_settle->bitasset_data(d).options.short_backing_asset,
                                                           op.asset_to_settle)));
   assert( itr != idx.end() && itr->debt_type() == op.asset_to_settle );
   const call_order_object& least_collateralized_short = *itr;
   FC_ASSERT(least_collateralized_short.get_debt() * op.settle_price <= least_collateralized_short.get_collateral(),
             "Cannot force settle at supplied price: least collateralized short lacks sufficient collateral to settle.");

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result asset_global_settle_evaluator::do_apply(const asset_global_settle_evaluator::operation_type& op)
{ try {
   database& d = db();
   d.globally_settle_asset( op.asset_to_settle(db()), op.settle_price );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result asset_settle_evaluator::do_evaluate(const asset_settle_evaluator::operation_type& op)
{ try {
   const database& d = db();
   asset_to_settle = &op.amount.asset_id(d);
   FC_ASSERT(asset_to_settle->is_market_issued());
   const auto& bitasset = asset_to_settle->bitasset_data(d);
   FC_ASSERT(asset_to_settle->can_force_settle() || bitasset.has_settlement() );
   if( bitasset.is_prediction_market )
      FC_ASSERT( bitasset.has_settlement(), "global settlement must occur before force settling a prediction market"  );
   else if( bitasset.current_feed.settlement_price.is_null()
            && ( d.head_block_time() <= HARDFORK_CORE_216_TIME
                 || !bitasset.has_settlement() ) )
      FC_THROW_EXCEPTION(insufficient_feeds, "Cannot force settle with no price feed.");
   FC_ASSERT(d.get_balance(d.get(op.account), *asset_to_settle) >= op.amount);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

operation_result asset_settle_evaluator::do_apply(const asset_settle_evaluator::operation_type& op)
{ try {
   database& d = db();

   const auto& bitasset = asset_to_settle->bitasset_data(d);
   if( bitasset.has_settlement() )
   {
      const auto& mia_dyn = asset_to_settle->dynamic_asset_data_id(d);

      auto settled_amount = op.amount * bitasset.settlement_price; // round down, in favor of global settlement fund
      if( op.amount.amount == mia_dyn.current_supply )
         settled_amount.amount = bitasset.settlement_fund; // avoid rounding problems
      else
         FC_ASSERT( settled_amount.amount <= bitasset.settlement_fund ); // should be strictly < except for PM with zero outcome

      if( settled_amount.amount == 0 && !bitasset.is_prediction_market )
      {
         if( d.get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_184_TIME )
            FC_THROW( "Settle amount is too small to receive anything due to rounding" );
         else // TODO remove this warning after hard fork core-184
            wlog( "Something for nothing issue (#184, variant F) occurred at block #${block}", ("block",d.head_block_num()) );
      }

      asset pays = op.amount;
      if( op.amount.amount != mia_dyn.current_supply
            && settled_amount.amount != 0
            && d.get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_342_TIME )
      {
         pays = settled_amount.multiply_and_round_up( bitasset.settlement_price );
      }

      d.adjust_balance( op.account, -pays );

      if( settled_amount.amount > 0 )
      {
         d.modify( bitasset, [&]( asset_bitasset_data_object& obj ){
            obj.settlement_fund -= settled_amount.amount;
         });

         d.adjust_balance( op.account, settled_amount );
      }

      d.modify( mia_dyn, [&]( asset_dynamic_data_object& obj ){
         obj.current_supply -= pays.amount;
      });

      return settled_amount;
   }
   else
   {
      d.adjust_balance( op.account, -op.amount );
      return d.create<force_settlement_object>([&](force_settlement_object& s) {
         s.owner = op.account;
         s.balance = op.amount;
         s.settlement_date = d.head_block_time() + asset_to_settle->bitasset_data(d).options.force_settlement_delay_sec;
      }).id;
   }
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result asset_publish_feeds_evaluator::do_evaluate(const asset_publish_feed_operation& o)
{ try {
   database& d = db();

   const asset_object& base = o.asset_id(d);
   //Verify that this feed is for a market-issued asset and that asset is backed by the base
   FC_ASSERT(base.is_market_issued());

   const asset_bitasset_data_object& bitasset = base.bitasset_data(d);
   if( bitasset.is_prediction_market || d.head_block_time() <= HARDFORK_CORE_216_TIME )
   {
      FC_ASSERT( !bitasset.has_settlement(), "No further feeds may be published after a settlement event" );
   }

   FC_ASSERT( o.feed.settlement_price.quote.asset_id == bitasset.options.short_backing_asset );
   if( d.head_block_time() > HARDFORK_480_TIME )
   {
      if( !o.feed.core_exchange_rate.is_null() )
      {
         FC_ASSERT( o.feed.core_exchange_rate.quote.asset_id == asset_id_type() );
      }
   }
   else
   {
      if( (!o.feed.settlement_price.is_null()) && (!o.feed.core_exchange_rate.is_null()) )
      {
         FC_ASSERT( o.feed.settlement_price.quote.asset_id == o.feed.core_exchange_rate.quote.asset_id );
      }
   }

   //Verify that the publisher is authoritative to publish a feed
   if( base.options.flags & witness_fed_asset )
   {
      FC_ASSERT( d.get(GRAPHENE_WITNESS_ACCOUNT).active.account_auths.count(o.publisher) );
   }
   else if( base.options.flags & committee_fed_asset )
   {
      FC_ASSERT( d.get(GRAPHENE_COMMITTEE_ACCOUNT).active.account_auths.count(o.publisher) );
   }
   else
   {
      FC_ASSERT(bitasset.feeds.count(o.publisher));
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) }

void_result asset_publish_feeds_evaluator::do_apply(const asset_publish_feed_operation& o)
{ try {

   database& d = db();

   const asset_object& base = o.asset_id(d);
   const asset_bitasset_data_object& bad = base.bitasset_data(d);

   auto old_feed =  bad.current_feed;
   // Store medians for this asset
   d.modify(bad , [&o,&d](asset_bitasset_data_object& a) {
      a.feeds[o.publisher] = make_pair(d.head_block_time(), o.feed);
      a.update_median_feeds(d.head_block_time());
   });

   if( !(old_feed == bad.current_feed) )
   {
      if( bad.has_settlement() ) // implies head_block_time > HARDFORK_CORE_216_TIME
      {
         const auto& mia_dyn = base.dynamic_asset_data_id(d);
         if( !bad.current_feed.settlement_price.is_null()
             && ( mia_dyn.current_supply == 0
                  || ~price::call_price(asset(mia_dyn.current_supply, o.asset_id),
                                        asset(bad.settlement_fund, bad.options.short_backing_asset),
                                        bad.current_feed.maintenance_collateral_ratio ) < bad.current_feed.settlement_price ) )
            d.revive_bitasset(base);
      }
      db().check_call_orders(base);
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) }



void_result asset_claim_fees_evaluator::do_evaluate( const asset_claim_fees_operation& o )
{ try {
   FC_ASSERT( db().head_block_time() > HARDFORK_413_TIME );
   FC_ASSERT( o.amount_to_claim.asset_id(db()).issuer == o.issuer, "Asset fees may only be claimed by the issuer" );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result asset_claim_fees_evaluator::do_apply( const asset_claim_fees_operation& o )
{ try {
   database& d = db();

   const asset_object& a = o.amount_to_claim.asset_id(d);
   const asset_dynamic_data_object& addo = a.dynamic_asset_data_id(d);
   FC_ASSERT( o.amount_to_claim.amount <= addo.accumulated_fees, "Attempt to claim more fees than have accumulated", ("addo",addo) );

   d.modify( addo, [&]( asset_dynamic_data_object& _addo  ) {
     _addo.accumulated_fees -= o.amount_to_claim.amount;
   });

   d.adjust_balance( o.issuer, o.amount_to_claim );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result asset_claim_pool_evaluator::do_evaluate( const asset_claim_pool_operation& o )
{ try {
    FC_ASSERT( db().head_block_time() >= HARDFORK_CORE_188_TIME,
         "This operation is only available after Hardfork #188!" );
    FC_ASSERT( o.asset_id(db()).issuer == o.issuer, "Asset fee pool may only be claimed by the issuer" );

    return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_claim_pool_evaluator::do_apply( const asset_claim_pool_operation& o )
{ try {
    database& d = db();

    const asset_object& a = o.asset_id(d);
    const asset_dynamic_data_object& addo = a.dynamic_asset_data_id(d);
    FC_ASSERT( o.amount_to_claim.amount <= addo.fee_pool, "Attempt to claim more fees than is available", ("addo",addo) );

    d.modify( addo, [&o]( asset_dynamic_data_object& _addo  ) {
        _addo.fee_pool -= o.amount_to_claim.amount;
    });

    d.adjust_balance( o.issuer, o.amount_to_claim );

    return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


} } // graphene::chain
