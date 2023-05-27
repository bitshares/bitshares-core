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

namespace graphene { namespace chain {
namespace detail {

   // TODO review and remove code below and links to it after hf_1774
   void check_asset_options_hf_1774(const fc::time_point_sec& block_time, const asset_options& options)
   {
      if( block_time < HARDFORK_1774_TIME )
      {
         FC_ASSERT( !options.extensions.value.reward_percent.valid() ||
                    *options.extensions.value.reward_percent < GRAPHENE_100_PERCENT,
            "Asset extension reward percent must be less than 100% till HARDFORK_1774_TIME!");
      }
   }

   void check_bitasset_options_hf_bsip74( const fc::time_point_sec& block_time, const bitasset_options& options)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      FC_ASSERT( block_time >= HARDFORK_CORE_BSIP74_TIME
            || !options.extensions.value.margin_call_fee_ratio.valid(),
            "A BitAsset's MCFR cannot be set before Hardfork BSIP74" );
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_81_TIME
   void check_asset_options_hf_bsip81(const fc::time_point_sec& block_time, const asset_options& options)
   {
      if (block_time < HARDFORK_BSIP_81_TIME) {
         // Taker fees should not be set until activation of BSIP81
         FC_ASSERT(!options.extensions.value.taker_fee_percent.valid(),
                   "Taker fee percent should not be defined before HARDFORK_BSIP_81_TIME");
      }
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_48_75_TIME
   void check_asset_options_hf_bsip_48_75(const fc::time_point_sec& block_time, const asset_options& options)
   {
      if ( !HARDFORK_BSIP_48_75_PASSED( block_time ) )
      {
         // new issuer permissions should not be set until activation of BSIP_48_75
         FC_ASSERT( 0 == (options.issuer_permissions & (uint16_t)(~ASSET_ISSUER_PERMISSION_ENABLE_BITS_MASK)),
                    "New asset issuer permission bits should not be set before HARDFORK_BSIP_48_75_TIME" );
         // Note: no check for flags here because we didn't check in the past
      }
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_48_75_TIME
   void check_bitasset_options_hf_bsip_48_75(const fc::time_point_sec& block_time, const bitasset_options& options)
   {
      if ( !HARDFORK_BSIP_48_75_PASSED( block_time ) )
      {
         // new params should not be set until activation of BSIP_48_75
         FC_ASSERT( !options.extensions.value.maintenance_collateral_ratio.valid(),
                    "Maintenance collateral ratio should not be defined by asset owner "
                    "before HARDFORK_BSIP_48_75_TIME" );
         FC_ASSERT( !options.extensions.value.maximum_short_squeeze_ratio.valid(),
                    "Maximum short squeeze ratio should not be defined by asset owner "
                    "before HARDFORK_BSIP_48_75_TIME" );
      }
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_48_75_TIME
   void check_asset_update_extensions_hf_bsip_48_75( const fc::time_point_sec& block_time,
                                                     const asset_update_operation::ext& extensions )
   {
      if ( !HARDFORK_BSIP_48_75_PASSED( block_time ) )
      {
         // new extensions should not be set until activation of BSIP_48_75
         FC_ASSERT( !extensions.new_precision.valid(),
                    "new_precision should not be set before HARDFORK_BSIP_48_75_TIME" );
         FC_ASSERT( !extensions.skip_core_exchange_rate.valid(),
                    "skip_core_exchange_rate should not be set before HARDFORK_BSIP_48_75_TIME" );
      }
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_77_TIME
   void check_asset_publish_feed_extensions_hf_bsip77( const fc::time_point_sec& block_time,
                                                       const asset_publish_feed_operation::ext& extensions )
   {
      if ( !HARDFORK_BSIP_77_PASSED( block_time ) )
      {
         // new extensions should not be set until activation of BSIP_77
         FC_ASSERT( !extensions.initial_collateral_ratio.valid(),
                   "Initial collateral ratio should not be defined before HARDFORK_BSIP_77_TIME" );
      }
   }

   // TODO review and remove code below and links to it after HARDFORK_BSIP_77_TIME
   void check_bitasset_options_hf_bsip77(const fc::time_point_sec& block_time, const bitasset_options& options)
   {
      if ( !HARDFORK_BSIP_77_PASSED( block_time ) ) {
         // ICR should not be set until activation of BSIP77
         FC_ASSERT(!options.extensions.value.initial_collateral_ratio.valid(),
                   "Initial collateral ratio should not be defined before HARDFORK_BSIP_77_TIME");
      }
   }

   void check_bitasset_options_hf_bsip87(const fc::time_point_sec& block_time, const bitasset_options& options)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      FC_ASSERT( !options.extensions.value.force_settle_fee_percent.valid()
                 || block_time >= HARDFORK_CORE_BSIP87_TIME,
                 "A BitAsset's FSFP cannot be set before Hardfork BSIP87" );
   }

   void check_asset_claim_fees_hardfork_87_74_collatfee(const fc::time_point_sec& block_time,
                                                        const asset_claim_fees_operation& op)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      FC_ASSERT( !op.extensions.value.claim_from_asset_id.valid() ||
                 block_time >= HARDFORK_CORE_BSIP_87_74_COLLATFEE_TIME,
                 "Collateral-denominated fees are not yet active and therefore cannot be claimed." );
   }

   void check_asset_options_hf_core2281( const fc::time_point_sec& next_maint_time, const asset_options& options)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      if ( !HARDFORK_CORE_2281_PASSED(next_maint_time) )
      {
         // new issuer permissions should not be set until activation of the hardfork
         FC_ASSERT( 0 == (options.issuer_permissions & asset_issuer_permission_flags::disable_collateral_bidding),
                    "New asset issuer permission bit 'disable_collateral_bidding' should not be set "
                    "before Hardfork core-2281" );
         // Note: checks about flags are more complicated due to old bugs,
         //       and likely can not be removed after hardfork, so do not put them here
      }
   }

   void check_asset_options_hf_core2467(const fc::time_point_sec& next_maint_time, const asset_options& options)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      if ( !HARDFORK_CORE_2467_PASSED(next_maint_time) )
      {
         // new issuer permissions should not be set until activation of the hardfork
         FC_ASSERT( 0 == (options.issuer_permissions & asset_issuer_permission_flags::disable_bsrm_update),
                    "New asset issuer permission bit 'disable_bsrm_update' should not be set "
                    "before Hardfork core-2467" );
      }
   }

