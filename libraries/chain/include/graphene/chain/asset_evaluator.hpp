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

   class asset_create_evaluator : public evaluator<asset_create_evaluator>
   {
      public:
         typedef asset_create_operation operation_type;

         object_id_type do_evaluate( const asset_create_operation& o );
         object_id_type do_apply( const asset_create_operation& o );
   };

   class asset_issue_evaluator : public evaluator<asset_issue_evaluator>
   {
      public:
         typedef asset_issue_operation operation_type;
         object_id_type do_evaluate( const asset_issue_operation& o );
         object_id_type do_apply( const asset_issue_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            to_account = nullptr;
   };

   class asset_burn_evaluator : public evaluator<asset_burn_evaluator>
   {
      public:
         typedef asset_burn_operation operation_type;
         object_id_type do_evaluate( const asset_burn_operation& o );
         object_id_type do_apply( const asset_burn_operation& o );

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
         const account_object*            from_account = nullptr;
   };


   class asset_update_evaluator : public evaluator<asset_update_evaluator>
   {
      public:
         typedef asset_update_operation operation_type;

         object_id_type do_evaluate( const asset_update_operation& o );
         object_id_type do_apply( const asset_update_operation& o );

         const asset_object* asset_to_update = nullptr;
   };

   class asset_update_bitasset_evaluator : public evaluator<asset_update_bitasset_evaluator>
   {
      public:
         typedef asset_update_bitasset_operation operation_type;

         object_id_type do_evaluate( const asset_update_bitasset_operation& o );
         object_id_type do_apply( const asset_update_bitasset_operation& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_update_feed_producers_evaluator : public evaluator<asset_update_feed_producers_evaluator>
   {
      public:
         typedef asset_update_feed_producers_operation operation_type;

         object_id_type do_evaluate( const operation_type& o );
         object_id_type do_apply( const operation_type& o );

         const asset_bitasset_data_object* bitasset_to_update = nullptr;
   };

   class asset_fund_fee_pool_evaluator : public evaluator<asset_fund_fee_pool_evaluator>
   {
      public:
         typedef asset_fund_fee_pool_operation operation_type;

         object_id_type do_evaluate(const asset_fund_fee_pool_operation& op);
         object_id_type do_apply(const asset_fund_fee_pool_operation& op);

         const asset_dynamic_data_object* asset_dyn_data = nullptr;
   };

   class asset_global_settle_evaluator : public evaluator<asset_global_settle_evaluator>
   {
      public:
         typedef asset_global_settle_operation operation_type;

         object_id_type do_evaluate(const operation_type& op);
         object_id_type do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };
   class asset_settle_evaluator : public evaluator<asset_settle_evaluator>
   {
      public:
         typedef asset_settle_operation operation_type;

         object_id_type do_evaluate(const operation_type& op);
         object_id_type do_apply(const operation_type& op);

         const asset_object* asset_to_settle = nullptr;
   };

   class asset_publish_feeds_evaluator : public evaluator<asset_publish_feeds_evaluator>
   {
      public:
         typedef asset_publish_feed_operation operation_type;

         object_id_type do_evaluate( const asset_publish_feed_operation& o );
         object_id_type do_apply( const asset_publish_feed_operation& o );

         std::map<std::pair<asset_id_type,asset_id_type>,price_feed> median_feed_values;
   };

} } // graphene::chain
