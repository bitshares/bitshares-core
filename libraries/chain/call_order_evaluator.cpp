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
#include <graphene/chain/call_order_evaluator.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/call_order_object.hpp>
#include <graphene/chain/limit_order_object.hpp>
#include <fc/uint128.hpp>

namespace graphene { namespace chain {

void_result call_order_update_evaluator::do_evaluate(const call_order_update_operation& o)
{ try {
   database& d = db();

   _paying_account = &o.funding_account(d);
   _debt_asset     = &o.delta_debt.asset_id(d);
   FC_ASSERT( _debt_asset->is_market_issued(), "Unable to cover ${sym} as it is not a collateralized asset.",
              ("sym", _debt_asset->symbol) );

   _bitasset_data  = &_debt_asset->bitasset_data(d);

   /// if there is a settlement for this asset, then no further margin positions may be taken and
   /// all existing margin positions should have been closed va database::globally_settle_asset
   FC_ASSERT( !_bitasset_data->has_settlement() );

   FC_ASSERT( o.delta_collateral.asset_id == _bitasset_data->options.short_backing_asset );

   if( _bitasset_data->is_prediction_market )
      FC_ASSERT( o.delta_collateral.amount == o.delta_debt.amount );
   else
      FC_ASSERT( !_bitasset_data->current_feed.settlement_price.is_null() );

   if( o.delta_debt.amount < 0 )
   {
      FC_ASSERT( d.get_balance(*_paying_account, *_debt_asset) >= o.delta_debt,
                 "Cannot cover by ${c} when payer only has ${b}",
                 ("c", o.delta_debt.amount)("b", d.get_balance(*_paying_account, *_debt_asset).amount) );
   }

   if( o.delta_collateral.amount > 0 )
   {
      FC_ASSERT( d.get_balance(*_paying_account, _bitasset_data->options.short_backing_asset(d)) >= o.delta_collateral,
                 "Cannot increase collateral by ${c} when payer only has ${b}", ("c", o.delta_collateral.amount)
                 ("b", d.get_balance(*_paying_account, o.delta_collateral.asset_id(d)).amount) );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (o) ) }


void_result call_order_update_evaluator::do_apply(const call_order_update_operation& o)
{
   database& d = db();
   //wdump( (_bitasset_data->current_feed) );

   if( o.delta_debt.amount != 0 )
   {
      d.adjust_balance( o.funding_account,  o.delta_debt        );

      // Deduct the debt paid from the total supply of the debt asset.
      d.modify(_debt_asset->dynamic_asset_data_id(d), [&](asset_dynamic_data_object& dynamic_asset) {
         dynamic_asset.current_supply += o.delta_debt.amount;
         assert(dynamic_asset.current_supply >= 0);
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


   auto& call_idx = d.get_index_type<call_order_index>().indices().get<by_account>();
   auto itr = call_idx.find( boost::make_tuple(o.funding_account, o.delta_debt.asset_id) );
   const call_order_object* call_obj = nullptr;
   if( itr == call_idx.end() )
   {
      FC_ASSERT( o.delta_collateral.amount > 0 );
      FC_ASSERT( o.delta_debt.amount > 0 );

      call_obj = &d.create<call_order_object>( [&](call_order_object& call ){
                                   call.borrower = o.funding_account;
                                   call.collateral = o.delta_collateral.amount;
                                   call.debt = o.delta_debt.amount;
                                   call.call_price = ~o.call_price;
                                   });
   }
   else
   {
      call_obj = &*itr;

      d.modify( *call_obj, [&]( call_order_object& call ){
          call.collateral += o.delta_collateral.amount;
          call.debt       += o.delta_debt.amount;
          call.call_price  = ~o.call_price;
      });
   }

   auto debt = call_obj->get_debt();
   if( debt.amount == 0 ) 
   {
      FC_ASSERT( call_obj->collateral == 0 );
      d.remove( *call_obj );
      return void_result();
   }
   auto collateral = call_obj->get_collateral();

   auto mp = _bitasset_data->current_feed.maintenance_price();

//   edump((debt)(collateral)((debt/collateral).to_real())(mp.to_real()) );
 //  edump((debt*mp));
   /// paying off the debt at the user specified call price should require
   /// less collateral than paying off the debt at the maitenance price
   auto col_at_call_price    = debt * o.call_price;
   auto col_at_min_callprice = debt * mp;
   FC_ASSERT( col_at_call_price <= col_at_min_callprice, "", ("debt*o.callprice",debt*o.call_price)("debt*mp",debt*mp) );
   FC_ASSERT( col_at_call_price <= collateral );

   //wdump( (o.call_price)(mp)(call_obj->call_price.to_real())(mp.to_real()) );
   //FC_ASSERT( call_obj->call_price <= mp );

   auto call_order_id = call_obj->id;

   //ilog( "checking call orders" );

   // check to see if the order needs to be margin called now, but don't allow black swans and require there to be 
   // limit orders available that could be used to fill the order.
   if( d.check_call_orders( *_debt_asset, false ) )
   {
       FC_ASSERT( !d.find_object( call_order_id ), "If updating the call order triggers a margin call, then it must completely cover the order" );
   }

   return void_result();
}

} } // graphene::chain
