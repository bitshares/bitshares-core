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
         dd.current_supply++;
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
     d.create<asset_object>( [&op,next_asset_id,&dyn_asset,bit_asset_id]( asset_object& a ) {
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
   FC_ASSERT( new_asset.id == next_asset_id, "Unexpected object database error, object id mismatch" );

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

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ){
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

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ){
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

   db().modify( *asset_dyn_data, [&o]( asset_dynamic_data_object& data ) {
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

   // For market-issued assets, if core change rate changed, update flag in bitasset data
   if( asset_to_update->is_market_issued()
          && asset_to_update->options.core_exchange_rate != o.new_options.core_exchange_rate )
   {
      const auto& bitasset = asset_to_update->bitasset_data(d);
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
void check_children_of_bitasset(database& d, const asset_update_bitasset_operation& op,
      const asset_object& new_backing_asset, bool after_hf_922_931)
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
         [after_hf_922_931, &new_backing_asset, &d, &op](const asset_bitasset_data_object& bitasset_data)
         {
            const auto& child = bitasset_data.asset_id(d);
            if ( after_hf_922_931 )
            {
               FC_ASSERT( child.get_id() != op.new_options.short_backing_asset,
                     "A BitAsset would be invalidated by changing this backing asset ('A' backed by 'B' backed by 'A')." );

               FC_ASSERT( child.issuer != GRAPHENE_COMMITTEE_ACCOUNT,
                     "A blockchain-controlled market asset would be invalidated by changing this backing asset." );

               FC_ASSERT( !new_backing_asset.is_market_issued(),
                     "A non-blockchain controlled BitAsset would be invalidated by changing this backing asset.");

            }
            else
            {
               if( child.get_id() == op.new_options.short_backing_asset )
               {
                  wlog( "Before hf-922-931, modified an asset to be backed by another, but would cause a continuous "
                        "loop. A cannot be backed by B which is backed by A." );
                  return;
               }

               if( child.issuer == GRAPHENE_COMMITTEE_ACCOUNT )
               {
                  wlog( "before hf-922-931, modified an asset to be backed by a non-CORE, but this asset "
                        "is a backing asset for a committee-issued asset. This occurred at block ${b}",
                        ("b", d.head_block_num()));
                  return;
               }
               else
               {
                  if ( new_backing_asset.is_market_issued() ) // a.k.a. !UIA
                  {
                     wlog( "before hf-922-931, modified an asset to be backed by an MPA, but this asset "
                           "is a backing asset for another MPA, which would cause MPA backed by MPA backed by MPA. "
                           "This occurred at block ${b}",
                           ("b", d.head_block_num()));
                     return;
                  }
               } // if child.issuer
            } // if hf 922/931
         } ); // end of lambda and std::for_each()
} // check_children_of_bitasset

void_result asset_update_bitasset_evaluator::do_evaluate(const asset_update_bitasset_operation& op)
{ try {
   database& d = db();

   const asset_object& asset_obj = op.asset_to_update(d);

   FC_ASSERT( asset_obj.is_market_issued(), "Cannot update BitAsset-specific settings on a non-BitAsset." );

   FC_ASSERT( op.issuer == asset_obj.issuer, "Only asset issuer can update bitasset_data of the asset." );

   const asset_bitasset_data_object& current_bitasset_data = asset_obj.bitasset_data(d);

   FC_ASSERT( !current_bitasset_data.has_settlement(), "Cannot update a bitasset after a global settlement has executed" );

   bool after_hf_core_922_931 = ( d.get_dynamic_global_properties().next_maintenance_time > HARDFORK_CORE_922_931_TIME );

   // Are we changing the backing asset?
   if( op.new_options.short_backing_asset != current_bitasset_data.options.short_backing_asset )
   {
      FC_ASSERT( asset_obj.dynamic_asset_data_id(d).current_supply == 0,
                 "Cannot update a bitasset if there is already a current supply." );

      const asset_object& new_backing_asset = op.new_options.short_backing_asset(d); // check if the asset exists

      if( after_hf_core_922_931 ) // TODO remove this check after hard fork if things in `else` did not occur
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

               check_children_of_bitasset( d, op, new_backing_asset, after_hf_core_922_931 );
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
               check_children_of_bitasset( d, op, new_backing_asset, after_hf_core_922_931 );
            }

         }

         // Check if the new backing asset is itself backed by something. It must be CORE or a UIA
         if ( new_backing_asset.is_market_issued() )
         {
            asset_id_type backing_backing_asset_id = new_backing_asset.bitasset_data(d).options.short_backing_asset;
            FC_ASSERT( (backing_backing_asset_id == asset_id_type() || !backing_backing_asset_id(d).is_market_issued()),
                  "A BitAsset cannot be backed by a BitAsset that itself is backed by a BitAsset.");
         }
      }
      else // prior to HF 922 / 931
      {
         // code to check if issues occurred before hard fork. TODO cleanup after hard fork
         if( op.new_options.short_backing_asset == asset_obj.get_id() )
         {
            wlog( "before hf-922-931, op.new_options.short_backing_asset == asset_obj.get_id() at block ${b}",
                  ("b",d.head_block_num()) );
         }
         if( current_bitasset_data.is_prediction_market && asset_obj.precision != new_backing_asset.precision )
         {
            wlog( "before hf-922-931, for a PM, asset_obj.precision != new_backing_asset.precision at block ${b}",
                  ("b",d.head_block_num()) );
         }

         if( asset_obj.issuer == GRAPHENE_COMMITTEE_ACCOUNT )
         {
            // code to check if issues occurred before hard fork. TODO cleanup after hard fork
            if( new_backing_asset.is_market_issued() )
            {
               if( new_backing_asset.bitasset_data(d).options.short_backing_asset != asset_id_type() )
                  wlog( "before hf-922-931, modified a blockchain-controlled market asset to be backed by an asset "
                        "which is not backed by CORE at block ${b}",
                        ("b",d.head_block_num()) );

               check_children_of_bitasset( d, op, new_backing_asset, after_hf_core_922_931 );
            }
            else
            {
               if( new_backing_asset.get_id() != asset_id_type() )
                  wlog( "before hf-922-931, modified a blockchain-controlled market asset to be backed by an asset "
                        "which is not market issued asset nor CORE at block ${b}",
                        ("b",d.head_block_num()) );
            }

            //prior to HF 922_931, these checks were mistakenly using the old backing_asset
            const asset_object& old_backing_asset = current_bitasset_data.options.short_backing_asset(d);

            if( old_backing_asset.is_market_issued() )
            {
               FC_ASSERT( old_backing_asset.bitasset_data(d).options.short_backing_asset == asset_id_type(),
                          "May not modify a blockchain-controlled market asset to be backed by an asset which is not "
                          "backed by CORE." );
            }
            else
            {
               FC_ASSERT( old_backing_asset.get_id() == asset_id_type(),
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
               check_children_of_bitasset( d, op, new_backing_asset, after_hf_core_922_931 );
            }
         }
         // if the new backing asset is backed by something which is not CORE and not a UIA, this is not allowed
         // Check if the new backing asset is itself backed by something. It must be CORE or a UIA
         if ( new_backing_asset.is_market_issued() )
         {
            asset_id_type backing_backing_asset_id = new_backing_asset.bitasset_data(d).options.short_backing_asset;
            if ( backing_backing_asset_id != asset_id_type() && backing_backing_asset_id(d).is_market_issued() )
            {
               wlog( "before hf-922-931, a BitAsset cannot be backed by a BitAsset that itself "
                     "is backed by a BitAsset. This occurred at block ${b}",
                     ("b", d.head_block_num() ) );
            } // not core, not UIA
         } // if market issued
      }
   }

   const auto& chain_parameters = d.get_global_properties().parameters;
   if( after_hf_core_922_931 ) // TODO remove this check after hard fork if things in `else` did not occur
   {
      FC_ASSERT( op.new_options.feed_lifetime_sec > chain_parameters.block_interval,
            "Feed lifetime must exceed block interval." );
      FC_ASSERT( op.new_options.force_settlement_delay_sec > chain_parameters.block_interval,
            "Force settlement delay must exceed block interval." );
   }
   else // code to check if issues occurred before hard fork. TODO cleanup after hard fork
   {
      if( op.new_options.feed_lifetime_sec <= chain_parameters.block_interval )
         wlog( "before hf-922-931, op.new_options.feed_lifetime_sec <= chain_parameters.block_interval at block ${b}",
               ("b",d.head_block_num()) );
      if( op.new_options.force_settlement_delay_sec <= chain_parameters.block_interval )
         wlog( "before hf-922-931, op.new_options.force_settlement_delay_sec <= chain_parameters.block_interval at block ${b}",
               ("b",d.head_block_num()) );
   }

   bitasset_to_update = &current_bitasset_data;
   asset_to_update = &asset_obj;

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

/*******
 * @brief Apply requested changes to bitasset options
 *
 * This applies the requested changes to the bitasset object. It also cleans up the
 * releated feeds
 *
 * @param op the requested operation
 * @param db the database
 * @param bdo the actual database object
 * @param asset_to_update the asset_object related to this bitasset_data_object
 * @returns true if the feed price is changed, and after hf core-868-890
 */
static bool update_bitasset_object_options(
      const asset_update_bitasset_operation& op, database& db,
      asset_bitasset_data_object& bdo, const asset_object& asset_to_update )
{
   const fc::time_point_sec& next_maint_time = db.get_dynamic_global_properties().next_maintenance_time;
   bool after_hf_core_868_890 = ( next_maint_time > HARDFORK_CORE_868_890_TIME );

   // If the minimum number of feeds to calculate a median has changed, we need to recalculate the median
   bool should_update_feeds = false;
   if( op.new_options.minimum_feeds != bdo.options.minimum_feeds )
      should_update_feeds = true;

   // after hardfork core-868-890, we also should call update_median_feeds if the feed_lifetime_sec changed
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
      if( asset_to_update.options.flags & ( witness_fed_asset | committee_fed_asset ) )
         is_witness_or_committee_fed = true;
   }

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

   if( should_update_feeds )
   {
      const auto old_feed = bdo.current_feed;
      bdo.update_median_feeds( db.head_block_time() );

      // TODO review and refactor / cleanup after hard fork:
      //      1. if hf_core_868_890 and core-935 occurred at same time
      //      2. if wlog did not actually get called

      // We need to call check_call_orders if the price feed changes after hardfork core-935
      if( next_maint_time > HARDFORK_CORE_935_TIME )
         return ( !( old_feed == bdo.current_feed ) );

      // We need to call check_call_orders if the settlement price changes after hardfork core-868-890
      if( after_hf_core_868_890 )
      {
         if( old_feed.settlement_price != bdo.current_feed.settlement_price )
            return true;
         else
         {
            if( !( old_feed == bdo.current_feed ) )
               wlog( "Settlement price did not change but current_feed changed at block ${b}", ("b",db.head_block_num()) );
         }
      }
   }

   return false;
}

void_result asset_update_bitasset_evaluator::do_apply(const asset_update_bitasset_operation& op)
{
   try
   {
      auto& db_conn = db();
      const auto& asset_being_updated = (*asset_to_update);
      bool to_check_call_orders = false;

      db_conn.modify( *bitasset_to_update,
                      [&op, &asset_being_updated, &to_check_call_orders, &db_conn]( asset_bitasset_data_object& bdo )
      {
         to_check_call_orders = update_bitasset_object_options( op, db_conn, bdo, asset_being_updated );
      });

      if( to_check_call_orders )
         // Process margin calls, allow black swan, not for a new limit order
         db_conn.check_call_orders( asset_being_updated, true, false, bitasset_to_update );

      return void_result();

   } FC_CAPTURE_AND_RETHROW( (op) )
}

void_result asset_update_feed_producers_evaluator::do_evaluate(const asset_update_feed_producers_evaluator::operation_type& o)
{ try {
   database& d = db();

   FC_ASSERT( o.new_feed_producers.size() <= d.get_global_properties().parameters.maximum_asset_feed_publishers,
              "Cannot specify more feed producers than maximum allowed" );

   const asset_object& a = o.asset_to_update(d);

   FC_ASSERT(a.is_market_issued(), "Cannot update feed producers on a non-BitAsset.");
   FC_ASSERT(!(a.options.flags & committee_fed_asset), "Cannot set feed producers on a committee-fed asset.");
   FC_ASSERT(!(a.options.flags & witness_fed_asset), "Cannot set feed producers on a witness-fed asset.");

   FC_ASSERT( a.issuer == o.issuer, "Only asset issuer can update feed producers of an asset" );

   asset_to_update = &a;

   // Make sure all producers exist. Check these after asset because account lookup is more expensive
   for( auto id : o.new_feed_producers )
      d.get_object(id);

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_update_feed_producers_evaluator::do_apply(const asset_update_feed_producers_evaluator::operation_type& o)
{ try {
   database& d = db();
   const auto head_time = d.head_block_time();
   const asset_bitasset_data_object& bitasset_to_update = asset_to_update->bitasset_data(d);
   d.modify( bitasset_to_update, [&o,head_time](asset_bitasset_data_object& a) {
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
         if( !o.new_feed_producers.count(itr->first) )
            itr = a.feeds.erase(itr);
         else
            ++itr;
      }
      //Now, add any new publishers
      for( const account_id_type acc : o.new_feed_producers )
      {
         a.feeds[acc];
      }
      a.update_median_feeds( head_time );
   });
   // Process margin calls, allow black swan, not for a new limit order
   d.check_call_orders( *asset_to_update, true, false, &bitasset_to_update );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }

void_result asset_global_settle_evaluator::do_evaluate(const asset_global_settle_evaluator::operation_type& op)
{ try {
   const database& d = db();
   asset_to_settle = &op.asset_to_settle(d);
   FC_ASSERT( asset_to_settle->is_market_issued(), "Can only globally settle market-issued assets" );
   FC_ASSERT( asset_to_settle->can_global_settle(), "The global_settle permission of this asset is disabled" );
   FC_ASSERT( asset_to_settle->issuer == op.issuer, "Only asset issuer can globally settle an asset" );
   FC_ASSERT( asset_to_settle->dynamic_data(d).current_supply > 0, "Can not globally settle an asset with zero supply" );

   const asset_bitasset_data_object& _bitasset_data  = asset_to_settle->bitasset_data(d);
   // if there is a settlement for this asset, then no further global settle may be taken
   FC_ASSERT( !_bitasset_data.has_settlement(), "This asset has settlement, cannot global settle again" );

   const auto& idx = d.get_index_type<call_order_index>().indices().get<by_collateral>();
   FC_ASSERT( !idx.empty(), "Internal error: no debt position found" );
   auto itr = idx.lower_bound( price::min( _bitasset_data.options.short_backing_asset, op.asset_to_settle ) );
   FC_ASSERT( itr != idx.end() && itr->debt_type() == op.asset_to_settle, "Internal error: no debt position found" );
   const call_order_object& least_collateralized_short = *itr;
   FC_ASSERT(least_collateralized_short.get_debt() * op.settle_price <= least_collateralized_short.get_collateral(),
             "Cannot force settle at supplied price: least collateralized short lacks sufficient collateral to settle.");

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result asset_global_settle_evaluator::do_apply(const asset_global_settle_evaluator::operation_type& op)
{ try {
   database& d = db();
   d.globally_settle_asset( *asset_to_settle, op.settle_price );
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
   FC_ASSERT( base.is_market_issued(), "Can only publish price feeds for market-issued assets" );

   const asset_bitasset_data_object& bitasset = base.bitasset_data(d);
   if( bitasset.is_prediction_market || d.head_block_time() <= HARDFORK_CORE_216_TIME )
   {
      FC_ASSERT( !bitasset.has_settlement(), "No further feeds may be published after a settlement event" );
   }

   // the settlement price must be quoted in terms of the backing asset
   FC_ASSERT( o.feed.settlement_price.quote.asset_id == bitasset.options.short_backing_asset,
              "Quote asset type in settlement price should be same as backing asset of this asset" );

   if( d.head_block_time() > HARDFORK_480_TIME )
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
   if( base.options.flags & witness_fed_asset )
   {
      FC_ASSERT( d.get(GRAPHENE_WITNESS_ACCOUNT).active.account_auths.count(o.publisher),
                 "Only active witnesses are allowed to publish price feeds for this asset" );
   }
   else if( base.options.flags & committee_fed_asset )
   {
      FC_ASSERT( d.get(GRAPHENE_COMMITTEE_ACCOUNT).active.account_auths.count(o.publisher),
                 "Only active committee members are allowed to publish price feeds for this asset" );
   }
   else
   {
      FC_ASSERT( bitasset.feeds.count(o.publisher),
                 "The account is not in the set of allowed price feed producers of this asset" );
   }

   asset_ptr = &base;
   bitasset_ptr = &bitasset;

   return void_result();
} FC_CAPTURE_AND_RETHROW((o)) }

void_result asset_publish_feeds_evaluator::do_apply(const asset_publish_feed_operation& o)
{ try {

   database& d = db();

   const asset_object& base = *asset_ptr;
   const asset_bitasset_data_object& bad = *bitasset_ptr;

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
      // Process margin calls, allow black swan, not for a new limit order
      d.check_call_orders( base, true, false, bitasset_ptr );
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