   void check_bitasset_opts_hf_core2467(const fc::time_point_sec& next_maint_time, const bitasset_options& options)
   {
      // HF_REMOVABLE: Following hardfork check should be removable after hardfork date passes:
      if ( !HARDFORK_CORE_2467_PASSED(next_maint_time) )
      {
         FC_ASSERT( !options.extensions.value.black_swan_response_method.valid(),
                    "A BitAsset's black swan response method cannot be set before Hardfork core-2467" );
      }
   }

} // graphene::chain::detail

void_result asset_create_evaluator::do_evaluate( const asset_create_operation& op ) const
{ try {

   const database& d = db();
   const time_point_sec now = d.head_block_time();
   const fc::time_point_sec next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   // Hardfork Checks:
   detail::check_asset_options_hf_1774(now, op.common_options);
   detail::check_asset_options_hf_bsip_48_75(now, op.common_options);
   detail::check_asset_options_hf_bsip81(now, op.common_options);
   detail::check_asset_options_hf_core2281( next_maint_time, op.common_options ); // HF_REMOVABLE
   detail::check_asset_options_hf_core2467( next_maint_time, op.common_options ); // HF_REMOVABLE
   if( op.bitasset_opts ) {
      detail::check_bitasset_options_hf_bsip_48_75( now, *op.bitasset_opts );
      detail::check_bitasset_options_hf_bsip74( now, *op.bitasset_opts ); // HF_REMOVABLE
      detail::check_bitasset_options_hf_bsip77( now, *op.bitasset_opts ); // HF_REMOVABLE
      detail::check_bitasset_options_hf_bsip87( now, *op.bitasset_opts ); // HF_REMOVABLE
      detail::check_bitasset_opts_hf_core2467( next_maint_time, *op.bitasset_opts ); // HF_REMOVABLE
   }

   // TODO move as many validations as possible to validate() if not triggered before hardfork
   if( HARDFORK_CORE_2281_PASSED( next_maint_time ) )
   {
      op.common_options.validate_flags( op.bitasset_opts.valid() );
   }
   else if( HARDFORK_BSIP_48_75_PASSED( now ) )
   {
      // do not allow the 'disable_collateral_bidding' bit
      op.common_options.validate_flags( op.bitasset_opts.valid(), false );
   }

   const auto& chain_parameters = d.get_global_properties().parameters;
   FC_ASSERT( op.common_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   FC_ASSERT( op.common_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );

   // Check that all authorities do exist
   for( auto id : op.common_options.whitelist_authorities )
      d.get(id);
   for( auto id : op.common_options.blacklist_authorities )
      d.get(id);

   auto& asset_indx = d.get_index_type<asset_index>().indices().get<by_symbol>();
   auto asset_symbol_itr = asset_indx.find( op.symbol );
   FC_ASSERT( asset_symbol_itr == asset_indx.end() );

   // This must remain due to "BOND.CNY" being allowed before this HF
   if( now > HARDFORK_385_TIME )
   {
      auto dotpos = op.symbol.rfind( '.' );
      if( dotpos != std::string::npos )
      {
         auto prefix = op.symbol.substr( 0, dotpos );
         auto asset_prefix_itr = asset_indx.find( prefix );
         FC_ASSERT( asset_prefix_itr != asset_indx.end(),
                    "Asset ${s} may only be created by issuer of asset ${p}, but asset ${p} has not been created",
                    ("s",op.symbol)("p",prefix) );
         FC_ASSERT( asset_prefix_itr->issuer == op.issuer, "Asset ${s} may only be created by issuer of ${p}, ${i}",
                    ("s",op.symbol)("p",prefix)("i", op.issuer(d).name) );
      }
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
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void asset_create_evaluator::pay_fee()
{
   constexpr int64_t two = 2;
   fee_is_odd = ( ( core_fee_paid.value % two ) != 0 );
   core_fee_paid -= core_fee_paid.value / two;
   generic_evaluator::pay_fee();
}

object_id_type asset_create_evaluator::do_apply( const asset_create_operation& op ) const
{ try {
   database& d = db();

   bool hf_429 = fee_is_odd && d.head_block_time() > HARDFORK_CORE_429_TIME;

   const asset_dynamic_data_object& dyn_asset =
      d.create<asset_dynamic_data_object>( [hf_429,this]( asset_dynamic_data_object& a ) {
         a.current_supply = 0;
         a.fee_pool = core_fee_paid - (hf_429 ? 1 : 0);
      });

   if( fee_is_odd && !hf_429 )
   {
      d.modify( d.get_core_dynamic_data(), []( asset_dynamic_data_object& dd ) {
         ++dd.current_supply;
      });
   }

   asset_bitasset_data_id_type bit_asset_id;

   auto next_asset_id = d.get_index_type<asset_index>().get_next_id();

   if( op.bitasset_opts.valid() )
      bit_asset_id = d.create<asset_bitasset_data_object>( [&op,next_asset_id]( asset_bitasset_data_object& a ) {
            a.options = *op.bitasset_opts;
            a.is_prediction_market = op.is_prediction_market;
            a.asset_id = next_asset_id;
         }).id;

   const asset_object& new_asset =
     d.create<asset_object>( [&op,next_asset_id,&dyn_asset,bit_asset_id,&d]( asset_object& a ) {
         a.issuer = op.issuer;
         a.symbol = op.symbol;
         a.precision = op.precision;
         a.options = op.common_options;
         if( 0 == a.options.core_exchange_rate.base.asset_id.instance.value )
            a.options.core_exchange_rate.quote.asset_id = next_asset_id;
         else
            a.options.core_exchange_rate.base.asset_id = next_asset_id;
         a.dynamic_asset_data_id = dyn_asset.id;
         if( op.bitasset_opts.valid() )
            a.bitasset_data_id = bit_asset_id;
         a.creation_block_num = d._current_block_num;
         a.creation_time      = d._current_block_time;
      });
   FC_ASSERT( new_asset.id == next_asset_id, "Unexpected object database error, object id mismatch" );

   return new_asset.id;
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result asset_issue_evaluator::do_evaluate( const asset_issue_operation& o )
{ try {
   const database& d = db();

   const asset_object& a = o.asset_to_issue.asset_id(d);
   FC_ASSERT( o.issuer == a.issuer );
   FC_ASSERT( !a.is_market_issued(), "Cannot manually issue a market-issued asset." );

   FC_ASSERT( !a.is_liquidity_pool_share_asset(), "Cannot manually issue a liquidity pool share asset." );

   FC_ASSERT( a.can_create_new_supply(), "Can not create new supply" );

   to_account = &o.issue_to_account(d);
   FC_ASSERT( is_authorized_asset( d, *to_account, a ) );

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   FC_ASSERT( (asset_dyn_data->current_supply + o.asset_to_issue.amount) <= a.options.max_supply );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_issue_evaluator::do_apply( const asset_issue_operation& o ) const
{ try {
   db().adjust_balance( o.issue_to_account, o.asset_to_issue );

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ){
        data.current_supply += o.asset_to_issue.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

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

   from_account = fee_paying_account;
   FC_ASSERT( is_authorized_asset( d, *from_account, a ) );

   asset_dyn_data = &a.dynamic_asset_data_id(d);
   if( !a.is_liquidity_pool_share_asset() )
   {
      FC_ASSERT( asset_dyn_data->current_supply >= o.amount_to_reserve.amount,
                 "Can not reserve an amount that is more than the current supply" );
   }
   else
   {
      FC_ASSERT( asset_dyn_data->current_supply > o.amount_to_reserve.amount,
                 "The asset is a liquidity pool share asset thus can only reserve an amount "
                 "that is less than the current supply" );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_reserve_evaluator::do_apply( const asset_reserve_operation& o ) const
{ try {
   db().adjust_balance( o.payer, -o.amount_to_reserve );

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ){
        data.current_supply -= o.amount_to_reserve.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_fund_fee_pool_evaluator::do_evaluate(const asset_fund_fee_pool_operation& o)
{ try {
   const database& d = db();

   const asset_object& a = o.asset_id(d);

   asset_dyn_data = &a.dynamic_asset_data_id(d);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_fund_fee_pool_evaluator::do_apply(const asset_fund_fee_pool_operation& o) const
{ try {
   db().adjust_balance(o.from_account, -o.amount);

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ) {
      data.fee_pool += o.amount;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

static void validate_new_issuer( const database& d, const asset_object& a, account_id_type new_issuer )
{ try {
   FC_ASSERT(d.find(new_issuer), "New issuer account does not exist");
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
} FC_CAPTURE_AND_RETHROW( (a)(new_issuer) ) } // GCOVR_EXCL_LINE

void_result asset_update_evaluator::do_evaluate(const asset_update_operation& o)
{ try {
   const database& d = db();
   const time_point_sec now = d.head_block_time();
   const fc::time_point_sec next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   // Hardfork Checks:
   detail::check_asset_options_hf_1774(now, o.new_options);
   detail::check_asset_options_hf_bsip_48_75(now, o.new_options);
   detail::check_asset_options_hf_bsip81(now, o.new_options);
   detail::check_asset_options_hf_core2281( next_maint_time, o.new_options ); // HF_REMOVABLE
   detail::check_asset_options_hf_core2467( next_maint_time, o.new_options ); // HF_REMOVABLE
   detail::check_asset_update_extensions_hf_bsip_48_75( now, o.extensions.value );

   bool hf_bsip_48_75_passed = ( HARDFORK_BSIP_48_75_PASSED( now ) );
   bool hf_core_2281_passed = ( HARDFORK_CORE_2281_PASSED( next_maint_time ) );
   bool hf_core_2467_passed = ( HARDFORK_CORE_2467_PASSED( next_maint_time ) );

   const asset_object& a = o.asset_to_update(d);
   auto a_copy = a;
   a_copy.options = o.new_options;
   a_copy.validate();

   if( o.new_issuer )
   {
      FC_ASSERT( now < HARDFORK_CORE_199_TIME,
                 "Since Hardfork #199, updating issuer requires the use of asset_update_issuer_operation.");
      validate_new_issuer( d, a, *o.new_issuer );
   }

   if( a.is_market_issued() )
      bitasset_data = &a.bitasset_data(d);

   if( hf_core_2467_passed )
   {
      // Unable to set non-UIA issuer permission bits on UIA
      if( !a.is_market_issued() )
         FC_ASSERT( 0 == ( o.new_options.issuer_permissions & NON_UIA_ONLY_ISSUER_PERMISSION_MASK ),
                    "Unable to set non-UIA issuer permission bits on UIA" );
      // Unable to set disable_bsrm_update issuer permission bit on PM
      else if( bitasset_data->is_prediction_market )
         FC_ASSERT( 0 == ( o.new_options.issuer_permissions & disable_bsrm_update ),
                    "Unable to set disable_bsrm_update issuer permission bit on PM" );
      // else do nothing
   }

   uint16_t enabled_issuer_permissions_mask = a.options.get_enabled_issuer_permissions_mask();
   if( hf_bsip_48_75_passed && a.is_market_issued() && bitasset_data->is_prediction_market )
   {
      // Note: if the global_settle permission was unset, it should be corrected
      FC_ASSERT( a_copy.can_global_settle(),
                 "The global_settle permission should be enabled for prediction markets" );
      enabled_issuer_permissions_mask |= global_settle;
   }

   const auto& dyn_data = a.dynamic_asset_data_id(d);
   if( dyn_data.current_supply != 0 )
   {
      // new issuer_permissions must be subset of old issuer permissions
      if( hf_core_2467_passed && !a.is_market_issued() ) // for UIA, ignore non-UIA bits
         FC_ASSERT( 0 == ( ( o.new_options.get_enabled_issuer_permissions_mask()
                        & (uint16_t)(~enabled_issuer_permissions_mask) ) & UIA_ASSET_ISSUER_PERMISSION_MASK ),
                 "Cannot reinstate previously revoked issuer permissions on a UIA if current supply is non-zero, "
                 "unless to unset non-UIA issuer permission bits.");
      else if( hf_core_2467_passed && bitasset_data->is_prediction_market ) // for PM, ignore disable_bsrm_update
         FC_ASSERT( 0 == ( ( o.new_options.get_enabled_issuer_permissions_mask()
                        & (uint16_t)(~enabled_issuer_permissions_mask) ) & (uint16_t)(~disable_bsrm_update) ),
                 "Cannot reinstate previously revoked issuer permissions on a PM if current supply is non-zero, "
                 "unless to unset the disable_bsrm_update issuer permission bit.");
      else
         FC_ASSERT( 0 == ( o.new_options.get_enabled_issuer_permissions_mask()
                        & (uint16_t)(~enabled_issuer_permissions_mask) ),
                 "Cannot reinstate previously revoked issuer permissions on an asset if current supply is non-zero.");
      // precision can not be changed
      FC_ASSERT( !o.extensions.value.new_precision.valid(),
                 "Cannot update precision if current supply is non-zero" );

      if( hf_bsip_48_75_passed ) // TODO review after hard fork, probably can assert unconditionally
      {
         FC_ASSERT( dyn_data.current_supply <= o.new_options.max_supply,
                    "Max supply should not be smaller than current supply" );
      }
   }

   // If an invalid bit was set in flags, it should be unset
   // TODO move as many validations as possible to validate() if not triggered before hardfork
   if( hf_core_2281_passed )
   {
      o.new_options.validate_flags( a.is_market_issued() );
   }
   else if( hf_bsip_48_75_passed )
   {
      // do not allow the 'disable_collateral_bidding' bit
      o.new_options.validate_flags( a.is_market_issued(), false );
   }

   // changed flags must be subset of old issuer permissions
   if( hf_bsip_48_75_passed )
   {
      // Note: if an invalid bit was set, it can be unset regardless of the permissions
      uint16_t valid_flags_mask = hf_core_2281_passed ? VALID_FLAGS_MASK
                                                      : (VALID_FLAGS_MASK & (uint16_t)(~disable_collateral_bidding));
      uint16_t check_bits = a.is_market_issued() ? valid_flags_mask : UIA_VALID_FLAGS_MASK;

      FC_ASSERT( 0 == ( (o.new_options.flags ^ a.options.flags) & check_bits
                        & (uint16_t)(~enabled_issuer_permissions_mask) ),
                 "Flag change is forbidden by issuer permissions" );
   }
   else
   {
      FC_ASSERT( 0 == ( (o.new_options.flags ^ a.options.flags) & (uint16_t)(~a.options.issuer_permissions) ),
                 "Flag change is forbidden by issuer permissions" );
   }

   asset_to_update = &a;
   FC_ASSERT( o.issuer == a.issuer,
              "Incorrect issuer for asset! (${o.issuer} != ${a.issuer})",
              ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   FC_ASSERT( a.can_update_max_supply() || a.options.max_supply == o.new_options.max_supply,
              "Can not update max supply" );

   if( o.extensions.value.new_precision.valid() )
   {
      FC_ASSERT( *o.extensions.value.new_precision != a.precision,
                 "Specified a new precision but it does not change" );

      if( a.is_market_issued() )
         FC_ASSERT( !bitasset_data->is_prediction_market, "Can not update precision of a prediction market" );

      // If any other asset is backed by this asset, this asset's precision can't be updated
      const auto& idx = d.get_index_type<graphene::chain::asset_bitasset_data_index>()
                         .indices().get<by_short_backing_asset>();
      auto itr = idx.lower_bound( o.asset_to_update );
      bool backing_another_asset = ( itr != idx.end() && itr->options.short_backing_asset == o.asset_to_update );
      FC_ASSERT( !backing_another_asset,
                 "Asset ${a} is backed by this asset, can not update precision",
                 ("a",itr->asset_id) );
   }

   const auto& chain_parameters = d.get_global_properties().parameters;

   FC_ASSERT( o.new_options.whitelist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.whitelist_authorities )
      d.get(id);
   FC_ASSERT( o.new_options.blacklist_authorities.size() <= chain_parameters.maximum_asset_whitelist_authorities );
   for( auto id : o.new_options.blacklist_authorities )
      d.get(id);

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) } // GCOVR_EXCL_LINE

void_result asset_update_evaluator::do_apply(const asset_update_operation& o)
{ try {
   database& d = db();

   // If we are now disabling force settlements, cancel all open force settlement orders
   if( 0 != (o.new_options.flags & disable_force_settle) && asset_to_update->can_force_settle() )
   {
      const auto& idx = d.get_index_type<force_settlement_index>().indices().get<by_expiration>();
      // Funky iteration code because we're removing objects as we go. We have to re-initialize itr every loop instead
      // of simply incrementing it.
      for( auto itr = idx.lower_bound(o.asset_to_update);
           itr != idx.end() && itr->settlement_asset_id() == o.asset_to_update;
           itr = idx.lower_bound(o.asset_to_update) )
         d.cancel_settle_order(*itr);
   }

   // If we are now disabling collateral bidding, cancel all open collateral bids
   if( 0 != (o.new_options.flags & disable_collateral_bidding) && asset_to_update->can_bid_collateral() )
   {
      const auto& bid_idx = d.get_index_type< collateral_bid_index >().indices().get<by_price>();
      auto itr = bid_idx.lower_bound( o.asset_to_update );
      const auto end = bid_idx.upper_bound( o.asset_to_update );
      while( itr != end )
      {
         const collateral_bid_object& bid = *itr;
         ++itr;
         d.cancel_bid( bid );
      }
   }

   // For market-issued assets, if core exchange rate changed, update flag in bitasset data
   if( !o.extensions.value.skip_core_exchange_rate.valid() && asset_to_update->is_market_issued()
          && asset_to_update->options.core_exchange_rate != o.new_options.core_exchange_rate )
   {
      const auto& bitasset = ( bitasset_data ? *bitasset_data : asset_to_update->bitasset_data(d) );
      if( !bitasset.asset_cer_updated )
      {
         d.modify( bitasset, [](asset_bitasset_data_object& b)
         {
            b.asset_cer_updated = true;
         });
      }
   }

   d.modify(*asset_to_update, [&o](asset_object& a) {
      if( o.new_issuer )
         a.issuer = *o.new_issuer;
      if( o.extensions.value.new_precision.valid() )
         a.precision = *o.extensions.value.new_precision;
      if( o.extensions.value.skip_core_exchange_rate.valid() )
      {
         const auto old_cer = a.options.core_exchange_rate;
         a.options = o.new_options;
         a.options.core_exchange_rate = old_cer;
      }
      else
         a.options = o.new_options;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_update_issuer_evaluator::do_evaluate(const asset_update_issuer_operation& o)
{ try {
   database& d = db();

   const asset_object& a = o.asset_to_update(d);

   validate_new_issuer( d, a, o.new_issuer );

   asset_to_update = &a;
   FC_ASSERT( o.issuer == a.issuer,
              "Incorrect issuer for asset! (${o.issuer} != ${a.issuer})",
              ("o.issuer", o.issuer)("a.issuer", a.issuer) );

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) } // GCOVR_EXCL_LINE

void_result asset_update_issuer_evaluator::do_apply(const asset_update_issuer_operation& o)
{ try {
   database& d = db();
   d.modify(*asset_to_update, [&](asset_object& a) {
      a.issuer = o.new_issuer;
   });

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

/****************
 * Loop through assets, looking for ones that are backed by the asset being changed. When found,
 * perform checks to verify validity
 *
 * @param d the database
 * @param op the bitasset update operation being performed
 * @param new_backing_asset
 * @param true if after hf 922/931 (if nothing triggers, this and the logic that depends on it
 *    should be removed).
 */
void check_children_of_bitasset(const database& d, const asset_update_bitasset_operation& op,
      const asset_object& new_backing_asset)
{
   // no need to do these checks if the new backing asset is CORE
   if ( new_backing_asset.get_id() == asset_id_type() )
      return;

   // loop through all assets that have this asset as a backing asset
   const auto& idx = d.get_index_type<graphene::chain::asset_bitasset_data_index>()
         .indices()
         .get<by_short_backing_asset>();
   auto backed_range = idx.equal_range(op.asset_to_update);
   std::for_each( backed_range.first, backed_range.second,
         [&new_backing_asset, &d, &op](const asset_bitasset_data_object& bitasset_data)
         {
            const auto& child = bitasset_data.asset_id(d);
            FC_ASSERT( child.get_id() != op.new_options.short_backing_asset,
                  "A BitAsset would be invalidated by changing this backing asset "
                  "('A' backed by 'B' backed by 'A')." );

            FC_ASSERT( child.issuer != GRAPHENE_COMMITTEE_ACCOUNT,
                  "A blockchain-controlled market asset would be invalidated by changing this backing asset." );

            FC_ASSERT( !new_backing_asset.is_market_issued(),
                  "A non-blockchain controlled BitAsset would be invalidated by changing this backing asset.");
         } ); // end of lambda and std::for_each()
} // check_children_of_bitasset

void_result asset_update_bitasset_evaluator::do_evaluate(const asset_update_bitasset_operation& op)
{ try {
   const database& d = db();
   const time_point_sec now = d.head_block_time();
   const fc::time_point_sec next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   // Hardfork Checks:
   detail::check_bitasset_options_hf_bsip_48_75( now, op.new_options );
   detail::check_bitasset_options_hf_bsip74( now, op.new_options ); // HF_REMOVABLE
   detail::check_bitasset_options_hf_bsip77( now, op.new_options ); // HF_REMOVABLE
   detail::check_bitasset_options_hf_bsip87( now, op.new_options ); // HF_REMOVABLE
   detail::check_bitasset_opts_hf_core2467( next_maint_time, op.new_options ); // HF_REMOVABLE

   const asset_object& asset_obj = op.asset_to_update(d);

   FC_ASSERT( asset_obj.is_market_issued(), "Cannot update BitAsset-specific settings on a non-BitAsset." );

   FC_ASSERT( op.issuer == asset_obj.issuer, "Only asset issuer can update bitasset_data of the asset." );

   const asset_bitasset_data_object& current_bitasset_data = asset_obj.bitasset_data(d);

   if( !HARDFORK_CORE_2282_PASSED( next_maint_time ) )
      FC_ASSERT( !current_bitasset_data.is_globally_settled(),
                 "Cannot update a bitasset after a global settlement has executed" );

   if( current_bitasset_data.is_prediction_market )
      FC_ASSERT( !op.new_options.extensions.value.black_swan_response_method.valid(),
                 "Can not set black_swan_response_method for Prediction Markets" );

   // TODO simplify code below when made sure operator==(optional,optional) works
   if( !asset_obj.can_owner_update_mcr() )
   {
      // check if MCR will change
      const auto& old_mcr = current_bitasset_data.options.extensions.value.maintenance_collateral_ratio;
      const auto& new_mcr = op.new_options.extensions.value.maintenance_collateral_ratio;
      bool mcr_changed = ( ( old_mcr.valid() != new_mcr.valid() )
                           || ( old_mcr.valid() && *old_mcr != *new_mcr ) );
      FC_ASSERT( !mcr_changed, "No permission to update MCR" );
   }
   if( !asset_obj.can_owner_update_icr() )
   {
      // check if ICR will change
      const auto& old_icr = current_bitasset_data.options.extensions.value.initial_collateral_ratio;
      const auto& new_icr = op.new_options.extensions.value.initial_collateral_ratio;
      bool icr_changed = ( ( old_icr.valid() != new_icr.valid() )
                           || ( old_icr.valid() && *old_icr != *new_icr ) );
      FC_ASSERT( !icr_changed, "No permission to update ICR" );
   }
   if( !asset_obj.can_owner_update_mssr() )
   {
      // check if MSSR will change
      const auto& old_mssr = current_bitasset_data.options.extensions.value.maximum_short_squeeze_ratio;
      const auto& new_mssr = op.new_options.extensions.value.maximum_short_squeeze_ratio;
      bool mssr_changed = ( ( old_mssr.valid() != new_mssr.valid() )
                           || ( old_mssr.valid() && *old_mssr != *new_mssr ) );
      FC_ASSERT( !mssr_changed, "No permission to update MSSR" );
   }
   // check if BSRM will change
   const auto old_bsrm = current_bitasset_data.get_black_swan_response_method();
   const auto new_bsrm = op.new_options.get_black_swan_response_method();
   if( old_bsrm != new_bsrm )
   {
      FC_ASSERT( asset_obj.can_owner_update_bsrm(), "No permission to update BSRM" );
      FC_ASSERT( !current_bitasset_data.is_globally_settled(),
                 "Unable to update BSRM when the asset has been globally settled" );

      // Note: it is probably OK to allow BSRM update, be conservative here so far
      using bsrm_type = bitasset_options::black_swan_response_type;
      if( bsrm_type::individual_settlement_to_fund == old_bsrm )
         FC_ASSERT( !current_bitasset_data.is_individually_settled_to_fund(),
                 "Unable to update BSRM when the individual settlement pool (for force-settlements) is not empty" );
      else if( bsrm_type::individual_settlement_to_order == old_bsrm )
         FC_ASSERT( !d.find_settled_debt_order( op.asset_to_update ),
                 "Unable to update BSRM when there exists an individual settlement order" );

      // Since we do not allow updating in some cases (above), only check no_settlement here
      if( bsrm_type::no_settlement == old_bsrm || bsrm_type::no_settlement == new_bsrm )
         update_feeds_due_to_bsrm_change = true;
   }


   // hf 922_931 is a consensus/logic change. This hf cannot be removed.
   bool after_hf_core_922_931 = ( next_maint_time > HARDFORK_CORE_922_931_TIME );

   // Are we changing the backing asset?
   if( op.new_options.short_backing_asset != current_bitasset_data.options.short_backing_asset )
   {
      FC_ASSERT( !current_bitasset_data.is_globally_settled(),
                 "Cannot change backing asset after a global settlement has executed" );

      const asset_dynamic_data_object& dyn = asset_obj.dynamic_asset_data_id(d);
      FC_ASSERT( dyn.current_supply == 0,
                 "Cannot change backing asset if there is already a current supply." );

      FC_ASSERT( dyn.accumulated_collateral_fees == 0,
                 "Must claim collateral-denominated fees before changing backing asset." );

      const asset_object& new_backing_asset = op.new_options.short_backing_asset(d); // check if the asset exists

      if( after_hf_core_922_931 )
      {
         FC_ASSERT( op.new_options.short_backing_asset != asset_obj.get_id(),
                    "Cannot update an asset to be backed by itself." );

         if( current_bitasset_data.is_prediction_market )
         {
            FC_ASSERT( asset_obj.precision == new_backing_asset.precision,
                       "The precision of the asset and backing asset must be equal." );
         }

         if( asset_obj.issuer == GRAPHENE_COMMITTEE_ACCOUNT )
         {
            if( new_backing_asset.is_market_issued() )
            {
               FC_ASSERT( new_backing_asset.bitasset_data(d).options.short_backing_asset == asset_id_type(),
                          "May not modify a blockchain-controlled market asset to be backed by an asset which is not "
                          "backed by CORE." );

               check_children_of_bitasset( d, op, new_backing_asset );
            }
            else
            {
               FC_ASSERT( new_backing_asset.get_id() == asset_id_type(),
                          "May not modify a blockchain-controlled market asset to be backed by an asset which is not "
                          "market issued asset nor CORE." );
            }
         }
         else
         {
            // not a committee issued asset

            // If we're changing to a backing_asset that is not CORE, we need to look at any
            // asset ( "CHILD" ) that has this one as a backing asset. If CHILD is committee-owned,
            // the change is not allowed. If CHILD is user-owned, then this asset's backing
            // asset must be either CORE or a UIA.
            if ( new_backing_asset.get_id() != asset_id_type() ) // not backed by CORE
            {
               check_children_of_bitasset( d, op, new_backing_asset );
            }

         }

         // Check if the new backing asset is itself backed by something. It must be CORE or a UIA
         if ( new_backing_asset.is_market_issued() )
         {
            asset_id_type backing_backing_asset_id = new_backing_asset.bitasset_data(d).options.short_backing_asset;
            FC_ASSERT( (backing_backing_asset_id == asset_id_type()
                        || !backing_backing_asset_id(d).is_market_issued()),
                  "A BitAsset cannot be backed by a BitAsset that itself is backed by a BitAsset.");
         }
      }
   }

   const auto& chain_parameters = d.get_global_properties().parameters;
   if( after_hf_core_922_931 )
   {
      FC_ASSERT( op.new_options.feed_lifetime_sec > chain_parameters.block_interval,
            "Feed lifetime must exceed block interval." );
      FC_ASSERT( op.new_options.force_settlement_delay_sec > chain_parameters.block_interval,
            "Force settlement delay must exceed block interval." );
   }

   bitasset_to_update = &current_bitasset_data;
   asset_to_update = &asset_obj;

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

/*******
 * @brief Apply requested changes to bitasset options
 *
 * This applies the requested changes to the bitasset object. It also cleans up the
 * releated feeds, and checks conditions that might necessitate a call to check_call_orders.
 * Called from asset_update_bitasset_evaluator::do_apply().
 *
 * @param op the requested operation
 * @param db the database
 * @param bdo the actual database object
 * @param asset_to_update the asset_object related to this bitasset_data_object
 *
 * @returns true if we should check call orders, such as if if the feed price is changed, or some
 *    cases after hf core-868-890, or if the margin_call_fee_ratio has changed, which affects the
 *    matching price of margin call orders.
 */
static bool update_bitasset_object_options(
      const asset_update_bitasset_operation& op, database& db,
      asset_bitasset_data_object& bdo, const asset_object& asset_to_update,
      bool update_feeds_due_to_bsrm_change )
{
   const fc::time_point_sec next_maint_time = db.get_dynamic_global_properties().next_maintenance_time;
   bool after_hf_core_868_890 = ( next_maint_time > HARDFORK_CORE_868_890_TIME );

   const auto& head_time = db.head_block_time();
   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   // If the minimum number of feeds to calculate a median has changed, we need to recalculate the median
   bool should_update_feeds = false;
   if( op.new_options.minimum_feeds != bdo.options.minimum_feeds )
      should_update_feeds = true;

   // after hardfork core-868-890, we also should call update_bitasset_current_feed if the feed_lifetime_sec changed
   if( after_hf_core_868_890
         && op.new_options.feed_lifetime_sec != bdo.options.feed_lifetime_sec )
   {
      should_update_feeds = true;
   }

   // feeds must be reset if the backing asset is changed after hardfork core-868-890
   bool backing_asset_changed = false;
   bool is_witness_or_committee_fed = false;
   if( after_hf_core_868_890
         && op.new_options.short_backing_asset != bdo.options.short_backing_asset )
   {
      backing_asset_changed = true;
      should_update_feeds = true;
      if( 0 != ( asset_to_update.options.flags & ( witness_fed_asset | committee_fed_asset ) ) )
         is_witness_or_committee_fed = true;
   }

   // TODO simplify code below when made sure operator==(optional,optional) works
   // check if ICR will change
   if( !should_update_feeds )
   {
      const auto& old_icr = bdo.options.extensions.value.initial_collateral_ratio;
      const auto& new_icr = op.new_options.extensions.value.initial_collateral_ratio;
      bool icr_changed = ( ( old_icr.valid() != new_icr.valid() )
                           || ( old_icr.valid() && *old_icr != *new_icr ) );
      should_update_feeds = icr_changed;
   }
   // check if MCR will change
   if( !should_update_feeds )
   {
      const auto& old_mcr = bdo.options.extensions.value.maintenance_collateral_ratio;
      const auto& new_mcr = op.new_options.extensions.value.maintenance_collateral_ratio;
      bool mcr_changed = ( ( old_mcr.valid() != new_mcr.valid() )
                           || ( old_mcr.valid() && *old_mcr != *new_mcr ) );
      should_update_feeds = mcr_changed;
   }
   // check if MSSR will change
   if( !should_update_feeds )
   {
      const auto& old_mssr = bdo.options.extensions.value.maximum_short_squeeze_ratio;
      const auto& new_mssr = op.new_options.extensions.value.maximum_short_squeeze_ratio;
      bool mssr_changed = ( ( old_mssr.valid() != new_mssr.valid() )
                           || ( old_mssr.valid() && *old_mssr != *new_mssr ) );
      should_update_feeds = mssr_changed;
   }

   // check if MCFR will change
   const auto& old_mcfr = bdo.options.extensions.value.margin_call_fee_ratio;
   const auto& new_mcfr = op.new_options.extensions.value.margin_call_fee_ratio;
   const bool mcfr_changed = ( ( old_mcfr.valid() != new_mcfr.valid() )
                               || ( old_mcfr.valid() && *old_mcfr != *new_mcfr ) );

   // Apply changes to bitasset options
   bdo.options = op.new_options;

   // are we modifying the underlying? If so, reset the feeds
   if( backing_asset_changed )
   {
      if( is_witness_or_committee_fed )
      {
         bdo.feeds.clear();
      }
      else
      {
         // for non-witness-feeding and non-committee-feeding assets, modify all feeds
         // published by producers to nothing, since we can't simply remove them. For more information:
         // https://github.com/bitshares/bitshares-core/pull/832#issuecomment-384112633
         for( auto& current_feed : bdo.feeds )
         {
            current_feed.second.second.settlement_price = price();
         }
      }
   }

   bool feed_actually_changed = false;
   if( should_update_feeds || update_feeds_due_to_bsrm_change )
   {
      const auto old_feed = bdo.current_feed;
      const auto old_median_feed = bdo.median_feed;
      // skip recalculating median feed if it is not needed
      db.update_bitasset_current_feed( bdo, !should_update_feeds );
      // Note: we don't try to revive the bitasset here if it was GSed // TODO probably we should do it

      // TODO potential optimization: check only when should_update_feeds == true

      // We need to call check_call_orders if the settlement price changes after hardfork core-868-890
      feed_actually_changed = ( after_hf_core_868_890 && !old_feed.margin_call_params_equal( bdo.current_feed ) );

      if( !feed_actually_changed && after_core_hardfork_2582
            && !old_median_feed.margin_call_params_equal( bdo.median_feed ) )
         feed_actually_changed = true;
   }

   // Conditions under which a call to check_call_orders is needed in response to the updates applied here:
   const bool retval = feed_actually_changed || mcfr_changed;

   return retval;
}

void_result asset_update_bitasset_evaluator::do_apply(const asset_update_bitasset_operation& op)
{
   try
   {
      auto& db_conn = db();
      bool to_check_call_orders = false;

      db_conn.modify( *bitasset_to_update,
                      [&op, &to_check_call_orders, &db_conn, this]( asset_bitasset_data_object& bdo )
      {
         to_check_call_orders = update_bitasset_object_options( op, db_conn, bdo, *asset_to_update,
                                                                update_feeds_due_to_bsrm_change );
      });

      if( to_check_call_orders )
         // Process margin calls, allow black swan, not for a new limit order
         db_conn.check_call_orders( *asset_to_update, true, false, bitasset_to_update );

      return void_result();

   } FC_CAPTURE_AND_RETHROW( (op) ) // GCOVR_EXCL_LINE
}

void_result asset_update_feed_producers_evaluator::do_evaluate(const asset_update_feed_producers_operation& o)
{ try {
   database& d = db();

   FC_ASSERT( o.new_feed_producers.size() <= d.get_global_properties().parameters.maximum_asset_feed_publishers,
              "Cannot specify more feed producers than maximum allowed" );

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update feed producers on a non-BitAsset.");
   FC_ASSERT(0 == (a.options.flags & committee_fed_asset), "Cannot set feed producers on a committee-fed asset.");
   FC_ASSERT(0 == (a.options.flags & witness_fed_asset), "Cannot set feed producers on a witness-fed asset.");

   FC_ASSERT( a.issuer == o.issuer, "Only asset issuer can update feed producers of an asset" );

   asset_to_update = &a;

   // Make sure all producers exist. Check these after asset because account lookup is more expensive
   for( auto id : o.new_feed_producers )
      d.get(id);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_update_feed_producers_evaluator::do_apply(const asset_update_feed_producers_operation& o) const
{ try {
   database& d = db();
   const asset_bitasset_data_object& bitasset_to_update = asset_to_update->bitasset_data(d);
   d.modify( bitasset_to_update, [&o](asset_bitasset_data_object& a) {
      //This is tricky because I have a set of publishers coming in, but a map of publisher to feed is stored.
      //I need to update the map such that the keys match the new publishers, but not munge the old price feeds from
      //publishers who are being kept.

      // TODO possible performance optimization:
      //      Since both the map and the set are ordered by account already, we can iterate through them only once
      //      and avoid lookups while iterating by maintaining two iterators at same time.
      //      However, this operation is not used much, and both the set and the map are small,
      //      so likely we won't gain much with the optimization.

      //First, remove any old publishers who are no longer publishers
      for( auto itr = a.feeds.begin(); itr != a.feeds.end(); )
      {
         if( o.new_feed_producers.count(itr->first) == 0 )
            itr = a.feeds.erase(itr);
         else
            ++itr;
      }
      //Now, add any new publishers
      for( const account_id_type& acc : o.new_feed_producers )
      {
         a.feeds[acc];
      }
   });
   d.update_bitasset_current_feed( bitasset_to_update );
   // Note: we don't try to revive the bitasset here if it was GSed // TODO probably we should do it

   // Process margin calls, allow black swan, not for a new limit order
   d.check_call_orders( *asset_to_update, true, false, &bitasset_to_update );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

void_result asset_global_settle_evaluator::do_evaluate(const asset_global_settle_evaluator::operation_type& op)
{ try {
   const database& d = db();
   asset_to_settle = &op.asset_to_settle(d);
   FC_ASSERT( asset_to_settle->is_market_issued(), "Can only globally settle market-issued assets" );
   FC_ASSERT( asset_to_settle->can_global_settle(), "The global_settle permission of this asset is disabled" );
   FC_ASSERT( asset_to_settle->issuer == op.issuer, "Only asset issuer can globally settle an asset" );
   FC_ASSERT( asset_to_settle->dynamic_data(d).current_supply > 0,
              "Can not globally settle an asset with zero supply" );

   const asset_bitasset_data_object& _bitasset_data  = asset_to_settle->bitasset_data(d);
   // if there is a settlement for this asset, then no further global settle may be taken
   FC_ASSERT( !_bitasset_data.is_globally_settled(),
              "This asset has been globally settled, cannot globally settle again" );

   // Note: after core-2467 hard fork, there can be no debt position due to individual settlements, so we check here
   const call_order_object* least_collateralized_short = d.find_least_collateralized_short( _bitasset_data, true );
   if( least_collateralized_short )
   {
      FC_ASSERT( ( least_collateralized_short->get_debt() * op.settle_price )
                   <= least_collateralized_short->get_collateral(),
                 "Cannot globally settle at supplied price: least collateralized short lacks "
                 "sufficient collateral to settle." );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result asset_global_settle_evaluator::do_apply(const asset_global_settle_evaluator::operation_type& op)
{ try {
   database& d = db();
   d.globally_settle_asset( *asset_to_settle, op.settle_price );
   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result asset_settle_evaluator::do_evaluate(const asset_settle_evaluator::operation_type& op)
{ try {
   const database& d = db();
   asset_to_settle = &op.amount.asset_id(d);
   FC_ASSERT( asset_to_settle->is_market_issued(),
              "Can only force settle a predition market or a market issued asset" );

   const auto& bitasset = asset_to_settle->bitasset_data(d);
   FC_ASSERT( asset_to_settle->can_force_settle() || bitasset.is_globally_settled()
                 || bitasset.is_individually_settled_to_fund(),
              "Either the asset need to have the force_settle flag enabled, or it need to be globally settled, "
              "or the individual settlement pool (for force-settlements) is not empty" );

   if( bitasset.is_prediction_market )
   {
      FC_ASSERT( bitasset.is_globally_settled(),
                 "Global settlement must occur before force settling a prediction market" );
   }
   else if( bitasset.current_feed.settlement_price.is_null() )
   {
      // TODO check whether the HF check can be removed
      if( d.head_block_time() <= HARDFORK_CORE_216_TIME )
      {
         FC_THROW_EXCEPTION( insufficient_feeds,
                             "Before the core-216 hard fork, unable to force settle when there is no sufficient "
                             " price feeds, no matter if the asset has been globally settled" );
      }
      if( !bitasset.is_globally_settled() && !bitasset.is_individually_settled_to_fund() )
      {
         FC_THROW_EXCEPTION( insufficient_feeds,
                             "Cannot force settle with no price feed if the asset is not globally settled and the "
                             "individual settlement pool (for force-settlements) is not empty" );
      }
   }

   FC_ASSERT( d.get_balance( op.account, op.amount.asset_id ) >= op.amount, "Insufficient balance" );

   // Since hard fork core-973, check asset authorization limitations
   if( HARDFORK_CORE_973_PASSED(d.head_block_time()) )
   {
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, *asset_to_settle ),
                 "The account is not allowed to settle the asset" );
      FC_ASSERT( is_authorized_asset( d, *fee_paying_account, bitasset.options.short_backing_asset(d) ),
                 "The account is not allowed to receive the backing asset" );
   }

   bitasset_ptr = &bitasset;

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

static optional<asset> pay_collateral_fees( database& d,
                                            const asset& pays,
                                            const asset& settled_amount,
                                            const asset_object& asset_to_settle,
                                            const asset_bitasset_data_object& bitasset )
{
   const auto& head_time = d.head_block_time();
   bool after_core_hardfork_2591 = HARDFORK_CORE_2591_PASSED( head_time ); // Tighter peg (fill settlement at MCOP)
   if( after_core_hardfork_2591 && !bitasset.is_prediction_market
         && !bitasset.current_feed.settlement_price.is_null() )
   {
      price fill_price = bitasset.get_margin_call_order_price();
      try
      {
         asset settled_amount_by_mcop = pays.multiply_and_round_up( fill_price ); // Throws fc::exception if overflow
         if( settled_amount_by_mcop < settled_amount )
         {
            asset collateral_fees = settled_amount - settled_amount_by_mcop;
            asset_to_settle.accumulate_fee( d, collateral_fees );
            return collateral_fees;
         }
      } FC_CAPTURE_AND_LOG( (pays)(settled_amount)(fill_price) ) // Catch and log the exception // GCOVR_EXCL_LINE
   }
   return optional<asset>();
}

static extendable_operation_result pay_settle_from_gs_fund( database& d,
                                                 const asset_settle_evaluator::operation_type& op,
                                                 const account_object* fee_paying_account,
                                                 const asset_object& asset_to_settle,
                                                 const asset_bitasset_data_object& bitasset )
{
   const auto& head_time = d.head_block_time();
   const auto& maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   const auto& mia_dyn = asset_to_settle.dynamic_asset_data_id(d);

   asset settled_amount = ( op.amount.amount == mia_dyn.current_supply )
                          ? asset( bitasset.settlement_fund, bitasset.options.short_backing_asset )
                          : ( op.amount * bitasset.settlement_price ); // round down, favors global settlement fund
   if( op.amount.amount != mia_dyn.current_supply )
   {
      // should be strictly < except for PM with zero outcome since in that case bitasset.settlement_fund is zero
      FC_ASSERT( settled_amount.amount <= bitasset.settlement_fund,
                 "Internal error: amount in the global settlement fund is not sufficient to pay the settlement" );
   }

   if( 0 == settled_amount.amount && !bitasset.is_prediction_market && maint_time > HARDFORK_CORE_184_TIME )
      FC_THROW( "Settle amount is too small to receive anything due to rounding" );
      // else do nothing. Before the hf, something for nothing issue (#184, variant F) could occur

   asset pays = op.amount;
   if( op.amount.amount != mia_dyn.current_supply
         && settled_amount.amount != 0
         && maint_time > HARDFORK_CORE_342_TIME )
   {
      pays = settled_amount.multiply_and_round_up( bitasset.settlement_price );
   }

   d.adjust_balance( op.account, -pays );

   asset issuer_fees( 0, bitasset.options.short_backing_asset );
   optional<asset> collateral_fees;

   if( settled_amount.amount > 0 )
   {
      d.modify( bitasset, [&settled_amount]( asset_bitasset_data_object& obj ){
         obj.settlement_fund -= settled_amount.amount;
      });

      // Calculate and pay collateral fees after HF core-2591
      collateral_fees = pay_collateral_fees( d, pays, settled_amount, asset_to_settle, bitasset );
      if( collateral_fees.valid() )
         settled_amount -= *collateral_fees;

      // The account who settles pays market fees to the issuer of the collateral asset after HF core-1780
      //
      // TODO Check whether the HF check can be removed after the HF.
      //      Note: even if logically it can be removed, perhaps the removal will lead to a small
      //            performance loss. Needs testing.
      if( head_time >= HARDFORK_CORE_1780_TIME )
      {
         issuer_fees = d.pay_market_fees( fee_paying_account, settled_amount.asset_id(d), settled_amount, false );
         settled_amount -= issuer_fees;
      }

      if( settled_amount.amount > 0 )
         d.adjust_balance( op.account, settled_amount );
   }

   d.modify( mia_dyn, [&pays]( asset_dynamic_data_object& obj ){
      obj.current_supply -= pays.amount;
   });
   // Note: we don't revive the asset here if current_supply become zero, but only do it on a new feed

   extendable_operation_result result;

   result.value.paid = vector<asset>({ pays });
   result.value.received = vector<asset>({ settled_amount });
   result.value.fees = collateral_fees.valid() ? vector<asset>({ *collateral_fees, issuer_fees })
                                               : vector<asset>({ issuer_fees });

   return result;
}

static extendable_operation_result pay_settle_from_individual_pool( database& d,
                                                 const asset_settle_evaluator::operation_type& op,
                                                 const account_object* fee_paying_account,
                                                 const asset_object& asset_to_settle,
                                                 const asset_bitasset_data_object& bitasset )
{
   asset pays( bitasset.individual_settlement_debt, bitasset.asset_id );
   asset settled_amount( bitasset.individual_settlement_fund, bitasset.options.short_backing_asset );
   if( op.amount.amount < bitasset.individual_settlement_debt )
   {
      auto settlement_price = bitasset.get_individual_settlement_price();
      settled_amount = op.amount * settlement_price; // round down, in favor of settlement fund
      FC_ASSERT( settled_amount.amount > 0, "Settle amount is too small to receive anything due to rounding" );
      pays = settled_amount.multiply_and_round_up( settlement_price );
   }

   d.adjust_balance( op.account, -pays );
   d.modify( bitasset, [&pays,&settled_amount]( asset_bitasset_data_object& obj ){
      obj.individual_settlement_debt -= pays.amount;
      obj.individual_settlement_fund -= settled_amount.amount;
   });
   d.modify( asset_to_settle.dynamic_asset_data_id(d), [&pays]( asset_dynamic_data_object& obj ){
      obj.current_supply -= pays.amount;
   });

   // Calculate and pay collateral fees after HF core-2591
   optional<asset> collateral_fees = pay_collateral_fees( d, pays, settled_amount, asset_to_settle, bitasset );
   if( collateral_fees.valid() )
      settled_amount -= *collateral_fees;

   auto issuer_fees = d.pay_market_fees( fee_paying_account, settled_amount.asset_id(d), settled_amount, false );
   settled_amount -= issuer_fees;

   if( settled_amount.amount > 0 )
      d.adjust_balance( op.account, settled_amount );

   // Update current_feed since fund price changed
   auto old_feed_price = bitasset.current_feed.settlement_price;
   d.update_bitasset_current_feed( bitasset, true );

   // When current_feed is updated, it is possible that there are limit orders able to get filled,
   // so we need to call check_call_orders()
   // Note: theoretically, if the fund is still not empty, its new CR should be >= old CR,
   //       in this case, calling check_call_orders() should not change anything.
   // Note: there should be no existing force settlements
   if( 0 == bitasset.individual_settlement_debt && old_feed_price != bitasset.current_feed.settlement_price )
      d.check_call_orders( asset_to_settle, true, false, &bitasset );

   extendable_operation_result result;

   result.value.paid = vector<asset>({ pays });
   result.value.received = vector<asset>({ settled_amount });
   result.value.fees = collateral_fees.valid() ? vector<asset>({ *collateral_fees, issuer_fees })
                                               : vector<asset>({ issuer_fees });

   return result;
}

operation_result asset_settle_evaluator::do_apply(const asset_settle_evaluator::operation_type& op)
{ try {
   database& d = db();

   const auto& bitasset = *bitasset_ptr;

   // Process global settlement fund
   if( bitasset.is_globally_settled() )
      return pay_settle_from_gs_fund( d, op, fee_paying_account, *asset_to_settle, bitasset );

   // Process individual settlement pool
   extendable_operation_result result;
   asset to_settle = op.amount;
   if( bitasset.is_individually_settled_to_fund() )
   {
      result = pay_settle_from_individual_pool( d, op, fee_paying_account, *asset_to_settle, bitasset );

      // If the amount to settle is too small, or force settlement is disabled, we return
      if( bitasset.is_individually_settled_to_fund() || !asset_to_settle->can_force_settle() )
         return result;

      to_settle -= result.value.paid->front();
   }

   // Process the rest
   const auto& head_time = d.head_block_time();

   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues
   if( after_core_hardfork_2582 && 0 == to_settle.amount )
      return result;

   bool after_core_hardfork_2587 = HARDFORK_CORE_2587_PASSED( head_time );
   if( after_core_hardfork_2587 && bitasset.current_feed.settlement_price.is_null() )
      return result;

   d.adjust_balance( op.account, -to_settle );
   const auto& settle = d.create<force_settlement_object>(
         [&op,&to_settle,&head_time,&bitasset](force_settlement_object& s) {
      s.owner = op.account;
      s.balance = to_settle;
      s.settlement_date = head_time + bitasset.options.force_settlement_delay_sec;
   });

   result.value.new_objects = flat_set<object_id_type>({ settle.id });

   const auto& maint_time = d.get_dynamic_global_properties().next_maintenance_time;
   if( HARDFORK_CORE_2481_PASSED( maint_time ) )
   {
      d.apply_force_settlement( settle, bitasset, *asset_to_settle );
   }

   return result;

} FC_CAPTURE_AND_RETHROW( (op) ) } // GCOVR_EXCL_LINE

void_result asset_publish_feeds_evaluator::do_evaluate(const asset_publish_feed_operation& o)
{ try {
   const database& d = db();
   const time_point_sec now = d.head_block_time();

   // TODO remove check after hard fork
   detail::check_asset_publish_feed_extensions_hf_bsip77( now, o.extensions.value );

   const asset_object& base = o.asset_id(d);
   //Verify that this feed is for a market-issued asset and that asset is backed by the base
   FC_ASSERT( base.is_market_issued(), "Can only publish price feeds for market-issued assets" );

   const asset_bitasset_data_object& bitasset = base.bitasset_data(d);
   if( bitasset.is_prediction_market || now <= HARDFORK_CORE_216_TIME )
   {
      FC_ASSERT( !bitasset.is_globally_settled(), "No further feeds may be published after a settlement event" );
   }

   // the settlement price must be quoted in terms of the backing asset
   FC_ASSERT( o.feed.settlement_price.quote.asset_id == bitasset.options.short_backing_asset,
              "Quote asset type in settlement price should be same as backing asset of this asset" );

   if( now > HARDFORK_480_TIME )
   {
      if( !o.feed.core_exchange_rate.is_null() )
      {
         FC_ASSERT( o.feed.core_exchange_rate.quote.asset_id == asset_id_type(),
                    "Quote asset in core exchange rate should be CORE asset" );
      }
   }
   else
   {
      if( (!o.feed.settlement_price.is_null()) && (!o.feed.core_exchange_rate.is_null()) )
      {
         // Old buggy code, but we have to live with it
         FC_ASSERT( o.feed.settlement_price.quote.asset_id == o.feed.core_exchange_rate.quote.asset_id, "Bad feed" );
      }
   }

   //Verify that the publisher is authoritative to publish a feed
   if( 0 != ( base.options.flags & witness_fed_asset ) )
   {
      FC_ASSERT( d.get(GRAPHENE_WITNESS_ACCOUNT).active.account_auths.count(o.publisher) > 0,
                 "Only active witnesses are allowed to publish price feeds for this asset" );
   }
   else if( 0 != ( base.options.flags & committee_fed_asset ) )
   {
      FC_ASSERT( d.get(GRAPHENE_COMMITTEE_ACCOUNT).active.account_auths.count(o.publisher) > 0,
                 "Only active committee members are allowed to publish price feeds for this asset" );
   }
   else
   {
      FC_ASSERT( bitasset.feeds.count(o.publisher) > 0,
                 "The account is not in the set of allowed price feed producers of this asset" );
   }

   asset_ptr = &base;
   bitasset_ptr = &bitasset;

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) } // GCOVR_EXCL_LINE

void_result asset_publish_feeds_evaluator::do_apply(const asset_publish_feed_operation& o)
{ try {

   database& d = db();
   const auto head_time = d.head_block_time();
   const auto next_maint_time = d.get_dynamic_global_properties().next_maintenance_time;

   const asset_object& base = *asset_ptr;
   const asset_bitasset_data_object& bad = *bitasset_ptr;

   auto old_feed = bad.current_feed;
   auto old_median_feed = bad.median_feed;
   // Store medians for this asset
   d.modify( bad , [&o,&head_time](asset_bitasset_data_object& a) {
      a.feeds[o.publisher] = make_pair( head_time, price_feed_with_icr( o.feed,
                                                      o.extensions.value.initial_collateral_ratio ) );
   });
   d.update_bitasset_current_feed( bad );

   bool after_core_hardfork_2582 = HARDFORK_CORE_2582_PASSED( head_time ); // Price feed issues

   if( !after_core_hardfork_2582 && old_feed.margin_call_params_equal(bad.current_feed) )
      return void_result();
   if( after_core_hardfork_2582 && old_median_feed.margin_call_params_equal(bad.median_feed) )
      return void_result();

   // Feed changed, check whether need to revive the asset and proceed if need
   if( bad.is_globally_settled() // has globally settled, implies head_block_time > HARDFORK_CORE_216_TIME
       && !bad.current_feed.settlement_price.is_null() ) // has a valid feed
   {
      bool should_revive = false;
      const auto& mia_dyn = base.dynamic_asset_data_id(d);
      if( mia_dyn.current_supply == 0 ) // if current supply is zero, revive the asset
         should_revive = true;
      // if current supply is not zero, revive the asset when collateral ratio of settlement fund
      //    is greater than ( MCR if before HF core-2290, ICR if after)
      else if( next_maint_time <= HARDFORK_CORE_1270_TIME )
      {
         // before core-1270 hard fork, calculate call_price and compare to median feed
         auto fund_call_price = ~price::call_price( asset(mia_dyn.current_supply, o.asset_id),
                                    asset(bad.settlement_fund, bad.options.short_backing_asset),
                                    bad.current_feed.maintenance_collateral_ratio );
         should_revive = ( fund_call_price < bad.current_feed.settlement_price );
      }
      else
      {
         // after core-1270 hard fork, calculate collateralization and compare to maintenance_collateralization
         price fund_collateralization( asset( bad.settlement_fund, bad.options.short_backing_asset ),
                                       asset( mia_dyn.current_supply, o.asset_id ) );
         should_revive = HARDFORK_CORE_2290_PASSED( next_maint_time ) ?
                               ( fund_collateralization > bad.current_initial_collateralization )
                             : ( fund_collateralization > bad.current_maintenance_collateralization );
      }
      if( should_revive )
         d.revive_bitasset( base, bad );
   }

   // Process margin calls, allow black swan, not for a new limit order
   d.check_call_orders( base, true, false, bitasset_ptr );

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) } // GCOVR_EXCL_LINE


/***
 * @brief evaluator for asset_claim_fees operation
 *
 * Checks that we are able to claim fees denominated in asset Y (the amount_to_claim asset),
 * from some container asset X which is presumed to have accumulated the fees we wish to claim.
 * The container asset is either explicitly named in the extensions, or else assumed as the same
 * asset as the amount_to_claim asset. Evaluation fails if either (a) operation issuer is not
 * the same as the container_asset issuer, or (b) container_asset has no fee bucket for
 * amount_to_claim asset, or (c) accumulated fees are insufficient to cover amount claimed.
 */
void_result asset_claim_fees_evaluator::do_evaluate( const asset_claim_fees_operation& o )
{ try {
   const database& d = db();

   detail::check_asset_claim_fees_hardfork_87_74_collatfee(d.head_block_time(), o); // HF_REMOVABLE

   container_asset = o.extensions.value.claim_from_asset_id.valid() ?
      &(*o.extensions.value.claim_from_asset_id)(d) : &o.amount_to_claim.asset_id(d);

   FC_ASSERT( container_asset->issuer == o.issuer, "Asset fees may only be claimed by the issuer" );
   FC_ASSERT( container_asset->can_accumulate_fee(d,o.amount_to_claim),
              "Asset ${a} (${id}) is not backed by asset (${fid}) and does not hold it as fees.",
              ("a",container_asset->symbol)("id",container_asset->id)("fid",o.amount_to_claim.asset_id) );

   container_ddo = &container_asset->dynamic_asset_data_id(d);

   if (container_asset->get_id() == o.amount_to_claim.asset_id) {
      FC_ASSERT( o.amount_to_claim.amount <= container_ddo->accumulated_fees,
                 "Attempt to claim more fees than have accumulated within asset ${a} (${id}). "
                 "Asset DDO: ${ddo}. Fee claim: ${claim}.", ("a",container_asset->symbol)
                 ("id",container_asset->id)("ddo",*container_ddo)("claim",o.amount_to_claim) );
   } else {
      FC_ASSERT( o.amount_to_claim.amount <= container_ddo->accumulated_collateral_fees,
                 "Attempt to claim more backing-asset fees than have accumulated within asset ${a} (${id}) "
                 "backed by (${fid}). Asset DDO: ${ddo}. Fee claim: ${claim}.", ("a",container_asset->symbol)
                 ("id",container_asset->id)("fid",o.amount_to_claim.asset_id)("ddo",*container_ddo)
                 ("claim",o.amount_to_claim) );
      // Note: asset authorization check on (account, collateral asset) is skipped here,
      //       because it is fine to allow the funds to be moved to account balance
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE


/***
 * @brief apply asset_claim_fees operation
 */
void_result asset_claim_fees_evaluator::do_apply( const asset_claim_fees_operation& o )
{ try {
   database& d = db();

   if ( container_asset->get_id() == o.amount_to_claim.asset_id ) {
      d.modify( *container_ddo, [&o]( asset_dynamic_data_object& _addo  ) {
         _addo.accumulated_fees -= o.amount_to_claim.amount;
      });
   } else {
      d.modify( *container_ddo, [&o]( asset_dynamic_data_object& _addo  ) {
         _addo.accumulated_collateral_fees -= o.amount_to_claim.amount;
      });
   }

   d.adjust_balance( o.issuer, o.amount_to_claim );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE


void_result asset_claim_pool_evaluator::do_evaluate( const asset_claim_pool_operation& o )
{ try {
    FC_ASSERT( o.asset_id(db()).issuer == o.issuer, "Asset fee pool may only be claimed by the issuer" );

    return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE

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
} FC_CAPTURE_AND_RETHROW( (o) ) } // GCOVR_EXCL_LINE


} } // graphene::chain
