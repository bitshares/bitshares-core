/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <graphene/chain/database.hpp>
#include <graphene/chain/short_order_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/short_order_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

asset call_order_update_evaluator::do_evaluate(const call_order_update_operation& o)
{ try {
   database& d = db();

   _paying_account = &o.funding_account(d);

   _debt_asset = &o.amount_to_cover.asset_id(d);
   _bitasset_data = &_debt_asset->bitasset_data(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );
   FC_ASSERT( o.collateral_to_add.asset_id == _bitasset_data->options.short_backing_asset );

   if( _bitasset_data->is_prediction_market )
   {
      FC_ASSERT( o.collateral_to_add.amount == -o.amount_to_cover.amount );
      FC_ASSERT( o.maintenance_collateral_ratio == 0 );
   }
   else
   {
      FC_ASSERT( o.maintenance_collateral_ratio == 0 ||
                 o.maintenance_collateral_ratio > _bitasset_data->current_feed.required_maintenance_collateral );
   }

   if( o.amount_to_cover > 0 )
   {
      FC_ASSERT( d.get_balance(*_paying_account, *_debt_asset) >= o.amount_to_cover,
                 "Cannot cover by ${c} when payer only has ${b}",
                 ("c", o.amount_to_cover.amount)("b", d.get_balance(*_paying_account, *_debt_asset).amount) );
   }
   if( o.collateral_to_add > 0 )
   {
      FC_ASSERT( d.get_balance(*_paying_account, bitasset_data.options.short_backing_asset(d)) >= o.collateral_to_add,
                 "Cannot increase collateral by ${c} when payer only has ${b}", ("c", o.amount_to_cover.amount)
                 ("b", d.get_balance(*_paying_account, bitasset_data.options.short_backing_asset(d)).amount) );
   }

   return asset();
} FC_CAPTURE_AND_RETHROW( (o) ) }


asset call_order_update_evaluator::do_apply(const call_order_update_operation& o)
{
   database& d = db();
   asset collateral_returned = -o.collateral_to_add;

   d.adjust_balance( o.funding_account, -o.amount_to_cover);
   d.adjust_balance( o.funding_account, -o.collateral_to_add);

   // Deduct the debt paid from the total supply of the debt asset.
   if( o.amount_to_cover != 0 )
   {
      d.modify(_debt_asset->dynamic_asset_data_id(d), [&](asset_dynamic_data_object& dynamic_asset) {
         dynamic_asset.current_supply -= o.amount_to_cover.amount;
         assert(dynamic_asset.current_supply >= 0);
      });
   }

   auto& call_idx = d.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(o.funding_account, o.amount_to_cover.asset_id) );
   if( itr == call_idx.end() )
   {
      FC_ASSERT( o.collateral_to_add.amount > 0 );
      FC_ASSERT( o.amount_to_cover.amount < 0 );
      d.create<call_order_object>( [&](call_order_object& call ){
                                   call.owner = o.funding_account;
                                   call.collateral = o.collateral_to_add.amount;
                                   call.debt = -o.amount_to_cover.amount;
                                   call.maintenance_collateral_ratio = o.maintenance_collateral_ratio;
                                   // TODO: this is only necessary for non-prediction markets
                                   call.update_call_price();
                                   FC_ASSERT( call.call_price < _bitasset_data->current_feed.settlement_price );
                                   });
   }
   else
   {
      if( itr->debt - o.amount_to_cover == 0 )
      {
         FC_ASSERT( o.collateral_to_add == 0 );
         collateral_returned = itr->get_collateral();
         d.adjust_balance( o.funding_account, call.
         d.remove( *itr );
      }
      else
      {
         FC_ASSERT( (itr->debt - o.amount_to_cover.amount) >= 0 );
         FC_ASSERT( (itr->collateral + o.collateral_to_add.amount) >= 0 );

         d.modify( *itr, [&]( call_order_object& call ){
             call.collateral += o.collateral_to_add.amount;
             call.debt       -= o.amount_to_cover.amount;
             if( o.maintenance_collateral_ratio )
                call.maintenance_collateral_ratio = o.maintenance_collateral_ratio;

             // TODO: this is only necessary for non-prediction markets
             call.update_call_price();
             FC_ASSERT( call.call_price < _bitasset_data->current_feed.settlement_price );
         });
      }
   }
   if( collateral_returned.asset_id == asset_id_type() && collateral_returned.amount != 0 )
   {
      d.modify(_paying_account->statistics(d), [&](account_statistics_object& stats) {
            stats.total_core_in_orders -= collateral_returned.amount;
      });
   }
   return collateral_returned;
}

} } // graphene::chain
