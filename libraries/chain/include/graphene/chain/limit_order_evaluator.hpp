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
#pragma once
#include <graphene/chain/evaluator.hpp>
#include <graphene/chain/operations.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace chain {

   class limit_order_create_evaluator : public evaluator<limit_order_create_evaluator>
   {
      public:
         typedef limit_order_create_operation operation_type;

         object_id_type do_evaluate( const limit_order_create_operation& o );
         object_id_type do_apply( const limit_order_create_operation& o );

         asset calculate_market_fee( const asset_object* aobj, const asset& trade_amount );

         const limit_order_create_operation* _op            = nullptr;
         const account_object*               _seller        = nullptr;
         const asset_object*                 _sell_asset    = nullptr;
         const asset_object*                 _receive_asset = nullptr;
   };

   class limit_order_cancel_evaluator : public evaluator<limit_order_cancel_evaluator>
   {
      public:
         typedef limit_order_cancel_operation operation_type;

         asset do_evaluate( const limit_order_cancel_operation& o );
         asset do_apply( const limit_order_cancel_operation& o );

         const limit_order_object* _order;
   };

} } // graphene::chain
