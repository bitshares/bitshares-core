/*
 * Copyright (c) 2020 Abit More, and contributors.
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
#include <graphene/chain/asset_object.hpp>
#include <graphene/chain/liquidity_pool_object.hpp>

#include <graphene/chain/liquidity_pool_evaluator.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/exceptions.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/chain/is_authorized_asset.hpp>

#include <graphene/protocol/liquidity_pool.hpp>

namespace graphene { namespace chain {

void_result liquidity_pool_create_evaluator::do_evaluate(const liquidity_pool_create_operation& op)
{ try {
   const database& d = db();
   const auto block_time = d.head_block_time();

   FC_ASSERT( HARDFORK_LIQUIDITY_POOL_PASSED(block_time), "Not allowed until the LP hardfork" );

   op.asset_a(d); // Make sure it exists
   op.asset_b(d); // Make sure it exists
   _share_asset = &op.share_asset(d);

   FC_ASSERT( _share_asset->issuer == op.account,
              "Only the asset owner can set an asset as the share asset of a liquidity pool" );

   FC_ASSERT( !_share_asset->is_market_issued(),
              "Can not specify a market-issued asset as the share asset of a liquidity pool" );

   FC_ASSERT( !_share_asset->is_liquidity_pool_share_asset(),
              "The share asset is already bound to another liquidity pool" );

   FC_ASSERT( _share_asset->dynamic_data(d).current_supply == 0,
              "Current supply of the share asset needs to be zero" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_operation_result liquidity_pool_create_evaluator::do_apply(const liquidity_pool_create_operation& op)
{ try {
   database& d = db();
   generic_operation_result result;

   const auto& new_liquidity_pool_object = d.create<liquidity_pool_object>([&op](liquidity_pool_object& obj){
      obj.asset_a = op.asset_a;
      obj.asset_b = op.asset_b;
      obj.share_asset = op.share_asset;
      obj.taker_fee_percent = op.taker_fee_percent;
      obj.withdrawal_fee_percent = op.withdrawal_fee_percent;
   });
   result.new_objects.insert( new_liquidity_pool_object.id );

   result.updated_objects.insert( _share_asset->id );
   d.modify( *_share_asset, [&new_liquidity_pool_object](asset_object& ao) {
      ao.for_liquidity_pool = new_liquidity_pool_object.id;
   });

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result liquidity_pool_delete_evaluator::do_evaluate(const liquidity_pool_delete_operation& op)
{ try {
   const database& d = db();

   _pool = &op.pool(d);

   FC_ASSERT( _pool->balance_a == 0 && _pool->balance_b == 0, "Can not delete a non-empty pool" );

   _share_asset = &_pool->share_asset(d);

   FC_ASSERT( _share_asset->issuer == op.account, "The account is not the owner of the liquidity pool" );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_operation_result liquidity_pool_delete_evaluator::do_apply(const liquidity_pool_delete_operation& op)
{ try {
   database& d = db();
   generic_operation_result result;

   result.updated_objects.insert( _share_asset->id );
   d.modify( *_share_asset, [](asset_object& ao) {
      ao.for_liquidity_pool.reset();
   });

   result.removed_objects.insert( _pool->id );
   d.remove( *_pool );

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result liquidity_pool_deposit_evaluator::do_evaluate(const liquidity_pool_deposit_operation& op)
{ try {
   const database& d = db();

   _pool = &op.pool(d);

   FC_ASSERT( op.amount_a.asset_id == _pool->asset_a, "Asset type A mismatch" );
   FC_ASSERT( op.amount_b.asset_id == _pool->asset_b, "Asset type B mismatch" );

   FC_ASSERT( (_pool->balance_a == 0) == (_pool->balance_b == 0), "Internal error" );

   const asset_object& share_asset_obj = _pool->share_asset(d);

   FC_ASSERT( share_asset_obj.can_create_new_supply(), "Can not create new supply for the share asset" );

   if( _pool->balance_a == 0 ) // which implies that _pool->balance_b == 0
   {
      FC_ASSERT( share_asset_obj.issuer == op.account, "The initial deposit can only be done by the pool owner" );
   }

   _share_asset_dyn_data = &share_asset_obj.dynamic_data(d);

   FC_ASSERT( (_pool->balance_a == 0) == (_share_asset_dyn_data->current_supply == 0), "Internal error" );

   FC_ASSERT( _share_asset_dyn_data->current_supply < share_asset_obj.options.max_supply,
              "Can not create new supply for the share asset" );

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, share_asset_obj ),
              "The account is unauthorized by the share asset" );
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _pool->asset_a(d) ),
              "The account is unauthorized by asset A" );
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _pool->asset_b(d) ),
              "The account is unauthorized by asset B" );

   if( _pool->balance_a == 0 )
   {
      share_type share_amount = std::max( op.amount_a.amount.value, op.amount_b.amount.value );
      FC_ASSERT( share_amount <= share_asset_obj.options.max_supply,
                 "For initial deposit, each amount of the two assets in the pool should not be greater than "
                 "the maximum supply of the share asset" );
      _pool_receives_a = op.amount_a;
      _pool_receives_b = op.amount_b;
      _account_receives = asset( share_amount, _pool->share_asset );
   }
   else
   {
      share_type max_new_supply = share_asset_obj.options.max_supply - _share_asset_dyn_data->current_supply;
      fc::uint128_t max128( max_new_supply.value );
      fc::uint128_t supply128( _share_asset_dyn_data->current_supply.value );
      fc::uint128_t new_supply_if_a = supply128 * op.amount_a.amount.value / _pool->balance_a.value;
      fc::uint128_t new_supply_if_b = supply128 * op.amount_b.amount.value / _pool->balance_b.value;
      fc::uint128_t new_supply = std::min( { new_supply_if_a, new_supply_if_b, max128 } );

      FC_ASSERT( new_supply > 0, "Aborting due to zero outcome" );

      fc::uint128_t a128 = ( new_supply * _pool->balance_a.value + supply128 - 1 ) / supply128; // round up
      FC_ASSERT( a128 <= fc::uint128_t( op.amount_a.amount.value ), "Internal error" );
      _pool_receives_a = asset( static_cast<int64_t>( a128 ), _pool->asset_a );

      fc::uint128_t b128 = ( new_supply * _pool->balance_b.value + supply128 - 1 ) / supply128; // round up
      FC_ASSERT( b128 <= fc::uint128_t( op.amount_b.amount.value ), "Internal error" );
      _pool_receives_b = asset( static_cast<int64_t>( b128 ), _pool->asset_b );

      _account_receives = asset( static_cast<int64_t>( new_supply ), _pool->share_asset );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_exchange_operation_result liquidity_pool_deposit_evaluator::do_apply(
      const liquidity_pool_deposit_operation& op)
{ try {
   database& d = db();
   generic_exchange_operation_result result;

   d.adjust_balance( op.account, -_pool_receives_a );
   d.adjust_balance( op.account, -_pool_receives_b );
   d.adjust_balance( op.account, _account_receives );

   d.modify( *_pool, [this]( liquidity_pool_object& lpo ){
      lpo.balance_a += _pool_receives_a.amount;
      lpo.balance_b += _pool_receives_b.amount;
      lpo.update_virtual_value();
   });

   d.modify( *_share_asset_dyn_data, [this]( asset_dynamic_data_object& data ){
      data.current_supply += _account_receives.amount;
   });

   FC_ASSERT( _pool->balance_a > 0 && _pool->balance_b > 0, "Internal error" );
   FC_ASSERT( _share_asset_dyn_data->current_supply > 0, "Internal error" );

   result.paid.emplace_back( _pool_receives_a );
   result.paid.emplace_back( _pool_receives_b );
   result.received.emplace_back( _account_receives );

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result liquidity_pool_withdraw_evaluator::do_evaluate(const liquidity_pool_withdraw_operation& op)
{ try {
   const database& d = db();

   _pool = &op.pool(d);

   FC_ASSERT( op.share_amount.asset_id == _pool->share_asset, "Share asset type mismatch" );

   FC_ASSERT( _pool->balance_a > 0 && _pool->balance_b > 0, "The pool has not been initialized" );

   const asset_object& share_asset_obj = _pool->share_asset(d);

   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, share_asset_obj ),
              "The account is unauthorized by the share asset" );
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _pool->asset_a(d) ),
              "The account is unauthorized by asset A" );
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, _pool->asset_b(d) ),
              "The account is unauthorized by asset B" );

   _share_asset_dyn_data = &share_asset_obj.dynamic_data(d);

   FC_ASSERT( _share_asset_dyn_data->current_supply >= op.share_amount.amount,
              "Can not withdraw an amount that is more than the current supply" );

   if( _share_asset_dyn_data->current_supply == op.share_amount.amount )
   {
      _pool_pays_a = asset( _pool->balance_a, _pool->asset_a );
      _pool_pays_b = asset( _pool->balance_b, _pool->asset_b );
      _fee_a = asset( 0, _pool->asset_a );
      _fee_b = asset( 0, _pool->asset_b );
   }
   else
   {
      fc::uint128_t share128( op.share_amount.amount.value );
      fc::uint128_t a128 = share128 * _pool->balance_a.value / _share_asset_dyn_data->current_supply.value;
      FC_ASSERT( a128 < fc::uint128_t( _pool->balance_a.value ), "Internal error" );
      fc::uint128_t fee_a = a128 * _pool->withdrawal_fee_percent / GRAPHENE_100_PERCENT;
      FC_ASSERT( fee_a <= a128, "Withdrawal fee percent of the pool is too high" );
      a128 -= fee_a;
      fc::uint128_t b128 = share128 * _pool->balance_b.value / _share_asset_dyn_data->current_supply.value;
      FC_ASSERT( b128 < fc::uint128_t( _pool->balance_b.value ), "Internal error" );
      fc::uint128_t fee_b = b128 * _pool->withdrawal_fee_percent / GRAPHENE_100_PERCENT;
      FC_ASSERT( fee_b <= b128, "Withdrawal fee percent of the pool is too high" );
      b128 -= fee_b;
      FC_ASSERT( a128 > 0 || b128 > 0, "Aborting due to zero outcome" );
      _pool_pays_a = asset( static_cast<int64_t>( a128 ), _pool->asset_a );
      _pool_pays_b = asset( static_cast<int64_t>( b128 ), _pool->asset_b );
      _fee_a = asset( static_cast<int64_t>( fee_a ), _pool->asset_a );
      _fee_b = asset( static_cast<int64_t>( fee_b ), _pool->asset_b );
   }

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_exchange_operation_result liquidity_pool_withdraw_evaluator::do_apply(
      const liquidity_pool_withdraw_operation& op)
{ try {
   database& d = db();
   generic_exchange_operation_result result;

   d.adjust_balance( op.account, -op.share_amount );

   if( _pool_pays_a.amount > 0 )
      d.adjust_balance( op.account, _pool_pays_a );
   if( _pool_pays_b.amount > 0 )
      d.adjust_balance( op.account, _pool_pays_b );

   d.modify( *_share_asset_dyn_data, [&op]( asset_dynamic_data_object& data ){
      data.current_supply -= op.share_amount.amount;
   });

   d.modify( *_pool, [this]( liquidity_pool_object& lpo ){
      lpo.balance_a -= _pool_pays_a.amount;
      lpo.balance_b -= _pool_pays_b.amount;
      lpo.update_virtual_value();
   });

   FC_ASSERT( (_pool->balance_a == 0) == (_pool->balance_b == 0), "Internal error" );
   FC_ASSERT( (_pool->balance_a == 0) == (_share_asset_dyn_data->current_supply == 0), "Internal error" );

   result.paid.emplace_back( op.share_amount );
   result.received.emplace_back( _pool_pays_a );
   result.received.emplace_back( _pool_pays_b );
   result.fees.emplace_back( _fee_a );
   result.fees.emplace_back( _fee_b );

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

void_result liquidity_pool_exchange_evaluator::do_evaluate(const liquidity_pool_exchange_operation& op)
{ try {
   const database& d = db();

   _pool = &op.pool(d);

   FC_ASSERT( _pool->balance_a > 0 && _pool->balance_b > 0, "The pool has not been initialized" );

   FC_ASSERT(    ( op.amount_to_sell.asset_id == _pool->asset_a && op.min_to_receive.asset_id == _pool->asset_b )
              || ( op.amount_to_sell.asset_id == _pool->asset_b && op.min_to_receive.asset_id == _pool->asset_a ),
              "Asset type mismatch" );

   const asset_object& asset_obj_a = _pool->asset_a(d);
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, asset_obj_a ),
              "The account is unauthorized by asset A" );

   const asset_object& asset_obj_b = _pool->asset_b(d);
   FC_ASSERT( is_authorized_asset( d, *fee_paying_account, asset_obj_b ),
              "The account is unauthorized by asset B" );

   _pool_receives_asset = ( op.amount_to_sell.asset_id == _pool->asset_a ? &asset_obj_a : &asset_obj_b );

   _maker_market_fee = d.calculate_market_fee( *_pool_receives_asset, op.amount_to_sell, true );
   FC_ASSERT( _maker_market_fee < op.amount_to_sell,
              "Aborting since the maker market fee of the selling asset is too high" );
   _pool_receives = op.amount_to_sell - _maker_market_fee;

   fc::uint128_t delta;
   if( op.amount_to_sell.asset_id == _pool->asset_a )
   {
      share_type new_balance_a = _pool->balance_a + _pool_receives.amount;
      // round up
      fc::uint128_t new_balance_b = ( _pool->virtual_value + new_balance_a.value - 1 ) / new_balance_a.value;
      FC_ASSERT( new_balance_b <= _pool->balance_b, "Internal error" );
      delta = fc::uint128_t( _pool->balance_b.value ) - new_balance_b;
      _pool_pays_asset = &asset_obj_b;
   }
   else
   {
      share_type new_balance_b = _pool->balance_b + _pool_receives.amount;
      // round up
      fc::uint128_t new_balance_a = ( _pool->virtual_value + new_balance_b.value - 1 ) / new_balance_b.value;
      FC_ASSERT( new_balance_a <= _pool->balance_a, "Internal error" );
      delta = fc::uint128_t( _pool->balance_a.value ) - new_balance_a;
      _pool_pays_asset = &asset_obj_a;
   }

   fc::uint128_t pool_taker_fee = delta * _pool->taker_fee_percent / GRAPHENE_100_PERCENT;
   FC_ASSERT( pool_taker_fee <= delta, "Taker fee percent of the pool is too high" );

   _pool_pays = asset( static_cast<int64_t>( delta - pool_taker_fee ), op.min_to_receive.asset_id );

   _taker_market_fee = d.calculate_market_fee( *_pool_pays_asset, _pool_pays, false );
   FC_ASSERT( _taker_market_fee <= _pool_pays, "Market fee should not be greater than the amount to receive" );
   _account_receives = _pool_pays - _taker_market_fee;

   FC_ASSERT( _account_receives.amount >= op.min_to_receive.amount, "Unable to exchange at expected price" );

   _pool_taker_fee = asset( static_cast<int64_t>( pool_taker_fee ), op.min_to_receive.asset_id );

   return void_result();
} FC_CAPTURE_AND_RETHROW( (op) ) }

generic_exchange_operation_result liquidity_pool_exchange_evaluator::do_apply(
      const liquidity_pool_exchange_operation& op)
{ try {
   database& d = db();
   generic_exchange_operation_result result;

   d.adjust_balance( op.account, -op.amount_to_sell );
   d.adjust_balance( op.account, _account_receives );

   // TODO whose registrar and referrer should receive the shared maker market fee?
   d.pay_market_fees( &_pool->share_asset(d).issuer(d), *_pool_receives_asset, op.amount_to_sell, true,
                      _maker_market_fee );
   d.pay_market_fees( fee_paying_account, *_pool_pays_asset, _pool_pays, false, _taker_market_fee );

   const auto old_virtual_value = _pool->virtual_value;
   if( op.amount_to_sell.asset_id == _pool->asset_a )
   {
      d.modify( *_pool, [this]( liquidity_pool_object& lpo ){
         lpo.balance_a += _pool_receives.amount;
         lpo.balance_b -= _pool_pays.amount;
         lpo.update_virtual_value();
      });
   }
   else
   {
      d.modify( *_pool, [this]( liquidity_pool_object& lpo ){
         lpo.balance_b += _pool_receives.amount;
         lpo.balance_a -= _pool_pays.amount;
         lpo.update_virtual_value();
      });
   }

   FC_ASSERT( _pool->balance_a > 0 && _pool->balance_b > 0, "Internal error" );
   FC_ASSERT( _pool->virtual_value >= old_virtual_value, "Internal error" );

   result.paid.emplace_back( op.amount_to_sell );
   result.received.emplace_back( _account_receives );
   result.fees.emplace_back( _maker_market_fee );
   result.fees.emplace_back( _taker_market_fee );
   result.fees.emplace_back( _pool_taker_fee );

   return result;
} FC_CAPTURE_AND_RETHROW( (op) ) }

} } // graphene::chain
