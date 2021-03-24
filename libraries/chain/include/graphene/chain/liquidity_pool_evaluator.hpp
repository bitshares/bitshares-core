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
#pragma once
#include <graphene/chain/evaluator.hpp>

#include <graphene/protocol/liquidity_pool.hpp>

namespace graphene { namespace chain {

   class asset_object;
   class asset_dynamic_data_object;
   class liquidity_pool_object;

   class liquidity_pool_create_evaluator : public evaluator<liquidity_pool_create_evaluator>
   {
      public:
         typedef liquidity_pool_create_operation operation_type;

         void_result do_evaluate( const liquidity_pool_create_operation& op );
         generic_operation_result do_apply( const liquidity_pool_create_operation& op );

         const asset_object* _share_asset = nullptr;
   };

   class liquidity_pool_delete_evaluator : public evaluator<liquidity_pool_delete_evaluator>
   {
      public:
         typedef liquidity_pool_delete_operation operation_type;

         void_result do_evaluate( const liquidity_pool_delete_operation& op );
         generic_operation_result do_apply( const liquidity_pool_delete_operation& op );

         const liquidity_pool_object* _pool = nullptr;
         const asset_object* _share_asset = nullptr;
   };

   class liquidity_pool_deposit_evaluator : public evaluator<liquidity_pool_deposit_evaluator>
   {
      public:
         typedef liquidity_pool_deposit_operation operation_type;

         void_result do_evaluate( const liquidity_pool_deposit_operation& op );
         generic_exchange_operation_result do_apply( const liquidity_pool_deposit_operation& op );

         const liquidity_pool_object* _pool = nullptr;
         const asset_dynamic_data_object* _share_asset_dyn_data = nullptr;
         asset _account_receives;
         asset _pool_receives_a;
         asset _pool_receives_b;
   };

   class liquidity_pool_withdraw_evaluator : public evaluator<liquidity_pool_withdraw_evaluator>
   {
      public:
         typedef liquidity_pool_withdraw_operation operation_type;

         void_result do_evaluate( const liquidity_pool_withdraw_operation& op );
         generic_exchange_operation_result do_apply( const liquidity_pool_withdraw_operation& op );

         const liquidity_pool_object* _pool = nullptr;
         const asset_dynamic_data_object* _share_asset_dyn_data = nullptr;
         asset _pool_pays_a;
         asset _pool_pays_b;
         asset _fee_a;
         asset _fee_b;
   };

   class liquidity_pool_exchange_evaluator : public evaluator<liquidity_pool_exchange_evaluator>
   {
      public:
         typedef liquidity_pool_exchange_operation operation_type;

         void_result do_evaluate( const liquidity_pool_exchange_operation& op );
         generic_exchange_operation_result do_apply( const liquidity_pool_exchange_operation& op );

         const liquidity_pool_object* _pool = nullptr;
         const asset_object* _pool_pays_asset = nullptr;
         const asset_object* _pool_receives_asset = nullptr;
         asset _pool_pays;
         asset _pool_receives;
         asset _account_receives;
         asset _maker_market_fee;
         asset _taker_market_fee;
         asset _pool_taker_fee;
   };

} } // graphene::chain
